#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

usage()
{
	cat <<'EOF'
Usage: tools/wii-fb-stress.sh [options]

Build and run a sustained RGB framebuffer workload on the Wii over SSH.

Options:
  --host HOST          Wii address (default: WII_SSH_HOST or 10.3.10.12)
  --duration SECONDS   workload duration, 0 runs until interrupted (default: 120)
  --fps RATE           requested framebuffer update rate, 1..60 (default: 30)
  --single-buffer      run the known tearing-prone control without VFB panning
  --rgb888             request a 32-bit XRGB8888 virtual framebuffer
  --direct-render      render directly into the inactive mapped VFB page
  --no-build           reuse /tmp/wii-fb-stress-$USER
  --reuse-remote       verify and reuse /tmp/wii-fb-stress on the Wii
  --allow-dirty        permit a test from an uncommitted source tree

Environment:
  WII_SSH_KEY          SSH private key (default: $HOME/.ssh/id_rsa)
  CROSS_COMPILE        target compiler prefix (default: powerpc-linux-gnu-)
EOF
}

ssh_host=${WII_SSH_HOST:-10.3.10.12}
duration=120
fps=30
build=1
allow_dirty=0
single_buffer=0
rgb888=0
direct_render=0
reuse_remote=0

while (($#)); do
	case "$1" in
	--host)
		ssh_host=$2
		shift
		;;
	--duration)
		duration=$2
		shift
		;;
	--fps)
		fps=$2
		shift
		;;
	--no-build)
		build=0
		;;
	--reuse-remote)
		reuse_remote=1
		;;
	--single-buffer)
		single_buffer=1
		;;
	--rgb888)
		rgb888=1
		;;
	--direct-render)
		direct_render=1
		;;
	--allow-dirty)
		allow_dirty=1
		;;
	-h|--help)
		usage
		exit 0
		;;
	*)
		echo "Unknown argument: $1" >&2
		usage >&2
		exit 2
		;;
	esac
	shift
done

if [[ ! $duration =~ ^[0-9]+$ ]] || ((duration > 86400)); then
	echo "Invalid duration: $duration" >&2
	exit 2
fi
if [[ ! $fps =~ ^[0-9]+$ ]] || ((fps < 1 || fps > 60)); then
	echo "Invalid fps: $fps" >&2
	exit 2
fi

repo=$(git rev-parse --show-toplevel)
cd "$repo"
if (( ! allow_dirty )) && [[ -n $(git status --porcelain --untracked-files=normal) ]]; then
	echo "Refusing to test from a dirty tree; commit first or pass --allow-dirty." >&2
	exit 1
fi

compiler=${CROSS_COMPILE:-powerpc-linux-gnu-}gcc
binary=${TMPDIR:-/tmp}/wii-fb-stress-${USER}
if ((build)); then
	"$compiler" -O2 -Wall -Wextra -Werror -static \
		-o "$binary" tools/wii-fb-stress.c
fi
if [[ ! -x $binary ]]; then
	echo "Stress binary not found: $binary" >&2
	exit 1
fi

ssh_key=${WII_SSH_KEY:-$HOME/.ssh/id_rsa}
if [[ $ssh_host == *@* ]]; then
	remote=$ssh_host
else
	remote=root@$ssh_host
fi
ssh_options=(
	-i "$ssh_key"
	-o IdentitiesOnly=yes
	-o BatchMode=yes
	-o PubkeyAcceptedAlgorithms=+ssh-rsa
	-o StrictHostKeyChecking=no
	-o UserKnownHostsFile=/dev/null
	-o ForwardX11=no
	-o RequestTTY=no
	-o ConnectTimeout=8
	-o LogLevel=ERROR
	-o ServerAliveInterval=5
	-o ServerAliveCountMax=3
)
remote_binary=/tmp/wii-fb-stress
run_id="$(git rev-parse --short=12 HEAD)-$(date +%s)-$$"
run_log=${TMPDIR:-/tmp}/wii-fb-stress-${run_id}.log
target_args="--duration $duration --fps $fps"
buffer_mode=double
pixel_format=RGB565
render_mode=staged
if ((single_buffer)); then
	target_args+=" --single-buffer"
	buffer_mode=single
fi
if ((rgb888)); then
	target_args+=" --rgb888"
	pixel_format=RGB888
fi
if ((direct_render)); then
	target_args+=" --direct-render"
	render_mode=direct
fi

remote_exec()
{
	# Commands are intentionally assembled locally for the controlled Wii shell.
	# shellcheck disable=SC2029
	ssh "${ssh_options[@]}" "$remote" "$1"
}

restore_console()
{
	remote_exec "printf '\\033[2J\\033[H=== GX STRESS COMPLETE: SSH AND CONSOLE LIVE ===\\n' > /dev/tty0" \
		>/dev/null 2>&1 || true
}
trap restore_console EXIT

binary_sha=$(sha256sum "$binary" | awk '{print $1}')
if (( reuse_remote )); then
	printf 'Reusing checksum-verified %s stress workload on %s\n' \
		"$pixel_format" "$remote"
	remote_sha=$(remote_exec "sha256sum $remote_binary 2>/dev/null | cut -d' ' -f1")
	if [[ $remote_sha != "$binary_sha" ]]; then
		printf 'Reusable remote workload mismatch: local=%s remote=%s\n' \
			"$binary_sha" "${remote_sha:-missing}" >&2
		exit 1
	fi
else
	printf 'Deploying %s stress workload to %s\n' "$pixel_format" "$remote"
	remote_exec "cat > $remote_binary.new" < "$binary"
	remote_sha=$(remote_exec "sha256sum $remote_binary.new | cut -d' ' -f1")
	if [[ $remote_sha != "$binary_sha" ]]; then
		remote_exec "rm -f $remote_binary.new"
		echo "Remote checksum mismatch: local=$binary_sha remote=$remote_sha" >&2
		exit 1
	fi
	remote_exec "chmod 755 $remote_binary.new && mv -f $remote_binary.new $remote_binary"
fi

module_state=$(remote_exec "awk '\$1 == \"gcn_gx\" { print \$1 }' /proc/modules")
if [[ $module_state != gcn_gx ]]; then
	echo "gcn_gx is not loaded; refusing to measure the CPU fallback." >&2
	exit 1
fi

irq_before=$(remote_exec "awk '/gcn-gx-pe-finish/ { print \$2 }' /proc/interrupts")
if [[ ! $irq_before =~ ^[0-9]+$ ]]; then
	echo "Unable to read gcn-gx-pe-finish interrupt counter." >&2
	exit 1
fi
dmesg_lines_before=$(remote_exec "dmesg | wc -l")
if [[ ! $dmesg_lines_before =~ ^[0-9]+$ ]]; then
	echo "Unable to record the starting kernel-log position." >&2
	exit 1
fi

begin_marker="wii-fb-stress: begin $run_id format=$pixel_format"
begin_marker+=" duration=$duration fps=$fps"
begin_marker+=" buffers=$buffer_mode sha256=$binary_sha"
remote_exec "printf '<6>%s\\n' '$begin_marker' > /dev/kmsg"
printf 'Running %s for %ss at %s fps (%s-buffered); ' \
	"$pixel_format" "$duration" "$fps" "$buffer_mode"
printf 'render=%s; watch for smooth moving bars and intact grid lines.\n' \
	"$render_mode"
set +e
remote_exec "$remote_binary $target_args" | tee "$run_log"
workload_status=${PIPESTATUS[0]}
set -e

achieved_fps=$(awk '
	/WII_FB_STRESS_DONE/ {
		for (field = 1; field <= NF; field++) {
			if ($field ~ /^fps=/) {
				sub(/^fps=/, "", $field)
				fps = $field
			}
		}
	}
	END { print fps }
' "$run_log")

irq_after=$(remote_exec "awk '/gcn-gx-pe-finish/ { print \$2 }' /proc/interrupts")
irq_delta=$((irq_after - irq_before))
if ((duration > 0)); then
	irq_rate=$(awk -v delta="$irq_delta" -v seconds="$duration" \
		'BEGIN { printf "%.2f", delta / seconds }')
else
	irq_rate=n/a
fi
remote_exec "printf '<6>wii-fb-stress: end $run_id status=$workload_status irq_delta=$irq_delta\\n' > /dev/kmsg"

printf '\n%s stress result\n' "$pixel_format"
printf '  commit:       %s\n' "$(git rev-parse --short=12 HEAD)"
printf '  binary sha:   %s\n' "$binary_sha"
printf '  exit status:  %s\n' "$workload_status"
printf '  VFB mode:     %s-buffered\n' "$buffer_mode"
printf '  pixel format: %s\n' "$pixel_format"
printf '  render mode:  %s\n' "$render_mode"
printf '  source rate:  %s fps requested, %s fps achieved\n' \
	"$fps" "${achieved_fps:-unknown}"
printf '  PE finish IRQ: %s -> %s (delta %s, %s/s)\n' \
	"$irq_before" "$irq_after" "$irq_delta" "$irq_rate"
printf '  target state:\n'
remote_exec "grep '^gcn_gx ' /proc/modules; ip -4 -o addr show dev wlan0; uptime"

printf '  kernel messages since test start:\n'
remote_exec "dmesg | tail -n +$((dmesg_lines_before + 1))"

if ((workload_status != 0)); then
	exit "$workload_status"
fi
if ((irq_delta <= 0)); then
	echo "GX finish IRQ did not advance during the workload." >&2
	exit 1
fi
if [[ ! $achieved_fps =~ ^[0-9]+([.][0-9]+)?$ ]] ||
   ! awk -v actual="$achieved_fps" -v requested="$fps" \
	'BEGIN { exit !(actual >= requested * 0.90) }'; then
	echo "Source workload did not sustain 90% of its requested frame rate." >&2
	exit 1
fi

#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

usage()
{
	cat <<'EOF'
Usage: tools/wii-gx-cycle.sh [options]

Build and live-reload gcn-gx.ko on the Wii over SSH.

Options:
  --host HOST             Wii address (default: WII_SSH_HOST or 10.3.10.12)
  --renderer MODE         generated, reference, or direct (default: generated)
  --texture-source MODE   console, pattern, or probe (default: console)
  --probe-seed N          seed for the deterministic probe (default: 0)
  --texcoord-source MODE  position or direct (default: position)
  --texcoord-mapping MODE affine or constant (default: affine)
  --direct-primitive MODE quad or triangle (default: quad)
  --direct-pattern MODE   grid or vstripes (default: grid)
  --texcoord-space MODE   normalized or texel (default: normalized)
  --texel-bias-eighths N  signed texture phase in eighths (default: -2)
  --hold-frame N          publish frame N once, then hold (default: 0)
  --unload                unload GX and leave the CPU console active
  --no-build              reuse the existing gcn-gx.ko
  --allow-dirty           permit loading from an uncommitted source tree

Environment:
  JOBS                    parallel build jobs (default: 16)
  WII_SSH_KEY             SSH private key (default: $HOME/.ssh/id_rsa)
EOF
}

ssh_host=${WII_SSH_HOST:-10.3.10.12}
renderer=generated
texture_source=console
probe_seed=0
texcoord_source=position
texcoord_mapping=affine
direct_primitive=quad
direct_pattern=grid
texcoord_space=normalized
texel_bias_eighths=-2
hold_frame=0
unload_only=0
build=1
allow_dirty=0

while (($#)); do
	case "$1" in
	--host)
		ssh_host=$2
		shift
		;;
	--renderer)
		renderer=$2
		shift
		;;
	--texture-source)
		texture_source=$2
		shift
		;;
	--probe-seed)
		probe_seed=$2
		shift
		;;
	--texcoord-source)
		texcoord_source=$2
		shift
		;;
	--texcoord-mapping)
		texcoord_mapping=$2
		shift
		;;
	--direct-primitive)
		direct_primitive=$2
		shift
		;;
	--direct-pattern)
		direct_pattern=$2
		shift
		;;
	--texcoord-space)
		texcoord_space=$2
		shift
		;;
	--texel-bias-eighths)
		texel_bias_eighths=$2
		shift
		;;
	--hold-frame)
		hold_frame=$2
		shift
		;;
	--unload)
		unload_only=1
		;;
	--no-build)
		build=0
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

if [[ $renderer != generated && $renderer != reference && $renderer != direct ]]; then
	echo "Invalid renderer: $renderer" >&2
	exit 2
fi
if [[ $texture_source != console && $texture_source != pattern &&
      $texture_source != probe ]]; then
	echo "Invalid texture source: $texture_source" >&2
	exit 2
fi
if [[ ! $hold_frame =~ ^[0-9]+$ ]]; then
	echo "Invalid hold-frame value: $hold_frame" >&2
	exit 2
fi
if [[ ! $probe_seed =~ ^[0-9]+$ ]]; then
	echo "Invalid probe-seed value: $probe_seed" >&2
	exit 2
fi
if [[ $texcoord_source != position && $texcoord_source != direct ]]; then
	echo "Invalid texcoord-source value: $texcoord_source" >&2
	exit 2
fi
if [[ $texcoord_mapping != affine && $texcoord_mapping != constant ]]; then
	echo "Invalid texcoord-mapping value: $texcoord_mapping" >&2
	exit 2
fi
if [[ $texcoord_mapping == constant && $texcoord_source != direct ]]; then
	echo "texcoord-mapping=constant requires texcoord-source=direct" >&2
	exit 2
fi
if [[ $direct_primitive != quad && $direct_primitive != triangle ]]; then
	echo "Invalid direct-primitive value: $direct_primitive" >&2
	exit 2
fi
if [[ $direct_pattern != grid && $direct_pattern != vstripes ]]; then
	echo "Invalid direct-pattern value: $direct_pattern" >&2
	exit 2
fi
if [[ $direct_primitive == triangle && $texcoord_source != direct ]]; then
	echo "direct-primitive=triangle requires texcoord-source=direct" >&2
	exit 2
fi
if [[ $texcoord_space != normalized && $texcoord_space != texel ]]; then
	echo "Invalid texcoord-space value: $texcoord_space" >&2
	exit 2
fi
if [[ ! $texel_bias_eighths =~ ^-?[0-9]+$ ]] ||
   ((texel_bias_eighths < -8 || texel_bias_eighths > 8)); then
	echo "Invalid texel-bias-eighths value: $texel_bias_eighths" >&2
	exit 2
fi

repo=$(git rev-parse --show-toplevel)
cd "$repo"
if (( ! allow_dirty )) && [[ -n $(git status --porcelain --untracked-files=normal) ]]; then
	echo "Refusing to load from a dirty tree; commit first or pass --allow-dirty." >&2
	exit 1
fi

module=$repo/drivers/video/fbdev/gcn-gx.ko
commit=$(git rev-parse --short=12 HEAD)
if (( build && ! unload_only )); then
	CCACHE_DISABLE=1 ARCH=powerpc CROSS_COMPILE=powerpc-linux-gnu- \
		make -j"${JOBS:-16}" drivers/video/fbdev/gcn-gx.ko
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
)

ssh_control_path=${TMPDIR:-/tmp}/wii-gx-ssh-${UID}-$$
ssh_options+=(
	-o ControlMaster=auto
	-o ControlPersist=30
	-o ControlPath="$ssh_control_path"
	-o ServerAliveInterval=5
	-o ServerAliveCountMax=3
)

close_ssh_master()
{
	ssh "${ssh_options[@]}" -O exit "$remote" >/dev/null 2>&1 || true
}
trap close_ssh_master EXIT

printf 'Opening persistent SSH connection to %s\n' "$remote"
ssh "${ssh_options[@]}" -Nf "$remote"

remote_exec()
{
	ssh "${ssh_options[@]}" "$remote" "$1"
}
remote_status()
{
	remote_exec "printf '<6>gx-cycle: %s\\n' '$1' > /dev/kmsg"
}

retrieve_debugfs_frame()
{
	local remote_file=$1
	local local_file=$2
	local local_gz=${local_file}.gz
	local local_chunk=${local_file}.gz.chunk
	local remote_gz=/tmp/gcn-gx-debugfs-frame.gz
	local remote_sha local_sha remote_size chunks chunk_size
	local index attempt chunk_ok
	local chunk_bytes=8192
	local max_attempts=10

	: > "$local_file"
	remote_sha=$(remote_exec "sha256sum $remote_file | cut -d' ' -f1") ||
		return 1
	remote_exec "gzip -1 -c $remote_file > $remote_gz" || return 1
	remote_size=$(remote_exec "stat -c %s $remote_gz") || return 1
	[[ $remote_size =~ ^[0-9]+$ && $remote_size -gt 0 ]] || return 1
	chunks=$(((remote_size + chunk_bytes - 1) / chunk_bytes))
	: > "$local_gz"

	for ((index = 0; index < chunks; index++)); do
		chunk_size=$((remote_size - index * chunk_bytes))
		if ((chunk_size > chunk_bytes)); then
			chunk_size=$chunk_bytes
		fi
		chunk_ok=0
		for ((attempt = 1; attempt <= max_attempts; attempt++)); do
			remote_exec "dd if=$remote_gz bs=$chunk_bytes skip=$index count=1 2>/dev/null" > "$local_chunk" || true
			if [[ $(stat -c %s "$local_chunk") == "$chunk_size" ]]; then
				chunk_ok=1
				break
			fi
			sleep 1
		done
		if (( ! chunk_ok )); then
			remote_exec "rm -f $remote_gz"
			return 1
		fi
		command cat "$local_chunk" >> "$local_gz"
	done

	remote_exec "rm -f $remote_gz"
	gzip -t "$local_gz" 2>/dev/null || return 1
	gzip -dc "$local_gz" > "$local_file"
	local_sha=$(sha256sum "$local_file" | cut -d' ' -f1)
	[[ $(stat -c %s "$local_file") == 614400 &&
	   $local_sha == "$remote_sha" ]]
}

remote_status "unloading prior accelerator"
remote_exec "if grep -q '^gcn_gx ' /proc/modules; then rmmod gcn_gx; fi"
remote_exec "printf '\\n=== GX UNLOADED: CPU CONSOLE LIVE ===\\n' > /dev/tty0"

if (( unload_only )); then
	printf 'GX unloaded; CPU console active on %s\n' "$remote"
	exit 0
fi

if [[ ! -f $module ]]; then
	echo "GX module not found: $module" >&2
	exit 1
fi

module_sha=$(sha256sum "$module" | awk '{print $1}')
remote_module=/tmp/gcn-gx.ko
remote_status "receiving module $commit $module_sha"
remote_exec "cat > $remote_module.new" < "$module"
remote_sha=$(remote_exec "sha256sum $remote_module.new | cut -d' ' -f1")
if [[ $remote_sha != "$module_sha" ]]; then
	remote_exec "rm -f $remote_module.new"
	echo "Remote module checksum mismatch: local=$module_sha remote=$remote_sha" >&2
	exit 1
fi
remote_exec "mv -f $remote_module.new $remote_module"

remote_status "loading renderer=$renderer source=$texture_source probe_seed=$probe_seed texsrc=$texcoord_source texmap=$texcoord_mapping prim=$direct_primitive pattern=$direct_pattern coord=$texcoord_space bias8=$texel_bias_eighths hold_frame=$hold_frame"
remote_exec "grep -q ' /sys/kernel/debug ' /proc/mounts || mount -t debugfs debugfs /sys/kernel/debug"
remote_exec "insmod $remote_module renderer=$renderer texture_source=$texture_source probe_seed=$probe_seed texcoord_source=$texcoord_source texcoord_mapping=$texcoord_mapping direct_primitive=$direct_primitive direct_pattern=$direct_pattern texcoord_space=$texcoord_space texel_bias_eighths=$texel_bias_eighths hold_frame=$hold_frame"
remote_exec "printf '\\n=== GX LOADED: $renderer source=$texture_source seed=$probe_seed texsrc=$texcoord_source texmap=$texcoord_mapping prim=$direct_primitive pattern=$direct_pattern coord=$texcoord_space bias8=$texel_bias_eighths hold=$hold_frame ===\\n' > /dev/tty0"

printf '\nGX live cycle complete\n'
printf '  commit:    %s\n' "$commit"
printf '  sha256:    %s\n' "$module_sha"
printf '  renderer:  %s\n' "$renderer"
printf '  texture source: %s\n' "$texture_source"
printf '  probe seed: %s\n' "$probe_seed"
printf '  texcoord source: %s\n' "$texcoord_source"
printf '  texcoord mapping: %s\n' "$texcoord_mapping"
printf '  direct primitive: %s\n' "$direct_primitive"
printf '  direct pattern: %s\n' "$direct_pattern"
printf '  texcoord space: %s\n' "$texcoord_space"
printf '  texel bias eighths: %s\n' "$texel_bias_eighths"
printf '  hold frame: %s\n' "$hold_frame"
remote_exec "grep '^gcn_gx ' /proc/modules; dmesg | grep -E 'gcn-gx:|gcnfb:' | tail -n 100"

capture=/tmp/wii-gx-${commit}-${renderer}-${texture_source}-s${probe_seed}-t${texcoord_source}-m${texcoord_mapping}-p${direct_primitive}-d${direct_pattern}-c${texcoord_space}-b${texel_bias_eighths}-h${hold_frame}.yuyv
capture_ready=$(remote_exec "i=0; while [ \$i -lt 5 ] && [ \"\$(cat /sys/kernel/debug/gcn_gx/xfb_width 2>/dev/null || echo 0)\" -eq 0 ]; do sleep 1; i=\$((i + 1)); done; cat /sys/kernel/debug/gcn_gx/xfb_width 2>/dev/null || echo 0")
if [[ $capture_ready == 640 ]]; then
	printf '  retrieving compressed, checksum-verified XFB\n'
	capture_ok=0
	if retrieve_debugfs_frame /sys/kernel/debug/gcn_gx/xfb_yuyv "$capture"; then
		capture_ok=1
	fi
	capture_size=$(stat -c %s "$capture")
	if (( capture_ok )) && [[ $capture_size == 614400 ]]; then
		printf '  XFB capture: %s (%s)\n' "$capture" "$(sha256sum "$capture" | awk '{print $1}')"
	else
		printf '  XFB capture truncated: %s bytes (expected 614400)\n' "$capture_size"
	fi
	if (( capture_ok )) && [[ $capture_size == 614400 ]] && command -v ffmpeg >/dev/null 2>&1; then
		capture_png=${capture%.yuyv}.png
		ffmpeg -loglevel error -y -f rawvideo -pixel_format yuyv422 \
			-video_size 640x480 -i "$capture" -frames:v 1 "$capture_png"
		printf '  XFB PNG:     %s\n' "$capture_png"
	fi

	vfb_capture=${capture%.yuyv}.vfb.rgb565be
	if [[ $(remote_exec "stat -c %s /sys/kernel/debug/gcn_gx/vfb_rgb565be 2>/dev/null || echo 0") == 614400 ]]; then
		printf '  retrieving compressed, checksum-verified VFB\n'
		if retrieve_debugfs_frame /sys/kernel/debug/gcn_gx/vfb_rgb565be "$vfb_capture"; then
			printf '  VFB capture: %s (%s)\n' "$vfb_capture" "$(sha256sum "$vfb_capture" | awk '{print $1}')"
			if command -v ffmpeg >/dev/null 2>&1; then
				vfb_png=${vfb_capture%.rgb565be}.png
				ffmpeg -loglevel error -y -f rawvideo -pixel_format rgb565be \
					-video_size 640x480 -i "$vfb_capture" -frames:v 1 "$vfb_png"
				printf '  VFB PNG:     %s\n' "$vfb_png"
			fi
		else
			printf '  VFB capture truncated\n'
		fi
	fi
else
	printf '  XFB capture unavailable (remote width %s, expected 640)\n' "$capture_ready"
fi
printf '\a'

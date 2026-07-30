#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

usage()
{
	cat <<'EOF'
Usage: tools/wii-gx-sweep.sh [options]

Reload and benchmark a matrix of GX module configurations over SSH.

Options:
  --host HOST            Wii address (default: WII_SSH_HOST or 10.3.10.12)
  --duration SECONDS     duration per candidate (default: 15)
  --fps RATE             requested source rate (default: 30)
  --rgb565               test RGB565 instead of RGB888
  --matrix FILE          NAME|wii-gx-cycle arguments, one candidate per line
  --results DIR          result directory (default: /tmp/wii-gx-sweep-TIMESTAMP)
  --capture-device PATH  record each run from a V4L2 HDMI capture device
  --capture-size WxH     capture resolution (default: 1920x1080)
  --no-build             reuse existing module and stress binary
  --allow-dirty          permit a sweep from an uncommitted tree

The default matrix compares generated rendering with source deduplication off
and on. Blank lines and lines beginning with # are ignored in matrix files.
Each candidate is isolated by a module reload. Artifacts are uploaded once and
then checksum-verified and reused. Numerical ranking never substitutes for
full-frame visual confirmation.

Environment:
  JOBS                   parallel build jobs (must remain 16 for this project)
  WII_SSH_KEY            SSH private key used by the child tools
EOF
}

ssh_host=${WII_SSH_HOST:-10.3.10.12}
duration=15
fps=30
rgb888=1
matrix=
results=
capture_device=
capture_size=1920x1080
build=1
allow_dirty=0

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
	--rgb565)
		rgb888=0
		;;
	--matrix)
		matrix=$2
		shift
		;;
	--results)
		results=$2
		shift
		;;
	--capture-device)
		capture_device=$2
		shift
		;;
	--capture-size)
		capture_size=$2
		shift
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

if [[ ! $duration =~ ^[1-9][0-9]*$ ]] || ((duration > 3600)); then
	echo "Invalid duration: $duration" >&2
	exit 2
fi
if [[ ! $fps =~ ^[1-9][0-9]*$ ]] || ((fps > 60)); then
	echo "Invalid fps: $fps" >&2
	exit 2
fi
if [[ ! $capture_size =~ ^[0-9]+x[0-9]+$ ]]; then
	echo "Invalid capture size: $capture_size" >&2
	exit 2
fi
if [[ ${JOBS:-16} != 16 ]]; then
	echo "This project requires JOBS=16; got ${JOBS}" >&2
	exit 2
fi

repo=$(git rev-parse --show-toplevel)
cd "$repo"
if (( ! allow_dirty )) && [[ -n $(git status --porcelain --untracked-files=normal) ]]; then
	echo "Refusing to sweep a dirty tree; commit first or pass --allow-dirty." >&2
	exit 1
fi

if [[ -z $results ]]; then
	results=${TMPDIR:-/tmp}/wii-gx-sweep-$(date +%Y%m%d-%H%M%S)
fi
mkdir -p "$results"
results=$(realpath "$results")

if [[ -z $matrix ]]; then
	matrix=$results/default.matrix
	cat > "$matrix" <<'EOF'
baseline|--renderer generated --source-dedup 0 --texel-bias-eighths -2
dedup|--renderer generated --source-dedup 1 --texel-bias-eighths -2
EOF
elif [[ ! -f $matrix ]]; then
	echo "Matrix not found: $matrix" >&2
	exit 1
else
	matrix=$(realpath "$matrix")
fi

if ((build)); then
	CCACHE_DISABLE=1 ARCH=powerpc CROSS_COMPILE=powerpc-linux-gnu- \
		make -j16 drivers/video/fbdev/gcn-gx.ko
	powerpc-linux-gnu-gcc -O2 -Wall -Wextra -Werror -static \
		-o "${TMPDIR:-/tmp}/wii-fb-stress-${USER}" tools/wii-fb-stress.c
fi

common_cycle=(--host "$ssh_host" --no-build --no-capture)
common_stress=(--host "$ssh_host" --duration "$duration" --fps "$fps" --no-build)
if ((allow_dirty)); then
	common_cycle+=(--allow-dirty)
	common_stress+=(--allow-dirty)
fi
if ((rgb888)); then
	common_stress+=(--rgb888)
fi

module_uploaded=0
workload_uploaded=0
restore_needed=0
summary=$results/summary.tsv
printf 'name\ttechnical\tcycle_status\tstress_status\tfps\tirq_delta\tfaults\tcapture\n' > "$summary"

restore_baseline()
{
	if (( ! restore_needed )); then
		return
	fi
	tools/wii-gx-cycle.sh "${common_cycle[@]}" --reuse-remote \
		--renderer generated --source-dedup 0 --texel-bias-eighths -2 \
		> "$results/restore.log" 2>&1 || true
}
trap restore_baseline EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

while IFS='|' read -r name argument_text; do
	[[ -z $name || $name == \#* ]] && continue
	if [[ ! $name =~ ^[A-Za-z0-9._-]+$ ]] || [[ -z $argument_text ]]; then
		echo "Invalid matrix row: $name|$argument_text" >&2
		exit 2
	fi

	candidate=$results/$name
	mkdir -p "$candidate"
	read -r -a candidate_args <<< "$argument_text"
	cycle_args=("${common_cycle[@]}")
	if ((module_uploaded)); then
		cycle_args+=(--reuse-remote)
	fi

	printf '\n=== GX sweep candidate: %s ===\n' "$name"
	set +e
	tools/wii-gx-cycle.sh "${cycle_args[@]}" "${candidate_args[@]}" \
		> "$candidate/cycle.log" 2>&1
	cycle_status=$?
	set -e
	if ((cycle_status)); then
		printf '%s\tfail\t%d\t-\t0\t0\t1\t-\n' \
			"$name" "$cycle_status" >> "$summary"
		printf '  module cycle failed; see %s\n' "$candidate/cycle.log"
		continue
	fi
	module_uploaded=1
	restore_needed=1

	capture=-
	capture_pid=
	if [[ -n $capture_device ]]; then
		capture=$candidate/hdmi.mkv
		ffmpeg -nostdin -loglevel warning -y -f v4l2 \
			-video_size "$capture_size" -i "$capture_device" \
			-t "$((duration + 2))" -c:v ffv1 \
			"$capture" > "$candidate/ffmpeg.log" 2>&1 &
		capture_pid=$!
	fi

	stress_args=("${common_stress[@]}")
	if ((workload_uploaded)); then
		stress_args+=(--reuse-remote)
	fi
	set +e
	tools/wii-fb-stress.sh "${stress_args[@]}" \
		> "$candidate/stress.log" 2>&1
	stress_status=$?
	set -e
	if grep -q '^Running ' "$candidate/stress.log"; then
		workload_uploaded=1
	fi
	if [[ -n $capture_pid ]]; then
		wait "$capture_pid" || true
	fi

	achieved=$(awk '
		/WII_FB_STRESS_DONE/ {
			for (i = 1; i <= NF; i++)
				if ($i ~ /^fps=/) { sub(/^fps=/, "", $i); fps = $i }
		}
		END { print fps + 0 }
	' "$candidate/stress.log")
	irq_delta=$(awk '
		/PE finish IRQ:/ {
			for (i = 1; i <= NF; i++)
				if ($i == "delta") { gsub(/[(),]/, "", $(i + 1)); irq = $(i + 1) }
		}
		END { print irq + 0 }
	' "$candidate/stress.log")
	faults=$(grep -Eic 'timed out|BUG:|Oops|Machine check|kernel panic|Unable to handle|watchdog' \
		"$candidate/stress.log" || true)
	technical=pass
	if ! grep -q 'WII_FB_STRESS_DONE' "$candidate/stress.log" ||
	   ! grep -q 'exit status:  0' "$candidate/stress.log" ||
	   ((faults > 0)); then
		technical=fail
	fi
	printf '%s\t%s\t%d\t%d\t%s\t%s\t%s\t%s\n' \
		"$name" "$technical" "$cycle_status" "$stress_status" \
		"$achieved" "$irq_delta" "$faults" "$capture" >> "$summary"
	printf '  %s: fps=%s irq_delta=%s faults=%s log=%s\n' \
		"$technical" "$achieved" "$irq_delta" "$faults" \
		"$candidate/stress.log"
done < "$matrix"

restore_baseline
restore_needed=0
trap - EXIT INT TERM

ranked=$results/ranked.tsv
{
	head -n 1 "$summary"
	tail -n +2 "$summary" | sort -t $'\t' -k2,2r -k5,5nr
} > "$ranked"
printf '\nGX sweep complete: %s\n' "$results"
column -t -s $'\t' "$ranked" 2>/dev/null || cat "$ranked"
printf '\a'

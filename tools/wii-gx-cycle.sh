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
  --renderer MODE         generated or reference (default: generated)
  --texture-source MODE   console or pattern (default: console)
  --hold-frame N          publish frame N once, then hold (default: 0)
  --unload                unload GX and leave the CPU console active
  --no-build              reuse the existing gcn-gx.ko
  --allow-dirty           permit loading from an uncommitted source tree

Environment:
  JOBS                    parallel build jobs (default: nproc)
  WII_SSH_KEY             SSH private key (default: $HOME/.ssh/id_rsa)
EOF
}

ssh_host=${WII_SSH_HOST:-10.3.10.12}
renderer=generated
texture_source=console
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

if [[ $renderer != generated && $renderer != reference ]]; then
	echo "Invalid renderer: $renderer" >&2
	exit 2
fi
if [[ $texture_source != console && $texture_source != pattern ]]; then
	echo "Invalid texture source: $texture_source" >&2
	exit 2
fi
if [[ ! $hold_frame =~ ^[0-9]+$ ]]; then
	echo "Invalid hold-frame value: $hold_frame" >&2
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
		make -j"${JOBS:-$(nproc)}" drivers/video/fbdev/gcn-gx.ko
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

remote_exec()
{
	ssh "${ssh_options[@]}" "$remote" "$1"
}
remote_status()
{
	remote_exec "printf '<6>gx-cycle: %s\\n' '$1' > /dev/kmsg"
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

remote_status "loading renderer=$renderer source=$texture_source hold_frame=$hold_frame"
remote_exec "insmod $remote_module renderer=$renderer texture_source=$texture_source hold_frame=$hold_frame"
remote_exec "printf '\\n=== GX LOADED: $renderer source=$texture_source hold=$hold_frame ===\\n' > /dev/tty0"

printf '\nGX live cycle complete\n'
printf '  commit:    %s\n' "$commit"
printf '  sha256:    %s\n' "$module_sha"
printf '  renderer:  %s\n' "$renderer"
printf '  texture source: %s\n' "$texture_source"
printf '  hold frame: %s\n' "$hold_frame"
remote_exec "grep '^gcn_gx ' /proc/modules; dmesg | grep -E 'gcn-gx:|gcnfb:' | tail -n 100"
printf '\a'

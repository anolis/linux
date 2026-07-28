#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

usage()
{
	cat <<'EOF'
Usage: tools/wii-deploy-kernel.sh [--no-build] [--allow-dirty] [--keep-mounted]

Build and deploy the Wii zImage through the user-owned BOOTWII mount.

Environment overrides:
  JOBS                 parallel build jobs (default: nproc)
  WII_BOOT_MOUNT       boot mountpoint (default: /media/$USER/BOOTWII)
  WII_ROOT_MOUNT       root mountpoint (default: /media/$USER/WII-LINUX-NGX)
  WII_KERNEL_DEST      deployed image path (default: $mount/gumboot/zImage.ngx)
  WII_DEPLOY_ARCHIVE   host backup directory (default: /tmp/wii-kernel-deploy-backups)
EOF
}

build=1
allow_dirty=0
keep_mounted=0
while (($#)); do
	case "$1" in
	--no-build)
		build=0
		;;
	--allow-dirty)
		allow_dirty=1
		;;
	--keep-mounted)
		keep_mounted=1
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

repo=$(git rev-parse --show-toplevel)
cd "$repo"

if (( ! allow_dirty )) && [[ -n $(git status --porcelain --untracked-files=normal) ]]; then
	echo "Refusing to deploy from a dirty tree; commit first or pass --allow-dirty." >&2
	exit 1
fi

commit=$(git rev-parse --short=12 HEAD)
jobs=${JOBS:-$(nproc)}
boot_mount=${WII_BOOT_MOUNT:-/media/$USER/BOOTWII}
root_mount=${WII_ROOT_MOUNT:-/media/$USER/WII-LINUX-NGX}
destination=${WII_KERNEL_DEST:-$boot_mount/gumboot/zImage.ngx}
archive=${WII_DEPLOY_ARCHIVE:-/tmp/wii-kernel-deploy-backups}
image=$repo/arch/powerpc/boot/zImage

if (( build )); then
	CCACHE_DISABLE=1 ARCH=powerpc CROSS_COMPILE=powerpc-linux-gnu- \
		make -j"$jobs" zImage modules
fi

if [[ ! -f $image ]]; then
	echo "Kernel image not found: $image" >&2
	exit 1
fi

if ! mountpoint -q "$boot_mount"; then
	mount "$boot_mount"
fi
if [[ ! -w $boot_mount ]]; then
	echo "BOOTWII is not writable by $(id -un): $boot_mount" >&2
	exit 1
fi

mkdir -p "$archive" "$(dirname "$destination")"
source_sha=$(sha256sum "$image" | awk '{print $1}')

if [[ -f $destination ]]; then
	previous_sha=$(sha256sum "$destination" | awk '{print $1}')
	previous=$archive/zImage.ngx.$previous_sha
	if [[ ! -f $previous ]]; then
		cp "$destination" "$previous"
	fi
fi

staged=$destination.new
trap 'rm -f "$staged"' EXIT
cp "$image" "$staged"
staged_sha=$(sha256sum "$staged" | awk '{print $1}')
if [[ $staged_sha != "$source_sha" ]]; then
	echo "Staged image checksum mismatch: source=$source_sha staged=$staged_sha" >&2
	exit 1
fi

sync "$staged"
mv -f "$staged" "$destination"
sync "$destination"
deployed_sha=$(sha256sum "$destination" | awk '{print $1}')
if [[ $deployed_sha != "$source_sha" ]]; then
	echo "Deployed image checksum mismatch: source=$source_sha card=$deployed_sha" >&2
	exit 1
fi
trap - EXIT

printf '\nWii kernel deployed\n'
printf '  commit: %s\n' "$commit"
printf '  sha256: %s\n' "$deployed_sha"
printf '  source: %s\n' "$image"
printf '  target: %s\n' "$destination"

if (( ! keep_mounted )); then
	for mount_path in "$boot_mount" "$root_mount"; do
		if mountpoint -q "$mount_path"; then
			mount_source=$(findmnt -nro SOURCE --target "$mount_path")
			if ! umount "$mount_path"; then
				udisksctl unmount -b "$mount_source"
			fi
		fi
	done
	printf '  media:  unmounted; card can be removed\n'
fi
printf '\a'

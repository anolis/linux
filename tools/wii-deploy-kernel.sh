#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

usage()
{
	cat <<'EOF'
Usage: tools/wii-deploy-kernel.sh [--no-build] [--allow-dirty] [--keep-mounted]
                                  [--host HOST] [--reboot]

Build and deploy the Wii zImage through a local BOOTWII mount or SSH.

Environment overrides:
  JOBS                 parallel build jobs (default: nproc)
  WII_BOOT_MOUNT       boot mountpoint (default: /media/$USER/BOOTWII)
  WII_ROOT_MOUNT       root mountpoint (default: /media/$USER/WII-LINUX-NGX)
  WII_KERNEL_DEST      deployed image path (default: $mount/gumboot/zImage.ngx)
  WII_DEPLOY_ARCHIVE   host backup directory (default: /tmp/wii-kernel-deploy-backups)
  WII_SSH_HOST         Wii hostname/address, equivalent to --host
  WII_SSH_KEY          SSH private key (default: $HOME/.ssh/id_rsa)
EOF
}

build=1
allow_dirty=0
keep_mounted=0
ssh_host=${WII_SSH_HOST:-}
remote_reboot=0
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
	--host)
		if (($# < 2)); then
			echo "--host requires an address" >&2
			exit 2
		fi
		ssh_host=$2
		shift
		;;
	--reboot)
		remote_reboot=1
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
	ARCH=powerpc CROSS_COMPILE=powerpc-linux-gnu- make wii_defconfig
	if ! grep -q '^CONFIG_FB_GAMECUBE=y$' .config; then
		echo "wii_defconfig did not enable CONFIG_FB_GAMECUBE=y" >&2
		exit 1
	fi
	CCACHE_DISABLE=1 ARCH=powerpc CROSS_COMPILE=powerpc-linux-gnu- \
		make -j"$jobs" zImage modules
fi

if [[ ! -f $image ]]; then
	echo "Kernel image not found: $image" >&2
	exit 1
fi

mkdir -p "$archive"
source_sha=$(sha256sum "$image" | awk '{print $1}')

if [[ -n $ssh_host ]]; then
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
	remote_mount=/tmp/bootwii
	remote_device=/dev/mmcblk0p1
	remote_destination=$remote_mount/gumboot/zImage.ngx
	remote_staged=$remote_destination.new

	remote_exec()
	{
		ssh "${ssh_options[@]}" "$remote" "$1"
	}
	remote_status()
	{
		remote_exec "printf '<6>gx-deploy: %s\\n' '$1' > /dev/kmsg"
	}

	remote_status "preparing commit $commit"
	if ! remote_exec "test -b $remote_device"; then
		remote_device=/tmp/mmcblk0p1
		remote_exec "set -- \$(tr ':' ' ' < /sys/class/block/mmcblk0p1/dev); rm -f $remote_device; mknod $remote_device b \$1 \$2"
	fi
	remote_exec "mkdir -p $remote_mount; grep -qs ' $remote_mount ' /proc/mounts || mount -t vfat $remote_device $remote_mount"

	remote_status "receiving zImage $source_sha"
	remote_exec "cat > $remote_staged" < "$image"
	staged_sha=$(remote_exec "sha256sum $remote_staged | cut -d' ' -f1")
	if [[ $staged_sha != "$source_sha" ]]; then
		remote_exec "rm -f $remote_staged"
		echo "Remote staged checksum mismatch: source=$source_sha staged=$staged_sha" >&2
		exit 1
	fi

	remote_status "checksum verified; installing zImage"
	remote_exec "sync $remote_staged; mv -f $remote_staged $remote_destination; sync $remote_destination"
	deployed_sha=$(remote_exec "sha256sum $remote_destination | cut -d' ' -f1")
	if [[ $deployed_sha != "$source_sha" ]]; then
		echo "Remote deployed checksum mismatch: source=$source_sha card=$deployed_sha" >&2
		exit 1
	fi
	remote_status "installed $deployed_sha"
	remote_exec "umount $remote_mount"

	printf '\nWii kernel deployed over SSH\n'
	printf '  commit: %s\n' "$commit"
	printf '  sha256: %s\n' "$deployed_sha"
	printf '  source: %s\n' "$image"
	printf '  target: %s:%s\n' "$remote" "$remote_destination"
	if (( remote_reboot )); then
		printf '  reboot: requested\n'
		remote_status "rebooting into commit $commit"
		remote_exec "/sbin/reboot -f" || true
	fi
	printf '\a'
	exit 0
fi

if ! mountpoint -q "$boot_mount"; then
	mount "$boot_mount"
fi
if [[ ! -w $boot_mount ]]; then
	echo "BOOTWII is not writable by $(id -un): $boot_mount" >&2
	exit 1
fi

mkdir -p "$(dirname "$destination")"

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

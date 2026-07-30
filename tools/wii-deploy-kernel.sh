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
  JOBS                 parallel build jobs (default: 16)
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
jobs=${JOBS:-16}
boot_mount=${WII_BOOT_MOUNT:-/media/$USER/BOOTWII}
root_mount=${WII_ROOT_MOUNT:-/media/$USER/WII-LINUX-NGX}
destination=${WII_KERNEL_DEST:-$boot_mount/gumboot/zImage.ngx}
archive=${WII_DEPLOY_ARCHIVE:-/tmp/wii-kernel-deploy-backups}
image=$repo/arch/powerpc/boot/zImage

if (( build )); then
	ARCH=powerpc CROSS_COMPILE=powerpc-linux-gnu- make -j"$jobs" wii_defconfig
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
	ssh_control_path=${TMPDIR:-/tmp}/wii-deploy-ssh-${UID}-$$
	ssh_options+=(
		-o ControlMaster=auto
		-o ControlPersist=30
		-o ControlPath="$ssh_control_path"
		-o ServerAliveInterval=5
		-o ServerAliveCountMax=3
	)
	remote_mount=/tmp/bootwii
	remote_device=/dev/mmcblk0p1
	remote_mounted_here=0
	remote_remounted_rw=0

	remote_exec()
	{
		ssh "${ssh_options[@]}" "$remote" "$1"
	}
	remote_status()
	{
		remote_exec "printf '<6>gx-deploy: %s\\n' '$1' > /dev/kmsg"
	}
	close_ssh_master()
	{
		ssh "${ssh_options[@]}" -O exit "$remote" >/dev/null 2>&1 || true
	}
	restore_remote_boot_mount()
	{
		if ((remote_remounted_rw)); then
			remote_exec "sync; mount -o remount,ro $remote_mount" || true
			remote_remounted_rw=0
		elif ((remote_mounted_here)); then
			remote_exec "umount $remote_mount" || true
			remote_mounted_here=0
		fi
	}
	cleanup_remote()
	{
		restore_remote_boot_mount
		close_ssh_master
	}
	trap cleanup_remote EXIT

	chunk_bytes=32768
	remote_upload()
	{
		local source=$1
		local target=$2
		local source_size chunks index expected_size attempt
		local chunk_ok remote_size upload_command

		source_size=$(stat -c %s "$source")
		chunks=$(((source_size + chunk_bytes - 1) / chunk_bytes))
		remote_exec ": > $target"
		for ((index = 0; index < chunks; index++)); do
			expected_size=$(((index + 1) * chunk_bytes))
			if ((expected_size > source_size)); then
				expected_size=$source_size
			fi
			chunk_ok=0
			for ((attempt = 1; attempt <= 5; attempt++)); do
				upload_command="dd of=$target bs=$chunk_bytes"
				upload_command+=" seek=$index conv=notrunc 2>/dev/null"
				dd if="$source" bs=$chunk_bytes skip=$index count=1 2>/dev/null |
					remote_exec "$upload_command" || true
				remote_size=$(remote_exec "stat -c %s $target")
				if [[ $remote_size == "$expected_size" ]]; then
					chunk_ok=1
					break
				fi
				sleep 1
			done
			if (( ! chunk_ok )); then
				echo "Remote upload failed at chunk $index/$chunks" >&2
				return 1
			fi
		done
	}

	remote_download()
	{
		local source=$1
		local target=$2
		local source_size chunks index chunk_size attempt
		local chunk_ok download_command
		local chunk_file=${target}.chunk

		source_size=$(remote_exec "stat -c %s $source")
		chunks=$(((source_size + chunk_bytes - 1) / chunk_bytes))
		: > "$target"
		for ((index = 0; index < chunks; index++)); do
			chunk_size=$((source_size - index * chunk_bytes))
			if ((chunk_size > chunk_bytes)); then
				chunk_size=$chunk_bytes
			fi
			chunk_ok=0
			for ((attempt = 1; attempt <= 5; attempt++)); do
				download_command="dd if=$source bs=$chunk_bytes"
				download_command+=" skip=$index count=1 2>/dev/null"
				remote_exec "$download_command" > "$chunk_file" || true
				if [[ $(stat -c %s "$chunk_file") == "$chunk_size" ]]; then
					chunk_ok=1
					break
				fi
				sleep 1
			done
			if (( ! chunk_ok )); then
				rm -f "$chunk_file"
				echo "Remote download failed at chunk $index/$chunks" >&2
				return 1
			fi
			command cat "$chunk_file" >> "$target"
		done
		rm -f "$chunk_file"
	}

	remote_status "preparing commit $commit"
	if ! remote_exec "test -b $remote_device"; then
		remote_device=/tmp/mmcblk0p1
		remote_exec "set -- \$(tr ':' ' ' < /sys/class/block/mmcblk0p1/dev); rm -f $remote_device; mknod $remote_device b \$1 \$2"
	fi
	remote_existing_mount=$(remote_exec "awk '\$1 ~ /mmcblk0p1$/ { print \$2; exit }' /proc/mounts")
	if [[ -n $remote_existing_mount ]]; then
		remote_mount=$remote_existing_mount
		remote_mount_options=$(remote_exec "awk '\$2 == \"$remote_mount\" { print \$4; exit }' /proc/mounts")
		if [[ ,$remote_mount_options, == *,ro,* ]]; then
			remote_exec "mount -o remount,rw $remote_mount"
			remote_remounted_rw=1
		fi
	else
		remote_exec "mkdir -p $remote_mount; mount -t vfat $remote_device $remote_mount"
		remote_mounted_here=1
	fi
	remote_destination=$remote_mount/gumboot/zImage.ngx
	remote_staged=$remote_destination.new
	if remote_exec "test -f $remote_destination"; then
		previous_sha=$(remote_exec "sha256sum $remote_destination | cut -d' ' -f1")
		previous=$archive/zImage.ngx.$previous_sha
		if [[ ! -f $previous ]]; then
			previous_tmp=$previous.new
			remote_download "$remote_destination" "$previous_tmp"
			if [[ $(sha256sum "$previous_tmp" | awk '{print $1}') != "$previous_sha" ]]; then
				rm -f "$previous_tmp"
				echo "Remote kernel backup checksum mismatch" >&2
				exit 1
			fi
			mv "$previous_tmp" "$previous"
		fi
	fi

	remote_status "receiving zImage $source_sha"
	remote_upload "$image" "$remote_staged"
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
	restore_remote_boot_mount
	close_ssh_master
	trap - EXIT

	printf '\nWii kernel deployed over SSH\n'
	printf '  commit: %s\n' "$commit"
	printf '  sha256: %s\n' "$deployed_sha"
	printf '  source: %s\n' "$image"
	printf '  target: %s:%s\n' "$remote" "$remote_destination"
	if (( remote_reboot )); then
		printf '  reboot: requested\n'
		remote_status "rebooting into commit $commit"
		# The diagnostic PID 1 does not reliably service reboot(8).
		timeout 10 ssh "${ssh_options[@]}" "$remote" \
			"sync; echo b > /proc/sysrq-trigger" || true
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

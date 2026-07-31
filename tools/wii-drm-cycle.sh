#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

usage()
{
	cat <<'EOF'
Usage: tools/wii-drm-cycle.sh [options]

Build, upload, and bind the experimental Wii VI DRM/KMS driver over SSH.

Options:
  --host HOST      Wii address (default: WII_SSH_HOST or 10.3.10.12)
  --no-build       reuse the current zImage and DRM module artifacts
  --reuse-remote   verify and reuse checksum-matched modules in /tmp
  --program-mode   have gcn-drm program fixed NTSC 480i VI timing
  --restore        unload DRM and restore legacy gcnfb/GX
  --allow-dirty    permit loading artifacts from an uncommitted tree

The running kernel must already contain built-in DRM core. A normal cycle
leaves gcn-drm active for inspection. Any failed transition restores gcnfb.
EOF
}

ssh_host=${WII_SSH_HOST:-10.3.10.12}
build=1
reuse_remote=0
restore_only=0
allow_dirty=0
program_mode=0

while (($#)); do
	case "$1" in
	--host)
		ssh_host=$2
		shift
		;;
	--no-build)
		build=0
		;;
	--reuse-remote)
		reuse_remote=1
		;;
	--program-mode)
		program_mode=1
		;;
	--restore)
		restore_only=1
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

repo=$(git rev-parse --show-toplevel)
cd "$repo"
if (( ! allow_dirty )) &&
   [[ -n $(git status --porcelain --untracked-files=normal) ]]; then
	echo "Refusing to load from a dirty tree; commit first or pass --allow-dirty." >&2
	exit 1
fi

commit=$(git rev-parse --short=12 HEAD)
device=c002000.video
legacy_driver=/sys/bus/platform/drivers/gcn-vifb
drm_driver=/sys/bus/platform/drivers/gcn-vi

module_paths=(
	drivers/gpu/drm/drm_panel_orientation_quirks.ko
	drivers/gpu/drm/drm.ko
	drivers/gpu/drm/drm_kms_helper.ko
	drivers/gpu/drm/drm_shmem_helper.ko
	drivers/gpu/drm/gcn/gcn-drm.ko
	drivers/video/fbdev/gcn-gx.ko
)
remote_modules=(
	/tmp/drm_panel_orientation_quirks.ko
	/tmp/drm.ko
	/tmp/drm_kms_helper.ko
	/tmp/drm_shmem_helper.ko
	/tmp/gcn-drm.ko
	/tmp/gcn-gx.ko
)
gx_module_index=5

if (( build && ! restore_only )); then
	CCACHE_DISABLE=1 ARCH=powerpc CROSS_COMPILE=powerpc-linux-gnu- \
		make -j16 zImage
	CCACHE_DISABLE=1 ARCH=powerpc CROSS_COMPILE=powerpc-linux-gnu- \
		make -j16 modules
fi

ssh_key=${WII_SSH_KEY:-$HOME/.ssh/id_rsa}
if [[ $ssh_host == *@* ]]; then
	remote=$ssh_host
else
	remote=root@$ssh_host
fi
ssh_control_path=${TMPDIR:-/tmp}/wii-drm-ssh-${UID}-$$
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

remote_exec()
{
	# Commands are intentionally assembled locally for the controlled Wii shell.
	# shellcheck disable=SC2029
	ssh "${ssh_options[@]}" "$remote" "$1"
}

remote_status()
{
	remote_exec "printf '<6>drm-cycle: %s\\n' '$1' > /dev/kmsg"
}

upload_module()
{
	local module=$1
	local remote_module=$2
	local local_sha
	local remote_sha

	local_sha=$(sha256sum "$module" | awk '{print $1}')
	if (( reuse_remote )); then
		remote_sha=$(remote_exec "sha256sum $remote_module 2>/dev/null | cut -d' ' -f1")
		if [[ $remote_sha != "$local_sha" ]]; then
			echo "Remote checksum mismatch for $remote_module" >&2
			return 1
		fi
		return 0
	fi

	remote_status "receiving $(basename "$remote_module") $local_sha"
	remote_exec "cat > $remote_module.new" < "$module"
	remote_sha=$(remote_exec "sha256sum $remote_module.new | cut -d' ' -f1")
	if [[ $remote_sha != "$local_sha" ]]; then
		remote_exec "rm -f $remote_module.new"
		echo "Upload checksum mismatch for $remote_module" >&2
		return 1
	fi
	remote_exec "mv -f $remote_module.new $remote_module"
}

restore_legacy()
{
	remote_status "restoring legacy display" || true
	remote_exec "
		set +e
		rmmod gcn_drm 2>/dev/null
		rmmod drm_shmem_helper 2>/dev/null
		rmmod drm_kms_helper 2>/dev/null
		rmmod drm 2>/dev/null
		rmmod drm_panel_orientation_quirks 2>/dev/null
		if [ ! -e $legacy_driver/$device ]; then
			printf '%s' '$device' > $legacy_driver/bind
		fi
		if ! grep -q '^gcn_gx ' /proc/modules && [ -f /tmp/gcn-gx.ko ]; then
			insmod /tmp/gcn-gx.ko renderer=generated texel_bias_eighths=-2
		fi
		printf '\\n=== DRM CYCLE: LEGACY GCNFB RESTORED ===\\n' > /dev/tty0
		test -e $legacy_driver/$device
	"
}

transition_started=0
leave_drm_active=0
on_exit()
{
	status=$?
	if (( status != 0 && transition_started && ! leave_drm_active )); then
		echo "DRM transition failed; restoring legacy display." >&2
		restore_legacy || true
	fi
	close_ssh_master
	exit "$status"
}
trap on_exit EXIT

printf 'Opening persistent SSH connection to %s\n' "$remote"
ssh "${ssh_options[@]}" -Nf "$remote"

if (( restore_only )); then
	if [[ ! -f ${module_paths[$gx_module_index]} ]]; then
		echo "GX module not found: ${module_paths[$gx_module_index]}" >&2
		exit 1
	fi
	upload_module "${module_paths[$gx_module_index]}" \
		"${remote_modules[$gx_module_index]}"
	transition_started=1
	restore_legacy
	printf 'Legacy gcnfb restored on %s\n' "$remote"
	exit 0
fi

for module in "${module_paths[@]}"; do
	if [[ ! -f $module ]]; then
		echo "DRM module not found: $module" >&2
		exit 1
	fi
done

for i in "${!module_paths[@]}"; do
	upload_module "${module_paths[$i]}" "${remote_modules[$i]}"
done

remote_status "preflighting generic DRM modules"
remote_exec "grep -q '^drm_panel_orientation_quirks ' /proc/modules || insmod /tmp/drm_panel_orientation_quirks.ko"
remote_exec "grep -q '^drm ' /proc/modules || insmod /tmp/drm.ko"
remote_exec "grep -q '^drm_kms_helper ' /proc/modules || insmod /tmp/drm_kms_helper.ko"
remote_exec "grep -q '^drm_shmem_helper ' /proc/modules || insmod /tmp/drm_shmem_helper.ko"

remote_exec "test -e $legacy_driver/$device" || {
	echo "Legacy gcnfb is not bound to $device; refusing ambiguous transition." >&2
	exit 1
}

transition_started=1
remote_status "unloading GX accelerator"
remote_exec "if grep -q '^gcn_gx ' /proc/modules; then rmmod gcn_gx; fi"
remote_exec "printf '\\n=== DRM CYCLE: UNBINDING LEGACY GCNFB ===\\n' > /dev/tty0"

remote_status "unbinding legacy gcnfb"
remote_exec "printf '%s' '$device' > $legacy_driver/unbind"

remote_status "loading gcn-drm $commit"
if (( program_mode )); then
	remote_exec "insmod /tmp/gcn-drm.ko program_mode=1"
else
	remote_exec "insmod /tmp/gcn-drm.ko"
fi
remote_exec "test -e $drm_driver/$device"
remote_exec "test -e /sys/class/drm/card0"
remote_exec "printf '\\n=== GCN DRM/KMS ACTIVE: $commit ===\\n' > /dev/tty0"

leave_drm_active=1
printf '\nWii DRM/KMS cycle active\n'
printf '  commit: %s\n' "$commit"
for i in "${!module_paths[@]}"; do
	printf '  %-24s %s\n' "$(basename "${remote_modules[$i]}")" \
		"$(sha256sum "${module_paths[$i]}" | awk '{print $1}')"
done
remote_exec "
	printf 'DRM devices:\\n'
	ls -l /sys/class/drm
	printf 'Loaded modules:\\n'
	grep -E '^(gcn_drm|drm_)' /proc/modules
	printf 'Recent display log:\\n'
	dmesg | grep -E 'gcn-vi|gcn_drm|\\[drm\\]|gcnfb:' | tail -n 100
"

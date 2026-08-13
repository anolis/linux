#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

usage()
{
	cat <<'EOF'
Usage: tools/wii-gcn-render-cycle.sh [options]

Upload and run the GCN render-UAPI hardware test over SSH.

Options:
  --host HOST          Wii address (default: WII_SSH_HOST or 10.3.10.12)
  --module FILE        gcn-gx module (default: drivers/video/fbdev/gcn-gx.ko)
  --client FILE        test client (default: /tmp/wii-gcn-render-test)
  --module-args ARGS   arguments passed to insmod
  --reuse-remote       require checksum-matched files already in /tmp
  --keep-loaded        leave gcn_gx loaded after a successful test
  --allow-dirty        permit testing from an uncommitted source tree

The Wii console reports connection, transfer, load, test, unload, and final
status. The complete test output remains on the invoking terminal.
EOF
}

ssh_host=${WII_SSH_HOST:-10.3.10.12}
module=
client=/tmp/wii-gcn-render-test
module_args=
reuse_remote=0
keep_loaded=0
allow_dirty=0

while (($#)); do
	case "$1" in
	--host)
		ssh_host=$2
		shift
		;;
	--module)
		module=$2
		shift
		;;
	--client)
		client=$2
		shift
		;;
	--module-args)
		module_args=$2
		shift
		;;
	--reuse-remote)
		reuse_remote=1
		;;
	--keep-loaded)
		keep_loaded=1
		;;
	--allow-dirty)
		allow_dirty=1
		;;
	-h|--help)
		usage
		exit 0
		;;
	*)
		printf 'Unknown argument: %s\n' "$1" >&2
		usage >&2
		exit 2
		;;
	esac
	shift
done

repo=$(git rev-parse --show-toplevel)
cd "$repo"
module=${module:-$repo/drivers/video/fbdev/gcn-gx.ko}
[[ -f $module ]] || { printf 'Module not found: %s\n' "$module" >&2; exit 1; }
[[ -f $client ]] || { printf 'Client not found: %s\n' "$client" >&2; exit 1; }
if (( ! allow_dirty )) &&
   [[ -n $(git status --porcelain --untracked-files=normal) ]]; then
	printf 'Refusing a dirty tree; commit first or pass --allow-dirty.\n' >&2
	exit 1
fi
if [[ ! $module_args =~ ^[A-Za-z0-9_.,=+\ -]*$ ]]; then
	printf 'Unsafe character in module arguments: %s\n' "$module_args" >&2
	exit 2
fi

ssh_key=${WII_SSH_KEY:-$HOME/.ssh/id_rsa}
if [[ $ssh_host == *@* ]]; then
	remote=$ssh_host
else
	remote=root@$ssh_host
fi
ssh_control_path=${TMPDIR:-/tmp}/wii-gcn-render-ssh-${UID}-$$
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

remote_exec()
{
	# shellcheck disable=SC2029
	ssh "${ssh_options[@]}" "$remote" "$1"
}

remote_notice()
{
	remote_exec "
		printf '<6>gcn-render-cycle: %s\\n' '$1' > /dev/kmsg
		printf '\\n=== GCN RENDER: %s ===\\n' '$1' > /dev/tty0 2>/dev/null || true
	"
}

close_ssh_master()
{
	ssh "${ssh_options[@]}" -O exit "$remote" >/dev/null 2>&1 || true
}

loaded=0
cleanup()
{
	local status=$?

	if (( loaded && (! keep_loaded || status != 0) )); then
		remote_notice "unloading accelerator module" >/dev/null 2>&1 || true
		remote_exec "rmmod gcn_gx" >/dev/null 2>&1 || true
		loaded=0
		remote_notice "CPU console restored" >/dev/null 2>&1 || true
	fi
	close_ssh_master
}
trap cleanup EXIT

upload_verified()
{
	local source=$1 destination=$2 label=$3
	local local_sha remote_sha

	local_sha=$(sha256sum "$source" | awk '{print $1}')
	if (( reuse_remote )); then
		remote_sha=$(remote_exec "sha256sum $destination 2>/dev/null | cut -d' ' -f1")
		[[ $remote_sha == "$local_sha" ]] || {
			printf '%s checksum mismatch: local=%s remote=%s\n' \
				"$label" "$local_sha" "${remote_sha:-missing}" >&2
			return 1
		}
	else
		remote_notice "downloading $label"
		remote_exec "cat > $destination.new" < "$source"
		remote_sha=$(remote_exec "sha256sum $destination.new | cut -d' ' -f1")
		if [[ $remote_sha != "$local_sha" ]]; then
			remote_exec "rm -f $destination.new"
			printf '%s checksum mismatch: local=%s remote=%s\n' \
				"$label" "$local_sha" "${remote_sha:-missing}" >&2
			return 1
		fi
		remote_exec "chmod 755 $destination.new && mv -f $destination.new $destination"
	fi
	remote_notice "$label verified"
}

printf 'Opening persistent SSH connection to %s\n' "$remote"
ssh "${ssh_options[@]}" -Nf "$remote"
remote_notice "connected"
remote_notice "unloading prior accelerator"
remote_exec "rmmod gcn_gx 2>/dev/null || true"

remote_module=/tmp/gcn-gx.ko
remote_client=/tmp/wii-gcn-render-test
upload_verified "$module" "$remote_module" module
upload_verified "$client" "$remote_client" "test client"

remote_notice "loading accelerator module"
remote_exec "insmod $remote_module $module_args"
loaded=1
remote_notice "running hardware test"
set +e
remote_exec "$remote_client"
test_status=$?
set -e

if (( test_status )); then
	remote_notice "TEST FAILED status $test_status"
	exit "$test_status"
fi
remote_notice "TEST PASSED"
if (( keep_loaded )); then
	remote_notice "accelerator left loaded"
	loaded=0
fi

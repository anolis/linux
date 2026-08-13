#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

JOBS=16
BOOT_LABEL=${WII_BOOT_LABEL:-BOOTWII}
ROOT_LABEL=${WII_ROOT_LABEL:-WII-LINUX-NGX}
BOOT_MOUNT=${WII_BOOT_MOUNT:-/media/$USER/BOOTWII}
ROOT_MOUNT=${WII_ROOT_MOUNT:-/media/$USER/WII-LINUX-NGX}
STATE_ROOT=${WII_TEST_STATE:-$HOME/.local/state/wii-linux-ngx/hw-tests}
LOG_NAMES=(dmesg.txt early-dmesg.txt wpa-debug.txt sshd-debug.txt)

die()
{
	printf 'wii-cycle: ERROR: %s\n' "$*" >&2
	exit 1
}

say()
{
	printf 'wii-cycle: %s\n' "$*" >&2
}

finish()
{
	printf '\a'
	if command -v notify-send >/dev/null 2>&1; then
		notify-send 'Wii hardware cycle' "$1" >/dev/null 2>&1 || true
	fi
}

usage()
{
	cat <<'EOF'
Usage:
  tools/wii-hw-cycle.sh build TEST_ID
  tools/wii-hw-cycle.sh stage TEST_ID
  tools/wii-hw-cycle.sh deploy IMAGE TEST_ID
  tools/wii-hw-cycle.sh artifacts MODULE CLIENT
  tools/wii-hw-cycle.sh collect [TEST_ID]
  tools/wii-hw-cycle.sh status

build   Build zImage with make -j16 and archive it with a manifest.
stage   Build and immediately deploy the checksum-verified archived image.
deploy  Mount the labelled SD partitions if needed, snapshot old log hashes,
        copy IMAGE to gumboot/zImage.ngx, sync, and verify the card checksum.
artifacts
        Stage a GX module and render-test client in rootfs /gx-test, verify both
        checksums, sync, and unmount both card partitions.
collect Copy returned logs and reject unchanged or wrongly-marked diagnostics.
status  Show the latest run manifest, card mounts, image hash, and log hashes.

TEST_ID must also appear in the kernel command line as wii_test=TEST_ID.
No action uses sudo or pkexec. Root-owned logs are compared, not deleted.
EOF
}

unmount_card()
{
	local mount_path source

	for mount_path in "$ROOT_MOUNT" "$BOOT_MOUNT"; do
		if findmnt -rn --target "$mount_path" >/dev/null 2>&1; then
			source=$(findmnt -nr -o SOURCE --target "$mount_path")
			say "unmounting $source"
			udisksctl unmount -b "$source" >/dev/null
		fi
	done
}

stage_artifacts()
{
	local module=$1 client=$2 root module_dst client_dst
	local module_hash client_hash staged_hash

	[[ -f "$module" ]] || die "module not found: $module"
	[[ -f "$client" ]] || die "client not found: $client"
	root=$(ensure_mount "$ROOT_LABEL" "$ROOT_MOUNT")
	ensure_mount "$BOOT_LABEL" "$BOOT_MOUNT" >/dev/null
	mkdir -p "$root/gx-test"
	module_dst=$root/gx-test/gcn-gx.ko
	client_dst=$root/gx-test/wii-gcn-render-test
	module_hash=$(hash_file "$module")
	client_hash=$(hash_file "$client")

	say "staging GX module $module_hash"
	cp "$module" "$module_dst.new"
	staged_hash=$(hash_file "$module_dst.new")
	[[ "$staged_hash" == "$module_hash" ]] ||
		die "module verification failed: $staged_hash != $module_hash"
	mv -f "$module_dst.new" "$module_dst"

	say "staging render client $client_hash"
	cp "$client" "$client_dst.new"
	chmod 0755 "$client_dst.new"
	staged_hash=$(hash_file "$client_dst.new")
	[[ "$staged_hash" == "$client_hash" ]] ||
		die "client verification failed: $staged_hash != $client_hash"
	mv -f "$client_dst.new" "$client_dst"

	sync "$module_dst" "$client_dst"
	[[ $(hash_file "$module_dst") == "$module_hash" ]] ||
		die 'final module checksum mismatch'
	[[ $(hash_file "$client_dst") == "$client_hash" ]] ||
		die 'final client checksum mismatch'
	say "verified $module_dst"
	say "verified $client_dst"
	unmount_card
	finish 'GX artifacts staged; card can be removed'
}

safe_id()
{
	case "$1" in
		''|*[!A-Za-z0-9._-]*) die "invalid test id '$1'" ;;
	esac
}

device_for_label()
{
	lsblk -nrpo NAME,LABEL | awk -v label="$1" '$2 == label { print $1; exit }'
}

ensure_mount()
{
	local label=$1 preferred=$2 device source options

	device=$(device_for_label "$label")
	[[ -n "$device" ]] || die "no block device with label $label; insert the card"

	source=$(findmnt -nr -o SOURCE --target "$preferred" 2>/dev/null || true)
	if [[ "$source" != "$device" ]]; then
		say "mounting $label ($device)"
		udisksctl mount -b "$device" >/dev/null
	fi

	source=$(findmnt -nr -o SOURCE --target "$preferred" 2>/dev/null || true)
	[[ "$source" == "$device" ]] || die "$label did not mount at $preferred"
	options=$(findmnt -nr -o OPTIONS --target "$preferred")
	case ",$options," in
		*,ro,*) die "$preferred is read-only; unmount and repair $device" ;;
	esac
	printf '%s\n' "$preferred"
}

hash_file()
{
	local file=$1
	if [[ -f "$file" ]]; then
		sha256sum "$file" | awk '{print $1}'
	else
		printf '%s\n' missing
	fi
}

latest_dir()
{
	local pointer=$STATE_ROOT/latest
	[[ -f "$pointer" ]] || die 'no recorded hardware cycle'
	cat "$pointer"
}

manifest_get()
{
	local dir=$1 key=$2
	awk -F '\t' -v key="$key" '$1 == key { sub(/^[^\t]*\t/, ""); print; exit }' "$dir/manifest.tsv"
}

new_run()
{
	local test_id=$1 run
	run=$STATE_ROOT/$(date +%Y%m%d-%H%M%S)-$test_id
	mkdir -p "$run"
	printf '%s\n' "$run" > "$STATE_ROOT/latest"
	printf 'test_id\t%s\ncreated_utc\t%s\n' \
		"$test_id" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$run/manifest.tsv"
	printf '%s\n' "$run"
}

record()
{
	printf '%s\t%s\n' "$2" "$3" >> "$1/manifest.tsv"
}

build_image()
{
	local test_id=$1 root run image hash marker commit branch build_id
	safe_id "$test_id"
	root=$(git rev-parse --show-toplevel)
	cd "$root"
	git diff --check
	[[ -z $(git status --porcelain --untracked-files=no) ]] || \
		die 'tracked source changes are uncommitted'
	marker=$(grep -rho 'wii_test=[A-Za-z0-9._-]*' arch/powerpc/boot/dts | head -n1 || true)
	[[ "$marker" == "wii_test=$test_id" ]] || \
		die "DTS marker is '${marker:-missing}', expected wii_test=$test_id"

	say "building zImage with -j$JOBS"
	ARCH=powerpc CROSS_COMPILE=powerpc-linux-gnu- make -j"$JOBS" zImage
	image=$root/arch/powerpc/boot/zImage
	[[ -s "$image" ]] || die "build did not produce $image"
	run=$(new_run "$test_id")
	cp "$image" "$run/zImage.ngx"
	hash=$(hash_file "$run/zImage.ngx")
	commit=$(git rev-parse HEAD)
	branch=$(git branch --show-current)
	build_id=$(strings vmlinux | grep '^Linux version ' | tail -n1 || true)
	record "$run" action build
	record "$run" git_commit "$commit"
	record "$run" git_branch "$branch"
	record "$run" image_sha256 "$hash"
	record "$run" kernel_build "$build_id"
	say "image: $run/zImage.ngx"
	say "sha256: $hash"
	finish "Build complete: $test_id"
}

deploy_image()
{
	local image=$1 test_id=$2 boot root run image_hash card_hash name old_hash
	[[ -f "$image" ]] || die "image not found: $image"
	safe_id "$test_id"
	boot=$(ensure_mount "$BOOT_LABEL" "$BOOT_MOUNT")
	root=$(ensure_mount "$ROOT_LABEL" "$ROOT_MOUNT")
	run=$(new_run "$test_id")
	image_hash=$(hash_file "$image")
	cp "$image" "$run/zImage.ngx"
	record "$run" action deploy
	record "$run" image_source "$(readlink -f "$image")"
	record "$run" image_sha256 "$image_hash"
	record "$run" boot_device "$(device_for_label "$BOOT_LABEL")"
	record "$run" root_device "$(device_for_label "$ROOT_LABEL")"

	for name in "${LOG_NAMES[@]}"; do
		old_hash=$(hash_file "$root/$name")
		record "$run" "pre_$name" "$old_hash"
	done

	say "deploying $image_hash"
	cp "$image" "$boot/gumboot/zImage.ngx"
	sync "$boot/gumboot/zImage.ngx"
	card_hash=$(hash_file "$boot/gumboot/zImage.ngx")
	[[ "$card_hash" == "$image_hash" ]] || \
		die "card verification failed: $card_hash != $image_hash"
	record "$run" card_sha256 "$card_hash"
	record "$run" deployed_utc "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	say "verified card image: $card_hash"
	say "run: $run"
	finish "Deployment complete: $test_id"
}

collect_logs()
{
	local requested=${1:-} root run test_id name old_hash new_hash changed=0 marked=0
	run=$(latest_dir)
	test_id=$(manifest_get "$run" test_id)
	[[ -z "$requested" || "$requested" == "$test_id" ]] || \
		die "latest run is $test_id, not $requested"
	root=$(ensure_mount "$ROOT_LABEL" "$ROOT_MOUNT")
	mkdir -p "$run/logs"

	for name in "${LOG_NAMES[@]}"; do
		old_hash=$(manifest_get "$run" "pre_$name")
		new_hash=$(hash_file "$root/$name")
		printf 'wii-cycle: %-18s old=%s new=%s\n' "$name" "$old_hash" "$new_hash"
		if [[ "$new_hash" != missing && "$new_hash" != "$old_hash" ]]; then
			cp "$root/$name" "$run/logs/$name"
			changed=$((changed + 1))
		fi
	done

	if [[ -f "$run/logs/early-dmesg.txt" ]] && \
		grep -Fq "wii_test=$test_id" "$run/logs/early-dmesg.txt"; then
		marked=1
	fi
	record "$run" collected_utc "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	record "$run" changed_logs "$changed"
	record "$run" marker_seen "$marked"

	(( changed > 0 )) || die 'no diagnostic log changed; kernel did not prove PID 1 entry'
	(( marked == 1 )) || die "fresh logs do not contain wii_test=$test_id"
	say "accepted fresh, self-identified logs in $run/logs"
	grep -E 'Linux version|Kernel command line|init-diag: final logs' \
		"$run/logs/early-dmesg.txt" || true
	finish "Logs collected: $test_id"
}

show_status()
{
	local run name root
	printf '%s\n' '--- card ---'
	lsblk -o NAME,RO,RM,FSTYPE,LABEL,MOUNTPOINTS | \
		grep -E "(^NAME|$BOOT_LABEL|$ROOT_LABEL)" || true
	if [[ -f "$STATE_ROOT/latest" ]]; then
		run=$(latest_dir)
		printf '\n--- latest run: %s ---\n' "$run"
		cat "$run/manifest.tsv"
	fi
	root=$ROOT_MOUNT
	if findmnt -rn --target "$root" >/dev/null 2>&1; then
		printf '\n--- current logs ---\n'
		for name in "${LOG_NAMES[@]}"; do
			printf '%-18s %s\n' "$name" "$(hash_file "$root/$name")"
		done
	fi
}

case ${1:-} in
	build)
		[[ $# == 2 ]] || { usage; exit 2; }
		build_image "$2"
		;;
	stage)
		[[ $# == 2 ]] || { usage; exit 2; }
		build_image "$2"
		build_run=$(latest_dir)
		deploy_image "$build_run/zImage.ngx" "$2"
		;;
	deploy)
		[[ $# == 3 ]] || { usage; exit 2; }
		deploy_image "$2" "$3"
		;;
	artifacts)
		[[ $# == 3 ]] || { usage; exit 2; }
		stage_artifacts "$2" "$3"
		;;
	collect)
		[[ $# -le 2 ]] || { usage; exit 2; }
		collect_logs "${2:-}"
		;;
	status)
		[[ $# == 1 ]] || { usage; exit 2; }
		show_status
		;;
	*)
		usage
		exit 2
		;;
esac

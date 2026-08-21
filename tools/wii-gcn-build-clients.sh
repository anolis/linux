#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

repo=$(git rev-parse --show-toplevel)
output_dir=${1:-/tmp}
headers_dir=$(mktemp -d)
vnc_prefix=${WII_LIBVNCSERVER_PREFIX:-}

cleanup()
{
	rm -rf "$headers_dir"
}
trap cleanup EXIT

mkdir -p "$output_dir"

make -C "$repo" -j16 ARCH=powerpc \
	INSTALL_HDR_PATH="$headers_dir" headers_install

common_flags=(
	-static
	-O2
	-Wall
	-Wextra
	-Werror
	-I"$headers_dir/include"
)
shell_flags=(-I"$repo/include")
shell_libraries=()

if [[ -n $vnc_prefix ]]; then
	if [[ ! -f $vnc_prefix/include/rfb/rfb.h ||
	      ! -f $vnc_prefix/lib/libvncserver.a ]]; then
		echo "invalid WII_LIBVNCSERVER_PREFIX: $vnc_prefix" >&2
		exit 1
	fi
	shell_flags+=(
		-DWII_HAVE_VNC
		-I"$vnc_prefix/include"
	)
	shell_libraries+=("$vnc_prefix/lib/libvncserver.a")
fi

powerpc-linux-gnu-gcc "${common_flags[@]}" \
	"$repo/tools/wii-gcn-render-test.c" \
	-o "$output_dir/wii-gcn-render-test"
powerpc-linux-gnu-gcc "${common_flags[@]}" \
	"$repo/tools/wii-gcn-kms-render-test.c" \
	-o "$output_dir/wii-gcn-kms-render-test"
powerpc-linux-gnu-gcc "${common_flags[@]}" \
	"$repo/tools/wii-gcn-kms-flip-test.c" \
	-o "$output_dir/wii-gcn-kms-flip-test"
powerpc-linux-gnu-gcc "${common_flags[@]}" \
	"$repo/tools/wii-drm-test.c" \
	-o "$output_dir/wii-drm-test"
powerpc-linux-gnu-gcc "${common_flags[@]}" \
	"${shell_flags[@]}" \
	"$repo/tools/wii-kolibri-shell.c" \
	"${shell_libraries[@]}" \
	-o "$output_dir/wii-kolibri-shell"

sha256sum "$output_dir/wii-gcn-render-test" \
	"$output_dir/wii-gcn-kms-render-test" \
	"$output_dir/wii-gcn-kms-flip-test" \
	"$output_dir/wii-drm-test" \
	"$output_dir/wii-kolibri-shell"

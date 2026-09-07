#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

repo=$(git rev-parse --show-toplevel)
output_dir=${1:-/tmp}
headers_dir=$(mktemp -d)
build_dir="$headers_dir/build"

cleanup()
{
	rm -rf "$headers_dir"
}
trap cleanup EXIT

mkdir -p "$output_dir"
mkdir -p "$build_dir"

make -C "$repo" O="$build_dir" -j16 ARCH=powerpc \
	INSTALL_HDR_PATH="$headers_dir" headers_install

common_flags=(
	-static
	-O2
	-Wall
	-Wextra
	-Werror
	-I"$headers_dir/include"
)

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

sha256sum "$output_dir/wii-gcn-render-test" \
	"$output_dir/wii-gcn-kms-render-test" \
	"$output_dir/wii-gcn-kms-flip-test" \
	"$output_dir/wii-drm-test"

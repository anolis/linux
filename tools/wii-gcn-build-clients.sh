#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0

set -euo pipefail

repo=$(git rev-parse --show-toplevel)
output_dir=${1:-/tmp}
headers_dir=$(mktemp -d)

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

powerpc-linux-gnu-gcc "${common_flags[@]}" \
	"$repo/tools/wii-gcn-render-test.c" \
	-o "$output_dir/wii-gcn-render-test"
powerpc-linux-gnu-gcc "${common_flags[@]}" \
	"$repo/tools/wii-gcn-kms-render-test.c" \
	-o "$output_dir/wii-gcn-kms-render-test"

sha256sum "$output_dir/wii-gcn-render-test" \
	"$output_dir/wii-gcn-kms-render-test"

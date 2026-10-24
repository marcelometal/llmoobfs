#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Run only inside the disposable guest created by issue118-client-mcp-host.sh.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
export PATH

[ "$#" -eq 3 ] || exit 64
out=$1 projects=$2 pages=$3
disk=/dev/vda
mount_dir=/tmp/moob118-mcp
base=$mount_dir/aim2
repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
client=/home/debian/clients/ai-memory/target/release/ai-memory
[ -b "$disk" ] && [ "$(blkid -s LABEL -o value "$disk")" = moob118mcp ] || {
    echo 'AIMEM_MCP_FAIL: expected disposable disk missing' >&2
    exit 1
}
[ -f "$out/moobfs_layer.ko" ] && [ -x "$out/query" ] && [ -x "$client" ] || exit 1
[ ! -e "$mount_dir" ] || { echo 'AIMEM_MCP_FAIL: mount path exists' >&2; exit 1; }
mkdir "$mount_dir"
mount -t ext4 "$disk" "$mount_dir"
cleanup() {
    umount "$mount_dir" 2>/dev/null || true
}
trap cleanup EXIT
[ ! -e "$base" ] || exit 1
insmod "$out/moobfs_layer.ko"
BASE="$base" AIM_BIN="$client" QUERY="$out/query" LATENCY=/nonexistent BULK=0 \
    sh "$repo/bench/client/stage2.sh" "$out/client" "$projects" "$pages"
expected=$((projects * pages))
grep -Fq "written=$expected refused=0 " "$out/client/workload.txt" || {
    echo 'AIMEM_MCP_FAIL: expected MCP writes missing or refused' >&2
    exit 1
}
grep -Eq '^MIRROR: pages=[0-9]+ mirrored=[0-9]+ missing_files=0 ' "$out/client/mirror.txt" || {
    echo 'AIMEM_MCP_FAIL: mirror result missing' >&2
    exit 1
}
grep -Eq '^COMPARE: projects=[0-9]+ checks=[1-9][0-9]* failed=0 ' "$out/client/compare.txt" || {
    echo 'AIMEM_MCP_FAIL: comparison result missing' >&2
    exit 1
}
printf 'AIMEM_MCP_PASS: projects=%s pages_per_project=%s expected_writes=%s\n' \
    "$projects" "$pages" "$expected"

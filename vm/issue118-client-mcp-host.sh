#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Opt-in real ai-memory MCP workload in a disposable native-KVM guest.
# Usage: sh vm/issue118-client-mcp-host.sh RUN_ID PROJECTS PAGES_PER_PROJECT
set -eu
PATH="$HOME/.venv-vng/bin:/usr/sbin:/sbin:$PATH"
export PATH

[ "$#" -eq 3 ] || { echo 'usage: issue118-client-mcp-host.sh RUN_ID PROJECTS PAGES_PER_PROJECT' >&2; exit 64; }
run_id=$1 projects=$2 pages=$3
case "$run_id" in ''|*[!a-zA-Z0-9_-]*) echo 'invalid run id' >&2; exit 64 ;; esac
case "$projects:$pages" in *[!0-9:]*|:*|*:) echo 'invalid client size' >&2; exit 64 ;; esac
[ "$projects" -ge 1 ] && [ "$projects" -le 10 ] &&
    [ "$pages" -ge 1 ] && [ "$pages" -le 600 ] || exit 64
command -v vng >/dev/null || { echo 'vng unavailable' >&2; exit 1; }
command -v qemu-system-x86_64 >/dev/null || { echo 'QEMU unavailable' >&2; exit 1; }
if pgrep -f '[q]emu-system' >/dev/null; then
    echo 'another QEMU VM is active' >&2
    exit 1
fi

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
client=/home/debian/clients/ai-memory/target/release/ai-memory
kernel=$(uname -r)
[ -x "$client" ] && [ -d "/lib/modules/$kernel/build" ] || {
    echo 'pinned client binary or matching kernel headers unavailable' >&2
    exit 1
}
out="$repo/build/issue118-client-mcp/$run_id"
mkdir -p "$repo/build/issue118-client-mcp"
mkdir "$out" || { echo 'run output already exists' >&2; exit 1; }
make -C "$repo" layer KDIR="/lib/modules/$kernel/build" > "$out/build.log" 2>&1 || {
    echo 'module build failed; see build.log' >&2
    exit 1
}
cc -static -O2 -Wall -Wextra -Werror -I"$repo/include" \
    "$repo/tests/query.c" "$repo/common/attr.c" -o "$out/query"
cp "$repo/layer/moobfs_layer.ko" "$out/moobfs_layer.ko"
truncate -s 2G "$out/lower.img"
mke2fs -q -F -t ext4 -L moob118mcp "$out/lower.img"
{
    printf 'repo_head='; git -C "$repo" rev-parse HEAD
    printf 'client_head='; git -C /home/debian/clients/ai-memory rev-parse HEAD
    printf 'kernel=%s\nprojects=%s\npages_per_project=%s\n' "$kernel" "$projects" "$pages"
    "$client" --version
    sha256sum "$out/moobfs_layer.ko" "$out/query" "$out/lower.img"
} > "$out/environment.txt"

rc=0
vng -r --user root --cpus 2 --memory 2G --disk "$out/lower.img" \
    --rwdir "$out" \
    --exec "sh $repo/vm/issue118-client-mcp-guest.sh $out $projects $pages" \
    > "$out/console.log" 2>&1 || rc=$?
grep -E '^(W-AIM:|MIRROR:|COMPARE:|AIMEM_MCP_)' "$out/console.log" || true
[ "$rc" -eq 0 ] || { echo 'VM command failed; see console.log' >&2; exit 1; }
grep -q '^AIMEM_MCP_PASS:' "$out/console.log" || {
    echo 'guest success marker absent; see console.log' >&2
    exit 1
}
! grep -q '^AIMEM_MCP_FAIL:' "$out/console.log"

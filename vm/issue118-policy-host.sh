#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Opt-in, native-KVM issue-118 probe on a dedicated Linux host.
# Usage: vm/issue118-policy-host.sh RUN_ID FILES READ_DELAY_MS [plain|six] [current|prelimit] [SEED_RUN]
# Each invocation creates a fresh image and output directory; nothing is overwritten.
set -eu
PATH="$HOME/.venv-vng/bin:/usr/sbin:/sbin:$PATH"
export PATH

[ "$#" -ge 3 ] && [ "$#" -le 6 ] || {
    echo 'usage: issue118-policy-host.sh RUN_ID FILES READ_DELAY_MS [plain|six] [current|prelimit] [SEED_RUN]' >&2
    exit 64
}
run_id=$1 files=$2 delay_ms=$3 shape=${4:-plain} module=${5:-current} seed_run=${6:-}
case "$run_id" in ''|*[!a-zA-Z0-9_-]*) echo 'invalid run id' >&2; exit 64 ;; esac
case "$files" in ''|*[!0-9]*) echo 'invalid file count' >&2; exit 64 ;; esac
case "$delay_ms" in ''|*[!0-9]*) echo 'invalid read delay' >&2; exit 64 ;; esac
[ "$files" -ge 1 ] && [ "$files" -le 30000 ] || exit 64
[ "$delay_ms" -ge 1 ] && [ "$delay_ms" -le 20 ] || exit 64
case "$shape" in plain|six) ;; *) echo 'invalid shape' >&2; exit 64 ;; esac
case "$module" in current|prelimit) ;; *) echo 'invalid module selection' >&2; exit 64 ;; esac
case "$seed_run" in *[!a-zA-Z0-9_-]*) echo 'invalid seed run' >&2; exit 64 ;; esac
[ "$seed_run" != "$run_id" ] || { echo 'seed and output run IDs must differ' >&2; exit 64; }
command -v vng >/dev/null || { echo 'vng unavailable' >&2; exit 1; }
command -v qemu-system-x86_64 >/dev/null || { echo 'QEMU unavailable' >&2; exit 1; }
if pgrep -f '[q]emu-system' >/dev/null; then
    echo 'another QEMU VM is active' >&2
    exit 1
fi

repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
kernel=$(uname -r)
[ -d "/lib/modules/$kernel/build" ] || { echo 'matching kernel headers unavailable' >&2; exit 1; }
if [ -n "$seed_run" ]; then
    seed="$repo/build/issue118-policy/$seed_run"
    [ -f "$seed/lower.img" ] &&
        grep -Fxq "files=$files" "$seed/environment.txt" &&
        grep -Fxq "shape=$shape" "$seed/environment.txt" || {
        echo 'seed image, file count or shape mismatch' >&2
        exit 1
    }
fi
out="$repo/build/issue118-policy/$run_id"
mkdir -p "$repo/build/issue118-policy"
mkdir "$out" || { echo 'run output already exists' >&2; exit 1; }

if [ "$module" = prelimit ]; then
    source_ref=f283f13
    mkdir "$out/prelimit-source"
    git -C "$repo" archive --format=tar -o "$out/prelimit-source.tar" "$source_ref"
    tar -xf "$out/prelimit-source.tar" -C "$out/prelimit-source"
    source_repo="$out/prelimit-source"
else
    source_ref=HEAD
    source_repo=$repo
fi
make -C "$source_repo" layer KDIR="/lib/modules/$kernel/build" > "$out/build.log" 2>&1 || {
    echo 'module build failed; see build.log' >&2
    exit 1
}
cc -static -O2 -Wall -Wextra -Werror -I"$repo/include" \
    "$repo/tests/query-build-limit.c" -o "$out/query-build-limit"
cc -static -O2 -Wall -Wextra -Werror -I"$repo/include" \
    "$repo/tests/query-build-denial.c" -o "$out/query-build-denial"
cp "$source_repo/layer/moobfs_layer.ko" "$out/moobfs_layer.ko"
if [ -n "$seed_run" ]; then
    cp --reflink=auto "$seed/lower.img" "$out/lower.img"
    prepare=reuse
else
    if [ "$shape" = six ]; then disk_size=256M; else disk_size=128M; fi
    truncate -s "$disk_size" "$out/lower.img"
    mke2fs -q -F -t ext4 -b 4096 -N 50000 -L moob118policy "$out/lower.img"
    prepare=plant
fi
{
    printf 'repo_head='; git -C "$repo" rev-parse HEAD
    printf 'module_ref='; git -C "$repo" rev-parse "$source_ref"
    printf 'kernel=%s\nfiles=%s\nread_delay_ms=%s\nshape=%s\nmodule=%s\nseed_run=%s\n' \
        "$kernel" "$files" "$delay_ms" "$shape" "$module" "$seed_run"
    qemu-system-x86_64 --version | head -n 1
    sha256sum "$out/moobfs_layer.ko" "$out/query-build-limit" \
        "$out/query-build-denial" "$out/lower.img"
} > "$out/environment.txt"

rc=0
vng -r --user root --cpus 2 --memory 2G --disk "$out/lower.img" \
    --rwdir "$out" \
    --exec "sh $repo/vm/issue118-policy-guest.sh $out $files $delay_ms $shape $prepare" \
    > "$out/console.log" 2>&1 || rc=$?
grep '^MOOBFS_POLICY_' "$out/console.log" || true
[ "$rc" -eq 0 ] || { echo 'VM command failed; see console.log' >&2; exit 1; }
grep -q '^MOOBFS_POLICY_PASS:' "$out/console.log" || {
    echo 'guest success marker absent; see console.log' >&2
    exit 1
}
! grep -q '^MOOBFS_POLICY_FAIL:' "$out/console.log"

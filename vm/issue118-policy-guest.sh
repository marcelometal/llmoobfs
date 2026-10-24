#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Run only inside a disposable virtme-ng guest with one newly made ext4 disk.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
export PATH

[ "$#" -eq 4 ] || exit 64
out=$1 files=$2 delay_ms=$3 shape=$4
case "$shape" in plain) plant=plant ;; six) plant=plant6 ;; *) exit 64 ;; esac
disk=/dev/vda
lower=/tmp/moob118-lower
layer=/tmp/moob118-layer
mapper=moob118slow
[ -b "$disk" ] && [ "$(blkid -s LABEL -o value "$disk")" = moob118policy ] || {
    echo 'MOOBFS_POLICY_FAIL: expected disposable disk missing' >&2
    exit 1
}
[ -f "$out/moobfs_layer.ko" ] && [ -x "$out/query-build-limit" ] &&
    [ -x "$out/query-build-denial" ] || exit 1
mkdir -p "$lower" "$layer"
test ! -e "/dev/mapper/$mapper"

cleanup() {
    umount "$layer" 2>/dev/null || true
    umount "$lower" 2>/dev/null || true
    dmsetup remove "$mapper" 2>/dev/null || true
}
trap cleanup EXIT

mount -t ext4 "$disk" "$lower"
mkdir -p "$lower/base/d/s"
"$out/query-build-limit" "$plant" "$lower/base/d/s" "$files"
sync
umount "$lower"
insmod "$out/moobfs_layer.ko"

measure() {
    condition=$1 device=$2
    sync
    echo 3 > /proc/sys/vm/drop_caches
    mount -t ext4 "$device" "$lower"
    mount -t moobfs_layer -o "lower=$lower/base,domain_depth=1" none "$layer"
    result=$("$out/query-build-denial" "$layer/d" 1001)
    printf 'MOOBFS_POLICY_RESULT: files=%s shape=%s condition=%s read_delay_ms=%s %s\n' \
        "$files" "$shape" "$condition" "$delay_ms" "$result"
    umount "$layer"
    umount "$lower"
}

measure direct "$disk"
modprobe dm-delay
sectors=$(blockdev --getsz "$disk")
dmsetup create "$mapper" --table \
    "0 $sectors delay $disk 0 $delay_ms $disk 0 0"
dmsetup table "$mapper"
measure delayed "/dev/mapper/$mapper"
dmsetup remove "$mapper"
echo "MOOBFS_POLICY_PASS: files=$files shape=$shape read_delay_ms=$delay_ms"

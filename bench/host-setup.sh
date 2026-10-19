#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# bench/host-setup: prepares an x86_64 Linux host (Ubuntu 24.04 or Debian 13, kernel from
# bench/kernel-build.sh already booted) to validate the layer and run the M4 campaign.
# Formats three dedicated block devices, mounts them, builds the module and the tools, and
# runs the full suite (vm/suite.sh) directly on the host as the validation gate.
#
#   sudo sh bench/host-setup.sh --ext4 /dev/sdc --btrfs /dev/sdd --xfs /dev/sde [--scratch FILE]
# Every device given is wiped. --scratch and the log device are 256 MiB loop files under
# /var/tmp unless devices are given with --scratch-dev/--log-dev. Prints HOST_SETUP_PASS at
# the end; the suite log is build/host/suite.log.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
ext4=""; btrfs=""; xfs=""; scratch=""; logdev=""; skip_suite=0
while [ $# -gt 0 ]; do
    case $1 in
        --ext4) ext4=$2; shift 2 ;;
        --btrfs) btrfs=$2; shift 2 ;;
        --xfs) xfs=$2; shift 2 ;;
        --scratch-dev) scratch=$2; shift 2 ;;
        --log-dev) logdev=$2; shift 2 ;;
        --skip-suite) skip_suite=1; shift ;;
        *) echo "host-setup: unknown option $1" >&2; exit 64 ;;
    esac
done
[ -n "$ext4" ] && [ -n "$btrfs" ] && [ -n "$xfs" ] || { echo "usage: see the header of bench/host-setup.sh" >&2; exit 64; }
[ "$(id -u)" = 0 ] || { echo "host-setup: run as root" >&2; exit 1; }
for d in $ext4 $btrfs $xfs; do [ -b "$d" ] || { echo "host-setup: $d is not a block device" >&2; exit 1; }; done
case $(uname -r) in *moobfs-bench*) ;; *) echo "host-setup: warning: kernel $(uname -r) is not the benchmark kernel (bench/kernel-build.sh)" >&2 ;; esac

echo "== packages"
if command -v apt-get > /dev/null; then
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential e2fsprogs btrfs-progs xfsprogs sqlite3 libsqlite3-dev git dmsetup acl python3 fio \
        linux-perf 2> /dev/null || DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential e2fsprogs btrfs-progs xfsprogs sqlite3 libsqlite3-dev git dmsetup acl python3 fio
fi

echo "== filesystems"
mke2fs -q -F -t ext4 -b 4096 -L moob-lower "$ext4"
mkfs.btrfs -q -f -L moob-lower "$btrfs"
mkfs.xfs -q -f -L moob-lower "$xfs"
if [ -z "$scratch" ]; then
    truncate -s 256M /var/tmp/moob-scratch.img; scratch=$(losetup -f --show /var/tmp/moob-scratch.img)
fi
if [ -z "$logdev" ]; then
    truncate -s 256M /var/tmp/moob-log.img; logdev=$(losetup -f --show /var/tmp/moob-log.img)
fi

echo "== build"
cd "$here"
BUILD=build/host
mkdir -p "$BUILD"   # a fresh checkout or copy has no build/ yet
make layer KDIR="/lib/modules/$(uname -r)/build" > "$BUILD.layer.log" 2>&1 || { tail -20 "$BUILD.layer.log"; exit 1; }
make attr-tools bench-tools BUILD=$BUILD CFLAGS="-O2 -g" > /dev/null
gcc -O2 -Wall -Wextra -Werror -Iinclude tests/query.c common/attr.c -o $BUILD/query
gcc -O2 -Wall -Wextra -Werror tests/layer.c -o $BUILD/layer
gcc -O2 -Wall -Wextra -Werror tests/hostile.c -o $BUILD/hostile
gcc -O2 -Wall -Wextra -Werror tests/idmap.c -o $BUILD/idmap
gcc -O2 -Itests/third_party/fsx tests/third_party/fsx/fsx.c -o $BUILD/fsx
gcc -O2 tests/third_party/log-writes/replay-log.c tests/third_party/log-writes/log-writes.c -o $BUILD/replay-log
cp layer/moobfs_layer.ko $BUILD/
cp bench/queries-w3.txt $BUILD/bench-queries-w3.txt
for f in sqlbase equiv run; do cp bench/$f.sh "$BUILD/bench-$f.sh"; done
cp bench/cpu-freq.sh "$BUILD/cpu-freq.sh"   # bench-run.sh records the CPU frequency state with it
# the suite finds host tools under $TOOLS; on a distro host they live in the usual places
mkdir -p $BUILD/tools
for t in git sqlite3 dmsetup mke2fs e2fsck setfacl getfacl; do ln -sf "$(command -v $t)" "$BUILD/tools/$t"; done

echo "== module"
rmmod moobfs_layer 2> /dev/null || true   # the suite loads and unloads the module itself
if [ "$skip_suite" -eq 1 ]; then
    insmod $BUILD/moobfs_layer.ko && grep -q moobfs_layer /proc/filesystems
    echo "HOST_SETUP_PASS: suite skipped; module loaded"; exit 0
fi
echo "== suite (validation gate on this kernel)"
mkdir -p /mnt/moob-lower /mnt/moob-layer
BIN=$here/$BUILD TOOLS=$here/$BUILD/tools MODS=/nonexistent LOWER=/mnt/moob-lower LAYER=/mnt/moob-layer
DEV_EXT4=$ext4 DEV_BTRFS=$btrfs DEV_XFS=$xfs DEV_SCRATCH=$scratch DEV_LOG=$logdev
export BIN TOOLS MODS LOWER LAYER DEV_EXT4 DEV_BTRFS DEV_XFS DEV_SCRATCH DEV_LOG
dmesg -C 2> /dev/null || true
sh vm/suite.sh 2>&1 | tee $BUILD/suite.log
dmesg > $BUILD/suite-dmesg.txt
if grep -qE 'BUG:|Oops:|WARNING:|KASAN|UBSAN' $BUILD/suite-dmesg.txt; then echo "HOST_SETUP_FAIL: kernel diagnostics in dmesg"; exit 1; fi
grep -q '^MOOBFS_LAYER_PASS' $BUILD/suite.log && grep -q '^MOOBFS_ACCEPTANCE_PASS' $BUILD/suite.log || { echo "HOST_SETUP_FAIL: suite"; exit 1; }
echo "HOST_SETUP_PASS: kernel $(uname -r); ext4=$ext4 btrfs=$btrfs xfs=$xfs; suite log $BUILD/suite.log"

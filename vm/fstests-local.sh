#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Runs fstests over the layer (security roadmap S2) on a local Linux host with KVM, in a
# virtme-ng guest of the host's running kernel, so a crash stays in the guest. fstests does not
# know the layer: vm/fstests/moobfs_layer.patch teaches it the type, in the places where it
# already handles filesystems without a block device (virtiofs, 9p).
#
#   sh vm/fstests-local.sh FSTESTS_SRC [LOWERFS] [check arguments...]
#
# FSTESTS_SRC is a built checkout of xfstests-dev at the tag named in vm/fstests/README.md;
# LOWERFS is ext4 (default), btrfs or xfs; the check arguments default to "-g quick".
# Run from the repository root as a user in the kvm group, with vng in PATH and the headers of
# the running kernel installed. The console goes to build/fstests/console-LOWERFS.log and the
# results stay in build/fstests/tree/results. Exit status 0 when ./check ran to the end, whatever
# the tests said: the verdict per test is vm/fstests/README.md's business.
set -eu
PATH=$PATH:/usr/sbin:/sbin
SRC=$1; LOWERFS=${2:-ext4}
if [ $# -ge 2 ]; then shift 2; else shift 1; fi
ARGS=${*:--g quick}
R=$(pwd)
[ -f vm/fstests-guest.sh ] || { echo "fstests-local: run from the repository root" >&2; exit 1; }
[ -x "$SRC/check" ] && [ -x "$SRC/ltp/fsstress" ] || { echo "fstests-local: $SRC is not a built fstests tree" >&2; exit 1; }
command -v vng >/dev/null || { echo "fstests-local: vng (virtme-ng) not in PATH" >&2; exit 1; }
make layer KDIR="/lib/modules/$(uname -r)/build" > build.fstests-layer.log 2>&1 || { tail -20 build.fstests-layer.log; exit 1; }
mkdir -p build/fstests
rm -rf build/fstests/tree
cp -a "$SRC" build/fstests/tree
(cd build/fstests/tree && git apply "$R/vm/fstests/moobfs_layer.patch" 2>/dev/null || patch -p1 -N -s < "$R/vm/fstests/moobfs_layer.patch" || true)
grep -q moobfs_layer build/fstests/tree/common/config || { echo "fstests-local: the patch did not apply" >&2; exit 1; }
for d in base-test base-scratch; do
    rm -f "build/fstests/$d.img"; truncate -s 8G "build/fstests/$d.img"
    case "$LOWERFS" in
    ext4) mke2fs -q -F -t ext4 "build/fstests/$d.img" ;;
    btrfs) mkfs.btrfs -q -f "build/fstests/$d.img" ;;
    xfs) mkfs.xfs -q -f "build/fstests/$d.img" ;;
    *) echo "fstests-local: unknown lower filesystem $LOWERFS" >&2; exit 1 ;;
    esac
done
printf 'R=%s\nFST=%s\nLOWERFS=%s\nARGS="%s"\n' "$R" "$R/build/fstests/tree" "$LOWERFS" "$ARGS" > build/fstests/env.sh
vng -r --user root --cpus 4 --memory 4G --verbose \
    --rwdir "$R/build" --overlay-rwdir /etc --overlay-rwdir /home --overlay-rwdir /var/tmp \
    --disk "$R/build/fstests/base-test.img" --disk "$R/build/fstests/base-scratch.img" \
    --exec "sh $R/vm/fstests-guest.sh" > "build/fstests/console-$LOWERFS.log" 2>&1 || true
grep -E '^MOOBFS_FSTESTS|^Ran:|^Not run:|^Failures:|^Failed [0-9]+ of|^Passed all' "build/fstests/console-$LOWERFS.log" | cut -c1-2000 | tail -12
grep -q '^MOOBFS_FSTESTS_DONE' "build/fstests/console-$LOWERFS.log"

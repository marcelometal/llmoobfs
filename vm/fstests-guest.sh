#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Guest side of vm/fstests-local.sh: mounts the two base filesystems, loads the layer and runs
# fstests' ./check over it. Expects in the environment (written by the host script into env.sh):
#   R        repository copy with layer/moobfs_layer.ko built for this kernel
#   FST      writable fstests tree with vm/fstests/moobfs_layer.patch applied and built
#   LOWERFS  ext4, btrfs or xfs
#   ARGS     arguments of ./check (for instance "-g quick")
set -u
PATH=$PATH:/usr/sbin:/sbin; export PATH
. "$(dirname "$0")/../build/fstests/env.sh"
fail() { echo "MOOBFS_FSTESTS_ENV_FAIL: $1"; exit 1; }
B=/tmp/moob
mkdir -p $B/base-test $B/base-scratch $B/test $B/scratch || fail mkdir
modprobe "$LOWERFS" 2>/dev/null || true
mount -t "$LOWERFS" /dev/vda $B/base-test || fail "mount base test"
mount -t "$LOWERFS" /dev/vdb $B/base-scratch || fail "mount base scratch"
insmod "$R/layer/moobfs_layer.ko" || fail insmod
# The accounts fstests uses for permission tests. /etc is a writable overlay in the guest, but the
# guest's root reads the host's files as the user who started it: /etc/gshadow cannot be read, so
# useradd fails. The accounts are written directly; virtme-ng already gives the guest its own
# /etc/shadow.
rm -f /etc/gshadow && : > /etc/gshadow || fail "gshadow"
uid=2001
for u in fsgqa fsgqa2 123456-fsgqa; do
    if ! grep -q "^$u:" /etc/passwd; then
        echo "$u:x:$uid:$uid::/home/$u:/bin/bash" >> /etc/passwd || fail "passwd $u"
        echo "$u:x:$uid:" >> /etc/group || fail "group $u"
        echo "$u:*:19000:0:99999:7:::" >> /etc/shadow || fail "shadow $u"
        mkdir -p "/home/$u" && chown "$uid:$uid" "/home/$u" || fail "home $u"
    fi
    uid=$((uid + 1))
done
[ "$(su fsgqa -c 'id -un' 2>&1)" = fsgqa ] || fail "su to fsgqa: $(su fsgqa -c 'id -un' 2>&1)"
cat > "$FST/local.config" <<CFG
export FSTYP=moobfs_layer
export TEST_DEV=moobtest
export TEST_DIR=$B/test
export SCRATCH_DEV=moobscratch
export SCRATCH_MNT=$B/scratch
export TEST_FS_MOUNT_OPTS="-o lower=$B/base-test,domain_depth=0"
export MOUNT_OPTIONS="-o lower=$B/base-scratch,domain_depth=0"
CFG
mount -t moobfs_layer -o "lower=$B/base-test,domain_depth=0" moobtest $B/test || fail "mount layer"
dmesg -C 2>/dev/null || true
cd "$FST" || fail "cd fstests"
# shellcheck disable=SC2086  # ARGS is a list of ./check arguments
./check $ARGS
echo "MOOBFS_FSTESTS_CHECK_EXIT: $?"
if dmesg | grep -E 'BUG:|kernel BUG|Oops:|\] WARNING:|^WARNING:|Kernel panic|KASAN:|possible circular locking'; then
    echo "MOOBFS_FSTESTS_KERNEL_DIAGNOSTIC"
fi
echo "MOOBFS_FSTESTS_DONE: lower=$LOWERFS args=$ARGS"

#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Run inside the project's moobfs-m1-test Docker image with this tree at /work.
# Modules are loaded only by the disposable QEMU guest. Each run uses a new directory.
set -eu
cd /work
KERNEL=6.12.111+deb13-amd64
RUN=$(mktemp -d /work/build/review/vm.XXXXXX)
STAGE=$RUN/stage
mkdir -p "$STAGE/bin" "$STAGE/dev" "$STAGE/proc" "$STAGE/sys" "$STAGE/tmp" "$STAGE/mods"
chmod 0755 "$STAGE"
cp /bin/busybox "$STAGE/bin/"
ln -s busybox "$STAGE/bin/sh"
make layer KDIR="/lib/modules/$KERNEL/build" > "$RUN/build.log" 2>&1
cp layer/moobfs_layer.ko "$STAGE/"
gcc -static -O2 -Wall -Wextra -Werror -Iinclude tests/review-build-permissions.c \
    -o "$STAGE/review-build-permissions"
python3 - "$KERNEL" "$STAGE/mods" <<'PY'
import lzma
import sys
from pathlib import Path
base = Path("/lib/modules") / sys.argv[1]
deps = {}
for line in (base / "modules.dep").read_text().splitlines():
    key, _, value = line.partition(":")
    deps[key] = value.split()
order = []
def visit(name):
    for dep in deps.get(name, []):
        visit(dep)
    if name not in order:
        order.append(name)
for name in ("kernel/crypto/crc32c_generic.ko.xz",
             "kernel/drivers/block/virtio_blk.ko.xz",
             "kernel/fs/ext4/ext4.ko.xz"):
    visit(name)
out = Path(sys.argv[2])
with (out / "order").open("w") as listing:
    for name in order:
        target = Path(name).name.removesuffix(".xz")
        (out / target).write_bytes(lzma.decompress((base / name).read_bytes()))
        listing.write(target + "\n")
PY
cat > "$STAGE/init" <<'INIT'
#!/bin/sh
/bin/busybox --install -s /bin
export PATH=/bin
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
fail() { echo "REVIEW_VM_FAIL: $*"; poweroff -f; }
while read -r name; do insmod "/mods/$name" || fail "$name"; done < /mods/order
mkdir -p /lower /layer /trace
mount -t ext4 /dev/vda /lower || fail ext4
insmod /moobfs_layer.ko || fail moobfs_layer
mount -t moobfs_layer -o lower=/lower none /layer || fail layer
mount -t tracefs tracefs /trace || fail tracefs
echo 1 > /trace/events/moobfs/moobfs_scan_reread/enable
echo "CONTROL: chmod after completed build"
echo 0 > /sys/module/moobfs_layer/parameters/scan_delay_us
/review-build-permissions /layer/control 300000
echo "CONTROL_EXIT=$?"
echo "RACE: chmod during first build, existing delay hook"
echo 1000000 > /sys/module/moobfs_layer/parameters/scan_delay_us
/review-build-permissions /layer/race 300000
echo "RACE_EXIT=$?"
echo "TRACE_BEGIN"
cat /trace/trace
echo "TRACE_END"
umount /layer || fail unmount-layer
rmmod moobfs_layer || fail unload
umount /lower || fail unmount-lower
echo "DMESG_BEGIN"
dmesg
echo "DMESG_END"
echo "REVIEW_VM_DONE"
poweroff -f
INIT
chmod +x "$STAGE/init"
truncate -s 64M "$RUN/lower.img"
mke2fs -q -F -t ext4 -b 4096 "$RUN/lower.img"
(cd "$STAGE" && find . -print0 | sort -z | cpio --null -o --format=newc 2>/dev/null | gzip -n) > "$RUN/initramfs.cpio.gz"
sha256sum layer/moobfs_layer.ko "/boot/vmlinuz-$KERNEL" > "$RUN/hashes.txt"
timeout 120 qemu-system-x86_64 -machine q35 -accel tcg -cpu max -m 512M -smp 2 \
    -drive "file=$RUN/lower.img,format=raw,if=virtio" \
    -kernel "/boot/vmlinuz-$KERNEL" -initrd "$RUN/initramfs.cpio.gz" \
    -append "console=ttyS0 rdinit=/init panic=-1 loglevel=4" \
    -nographic -no-reboot -nic none > "$RUN/console.log" 2>&1
echo "$RUN"
grep -E 'CONTROL|RACE|QUERY |REVOKED|REVIEW_|moobfs_scan_reread:' "$RUN/console.log"
grep -q '^REVIEW_VM_DONE' "$RUN/console.log"
grep -q 'CONTROL_EXIT=0' "$RUN/console.log"
grep -q 'RACE_EXIT=1' "$RUN/console.log"
grep -q 'REVIEW_RESULT uid=1001 first=1 after=1 getxattr=-1 errno=13' "$RUN/console.log"
grep -q 'moobfs_scan_reread:' "$RUN/console.log"

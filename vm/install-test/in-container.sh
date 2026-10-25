#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Runs inside the moobfs-install-test container (vm/install-test/run.sh). Boots the Debian 13
# generic cloud image under QEMU with a throwaway overlay, configures it through cloud-init
# NoCloud over HTTP, and drives vm/install-test/guest.sh over SSH across two reboots.
set -eu
cd /work
OUT=build/install-test
IMG_DATE=20261021-2638
IMG=debian-13-generic-amd64-$IMG_DATE.qcow2
URL=https://cloud.debian.org/images/cloud/trixie/$IMG_DATE/$IMG
# SHA-512 from that directory's SHA512SUMS, read on 2026-10-25
SUM=70ef6adc1c16155f8d575731b37d0f976d1c60b6144ac3f57914a3f4b30b202b69584d5d143aa39da0f0fe81df9e5b698909c0d94c7d91c6f6c4bb6581ead5f0
LOG=$OUT/log.txt
: >"$LOG"
log() { echo "$*" | tee -a "$LOG"; }

if [ ! -f "$OUT/$IMG" ]; then
	curl -fsSL -o "$OUT/$IMG.part" "$URL"
	mv "$OUT/$IMG.part" "$OUT/$IMG"
fi
echo "$SUM  $OUT/$IMG" | sha512sum -c - >/dev/null || { log "MOOBFS_INSTALL_FAIL: image checksum"; exit 1; }

# A fresh overlay every run: the base image is never written.
rm -f "$OUT/disk.qcow2" "$OUT"/console-*.log
qemu-img create -q -f qcow2 -F qcow2 -b "$IMG" "$OUT/disk.qcow2" 16G

# The key stays in the container: a bind mount from macOS may not keep mode 0600.
SEED=$(mktemp -d)
KEY=$SEED/key
ssh-keygen -q -t ed25519 -N '' -f "$KEY"
QPID=
HPID=
cleanup() {
	[ -z "$QPID" ] || kill "$QPID" 2>/dev/null || true
	[ -z "$HPID" ] || kill "$HPID" 2>/dev/null || true
	rm -rf "$SEED"
}
trap cleanup EXIT
printf 'instance-id: moobfs-install-test\nlocal-hostname: moobfs-test\n' >"$SEED/meta-data"
cat >"$SEED/user-data" <<EOF
#cloud-config
users:
  - name: tester
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash
    ssh_authorized_keys:
      - $(cat "$KEY.pub")
package_update: false
package_upgrade: false
EOF
python3 -m http.server 8000 --bind 127.0.0.1 --directory "$SEED" >/dev/null 2>&1 &
HPID=$!

boot() {
	qemu-system-x86_64 -accel tcg,thread=multi -cpu max -smp 4 -m 4096 \
		-drive file="$OUT/disk.qcow2",if=virtio,format=qcow2 \
		-smbios 'type=1,serial=ds=nocloud;s=http://10.0.2.2:8000/' \
		-nic user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:2222-:22 \
		-display none -serial "file:$OUT/console-$1.log" &
	QPID=$!
}

SSH="ssh -q -i $KEY -p 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10 tester@127.0.0.1"
wait_ssh() {
	i=0
	while ! $SSH true 2>/dev/null; do
		i=$((i + 1))
		[ "$i" -lt 180 ] || { log "MOOBFS_INSTALL_FAIL: no ssh after boot $1"; exit 1; }
		sleep 10
	done
	log "BOOTED: $1 after about $((i * 10)) s"
}
# The guest reboots itself; QEMU keeps running across a guest reboot.
reboot_guest() {
	$SSH 'sudo systemctl reboot' || true
	sleep 30
	wait_ssh "$1"
}
phase() {
	$SSH "sudo sh guest.sh $1" 2>&1 | tee -a "$LOG" || true
	grep -q "PHASE_DONE: $1" "$LOG" || { log "MOOBFS_INSTALL_FAIL: phase $1"; exit 1; }
}

START=$(date +%s)
boot 1
wait_ssh 1
scp -q -i "$KEY" -P 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	"$OUT/src.tar" vm/install-test/guest.sh tester@127.0.0.1:
phase 0
reboot_guest 2
phase 1
reboot_guest 3
phase 2
log "ELAPSED: $(($(date +%s) - START)) s under TCG emulation"
log "MOOBFS_INSTALL_PASS: docs/operations.md followed on Debian 13; refused 6.12.113, installed on 6.12.111, mounted by hand, by fstab at boot and by a mount unit, two users isolated, uninstalled with the lower readable"

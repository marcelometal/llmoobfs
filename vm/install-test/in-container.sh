#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Runs inside the moobfs-install-test container (vm/install-test/run.sh). Boots the Debian 13
# generic cloud image under QEMU with a throwaway overlay, configures it through cloud-init
# NoCloud over HTTP, and drives a guest script over SSH, rebooting between its phases.
#   MOOB_TEST=install (default)  vm/install-test/guest.sh, phases 0 1 2 (roadmap C2)
#   MOOB_TEST=container          vm/container-test/guest.sh, phases 0 1 (roadmap C3)
# With /dev/kvm writable the guest runs under KVM, otherwise under TCG emulation. MOOB_WORK is the
# repository root (default /work, the container's bind mount; run.sh sets it for MOOB_HOST_QEMU=1).
set -eu
cd "${MOOB_WORK:-/work}"
if [ -w /dev/kvm ]; then
	ACCEL="-accel kvm -cpu host" ACCEL_NAME=KVM
else
	ACCEL="-accel tcg,thread=multi -cpu max" ACCEL_NAME="TCG emulation"
fi
case ${MOOB_TEST:-install} in
install) OUT=build/install-test GUEST=vm/install-test/guest.sh PHASES="0 1 2" NAME=INSTALL ;;
container) OUT=build/container-test GUEST=vm/container-test/guest.sh PHASES="0 1" NAME=CONTAINER ;;
*) echo "unknown MOOB_TEST ${MOOB_TEST}" >&2; exit 64 ;;
esac
CACHE=build/install-test	# the base image is shared by both tests
mkdir -p "$OUT" "$CACHE"
IMG_DATE=20261021-2638
IMG=debian-13-generic-amd64-$IMG_DATE.qcow2
URL=https://cloud.debian.org/images/cloud/trixie/$IMG_DATE/$IMG
# SHA-512 from that directory's SHA512SUMS, read on 2026-10-25
SUM=70ef6adc1c16155f8d575731b37d0f976d1c60b6144ac3f57914a3f4b30b202b69584d5d143aa39da0f0fe81df9e5b698909c0d94c7d91c6f6c4bb6581ead5f0
LOG=$OUT/log.txt
: >"$LOG"
log() { echo "$*" | tee -a "$LOG"; }

if [ ! -f "$CACHE/$IMG" ]; then
	curl -fsSL -o "$CACHE/$IMG.part" "$URL"
	mv "$CACHE/$IMG.part" "$CACHE/$IMG"
fi
echo "$SUM  $CACHE/$IMG" | sha512sum -c - >/dev/null || { log "MOOBFS_${NAME}_FAIL: image checksum"; exit 1; }

# A fresh overlay every run: the base image is never written.
rm -f "$OUT/disk.qcow2" "$OUT"/console-*.log
qemu-img create -q -f qcow2 -F qcow2 -b "$(pwd)/$CACHE/$IMG" "$OUT/disk.qcow2" 16G

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
	# shellcheck disable=SC2086   # $ACCEL holds several options
	qemu-system-x86_64 $ACCEL -smp 4 -m 4096 \
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
		[ "$i" -lt 180 ] || { log "MOOBFS_${NAME}_FAIL: no ssh after boot $1"; exit 1; }
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
	grep -q "PHASE_DONE: $1" "$LOG" || { log "MOOBFS_${NAME}_FAIL: phase $1"; exit 1; }
}

START=$(date +%s)
boot 1
wait_ssh 1
cp "$GUEST" "$SEED/guest.sh"
scp -q -i "$KEY" -P 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
	"$OUT/src.tar" "$SEED/guest.sh" tester@127.0.0.1:
n=1
for p in $PHASES; do
	if [ "$n" -gt 1 ]; then
		reboot_guest "$n"
	fi
	phase "$p"
	n=$((n + 1))
done
log "ELAPSED: $(($(date +%s) - START)) s under $ACCEL_NAME"
case $NAME in
INSTALL) log "MOOBFS_INSTALL_PASS: docs/operations.md followed on Debian 13; refused an unlisted kernel release, installed on the image's 6.12.113, mounted by hand, by fstab at boot and by a mount unit, two users isolated, uninstalled with the lower readable" ;;
CONTAINER) log "MOOBFS_CONTAINER_PASS: docs/container-integration.md followed on Debian 13 with Docker and Podman" ;;
esac

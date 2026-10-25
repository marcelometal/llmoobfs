#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Runs as root inside the disposable Debian 13 guest of vm/install-test/. Each phase follows
# docs/operations.md with the same commands, in the same order, and prints markers.
#   phase 0: stock kernel; install refused; switch to the listed kernel (reboot follows)
#   phase 1: listed kernel; install, load, mount, users, fstab (reboot follows)
#   phase 2: mounted at boot; mount unit; uninstall; lower still readable
set -eu
cd /home/tester
SRC=/home/tester/llmoobfs
KVER=6.12.111+deb13-amd64
mark() { echo "$*"; }
fail() { echo "MOOBFS_INSTALL_FAIL: $*"; exit 1; }
as() { u=$1; shift; runuser -u "$u" -- "$@"; }   # sudo -u in the guide; runuser needs no tty

phase0() {
	mark "STOCK_KERNEL: $(uname -r)"
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -q >/dev/null
	# section 2, packages (headers for the stock kernel, as a user would first try)
	apt-get install -y -q build-essential git attr acl "linux-headers-$(uname -r)" >/dev/null
	mkdir -p "$SRC" && tar -xf src.tar -C "$SRC" && chown -R tester:tester "$SRC"
	cd "$SRC"
	if make install >install-stock.log 2>&1; then
		fail "make install accepted the unlisted kernel $(uname -r)"
	fi
	grep -q 'is not supported' install-stock.log || fail "refusal without the expected message"
	mark "UNSUPPORTED_REFUSED: $(uname -r)"
	# informational only: does it even compile there? Staged, never loaded.
	if MOOB_ALLOW_UNSUPPORTED=1 DESTDIR=/tmp/stage make install >install-forced.log 2>&1; then
		mark "UNSUPPORTED_STAGED_BUILD: compiled on $(uname -r), not loaded, not tested"
	else
		mark "UNSUPPORTED_STAGED_BUILD_FAILED: $(tail -n 3 install-forced.log | tr '\n' ' ')"
	fi
	make -s clean -C "/lib/modules/$(uname -r)/build" M="$SRC/layer" >/dev/null 2>&1 || true
	# section 1, switch to the listed kernel
	apt-get install -y -q "linux-image-$KVER=6.12.111-1" "linux-headers-$KVER=6.12.111-1" >/dev/null
	sed -i 's/^GRUB_DEFAULT=.*/GRUB_DEFAULT="Advanced options for Debian GNU\/Linux>Debian GNU\/Linux, with Linux 6.12.111+deb13-amd64"/' /etc/default/grub
	update-grub >/dev/null 2>&1
	mark "PHASE_DONE: 0"
}

phase1() {
	[ "$(uname -r)" = "$KVER" ] || fail "booted $(uname -r), not $KVER (GRUB_DEFAULT did not take)"
	mark "LISTED_KERNEL: $(uname -r)"
	cd "$SRC"
	make install >install.log 2>&1 || { tail -n 20 install.log; fail "make install"; }
	mark "INSTALLED: $(ls /lib/modules/$KVER/updates/moobfs/) $(echo /usr/local/bin/moobfs-*)"
	if make install >install-again.log 2>&1; then fail "second install overwrote without FORCE"; fi
	grep -q 'exists; uninstall first' install-again.log || fail "second install: unexpected message"
	mark "OVERWRITE_REFUSED"
	modprobe moobfs_layer
	grep -q 'moobfs_layer' /proc/filesystems || fail "type not registered"
	mark "LOADED: $(grep moobfs_layer /proc/filesystems | tr '\t' ' ')"
	# section 3
	install -d -m 0700 /srv/moobfs
	install -d -m 0755 /srv/moobfs/lower
	install -d -m 0755 /srv/memory
	mount -t moobfs_layer -o lower=/srv/moobfs/lower,nolinks none /srv/memory
	findmnt -n /srv/memory >/dev/null || fail "manual mount"
	mark "MANUAL_MOUNT: $(findmnt -n -o FSTYPE,OPTIONS /srv/memory)"
	# section 5
	useradd -m alice
	useradd -m bob
	install -d -o alice -g alice -m 0700 /srv/memory/alice
	as alice sh -c 'echo "use the staging cluster" > /srv/memory/alice/note.md'
	as alice moobfs-attr set /srv/memory/alice/note.md kind string decision
	[ "$(as alice moobfs-attr list /srv/memory/alice/note.md)" = "$(printf 'kind\tstring\tdecision')" ] ||
		fail "attribute not read back"
	if as bob cat /srv/memory/alice/note.md 2>/dev/null; then fail "bob read alice's file"; fi
	if as bob moobfs-attr list /srv/memory/alice/note.md 2>/dev/null; then fail "bob read alice's attributes"; fi
	if as bob ls /srv/memory/alice 2>/dev/null; then fail "bob listed alice's directory"; fi
	if as bob sh -c 'echo x > /srv/memory/alice/x' 2>/dev/null; then fail "bob wrote in alice's directory"; fi
	mark "USERS_ISOLATED: alice reads and tags her note; bob cannot read, list or write"
	if as alice ln -s note.md /srv/memory/alice/link 2>/dev/null; then fail "nolinks did not refuse"; fi
	mark "NOLINKS_REFUSED"
	if as bob cat /srv/moobfs/lower/alice/note.md 2>/dev/null; then fail "lower reachable by a user"; fi
	mark "LOWER_HIDDEN"
	# section 4, fstab
	umount /srv/memory
	echo 'none  /srv/memory  moobfs_layer  lower=/srv/moobfs/lower,nolinks,x-systemd.requires-mounts-for=/srv/moobfs/lower  0  0' >>/etc/fstab
	systemctl daemon-reload
	mount /srv/memory
	findmnt -n /srv/memory >/dev/null || fail "fstab mount"
	mark "FSTAB_MOUNT"
	# leave the module unloaded so the boot proves the autoload through the fs alias
	umount /srv/memory
	rmmod moobfs_layer
	sync
	mark "PHASE_DONE: 1"
}

phase2() {
	findmnt -n /srv/memory >/dev/null || fail "not mounted at boot"
	mark "BOOT_MOUNT: $(findmnt -n -o FSTYPE,OPTIONS /srv/memory); module $(grep -c '^moobfs_layer ' /proc/modules) loaded"
	[ "$(as alice moobfs-attr get /srv/memory/alice/note.md kind)" = "$(printf 'kind\tstring\tdecision')" ] ||
		fail "attribute lost across reboot"
	mark "SURVIVED_REBOOT"
	moobfs-check /srv/memory >check.json || fail "moobfs-check exit $?"
	mark "CHECK_CLEAN"
	# the mount unit variant of section 4
	umount /srv/memory
	sed -i '\|^none  */srv/memory  *moobfs_layer|d' /etc/fstab
	cat >/etc/systemd/system/srv-memory.mount <<'EOF'
[Unit]
Description=MoobFS layer over /srv/moobfs/lower
RequiresMountsFor=/srv/moobfs/lower

[Mount]
What=none
Where=/srv/memory
Type=moobfs_layer
Options=lower=/srv/moobfs/lower,nolinks

[Install]
WantedBy=multi-user.target
EOF
	systemctl daemon-reload && systemctl enable --now srv-memory.mount >/dev/null 2>&1
	findmnt -n /srv/memory >/dev/null || fail "mount unit"
	mark "UNIT_MOUNT"
	systemctl disable --now srv-memory.mount >/dev/null 2>&1
	rm /etc/systemd/system/srv-memory.mount
	systemctl daemon-reload
	# section 7
	cd "$SRC"
	if make uninstall >uninstall-loaded.log 2>&1; then fail "uninstall ran with the module loaded"; fi
	mark "UNINSTALL_REFUSED_WHILE_LOADED"
	findmnt -n /srv/memory >/dev/null && fail "still mounted"
	rmmod moobfs_layer
	make uninstall >uninstall.log 2>&1 || { cat uninstall.log; fail "make uninstall"; }
	if modinfo moobfs_layer >/dev/null 2>&1; then fail "module still installed"; fi
	[ ! -e /usr/local/bin/moobfs-attr ] || fail "tools still installed"
	mark "UNINSTALLED"
	getfattr -d -m '^user\.moob\.' /srv/moobfs/lower/alice/note.md | grep -q '^user.moob.kind=' ||
		fail "attribute not on the lower"
	grep -q 'staging cluster' /srv/moobfs/lower/alice/note.md || fail "content not on the lower"
	mark "LOWER_READABLE: content and user.moob.kind without the module"
	mark "PHASE_DONE: 2"
}

case ${1:-} in
0) phase0 ;;
1) phase1 ;;
2) phase2 ;;
*) fail "usage: guest.sh 0|1|2" ;;
esac

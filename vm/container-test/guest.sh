#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Runs as root inside the disposable Debian 13 guest of vm/install-test/ with MOOB_TEST=container
# (roadmap item C3). Follows docs/container-integration.md with Docker and Podman and prints
# markers; a recipe that is documented as refused must be refused.
#   phase 0: packages, Docker, Podman and the listed kernel (reboot follows)
#   phase 1: install and mount as docs/operations.md, then every container case
set -eu
cd /home/tester
SRC=/home/tester/llmoobfs
KVER=6.12.111+deb13-amd64
# the image of vm/Dockerfile and vm/install-test/Dockerfile (multi-architecture index digest)
IMG=docker.io/library/debian@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f
mark() { echo "$*"; }
fail() { echo "MOOBFS_CONTAINER_FAIL: $*"; exit 1; }
as() { u=$1; shift; runuser -u "$u" -- "$@"; }

phase0() {
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -q >/dev/null
	apt-get install -y -q build-essential git attr acl docker.io podman uidmap \
		"linux-image-$KVER=6.12.111-1" "linux-headers-$KVER=6.12.111-1" >/dev/null
	mkdir -p "$SRC" && tar -xf src.tar -C "$SRC"
	sed -i 's/^GRUB_DEFAULT=.*/GRUB_DEFAULT="Advanced options for Debian GNU\/Linux>Debian GNU\/Linux, with Linux 6.12.111+deb13-amd64"/' /etc/default/grub
	update-grub >/dev/null 2>&1
	mark "RUNTIMES: $(docker --version | cut -d, -f1); $(podman --version)"
	mark "PHASE_DONE: 0"
}

# run <runtime> <uid:gid> <run options...> -- <shell command>: one container from IMG with the
# MoobFS tools bound read-only, no network; prints its output, returns its status
run() {
	rt=$1 user=$2
	shift 2
	opts=
	while [ "$1" != -- ]; do opts="$opts $1"; shift; done
	shift
	# shellcheck disable=SC2086   # $opts holds several run options
	$rt run --rm --network none --user "$user" \
		-v /usr/local/bin/moobfs:/usr/local/bin/moobfs:ro \
		-v /usr/local/bin/moobfs-attr:/usr/local/bin/moobfs-attr:ro \
		$opts "$IMG" sh -c "$1"
}

cases() {
	rt=$1
	# 1. the recipe: the agent's domain directory, read-write, under the owner's numeric uid
	out=$(run "$rt" 1001:1001 -v /srv/memory/alice:/memory -- '
		echo "written in a container" > /memory/c-'"$rt"'.md &&
		moobfs-attr set /memory/c-'"$rt"'.md kind string decision &&
		moobfs-attr list /memory/c-'"$rt"'.md') || fail "$rt: write through the bind mount"
	[ "$out" = "$(printf 'kind\tstring\tdecision')" ] || fail "$rt: attribute read back: $out"
	[ "$(stat -c %u "/srv/moobfs/lower/alice/c-$rt.md")" = 1001 ] || fail "$rt: owner on the lower"
	getfattr -n user.moob.kind "/srv/moobfs/lower/alice/c-$rt.md" >/dev/null 2>&1 || fail "$rt: attribute on the lower"
	run "$rt" 1001:1001 -v /srv/memory/alice:/memory -- 'moobfs-attr rawset /memory/note.md kind 01ff' >/dev/null 2>&1 &&
		fail "$rt: an invalid typed value was accepted through the bind mount"
	mark "BIND_RW_$rt: read, write and setxattr through the domain bind; invalid value refused by the kernel"
	# QUERY2 through the bind of the domain directory itself
	out=$(run "$rt" 1001:1001 -v /srv/memory/alice:/memory -- 'moobfs query /memory kind eq string decision' 2>&1) ||
		fail "$rt: QUERY2 through the domain bind: $out"
	printf '%s\n' "$out" | grep -qx note.md && printf '%s\n' "$out" | grep -qx "c-$rt.md" ||
		fail "$rt: QUERY2 through the domain bind answered: $out"
	mark "QUERY2_DOMAIN_BIND_$rt: answered inside the container ($(printf '%s\n' "$out" | wc -l) results)"
	# GET_INFO needs the mount root, which the domain bind does not reach
	rc=0
	out=$(run "$rt" 1001:1001 -v /srv/memory/alice:/memory -- 'moobfs info /memory' 2>&1) || rc=$?
	[ "$rc" -eq 69 ] && printf '%s\n' "$out" | grep -q 'bind mount of /alice' || fail "$rt: info through the domain bind: $rc $out"
	mark "INFO_DOMAIN_BIND_$rt: refused with exit 69, as documented"
	# 2. a second agent under another uid sees only its own directory
	out=$(run "$rt" 1002:1002 -v /srv/memory/bob:/memory -- 'ls -A /memory; moobfs query /memory kind eq string decision') ||
		fail "$rt: bob's container"
	[ "$out" = "$(printf 'bob.md\nbob.md')" ] || fail "$rt: bob's container saw: $out"
	mark "SECOND_AGENT_$rt: uid 1002 sees only its own directory"
	# 3. a wrong uid: the kernel's permissions still hold inside the container
	rc=0
	run "$rt" 1002:1002 -v /srv/memory/alice:/memory -- 'cat /memory/note.md' >/dev/null 2>&1 || rc=$?
	[ "$rc" -ne 0 ] || fail "$rt: uid 1002 read alice's note through her bind"
	rc=0
	run "$rt" 1002:1002 -v /srv/memory/alice:/memory -- 'moobfs query /memory kind eq string decision' >/dev/null 2>&1 || rc=$?
	[ "$rc" -eq 77 ] || fail "$rt: uid 1002 querying alice's bind: exit $rc, not 77"
	mark "WRONG_UID_$rt: alice's directory under uid 1002 is refused (read and query, exit 77)"
	# 4. the alternative of distribution strategy 2.5: the whole root read-only, beside the domain
	out=$(run "$rt" 1001:1001 -v /srv/memory:/all:ro -v /srv/memory/alice:/memory -- '
		moobfs query /all/alice kind eq string decision | grep -qx note.md || exit 11
		moobfs info /all | grep -q "directories 1 level below /all" || exit 12
		touch /all/alice/x 2>/dev/null && exit 13
		moobfs query /all/bob kind eq string decision >/dev/null 2>&1; [ $? -eq 77 ] || exit 14
		moobfs query /all kind eq string decision >/dev/null 2>&1; [ $? -eq 69 ] || exit 15
		ls /all' 2>&1) || fail "$rt: read-only root bind (step $?): $out"
	[ "$(printf '%s\n' "$out" | sort | tr '\n' ' ')" = "alice bob " ] || fail "$rt: read-only root lists: $out"
	mark "RO_ROOT_BIND_$rt: QUERY2 and GET_INFO work through it; writes refused; bob's domain refused; top-level names visible"
}

phase1() {
	[ "$(uname -r)" = "$KVER" ] || fail "booted $(uname -r), not $KVER"
	cd "$SRC"
	make install >install.log 2>&1 || { tail -n 20 install.log; fail "make install"; }
	modprobe moobfs_layer
	install -d -m 0700 /srv/moobfs
	install -d -m 0755 /srv/moobfs/lower
	install -d -m 0755 /srv/memory
	mount -t moobfs_layer -o lower=/srv/moobfs/lower,nolinks none /srv/memory
	useradd -m -u 1001 alice
	useradd -m -u 1002 bob
	install -d -o alice -g alice -m 0700 /srv/memory/alice
	install -d -o bob -g bob -m 0700 /srv/memory/bob
	as alice sh -c 'echo "use the staging cluster" > /srv/memory/alice/note.md'
	as alice moobfs-attr set /srv/memory/alice/note.md kind string decision
	as bob sh -c 'echo "bob only" > /srv/memory/bob/bob.md'
	as bob moobfs-attr set /srv/memory/bob/bob.md kind string decision
	mark "HOST_READY: /srv/memory mounted; alice 1001 and bob 1002 have their domains"
	docker pull -q "$IMG" >/dev/null || fail "docker pull"
	podman pull -q "$IMG" >/dev/null || fail "podman pull"
	cases docker
	cases podman
	# 5. user namespaces: without an idmapped mount the files keep their host owners, so a
	# container whose uid 1001 maps to another host uid is refused by plain permissions
	rc=0
	podman run --rm --network none --uidmap 0:200000:65536 --user 1001:1001 \
		-v /srv/memory/alice:/memory "$IMG" sh -c 'cat /memory/note.md' >/dev/null 2>&1 || rc=$?
	[ "$rc" -ne 0 ] || fail "a remapped uid 1001 read alice's note"
	mark "USERNS_REMAP_REFUSED: a remapped uid 1001 cannot read alice's files"
	# An idmapped bind (:idmap). The layer does not set FS_ALLOW_IDMAP, so mount_setattr must
	# fail; what Podman then does is observed: refused, or a plain bind without translation.
	# Translated owners (alice's files readable by the remapped uid 1001) would be a kernel
	# finding for the core front.
	rc=0
	out=$(podman run --rm --network none --uidmap 0:200000:65536 --user 1001:1001 \
		-v /srv/memory/alice:/memory:idmap "$IMG" sh -c '
		grep " /memory " /proc/self/mountinfo
		stat -c "owner=%u:%g" /memory/note.md
		cat /memory/note.md >/dev/null 2>&1 && echo read=yes || echo read=no' 2>&1) || rc=$?
	mark "IDMAP_BIND_OBSERVED: exit $rc: $(printf '%s' "$out" | tr '\n' ' ' | cut -c1-400)"
	if [ "$rc" -eq 0 ] && printf '%s\n' "$out" | grep -q 'read=yes'; then
		fail "an idmapped bind translated the owners: alice's note was read by a remapped uid"
	fi
	if [ "$rc" -eq 0 ] && printf '%s\n' "$out" | grep ' /memory ' | grep -q idmapped; then
		fail "the bind of the layer is marked idmapped in mountinfo"
	fi
	mark "IDMAP_NO_TRANSLATION: no idmapped mount of the layer took effect (exit $rc)"
	# 6. rootless Podman run by alice herself, keeping her uid: reported, not required
	loginctl enable-linger alice
	sleep 2
	rc=0
	out=$(runuser -l alice -c "XDG_RUNTIME_DIR=/run/user/1001 podman pull -q $IMG >/dev/null && \
		XDG_RUNTIME_DIR=/run/user/1001 podman run --rm --network none --userns=keep-id \
		-v /srv/memory/alice:/memory -v /usr/local/bin/moobfs:/usr/local/bin/moobfs:ro \
		$IMG moobfs query /memory kind eq string decision" 2>&1) || rc=$?
	if [ "$rc" -eq 0 ] && printf '%s\n' "$out" | grep -qx note.md; then
		mark "ROOTLESS_KEEP_ID: alice's rootless Podman with --userns=keep-id queried her domain"
	else
		mark "ROOTLESS_KEEP_ID_NOT_WORKING: exit $rc: $(printf '%s' "$out" | tail -n 2 | tr '\n' ' ')"
	fi
	umount /srv/memory
	mark "PHASE_DONE: 1"
}

case ${1:-} in
0) phase0 ;;
1) phase1 ;;
*) fail "usage: guest.sh 0|1" ;;
esac

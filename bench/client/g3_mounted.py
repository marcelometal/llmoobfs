#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""G3 restore drill. Run as root on the dedicated Linux host.

Copies the G1 lower tree with bench/client/g3_restore.py, then checks the
protocol's restore conditions: bytes, typed attributes, identity, QUERY2
answers, moobfs-check, a read without the module, an illegal type byte, a
format marker newer than version 1, and a second run onto a non-empty
destination. The drill removes only paths whose names contain g3-0186.
"""
import hashlib
import os
import stat
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import g3_restore
from g1_mounted import ALICE, BOB, ensure_users, mount_layer, search, umount
from g1_protocol import identity

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def fail(message):
    raise SystemExit(f"G3_MOUNTED_FAIL {message}")


def check(cond, message):
    if not cond:
        fail(message)


def owned(path):
    check("g3-0186" in path, f"refusing to remove {path}")
    if os.path.ismount(path):
        umount(path)
    if os.path.lexists(path):
        subprocess.check_call(["rm", "-rf", path])


def snapshot(root):
    rows = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in dirnames + filenames:
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, root)
            st = os.lstat(path)
            digest = None
            if stat.S_ISREG(st.st_mode):
                h = hashlib.sha256()
                with open(path, "rb") as fh:
                    for chunk in iter(lambda: fh.read(1 << 20), b""):
                        h.update(chunk)
                digest = h.hexdigest()
            attrs = {}
            for attr in os.listxattr(path, follow_symlinks=False):
                if attr.startswith("user.moob.") or attr.startswith("system.posix_acl"):
                    attrs[attr] = os.getxattr(path, attr, follow_symlinks=False)
            rows[rel] = (st.st_uid, st.st_gid, stat.S_IMODE(st.st_mode), digest,
                         tuple(sorted(attrs.items())))
    return rows


def answers(mnt, alice_gid, bob_gid, team_gid):
    groups = [team_gid]
    out = {}
    for name, uid, gid in (("alice", ALICE, alice_gid), ("bob", BOB, bob_gid),
                           ("team", ALICE, alice_gid)):
        items, _calls = search(mnt, uid, gid, groups, name, None, 10000)
        out[name] = {identity(item) for item in items}
    for uid, gid, who in ((ALICE, alice_gid, "alice"), (BOB, bob_gid, "bob")):
        got, _calls = search(mnt, uid, gid, groups, "team", {"kind": "secret"}, 10)
        out["secret-" + who] = [identity(item) for item in got]
    return out


def main():
    check(sys.platform == "linux" and os.geteuid() == 0, "run as root on Linux")
    src = "/srv/moob/ext4/g1-0189-lower"
    live = "/mnt/g1-0189"
    dest = "/srv/moob/ext4/g3-0186-restore"
    dest_mnt = "/mnt/g3-0186"
    bad = "/srv/moob/ext4/g3-0186-bad"
    fmt = "/srv/moob/ext4/g3-0186-format"
    fmt_mnt = "/mnt/g3-0186-format"
    checker = os.path.join(ROOT, "build/host/moobfs-check")
    check(os.path.isdir(src), "G1 lower is absent")
    check(os.path.isfile(checker), "moobfs-check is not built")
    team_gid, alice_gid, bob_gid = ensure_users()
    umount("/mnt/g1-0189-budget")
    if not os.path.ismount(live):
        mount_layer(src, live, "")
    before = answers(live, alice_gid, bob_gid, team_gid)
    check(before["secret-alice"] == [] and before["secret-bob"] == [], "salary visible before restore")
    check(len(before["team"]) >= 5000, f"team oracle {len(before['team'])}")
    umount(live)
    original = snapshot(src)
    for path in (dest, dest_mnt, bad, fmt, fmt_mnt):
        owned(path)
    os.makedirs(dest_mnt)
    os.makedirs(fmt_mnt)
    sys.argv = ["g3_restore.py", src, dest]
    g3_restore.main()
    copied = snapshot(dest)
    if original != copied:
        missing = sorted(set(original) - set(copied))[:5]
        extra = sorted(set(copied) - set(original))[:5]
        differed = [key for key in original if key in copied and original[key] != copied[key]][:3]
        print(f"G3_DIFF missing={missing} extra={extra}")
        for key in differed:
            print("G3_DIFF", key)
            print(" src", original[key][:4], "attrs", [n for n, _v in original[key][4]])
            print(" dst", copied[key][:4], "attrs", [n for n, _v in copied[key][4]])
        fail("restored tree differs in bytes, attributes or identity")
    clean = subprocess.run([checker, dest], capture_output=True, text=True)
    check(clean.returncode == 0, f"moobfs-check {clean.returncode} {clean.stdout[-400:]}")
    sample = os.path.join(dest, "alice", "m00000.json")
    body = open(sample, "rb").read()
    kind = os.getxattr(sample, "user.moob.v.kind")
    check(b"kind" in body and kind[:1] == b"\x01", "plain read failed")
    print("G3_PLAIN cat and getfattr read the restored tree")
    mount_layer(dest, dest_mnt, "")
    after = answers(dest_mnt, alice_gid, bob_gid, team_gid)
    umount(dest_mnt)
    check(after == before, "QUERY2 answers differ after restore")
    print("G3_QUERY2 answers and salary denials match")
    os.makedirs(bad)
    bad_file = os.path.join(bad, "m00000.json")
    open(bad_file, "wb").write(body)
    os.setxattr(bad_file, "user.moob.v.bad", bytes([9]) + b"no")
    broken = subprocess.run([checker, bad], capture_output=True, text=True)
    check(broken.returncode == 2, f"illegal type exit {broken.returncode}")
    print("G3_CHECK illegal type byte exit 2")
    os.makedirs(fmt)
    os.setxattr(fmt, "user.moob.format", b"2")
    refused = subprocess.run(
        ["mount", "-t", "moobfs_layer", "-o", f"lower={fmt},domain_depth=1", "none", fmt_mnt],
        capture_output=True, text=True)
    if os.path.ismount(fmt_mnt):
        umount(fmt_mnt)
        fail("newer format marker mounted")
    check(refused.returncode != 0, "newer format marker was accepted")
    print("G3_FORMAT marker 2 refused")
    digest = hashlib.sha256(open(os.path.join(dest, "team", "m00000.json"), "rb").read()).hexdigest()
    sentinel = os.path.join(dest, "sentinel-keep")
    open(sentinel, "w").write("keep")
    again = subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "g3_restore.py"),
                            src, dest], capture_output=True, text=True)
    check(again.returncode == 2, f"second run exit {again.returncode} {again.stderr}")
    check("not empty" in again.stderr, again.stderr)
    now = hashlib.sha256(open(os.path.join(dest, "team", "m00000.json"), "rb").read()).hexdigest()
    check(now == digest and open(sentinel).read() == "keep", "second run wrote into the destination")
    print("G3_SECOND_RUN refused")
    print("G3_MOUNTED_PASS")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Record whether one user's build releases another user's domain.

This is the G2 baseline on the current module. It does not pass G2: there is
no latency ceiling, and fair shares are not implemented. A result is printed
and the process exits 0 when the counters were read. Exit 3 when both sizes
still fit in the 1 MiB budget, so the count is not raised further.

Two mounts, both index_budget=1M and domain_depth=1. The cross-user mount
has alice and bob. The same-uid mount has two domains of alice. Each domain
gets the same number of files. The first size is 2000; if nothing is released
and every search answers, one retry uses 4000 and then stops.
"""
import os
import pwd
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import langgraph_store as store
from g1_mounted import (ALICE, BOB, as_user, ensure_users, get_info, mount_layer, search, umount)

VALUE = {"kind": "decision", "flag": True, "score": 0.25, "note": "n"}


def fail(message):
    raise SystemExit(f"G2_BASELINE_FAIL {message}")


def check(cond, message):
    if not cond:
        fail(message)


def owned(path):
    check("g2-018" in path, f"refusing to remove {path}")
    if os.path.ismount(path):
        umount(path)
    if os.path.lexists(path):
        subprocess.check_call(["rm", "-rf", path])


def prepare(root, name, uid, gid, mode):
    path = os.path.join(root, name)
    os.makedirs(path, exist_ok=True)
    os.chown(path, uid, gid)
    os.chmod(path, mode)


def plant(mnt, uid, gid, groups, name, n):
    def work():
        s = store.MoobStore(mnt, domain_depth=1)
        for i in range(n):
            s.put((name,), f"m{i:05d}", VALUE)
            if (i + 1) % 500 == 0:
                print(f"G2_PLANT {name} {i + 1}/{n}", flush=True)

    as_user(uid, gid, groups, work)


def ask(mnt, uid, gid, groups, name):
    try:
        items, calls = search(mnt, uid, gid, groups, name, None, 10000)
    except store.MoobQueryError as err:
        return None, err.errno
    return len(items), None


def line(info):
    return (f"budget={info.index_budget} used={info.index_used} inodes={info.index_inodes} "
            f"built={info.domains_built} builds={info.domain_builds} releases={info.domain_releases}")


def scenario(lower, mnt, n, cross, alice_gid, bob_gid):
    owned(lower)
    owned(mnt)
    mount_layer(lower, mnt, "index_budget=1M")
    os.chmod(mnt, 0o755)
    groups = []
    if cross:
        prepare(mnt, "alice", ALICE, alice_gid, 0o700)
        prepare(mnt, "bob", BOB, bob_gid, 0o700)
        plant(mnt, ALICE, alice_gid, groups, "alice", n)
        first, first_err = ask(mnt, ALICE, alice_gid, groups, "alice")
        after_first = get_info(mnt)
        print(f"G2_CROSS n={n} after_alice count={first} errno={first_err} {line(after_first)}",
              flush=True)
        plant(mnt, BOB, bob_gid, groups, "bob", n)
        second, second_err = ask(mnt, BOB, bob_gid, groups, "bob")
        after_second = get_info(mnt)
        print(f"G2_CROSS n={n} after_bob count={second} errno={second_err} {line(after_second)}",
              flush=True)
        again, again_err = ask(mnt, ALICE, alice_gid, groups, "alice")
        after_again = get_info(mnt)
        print(f"G2_CROSS n={n} alice_again count={again} errno={again_err} {line(after_again)}",
              flush=True)
        released = after_again.domain_releases > after_first.domain_releases
        answered = first == n and second == n and again == n and not any((first_err, second_err, again_err))
    else:
        prepare(mnt, "east", ALICE, alice_gid, 0o700)
        prepare(mnt, "west", ALICE, alice_gid, 0o700)
        plant(mnt, ALICE, alice_gid, groups, "east", n)
        first, first_err = ask(mnt, ALICE, alice_gid, groups, "east")
        after_first = get_info(mnt)
        print(f"G2_SAME n={n} after_east count={first} errno={first_err} {line(after_first)}",
              flush=True)
        plant(mnt, ALICE, alice_gid, groups, "west", n)
        second, second_err = ask(mnt, ALICE, alice_gid, groups, "west")
        after_second = get_info(mnt)
        print(f"G2_SAME n={n} after_west count={second} errno={second_err} {line(after_second)}",
              flush=True)
        again, again_err = ask(mnt, ALICE, alice_gid, groups, "east")
        after_again = get_info(mnt)
        print(f"G2_SAME n={n} east_again count={again} errno={again_err} {line(after_again)}",
              flush=True)
        released = after_again.domain_releases > after_first.domain_releases
        answered = first == n and second == n and again == n and not any((first_err, second_err, again_err))
    umount(mnt)
    return released, answered


def main():
    check(sys.platform == "linux" and os.geteuid() == 0, "run as root on Linux")
    ko = sys.argv[1]
    if "moobfs_layer" not in open("/proc/modules").read():
        subprocess.check_call(["insmod", ko])
    _team, alice_gid, bob_gid = ensure_users()
    check(pwd.getpwuid(ALICE).pw_uid == ALICE, "alice")
    fitted = True
    for n in (2000, 4000):
        cross_rel, cross_ok = scenario(
            "/srv/moob/ext4/g2-0187-cross", "/mnt/g2-0187-cross", n, True, alice_gid, bob_gid)
        same_rel, same_ok = scenario(
            "/srv/moob/ext4/g2-0187-same", "/mnt/g2-0187-same", n, False, alice_gid, bob_gid)
        print(f"G2_BASELINE n={n} cross_released={int(cross_rel)} cross_all_answered={int(cross_ok)} "
              f"same_released={int(same_rel)} same_all_answered={int(same_ok)}", flush=True)
        fitted = cross_ok and same_ok and not cross_rel and not same_rel
        if not fitted:
            print("G2_BASELINE_RECORDED")
            return
    print("G2_BASELINE_FIT 4000 files in each of two domains still fit in 1M without a release")
    raise SystemExit(3)


if __name__ == "__main__":
    main()

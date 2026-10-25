#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Regression for ADR-0021. Run as root on the dedicated Linux host.

Two mounts, index_budget=1M, domain_depth=1, 2000 files per domain. The
cross-user mount passes when bob's search returns ENOMEM, alice's domain
stays built, and her following search does not rebuild. The same-uid mount
passes when the second domain still releases the first. Exit 2 is the old
module: bob's build released alice. Exit 0 is the property.
"""
import errno
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from g1_mounted import ALICE, BOB, ensure_users, get_info, mount_layer, umount
from g2_baseline import ask, owned, plant, prepare


def fail(message):
    print(f"G2_SHARE_FAIL {message}", flush=True)
    raise SystemExit(2)


def main():
    if sys.platform != "linux" or os.geteuid() != 0:
        raise SystemExit("G2_SHARE_FAIL run as root on Linux")
    ko = sys.argv[1]
    if "moobfs_layer" not in open("/proc/modules").read():
        subprocess.check_call(["insmod", ko])
    _team, alice_gid, bob_gid = ensure_users()
    n = 2000
    cross = "/mnt/g2-0188-cross"
    cross_lower = "/srv/moob/ext4/g2-0188-cross"
    owned(cross_lower)
    owned(cross)
    mount_layer(cross_lower, cross, "index_budget=1M")
    os.chmod(cross, 0o755)
    prepare(cross, "alice", ALICE, alice_gid, 0o700)
    prepare(cross, "bob", BOB, bob_gid, 0o700)
    plant(cross, ALICE, alice_gid, [], "alice", n)
    count, err = ask(cross, ALICE, alice_gid, [], "alice")
    after = get_info(cross)
    print(f"G2_SHARE alice count={count} errno={err} builds={after.domain_builds} "
          f"releases={after.domain_releases} used={after.index_used}", flush=True)
    if count != n or err is not None:
        fail("alice did not build")
    plant(cross, BOB, bob_gid, [], "bob", n)
    bcount, berr = ask(cross, BOB, bob_gid, [], "bob")
    mid = get_info(cross)
    print(f"G2_SHARE bob count={bcount} errno={berr} builds={mid.domain_builds} "
          f"releases={mid.domain_releases} built={mid.domains_built} used={mid.index_used}",
          flush=True)
    if berr != errno.ENOMEM or mid.domain_releases != after.domain_releases:
        fail("bob released alice or did not get ENOMEM")
    if mid.domain_builds != after.domain_builds or mid.domains_built != 1:
        fail("alice's domain was not left built")
    again, aerr = ask(cross, ALICE, alice_gid, [], "alice")
    end = get_info(cross)
    print(f"G2_SHARE alice_again count={again} errno={aerr} builds={end.domain_builds} "
          f"releases={end.domain_releases}", flush=True)
    if again != n or aerr is not None or end.domain_builds != after.domain_builds:
        fail("alice rebuilt")
    umount(cross)

    same = "/mnt/g2-0188-same"
    same_lower = "/srv/moob/ext4/g2-0188-same"
    owned(same_lower)
    owned(same)
    mount_layer(same_lower, same, "index_budget=1M")
    os.chmod(same, 0o755)
    prepare(same, "east", ALICE, alice_gid, 0o700)
    prepare(same, "west", ALICE, alice_gid, 0o700)
    plant(same, ALICE, alice_gid, [], "east", n)
    ecount, eerr = ask(same, ALICE, alice_gid, [], "east")
    east = get_info(same)
    plant(same, ALICE, alice_gid, [], "west", n)
    wcount, werr = ask(same, ALICE, alice_gid, [], "west")
    west = get_info(same)
    print(f"G2_SHARE same east={ecount}/{eerr} west={wcount}/{werr} "
          f"releases={east.domain_releases}->{west.domain_releases} "
          f"builds={east.domain_builds}->{west.domain_builds}", flush=True)
    umount(same)
    if ecount != n or wcount != n or eerr or werr:
        fail("same-uid search failed")
    if west.domain_releases <= east.domain_releases:
        fail("same-uid build did not release")
    print("G2_SHARE_PASS")


if __name__ == "__main__":
    main()

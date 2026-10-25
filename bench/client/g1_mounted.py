#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Mounted G1 gate. Run as root on the dedicated Linux host, not on macOS.

Expects the layer module already built. Loads it if needed, mounts ext4 lowers,
and runs the protocol's two-uid corpus. Prints G1_MOUNTED_PASS only when every
mounted check holds, including the 1 MiB budget refusal. A budget that still
answers is G1_BUDGET_FIT and a non-zero exit: the file count is not changed.
"""
import argparse
import ctypes
import errno
import fcntl
import json
import os
import pwd
import stat
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import langgraph_store as mod
from g1_protocol import SHAPES, corpus_slots, identity
from langgraph.store.memory import InMemoryStore

ALICE, BOB = 1001, 1002
STAMP = "2026-01-01T00:00:00+00:00"


class Info(ctypes.Structure):
    _fields_ = [
        ("uapi_version", ctypes.c_uint32),
        ("format_version", ctypes.c_uint32),
        ("attr_name_max", ctypes.c_uint32),
        ("attr_string_max", ctypes.c_uint32),
        ("attr_raw_max", ctypes.c_uint32),
        ("attr_max_per_file", ctypes.c_uint32),
        ("caps", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("index_budget", ctypes.c_uint64),
        ("index_used", ctypes.c_uint64),
        ("index_entries", ctypes.c_uint32),
        ("index_inodes", ctypes.c_uint32),
        ("lower_fs", ctypes.c_char * 16),
        ("domain_depth", ctypes.c_uint32),
        ("heterogeneous", ctypes.c_uint32),
        ("fallback_domains", ctypes.c_uint32),
        ("domains_built", ctypes.c_uint32),
        ("domain_builds", ctypes.c_uint32),
        ("domain_releases", ctypes.c_uint32),
        ("reserved", ctypes.c_uint64),
    ]


def fail(message):
    raise SystemExit(f"G1_MOUNTED_FAIL {message}")


def check(cond, message):
    if not cond:
        fail(message)


def run(cmd):
    subprocess.check_call(cmd)


def ensure_users():
    try:
        grp = __import__("grp").getgrnam("moobteam")
    except KeyError:
        run(["groupadd", "moobteam"])
        grp = __import__("grp").getgrnam("moobteam")
    for name, uid in (("alice", ALICE), ("bob", BOB)):
        try:
            ent = pwd.getpwuid(uid)
            check(ent.pw_name == name, f"uid {uid} is {ent.pw_name}, not {name}")
        except KeyError:
            run(["useradd", "-u", str(uid), "-U", "-M", "-s", "/usr/sbin/nologin", name])
        run(["usermod", "-aG", "moobteam", name])
    return grp.gr_gid, pwd.getpwuid(ALICE).pw_gid, pwd.getpwuid(BOB).pw_gid


def become(uid, gid, groups):
    os.setgroups(groups)
    os.setresgid(0, gid, 0)
    os.setresuid(0, uid, 0)


def restore():
    os.setresuid(0, 0, 0)
    os.setresgid(0, 0, 0)
    os.setgroups([])


def as_user(uid, gid, groups, fn):
    become(uid, gid, groups)
    try:
        return fn()
    finally:
        restore()


def umount(mnt):
    if os.path.ismount(mnt):
        run(["umount", mnt])


def mount_layer(lower, mnt, extra):
    umount(mnt)
    os.makedirs(lower, exist_ok=True)
    os.makedirs(mnt, exist_ok=True)
    opts = f"lower={lower},domain_depth=1" + (f",{extra}" if extra else "")
    run(["mount", "-t", "moobfs_layer", "-o", opts, "none", mnt])


def prepare_domains(root, team_gid):
    for name, uid, gid, mode in (
            ("alice", ALICE, pwd.getpwuid(ALICE).pw_gid, 0o700),
            ("bob", BOB, pwd.getpwuid(BOB).pw_gid, 0o700),
            ("team", 0, team_gid, 0o2770)):
        path = os.path.join(root, name)
        os.makedirs(path, exist_ok=True)
        os.chown(path, uid, gid)
        os.chmod(path, mode)


def plant(mount, uid, gid, groups, name, n):
    slots = corpus_slots(n, __import__("random").Random(1))

    def work():
        store = mod.MoobStore(mount, domain_depth=1)
        for i, value in enumerate(slots):
            store.put((name,), f"m{i:05d}", value)
            if (i + 1) % 500 == 0:
                print(f"G1_PLANT {name} {i + 1}/{n}", flush=True)

    as_user(uid, gid, groups, work)
    oracle = InMemoryStore()
    for i, value in enumerate(slots):
        oracle.put((name,), f"m{i:05d}", value)
    return oracle


def search(mount, uid, gid, groups, name, flt, limit, offset=0):
    def work():
        store = mod.MoobStore(mount, domain_depth=1)
        items = store.search((name,), filter=flt, limit=limit, offset=offset)
        return items, store.stats["query2_calls"]

    return as_user(uid, gid, groups, work)


def vmwm():
    for line in open("/proc/self/status"):
        if line.startswith("VmHWM:"):
            return line.split()[1]
    return "?"


def get_info(mnt):
    info = Info()
    fd = os.open(mnt, os.O_RDONLY | os.O_DIRECTORY)
    try:
        ioctl = (2 << 30) | (ctypes.sizeof(Info) << 16) | (ord("M") << 8) | 2
        fcntl.ioctl(fd, ioctl, info)
    finally:
        os.close(fd)
    return info


def prefix_test(mount, uid, gid, groups):
    def work():
        store = mod.MoobStore(mount, domain_depth=1)
        store.put(("alice",), "zprobe",
                  {"kind": "decision", "flag": True, "score": 0.25, "note": "v1"})
        path = os.path.join(mount, "alice", "zprobe.json")
        before = json.loads(open(path).read())["value"]["note"]
        store.stop_after = "content"
        try:
            store.put(("alice",), "zprobe",
                      {"kind": "decision", "flag": True, "score": 0.25, "note": "v2"})
            raise AssertionError("stop returned")
        except mod.PublicationStopped:
            pass
        after = json.loads(open(path).read())["value"]["note"]
        check(before == after == "v1", f"prefix mixed {before} {after}")
        store.stop_after = None
        store.delete(("alice",), "zprobe")

    as_user(uid, gid, groups, work)


def salary(team, bob_uid, bob_gid, groups):
    def work():
        path = os.path.join(team, "salary.json")
        fd = os.open(path, os.O_CREAT | os.O_WRONLY | os.O_EXCL, 0o600)
        os.write(fd, json.dumps({
            "value": {"kind": "secret"}, "created_at": STAMP, "updated_at": STAMP}).encode())
        os.setxattr(fd, "user.moob.v.kind", bytes([mod.T_STRING]) + b"secret")
        os.close(fd)

    as_user(bob_uid, bob_gid, groups, work)


def confine(mount, alice_gid):
    pid = os.fork()
    if pid:
        _, status = os.waitpid(pid, 0)
        check(os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0,
              f"confine status {status}")
        return
    try:
        os.unshare(os.CLONE_NEWNS)
        run(["mount", "--make-rprivate", "/"])
        base = tempfile.mkdtemp(prefix="g1ns-")
        root = os.path.join(base, "root")
        os.mkdir(root)
        os.mkdir(os.path.join(root, "alice"))
        run(["mount", "--bind", os.path.join(mount, "alice"), os.path.join(root, "alice")])
        os.chroot(root)
        os.chdir("/")
        os.setgroups([])
        os.setresgid(alice_gid, alice_gid, alice_gid)
        os.setresuid(ALICE, ALICE, ALICE)
        if not mod.layer_mounted("/alice"):
            os._exit(3)
        try:
            os.open("/no-mount-root", os.O_RDONLY)
            os._exit(4)
        except FileNotFoundError:
            pass
        store = mod.MoobStore("/", domain_depth=1)
        got = store.search(("alice",), filter={"kind": "decision"}, limit=5000)
        if len(got) != 125:
            os._exit(5)
        os._exit(0)
    except Exception:
        os._exit(6)


def concurrent(mount, team_gid, alice_gid, bob_gid):
    ready = "/tmp/g1-0189-go"
    try:
        os.unlink(ready)
    except FileNotFoundError:
        pass
    pid_w = os.fork()
    if pid_w == 0:
        try:
            while not os.path.exists(ready):
                time.sleep(0.01)
            become(BOB, bob_gid, [team_gid])
            store = mod.MoobStore(mount, domain_depth=1)
            for i in range(30):
                store.put(("team",), f"c{i:05d}",
                          {"kind": "fact", "flag": True, "score": 0.0, "note": f"n{i}"})
            restore()
            os._exit(0)
        except Exception:
            os._exit(7)
    pid_r = os.fork()
    if pid_r == 0:
        try:
            while not os.path.exists(ready):
                time.sleep(0.01)
            misses = 0
            become(ALICE, alice_gid, [team_gid])
            store = mod.MoobStore(mount, domain_depth=1)
            for _ in range(30):
                found = {item.key for item in store.search(("team",), filter={"kind": "fact"}, limit=5000)}
                misses += sum(1 for i in range(30) if f"c{i:05d}" not in found)
            restore()
            open("/tmp/g1-0189-misses", "w").write(str(misses))
            os._exit(0)
        except Exception:
            os._exit(8)
    open(ready, "w").close()
    _, sw = os.waitpid(pid_w, 0)
    _, sr = os.waitpid(pid_r, 0)
    check(os.WIFEXITED(sw) and os.WEXITSTATUS(sw) == 0, f"writer {sw}")
    check(os.WIFEXITED(sr) and os.WEXITSTATUS(sr) == 0, f"reader {sr}")
    for i in range(30):
        path = os.path.join(mount, "team", f"c{i:05d}.json")
        note = json.loads(open(path).read())["value"]["note"]
        attr = os.getxattr(path, "user.moob.v.note")
        check(attr == bytes([mod.T_STRING]) + note.encode(), f"torn {path}")
    misses = open("/tmp/g1-0189-misses").read().strip() if os.path.exists("/tmp/g1-0189-misses") else "?"
    print(f"G1_CONCURRENT 30 team files complete; in-flight misses={misses}")


def fallback(plain, team_gid, alice_gid, bob_gid):
    prepare_domains(plain, team_gid)
    for name, n, uid, gid in (
            ("alice", 8, ALICE, alice_gid), ("bob", 8, BOB, bob_gid), ("team", 16, ALICE, alice_gid)):
        plant(plain, uid, gid, [team_gid], name, n)
    salary(os.path.join(plain, "team"), BOB, bob_gid, [team_gid])

    def visible(uid, gid):
        become(uid, gid, [team_gid])
        try:
            store = mod.MoobStore(plain, domain_depth=1)
            items = store.search(("team",), limit=1000)
            check(store.stats["query2_calls"] == 0, "fallback issued QUERY2")
            keys = {item.key for item in items}
            hidden = os.path.join(plain, "team", "salary.json")
            try:
                fd = os.open(hidden, os.O_RDONLY)
                os.close(fd)
                can = True
            except PermissionError:
                can = False
            return keys, can
        finally:
            restore()

    keys, can = visible(ALICE, alice_gid)
    check(not can and "salary" not in keys, f"alice fallback saw salary {can} {keys}")
    keys, can = visible(BOB, bob_gid)
    check(can and "salary" in keys, "bob fallback hid a file he can open")
    print("G1_FALLBACK alice omits salary; bob sees it")


def budget(lower, mnt, ko_loaded):
    del ko_loaded
    mount_layer(lower, mnt, "index_budget=1M")
    prepare = os.path.join(mnt, "bulk")
    os.mkdir(prepare)
    os.chmod(prepare, 0o755)
    store = mod.MoobStore(mnt, domain_depth=1)
    value = {"kind": "decision"}
    for i in range(6):
        value[f"f{i}"] = "v"
    for i in range(5000):
        store.put(("bulk",), f"b{i:05d}", value)
        if (i + 1) % 500 == 0:
            print(f"G1_BUDGET_PLANT {i + 1}/5000", flush=True)
    try:
        store.search(("bulk",), filter={"kind": "decision"}, limit=10)
    except mod.MoobQueryError as err:
        check(err.errno in mod.RESOURCE_ERRNO, f"budget errno {err.errno}")
        print(f"G1_BUDGET errno={errno.errorcode.get(err.errno, err.errno)}")
        return True
    print("G1_BUDGET_FIT 5000 files with 8 attributes answered inside 1M")
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ko", required=True)
    ap.add_argument("--lower", default="/srv/moob/ext4/g1-0189-lower")
    ap.add_argument("--mnt", default="/mnt/g1-0189")
    ap.add_argument("--budget-lower", default="/srv/moob/ext4/g1-0189-budget")
    ap.add_argument("--budget-mnt", default="/mnt/g1-0189-budget")
    ap.add_argument("--plain", default="/srv/moob/ext4/g1-0189-plain")
    args = ap.parse_args()
    check(sys.platform == "linux" and os.geteuid() == 0, "run as root on Linux")
    loaded = "moobfs_layer" in open("/proc/modules").read()
    if loaded:
        have = subprocess.check_output(["cat", "/sys/module/moobfs_layer/srcversion"], text=True).strip()
        want = subprocess.check_output(["modinfo", "-F", "srcversion", args.ko], text=True).strip()
        mounts = [ln for ln in open("/proc/mounts") if "moobfs_layer" in ln.split()[2:3]]
        if have != want:
            check(not mounts, "another moobfs_layer is mounted; not replacing it")
            run(["rmmod", "moobfs_layer"])
            loaded = False
    if not loaded:
        run(["insmod", args.ko])
    print("G1_MODULE ready", flush=True)
    team_gid, alice_gid, bob_gid = ensure_users()
    groups = [team_gid]
    umount(args.mnt)
    umount(args.budget_mnt)
    for path in (args.lower, args.plain, args.budget_lower):
        if os.path.isdir(path):
            run(["rm", "-rf", path])
    mount_layer(args.lower, args.mnt, "")
    info = get_info(args.mnt)
    print(f"G1_GET_INFO mounted budget={info.index_budget} used={info.index_used}")
    os.chmod(args.mnt, 0o755)
    prepare_domains(args.mnt, team_gid)
    prefix_test(args.mnt, ALICE, alice_gid, groups)
    oracles = {
        "alice": plant(args.mnt, ALICE, alice_gid, groups, "alice", 1000),
        "bob": plant(args.mnt, BOB, bob_gid, groups, "bob", 1000),
        "team": plant(args.mnt, ALICE, alice_gid, groups, "team", 5000),
    }
    sample = os.stat(os.path.join(args.mnt, "team", "m00000.json"))
    print(f"G1_TEAM_MODE {oct(stat.S_IMODE(sample.st_mode))} uid={sample.st_uid} gid={sample.st_gid}",
          flush=True)
    info = get_info(args.mnt)
    print(f"G1_GET_INFO after_plant budget={info.index_budget} used={info.index_used} "
          f"inodes={info.index_inodes} built={info.domains_built}", flush=True)
    for shape in SHAPES:
        for name, uid, gid in (("alice", ALICE, alice_gid), ("bob", BOB, bob_gid),
                               ("team", ALICE, alice_gid)):
            got, calls = search(args.mnt, uid, gid, groups, name, shape, 10000)
            exp = oracles[name].search((name,), filter=shape, limit=10000)
            check({identity(x) for x in got} == {identity(x) for x in exp},
                  f"shape {shape} {name} {len(got)} vs {len(exp)} calls={calls}")
    full, calls = search(args.mnt, ALICE, alice_gid, groups, "team", None, 10000)
    check(calls >= 2, f"team scan used {calls} QUERY2 calls")
    check(len(full) == 5000, f"team scan {len(full)}")
    page0, _ = search(args.mnt, ALICE, alice_gid, groups, "team", {"kind": "decision"}, 100, 0)
    page1, _ = search(args.mnt, ALICE, alice_gid, groups, "team", {"kind": "decision"}, 100, 100)
    whole, _ = search(args.mnt, ALICE, alice_gid, groups, "team", {"kind": "decision"}, 10000)
    check([identity(x) for x in page0] == [identity(x) for x in whole[:100]], "page 0")
    check([identity(x) for x in page1] == [identity(x) for x in whole[100:200]], "page 1")
    check(len(whole) == 625, f"decision {len(whole)}")
    info = get_info(args.mnt)
    print(f"G1_GET_INFO warm budget={info.index_budget} used={info.index_used} "
          f"inodes={info.index_inodes} built={info.domains_built} VmHWM_kB={vmwm()}")
    item = as_user(ALICE, alice_gid, groups,
                   lambda: mod.MoobStore(args.mnt, 1).get(("alice",), "m00000"))
    got, _ = search(args.mnt, ALICE, alice_gid, groups, "alice",
                    {"kind": item.value["kind"]}, 5000)
    check(any(x.key == "m00000" for x in got), "fresh miss")
    def delete_ten():
        store = mod.MoobStore(args.mnt, 1)
        for i in range(10):
            store.delete(("bob",), f"m{i:05d}")
    as_user(BOB, bob_gid, groups, delete_ten)
    for i in range(10):
        oracles["bob"].delete(("bob",), f"m{i:05d}")
    left, _ = search(args.mnt, BOB, bob_gid, groups, "bob", None, 5000)
    exp = oracles["bob"].search(("bob",), limit=5000)
    check({identity(x) for x in left} == {identity(x) for x in exp}, "delete drift")
    salary(os.path.join(args.mnt, "team"), BOB, bob_gid, groups)
    for uid, gid, who in ((ALICE, alice_gid, "alice"), (BOB, bob_gid, "bob"), (0, 0, "root")):
        got, _ = search(args.mnt, uid, gid, groups if uid else [], "team", {"kind": "secret"}, 10)
        check(got == [], f"{who} saw salary")
    def open_salary(uid, gid):
        become(uid, gid, groups)
        try:
            fd = os.open(os.path.join(args.mnt, "team", "salary.json"), os.O_RDONLY)
            os.close(fd)
            return True
        except PermissionError:
            return False
        finally:
            restore()
    check(not open_salary(ALICE, alice_gid), "alice opened salary")
    check(open_salary(BOB, bob_gid), "bob could not open salary")
    confine(args.mnt, alice_gid)
    print("G1_CONFINE alice searched without the mount root")
    concurrent(args.mnt, team_gid, alice_gid, bob_gid)
    fallback(args.plain, team_gid, alice_gid, bob_gid)
    fitted = not budget(args.budget_lower, args.budget_mnt, True)
    if fitted:
        raise SystemExit(3)
    print("G1_MOUNTED_PASS")


if __name__ == "__main__":
    main()

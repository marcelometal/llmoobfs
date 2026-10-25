#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Host checks for the G1 package in langgraph_store.py (journal 0189).

Runs without a mounted layer: publication prefixes, file modes, the twelve filter
shapes against InMemoryStore, client paging, fallback open checks, resource-error
discard, and the burden counts. The mounted gate (two uids, QUERY2 pages of a
5 000-file domain, salary under the module, the 1 MiB budget mount, a confined
namespace, GET_INFO) is not executed here. Passing this script does not close G1.

  g1_protocol.py

Requires langgraph 1.2.15 and langgraph-checkpoint 4.3.0, the pins in
docs/references.md. Those packages are not vendored here.
"""
import errno
import json
import os
import random
import stat
import sys
import tempfile
import threading
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import langgraph_store as mod
from langgraph.store.memory import InMemoryStore

KINDS = ("decision", "note", "task", "fact", "plan", "event", "ref", "log")
SHAPES = (
    {"kind": "decision"},
    {"flag": True},
    {"score": {"$gt": 0.5}},
    {"score": {"$gte": 0.5}},
    {"score": {"$lt": 0.5}},
    {"score": {"$lte": 0.5}},
    {"kind": {"$ne": "decision"}},
    {"kind": "decision", "flag": True},
    {"tags": ["a", "b"]},
    {"meta": {"src": "chat"}},
    {"Mixed-Case Key": "decision"},
    {"note": "n1"},
)


def fail(message):
    raise SystemExit(f"G1_HOST_FAIL {message}")


def check(cond, message):
    if not cond:
        fail(message)


def roundtrip(value):
    return json.loads(json.dumps(value))


def corpus_slots(n, rng):
    """n memories. kind counts are exact. Random(seed) only shuffles slots."""
    check(n % 8 == 0 and n % 2 == 0, f"corpus size {n} is not divisible by 8 and 2")
    slots = []
    for i in range(n):
        slot = {
            "kind": KINDS[i % 8],
            "flag": i < n // 2,
            "score": roundtrip(round((i % 101) / 100, 2)),
            "note": f"n{i % 17}",
        }
        if i % 5 == 0:
            slot["tags"] = ["a", "b"]
        if i % 7 == 0:
            slot["meta"] = {"src": "chat", "n": 1}
        if i % 11 == 0:
            slot["Mixed-Case Key"] = "decision"
        if i % 13 == 0:
            del slot["note"]
        slots.append(slot)
    rng.shuffle(slots)
    return slots


def identity(item):
    return (item.namespace, item.key, json.dumps(item.value, sort_keys=True))


def prepare_domains(root):
    for name, mode in (("alice", 0o700), ("bob", 0o700), ("team", 0o2770)):
        path = os.path.join(root, name)
        os.mkdir(path, 0o700)
        os.chmod(path, mode)


def plant(store, name, n):
    for i, value in enumerate(corpus_slots(n, random.Random(1))):
        store.put((name,), f"m{i:05d}", value)


ATTRS = {}


def install_xattr_double():
    """Darwin's os module has no setxattr. Remember attributes by inode so the
    publication check can read them back. Linux keeps the real calls."""
    if hasattr(os, "setxattr"):
        return

    def setxattr(target, name, value, flags=0):
        if isinstance(target, int):
            ino = os.fstat(target).st_ino
        else:
            ino = os.stat(target).st_ino
        ATTRS.setdefault(ino, {})[name] = bytes(value)

    os.setxattr = setxattr


def read_attr(path, name):
    if hasattr(os, "getxattr"):
        return os.getxattr(path, name)
    fd = os.open(path, os.O_RDONLY)
    try:
        ino = os.fstat(fd).st_ino
    finally:
        os.close(fd)
    return ATTRS[ino][name]


def test_quotas():
    for n, expect in ((1000, 125), (5000, 625)):
        slots = corpus_slots(n, random.Random(1))
        check(sum(s["kind"] == "decision" for s in slots) == expect, f"kind quota {n}")
        check(sum(s["flag"] for s in slots) == n // 2, f"flag quota {n}")


def test_burden_source():
    text = Path(mod.__file__).read_text()
    check("os.access(" not in text, "adapter calls os.access")
    check("sqlite" not in text.lower(), "adapter mentions sqlite")
    check(".db" not in text, "adapter mentions a database file")


def test_modes_and_publication(root):
    store = mod.MoobStore(root, domain_depth=1)
    saved = os.umask(0o077)
    try:
        store.put(("alice",), "m00000",
                  {"kind": "decision", "flag": True, "score": 0.25, "note": "v1"})
    finally:
        os.umask(saved)
    alice = os.path.join(root, "alice", "m00000.json")
    check(stat.S_IMODE(os.stat(alice).st_mode) == 0o600, "private file mode")
    try:
        os.umask(0o077)
        store.put(("team",), "m00000",
                  {"kind": "note", "flag": False, "score": 0.5, "note": "shared"})
    finally:
        os.umask(saved)
    team = os.path.join(root, "team", "m00000.json")
    st, dst = os.stat(team), os.stat(os.path.join(root, "team"))
    check(stat.S_IMODE(st.st_mode) == 0o660, f"shared file mode {oct(stat.S_IMODE(st.st_mode))}")
    check(st.st_gid == dst.st_gid, "shared file group")

    def body():
        return json.loads(Path(alice).read_text())["value"]["note"]

    previous = body()
    previous_attr = read_attr(alice, "user.moob.v.note")
    for step, note in (("content", "v2"), ("attributes", "v3"), ("file_fsync", "v4")):
        store.stop_after = step
        try:
            store.put(("alice",), "m00000",
                      {"kind": "decision", "flag": True, "score": 0.25, "note": note})
            fail(f"stop {step} returned")
        except mod.PublicationStopped as stopped:
            check(stopped.step == step, f"stop step {stopped.step}")
        check(body() == previous, f"final name changed at {step}")
        check(read_attr(alice, "user.moob.v.note") == previous_attr, f"attributes changed at {step}")
    store.stop_after = "rename"
    try:
        store.put(("alice",), "m00000",
                  {"kind": "log", "flag": True, "score": 0.25, "note": "v5"})
        fail("stop rename returned")
    except mod.PublicationStopped as stopped:
        check(stopped.step == "rename", "rename step")
    check(body() == "v5", "published note")
    check(read_attr(alice, "user.moob.v.kind") == bytes([mod.T_STRING]) + b"log", "published kind")
    store.stop_after = None


def extra_names(root):
    bad = []
    for dirpath, dirnames, filenames in os.walk(root):
        for name in dirnames + filenames:
            if name.endswith(".json") or name.startswith(".tmp.") or name in ("alice", "bob", "team"):
                continue
            if os.path.isdir(os.path.join(dirpath, name)) and name in ("alice", "bob", "team"):
                continue
            bad.append(os.path.join(dirpath, name))
    return bad


def test_answers_and_fallback(root):
    prepare_ok = os.path.isdir(os.path.join(root, "alice"))
    check(prepare_ok, "domains missing")
    oracle, store = InMemoryStore(), mod.MoobStore(root, domain_depth=1)
    for name, n in (("alice", 8), ("bob", 16), ("team", 16)):
        plant(oracle, name, n)
        plant(store, name, n)
    check(store.stats["query2_calls"] == 0, "fallback issued QUERY2")
    for shape in SHAPES:
        for name in ("alice", "bob", "team"):
            got = store.search((name,), filter=shape, limit=1000)
            exp = oracle.search((name,), filter=shape, limit=1000)
            check({identity(x) for x in got} == {identity(x) for x in exp},
                  f"shape {shape} on {name}: {len(got)} vs {len(exp)}")
    full = store.search(("team",), filter={"flag": True}, limit=1000)
    page0 = store.search(("team",), filter={"flag": True}, limit=2, offset=0)
    page1 = store.search(("team",), filter={"flag": True}, limit=2, offset=2)
    check([identity(x) for x in page0] == [identity(x) for x in full[:2]], "page 0")
    check([identity(x) for x in page1] == [identity(x) for x in full[2:4]], "page 1")
    check(len(full) == 8, f"flag half {len(full)}")

    hidden = os.path.join(root, "team", "salary.json")
    Path(hidden).write_text(json.dumps({"value": {"kind": "secret"}, "created_at": "2020-01-01T00:00:00+00:00",
                                         "updated_at": "2020-01-01T00:00:00+00:00"}))
    os.chmod(hidden, 0)
    opened = False
    try:
        fd = os.open(hidden, os.O_RDONLY)
        os.close(fd)
        opened = True
    except PermissionError:
        opened = False
    found = [identity(x) for x in store.search(("team",), filter={"kind": "secret"}, limit=10)]
    if opened:
        print("G1_UNREADABLE_NOT_RUN owner can open mode 000")
    else:
        check(found == [], f"fallback returned an unopenable file {found}")
    if not opened:
        names = [item.key for item in store.search(("team",), limit=1000)]
        check("salary" not in names, f"fallback returned salary {names}")

    item = store.get(("alice",), "m00000")
    fresh = store.search(("alice",), filter={"kind": item.value["kind"]}, limit=1000)
    check(any(x.key == "m00000" for x in fresh), "fresh put missing")
    for i in range(10):
        store.delete(("bob",), f"m{i:05d}")
        oracle.delete(("bob",), f"m{i:05d}")
    left = store.search(("bob",), limit=1000)
    check(all(not x.key.startswith("m0000") or int(x.key[1:]) >= 10 for x in left), "deleted key remains")
    check({identity(x) for x in left} ==
          {identity(x) for x in oracle.search(("bob",), limit=1000)}, "delete drifted from the oracle")
    check(extra_names(root) == [], f"side files {extra_names(root)}")


def test_mounted_does_not_scan(root):
    store = mod.MoobStore(root, domain_depth=1)

    def only_one(domain, terms):
        return ["m00000.json"]

    store._query2 = only_one
    original = mod.layer_mounted
    mod.layer_mounted = lambda path: True
    try:
        got = store.search(("team",), limit=1000)
    finally:
        mod.layer_mounted = original
    check([x.key for x in got] == ["m00000"], f"mounted search scanned { [x.key for x in got] }")
    check(store.stats["fallback_domains"] == 0, "mounted search used the fallback")


def test_resource_error(root):
    store = mod.MoobStore(root, domain_depth=1)
    calls = {"n": 0}

    def ioctl(fd, request, query):
        entered = bytes(query.cursor)
        if calls["n"] == 0:
            buf = mod.ctypes.cast(query.results, mod.ctypes.POINTER(mod.Result))
            buf[0].flags = 0
            buf[0].path = b"m00000.json"
            query.count = 1
            query.truncated = 1
            query.cursor[0] = 9
            calls["n"] = 1
            calls["first_cursor"] = entered
            return 0
        check(entered[0] == 9, "second page did not continue the cursor")
        raise OSError(errno.ENOMEM, "budget")

    original = mod.fcntl.ioctl
    mod.fcntl.ioctl = ioctl
    try:
        try:
            store._query2(os.path.join(root, "team"), [("v.kind", mod.OP_EQ, mod.T_STRING, b"decision")])
            fail("resource error returned a list")
        except mod.MoobQueryError as err:
            check(err.errno == errno.ENOMEM, f"errno {err.errno}")
    finally:
        mod.fcntl.ioctl = original

    def refuse(domain, terms):
        raise mod.MoobQueryError(errno.ENOMEM, "budget")

    store._query2 = refuse
    original_mounted = mod.layer_mounted
    mod.layer_mounted = lambda path: True
    try:
        try:
            store.search(("team",), limit=10)
            fail("search swallowed a resource error")
        except mod.MoobQueryError:
            pass
    finally:
        mod.layer_mounted = original_mounted


def test_two_pages(root):
    store = mod.MoobStore(root, domain_depth=1)
    calls = {"n": 0}

    def ioctl(fd, request, query):
        buf = mod.ctypes.cast(query.results, mod.ctypes.POINTER(mod.Result))
        if calls["n"] == 0:
            buf[0].flags = 0
            buf[0].path = b"a.json"
            query.count = 1
            query.truncated = 1
            query.cursor[0] = 3
        else:
            check(bytes(query.cursor)[0] == 3, "cursor lost")
            buf[0].flags = 0
            buf[0].path = b"b.json"
            query.count = 1
            query.truncated = 0
        calls["n"] += 1
        return 0

    original = mod.fcntl.ioctl
    mod.fcntl.ioctl = ioctl
    try:
        paths = store._query2(os.path.join(root, "alice"), [("lg.updated", mod.OP_EXISTS, 0, b"")])
    finally:
        mod.fcntl.ioctl = original
    check(paths == ["a.json", "b.json"], f"pages {paths}")
    check(calls["n"] == 2, "did not follow the cursor")


def test_last_rename(root):
    store = mod.MoobStore(root, domain_depth=1)
    barrier = threading.Barrier(2)
    errors = []

    def worker(note):
        try:
            barrier.wait()
            mod.MoobStore(root, domain_depth=1).put(
                ("team",), "race", {"kind": "fact", "flag": True, "score": 0.0, "note": note})
        except Exception as err:
            errors.append(err)

    threads = [threading.Thread(target=worker, args=(note,)) for note in ("left", "right")]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    check(errors == [], f"concurrent put failed {errors}")
    path = os.path.join(root, "team", "race.json")
    value = json.loads(Path(path).read_text())["value"]
    check(value["note"] in ("left", "right"), f"torn note {value}")
    check(read_attr(path, "user.moob.v.note") == bytes([mod.T_STRING]) + value["note"].encode(),
          "attribute does not match the published body")
    print(f"G1_CONCURRENT last-rename-wins note={value['note']}")
    del store


def main():
    install_xattr_double()
    test_quotas()
    test_burden_source()
    with tempfile.TemporaryDirectory(prefix="moob-g1-") as root:
        prepare_domains(root)
        test_modes_and_publication(root)
        test_answers_and_fallback(root)
        test_mounted_does_not_scan(root)
        test_resource_error(root)
        test_two_pages(root)
        test_last_rename(root)
    print("G1_HOST_PASS")
    print("G1_MODULE_NOT_RUN two uids, QUERY2 on 5000 files, salary under the module,")
    print("G1_BUDGET_NOT_RUN index_budget=1M,")
    print("G1_CONFINE_NOT_RUN mount namespace,")
    print("G1_GET_INFO_NOT_RUN")


if __name__ == "__main__":
    main()

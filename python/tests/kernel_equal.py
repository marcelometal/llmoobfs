#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""The library's two backends against each other on a mounted layer (ROADMAP C5).

Run as root on a dedicated test machine or VM, with moobfs_layer mounted at MNT over LOWER with
domain_depth=1:

    python3 python/tests/kernel_equal.py MNT LOWER

Builds a seeded random corpus in two domains through the mount (every type, missing and invalid
attributes, raw values, heterogeneous files, hard links, subdirectories, a directory with
attributes, written on the lower since the layer refuses them), then for random requests compares the kernel's answer (QUERY2 with small pages,
and QUERY2_BATCH) with the scan of the same directory through the mount and the scan of the
lower directory without the module. Unordered answers are compared as sets, ordered ones in
order. Repeats a part as the domain's owner, checks that another uid gets PermissionError
from both backends, and mounts a second layer over LOWER-refusal with index_build_work=1024 to
check a refused query. Prints MOOBFS_PYLIB_PASS, or MOOBFS_PYLIB_FAIL and the first difference.
"""
import os
import random
import subprocess
import sys
from datetime import datetime, timezone

if not os.environ.get("MOOBFS_TEST_INSTALLED"):   # else test the installed package
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src"))

import moobfs  # noqa: E402
from moobfs import Order, Term  # noqa: E402
from moobfs import query as q  # noqa: E402
from moobfs import scan  # noqa: E402

OWNER, OTHER, GROUP = 2001, 2002, 2100
SEED, FILES, REQUESTS, BATCHES = 1, 600, 400, 60
KINDS = ["note", "fact", "task", "nota", "é", "日本", ""]
NAMES = ["kind", "n", "score", "flag", "t", "tag"]


def value_for(rng, name):
    if name in ("kind", "tag"):
        base = rng.choice(KINDS)
        return base + (rng.choice(["", "-a", "-b", "x"]) if name == "tag" else "")
    if name == "n":
        return rng.choice([rng.randint(-50, 50), rng.choice([-2**63, 2**63 - 1, 0])])
    if name == "score":
        return rng.choice([rng.uniform(-10, 10), -0.0, 0.0, float("inf"), float("-inf"), 1.5])
    if name == "flag":
        return rng.random() < 0.5
    return datetime.fromtimestamp(rng.randint(-10**6, 2 * 10**9), timezone.utc)


def build(rng, mnt, lower, seen):
    dom = os.path.join(mnt, "alice")
    os.mkdir(dom, 0o700)
    os.chown(dom, OWNER, OWNER)
    os.mkdir(os.path.join(dom, "sub"), 0o700)
    os.chown(os.path.join(dom, "sub"), OWNER, OWNER)
    os.mkdir(os.path.join(dom, "sub", "deeper"), 0o700)
    os.chown(os.path.join(dom, "sub", "deeper"), OWNER, OWNER)
    team = os.path.join(mnt, "team")
    os.mkdir(team)
    os.chown(team, 0, GROUP)
    os.chmod(team, 0o2770)
    for d in (dom, team):
        for i in range(FILES // 2):
            where = d if d == team or i % 5 else os.path.join(d, rng.choice(["sub", "sub/deeper"]))
            attrs = {n: value_for(rng, n) for n in NAMES if rng.random() < 0.7}
            for k, v in attrs.items():
                seen.setdefault(k, []).append(v)
            if rng.random() < 0.1:
                attrs["blob"] = b"raw bytes"
            if not attrs:
                attrs = {"kind": "note"}
            p = moobfs.publish(where, f"m{i:04d}.json", b"{}", attrs, domain=d)
            r = rng.random()
            low = os.path.join(lower, os.path.relpath(p, mnt))   # the layer refuses these
            if r < 0.04:
                os.setxattr(low, "user.moob.n", b"\x02\x00")         # wrong size: not indexed
            elif r < 0.07:
                os.setxattr(low, "user.moob.kind", b"\x01\xff")      # not UTF-8
            elif r < 0.10:
                os.chmod(p, 0o200 if d == dom else 0o604)           # heterogeneous
            elif r < 0.12:
                os.link(p, os.path.join(d, f"link{i:04d}"))          # two names
    os.setxattr(os.path.join(dom, "sub"), "user.moob.kind", b"\x01note")   # a directory
    os.setxattr(dom, "user.moob.kind", b"\x01note")                     # the domain itself
    return [dom, team]


def random_term(rng, seen):
    name = rng.choice(NAMES) if rng.random() < 0.95 else "missing"
    op = rng.choice(["==", "==", "<", "<=", ">", ">=", "prefix", "exists"])
    if op == "exists":
        return Term(name, op)
    if op == "prefix":
        return Term(name, op, rng.choice(KINDS)[:1], missing_ok=rng.random() < 0.2)
    pool = seen.get(name)
    v = rng.choice(pool) if pool and rng.random() < 0.85 else value_for(rng, rng.choice(NAMES))
    if rng.random() < 0.05:
        v = rng.choice([1, 1.0, "1", True])                          # a type the name may not have
    return Term(name, op, v, missing_ok=rng.random() < 0.2)


def random_request(rng, seen):
    n = rng.choices([0, 1, 2, 3, 4], weights=[15, 45, 28, 8, 4])[0]
    terms = [random_term(rng, seen) for _ in range(n)]
    order = None
    if rng.random() < 0.4 or not terms:
        name, kind = rng.choice([("n", "int64"), ("score", "double"), ("t", "time"),
                                 ("kind", "string"), ("tag", "string")])
        if rng.random() < 0.15:
            kind = rng.choice(["int64", "double", "time", "string"])   # often a type it lacks
        order = Order(name, kind, rng.random() < 0.5)
    if order is None and all(t.missing_ok for t in terms):
        terms[0] = Term(terms[0].name, "exists")
    return terms, order


def norm(answers, order):
    pairs = [(m.path, m.ino) for m in answers]
    return pairs if order is not None else sorted(pairs)


def compare(dom, lower_dom, requests, page):
    fd = os.open(dom, os.O_RDONLY | os.O_DIRECTORY)
    try:
        mounted, lowered = scan.candidates(dom), scan.candidates(lower_dom)
        for terms, order in requests:
            k = norm(q.kernel_query(fd, terms, order, page=page), order)
            s = norm(scan.run(mounted, terms, order), order)
            lo = norm(scan.run(lowered, terms, order), order)
            if not k == s == lo:
                return f"{dom}: {q.describe(terms, order)}: kernel {k[:5]}... scan {s[:5]}... lower {lo[:5]}..."
    finally:
        os.close(fd)
    return None


def compare_batches(dom, lower_dom, batches):
    for reqs in batches:
        k = moobfs.Domain(dom, backend="kernel").batch(reqs)
        s = scan.scan_batch(lower_dom, reqs)
        for (terms, order), a, b in zip(reqs, k, s):
            if norm(a, order) != norm(b, order):
                return f"batch {dom}: {q.describe(terms, order)}"
    return None


def as_uid(uid, gids, fn):
    r, w = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(r)
        msg = b"ok"
        try:
            os.setgroups(gids)
            os.setgid(gids[0])
            os.setuid(uid)
            msg = (fn() or "ok").encode()
        except BaseException as err:              # noqa: BLE001 - reported to the parent
            msg = f"error {type(err).__name__}: {err}".encode()
        os.write(w, msg[:4000])
        os._exit(0)
    os.close(w)
    out = os.read(r, 4096).decode()
    os.close(r)
    os.waitpid(pid, 0)
    return out


def refusal(mnt, lower):
    """A domain too large for index_build_work=1024 (the minimum, ADR-0020): QueryRefused by
    default, the scan's answer with on_refusal="scan"."""
    low, top = lower.rstrip("/") + "-refusal", mnt.rstrip("/") + "-refusal"
    big = os.path.join(low, "big")
    os.makedirs(big, 0o700)
    os.makedirs(top, exist_ok=True)
    for i in range(3000):
        moobfs.publish(big, f"f{i:05d}", b"", {"n": i % 10})
    subprocess.run(["mount", "-t", "moobfs_layer", "-o",
                    f"lower={low},domain_depth=1,index_build_work=1024", "none", top], check=True)
    try:
        t = Term("n", "==", 3)
        try:
            moobfs.Domain(os.path.join(top, "big")).query(t)
            return "the kernel answered; the domain was meant to exceed the build work"
        except moobfs.QueryRefused:
            pass
        d = moobfs.Domain(os.path.join(top, "big"), on_refusal="scan")
        got = sorted(m.path for m in d.query(t))
        if d.last_backend != "scan" or got != sorted(m.path for m in scan.scan_query(big, [t])) \
                or len(got) != 300:
            return f"on_refusal=scan gave {len(got)} answers from {d.last_backend}"
    finally:
        subprocess.run(["umount", top], check=False)
    return None


def main():
    mnt, lower = sys.argv[1], sys.argv[2]
    if os.geteuid() != 0 or not moobfs.layer_mounted(mnt):
        print(f"MOOBFS_PYLIB_FAIL: run as root with the layer mounted at {mnt}")
        return 1
    info = moobfs.get_info(mnt)
    if info["domain_depth"] != 1 or not moobfs.usable_info(info):
        print(f"MOOBFS_PYLIB_FAIL: needs domain_depth=1 and the index on (flags {info['flags']:#x})")
        return 1
    rng = random.Random(SEED)
    seen = {}
    doms = build(rng, mnt, lower, seen)
    lows = [os.path.join(lower, os.path.basename(d)) for d in doms]
    requests = [random_request(rng, seen) for _ in range(REQUESTS)]
    batches = [[random_request(rng, seen) for _ in range(rng.randint(1, 8))]
               for _ in range(BATCHES)]
    for dom, low in zip(doms, lows):
        for page in (7, 4096):
            diff = compare(dom, low, requests, page)
            if diff:
                print(f"MOOBFS_PYLIB_FAIL: page {page}: {diff}")
                return 1
        diff = compare_batches(dom, low, batches)
        if diff:
            print(f"MOOBFS_PYLIB_FAIL: {diff}")
            return 1
    cands = scan.candidates(doms[0])
    nonempty = sum(1 for t, o in requests if scan.run(cands, t, o))
    if nonempty < REQUESTS // 2:
        print(f"MOOBFS_PYLIB_FAIL: only {nonempty} of {REQUESTS} requests have answers: too weak")
        return 1
    got = as_uid(OWNER, [OWNER], lambda: compare(doms[0], lows[0], requests[:100], 7))
    if got != "ok":
        print(f"MOOBFS_PYLIB_FAIL: as the owner: {got}")
        return 1
    got = as_uid(OWNER, [OWNER, GROUP], lambda: compare(doms[1], lows[1], requests[:100], 7))
    if got != "ok":
        print(f"MOOBFS_PYLIB_FAIL: as a team member: {got}")
        return 1

    def denied():
        seen = []
        for backend in ("kernel", "scan"):
            try:
                moobfs.Domain(doms[0], backend=backend).query(Term("kind", "exists"))
                seen.append(f"{backend} answered")
            except PermissionError:
                pass
        return "; ".join(seen) or None
    got = as_uid(OTHER, [OTHER], denied)
    if got != "ok":
        print(f"MOOBFS_PYLIB_FAIL: another uid: {got}")
        return 1
    got = refusal(mnt, lower)
    if got:
        print(f"MOOBFS_PYLIB_FAIL: refusal: {got}")
        return 1
    auto = moobfs.Domain(doms[0])
    auto.query(Term("kind", "exists"))
    if auto.last_backend != "kernel":
        print("MOOBFS_PYLIB_FAIL: auto did not use the kernel on the mount")
        return 1
    print(f"MOOBFS_PYLIB_PASS: {REQUESTS} requests ({nonempty} with answers in alice) x 2 domains"
          f" x pages 7 and 4096, {BATCHES} batches: kernel = scan through the mount = scan of the"
          " lower; owner and team member agree; another uid refused by both; a refused query"
          " raises QueryRefused or is answered by the scan")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""The MCP adapter's acceptance on a mounted layer (roadmap C7, ADR-0024, journal 0208).

Run as root on a dedicated test machine or VM, in a Python environment with the library and
its mcp extra installed, with moobfs_layer mounted at MNT over LOWER with domain_depth=1:

    python3 python/tests/mcp_mounted.py MNT LOWER

Builds kernel_equal.py's corpus (alice, uid 2001, 0700; team, group 2100, 2770) plus a private
bob domain (uid 2002). Each check starts `python -m moobfs.mcp --root MNT` as a stdio
subprocess of a process that has become the user, as an agent host would:

1. answers: for random requests, the server's query equals moobfs.Domain's in the same
   process, as alice on alice and on team (requests with an infinite double are skipped: JSON
   cannot carry them);
2. isolation: alice's server cannot query, read or write bob's domain, nor bob's alice's; both
   see the same team answers; a memory bob writes in team reaches alice's query;
3. identity: the server refuses to start as root.

Prints MOOBFS_MCP_PASS, or MOOBFS_MCP_FAIL and the first difference.
"""
import asyncio
import json
import math
import os
import random
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if not os.environ.get("MOOBFS_TEST_INSTALLED"):
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "src"))

import kernel_equal as ke  # noqa: E402
import moobfs  # noqa: E402
from moobfs import _uapi as u  # noqa: E402
from mcp import Client, StdioServerParameters  # noqa: E402

TYPE = {u.T_STRING: "string", u.T_INT64: "int64", u.T_DOUBLE: "double", u.T_BOOL: "bool",
        u.T_TIME: "time"}
REQUESTS = 150
BOB, BOB_GID = ke.OTHER, ke.OTHER


def term_json(t):
    out = {"name": t.name, "op": t.op}
    if t.missing_ok:
        out["missing_ok"] = True
    if t.value is not None:
        v = t.value
        py = struct.unpack("<q", v.payload)[0] if v.type == u.T_TIME else v.python()
        if isinstance(py, float) and math.isinf(py):
            return None
        out["value"], out["type"] = py, TYPE[v.type]
    return out


def order_json(o):
    return None if o is None else {"name": o.name, "type": TYPE[o.type], "descending": o.descending}


def params(mnt):
    return StdioServerParameters(command=sys.executable, args=["-m", "moobfs.mcp", "--root", mnt],
                                 env={"PATH": "/usr/bin:/bin", "MOOBFS_TEST_INSTALLED": "1"},
                                 cwd="/")


async def calls(mnt, batch):
    """[(is_error, structured, text)] for [(tool, args)], one server for the whole batch."""
    out = []
    async with Client(params(mnt)) as client:
        for name, args in batch:
            r = await client.call_tool(name, args)
            out.append((r.is_error, r.structured_content or {},
                        " ".join(getattr(c, "text", "") for c in r.content)))
    return out


def run_as(uid, gids, fn):
    """fn() in a child that has become uid; its JSON result, or an error string."""
    r, w = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(r)
        try:
            os.setgroups(gids)
            os.setgid(gids[0])
            os.setuid(uid)
            msg = json.dumps({"ok": fn()})
        except BaseException as err:          # noqa: BLE001 - reported to the parent
            msg = json.dumps({"error": f"{type(err).__name__}: {err}"})
        data = msg.encode()
        while data:
            data = data[os.write(w, data):]
        os._exit(0)
    os.close(w)
    chunks = []
    while True:
        b = os.read(r, 65536)
        if not b:
            break
        chunks.append(b)
    os.close(r)
    os.waitpid(pid, 0)
    got = json.loads(b"".join(chunks) or b'{"error": "no output"}')
    return got.get("ok"), got.get("error")


def answers(mnt, domain, requests):
    """Differences between the server and moobfs.Domain for these requests, as this user."""
    batch, kept = [], []
    for terms, order in requests:
        tj = [term_json(t) for t in terms]
        if any(x is None for x in tj):
            continue
        batch.append(("query", {"domain": domain, "terms": tj, "order": order_json(order),
                                "limit": 100000}))
        kept.append((terms, order))
    res = asyncio.run(calls(mnt, batch))
    d = moobfs.Domain(os.path.join(mnt, domain))
    nonempty = 0
    for (terms, order), (err, out, text) in zip(kept, res):
        if err:
            return {"diff": f"{domain}: server error {text} for {moobfs.query.describe(terms, order)}"}
        lib = [[m.path, m.ino] for m in d.query(*terms, order=order)]
        srv = [[m["path"], m["ino"]] for m in out["matches"]]
        nonempty += bool(lib)
        if order is None:
            lib, srv = sorted(lib), sorted(srv)
        if lib != srv:
            return {"diff": f"{domain}: {moobfs.query.describe(terms, order)}: library {lib[:3]} "
                            f"server {srv[:3]}"}
    if nonempty < len(kept) // 3:
        return {"diff": f"{domain}: only {nonempty} of {len(kept)} requests have answers: too weak"}
    return {"compared": len(kept), "nonempty": nonempty, "skipped": len(requests) - len(kept),
            "backend": res[0][1].get("backend") if res else None}


def isolation(mnt, who, own, other):
    res = asyncio.run(calls(mnt, [
        ("query", {"domain": other, "terms": [{"name": "kind", "op": "exists"}]}),
        ("read", {"domain": other, "path": "p0.json"}),
        ("write", {"domain": other, "path": f"from-{who}.json", "content": "x"}),
        ("read", {"domain": own, "path": f"../{other}/p0.json"}),
        ("query", {"domain": "team", "terms": [{"name": "kind", "op": "exists"}], "limit": 100000}),
        ("info", {}),
    ]))
    refused = [r[0] for r in res[:4]]
    return {"refused": refused, "team": sorted(m["path"] for m in res[4][1]["matches"]),
            "uid": res[5][1].get("uid"), "domains": res[5][1].get("domains"),
            "mounted": res[5][1].get("layer_mounted")}


def main():
    mnt, lower = sys.argv[1], sys.argv[2]
    if os.geteuid() != 0 or not moobfs.layer_mounted(mnt):
        print(f"MOOBFS_MCP_FAIL: run as root with the layer mounted at {mnt}")
        return 1
    rng = random.Random(ke.SEED)
    seen = {}
    ke.build(rng, mnt, lower, seen)
    bob = os.path.join(mnt, "bob")
    os.mkdir(bob, 0o700)
    os.chown(bob, BOB, BOB_GID)
    for i in range(20):
        moobfs.publish(bob, f"p{i}.json", b"{}", {"kind": "private", "n": i})
    alice = os.path.join(mnt, "alice")
    moobfs.publish(alice, "p0.json", b"{}", {"kind": "private"})
    requests = [ke.random_request(rng, seen) for _ in range(REQUESTS)]

    for domain, gids in (("alice", [ke.OWNER]), ("team", [ke.OWNER, ke.GROUP])):
        got, err = run_as(ke.OWNER, gids, lambda d=domain: answers(mnt, d, requests))
        if err or "diff" in got:
            print(f"MOOBFS_MCP_FAIL: answers: {err or got['diff']}")
            return 1
        print(f"MCP_ANSWERS {domain}: {got['compared']} requests ({got['nonempty']} with answers) "
              f"equal to the library "
              f"({got['skipped']} skipped: infinite double), backend {got['backend']}", flush=True)

    a, err = run_as(ke.OWNER, [ke.OWNER, ke.GROUP], lambda: isolation(mnt, "alice", "alice", "bob"))
    if err:
        print(f"MOOBFS_MCP_FAIL: alice isolation: {err}")
        return 1
    b, err = run_as(BOB, [BOB_GID, ke.GROUP], lambda: isolation(mnt, "bob", "bob", "alice"))
    if err:
        print(f"MOOBFS_MCP_FAIL: bob isolation: {err}")
        return 1
    if a["refused"] != [True] * 4 or b["refused"] != [True] * 4:
        print(f"MOOBFS_MCP_FAIL: cross-domain call answered: alice {a['refused']} bob {b['refused']}")
        return 1
    if a["team"] != b["team"] or not a["team"]:
        print(f"MOOBFS_MCP_FAIL: team answers differ ({len(a['team'])} vs {len(b['team'])})")
        return 1
    if (a["uid"], b["uid"]) != (ke.OWNER, BOB) or "bob" in a["domains"] or "alice" in b["domains"]:
        print(f"MOOBFS_MCP_FAIL: info: alice {a['uid']} {a['domains']}, bob {b['uid']} {b['domains']}")
        return 1
    for p in ("from-alice.json", "from-bob.json"):
        if os.path.exists(os.path.join(mnt, "bob" if "alice" in p else "alice", p)):
            print(f"MOOBFS_MCP_FAIL: {p} was written")
            return 1

    def bob_writes():
        return asyncio.run(calls(mnt, [("write", {"domain": "team", "path": "from-bob.json",
                                                  "content": "shared", "attributes": {"kind": "handoff"}})]))[0][0]

    def alice_finds():
        res = asyncio.run(calls(mnt, [("query", {"domain": "team", "terms": [
            {"name": "kind", "op": "==", "value": "handoff"}]})]))
        return [m["path"] for m in res[0][1]["matches"]]
    werr, err = run_as(BOB, [BOB_GID, ke.GROUP], bob_writes)
    found, err2 = run_as(ke.OWNER, [ke.OWNER, ke.GROUP], alice_finds)
    if err or err2 or werr or found != ["from-bob.json"]:
        print(f"MOOBFS_MCP_FAIL: shared write: {err or err2 or werr} {found}")
        return 1

    root = subprocess.run([sys.executable, "-m", "moobfs.mcp", "--root", mnt], input=b"",
                          capture_output=True, timeout=60)
    if root.returncode == 0 or b"refusing to run as root" not in root.stderr:
        print(f"MOOBFS_MCP_FAIL: started as root: exit {root.returncode} {root.stderr[-200:]!r}")
        return 1

    print(f"MOOBFS_MCP_PASS: answers equal to the library on alice and team through stdio as "
          f"uid {ke.OWNER}; alice and bob each refused 4 of 4 calls on the other's domain; same "
          f"{len(a['team'])} team answers; bob's team write reached alice; the server refused root")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Stage 2 workload (W-AIM) of docs/client-test-plan.md: writes a deterministic mix of pages into a
running `ai-memory serve` through its own MCP tool `memory_write_page`, so the pages, front matter
and SQLite rows are the client's. Standard library only.

  aimem-workload.py --url http://127.0.0.1:PORT --token TOKEN --projects N --pages M [--seed S]
                    [--workspace NAME] [--out FILE.csv]

Per project: notes, decisions, gotchas, procedures, concepts, rules and pinned slots; a fraction of
the pages get `expires_at` in the past or the future and some are pinned. Prints one summary line;
--out writes one CSV row per call (project, path, seconds, ok)."""
import argparse
import datetime as dt
import json
import random
import sys
import time
import urllib.request

FAMILIES = [  # (directory, kind, weight)
    ("notes", "note", 30), ("decisions", "decision", 20), ("gotchas", "gotcha", 15),
    ("procedures", "procedure", 10), ("concepts", "fact", 10), ("_rules", "rule", 10),
    ("_slots", None, 5),
]
TIERS = ["working", "episodic", "semantic", "procedural"]


def call(url, token, args):
    req = {"jsonrpc": "2.0", "id": 1, "method": "tools/call",
           "params": {"name": "memory_write_page", "arguments": args}}
    r = urllib.request.Request(url + "/mcp", data=json.dumps(req).encode(), method="POST", headers={
        "content-type": "application/json", "accept": "application/json, text/event-stream",
        "authorization": "Bearer " + token})
    with urllib.request.urlopen(r, timeout=60) as resp:
        body = resp.read().decode()
    if body.lstrip().startswith("{"):
        msg = json.loads(body)
    else:  # server-sent events: the JSON-RPC message is on a "data:" line
        msg = next(json.loads(l[5:]) for l in body.splitlines() if l.startswith("data:"))
    if "error" in msg:
        return False, str(msg["error"])[:200]
    res = msg.get("result", {})
    return not res.get("isError", False), json.dumps(res)[:200]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--url", required=True)
    ap.add_argument("--token", required=True)
    ap.add_argument("--projects", type=int, required=True)
    ap.add_argument("--pages", type=int, required=True, help="pages per project")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--workspace", default="stage2")
    ap.add_argument("--out")
    a = ap.parse_args()
    rnd = random.Random(a.seed)
    now = dt.datetime.now(dt.timezone.utc).replace(microsecond=0)
    weights = [f[2] for f in FAMILIES]
    rows, ok_n, fail_n, t0 = [], 0, 0, time.monotonic()
    for p in range(a.projects):
        project = "p%04d" % p
        for j in range(a.pages):
            fam, kind, _ = rnd.choices(FAMILIES, weights)[0]
            path = "%s/%s-%05d.md" % (fam, fam.strip("_"), j)
            args = {"workspace": a.workspace, "project": project, "path": path,
                    "body": "# %s %d of %s\n\nStage 2 page %d, seed %d.\n" % (fam.strip("_"), j, project, j, a.seed),
                    "tier": rnd.choice(TIERS), "pinned": fam == "_slots" or rnd.random() < 0.1}
            if kind:
                args["kind"] = kind
            r = rnd.random()
            if r < 0.15:
                args["expires_at"] = (now - dt.timedelta(minutes=rnd.randint(1, 30 * 24 * 60))).isoformat().replace("+00:00", "Z")
            elif r < 0.35:
                args["expires_at"] = (now + dt.timedelta(minutes=rnd.randint(60, 90 * 24 * 60))).isoformat().replace("+00:00", "Z")
            s = time.monotonic()
            ok, info = call(a.url, a.token, args)
            rows.append((project, path, time.monotonic() - s, int(ok)))
            if ok:
                ok_n += 1
            else:
                fail_n += 1
                if fail_n <= 5:
                    print("aimem-workload: %s/%s refused: %s" % (project, path, info), file=sys.stderr)
    if a.out:
        with open(a.out, "w") as f:
            f.write("project,path,seconds,ok\n")
            f.writelines("%s,%s,%.6f,%d\n" % row for row in rows)
    print("W-AIM: projects=%d pages=%d written=%d refused=%d seconds=%.1f"
          % (a.projects, a.projects * a.pages, ok_n, fail_n, time.monotonic() - t0))
    return 0 if ok_n else 1


if __name__ == "__main__":
    sys.exit(main())

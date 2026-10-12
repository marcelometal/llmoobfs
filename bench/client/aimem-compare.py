#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Stage 2 comparator of docs/client-test-plan.md: for every ai-memory project, runs six structured
queries on ai-memory's own SQLite (predicates from its store/src/reader.rs) and the same queries as
QUERY2 on the project directory (a domain, domain_depth=2), and checks that the answers are equal:
the same pages in the same order, pages with equal updated_at compared as a set (QUERY2 breaks ties
by inode). Correctness only; latency is measured separately. Standard library only.

  aimem-compare.py --db DATA_DIR/db/memory.sqlite --wiki WIKI_ON_THE_LAYER --query PATH/TO/query
                   [--limit 20] [--now-us US] [--stale-days 7]

Shapes: recent (not expired, newest first), pinned, rules, settled (kind in rule, decision; a set),
stale (episodic, older than the cutoff, oldest first), incremental (updated after a point, oldest
first, every page, through the QUERY2 cursor with small pages)."""
import argparse
import os
import sqlite3
import subprocess
import sys
import time
import uuid

NOT_EXPIRED = " AND (expires_at IS NULL OR expires_at > :now)"
KIND = ("COALESCE(json_extract(frontmatter_json, '$.kind'), CASE "
        "WHEN path LIKE '\\_rules/%' ESCAPE '\\' THEN 'rule' WHEN path LIKE '\\_slots/%' ESCAPE '\\' THEN 'slot' "
        "WHEN path LIKE 'sessions/%' THEN 'session' WHEN path LIKE 'decisions/%' THEN 'decision' "
        "WHEN path LIKE 'gotchas/%' THEN 'gotcha' WHEN path LIKE 'concepts/%' THEN 'concept' "
        "WHEN path LIKE 'procedures/%' THEN 'procedure' WHEN path LIKE 'notes/%' THEN 'note' "
        "WHEN path LIKE 'profile/%' THEN 'preference' ELSE 'fact' END)")
SCOPE = "workspace_id = :ws AND project_id = :proj AND is_latest = 1"


def q2(binary, domain, args):
    out = subprocess.run([binary, domain] + args, capture_output=True, text=True)
    if out.returncode:
        raise RuntimeError("query %s: %s" % (" ".join(args), out.stderr.strip()))
    rows = []
    for line in out.stdout.splitlines():
        parts = line.split("\t")
        rows.append(parts[-1])
    return rows


def groups(paths, updated):
    """Ordered list of sets of paths sharing one updated_at."""
    g, last = [], None
    for p in paths:
        u = updated.get(p)
        if not g or u != last:
            g.append(set())
            last = u
        g[-1].add(p)
    return g


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", required=True)
    ap.add_argument("--wiki", required=True)
    ap.add_argument("--query", required=True)
    ap.add_argument("--limit", type=int, default=20)
    ap.add_argument("--now-us", type=int, default=int(time.time() * 1e6))
    ap.add_argument("--stale-days", type=int, default=7)
    a = ap.parse_args()
    con = sqlite3.connect("file:%s?mode=ro" % a.db, uri=True)
    now_ns = a.now_us * 1000
    cutoff_us = a.now_us - a.stale_days * 86400 * 10**6
    projects = con.execute("SELECT DISTINCT workspace_id, project_id FROM pages WHERE is_latest = 1").fetchall()
    checked = failed = 0
    sizes = {}
    for ws, proj in projects:
        domain = os.path.join(a.wiki, str(uuid.UUID(bytes=ws)), str(uuid.UUID(bytes=proj)))
        if not os.path.isdir(domain):
            continue
        p = {"ws": ws, "proj": proj, "now": a.now_us, "lim": a.limit, "cut": cutoff_us}
        updated = dict(con.execute("SELECT path, updated_at FROM pages WHERE " + SCOPE, p).fetchall())
        mid = sorted(updated.values())[len(updated) // 2] if updated else 0
        p["mid"] = mid
        # pages written in one run are all recent: unless --stale-days reaches back to them, the stale
        # shape uses the project's median updated_at as its cutoff, the same query with a cutoff that
        # actually splits the pages
        if not any(u < cutoff_us for u in updated.values()):
            p["cut"] = mid
        lim = ["--limit", str(a.limit)]
        exp = ["expires_at", "gt?", "time", str(now_ns)]
        shapes = {
            "recent": ("SELECT path FROM pages WHERE " + SCOPE + NOT_EXPIRED + " ORDER BY updated_at DESC LIMIT :lim",
                       ["q2", "--order", "updated_at", "time", "desc"] + lim + exp, True),
            "pinned": ("SELECT path FROM pages WHERE " + SCOPE + " AND pinned = 1" + NOT_EXPIRED +
                       " ORDER BY updated_at DESC LIMIT :lim",
                       ["q2", "--order", "updated_at", "time", "desc"] + lim + ["pinned", "eq", "bool", "1"] + exp, True),
            "rules": ("SELECT path FROM pages WHERE " + SCOPE + " AND path LIKE '\\_rules/%' ESCAPE '\\'" + NOT_EXPIRED +
                      " ORDER BY updated_at DESC LIMIT :lim",
                      ["q2", "--order", "updated_at", "time", "desc"] + lim + ["family", "eq", "string", "_rules"] + exp, True),
            "stale": ("SELECT path FROM pages WHERE " + SCOPE + " AND tier = 'episodic' AND updated_at < :cut"
                      " ORDER BY updated_at ASC LIMIT :lim",
                      ["q2", "--order", "updated_at", "time", "asc"] + lim +
                      ["tier", "eq", "string", "episodic", "updated_at", "lt", "time", str(p["cut"] * 1000)], True),
            "incremental": ("SELECT path FROM pages WHERE " + SCOPE + " AND updated_at > :mid" + NOT_EXPIRED +
                            " ORDER BY updated_at ASC",
                            ["q2", "--order", "updated_at", "time", "asc", "--limit", "7", "--pages",
                             "updated_at", "gt", "time", str(mid * 1000)] + exp, True),
        }
        for name, (sql, args, ordered) in shapes.items():
            want = [r[0] for r in con.execute(sql, p).fetchall()]
            got = q2(a.query, domain, args)
            sizes[name] = sizes.get(name, 0) + len(want)
            ok = groups(want, updated) == groups(got, updated) if ordered else set(want) == set(got)
            # with a LIMIT the last group may be cut at a different member on each side
            if not ok and ordered and "--pages" not in args and len(want) == len(got) == a.limit:
                gw, gg = groups(want, updated), groups(got, updated)
                ok = gw[:-1] == gg[:-1] and len(gw) == len(gg)
            checked += 1
            if not ok:
                failed += 1
                if failed <= 10:
                    print("DIFF %s %s: sql=%d q2=%d first sql=%s q2=%s" % (domain, name, len(want), len(got), want[:3], got[:3]))
        want = {r[0] for r in con.execute("SELECT path FROM pages WHERE " + SCOPE + " AND " + KIND +
                                          " IN ('rule', 'decision')" + NOT_EXPIRED, p).fetchall()}
        out = subprocess.run([a.query, domain, "q2batch", "--"] + ["kind", "eq", "string", "rule"] + exp +
                             ["--", "kind", "eq", "string", "decision"] + exp, capture_output=True, text=True)
        got = {l.split("\t")[-1] for l in out.stdout.splitlines()} if out.returncode == 0 else None
        sizes["settled"] = sizes.get("settled", 0) + len(want)
        checked += 1
        if got != want:
            failed += 1
            if failed <= 10:
                print("DIFF %s settled: sql=%d q2=%s" % (domain, len(want), "error" if got is None else len(got)))
    print("COMPARE: projects=%d checks=%d failed=%d results=%s" % (len(projects), checked, failed,
          ",".join("%s:%d" % kv for kv in sorted(sizes.items()))))
    return 0 if checked and not failed else 1


if __name__ == "__main__":
    sys.exit(main())

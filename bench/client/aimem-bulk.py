#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Stage 2 bulk loader of docs/client-test-plan.md: writes the W-AIM mix of aimem-workload.py as
page files in ai-memory's own layout, for `ai-memory reindex` to build its SQLite from. Through
MCP every write checkpoints the whole wiki in git, so the cost per page grows with the store
(journal 0068); this path reaches the large scales. Standard library only.

  aimem-bulk.py --wiki WIKI_DIR --projects N --pages M [--seed S] [--workspace NAME]

Layout: <wiki>/<workspace-uuid>/_meta.md, <workspace-uuid>/<project-uuid>/_meta.md and
<project-uuid>/<family>/<name>.md with a YAML front matter (kind, tier, pinned, expires_at).
Unlike the MCP path, `updated_at` is assigned by the reindex, directory by directory, so the
ordering key is correlated with the page family. Run it with the umask the server would have."""
import argparse
import datetime as dt
import os
import random
import sys

FAMILIES = [  # (directory, kind, weight), as aimem-workload.py
    ("notes", "note", 30), ("decisions", "decision", 20), ("gotchas", "gotcha", 15),
    ("procedures", "procedure", 10), ("concepts", "fact", 10), ("_rules", "rule", 10),
    ("_slots", None, 5),
]
TIERS = ["working", "episodic", "semantic", "procedural"]


def manifest(path, key, name):
    with open(path, "w") as f:
        f.write("---\n%s: %s\ntype: Scope Manifest\n---\n" % (key, name))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wiki", required=True)
    ap.add_argument("--projects", type=int, required=True)
    ap.add_argument("--pages", type=int, required=True, help="pages per project")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--workspace", default="stage2")
    a = ap.parse_args()
    rnd = random.Random(a.seed)
    now = dt.datetime.now(dt.timezone.utc).replace(microsecond=0)
    weights = [f[2] for f in FAMILIES]
    ws = os.path.join(a.wiki, "01a13300-0000-7000-8000-000000000001")
    os.makedirs(ws)
    manifest(os.path.join(ws, "_meta.md"), "workspace", a.workspace)
    written = 0
    for p in range(a.projects):
        proj = os.path.join(ws, "01a13300-%04x-7000-8000-%012x" % (p + 1, p + 1))
        os.makedirs(proj)
        manifest(os.path.join(proj, "_meta.md"), "project", "p%04d" % p)
        for fam, _, _ in FAMILIES:
            os.mkdir(os.path.join(proj, fam))
        for j in range(a.pages):
            fam, kind, _ = rnd.choices(FAMILIES, weights)[0]
            tier = rnd.choice(TIERS)
            pinned = fam == "_slots" or rnd.random() < 0.1
            fm = []
            if kind:
                fm.append("kind: " + kind)
            r = rnd.random()
            if r < 0.15:
                when = now - dt.timedelta(minutes=rnd.randint(1, 30 * 24 * 60))
                fm.append("expires_at: " + when.isoformat().replace("+00:00", "Z"))
            elif r < 0.35:
                when = now + dt.timedelta(minutes=rnd.randint(60, 90 * 24 * 60))
                fm.append("expires_at: " + when.isoformat().replace("+00:00", "Z"))
            fm.append("tier: " + tier)
            if pinned:
                fm.append("pinned: true")
            name = fam.strip("_")
            with open(os.path.join(proj, fam, "%s-%05d.md" % (name, j)), "w") as f:
                f.write("---\n%s\n---\n# %s %d of p%04d\n\nStage 2 page %d, seed %d.\n"
                        % ("\n".join(fm), name, j, p, j, a.seed))
            written += 1
    print("W-AIM-BULK: projects=%d pages=%d written=%d" % (a.projects, a.projects * a.pages, written))
    return 0


if __name__ == "__main__":
    sys.exit(main())

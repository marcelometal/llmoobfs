#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Stage 2 mirror of docs/client-test-plan.md: copies the structured fields of every latest
ai-memory page into typed MoobFS attributes on the page file, read from ai-memory's own SQLite so
that the ordering key (`updated_at`, store clock) is identical on both sides. Standard library only.

  aimem-mirror.py --db DATA_DIR/db/memory.sqlite --wiki WIKI_ON_THE_LAYER [--out FILE.csv]

Attributes: family (first path segment), tier, kind (front matter `kind`, else derived from the
path exactly as ai-memory's `page_kind_expr`),
pinned (bool), updated_at (time), expires_at (time, only when set). Encoding as common/attr.c:
one type byte, then the payload (string: UTF-8; bool: one byte; time: int64 nanoseconds,
little-endian)."""
import argparse
import json
import os
import sqlite3
import struct
import sys
import time
import uuid

T_STRING, T_BOOL, T_TIME = 1, 4, 5
# page_kind_expr of ai-memory store/src/reader.rs: front matter `kind`, else from the first segment
PATH_KIND = {"_rules": "rule", "_slots": "slot", "sessions": "session", "decisions": "decision",
             "gotchas": "gotcha", "concepts": "concept", "procedures": "procedure", "notes": "note",
             "profile": "preference"}


def enc_str(s):
    return bytes([T_STRING]) + s.encode()


def enc_bool(b):
    return bytes([T_BOOL, 1 if b else 0])


def enc_time_us(us):
    return bytes([T_TIME]) + struct.pack("<q", int(us) * 1000)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--db", required=True)
    ap.add_argument("--wiki", required=True)
    ap.add_argument("--out")
    a = ap.parse_args()
    con = sqlite3.connect("file:%s?mode=ro" % a.db, uri=True)
    rows = con.execute("SELECT workspace_id, project_id, path, tier, pinned, updated_at, expires_at, "
                       "frontmatter_json FROM pages WHERE is_latest = 1").fetchall()
    done, missing, per, t0 = 0, 0, [], time.monotonic()
    for ws, proj, path, tier, pinned, updated, expires, fm in rows:
        f = os.path.join(a.wiki, str(uuid.UUID(bytes=ws)), str(uuid.UUID(bytes=proj)), path)
        if not os.path.exists(f):
            missing += 1
            continue
        try:
            kind = json.loads(fm or "{}").get("kind")
        except ValueError:
            kind = None
        family = path.split("/", 1)[0]
        if not isinstance(kind, str):
            kind = PATH_KIND.get(family, "fact")
        attrs = [("family", enc_str(family)), ("tier", enc_str(tier)), ("kind", enc_str(kind)),
                 ("pinned", enc_bool(pinned)), ("updated_at", enc_time_us(updated))]
        if expires is not None:
            attrs.append(("expires_at", enc_time_us(expires)))
        s = time.monotonic()
        for name, value in attrs:
            os.setxattr(f, "user.moob." + name, value)
        per.append(time.monotonic() - s)
        done += 1
    if a.out:
        with open(a.out, "w") as o:
            o.write("seconds\n")
            o.writelines("%.6f\n" % x for x in per)
    per.sort()
    p50 = per[len(per) // 2] * 1e6 if per else 0
    print("MIRROR: pages=%d mirrored=%d missing_files=%d seconds=%.1f per_page_p50_us=%.0f"
          % (len(rows), done, missing, time.monotonic() - t0, p50))
    return 0 if done and not missing else 1


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""The moobfs SQLite virtual table measured (journal 0108).

For each statement: the answer through the virtual table over a domain (no database: QUERY2 and
getxattr) must equal the answer of an ordinary in-memory SQLite table holding the same metadata
(the "SQLite next to the files" arrangement), and both are timed, warm, median of REPS runs.

    python3 bench/sqlext.py <moobfs.so> <layer domain> <lower domain> [REPS]

Prints one line per statement: rows, median ms through the virtual table, median ms through the
ordinary table, and the plan the virtual table got. Standard library only.
"""
import os
import sqlite3
import statistics
import struct
import sys
import time

COLS = [("kind", "string"), ("created", "time"), ("agent", "string"), ("score", "int64"),
        ("pinned", "bool"), ("namespace", "string")]


def decode(v):
    t, p = v[0], v[1:]
    if t == 1:
        return p.decode()
    if t in (2, 5):
        return struct.unpack("<q", p)[0]
    if t == 3:
        return struct.unpack("<d", p)[0]
    if t == 4:
        return p[0]
    return p


def load_ref(db, lower):
    db.execute("CREATE TEMP TABLE ref(rowid INTEGER PRIMARY KEY, path TEXT, "
               + ", ".join(f"{n} {'TEXT' if t == 'string' else 'INTEGER'}" for n, t in COLS) + ")")
    rows = []
    for d, _, files in os.walk(lower):
        for f in files:
            p = os.path.join(d, f)
            vals = []
            for n, _ in COLS:
                try:
                    vals.append(decode(os.getxattr(p, "user.moob." + n, follow_symlinks=False)))
                except OSError:
                    vals.append(None)
            if vals[0] is None or vals[1] is None:
                continue  # key and required column, as in the virtual table
            rows.append([os.lstat(p).st_ino, os.path.relpath(p, lower)] + vals)
    db.executemany(f"INSERT INTO ref VALUES({','.join('?' * (len(COLS) + 2))})", rows)
    return len(rows)


def timed(db, sql, reps):
    out, times = None, []
    for _ in range(reps):
        t0 = time.perf_counter()
        out = db.execute(sql).fetchall()
        times.append((time.perf_counter() - t0) * 1000)
    return out, statistics.median(times)


def main():
    so, layer, lower = sys.argv[1:4]
    reps = int(sys.argv[4]) if len(sys.argv) > 4 else 20
    db = sqlite3.connect(":memory:")
    db.enable_load_extension(True)
    db.load_extension(so)
    db.enable_load_extension(False)
    spec = ", ".join([f"'{layer}'", "'kind string'", "'created time required'"]
                     + [f"'{n} {t}'" for n, t in COLS[2:]])
    db.execute(f"CREATE VIRTUAL TABLE temp.vt USING moobfs({spec})")
    t0 = time.perf_counter()
    n = load_ref(db, lower)
    print(f"ref: {n} files loaded from the lower filesystem in {time.perf_counter() - t0:.1f} s")
    kinds = [r[0] for r in db.execute("SELECT kind FROM ref GROUP BY kind ORDER BY count(*) DESC")]
    k1, k2 = kinds[0], kinds[1]
    stmts = [
        f"SELECT count(*) FROM {{T}} WHERE kind = '{k1}'",
        f"SELECT path FROM {{T}} WHERE kind IN ('{k1}', '{k2}')",
        f"SELECT path FROM {{T}} WHERE kind = '{k2}' AND agent = 'a07'",
        f"SELECT path FROM {{T}} WHERE kind = '{k2}' OR agent = 'a07'",
        "SELECT path, created FROM {T} ORDER BY created DESC, path LIMIT 20",
        "SELECT path, created FROM {T} ORDER BY created DESC LIMIT 20",
        f"SELECT path, created FROM {{T}} WHERE kind = '{k2}' ORDER BY created DESC LIMIT 20",
        f"SELECT path, created FROM {{T}} WHERE kind = '{k2}' ORDER BY created DESC, path LIMIT 20",
        f"SELECT path, created FROM {{T}} WHERE kind IN ('{k1}', '{k2}') ORDER BY created DESC, path LIMIT 20",
        "SELECT path, score FROM {T} WHERE agent = 'a07' AND score >= 500 ORDER BY created DESC, path LIMIT 20",
        "SELECT kind, count(*) FROM {T} GROUP BY kind ORDER BY kind",
        "SELECT count(*) FROM {T} WHERE pinned = 1 AND score < 100",
    ]
    for s in stmts:
        a, b = s.replace("{T}", "vt"), s.replace("{T}", "ref")
        plan = " | ".join(r[3] for r in db.execute("EXPLAIN QUERY PLAN " + a))
        ra, ta = timed(db, a, reps)
        rb, tb = timed(db, b, reps)
        same = ra == rb if "ORDER BY" in s else sorted(ra) == sorted(rb)
        print(f"{'same' if same else 'DIFFERENT'} rows={len(ra)} vt_ms={ta:.2f} ref_ms={tb:.2f} :: {s} :: {plan}")
        if not same:
            sys.exit(1)
    print("SQLEXT_BENCH_DONE")


if __name__ == "__main__":
    main()

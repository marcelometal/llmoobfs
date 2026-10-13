#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Stage 2 live mirror of docs/client-test-plan.md: follows writes under ai-memory's wiki with
inotify and copies each page's front matter into typed MoobFS attributes as the page appears,
without reading the client's database. The ordering key is the file's mtime, since the store's
`updated_at` exists only in its SQLite. Linux only; standard library only.

  aimem-sidecar.py --wiki WIKI_ON_THE_LAYER [--out FILE.csv]

Watches every directory below the wiki (new ones as they appear), reacts to a page file being
closed after writing or renamed into place, and sets family, tier, kind, pinned, updated_at and
expires_at as aimem-mirror.py does. It does not watch attribute changes, so its own writes do not
wake it. Stops on SIGTERM or SIGINT and prints one summary line; --out gets one row per page
(seconds spent, seconds between the file's mtime and the attributes being set)."""
import argparse
import ctypes
import datetime as dt
import os
import select
import signal
import struct
import sys
import time

IN_CLOSE_WRITE, IN_MOVED_TO, IN_CREATE, IN_ISDIR, IN_Q_OVERFLOW = 0x8, 0x80, 0x100, 0x40000000, 0x4000
T_STRING, T_BOOL, T_TIME = 1, 4, 5
PATH_KIND = {"_rules": "rule", "_slots": "slot", "sessions": "session", "decisions": "decision",
             "gotchas": "gotcha", "concepts": "concept", "procedures": "procedure", "notes": "note",
             "profile": "preference"}
libc = ctypes.CDLL(None, use_errno=True)
stop = False


def front_matter(path):
    """Top-level `key: value` pairs of the YAML block that opens the file."""
    fm = {}
    with open(path, "r", errors="replace") as f:
        if f.readline().strip() != "---":
            return fm
        for line in f:
            if line.strip() == "---":
                break
            if line[:1] in " \t" or ":" not in line:
                continue
            k, v = line.split(":", 1)
            fm[k.strip()] = v.strip().strip("'\"")
    return fm


def time_ns_of(text):
    t = dt.datetime.fromisoformat(text.replace("Z", "+00:00"))
    if t.tzinfo is None:
        t = t.replace(tzinfo=dt.timezone.utc)
    return int(t.timestamp()) * 10**9 + t.microsecond * 1000


def mirror(path, family):
    fm = front_matter(path)
    st = os.stat(path)
    attrs = [("family", bytes([T_STRING]) + family.encode()),
             ("tier", bytes([T_STRING]) + fm.get("tier", "episodic").encode()),
             ("kind", bytes([T_STRING]) + (fm.get("kind") or PATH_KIND.get(family, "fact")).encode()),
             ("pinned", bytes([T_BOOL, 1 if fm.get("pinned") == "true" or family == "_slots" else 0])),
             ("updated_at", bytes([T_TIME]) + struct.pack("<q", st.st_mtime_ns))]
    if fm.get("expires_at"):
        attrs.append(("expires_at", bytes([T_TIME]) + struct.pack("<q", time_ns_of(fm["expires_at"]))))
    for name, value in attrs:
        os.setxattr(path, "user.moob." + name, value)
    return st.st_mtime_ns


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wiki", required=True)
    ap.add_argument("--out")
    a = ap.parse_args()
    wiki = os.path.realpath(a.wiki)
    fd = libc.inotify_init1(os.O_CLOEXEC)
    if fd < 0:
        raise OSError(ctypes.get_errno(), "inotify_init1")
    dirs = {}

    def watch(d):
        wd = libc.inotify_add_watch(fd, d.encode(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE)
        if wd >= 0:
            dirs[wd] = d
        for e in os.scandir(d):     # a directory may gain children before its watch exists
            if e.is_dir(follow_symlinks=False) and e.name != ".git":
                watch(e.path)

    def handler(*_):
        global stop
        stop = True
    signal.signal(signal.SIGTERM, handler)
    signal.signal(signal.SIGINT, handler)
    watch(wiki)
    print("SIDECAR: watching %d directories" % len(dirs), flush=True)
    rows, errors, overflow = [], 0, 0
    while True:
        # os.read() restarts after a signal, so wait with a timeout to notice the request to stop;
        # what is already queued is still read before leaving
        ready = select.select([fd], [], [], 0.2)[0]
        if not ready:
            if stop:
                break
            continue
        buf = os.read(fd, 1 << 16)
        off = 0
        while off < len(buf):
            wd, mask, _, n = struct.unpack_from("iIII", buf, off)
            name = buf[off + 16:off + 16 + n].split(b"\0", 1)[0].decode(errors="replace")
            off += 16 + n
            if mask & IN_Q_OVERFLOW:
                overflow += 1
                continue
            d = dirs.get(wd)
            if d is None:
                continue
            path = os.path.join(d, name)
            if mask & IN_ISDIR:
                if mask & (IN_CREATE | IN_MOVED_TO) and name != ".git":
                    watch(path)
                continue
            rel = os.path.relpath(path, wiki).split(os.sep)
            # <workspace>/<project>/<family>/.../<name>.md; manifests and temporary files are skipped
            if not mask & (IN_CLOSE_WRITE | IN_MOVED_TO) or len(rel) < 4 or not name.endswith(".md"):
                continue
            s = time.monotonic()
            try:
                mtime_ns = mirror(path, rel[2])
            except (OSError, ValueError):
                errors += 1     # replaced or removed before we got to it, or an unparsable date
                continue
            rows.append((time.monotonic() - s, (time.time_ns() - mtime_ns) / 1e9))
    if a.out:
        with open(a.out, "w") as f:
            f.write("seconds,lag_seconds\n")
            f.writelines("%.6f,%.6f\n" % r for r in rows)
    cost = sorted(r[0] for r in rows)
    lag = sorted(r[1] for r in rows)
    mid = len(rows) // 2
    print("SIDECAR: events=%d errors=%d overflow=%d directories=%d per_page_p50_us=%.0f lag_p50_ms=%.1f lag_p99_ms=%.1f"
          % (len(rows), errors, overflow, len(dirs), cost[mid] * 1e6 if rows else 0,
             lag[mid] * 1e3 if rows else 0, lag[len(rows) * 99 // 100] * 1e3 if rows else 0), flush=True)
    return 0 if rows and not overflow else 1


if __name__ == "__main__":
    sys.exit(main())

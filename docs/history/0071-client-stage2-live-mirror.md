# 0071 — Real-client test, stage 2: live mirror, and the current module on the benchmark host

Date: 2026-10-13. Status: measured at 2 000 pages, one client, two runs. Local series (after pull
request 57).

## Goal

Close two limitations journal 0068 left: the mirror ran offline, so the cost of keeping attributes
in step with live writes was not measured (the part of stage 2's "done when" still open); and
every stage 2 number came from a module that predates journals 0061 and 0069.

## Live mirror

`bench/client/aimem-sidecar.py` follows the wiki with inotify (a watch per directory, added as
directories appear), and when a page file is closed after writing or renamed into place it reads
the front matter and sets the same attributes as the offline mirror. It does not read the client's
database. `LIVE=1` in `stage2.sh` runs it beside the server; `aimem-mirror.py --verify` then
compares what is on every page with what the database says.

Decisions:

- **The ordering key is the file's mtime.** The client's `updated_at` exists only in its SQLite,
  and a mirror that reads the database to copy it is not independent of it. The verification
  measures the difference instead of hiding it. Because the keys differ, the answer comparison
  and the latency tool, which take their thresholds from the store's key, do not run in this mode;
  they were measured with the offline mirror.
- **No watch on attribute changes**, so the sidecar's own `setxattr` calls do not wake it.

Result, ten projects of 200 pages through the client's MCP tool, maintenance off, current module
(`bench/results/2026-10-12-macmini-aimem-stage2-live/live-10x200/`):

- 2 001 pages checked against the database: **no structured field differs, no page left without
  attributes**, although every project and family directory was created during the run.
- mtime is 11.9 ms before the store's `updated_at` at the median (7 to 305 ms), and **no two
  neighbouring pages of a project change places** when ordered by it.
- The sidecar handled 4 002 events (two per page) in 0.79 ms each at the median; attributes were
  on the file 11.2 ms after its mtime at the median, 17.9 ms at the 99th percentile, 306 ms at
  most.
- **No write loop:** the database ends with 2 001 rows, all latest versions, and the server logs
  no reconciliation. Setting attributes did not wake the client's file watcher.
- **The client's writes are not slowed:** 322.7 s for the 2 000 pages, against 321.7 s without the
  sidecar (journal 0068), and the same cost per page from the first project to the tenth.

Cost against the client's own synchronization: its `reindex`, which parses a page and updates its
SQLite and full-text index, takes 2.9 to 3.75 ms per page (journal 0068). The sidecar spends about
1.6 ms per page in an interpreted language, most of it reading and parsing the file twice; setting
the attributes themselves costs 78 µs per page (the offline mirror). The two do different amounts
of work, so this bounds the comparison and does not settle it: the attribute side is cheaper by
one to two orders of magnitude for the structured fields alone.

A first run lost the sidecar's summary: `os.read` restarts after a signal, so the request to stop
was never noticed and the process was killed. It now waits with a timeout. The verification of
that run gave the same result (no difference, no missing page, no swap).

## The module with the fixes of this series

The benchmark host now runs the module built from the series' tip
(`/opt/moob-test/moobfs_layer-0069.ko`), with the three layers remounted.

- As root `GET_INFO` carries the counters (flags `0x30`); as an ordinary user they are zero.
- An ordinary user wrote to a `4755` file through the layer: the write succeeded, the mode became
  `755`, and the kernel log has no `BUG` or `Oops`. On the previous module this crashed the host.
- The 20 000-page scale was repeated (`bulk-10x2000-module-0069/`): 60 of 60 checks equal, the
  same index size to the byte (10 609 402), and every warm `QUERY2` median within 3 % of journal
  0068 (for instance `settled` 326.9 against 336.5 µs, `incremental` 595.1 against 611.3 µs). The
  two fixes did not change what was measured.

## Also recorded

CodeQL raised one alert on pull request 57 (`cpp/path-injection` at
`bench/client/aimem-latency.c`, the operator's `--out` path). It is the class journal 0045 decided
to dismiss as "used in tests" for the other benchmark tools; the maintainer dismissed it with that
reason.

## What this says about the layer, and what is the client's

About the layer: attributes can be kept in step with writes by a process that only watches files,
at a cost far below a write, and nothing in the layer had to change for it.

The client's own: the front matter format the sidecar parses, the two events per page, and the
fact that its store clock and the file's mtime agree in order. Another system may write files in a
way where they do not (several files per memory, or rewrites in place), and that has to be checked
per client.

## Limitations

2 000 pages, sequential writes by one client, two runs; nothing concurrent, no edits or deletions,
no burst large enough to overflow the inotify queue (the sidecar counts overflows and reported
none). The sidecar is a test tool in Python, not a component of the project. A page is queryable
about 11 ms after it is written, not at once: a client that needs read-your-writes must set the
attributes itself, in the write sequence of `docs/client-contract.md` §6.

## Next step

Stage 2 is closed. M3 phase 3c next, in the order confirmed by the maintainer on this date: 3c,
S4 and S2, the rest of S6, the M4 report, and the open proposals after those and before stage 3.

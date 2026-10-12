# 0065 — Real-client test, stage 2: latency tool and first numbers

Date: 2026-10-12. Status: tool written; measured at 150 and 1 905 pages on the benchmark host;
the scales up to 60 000 pages are not run. Local series.

## Goal

Measure the latency of the six stage 2 shapes on both sides. The comparator of journal 0064 starts
the `query` client once per call, which measures process start, not the query.

## Tool

`bench/client/aimem-latency.c` (`make bench-tools` builds `bench-aimem-latency`; needs
`libsqlite3-dev`). One process runs the comparator's SQL through libsqlite3 and the same requests
as `QUERY2`/`QUERY2_BATCH` on the project directory, and records one sample per call.
`bench/client/stage2.sh` runs it after the comparison when the binary is installed.

- **Warm:** per project, one connection and one descriptor of the domain directory stay open; each
  shape runs once unmeasured and then 200 times per side, the sides alternating.
- **Cold:** before every sample the connection and the descriptor are closed and the page, dentry
  and inode caches are dropped. The sample is what a client that just started pays; `open_ns` is
  the part before the first row is asked for (open and prepare, or open of the directory).
- Row counts of the two sides are compared on every call; answers stay with `aimem-compare.py`.

## Decisions

- **C with libsqlite3, not Python.** At 15 to 90 µs per call the interpreter's own cost would be a
  large part of both numbers. `bench/sqlclient.c` already measures SQLite this way.
- **Statements prepared once.** ai-memory uses `prepare_cached` in `reader.rs`; preparing per call
  would charge SQLite for work the client does not do.
- **Cold samples split into open and run.** The first cold numbers (18 ms against 2 ms) were almost
  all open: about 17 ms to open the database and prepare, about 2.5 ms to walk to the directory.
  Reported as one number they would have said nothing about the query.
- **`incremental` is the whole traversal** on both sides: one statement in SQL, the cursor with 256
  results per call in `QUERY2`. Paging the SQL side by keyset on `updated_at` alone would be wrong
  with ties.
- **The SQL selects only `path`,** as the comparator does. ai-memory's own statements also return
  the title and a descriptor computed from the body, so this is SQLite's most favourable case.

## Evidence

Mac mini (i7-2635QM, Debian 13, kernel 6.12.111), module of `main` with the rename fix, ai-memory
385b81e, SQLite 3.46.1, lower ext4 on an SSD
(`bench/results/2026-10-12-macmini-aimem-stage2-latency/`). One run per scale.

Equality: 18 of 18 checks at 3 × 50 pages and 66 of 66 at 1 905 pages; no row-count mismatch in
7 560 and 27 720 latency samples; no result with an unknown path, also after dropping the caches.

Warm, p50 in µs (SQL / `QUERY2`):

| Shape | 150 pages, 3 projects | 1 905 pages, 11 projects |
| --- | --- | --- |
| recent | 18.8 / 20.8 | 18.7 / 18.0 |
| pinned | 25.2 / 15.4 | 57.5 / 24.8 |
| rules | 27.9 / 16.3 | 88.3 / 28.6 |
| settled | 70.6 / 27.9 | 241.6 / 41.6 |
| stale | 15.1 / 16.7 | 28.2 / 25.2 |
| incremental | 17.4 / 23.7 | 46.1 / 53.4 |

- `QUERY2` is faster where the SQL filters rows after the index (`pinned`, `rules`: 1.6 to 3.1
  times) or computes `kind` from the JSON front matter (`settled`: 2.5 to 5.8 times), and the gap
  grows with the pages per project.
- `recent` and `stale` are equal within the variation between runs: an earlier run of the same
  1 905 pages gave 18.9 / 20.4 for `recent` and 28.5 / 27.4 for `stale`.
- **`QUERY2` loses on `incremental`** (0.74 and 0.86 of SQLite's speed; 0.73 in the earlier run):
  an ordered traversal that returns 20 to 77 rows of 272 bytes each.

Cold, 1 905 pages, p50:

| | SQL | `QUERY2` |
| --- | --- | --- |
| open | 22.8 to 23.0 ms | 2.6 ms |
| run, by shape | 1.3 to 6.6 ms | 0.09 to 0.22 ms |
| total | 24.5 to 29.7 ms | 2.7 to 2.8 ms |

The index of the layer is kernel memory and is not dropped, so the cold `QUERY2` run is 4 to 5
times its warm time, while SQLite reads its pages again.

## Found on the way

- **ai-memory's scheduled maintenance changes the workload.** Sixty seconds after start the server
  runs a rule-based lint, which writes `_lint/report.md` in the projects that exist by then, and a
  forget sweep, which deletes their expired pages. In the 2 000-page run the first three projects
  lost 27 to 38 pages each (1 905 latest pages left) and the "not expired" predicate filters
  nothing there. It also created a `_global` project with one page. Equality is unaffected
  (the mirror and both sides read the same state), but the mix is no longer the one written.
- **Its write cost grows with the store:** 75 ms per page in the first project, 221 ms in the
  tenth (2 000 pages in 320 s), close to linear. The plan's "about 66 ms per page, one hour for
  60 000 pages" does not hold. Not investigated: whether the growth is ai-memory's own or the
  layer's is not known.
- At 150 pages ai-memory leaves a 4 MB write-ahead log next to a 0.9 MB database. A sole
  connection recovers it at open, which is probably most of SQLite's 17 to 23 ms cold open; not
  measured separately. The tool's opens rewrite `memory.sqlite-shm` (the WAL index), nothing else.

## Limitations

Two small scales, one run each, one host; at most 200 pages per project. Not an M4 result. The
loaded module predates the `GET_INFO` change of journal 0061. The 15 µs floor of `QUERY2` (a request
of about 4.5 KiB copied in and out) is a hypothesis, not profiled. The cold open of SQLite includes
the recovery of the log ai-memory left, which a checkpointed database would not pay. The mirror
still runs offline.

## Next step

Stage 2 scale. First find why ai-memory's write cost grows (same workload with its `wiki` on plain
ext4) and decide whether to turn its scheduled maintenance off for the run; then the scales up to
60 000 pages with equality, latency, mirror cost and index memory.

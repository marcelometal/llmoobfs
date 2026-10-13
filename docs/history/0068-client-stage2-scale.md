# 0068 — Real-client test, stage 2: scale to 60 000 pages

Date: 2026-10-12. Status: measured from 2 000 to 60 000 pages on the benchmark host, one run per
scale; the live mirror is not built. Local series.

## Goal

Stage 2 of `docs/client-test-plan.md` at scale: equal answers, latency, the mirror's cost and the
index memory up to the 60 000 pages of journal 0044. ai-memory is the client used; the questions
are about the layer.

## What had to change first

Journal 0065 found two behaviours of the client that made the planned run impossible or
misleading. Both were settled before measuring.

- **Scheduled maintenance off.** `stage2.sh` writes `[maintenance] enabled = false` into the
  client's `config.toml` (`MAINTENANCE=1` restores it). The server log confirms it, and no lint
  report or forget sweep appeared. One page still appears on its own in a `_global` project: a
  "profile pass" on the write path, not a scheduled job. It is mirrored and compared like any other.
- **The client's write cost, with and without the layer.** The same 2 000 pages through MCP with
  its `wiki` on plain ext4 (`PLAIN=1`) and on the layer: 66 to 205 ms per page on ext4, 74 to
  233 ms on the layer, first project to tenth (293 s against 322 s). The growth is the client's:
  every MCP write ends with a git checkpoint of the whole wiki (`commit_all` in its
  `wiki/src/git.rs`, which walks and adds every file). This was read in its code, not profiled.
  At 6 000 pages the last project costs 488 ms per page and the run took 30 minutes; 60 000 pages
  through MCP would take days.

## Decision: a second way to load

`bench/client/aimem-bulk.py` writes the same mix as page files in the client's own layout, and the
client's `reindex` command builds its SQLite from them (`BULK=1` in `stage2.sh`). The rows are
still produced by the client's code. Cost: about 3 to 4 ms per page (60 000 pages in 225 s).

Alternative: stop at the scale MCP reaches in an hour (about 8 000 pages). Rejected: the plan asks
for 60 000 and the index memory at that size.

One difference matters and is measured below: `reindex` assigns `updated_at` directory by
directory, so the ordering key is correlated with the page family. Through MCP the families are
interleaved in time.

## Evidence

Mac mini (i7-2635QM, Debian 13, kernel 6.12.111), module of `main` with the rename fix, ai-memory
385b81e, lower ext4 on an SSD, ten projects, maintenance off
(`bench/results/2026-10-12-macmini-aimem-stage2-scale/`).

**Equal answers** at every scale: 66 of 66 checks at 2 001 and 6 001 pages (MCP), 60 of 60 at
6 000, 20 000 and 60 000 (bulk). No row-count mismatch in 131 040 latency samples and no result
with an unknown path, also after dropping the caches.

**Warm latency**, p50 in µs, SQL / `QUERY2`:

| Shape | MCP 2 000 | MCP 6 000 | bulk 6 000 | bulk 20 000 | bulk 60 000 |
| --- | --- | --- | --- | --- | --- |
| recent | 18.9 / 20.3 | 21.4 / 21.8 | 21.4 / 21.4 | 22.2 / 21.7 | 32.1 / 24.1 |
| pinned | 63.1 / 27.9 | 70.2 / 28.6 | 22.1 / 22.2 | 23.3 / 22.3 | 24.4 / 23.2 |
| rules | 88.9 / 30.4 | 111.1 / 36.5 | 39.7 / 25.1 | 72.3 / 30.2 | 177.5 / 46.6 |
| settled | 244.1 / 48.8 | 716.6 / 110.6 | 608.2 / 107.8 | 2 007.8 / 336.5 | 7 540.8 / 1 506.4 |
| stale | 28.4 / 27.5 | 31.0 / 29.1 | 32.3 / 28.8 | 30.2 / 28.9 | 64.5 / 48.3 |
| incremental | 46.2 / 65.5 | 122.1 / 176.2 | 121.2 / 169.6 | 390.6 / 611.3 | 1 394.0 / 2 738.9 |

- `settled` (a set of kinds, 1 538 rows per project at the largest scale): `QUERY2` is 5 to 6.5
  times faster at every scale. The SQL computes `kind` from the JSON front matter for every row of
  the project.
- `rules` (a family filter with an ordering and a limit): 1.6 to 3.8 times faster, 2.9 to 3.0 on
  the two stores written in real order.
- `recent` and `stale`: equal up to 20 000 pages; 1.3 times faster at 60 000.
- **`incremental` loses, and loses more as the answer grows:** 0.71 of SQLite's speed at 2 000
  pages, 0.64 at 20 000, 0.51 at 60 000 (2 549 rows per project, about 1.07 µs per row against
  0.55). Each result is a 272-byte record copied to the caller, 256 per call.
- **Bulk against MCP at 6 000 pages:** four shapes agree within 20 %. `pinned` and `rules` do not
  (SQL 22.1 against 70.2 µs, and 39.7 against 111.1). The probable reason is the correlated
  ordering: when the pinned pages or the rules were indexed together, the newest twenty are found
  without skipping other families. Not verified. For those two shapes the bulk numbers understate
  what SQLite pays on a store written in real order, and the MCP columns are the ones to quote.

**Cold**, 60 000 pages, p50: opening costs 17.4 ms on SQLite and 2.5 ms on the layer (17 to 22 ms
and 2.5 ms across the scales). After that the query itself takes 2.0 to 129 ms in SQL (129 ms for `settled`, 73 ms for
`incremental`) and 0.07 to 4.0 ms in `QUERY2`, whose index stays in memory.

**Mirror:** 74 to 78 µs per page (five or six attributes), 8.5 s for 60 000 pages, offline.

**Index memory** (`GET_INFO` as root): 3.0, 10.1 and 30.3 MiB at 6 000, 20 000 and 60 000 pages;
530 bytes per page and 99 per attribute entry, linear. The default budget of 128 MiB holds about
250 000 pages of this shape. ai-memory's database is 12, 27 and 70 MB at the same scales, with
page bodies and full-text index included.

## What this says about the layer, and what is the client's

About the layer:

- answers equal to a SQL engine's on six shapes up to 60 000 files in ten domains;
- faster where the engine must evaluate an expression or a filter per row, equal on plain ordered
  scans with a limit, slower on long ordered traversals because of the result record;
- unaffected by a cold page cache, apart from the walk to the directory;
- 530 bytes of unpageable memory per file with six attributes.

The client's own, which must not shape the layer:

- the git checkpoint on every write and the cost that grows with the store;
- scheduled maintenance and the profile pass that add and remove pages;
- `kind` kept inside a JSON column, which is why `settled` is slow in its SQL. Another system with
  `kind` as an indexed column would narrow that gap;
- the leftover write-ahead log that its cold open has to recover.

## Limitations

One run per scale, one host, one client, ten projects of equal size. Not an M4 result. The three
largest scales were loaded by `reindex`, with the ordering difference described above. The loaded
module predates journals 0061 and 0069. The mirror runs offline after the load: the cost of
keeping attributes in step with live writes, against what the client spends on SQLite, is the
part of the plan's "done when" that is **not** met. The floor of about 20 µs per `QUERY2` call and
the per-row cost of long answers are not profiled.

## Next step

Stage 2 is closed for answers, latency and index memory; the live mirror stays open. In the agreed
order (journal 0059) the remaining M3 comes next. Two proposals from `docs/adoption-risks.md` wait
for the maintainer: a client contract and a second client before stage 3, and a smaller result
record for long traversals only if the other surveyed systems also read long ordered answers.

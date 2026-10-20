# 0139 — Steady state measured with a test and an interval; a 2x query regression found and fixed

Date: 2026-10-19. Status: measured, bisected and fixed; the fix waits for its pull request's
KASAN job.

## Goal

Answer, under the method of journal 0134, the question left by the report of 2026-10-14
(`bench/results/2026-10-14-macmini-release/`): were the on-demand modules 8 to 15 % slower than
the module built at mount, or was that noise? And measure today's `main` the same way.

## Method

Benchmark host, data of 2026-10-13 (domain `ns3`, about 7 500 files), CPU frequency held still
(`bench/cpu-freq.sh fix`, restored after), every module freshly loaded and mounted for each
repetition, a discarded warm-up, module order rotated per repetition, `bench/compare.py` on the
per-repetition p50 (95 % bootstrap interval, Mann-Whitney U). Before the run the driver
unmounted the seven long-standing benchmark mounts of earlier sessions (`/srv/moob/layer-*`,
`/srv/moob/q2-*`, `/srv/moob/udx-layer`, listed in `environment.txt`); they were not remounted.
Results, scripts and tables: `bench/results/2026-10-19-macmini-steady/`.

## Results

- **The 8 to 15 % was real.** With 40 repetitions, 0102 (index on demand, ADR-0012) is 7.4 %,
  9.9 % and 12.3 % slower than 0084 on the three filtered shapes, intervals well clear of zero,
  and about 4 % on the "newest 20" shapes.
- **`main` (7292cee) was much slower:** +99 %, +145 % and +31 % against 0084 on the filtered
  shapes (+85 %, +123 %, +17 % against 0102), while the "newest 20" shapes moved by 4 to 6 %.
- **Bisection** over the merges that touched `layer/` since 0102: pull requests 67 and 70 are
  within ±1.2 % of 0102; pull request 71 (`6502d46`, journal 0105) carries the whole step;
  every later merge equals it.

## Cause

Pull request 71 fixed finding F7: query results are copied to user space only after
`query_sem` is released. To do that, `do_query()` and `query2_one()` allocated a kernel buffer
for the **requested limit** on every call, `kvmalloc_array(q->limit, 272 bytes)`. Clients ask
for the maximum, 4 096, so each query allocated, and freed, about 1.1 MB through vmalloc; the
cost does not depend on the number of results, which is why the "newest 20" shape, limited to
20, barely moved. The code before that change had said so in a comment: one result at a time,
"so the kernel never allocates (or zeroes) a limit-sized buffer".

## Fix

`moobl_qbuf_get()` / `moobl_qbuf_put()` (`layer/main.c`): the buffer is kept with the open file
(`struct moobl_file`), grown to the largest limit used on it and freed when the file is
released. A query on a file whose buffer is lent to another query at that moment (two threads,
or two processes after `fork`, on one open file) gets a buffer of its own, as before. F7 is
unchanged: the copy still happens after `query_sem` is released, and the allocation happens
before it is taken. Nothing of an earlier query can reach the caller: `put_result()` zeroes
every record before filling it, and only the `count` records written by this call are copied.
Memory: up to 1.1 MB stays allocated per open file that queried with the maximum limit, until
it is closed.

Alternatives considered: running the query in chunks of a small buffer (breaks the single-call
snapshot of a page); sizing the buffer by the domain's entry count (needs the lock first, and
the count can grow); a mount-wide pool (contention, and a per-file lifetime is simpler).

Test: the suite's `race2` run (concurrent renames, chmods and moves against a paginating
`QUERY2`) now runs as two processes sharing one open file (`MOOB_RACE2_FORK=1`), so the
"buffer lent" path is taken under the same race.

## Measured after the fix

20 repetitions, 0102 / main / fix: v2-dom 178 / 331 / 180 µs, 117 / 256 / 118 µs,
1 352 / 1 572 / 1 402 µs; v2-recent unchanged. Against 0102 the fix is within +1.2 %, +1.4 %
(no difference detected) and +3.7 %.

## Limitations and next step

- One host, one data set, one kernel; the bisect and fix runs have fewer than 30 repetitions
  (they locate and confirm; the 40-repetition run is the measurement).
- The residual +3.7 % on the 3 030-result shape is not explained.
- Next: the pull request's KASAN job; a 40-repetition rerun of 0084 / 0102 / fixed `main` to
  replace the numbers of the README's measurements once merged.

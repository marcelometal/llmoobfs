# 0029 — `QUERY` scale series (1000 to 60 000 memories)

Date: 2026-10-11. Status: completed (pilot-level series, not an M4 result).

## Goal

Option 1 decided by the maintainer: measure `QUERY` at larger scales with the current disks,
since queries run from memory and do not depend on the bursting of the P6 disks. Question: how
the per-result and per-candidate costs behave with the size of the set, and how much space the
index takes.

## Execution

Azure host (journals 0025, 0027, 0028), module and harness from commit `f4f98b0` (main with the
seek from the first candidate and the RCU revalidation, plus the query-only mode of `run.sh`).
Scales of 1000, 5000, 10 000, 25 000, 40 000 and 60 000 memories, profile 8, seed 1, three
repetitions, W3 with 50 runs per shape. Only the ext4 pair (btrfs and XFS followed the same
pattern in the pilots). Evidence in `bench/results/2026-10-11-azure-query-scale/`; raw package
in the pre-release `bench-2026-10-11-azure-query-scale`.

Changes to the harness for the series (branch `m4/query-scale`): `QUERY_ONLY=1` mode in
`run.sh`; `equiv.sh` records a response truncated by the 4096-result limit as
`EQUIV_TRUNCATED`, without aborting, and keeps comparing SQLite and the oracle; SQLite load in a
single pass of `find -exec stat` and `awk` (the previous version created four processes per
memory; the first attempt with `find -printf` failed in the VM because busybox's `find` does not
support it).

## Results (median, ext4, as root)

| Memories | Q3 SQLite | Q3 `QUERY` | Q4 SQLite | Q4 `QUERY` | Q5 SQLite | Q5 `QUERY` | Q8 SQLite | Q8 `QUERY` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 000 | 15 µs | 34 µs | 26 µs | 67 µs | 43 µs | 126 µs | 19 µs | 5 µs |
| 5 000 | 24 µs | 457 µs | 56 µs | 500 µs | 281 µs | 676 µs | 23 µs | 6 µs |
| 10 000 | 33 µs | 1.10 ms | 96 µs | 1.14 ms | 541 µs | 1.50 ms | 25 µs | 8 µs |
| 25 000 | 100 µs | 2.83 ms | 394 µs | 2.90 ms | 1.92 ms | 3.69 ms | 53 µs | 8 µs |
| 40 000 | 205 µs | 4.64 ms | 838 µs | 4.68 ms | 3.40 ms | 6.11 ms | 64 µs | 7 µs |
| 60 000 | 392 µs | 6.95 ms | 1.45 ms | 6.99 ms | 5.18 ms | 9.15 ms | 67 µs | 7 µs |

Q3 is `namespace = X AND agent = Y` (253 results with 60 000), Q4 is `namespace = X AND kind = Y
AND created >= T` (1021), Q5 is `pinned = 1 AND score < 500` (2926), Q8 matches nothing because
`tag1` does not exist in profile 8. The broad shapes (Q1, Q2, Q6, Q7, Q9) exceed 4096 results
from 10 000 to 40 000 memories onward and are truncated by `QUERY`; they stop being comparable.

Index: 600 bytes accounted per memory with 8 attributes at all scales (36 MB with 60 000
memories, 437 882 entries). By the index's own accounting, the default budget of 128 MiB would
hold about 220 000 memories of this profile; the accounting underestimates the real RAM (slab
rounding, xarray nodes), so the real number is lower. Index rebuild on remount (W7): 19 ms with
1000 memories, 89 ms with 5000 and 192 ms with 10 000, about 19 µs per memory; at the larger
scales W7 produced no value (see limitations).

## Reading

- The cost of queries with more than one condition grows with the size of the set, not with the
  number of results: `QUERY` searches by the first condition and tests the others on each
  candidate, at about 0.93 µs per candidate (Q3 with 60 000 memories: 7500 candidates, 6.95 ms).
  With a composite index, SQLite grows much less; on Q3 the gap goes from 2 times with 1000
  memories to 18 times with 60 000.
- Query with no match: `QUERY` is 4 to 9 times faster than SQLite at all scales.
- Where the SQLite planner chooses a scan (Q5), the gap is smaller (1.8 times with 60 000).
- The 4096-result limit of UAPI v1 is a real constraint at this scale: most of the broad shapes
  do not fit in one response, and there is no pagination.

These results support the proposal of ADR-0009
(`docs/decisions/0009-query-role-and-user-space-index.md`) and its alternative C' (small
intersection in the kernel), which attacks exactly the per-candidate growth measured here.

## Limitations

- Pilot-level series: one host, one kernel, disks with bursting, three repetitions.
- W7 used the broadest shape as its first query; from 25 000 memories onward the response was
  truncated and the loader recorded the remount as an error. Fixed in `bench/load.c` (a
  truncated response counts as index ready); the rebuild time at those scales needs to be
  measured again.
- SQLite prepares the statement on every call, which makes it a little slower than an
  application with prepared statements.

## Next step

Maintainer decision on ADR-0009; if the direction is accepted, measure in M4 the variants it
lists, and measure W7 again at the larger scales.

# 0094 — The user-space baseline measured against the layer

Date: 2026-10-13. Status: one run on the benchmark host; pilot-grade. Local series (after pull
request 64).

## Goal

Journal 0092 built the alternative without a kernel module and compared answers, not time. This
journal runs both on the same host and data: 60 000 memories of profile 8, ext4, the baseline on
a plain directory followed by `udx-watch`, the layer on a fresh mount.

## Method

`bench/userspace/compare.sh` (new): the same manifest written with `bench-load` W1 and W4 to
both; equal answers checked against the oracle; then five query shapes in domain `ns3`, three
repetitions of 50 calls, through `bench-sqlclient` on the baseline's database and through
`QUERY2`. Results in `bench/results/2026-10-13-macmini-udx-compare/`.

## Results

Equal answers: the oracle, the baseline and the layer return the same 253, 253 and 3 030 files
on the three shapes checked.

Writes, as the writer sees them (median):

| | baseline | layer |
| --- | --- | --- |
| create with attributes, `fsync`, rename, directory `fsync` | 13 169 µs | 13 428 µs |
| set one attribute | 10.5 µs | 16.2 µs |
| set and remove one attribute | 14.1 µs | 20.5 µs |

The baseline's writer pays nothing for the index; the follower pays later: 54.9 s of CPU for
about 180 000 changes (0.3 ms each), and the kernel's event queue overflowed twice during the
load, each forcing a full pass. Right after the writes the index reported
`watcher=absent state=resync`: a reader would have been told the index was not live.

Queries, median in µs:

| Shape (results) | baseline, generic SQLite schema | layer, `QUERY2` |
| --- | --- | --- |
| `namespace AND agent` (253) | 6 557 | 120 |
| `agent` (253) | 240 | 77 |
| `kind` (3 030) | 2 221 | 925 |
| 20 most recent with `kind` | 3 997 | 26 |
| 20 most recent with `agent` | 439 | 136 |

Resources: the follower held 19 to 22 MB of ordinary, pageable memory; its index is 69 MB on
disk for the 60 000 files. The layer's index is 36.4 MB of kernel memory that cannot be paged.

## Reading, with the caution it needs

- **The layer is 2 to 150 times faster than this baseline on these queries, and most of that is
  the baseline's schema, not user space.** The baseline stores one row per attribute, because
  like the layer it knows no schema in advance; a conjunction is then a join, and SQLite begins
  with the wrong term on the first shape (every file of the domain matches `namespace`).
  `ANALYZE` did not change it. The earlier SQLite baseline of journal 0047, with one wide table
  and composite indexes written for these queries, answered the same first shape in 178 to
  476 µs and the others within 1.1 to 6 times of the layer.
- So there are two user-space alternatives and they bracket the layer: **an index that knows the
  application's schema is close to the layer; a generic one that accepts any attribute, as the
  layer does, is far behind**, at least as built here. A better generic design in user space
  (the layer's own ordered sets per attribute, in a daemon) was not tried.
- **Writes are a tie for the writer**; the baseline defers 0.3 ms of CPU per change to another
  process and, under a bulk load, falls behind and resynchronizes.
- The baseline wins on memory kind (pageable), persistence and risk; the layer on memory size.

## Limitations

- One run, one host, one data set, one domain queried; several rows are flagged inconclusive by
  the harness. `bench-sqlclient` prepares each statement on every call, which costs the
  baseline a little on the fast shapes and nothing that matters on the slow ones.
- The baseline's queries ran as root on a database readable only by its owner; the layer's as an
  ordinary user through the domain's permissions.
- Freshness was not measured, only observed (the status line and two overflows). The settle
  check of the script matched the wrong word and is fixed for the next run.
- The baseline is a first implementation by one tool-assisted pass; a person who tunes SQLite
  for a living might do much better with the generic schema.

## What this changes

The M4 report said the comparison with a user-space index was missing. It now exists in two
forms, and the statement the evidence supports is: an application that knows its schema and
keeps its own index loses little speed and avoids the kernel module; what it cannot have is
validation at write time, an answer consistent with a write and with permissions at once, and
sharing between users without a privileged process (journal 0092). A generic index, which is
what the layer offers to any client, is where being in the kernel paid off in this run.

## Next step

A freshness instrument for the baseline; the same comparison at 20 000 and 200 000 files; an
outside reader for both journals.

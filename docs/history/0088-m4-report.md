# 0088 — M4 report: the four questions of the benchmark plan, answered with what was measured

Date: 2026-10-13. Status: **complete as a pilot-grade report**; the series the plan asked for
and that were not run are listed in each "not measured" paragraph and in the last section.
Three campaigns were run for this report: space (journal 0085), latency per operation on the
current module (section 2.1) and the write-side cost of a side index (section 2.4). Everything
else is quoted from the campaigns of the journals named.

This is the exit criterion of `docs/benchmark-plan.md`: "the four questions answered by numbers,
including the cases in which the layer loses. If the layer shows no measurable advantage in any
of the four, that is a valid result and changes the course of the project."

## How to read the numbers

- Two hosts. **Azure**: one cloud VM, early modules, 1 000 to 60 000 files (journals 0025 to
  0031). **Mac mini**: the dedicated host of journal 0042 (Debian 13, kernel 6.12.111, one SATA
  SSD), from journal 0043 on. Numbers from different hosts are never compared with each other.
- Every campaign is **pilot-grade**: one data-set seed, three repetitions at most, often one run.
  The journals put the run-to-run noise at about 20 to 25 % for values of tens of microseconds.
  A difference smaller than that is called a tie here.
- Latencies are medians (p50), warm unless stated.
- `QUERY` is the first interface (v1, on the mount root, a permission check per result).
  `QUERY2` is the per-domain interface (v2), which is the one a client would use.
- The SQLite baseline of the synthetic campaigns prepares each statement on every call, which
  costs it a little; the real-client comparison prepares once.
- The index code changed after every measurement below (journals 0082 and 0083: lock scope,
  record removal on rename). Sections 2.1 and 2.4 were measured after those changes; nothing else was.

## Question 1 — Is the layer's query faster or cheaper than an index the application keeps?

**Answer: `QUERY2` is faster than SQLite with composite indexes on selective queries inside one
domain, by 1.1 to 6 times; the first interface (v1) is slower than SQLite on everything that
returns results; and `QUERY2` loses on long ordered traversals. "Cheaper" (CPU, memory) was not
measured.**

Synthetic data set, 60 000 memories with 8 attributes, Mac mini, ext4 (journals 0044, 0047,
0048, 0049; µs):

| Query (results) | SQLite | `QUERY2` | |
| --- | --- | --- | --- |
| `namespace AND agent` (253) | 178 to 476 | 119 to 162 | `QUERY2` 1.1 to 3.7 times; a tie at SQLite's best case |
| `namespace AND agent AND kind` (98) | 509 | 82 to 114 | `QUERY2` 4.5 to 6 times |
| `namespace AND kind AND created ≥ T` (1 021) | 1 729 | 569 to 723 | `QUERY2` 2.4 to 3 times |
| 20 most recent with `kind` | 47 | 19 to 26 | `QUERY2` about 2 times, after journal 0048 |
| 20 most recent with `agent` | 279 | 102 to 141 | `QUERY2` 2 to 2.7 times, after journal 0048 |

The same shapes on btrfs and XFS give the same ordering (journal 0049).

Where the layer loses or ties:

- **v1 `QUERY` loses to SQLite on every shape that returns results**, on both hosts, and by more
  as the store grows: 2 times slower at 1 000 memories and 18 times at 60 000 for
  `namespace AND agent` on Azure (journal 0029); 3.9 to 4.1 ms against 0.18 to 0.48 ms on the Mac
  mini. Its only win is a query that matches nothing. It also cannot return more than 4 096
  results in one call, where SQLite returned 24 595 rows in 46 ms.
- **Before journal 0048, `QUERY2` lost the "N most recent" shapes** to SQLite by 4 and 6 times.
  The numbers above are after that optimization.
- **Long ordered traversal** (the real client's `incremental`): `QUERY2` runs at 0.74 to 0.51 of
  SQLite's speed, worse as the answer grows (question 4).
- **Fields of the results.** A query returns inode numbers and paths. A client that needs field
  values reads them file by file: 2.0 µs for one attribute and 16.4 µs for all, against 0.9 µs
  per result for the query itself (journal 0072). SQL returns columns in the same call. Every
  comparison above selects only the path, so it understates the layer's cost for such a client.
- **Cold start**: v1 took 179 ms cold against 5.4 ms warm (journal 0055). And a mount scans every
  file before any query is answered (question 2).

Not measured: CPU and memory per query; concurrency; paging a whole broad answer by cursor
against SQLite returning it at once; the cost of keeping a side SQLite in step on each write;
`QUERY2` at other scales on the synthetic data set; p95 and p99.

## Question 2 — What does the layer cost?

**Answer: 2 to 5 µs more per read by path, 6 to 13 µs more per attribute write, nothing
measurable on a create that ends in `fsync`; 12 to 20 µs per file to mount with a warm cache and
0.3 to 2 ms per file with a cold one; 530 to 607 bytes of unpageable kernel memory per file. The
128 MiB default was never reached: it is extrapolated to 220 000 to 250 000 files. Keeping a
side SQLite in step costs about as little at write time as the layer's attributes do.**

### 2.1 Latency per operation against the bare lower filesystem

Measured for this report on the Mac mini with the current module (the layer code of
`v0.3.0-experimental`): `bench/run.sh`, 10 000 memories, profile 8, 4 processes, 3 repetitions,
medians in µs, **bare lower / through the layer**
(`bench/results/2026-10-13-macmini-q2-posix/`):

| | ext4 | btrfs | XFS |
| --- | --- | --- | --- |
| read by known path (open, read, close) | 8.5 / 11.0 | 10.1 / 12.0 | 9.8 / 15.0 |
| set one typed attribute | 8.9 / 21.7 | 8.9 / 21.3 | 6.0 / 11.6 |
| set and remove one typed attribute | 12.0 / 38.1 | 21.1 / 42.2 | 9.9 / 18.3 |
| create with attributes, `fsync`, rename, directory `fsync` | 13 153 / 13 057 | 13 785 / 14 858 | 11 640 / 11 801 |

The harness marks 43 of its lines inconclusive (spread between repetitions), among them most
attribute rows on ext4 and btrfs; the direction is the same in every row and on every lower. In
the same run the kernel, SQLite and the oracle gave equal answers on every query shape that fits
the 4 096-result limit.

Earlier figures, Azure, 1 000 memories, modules before `QUERY2` (journals 0025 to 0028; µs,
plain / layer), for comparison of the ordering only:

| | ext4 | btrfs | XFS |
| --- | --- | --- | --- |
| read by known path (open, read, close) | 9.3 / 12.5 to 13.0 | 9.1 / 12.3 to 12.8 | 9.7 to 10.7 / 13.4 to 14.5 |
| set one typed attribute | 10.0 / 23.7 to 24.5 | 18.3 to 18.9 / 26.5 to 28.6 | 8.0 to 8.2 / 13.4 to 13.7 |
| set and remove one typed attribute | 16.0 / 42.1 to 43.6 | 47.3 to 48.5 / 72.4 to 77.9 | 13.9 to 14.1 / 23.8 to 25.0 |
| create with attributes, `fsync`, rename, directory `fsync` | 9 552 to 9 899 / 9 693 to 10 149 | 14 403 to 15 257 / 15 077 to 15 852 | 4 633 to 4 787 / 4 477 to 4 872 |

The layer is slower on every read and attribute operation, as a stacked filesystem must be. The
create is a tie dominated by `fsync` (2 to 4 ms each on those disks), and stays one from 1 000 to
60 000 memories. On the Mac mini, a read costs 6.0 µs per file through the layer and 4.5 µs on
ext4 directly, and reading all attributes 16.4 against 15.3 µs (journal 0072). Domains add about
1 % to a create and nothing measurable to an attribute update (journal 0055).

### 2.2 Mount time

The index is rebuilt by scanning every file at each mount; no query is answered before it ends.

| Files | Warm cache | Cold cache | Host |
| --- | --- | --- | --- |
| 25 000 | 0.47 s | 49 s | Azure |
| 40 000 | 0.79 s | 75 s | Azure |
| 60 000 | 1.18 s | 126 s | Azure |
| 60 000 | 0.71 to 0.74 s | 18.5 to 19.3 s | Mac mini |

Linear in the number of files: about 12 to 20 µs per file warm; cold, 0.31 ms per file on the
SATA SSD and about 2 ms on the cloud disk (journals 0031, 0044, 0055). A SQLite file needs no
such scan; the real client's database opened cold in 17 to 23 ms.

### 2.3 Index memory

By the index's own accounting, which underestimates real kernel memory (slab rounding, tree
nodes): 600 to 607 bytes per file with 8 attributes (36.4 MB at 60 000 files), 530 bytes per file
with 6 attributes and 99 bytes per attribute entry on the real client's data (30.3 MiB at 60 000
pages), linear at every scale tried (journals 0029, 0044, 0068). This memory cannot be paged
out. For comparison, the real client's whole database, page bodies and full-text index included,
is 70 MB on disk at 60 000 pages.

**The 128 MiB budget was never exhausted in a test.** Dividing gives about 220 000 files of the
synthetic profile and about 250 000 pages of the real client's shape; the real figures are lower
because the accounting undercounts. What happens at and past the limit was tested only for
correctness (the index is marked incomplete), not for scale.

### 2.4 What a side index costs at write time

The project's motivation says an application-kept index must be "kept in step on every write".
That cost had never been measured. `bench/synccost.c` publishes each memory durably (temporary
file, `fsync`, rename, `fsync` of the directory) in five ways; 3 000 memories of 3 000 bytes with
eight fields, caches dropped before each run, three repetitions, median of the three medians, in
milliseconds per memory (`bench/results/2026-10-13-macmini-synccost/`):

| | ext4 | btrfs | XFS |
| --- | --- | --- | --- |
| the file only (nothing queryable) | 7.83 | 8.51 | 6.57 |
| the file with eight typed attributes, no layer | 10.50 | 8.58 | 6.60 |
| the same through the layer (validated and indexed) | 9.81 | 8.72 | 6.96 |
| the file, then a row in SQLite, as durable as the file (`synchronous=FULL`) | 9.69 | 13.08 | 8.26 |
| the file, then a row in SQLite, `synchronous=NORMAL` | 7.91 | 8.72 | 6.63 |

- **A side SQLite in its usual mode adds almost nothing**: 0.06 to 0.2 ms on a write of 6.6 to
  8.5 ms. In that mode the last rows can be lost on power failure while the files survive, so
  the index can lag the files after a crash and must be reconciled.
- **Made as durable as the file, it costs 1.7 to 4.6 ms more**, which is more than the layer's
  attributes cost on btrfs and XFS (0.2 to 0.4 ms) and about the same on ext4 (2.0 ms, where the
  attributes themselves force a second block to be synced: 2.7 ms even without the layer).
- **The layer's own share is small in every case**: within 0.4 ms of the same attributes written
  without it, and on ext4 below them in two repetitions of three (noise of that order).

So the layer does not make writes cheaper than a side index in the mode most applications use.
What it removes is the second source of truth: there is no index that can disagree with the
files after a crash, because the fields are in the file's own metadata and are synced with it.
That is a consistency argument, and it should be made as one.

Not measured: `stat`, `unlink`, `rename` and plain `write` against the bare lower; any operation
under 1, 10 and 100 concurrent processes; write amplification; CPU; real kernel memory of the
index; mount time of the real client's tree and on btrfs or XFS above 1 000 files.

## Question 3 — Do reflink and dedupe save space on agent-memory loads?

**Answer: they work through the layer and save what the lower filesystem can save, which for
small memories is little. The saving is the lower's, not the layer's, and on ext4 there is none.**

Measured in journal 0085 on the 60 000-file data set (183 MB of content, mean 3 KiB per file,
15 % of the files byte-for-byte copies by the generator's parameter):

| | btrfs | XFS |
| --- | --- | --- |
| whole-file dedupe through the layer, calls accepted | 9 102 of 9 102 | 9 102 of 9 102 |
| space freed by dedupe | 6.7 % | 13.6 % |
| a reflink copy of the tree, against a plain copy | 47 % of the cost | 9.5 % of the cost |

Half of these files are at most 2 KiB, which btrfs keeps inside its metadata, where nothing is
shared; a filesystem cannot share less than a block in any case.

Not measured: how much duplicate content a real agent-memory store contains (the 15 % is a
parameter); a real client's store on btrfs or XFS; the latency of clone and dedupe through the
layer against the bare lower.

## Question 4 — Does a real client run unchanged over the layer at an acceptable cost?

**Answer: one client (ai-memory) runs unchanged, with three conditions; its own tests and
retrieval results are identical on the layer; its writes are about 10 % slower in one run. Used
for its structured queries, `QUERY2` gives the same answers as its SQLite from 150 to 60 000
pages, faster on three shapes, equal on two, slower on one. The plan never said what cost is
"acceptable", so that judgement is left to the reader.**

Unchanged, on the layer against plain ext4 (journals 0054, 0057; one run each):

- its 1 147 tests pass on both (316 s against 297 s);
- LongMemEval-S retrieval, 50 questions: identical results question by question; query p50 15
  against 14 ms, p95 45 against 36 ms;
- writing 2 000 pages through its own interface: 322 s against 293 s (journal 0068).

The three conditions: a layer defect it exposed had to be fixed first (rename over a
just-deleted name, journal 0057); its files must be created with `umask 077`, or none of them
is queryable; and for `QUERY2` a separate process must copy its fields into typed attributes
(78 µs per page offline; a live mirror kept 2 001 pages in step, 11 ms behind at the median,
without slowing its writes, journal 0071).

Its six structured queries, its own SQLite against `QUERY2` (journal 0068; warm, µs, SQL /
`QUERY2`; one run per scale):

| Shape | 2 000 pages | 20 000 pages | 60 000 pages | |
| --- | --- | --- | --- | --- |
| settled | 244.1 / 48.8 | 2 007.8 / 336.5 | 7 540.8 / 1 506.4 | `QUERY2` 5 to 6.5 times faster |
| rules | 88.9 / 30.4 | 72.3 / 30.2 | 177.5 / 46.6 | `QUERY2` 1.6 to 3.8 times faster |
| pinned | 63.1 / 27.9 | 23.3 / 22.3 | 24.4 / 23.2 | faster on stores written in real order, a tie on bulk-loaded ones |
| recent | 18.9 / 20.3 | 22.2 / 21.7 | 32.1 / 24.1 | a tie up to 20 000, 1.3 times faster at 60 000 |
| stale | 28.4 / 27.5 | 30.2 / 28.9 | 64.5 / 48.3 | a tie up to 20 000 |
| incremental | 46.2 / 65.5 | 390.6 / 611.3 | 1 394.0 / 2 738.9 | **`QUERY2` loses: 0.71, 0.64, 0.51 of SQLite's speed** |

Answers were equal in every check (66 of 66 and 60 of 60 per scale) and in 131 040 latency
samples. Cold, at 60 000 pages, opening costs 17.4 ms on SQLite and 2.5 ms on the layer.

Where the layer loses, ties, or wins for a reason that is the client's:

- `incremental`, at every scale, by more as the answer grows: a result record is 272 bytes and
  the traversal pays about 1.07 µs per row against 0.55.
- `settled` is slow in SQL because this client keeps `kind` inside a JSON column. A system with
  `kind` as an indexed column would narrow the gap; the win is partly this client's schema.
- The fields of the results are not counted (question 1).
- A page is queryable about 11 ms after it is written, not when the write returns.
- In all of stage 2 the client itself never calls `QUERY2`: a test tool issues both sides'
  queries. The client calling it (stage 3) has not been done.

A second client, the LangGraph store, gives the same answers as its reference store through an
adapter (journal 0089), with no latency measured. Not measured: stage 3; any client
on btrfs or XFS; repetitions; CPU and memory.

## What this changes

The plan's last sentence asks whether the layer shows a measurable advantage in any of the
four questions. It does in one, with limits; the other three are costs or neutral.

1. **The advantage exists and is narrower than "faster than SQLite".** It holds for `QUERY2`,
   for selective conjunctions inside one domain, when the caller wants paths. It does not hold
   for the first interface, for long ordered traversals, or for a caller that needs field values.
   Any public statement should carry those three limits. The ROADMAP row that said "faster on
   every measured shape" is corrected with this journal.
2. **The largest cost the comparisons leave out is reading the fields of the results**
   (16 µs per file for all attributes against 0.9 µs per result for the query). This is the
   measured case for returning attribute values with the results, which the ROADMAP keeps under
   "depends on validation". The numbers support writing its ADR; whether to build it is the
   maintainer's, after a second client shows the same need.
3. **The traversal loss has a known cause**, the 272-byte result record and its per-row cost.
   A smaller record or a projection would address it; neither is designed.
4. **Write-time cost is not an argument for the layer.** A side SQLite in its usual mode costs
   almost nothing per write (section 2.4). The claim the evidence supports is narrower: one
   source of truth that cannot diverge, and authorization decided by the operating system.
   Texts that say the layer "removes the synchronization cost" should say "removes the
   synchronization" instead.
5. **The price of the layer is memory and mount time, not latency.** Per-operation overhead is
   microseconds. The index costs about 0.5 to 0.6 KiB of unpageable kernel memory per file and a
   scan of every file at each mount: under a second warm, but 19 s on an SSD and two minutes on
   a cloud disk for 60 000 files after a reboot. Index persistence stays conditional; these are
   the numbers its decision was waiting for, and at a few hundred thousand files they argue for
   it.
6. **Reflink and dedupe are not a reason to adopt the layer for small memories.** They work
   and are worth keeping delegated; the project should not claim space savings for this kind of
   load.
7. **"A real client runs unchanged" is supported by one client with three conditions.** The
   second client (LangGraph store) and stage 3 are where this claim is tested next.
8. **The alternative without a kernel module has not been measured.** A per-domain index kept
   in user space, protected by the same directory permissions, is being built as a baseline
   (journal 0092). Until it is compared, this report cannot say that the kernel module is the
   right place for the index, only what it costs and gives.
   Note of 2026-10-13, later the same day: it was built (journal 0092) and measured once
   (journal 0094). A generic index in user space was 2 to 150 times slower on five query shapes
   and fell behind under a bulk load; an index that knows the application's schema is close to
   the layer. See those journals before quoting either number.
9. **Everything here is pilot-grade.** The plan's method (repetitions with confidence intervals,
   three data-set profiles, 1 to 100 concurrent processes, a budget-saturation series, CPU and
   memory per system) was only partly carried out. Two ways forward, for the maintainer: accept
   this report as the M4 result with that label, or run the missing series first. The second
   costs about a week of the dedicated host and would mostly tighten numbers whose ordering is
   already clear; the saturation series and the concurrency runs are the two that could still
   change a conclusion.

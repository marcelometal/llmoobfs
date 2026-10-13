# M4 benchmark harness — specification

Status: being implemented (2026-10-10). Complements [benchmark-plan.md](benchmark-plan.md),
which defines questions, baselines, workloads, scales, metrics and method. This document
defines the programs, formats and tests that make the plan executable and reproducible by
third parties.

Implemented in `bench/` (entry 0025): `gen` (deterministic manifest), `load` (W1, W2, W3, W4
with per-call latency recording), `report.py` (percentiles, dispersion, CSV and JSON) and
`smoke.sh` (`make bench-smoke`, harness self-check in a common directory, in CI),
`sqlbase.sh` (schema and composite indexes of §4, load from the manifest, SQL of the Q1–Q9
forms, `EXPLAIN QUERY PLAN`) and `equiv.sh` (equal inode sets across `QUERY`, SQLite and the
oracle, without truncation; exercised in `vm/suite.sh` over the layer). `sqlbase` and `equiv`
are shell over the `sqlite3` CLI so they run in the VM's initramfs; latency measurement of
the SQLite path does not exist yet and will be in C. W6 and W7 are workloads of `load`
(remount with and without discarding the page cache up to the first `QUERY`), instead of a
separate `mountclock`. W5 is in `load`; W9 is the suite's replay block; `run.sh` orchestrates
the campaign; `sqlclient.c` measures W3 on the baselines through the SQLite library. Every
component of this specification exists; no numbers have been produced (accelerated hardware
is missing).

Plan-gate finding (2026-10-10, 300 inodes, profile 16): SQLite's planner chooses `SCAN` for
Q1 (`kind = note`, 41% of rows), Q5 (`pinned = 1 AND score < 500`) and Q9 (`weight IS NOT
NULL`, 100% of rows), even with the §4.1 indexes and `ANALYZE`, because selectivity is low
at that scale. The gate now records `SCAN` as an exception to justify in each measurement,
not as a failure; set equality remains mandatory. Q9 over an attribute present in every row
will always be a scan in any system; the form must be re-evaluated or applied to a minority
attribute before M4 measures. Resolved the same day: `weight` now exists in 30% of memories
(above), so Q9 asks about a minority. Recorded deviation: the minimum profile is now called
6, not 3, because §3 fixes six attributes in every profile; valid profiles are 6, 8 and 16.
No performance numbers have been produced.

## 1. Why a harness of our own

Survey of existing tools (2026-10-10):

- `mdtest` (IOR) and `fs_mark` measure metadata operations per second over file trees; they
  report throughput, not per-call latency percentiles, and exercise neither xattrs nor
  queries.
- `filebench` has a workload language, but has been dormant for years and does not cover
  xattrs.
- `fio` covers the data path; it serves to calibrate the device, not metadata.
- `mdbench` (Tigris) and MDWorkbench report per-operation latency, but without xattrs and
  without the concept of a structured query.

Conclusion: the harness is our own code, in C with no dependencies, reusing what the
repository already has (`tests/query.c`, `tools/moobfs-index`, `tools/moobfs-attr`,
`replay-log`). The tools above enter only as calibration (`fio`) and as output-format
reference.

## 2. Components

| Program | Role | Language |
| --- | --- | --- |
| `bench/gen` | Generates the deterministic dataset from a seed: list of memories with path, size, pseudorandom content and typed attributes per the profile (3, 8 or 16 attributes) and the chosen distribution | C |
| `bench/load` | Runs W1 to W5 and W8 against a target (mounted directory) with N processes, recording each call's latency in a per-process binary file | C |
| `bench/mountclock` | Runs W6 and W7: unmounts, discards caches (`echo 3 > drop_caches`) when asked, mounts, and measures until the first `QUERY` or the first SQL query answers | sh + C |
| `bench/sqlbase` | Keeps the attribute index of baselines A and B in SQLite, with the same schema and the same predicates; validates plans with `EXPLAIN QUERY PLAN` | C (vendored sqlite3 amalgamation or system lib) |
| `bench/equiv` | Checks that, for each query, kernel (`QUERY`), SQLite and the oracle (`moobfs-index`) return the same inode set; fails if they diverge or if any result came truncated | sh |
| `bench/report` | Reads the raw files, computes p50/p95/p99, throughput, median and dispersion across repetitions, and emits CSV and JSON; plots are generated outside (gnuplot or python), never a harness requirement | Python with no dependencies beyond the standard library |
| `bench/run.sh` | Orchestrates: prepares targets A, B, C; warms up; measures; repeats; alternates order; collects `environment.txt`, `/proc/diskstats`, `perf stat`, `GET_INFO`; preserves everything in `build/bench/<date>/` | sh |

## 3. Dataset

`bench/gen` takes `--seed`, `--inodes`, `--profile {3,8,16}`, `--depth`, `--size-dist` and
writes a text manifest with one line per memory: relative path, size in bytes, content seed,
and the attributes as `name=type:value`. Rules:

- Two-level paths (`<ns>/<agent>/m<n>.md`) with 8 namespaces and 32 agents by default, so
  that W3 has varied selectivity.
- Attributes fixed in every profile: `namespace` (string, 8 values), `agent` (string, 32),
  `memory_id` (textual UUID v7 string, unique), `kind` (string, 6 values, Zipf), `score`
  (uniform int64 0..1000), `created` (increasing time with jitter). Profiles 8 and 16 add
  `pinned` (bool, 10% true), `weight` (normal double, present in 30% of memories, so that
  the existence query Q9 is selective; decision of 2026-10-10), `tag1..tagN` (string, 50
  values, Zipf) and `raw` (256-byte raw, profile 16 only, never indexed).
- Log-normal content sizes with 2 KiB median and tail up to 64 KiB; pseudorandom content
  from the line's seed, with 15% of memories byte-for-byte duplicates of another, for
  W-sharing.
- The manifest is the only source: `load` reads it and reproduces it on any target; the
  same manifest feeds `sqlbase`.
- Cardinality, average size and distribution of each attribute are printed in the manifest
  header and copied into the report.

## 4. Baselines A and B: SQLite schema and equivalence

Table `memories(ino INTEGER PRIMARY KEY, path TEXT UNIQUE, namespace TEXT, agent TEXT,
memory_id TEXT UNIQUE, kind TEXT, score INTEGER, created INTEGER, pinned INTEGER, weight
REAL, tag1 TEXT, …)`, one column per indexable attribute, types affine to the MoobFS types.
Composite indexes covering the W3 conjunctions, with the equality column before the range
column: `(kind, score)`, `(namespace, agent)`, `(namespace, kind, created)`, `(pinned,
score)`, plus one single-column index per attribute; `ANALYZE` after the load. Mandatory
gate: the `EXPLAIN QUERY PLAN` of each W3 query must show `SEARCH` using an index; a `SCAN`
invalidates the run and demands a new index, because a baseline without an adequate index
is not equivalent. The database file lives on the same filesystem as the files (ext4 on A,
btrfs on B), in WAL, with `synchronous=NORMAL`, and the writer updates index and file in
the same sequence a client would (file first, then the row), so that W1 pays the real cost
of maintaining the index.

Equivalence: `bench/equiv` runs the W3 queries on the three systems and compares the inode
sets after mapping paths; any difference aborts the measurement. The oracle is the arbiter
when kernel and SQLite diverge.

### 4.1 W3 query forms and indexes

W3 uses no arbitrary combinations: it has fixed forms, each covered by a composite index
with equality before range. For the 8-attribute profile:

| Form | Predicate | Index covering it |
| --- | --- | --- |
| Q1 | `kind =` | `(kind, score)` by prefix |
| Q2 | `kind = AND score >` | `(kind, score)` |
| Q3 | `namespace = AND agent =` | `(namespace, agent)` |
| Q4 | `namespace = AND kind = AND created >=` | `(namespace, kind, created)` |
| Q5 | `pinned = AND score <` | `(pinned, score)` |
| Q6 | `kind prefix` | `(kind, score)` as the binary range `[p, p+1)` with `COLLATE BINARY` |
| Q7 | `score >= AND score <=` | `(score)` |
| Q8 | `tag1 = AND kind = AND pinned = AND score >` | `(tag1, kind, pinned, score)` |
| Q9 | `weight exists` | `(weight)` with `IS NOT NULL` |

Single-column indexes whose role is covered by the prefix of a composite one (`kind`,
`namespace`, `pinned`) are not created. Plain `agent`, `created` and `weight` remain for
single-term queries over them. Combinations outside this list do not enter W3; if needed,
they get their own index and pass through the same gate. Semantics: prefix by binary range
(upper bound by incrementing the last byte, with `0xFF` handling); existence as `IS NOT
NULL`, because an absent attribute is `NULL` in the baseline; the generator avoids `-0.0`
and NaN, because MoobFS orders `-0.0 < 0.0` and SQLite equates them.

Gate before each measurement: load the dataset, `ANALYZE`, record the SQLite version,
generate the concrete queries, `EXPLAIN QUERY PLAN` on each, record `SEARCH`/`SCAN`, the
index used and the predicates exploited; validate equivalence against the oracle; measure
only afterwards. A legitimately cheaper `SCAN` for a distribution is documented as an
exception with justification; `INDEXED BY` is not used to force plans.
`sqlite3_stmt_scanstatus_v2()` only if the build has `SQLITE_ENABLE_STMT_SCANSTATUS`.
The database stays on the same filesystem as the files, in WAL with `synchronous=NORMAL`,
and write amplification includes files, metadata, database pages, indexes, WAL and
checkpoints; the logical growth of each part is recorded separately. The authorization cost
of `QUERY` is measured apart from the index cost, comparing the same query as root and as a
restricted user, with `perf` over the authorization function.

## 5. Latency collection

`load` records per call: workload, operation, start and duration in nanoseconds
(`clock_gettime(CLOCK_MONOTONIC)`), process, and result (bytes or number of results). Fixed
32-byte binary record format, one file per process, converted by `report`. No aggregation
during measurement, so as not to distort the distribution. Calls that fail enter as errors
and count in the error rate, never in latencies.

## 6. System metrics

Per repetition: `perf stat -e task-clock,context-switches` of the process set;
`/proc/meminfo` and process RSS before and after; `/proc/diskstats` of the lower device
(sectors written) for write amplification, with `sync` before reading; `GET_INFO`
(`index_used`, `index_entries`, `index_inodes`, flags) in C; `df` of the lower layer before
and after W-sharing; `environment.txt` with kernel, CPU, memory, versions.

## 7. Statistics and reporting

Median and p5–p95 interval across repetitions for each metric; p50/p95/p99 per operation
within each repetition, reported as the median across repetitions; relative dispersion
above 20% marks "inconclusive". `report` emits `results.csv` (one row per system, workload,
operation, metric) and `results.json` with the metadata. No number is written into a project
document without the path of the `results.csv` that originated it.

### 7.1 Where a campaign's artefacts live

Added on 2026-10-11 (entry 0041). The campaign's text files (report, `results.csv`,
`results.json`, environment, equivalences, hash manifest) are versioned in
`bench/results/<date>-<host>-<campaign>/`, with a `README.md` describing the run. The raw
package (`*.rec` records, SQLite databases, manifests, `perf.data`) does not enter Git: it
goes as an attachment of the single `bench-evidence` release, never in a new pre-release.
Steps:

1. Package the raws as `<date>-<host>-<campaign>-raw.tgz` and compute size and SHA-256.
2. Add a row to `bench/evidence-release.md`'s table ("Original tag" column with `—`) and
   cite in the campaign's `README.md` the link
   `https://github.com/studiomoob/llmoobfs/releases/download/bench-evidence/<file>` and the
   SHA-256.
3. Run `scripts/bench-evidence-release.sh add <file>` (simulation) and then with `--apply`:
   the script checks the file against the table, uploads, verifies size and digest via the
   API and the public download, and updates the release text from `bench/evidence-release.md`.

If the repository moves to immutable releases, attachments can no longer be added to a
published release; in that case this rule must be revisited.

## 8. Execution

Target environment: Linux VM with KVM, 4 vCPUs, 4 GiB, three dedicated virtio disks (one per
baseline), Debian 6.12 kernel or the KASAN workflow kernel without instrumentation. The
current Debian harness (TCG) runs only the S1 scale as a self-check of the harness, not for
numbers. `make bench-smoke` does that in CI; `make bench` requires KVM and produces numbers.

## 9. Tests of the harness itself

- `gen`: same seed produces identical manifest (hash); profiles have the right number of
  attributes; `memory_id` unique; distributions within tolerance at S1.
- `load`: on a tmpfs directory, W1 creates exactly the manifest and `moobfs-check` validates;
  the latency file has one record per call; calls with injected errors (`dm-flakey`) appear
  in the error rate and not in latencies.
- `sqlbase`: schema created; `EXPLAIN QUERY PLAN` of all W3 queries returns `SEARCH`;
  equivalence with the oracle at S1 on the three systems.
- `report`: percentiles checked against a known set; dispersion computed; "inconclusive"
  when tolerance is exceeded.
- `run.sh`: full run at S1 with 2 repetitions inside the TCG VM, verifying that all
  artefacts exist and that `equiv` passed.

## 10. Deliverables

Code in `bench/`, target `make bench-smoke` in CI, `docs/testing.md` with the benchmark
section, and a first report in `docs/history/` with S1 to S3 on the three profiles over
ext4 and btrfs, including the cases where the layer loses.

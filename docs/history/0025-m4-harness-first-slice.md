# 0025 — M4: first slice of the benchmark harness

Date: 2026-10-10. Branch: `m4/bench-harness`. Status: in progress (harness; no numbers).

## Goal

Start M4 with the harness in correctness mode, according to `docs/benchmark-harness.md`, before
any measurement: deterministic generator, loader with per-call latency, and report.

## Changes

- `bench/common.h`: splitmix64 PRNG, manifest format, 32-byte latency record.
- `bench/gen.c`: deterministic manifest (seed, inodes, profile 6/8/16, namespaces, agents);
  paths `ns<i>/a<j>/m<n>.md`, unique textual `memory_id`, Zipf `kind`, uniform `score`,
  increasing `created` with jitter, log-normal sizes with a 2 KiB median, 15% byte-for-byte
  duplicates (same seed and same size as the origin), profiles 8 and 16 with `pinned`, `weight`,
  `tag1..tag7` and `raw`.
- `bench/load.c` (Linux): W1 (tmp, typed attributes, fsync, rename, directory fsync), W2
  (read by path), W3 (`MOOB_IOC_QUERY` per line of a queries file, `--root`), W4 (update of
  `score`, set and removal of `scratch`); N processes by index modulo of the line; one `.rec`
  file per process; errors recorded as `-errno`, never as latency.
- `bench/report.py`: p50/p95/p99 per system, load and operation; throughput; median and p5–p95
  across repetitions; "inconclusive" above 20% dispersion; `results.csv` and `results.json`.
- `bench/smoke.sh` and `bench-tools`/`bench-smoke` targets in the `Makefile`; step in CI.

## Decisions

1. Profiles are 6, 8 and 16 attributes (the specification said 3 for the smallest, but fixes six
   attributes in every profile; 6 is the consistent reading). Noted in `benchmark-harness.md`.
2. Duplicates inherit the size and content seed of the origin, so that W-share measures real
   dedup.
3. `load` computes nothing during the measurement; it only writes records.

## Evidence

Run in a Linux container (`gcc:13`, Docker Desktop) with the target on the container's
filesystem: `BENCH_SMOKE_PASS` with deterministic `gen` (same seed, identical bytes; different
seed, different bytes; 300 unique `memory_id`; 16/8/6 attributes per profile), W1 with 3
processes creating 300 files with no leftover `.tmp` and `moobfs-check` clean with 4800
attributes, W2 and W4, 1200 records, errors reported (`n=0 err=300` against a missing tree),
report with two repetitions. The times printed by the smoke are not project measurements:
emulated environment, plain directory, no layer. W3 was exercised next in the VM suite (below).

`bench` block in `vm/suite.sh` (`make vm-test`, Debian kernel 6.12.111,
`build/vm-test-bench.log`): over the layer mounted on ext4, btrfs and XFS, W1 with 2 processes
created 300 memories of profile 16 (`moobfs-check` on the lower: 4800 attributes, no problems),
W2 read all of them, each of the nine W3 shapes (`bench/queries-w3.txt`, Q1–Q9) returned through
`QUERY` exactly the set of the `moobfs-index` oracle, W3 and W4 wrote one record per call (1209
records in total); `BENCH_LAYER_PASS` on all three, `MOOBFS_LAYER_PASS` and
`MOOBFS_ACCEPTANCE_PASS` unchanged. The harness binaries are compiled static and staged in
`vm/in-container.sh` and in the KASAN workflow. A first run failed at `attr-set-ext4` because of
dynamic binaries reused from a `build/linux` left by a different container; `rm -rf build/linux`
solved it (noted in `docs/development.md` as a caveat when switching containers).

## SQLite slice: `sqlbase` and `equiv` (2026-10-10)

`bench/sqlbase.sh` creates the schema and the composite indexes of §4 of the harness, loads the
manifest with each file's real inode (`stat`), generates the SQL of each shape Q1–Q9 (prefix as
a binary range, existence as `IS NOT NULL`, `raw` without a column) and exposes `EXPLAIN QUERY
PLAN`. `bench/equiv.sh` compares, per shape, the inode sets of `QUERY`, SQLite and the oracle
and fails on any difference or truncation. Both in shell over the `sqlite3` CLI, so they run in
the initramfs (no Python); decision recorded in the harness.

Evidence: `make bench-smoke` in a Linux container (`BENCH_SMOKE_PASS`, SQLite equal to the
oracle in the 9 shapes) and `make vm-test` (`build/vm-test-bench.log`): `BENCH_LAYER_PASS` on
ext4, btrfs and XFS with `EQUIV_PASS: 9 shapes (kernel, SQLite, oracle)` inside the block;
`MOOBFS_LAYER_PASS`.

Finding: with 300 rows SQLite plans `SCAN` for Q1, Q5 and Q9 due to low selectivity, despite
the indexes and `ANALYZE`. Decision: the gate records `SCAN` as an exception to be justified in
the measurement instead of failing, and Q9 (existence of an attribute present in 100% of rows)
is flagged for reassessment in the specification before measuring. Closes issues 10 and 11 on
merge.

## W6 and W7: remount with cold and warm index (2026-10-10)

`bench/load --workload W6|W7 --target MOUNT --lower DIR --queries FILE --repeat N` (root):
unmounts, for W6 does `sync` and writes 3 to `/proc/sys/vm/drop_caches`, mounts `moobfs_layer`
and issues the first query of the file; the record covers `mount()` (which rebuilds the index)
and that first `QUERY`. In the VM suite, two repetitions of each over the 300-memory dataset,
with the layer remounted and `GET_INFO` without flags at the end. `make vm-test`:
`BENCH_LAYER_PASS` on ext4, btrfs and XFS; the three-way equivalence recorded one `SCAN` plan to
be justified per lower with seed 11 (against three with seed 7 in the container: the planner's
choice depends on the generated distribution). Times are not measurements: TCG emulation.

## W8: concurrent mixed load (2026-10-10)

`bench/load --workload W8 --procs N [--queries FILE --root MOUNT]`: each process walks its
slice of the manifest and, with a PRNG seeded by the process number, chooses a read (70%), an
attribute update (20%) or a `QUERY` drawn from the queries file (10%, only with `--queries` and
`--root`). VM suite: 10 processes over the layer on the three lowers, all calls recorded, no
failure; `make bench-smoke` covers W8 without a mount root. `BENCH_LAYER_PASS` on ext4, btrfs
and XFS; `MOOBFS_LAYER_PASS`.

## W5, W9 and the `run.sh` orchestrator (2026-10-10)

- W5 in `load`: processes 1..N-1 rename each memory of their slice (there and back) and then
  remove and recreate it with the same bytes and attributes, while process 0 walks the queries
  file until the mutators finish; records `RENAME`, `RECREATE` and `QUERY`. In the suite:
  4 processes over the layer; at the end, 300 files, no `.away`, `moobfs-check` clean and the
  nine W3 shapes equal to the oracle, on the three lowers.
- W9 (durability and recovery) gets no code in `load`: it is the `dm-log-writes` and
  `dm-flakey` replay block the suite already runs (`POWER_LOSS_REPLAY_PASS`,
  `IO_ERROR_INJECTION_PASS`); the M4 report cites that block as W9.
- `bench/run.sh`: orchestrates a campaign (dataset, `environment.txt`, per repetition and
  system: W1, `diskstats` before and after, `GET_INFO`, `moobfs-check`, SQLite baseline,
  equivalence, W2, W3, W4, W8, W5, W6 and W7 when root; alternating order across repetitions;
  report at the end if Python is available). Systems: `--plain NAME=DIR` (A and B) and
  `--layer NAME=MOUNT:LOWER` (C). Run in smoke mode in the suite (60 memories, one repetition,
  C only) with the artifacts checked. A first defect of the script (remounting the layer with
  `lower=<root>/bench` instead of the lower root, which broke the following fsx test) was fixed
  before the commit.
- The latency measurement of the SQLite path (W3 on baselines A and B) is still missing;
  `run.sh` skips W3 on those systems and the report must say so until the C client exists.

## SQLite client in C (2026-10-10)

`bench/sqlclient.c`: runs, through the `libsqlite3` library (not the shell), one prepared
statement per line of an SQL file generated by `sqlbase.sh sql` for the same shapes Q1–Q9, and
writes one record per statement in the same format as `QUERY` (load 3, operation `QUERY`,
result = rows). `run.sh` uses the client on the `--plain` systems (A and B) and `QUERY` on the
`--layer` systems (C), with the same `queries.sql`/`queries.txt`. The VM image and the KASAN
workflow got `libsqlite3-dev`; the binaries are static (`-lsqlite3 -ldl -lpthread`).
Evidence: `make bench-smoke` in a container (18 client records in two repetitions,
`sqlite W3 query: n=18 err=0` in the report) and `make vm-test` with the rebuilt image:
`BENCH_LAYER_PASS` on ext4, btrfs and XFS, `MOOBFS_LAYER_PASS`. With this the harness covers
everything `docs/benchmark-harness.md` asks for; what is missing is accelerated hardware to
measure on.

## Q9 reassessed and accelerated guest guide (2026-10-10)

- Q9 (`weight exists`): `bench/gen` now emits `weight` in 30% of the memories (profiles 8 and
  16), making the existence query selective; attribute count checks in the smoke and in the
  suite now derive the total from the manifest. With 300 rows the SQLite planner still chooses
  `SCAN` for Q9 (85 of 300 rows), recorded as an exception.
- Defect of the harness itself found and fixed: the smoke loaded the SQLite baseline after W4
  had added 1 to every `score` in the files, so a file at the boundary of Q7 entered the oracle
  and not SQLite. The baseline is now loaded right after W1 in all runs (`run.sh` already did
  this). The set equality caught the error: that is its job.
- `docs/bench-accelerated-guest.md`: guide for measuring with hardware acceleration, on both
  paths (Linux x86 host with KVM for the report; Apple Silicon Mac with Hypervisor.framework
  and Debian arm64 as a preliminary campaign, outside the tested matrix), with suite validation
  before measuring, disk calibration with `fio`, invocation of `run.sh` and what to record.
  Proposal not executed.

## Limitations

The Docker Desktop bind mount on macOS does not support user xattrs; the smoke must use a target
on the container's filesystem or run on Linux (CI uses ext4). `load` does not cover W5 and W9;
the report does not run in the VM (no Python in the initramfs);
`mountclock` became the W6/W7 workloads of `load`.

## Next step

Reassess Q9; guide for an accelerated Linux guest (x86 with KVM, or arm64 with
Hypervisor.framework on the Mac as a preliminary campaign); then numbers and the M4 report.

## Benchmark host preparation (2026-10-11)

Maintainer decision: the first campaign runs on an x86_64 cloud VM (Azure, D4as_v5 or similar,
Ubuntu 24.04, Standard security type) used as the host, with the module in the instance's own
kernel, instead of a nested VM. Added `vm/bench.config` (fragment without sanitizers or lockdep
over the distribution config), `bench/kernel-build.sh` (same 6.12.112 tarball with the same
SHA-256 as the KASAN workflow, `bindeb-pkg`, installation) and `bench/host-setup.sh` (formats
three dedicated disks, compiles module and tools, runs the whole suite on the host as a gate,
`HOST_SETUP_PASS`). Path C documented in `docs/bench-accelerated-guest.md`. None of the three
has been run yet: the first run will be on the instance, and the result goes here or in a
journal of its own.

First run of `bench/kernel-build.sh` on the Azure VM (Standard_D4as_v4, AMD EPYC 7763,
Ubuntu 24.04 with kernel 6.17.0-1022-azure, 2026-10-11): compilation and installation completed
from the Azure kernel config. Three adjustments came out of it: the version suffix came out
duplicated (`-moobfs-bench-moobfs-bench`, because both the fragment and `make` defined
`LOCALVERSION`), `dpkg -i` included the `-dbg` package, which fails without debug information,
and GRUB keeps the newest kernel as the default, so 6.12 needs `grub-reboot` (a single boot, with
automatic return to 6.17 if it fails). The suffix and the package were fixed; `grub-reboot` is
in the guide. The instance uses Trusted launch with Secure Boot on, which must be turned off for
the unsigned kernel. The kernel already installed on the instance keeps the duplicated suffix;
it is not recompiled.

## Gate on the Azure host: `HOST_SETUP_PASS` (2026-10-11)

VM Standard_D4as_v4 (4 vCPU AMD EPYC 7763, 16 GB), Ubuntu 24.04, kernel
`6.12.112-moobfs-bench-moobfs-bench` compiled by `bench/kernel-build.sh`, Secure Boot off,
three Premium SSD P6 data disks of 64 GB with cache "None", addressed by
`/dev/disk/azure/scsi1/lun0..2` (the `sdX` names changed between boots and are not used).
`sh bench/host-setup.sh --ext4 .../lun0 --btrfs .../lun1 --xfs .../lun2` finished with
`HOST_SETUP_PASS`: `ISOLATION_PASS`, `BENCH_LAYER_PASS` with `EQUIV_PASS` on the three lowers,
`LAYER_LOWER_PASS`, `SQLITE_VARIANTS` with no losses, `POWER_LOSS_REPLAY_PASS`,
`IO_ERROR_INJECTION_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`, with no diagnostic in
`dmesg`. It is the second environment in which the whole suite passes and the first with
hardware acceleration. The concurrency test did 12936 queries in 5 s, against about 620 in the
emulated VM; it is a count from a correctness test, not a measurement, and only illustrates why
times under TCG are useless. The kernel was left as the GRUB default (`grub-set-default`).

Initial calibration on the Azure host (P6 with bursting, 2026-10-11), 4 KiB write followed by
`fsync`, 30 s: ext4 313/s, btrfs 229/s, xfs 418/s (from 2.4 to 4.4 ms per operation). The
command used printed the write completion latency (about 10 µs), which with `--fsync=1` does not
include the `fsync`; `bench/calibrate.sh` now extracts the `fsync` latency from the `fio` JSON.
Decision: the campaign that feeds the report uses Premium SSD v2 disks with fixed IOPS and
throughput, because the credit-based bursting of P1–P20 would change the device speed over the
hours; the current P6 disks are fine for the gate and the pilot.

## Pilot on the Azure host and harness fixes (2026-10-11)

Pilot (`run.sh --scale 1000 --profile 8 --reps 3 --procs 4`, six systems: A-ext4,
B-btrfs, X-xfs without the layer and C-ext4, C-btrfs, C-xfs with the layer, each pair on the
same P6 disk with bursting). It ran in full, without errors, with equivalence in all shapes. It
serves to validate the procedure, not to draw conclusions. Signals to confirm in the campaign:
W1 dominated by the disk `fsync` and practically equal with and without the layer; W2 and W4
with an addition of a few microseconds per operation in the layer; W3 with `QUERY` about eight
times slower than SQLite with a composite index at the median. The artifacts are on the host
(`build/bench/pilot-s1/`) and have not yet been copied to the repository; the numbers above are
not an M4 result.

The pilot exposed three method defects in W3, fixed before the campaign: each shape ran once per
repetition (now `--repeat`, default 50 in `run.sh`); the records did not separate the shapes
(the operation code now carries the shape number, Q1–Q9, and the report shows one row per shape
with the median of results returned); and the comparison was not equivalent (SQLite now returns
`ino` and `path`, like `QUERY`, and `QUERY` is also measured as an ordinary user,
`--uid 65534`, recorded as system `<name>+u`, to separate the cost of per-result authorization,
as the plan asks). Shape Q8 is no longer removed in the profiles without `tag1`, so that the
numbering stays stable; in those profiles it matches nothing on any system. Validated with
`make bench-smoke` in a container and `make vm-test` (`BENCH_LAYER_PASS` on all three).

## Pilot 2 with the fixed harness (2026-10-11)

Remote run through the dedicated key `moobfs-bench-claude` (revocable in the host's
`authorized_keys`). Calibration with `bench/calibrate.sh`: `fsync` p50/p99 of 2.8/7.8 ms on
ext4, 3.6/12.3 ms on btrfs and 2.1/6.8 ms on XFS (P6 with bursting). Pilot: 1000 memories,
profile 8, 5 repetitions, W3 with 50 runs per shape, six systems plus the three `+u`. Evidence
in `bench/results/2026-10-11-azure-pilot2-s1/`; raw package in the pre-release
`bench-2026-10-11-azure-pilot2-s1`. The 30 equivalences passed.

Medians on the ext4 pair (without layer / with layer): W1 9.90 / 9.82 ms; W2 9.3 / 12.9 µs;
W4 set 10.0 / 24.2 µs; W4 removal 16.0 / 43.6 µs; W6 and W7 (layer only) 35 and 17 ms for
1000 memories.

W3 per shape (median, ext4; the pattern is the same on btrfs and XFS):

| Shape | Results | SQLite | `QUERY` root | `QUERY` ordinary user |
| --- | --- | --- | --- | --- |
| Q8 (no match) | 0 | 19 µs | 54 µs | 54 µs |
| Q3 `namespace = AND agent =` | 3 | 16 µs | 141 µs | 142 µs |
| Q4 | 23 | 26 µs | 178 µs | 180 µs |
| Q5 | 70 | 43 µs | 248 µs | 254 µs |
| Q2 | 100 | 56 µs | 323 µs | 328 µs |
| Q6 `kind prefix` | 212 | 99 µs | 502 µs | 517 µs |
| Q9 `weight exists` | 291 | 78 µs | 608 µs | 630 µs |
| Q1 `kind =` | 390 | 91 µs | 843 µs | 874 µs |
| Q7 `score` between 250 and 750 | 513 | 225 µs | 1136 µs | 1177 µs |

Reading of the code (`layer/index.c`, `do_query`) that explains the difference:

1. Fixed cost of about 50 µs per call, visible in Q8: `kvcalloc(q->limit, ...)` allocates and
   zeroes a results vector the size of the requested limit (4096 × 280 bytes, about 1.1 MB)
   on every call, even when no result comes back.
2. Linear scan of the first term: without a resumption key, `first_after` starts at `rb_first`
   and walks all the entries of the attribute, even for equality, range or prefix, where the
   tree would allow jumping to the first candidate value and stopping after the last. In Q3 this
   costs about 85 µs for 1000 entries.
3. About 2 µs per returned result, against about 0.4 µs in SQLite: for each result the layer
   copies the path, resolves it with `vfs_path_lookup` under the caller's credentials and checks
   the permission (phase 3b). As an ordinary user the cost rises only 3%: the weight is in the
   path resolution, not in the permission decision.

Decision: 1 and 2 are implementation inefficiencies and will be fixed before the campaign, with
the same equivalence, isolation and KASAN tests; 3 is a consequence of the per-result
authorization design and stays as a declared cost, to be measured again after the fixes. The
baseline SQLite prepares the statement on every call, which makes it a little slower than an
application that reuses prepared statements; the comparison, therefore, does not favor SQLite.
Pilots 1 and 2 stay as a record of the state before the fixes.

Note of 2026-10-11: the first attempt of pilot 4 (with the RCU revalidation of journal 0028)
aborted in the equivalence of system C-ext4: `QUERY` returned 291 more results in shape Q9 than
the oracle. The cause was a set of 1000 memories left in `/srv/moob/layer-ext4/prof` by the
profiling of journal 0028; `QUERY` answers over the whole mount, while the oracle and SQLite
look only at `bench/`. No number was produced. The folder was removed and the pilot redone;
`run.sh` now refuses to start if the root of a layer contains anything besides `bench/`.

The guard failed in the CI of PR 20: the suite mounts the layer over the root of a freshly
formatted ext4, which carries `lost+found`. The guard now ignores `lost+found`, which never has
typed attributes. `make vm-test` with the fix: `BENCH_LAYER_PASS` on the three lowers.

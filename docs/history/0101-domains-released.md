# 0101 — ADR-0012, second half: built domains are released

Date: 2026-10-14. Status: implemented; run in the local VM and under KASAN on the benchmark host
(results below).

## Goal

Items 3 and 4 of ADR-0012. After journal 0098 a domain, once built, stayed in memory until
unmount, and the budget was a point after which builds failed. Now a built domain can go back
to "not built": to make room for another under the budget, and when the kernel asks for memory.
The idle floor decided on 2026-10-13 (60 s by default, a mount option) protects the domains in
use from the shrinker.

## Change

`layer/index.c`, `layer/moobl.h`, `layer/main.c`, `include/moobfs_ioctl.h`:

- `domain_takedown()`: the sequence the stale-domain rebuild of journal 0098 already used
  (drop the domain's entries, state back to "not built", `domains_built` down, `drop_gen` up),
  now one function with its locks asserted: the domain's build mutex, `query_sem` for writing,
  the index lock for writing.
- Each domain records `used_at` (jiffies) when a query uses it or a build completes.
- `domain_lru()` finds the built domain queried least recently, skipping one domain and those
  used in the last `idle` jiffies. A plain walk of the domains of the mount, not a `list_lru`:
  the domains of one mount are few, and the index memory is not charged to a memory cgroup, so
  per-cgroup, per-node lists would buy nothing. The walk needs no lock (domains are never
  removed from the xarray before unmount); `domain_release()` checks the state again under the
  locks.
- `domain_release()`: from the shrinker every lock is only tried (`mutex_trylock`,
  `down_write_trylock` twice), because reclaim can run inside a query, a build or an update of
  the same mount. From a build over the budget, the caller holds its own domain's build mutex;
  the other domain's is only tried, so two builds cannot wait for each other, and the rest is
  taken in the order `domain_ensure()` uses.
- **Budget.** `charge()` records that it refused (`over_budget`). When a build fails with
  `ENOMEM` and the flag is set, `domain_ensure()` releases the least recently queried other
  domain and builds again, up to 1 024 times, until the domain fits or nothing is left to
  release. The mount-wide `QUERY` passes `may_release = false`: it needs every domain at once,
  and releasing one to build the next would only go round.
- **Shrinker.** One per mount, `shrinker_alloc()` and `shrinker_register()` at mount (named
  `moobfs-index:<major>:<minor>` for the shrinker debugfs interface), `shrinker_free()` at
  unmount before the index is torn down; `shrinker_free()` waits for a scan in progress. Count:
  an estimate in pages, `index_used` shared evenly among the built domains times the number of
  those idle for longer than the floor; the index does not count memory per domain. Scan:
  `SHRINK_STOP` without `__GFP_FS`; otherwise release domains, least recently queried first,
  until about `nr_to_scan` pages are freed.
- **Mount option** `index_idle=<seconds>`, 0 to 86 400, default 60; shown in `/proc/mounts`
  when not the default.
- **`GET_INFO`**: `domain_builds` becomes `__u32` and `domain_releases` (`__u32`) takes the
  other half of its eight bytes; size and offsets unchanged. Both fields were added in journal
  0098 and never published, so no compatibility decision is needed. `tests/query.c info`
  prints the new counter.

## Test

New suite step `release` (`vm/suite.sh`, `MOOB_ONLY=release`), on ext4, btrfs and XFS. Four
domains of 500 files each, written with `bench-gen` and `bench-load W1` (profile 16):

1. **Budget.** One domain is built on a fresh mount to measure what it takes; the layer is
   mounted again with `index_budget` at 2.5 times that, so two fit and three do not. Querying
   the four domains must give the oracle's answers, release at least two, leave at most two
   built and stay under the budget. Then the order: after `d0` and `d1` are the last two
   queried, querying them again must build nothing. The mount-wide `QUERY` must fail, and the
   index must not be marked incomplete.
2. **Shrinker.** `index_idle=0`; after the four domains are built, `echo 2 >
   /proc/sys/vm/drop_caches` (which calls every shrinker) must leave none built and count four
   releases; the answers after the rebuild must equal the oracle.
3. **Floor.** Default `index_idle`; the same `drop_caches` must release nothing.
4. **Race.** `index_idle=0`, eight domains of 60 files, `tests/buildrace.c` five times while a
   loop writes `drop_caches` without pause; at least one release must have happened, and every
   domain must then equal the oracle.

## Results

Local VM (`make vm-test`, Debian 6.12.111 in QEMU):

- `MOOB_ONLY="release buildrace"` five times: 15 of 15 `RELEASE_PASS` and 15 of 15
  `BUILD_RACE_PASS` over ext4, btrfs and XFS. One domain took 540 842 bytes, the budget was
  1 352 105; releases during the race block: 40 to 42 per filesystem and repetition.
- Negative runs, one protection removed at a time, each failing at the first run:
  shrinker scan returning `SHRINK_STOP` always: `rl-shrinker-released-nothing-ext4`;
  no release over the budget: `rl-budget-answer-differs-ext4`; idle floor at zero:
  `rl-idle-floor-not-kept-ext4`; most recently queried chosen instead of least:
  `rl-budget-recent-domain-was-released-ext4`.
- The idle-floor negative first passed: the floor was applied in two places (the count and the
  release) and only one was removed. Both now read one function, `reclaim_idle()`, and with it
  at zero the step fails.
- `sh scripts/checkpatch.sh`: exit 0; shellcheck at the CI's severity: clean.
- Complete `make vm-test`: `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`, with
  `BUILD_RACE_PASS` and `RELEASE_PASS` on the three filesystems.

Benchmark host, `sh vm/kasan-local.sh` (upstream 6.12.112 from `vm/kasan.config`), complete
suite at commit 81a2da1: `MOOBFS_KASAN_PASS: layer suite under KASAN, UBSAN and lockdep with no
kernel diagnostics`; within it `RELEASE_PASS` and `BUILD_RACE_PASS` on the three filesystems and
`FAULT_INJECTION_PASS: operations=18 calls=300 faults=282 unexpected=0`.

## Errors of the test found on the way

- 300 files per domain took 325 KB, under the 1 MiB minimum of `index_budget` at 2.5 times;
  the step said so and stopped. 500 files: 541 KB.
- The race block first passed on btrfs and XFS with zero releases: one `buildrace` lasts well
  under a second. With the requirement of at least one release it then failed on btrfs, and
  the cause was the test: the stop file of the ext4 round still existed when the loop of the
  btrfs round started, and was removed only after the loop had seen it and quit. The file is
  now removed before the loop starts, and `buildrace` runs five times with pauses.

## Limitations

- What the shrinker reports as freeable is an estimate (even share per domain).
- A release waits for running queries and is only tried by the shrinker; under a steady query
  load the shrinker can free nothing, and a build over the budget may fail with `ENOMEM`
  because another build holds the domain it would release.
- An attribute write that does not fit the budget in a built domain still marks the whole index
  incomplete (finding F2 of ADR-0011 unchanged).
- A cursor resuming after its domain was released and built again is argued from the code,
  not tested.
- Thrashing is visible (`domain_builds`, `domain_releases`) but not limited beyond the floor.
- The shrinker was exercised only through `drop_caches`, not under real memory pressure.

## Next step

Tracepoints for builds and releases (security roadmap S13), replacing the test-only
`scan_retries` parameter; then measurements on the benchmark host.

# 0096 — Systematic allocation-failure injection; the domain walk no longer allocates

Date: 2026-10-13. Status: implemented; run on the benchmark host under KASAN, UBSAN and lockdep
(results below). Local series (after pull request 64). First step of the order set with
ADR-0012.

## Goal

Every allocation the module makes can fail. Until now those paths were covered only by reading
the code. This journal makes the kernel fail each of them in turn and checks three things: the
system call fails cleanly or succeeds, the kernel log stays silent, and afterwards the index
either equals the oracle or says that it is incomplete and equals the oracle after a remount.

## Method

The kernel's own mechanism, not a new one: `Documentation/fault-injection/fault-injection.rst`,
"Systematic faults using fail-nth". A thread writes N to `/proc/self/task/<tid>/fail-nth` and
its N-th allocation fails; reading the file back tells whether the fault was injected.

- `tests/failnth.c` (new): 17 operations (set a new attribute, replace one, raw over typed,
  remove, create, mkdir, rename over an indexed file, unlink an indexed file, `QUERY2`, ordered
  `QUERY2`, `QUERY2_BATCH`, `GET_INFO`, chmod of a directory, open and read, lookup, fiemap,
  syncfs). For each, N goes 1, 2, 3, ... on a fresh file until a call runs with no fault
  injected. A failed call must return `ENOMEM`, `ENOSPC`, `EIO`, `ENOBUFS`, `EAGAIN` or `EINTR`;
  anything else is reported and the tool exits 1.
- `vm/kasan.config`: `CONFIG_FAULT_INJECTION`, `FAILSLAB`, `FAIL_PAGE_ALLOC`,
  `FAULT_INJECTION_DEBUG_FS`, `FAULT_INJECTION_STACKTRACE_FILTER`, `DEBUG_FS`, `STACKTRACE`.
- `vm/kasan-guest.sh`: a block after the suite. It confines the faults to allocations with the
  module's text on the stack (`require-start`, `require-end` of `failslab` and
  `fail_page_alloc`), so the lower filesystem and the VFS are not what is being failed; runs
  `failnth` on an ext4 lower with `domain_depth=1`; checks the kernel log; compares `QUERY2`
  with `moobfs-index` on both domains unless `GET_INFO` reports `MOOB_FLAG_INDEX_INCOMPLETE`;
  remounts and compares again. Prints `FAULT_INJECTION_PASS`.
- `vm/kasan-local.sh` and `.github/workflows/kasan.yml` build `failnth`; `vm/suite.sh` accepts
  `faults` in `MOOB_ONLY` (the step exists only under the KASAN harness).

## Finding and fix

First valid run: 244 faults in 261 calls, no kernel diagnostic, three unexplained errors.
`QUERY2`, ordered `QUERY2` and `QUERY2_BATCH` returned `ENOTTY` when their first allocation
failed.

Cause: `depth_and_domain()` and `place_dentry()` in `layer/index.c` allocated an array of 64
dentry pointers to walk from a dentry to the root, and treated a failed allocation the same as
a path deeper than the limit. Consequences, by caller:

- `QUERY2` and `QUERY2_BATCH`: `ENOTTY` ("not a domain directory") instead of `ENOMEM`. A
  client that probes for the layer by this error would conclude it is on a plain filesystem.
- `place_dentry()` (attribute write, rename, permission change): the file was marked as not
  homogeneous, which keeps it out of every domain answer, and nothing set
  `MOOB_FLAG_INDEX_INCOMPLETE`. Read from the code; this run did not isolate it (see
  limitations).
- `moobl_rename_crosses_domain()`: a directory rename was refused with `EXDEV`.
- `moobl_index_perm_changed()`: the fallback to per-result checks for the domain was skipped.

Fix: the walk no longer allocates. `depth_of()` counts the levels with `dget_parent()`,
`ancestor_get()` returns the ancestor a given number of levels up, and `place_dentry()` checks
the directories in between while walking, holding at most three references at a time. The
depth limit (64) and its meaning are unchanged. Alternatives discarded: returning `ENOMEM` from
every caller (four call sites, two of them `void`, each needing its own degraded mode), and an
on-stack array (512 bytes in a stacked filesystem's call chain).

## Results

Benchmark host, kernel 6.12.112 built from `vm/kasan.config`, virtme-ng.

`MOOB_ONLY=faults sh vm/kasan-local.sh`, after the fix:

```
FAILNTH setxattr-new               faults=4 failed_cleanly=1 succeeded_anyway=3
FAILNTH setxattr-replace           faults=2 failed_cleanly=1 succeeded_anyway=1
FAILNTH setxattr-raw-over-typed    faults=1 failed_cleanly=1 succeeded_anyway=0
FAILNTH removexattr                faults=1 failed_cleanly=1 succeeded_anyway=0
FAILNTH create                     faults=7 failed_cleanly=6 succeeded_anyway=1
FAILNTH mkdir                      faults=12 failed_cleanly=10 succeeded_anyway=2
FAILNTH rename-over-indexed        faults=14 failed_cleanly=0 succeeded_anyway=14
FAILNTH unlink-indexed             faults=1 failed_cleanly=1 succeeded_anyway=0
FAILNTH query2                     faults=48 failed_cleanly=47 succeeded_anyway=1
FAILNTH query2-ordered             faults=47 failed_cleanly=45 succeeded_anyway=2
FAILNTH query2-batch               faults=93 failed_cleanly=90 succeeded_anyway=3
FAILNTH get-info                   faults=0 failed_cleanly=0 succeeded_anyway=0
FAILNTH chmod-directory            faults=1 failed_cleanly=0 succeeded_anyway=1
FAILNTH open-read                  faults=1 failed_cleanly=1 succeeded_anyway=0
FAILNTH lookup-cold                faults=0 failed_cleanly=0 succeeded_anyway=0
FAILNTH fiemap                     faults=0 failed_cleanly=0 succeeded_anyway=0
FAILNTH syncfs                     faults=1 failed_cleanly=0 succeeded_anyway=1
FAILNTH_DONE: operations=17 calls=250 faults=233 unexpected=0
FAULTS: the index declared itself incomplete (flags=0x38), as designed when an update fails after the lower write
FAULT_INJECTION_PASS: operations=17 calls=250 faults=233 unexpected=0; no kernel diagnostic; index equal to the oracle
MOOBFS_KASAN_ONLY_PASS: steps faults under KASAN, UBSAN and lockdep with no kernel diagnostics (not a complete run)
```

Complete run (`sh vm/kasan-local.sh`, the whole suite and then the block above) with the fix:
```
MOOBFS_ACCEPTANCE_PASS: git, sqlite wal, memory client, dm-log-writes replay, dm-flakey
MOOBFS_LAYER_PASS: pass-through over ext4, btrfs and xfs; layer tests; typed attributes and tools; index and QUERY equal to the oracle; fsx 9000 ops each; reflink on btrfs/xfs; nolinks; unload
FAILNTH_DONE: operations=17 calls=259 faults=242 unexpected=0
FAULTS: the index declared itself incomplete (flags=0x38), as designed when an update fails after the lower write
FAULT_INJECTION_PASS: operations=17 calls=259 faults=242 unexpected=0; no kernel diagnostic; index equal to the oracle
MOOBFS_KASAN_PASS: layer suite under KASAN, UBSAN and lockdep with no kernel diagnostics
```

The number of faults differs between runs (233, 242, and 244 before the fix). The cause was
not investigated.

`sh scripts/checkpatch.sh`: exit 0.

Errors of the harness found on the way, fixed before the first valid run: the end address of
the module (`0xffffffffc0...`) does not fit the signed arithmetic of `dash`, so it is now
computed on the low 32 bits.

## References brought up to date

`docs/references.md` was checked against what journals 0088 to 0096 used and completed: the
kernel's fault-injection documentation moved out of the proposal section into its own, now that
it is implemented; the kernel facilities ADR-0012 rests on (shrinker, `list_lru`, the btrfs
extent-map shrinker, sequence counters) listed with the files read; fstests as a suite with its
version; SQLite and inotify as used by the user-space baseline and `bench/synccost.c`; the
LangGraph versions the adapter was tested with.

## Limitations

- The index declared itself incomplete during the run, so the comparison with the oracle
  before the remount did not happen; only the one after the remount did. The run therefore
  shows that a failed update is declared, not that every failed update is.
- The silent case of `place_dentry()` was found by reading the code after the `ENOTTY`
  pointed at the shared cause. No run reproduced it before the fix.
- `lookup-cold` is not cold: the dentry is cached by the preparation, and no fault was
  injected there, nor in `GET_INFO` and fiemap, which do not allocate in the module.
- Only allocations are failed. Not covered: errors of the lower filesystem returned to the
  layer in the middle of an operation (the suite's `ioerror` step covers device errors only),
  failures at mount time and during the mount scan, and `domain_depth` other than 1.
- One lower filesystem (ext4), one run. Not run on `make vm-test` locally; the CI runs it.

## Next step

ADR-0012: build each domain's partition on demand and release it under memory pressure. The
build path allocates heavily; this harness is the first test it has to pass, extended with a
mount-time and a first-query case.

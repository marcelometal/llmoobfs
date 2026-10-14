# 0105 — Query results are copied to the caller after the query semaphore is released

Date: 2026-10-14. Status: **in progress**; implemented; confirmation runs below.

## Goal

A code review of `layer/` (requested by the maintainer as a checkpoint after ADR-0012, S13 and
S12) found that every query copied its results to user space while holding `ix->query_sem` for
reading. The maintainer asked for the defect to be confirmed, against documentation and other
kernel code, before it is fixed.

## The defect

`query2_pin()` takes `query_sem` for reading and returns with it held; `query2_exec()`,
`query_run()` and `put_result()` then copy each result with `copy_to_user()`, and the semaphore
is released only after the last copy. v1 `QUERY` does the same, and also reads the request
(`memdup_user()`) after taking the semaphore. `QUERY2_BATCH` held it across all its requests.

`query_sem` is taken for writing, blocking, by the rebuild of a stale domain
(`domain_ensure()`) and by a release over the budget (`domain_release()` from a build), both
reachable by any user through `QUERY2`. A copy to user space may wait on a page fault for as long
as whoever backs that memory decides. While it waits, the writer waits for it, and readers that
arrive after a waiting writer wait behind it: the queries of every user of the mount stop. That
breaks promise 1 of ADR-0011 (no hang, denial of service included).

Not a lock-order deadlock: a page fault does not take `query_sem` back (the index lock that
eviction takes is a different lock, and it is not held during the copy).

## Confirmation

- **Code:** the call chain above, with the line numbers of the commit reviewed (`e3e45de`):
  `down_read` at 3007, `copy_to_user` at 2601, `up_read` at 3149.
- **Documentation:** `Documentation/admin-guide/sysctl/vm.rst`, `unprivileged_userfaultfd`:
  handling faults raised in kernel mode is denied to unprivileged users by default because
  doing so "may make certain vulnerabilities more difficult to exploit". Holding the kernel
  inside a copy to user space is the technique that setting guards against; other ways to
  delay a fault on memory a user controls remain.
- **Other kernel code:** btrfs's search ioctl (`fs/btrfs/ioctl.c`, `search_ioctl()` and
  `copy_to_sk()`, Linux 6.12.112) faults the user buffer in before taking its locks, copies
  with `copy_to_user_nofault()` under them, and retries when that fails (commit
  `a48b73eca4ce`, "btrfs: fix potential deadlock in the search ioctl", 2020; refined for
  sub-page faults with `fault_in_subpage_writeable()`). Its trigger was a lock-order report
  between `mmap_lock` and the tree lock; the rule is the same: no fault-prone copy under a lock
  that other tasks need.
- **Lockdep:** `results_to_user()` asserts `lockdep_assert_not_held(&ix->query_sem)` before the
  only copy of results. The confirmation run puts the same assertion at the old copy site of
  commit `e3e45de`; it must fire there, and stay silent with the fix. A test that holds a page
  fault open on purpose was not written.

## Fix

- `query_run()` fills a kernel array of `limit` results (at most 4096 x 272 bytes) instead of
  copying each one; `results_to_user()` copies them in one call after `query_sem` is released.
- `QUERY2`: `query2_one()` pins the domain, runs, unpins, then copies. `QUERY2_BATCH` does that
  per request, so the semaphore is no longer held across the batch; the batch was already
  documented as not a snapshot.
- v1 `QUERY` reads and validates the request before building anything and before taking the
  semaphore. Before, with `domain_depth=0`, an invalid request from any user built the whole
  index first (review item 2).

## The rest of the review

Read: `layer/main.c`, `layer/index.c`, `layer/moobl.h`, `include/moobfs_ioctl.h`. Automatic
checks, all clean: a build with `W=1`, `sparse` (`make C=1`), `checkpatch`, a search for TODO,
FIXME and commented-out code; no build artefact is tracked. Not read in this pass: `tools/`,
`tests/`, `bench/`, the scripts of `vm/`, `site/`, and `kernel/` (the native filesystem of phase
1, still tested by the suite).

Items, with what was done:

- **Done in this journal:** a v1 `QUERY` with an invalid request built the whole index first
  (above). Stale texts: the header of `main.c` and `MODULE_DESCRIPTION` (shown by `modinfo`)
  still said "phase 2a pass-through"; a section comment said typed validation was "phase 2c";
  the UAPI header said every ioctl goes to the mount root. `moobl_other_device()` was declared
  under `index.c` and is defined in `main.c`. `<linux/magic.h>` was included and unused.
- **Need a measurement or a test before any change:** the index write lock is taken for every
  layer inode that enters or leaves the inode cache, indexed or not (contention not measured;
  `index_load()` relies on that lock to link a record and its inode); a build over the budget
  starts its scan again after each release (only when a build does not fit); the per-result
  fallback after a permission change lasts until remount, although a rebuild recomputes the
  partition (a permission change does not cause a rebuild, so the gain is small); a remount with
  new options appears to be accepted and ignored, since the filesystem has no `reconfigure`
  operation (read in the kernel source, not tried). Note of 2026-10-14: checked in journal 0106;
  for `mount(2)` that is what overlayfs does on purpose, and only the new mount API differed.
- **Recorded, not changed:** `struct moob_query_term` has one byte of implicit tail padding
  (1097 bytes declared, 1098 in size; nothing leaks, the kernel writes back only what the caller
  sent), and the ioctl letter `'M'` is shared with other drivers in
  `Documentation/userspace-api/ioctl/ioctl-number.rst`; both are interface changes, to be
  decided before a stable interface. The table of domains never shrinks before unmount (about
  100 bytes per domain ever queried, outside the budget). `inode_matches_rest()` and
  `inode_matches_all()` differ only in their first term; a declaration after statements in
  `scan_push_dir()`; an orphaned comment above `paths_fresh()`. The test hook `scan_delay_us`
  stays, as decided in journal 0098.

## Results

- **Lockdep, old code** (commit `e3e45de` with `lockdep_assert_not_held(&ix->query_sem)` added at
  the old copy site, `MOOB_ONLY=ext4 sh vm/kasan-local.sh` on the benchmark host): the assertion
  fired at every copy of a result in the `QUERY2` tests of the step, from several processes
  (`WARNING: ... at layer/index.c:2748 query_run`), and the step failed with
  `kernel-diagnostic-layer`. The kernel confirms that every result was copied with the
  semaphore held.
- `make vm-test`, complete, with the fix: pass (`MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`).
- Build with `W=1` and `sparse`: clean. `sh scripts/checkpatch.sh`: exit 0.
- KASAN with the fix (the assertion now sits in `results_to_user()`): see below when recorded.

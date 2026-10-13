# 0082 — Fix: allocations with the index lock held could deadlock against memory reclaim

Date: 2026-10-13. Status: implemented; `make vm-test` passes; under lockdep the run without the
fix reports the cycle and the run with it reports nothing. The deadlock itself was not
reproduced: the defect was found by reading and proved through lockdep. Local series (after pull
request 59).

## How it was found

A separate inspection session asked why the query path had the module's only two `GFP_ATOMIC`
allocations, with no spinlock anywhere. The answer led to the defect.

## What was wrong

`moobl_evict_inode` calls `moobl_index_inode_evicted`, which takes the index lock for writing.
Memory reclaim evicts inodes. So an allocation that may enter filesystem reclaim, made with the
index lock held, can end up waiting for the lock its own task holds.

The two `GFP_ATOMIC` allocations (candidates of a query, under the read lock) avoided reclaim,
at the price of failing easily. But the write side allocated with `GFP_KERNEL` under the write
lock: a new name node, a new inode record, the xarray slot and the entry of every
`setxattr("user.moob.*")`, and the recorded path on a rename.

Reach: an ordinary user setting a typed attribute while the machine is short of memory. The task
would hang holding the write lock, and then every query and every attribute change on that mount
would hang with it. This is older than the series: it is in `v0.2.0-experimental`.

It never showed in the KASAN job because lockdep only learns "reclaim takes this lock" when an
eviction actually happens inside reclaim during the run, which the suite never caused.

## Fix

- Every section that holds the index lock runs in a `memalloc_nofs_save()` scope
  (`ix_write_lock`, `ix_read_lock` and their unlock counterparts in `layer/index.c`). Allocations
  inside it cannot recurse into filesystem reclaim, so they stay `GFP_KERNEL`, and the two
  `GFP_ATOMIC` ones become `GFP_KERNEL` too: they may now sleep and reclaim page cache.
- The mount-time scan no longer holds the lock across the whole walk of the lower tree. It takes
  it per attribute, around the insertion. The first version of this fix kept the old shape and
  lockdep then reported a second cycle (reclaim → index lock → the lower directory's inode
  lock, taken by the scan → reclaim, through the lower's own allocations under that inode
  lock). Nothing else can use the index during the scan, so the wide lock bought nothing.
- `moobl_index_init` tells lockdep that reclaim takes the lock (`fs_reclaim_acquire`,
  `might_lock`, `fs_reclaim_release`). From now on an allocation outside a scope is reported by
  the KASAN job on the first mount, whether or not memory is short.
- `lockdep_assert_held` / `lockdep_assert_held_write` in the nine internal functions that
  assume the lock.

Also here, from the same inspection reports: `MODULE_VERSION("0.2.0+dev")` in the layer, and
`MODULE_AUTHOR` in the frozen M1 module.

Alternative: `GFP_NOFS` at each allocation. Rejected: the scope covers allocations made by
helpers (the xarray, `kstrdup`) and by code added later, and the kernel documentation prefers the
scope API.

## Evidence

- `make checkpatch`: clean. `make vm-test`: every marker through `MOOBFS_LAYER_PASS`.
- **Negative run** (local KASAN job, helpers reduced to plain lock and unlock, the priming kept):
  `WARNING: possible circular locking dependency detected` at the first mount, `fs_reclaim`
  against `&ix->lock` in `moobl_index_build`; `MOOBFS_VM_FAIL: kernel-diagnostic-layer`.
- **First version of the fix** (scope added, scan still under the lock): the second cycle
  described above; also `MOOBFS_VM_FAIL: kernel-diagnostic-layer`.
- **Final version**: `MOOBFS_KASAN_PASS`, no kernel diagnostic, upstream 6.12.112 with KASAN,
  UBSAN and lockdep.

## Limitations

- The deadlock was not produced; the proof is lockdep's, which reasons about lock classes.
- One related path is not covered by the scope: `fill_result` drops a dentry reference with the
  read lock held, and if that were the last reference to an unlinked inode the eviction would
  take the write lock in the same task. The inspection of the same day found the window (a rename
  over an indexed file); it is closed in the next journal.
- Accounting of index memory to the caller's cgroup (`__GFP_ACCOUNT`) was raised too and is left
  for S9.

## Next step

The findings of the read-only inspection of `layer/index.c`: rename over an indexed file, the
truncated-path flag, a typed attribute replaced by a raw one, the unchecked fallback store, and
nested btrfs subvolumes.

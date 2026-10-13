# 0083 — Fixes from a read-only inspection of the index: rename over a file, cut paths, raw over typed, nested subvolumes

Date: 2026-10-13. Status: implemented; `make vm-test` passes; each testable fix has a negative
run; the KASAN job for this batch is recorded at the end. Local series (after pull request 59).

## Source

The maintainer ran a separate session that only read `layer/index.c` and `layer/main.c` and
reported findings with a verdict each ("confirmed by reading", "suspicion", "style"). None had
been run. Each was checked here against the code, and one by experiment, before being changed.
All of them are older than this week's series.

## Findings and fixes

**B. A rename over an indexed file left the replaced file in the index.** Only `unlink` and
`rmdir` dropped a record; `moobl_rename` moved the source and ignored the file it replaced.
The dead record stayed until unmount: `QUERY2` without per-result checks could return its inode
number under a name that now belongs to another file, and the index budget grew with every
"write a temporary file, rename over the old one", which is the write sequence the client
contract recommends. Fix: after a successful rename that is not an exchange, the replaced
directory, or the replaced file when no other name links to it, is dropped from the index.

**A. A dentry reference was dropped with the index lock held.** `fill_result` took a reference
to an alias of the inode and dropped it under the read lock. If that were the last reference to
an unlinked file, the eviction would take the write lock in the same task. The window found was
the one B left open. Fix, independent of B: the reference travels with the candidate and is
dropped after the lock is released.

**C. `MOOB_RES_PATH_TRUNCATED` never reached the caller.** The name was cut to 255 bytes while
still relative to the mount root, the flag was set on a temporary and lost, and the cut name
then failed the per-result permission check, so the result disappeared. Fix: the candidate keeps
the whole name, which the permission check resolves; only the copy sent to the caller is cut,
relative to the scope, with the flag set.

**D. A typed attribute overwritten by a raw one kept its index entry.** Raw values are stored
and never indexed, and the early return skipped the removal of the entry the old value had.
Queries kept matching a value the file no longer has. Fix: setting a raw value removes the
entry of that name.

**E. An unchecked `xa_store` when a directory's permissions change.** That store marks the domain
for per-result checks. If the allocation failed, the domain kept answering without them: the
failure opened instead of closing. Fix: on failure a flag turns per-result checks on for every
domain of the mount. Not testable without fault injection; not tested.

**F. Two files with one inode number shared an index record.** Confirmed by experiment on the
benchmark host (module of journal 0069, lower btrfs with a subvolume inside the tree): after
setting `kind=alpha` on a file outside the subvolume and `kind=gamma` on a file inside it with
the same inode number, the query for `alpha` returned nothing; removing the inner file left the
outer one out of the index until remount. The files on disk were not touched.

Decision (maintainer): **refuse**. A directory that is the root of a btrfs subvolume, found
below the lower root, is not entered: `lookup` returns `EXDEV`, and the mount scan leaves it out
with one informational line. Detection: btrfs, a directory, inode number 256
(`BTRFS_FIRST_FREE_OBJECTID`, the number of every subvolume root and of nothing else).

Alternative: key the index and the layer's inodes by subvolume and inode number. Rejected for
now: query results carry only an inode number (a UAPI change), and telling subvolumes apart
needs either btrfs internals or a `getattr` per lookup. A store of agent memory has no need to
query inside snapshots; it remains possible later.

Consequence: a snapshot kept inside the covered directory is not reachable through the layer.
It is reachable on the lower. `MOOB_CAP_SNAPSHOTS` keeps its meaning (the lower can snapshot).

**Also:** a `BUILD_BUG_ON` ties `MOOB_ATTR_STRING_MAX` to `MOOB_QUERY_VALUE_MAX` (a cursor
carries the value it stopped at; both are 1024 today).

## Reported and not changed

- After a hard link is removed and the count returns to one, the file's partition is not
  recomputed, so it stays out of `QUERY2` answers until another change: a false negative, noted.
- The query loops walk non-matching entries without `cond_resched`, holding the read lock.
- The mount scan held the write lock across the walk: already changed in journal 0082.
- Points 2, 3 and 5 of the report (raw pointers, lock order, `copy_from_user`): no defect found
  by the inspection; nothing to change.

## Tests

`vm/suite.sh`, on the three lower filesystems unless noted:

- rename over an indexed file: the index's inode count drops by one, the replaced value is not
  found, the moved file is found under the new name, kernel equal to the oracle;
- a typed value replaced by raw is no longer found;
- a file 370 bytes deep is returned as one result marked `(truncated)` (`tests/query.c` prints
  the flag);
- btrfs only: a subvolume created in the lower tree (`layer subvol`, new mode of
  `tests/layer.c`) with an attributed file in it; the layer mounts, the directory cannot be
  entered, the file is not in the index.

## Evidence

- `make checkpatch`: clean. `make vm-test`: every marker through `MOOBFS_LAYER_PASS`, with
  `NESTED_SUBVOLUME_REFUSED`.
- Negative runs, each with one fix removed:
  - rename: `inodes 8 -> 8`, `MOOBFS_VM_FAIL: insp-rename-over-count-ext4`. The first version of
    this test only queried for the replaced value and passed without the fix on ext4, because
    the v1 `QUERY` checks each result and hides the dead record; the count check was added for
    that reason.
  - raw over typed: `MOOBFS_VM_FAIL: insp-raw-over-typed-ext4`.
  - truncation flag: `MOOBFS_VM_FAIL: insp-deep-truncated-ext4`.
  - subvolume refusal: `MOOBFS_VM_FAIL: subvol-entered`.
- Local KASAN, once for the batch (upstream 6.12.112 with KASAN, UBSAN and lockdep, with the
  lockdep priming of journal 0082): `MOOBFS_KASAN_PASS`, no kernel diagnostic. The kernel line
  "a btrfs subvolume inside the lower tree is left out" shows the subvolume test ran there; the
  console capture of that harness did not keep the `NESTED_SUBVOLUME_REFUSED` line, nor the
  per-filesystem markers of ext4 and btrfs, which is a property of the capture not looked into.

## Limitations

- A and E have no test of their own: A is a restructuring that the lockdep assertions and the
  race tests cover indirectly; E needs fault injection.
- The refusal recognizes btrfs only. Another lower filesystem that reuses inode numbers inside
  one superblock would have the same defect; ext4 and XFS do not.
- A subvolume moved into the tree behind a mounted layer is adversary A2 of ADR-0011.

## Next step

`MOOB_ONLY` in the suite and KASAN as a required check (S6); then the M4 report.

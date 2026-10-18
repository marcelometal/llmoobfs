# 0120 — The review's medium and low items: collation, write-only files, long walks, directory reads, ref-walk revalidation, listings in batches

Date: 2026-10-18. Started 01:10, ended 02:30 (calendar time, local). Status: written and committed;
not compiled against the kernel nor run (the benchmark host was off). Estimated effort: M.

## Goal

Fix the six items of the independent source review of 5582e06 (journal 0114) that its
verification confirmed with a medium or low severity and that the batch of journals 0115 to 0118
left out. Item numbers are the review's.

## Changes

- **Item 9, SQL conditions under a collation** (`sqlite/moobfs_vtab.c`). The kernel compares bytes,
  which is SQLite's BINARY collation; under another one (`kind = 'Note' COLLATE NOCASE`) the kernel
  left out rows SQLite counts as equal, and journal 0112's re-check on each row could not bring
  them back. A condition whose `sqlite3_vtab_collation()` is not BINARY is no longer handed to the
  kernel. ORDER BY needs no such test: SQLite offers a virtual table an ORDER BY term with a
  COLLATE only when it matches the column's collation (`allocateIndexInfo()` in `where.c`, read in
  the 3.50.4 amalgamation). Test: three statements with COLLATE NOCASE in the suite step `sqlext`,
  compared with the ordinary table; they return rows only if the condition stays SQLite's.
- **Item 7, typed attributes on write-only files** (`layer/main.c`, `moobl_typed_check()`). The
  check of whether the attribute already exists used `vfs_getxattr()`, which asks read permission
  for `user.*`, so the owner of a `0200` file could not set a typed attribute the lower accepts. It
  now uses `__vfs_getxattr()`: the VFS already authorized the write on the layer's inode. Test: in
  every lower filesystem's run, uid 1001 sets `user.moob.kind` on its own `0200` file
  (`query --uid 1001 ... xset`, new in `tests/query.c`) and the lower carries it.
- **Item 6, long walks under the write lock** (`layer/index.c`). `domain_drop()` walks every name
  of the mount and `moobl_index_destroy()` every inode with the index lock held for writing; both
  now call `cond_resched()` between names and inodes. The lock is a semaphore, so this is allowed.
  The verification found it reaches seconds only with a budget of several GiB, chosen by the
  administrator; no test, the cost is a scheduling point.
- **Item 8, directory reads** (`layer/main.c`, `moobl_iterate()`). ADR-0011 says data operations on
  an open file, directory reads included, run with the mounter's credentials; directory reads ran
  with the caller's, so an LSM judged the caller against the lower directory's label in
  `iterate_dir()`. They now run under `override_creds()`, as `ovl_iterate()` does in overlayfs. No
  test: the difference shows only under an LSM policy, which the harnesses do not load.
- **Item 3, ref-walk revalidation** (`layer/main.c`, `moobl_d_revalidate()`). Without the parent's
  lock, `moobl_instantiate()` can swap a negative dentry's lower dentry and drop the old one while
  ref-walk reads it. The read is now under `rcu_read_lock()` (the old dentry is freed after a grace
  period), and the lower dentry is held with `lockref_get_not_dead()` only to call the lower's own
  `d_revalidate` (ext4 has one for casefolded and encrypted names); a dead one means it was
  replaced, and the dentry is looked up again. No reproducer: the window needs a preemptible kernel
  and a full grace period between two loads, as the verification found.
- **Item 5, listings held whole during a build** (`layer/index.c`, `scan_run()`). A build read a
  whole directory into a list before handling any name, outside the index budget and the caller's
  accounting, so a user could make it hold a listing as large as a directory they filled. A
  directory is now read in batches of 1024 names (`MOOBL_SCAN_BATCH`), with the directory open and
  the position kept by the file, as `getdents(2)` reads it: the actor stops before taking the name
  that would overflow the batch, and the next `iterate_dir()` starts with it. Test: suite step
  `bigdir`, a domain whose directory holds 3000 entries with long names, planted on the lower and
  built by the first query, compared with the oracle for two queries.

## Results

Not run. Checked only that the user-space files parse (`tests/query.c` and
`sqlite/moobfs_vtab.c` with stub Linux headers on macOS: no error but the macOS signatures of
`setxattr`/`getxattr`). The module was not compiled.

## Limitations

- Everything above waits for the benchmark host: the module build, `scripts/checkpatch.sh`, the
  full suite under KASAN with the steps `sqlext` and `bigdir`, and a negative round each for items
  9, 7 and 5 (the change undone, the test failing).
- `bigdir` checks the answer, not the memory: that the build held at most one batch is read from
  the code, not measured.
- Readdir between batches has the usual `getdents(2)` semantics against concurrent changes; the
  build's existing protections (writes during a build mark it dirty, renamed directories are
  rescanned) are what covers changes meanwhile, as before; `buildrace` exercises them on small
  directories only, not across batches.

## Next step

The KASAN run of the stabilization batch (journals 0115 to 0118) and of this one on the benchmark
host, then one pull request per batch, this one after the first.

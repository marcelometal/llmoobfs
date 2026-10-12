# 0057 — Bug: renaming a file over a just-deleted name failed with EINVAL

Date: 2026-10-12. Status: fixed on branch `fix/rename-over-deleted-name`; suite passes; KASAN by CI
on the pull request.

## How it was found

Stage 1a of the real-client test (`docs/client-test-plan.md`) ran ai-memory's own test suites with
`TMPDIR` on plain ext4 and inside the layer. On ext4, 1 147 tests passed in 6 suites. On the layer,
`wiki::tests::backfill_then_reindex_rebuilds_from_wiki_alone` failed: ai-memory deletes `_meta.md`,
then rewrites it the usual atomic way (a temporary file in the same directory, renamed over the
name), and the rename returned `EINVAL` (Rust `PersistError`, errno 22).

## Reproduction

On the Mac mini host, with the module of `main`, using only shell and Python on the layer:
`echo one > a; ls a; rm a; echo two > b; mv b a` failed (`mv` prints the misleading "cannot move
'b' to a subdirectory of itself"), and `os.rename("y", "x")` after creating, stating and unlinking
`x` raised `[Errno 22] Invalid argument`. Recreating a deleted name with `open(O_CREAT)` worked.

## Cause

After `unlink`, the VFS keeps the upper dentry as a negative dentry, and its `d_fsdata` still
references the lower dentry of the deleted file, which the lower filesystem has unhashed. Create,
mkdir, link and symlink re-look up the lower child under the parent lock (`moobl_lock_parent`) and
repoint the upper dentry (`moobl_instantiate`). `moobl_rename` used the cached lower dentry of the
target instead, and its `d_unhashed(lower_new)` check returned `EINVAL`.

## Fix

`moobl_rename` now looks up the target again with `lookup_one_len` after `lock_rename`, as create
does (and as eCryptfs does in its rename). If the upper and lower targets disagree on existence, or
a positive target maps to another lower inode, the lower tree was changed behind the layer and the
rename returns `ESTALE` instead of acting on another file. `tests/layer.c` gains the regression: a
file is created, stated, unlinked, and a new file is renamed over its name; the same for a
directory removed with `rmdir`.

## Evidence

`make vm-test` with the fix: `LAYER_PASSTHROUGH_TESTS_PASS` (with the new case) on ext4, btrfs and
XFS, and every other marker up to `MOOBFS_LAYER_PASS`. checkpatch clean.

## Why the suite missed it

No test deleted a name and then renamed another file onto it. `fsx`, the race tests and the SQLite
and git acceptance tests rename onto existing targets or create new names; SQLite recreates its
`-wal` and `-shm` files with `open`, which already worked (journal 0015 fixed that path).

## Next step

Rerun ai-memory stage 1a and stage 1b on the host with the fixed module.

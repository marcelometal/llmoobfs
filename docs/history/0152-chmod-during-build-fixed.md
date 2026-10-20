# 0152 — A chmod during the first build of a domain no longer leaves the file in its answers

Date: 2026-10-20. Status: done locally; not yet in a pull request.

## Goal

Fix the finding of journal 0151 (review of `d110d4e`, finding F1 of that review): a chmod to
0600 that lands while the first build of a domain is reading a file leaves the file in the
domain's homogeneous partition, so `QUERY2` keeps returning its path and matching its
attributes to a caller whose `getxattr` fails with `EACCES`.

## Cause

`index_load()` registers a loader, reads the file's attributes and, when an update touched
the file during the reading (`ld.dirty`), reads them again. The placement it stores
(`struct moobl_place`, domain and homogeneity) was computed by the caller before the loader
was registered and was never computed again. `moobl_index_perm_changed()` marks the loader
dirty, but finds no record yet to reclassify (`rec_set_partition()`), so nothing corrected the
old homogeneity. The window is wider than the repeated reading: a chmod that completed after
the caller's check and before the registration was not seen at all.

## Fix

The file's own homogeneity check (`target_ok()`: link count, mode and ACL against the domain
directory) moves into `index_load()`. It runs after the loader is registered and again before
every repeated reading. A permission change that completed before the registration is
visible to the check: the hook took the index lock after changing the lower inode, and the
registration takes the same lock. A change that runs after the registration marks the loader
dirty, and the reading and the check are done again. The callers now pass only the part that
does not depend on the file itself, whether the directories from the domain down to it pass
(`inner_ok`), plus the domain directory (`dom`). A `NULL` `dom` keeps the old behaviour.

- Scanned files (`scan_file()`) and scanned directories (`scan_push_dir()`, `scan_run()`,
  through `self_dom()`) pass the domain directory of the scan.
- Directories renamed during a build (`dir_moved_prepare()`) keep only the result of the
  directories above them in `md->self`; their own check is made when the scan reaches them.
- `index_adopt()` (a renamed or newly linked file) keeps the placement its caller computed: it
  runs from the rename and link hooks, while the VFS holds the inode lock of that file, which
  a chmod also needs. That claim was checked against the code, not by a test.

Alternatives discarded: a global counter of permission changes, as `move_gen` does for
renames, with the file stored as heterogeneous when the counter moved. It is simpler, but a
heterogeneous file is absent from v2 answers (`docs/spec-query-v2.md` §3), so any chmod
anywhere during a build would have hidden unrelated files until their next change.

## Test

`tests/review-build-permissions.c` becomes `tests/chmodrace.c`. It exits 1 when the file is
still answered, and 3 when the revoked state was not reached. It is built by
`vm/build-tests.sh` and runs in the `buildrace` step of `vm/suite.sh` on ext4, btrfs and XFS.
`scan_delay_us=1000000` makes the scan sleep while it reads the file; the chmod lands at
0.3 s. The step requires at least one `moobfs_scan_reread` event, so a run where the chmod
missed the window fails instead of passing vacuously. `tests/review-d110d4e-vm.sh` (journal
0151) now compiles `tests/chmodrace.c`; its exit codes keep their meaning.

## Evidence

- `MOOB_ONLY=buildrace make vm-test` with the fix: `REVIEW_RESULT uid=1001 first=0 after=0
  getxattr=-1 errno=13` and `BUILD_RACE_PASS` on ext4, btrfs and XFS
  (`readings_repeated` 47, 24 and 58), `MOOBFS_ONLY_PASS: steps buildrace`.
- Negative round, branch `neg/chmodrace-test-only` (the same tree with `layer/index.c` of
  `d110d4e`): `REVIEW_RESULT uid=1001 first=1 after=1 getxattr=-1 errno=13` and
  `MOOBFS_VM_FAIL: br-chmod-during-build-still-answered-ext4`.
- `make checkpatch`: clean.
- Full `make vm-test` with the fix (Docker/QEMU, Debian 6.12.111): exit 0, ending
  `MOOBFS_LAYER_PASS` and `MOOBFS_ACCEPTANCE_PASS`. `BUILD_RACE_PASS` with `chmod during a
  build: absent` on all three filesystems; `PERMMATRIX_STEP_PASS`, `RELEASE_PASS`,
  `LOWERRO_PASS`, `BIGDIR_PASS` and the other steps unchanged.

## Limitations

- Not yet run under KASAN/lockdep. `target_ok()` may read an ACL and sleep; it runs outside
  the index lock, as it did in the callers before.
- A chmod of a directory inside a domain is still handled as before: the domain falls back to
  per-result checks until the next mount (journal 0035). This fix concerns the file's own
  check.
- The public surfaces (`SECURITY.md`, ADR-0011 findings, README status) are not updated in
  this branch. The correction batch on `codex/review-corrections-batch` stages them, and this
  fix joins that batch.

## Next step

Merge into the correction batch. Then run KASAN once for the batch, update the public
surfaces, and open the pull request.

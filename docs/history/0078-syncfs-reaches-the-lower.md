# 0078 — Fix: `syncfs` on the layer did not sync the lower filesystem

Date: 2026-10-13. Status: implemented; `make vm-test` passes, the new test fails without the fix
and the local KASAN job passes. Local series (after pull request 57). Found with fstests
`generic/674` over btrfs (journal 0077).

## What was wrong

The layer had no `sync_fs` superblock operation. `syncfs(2)` on a descriptor of the layer
syncs the layer's superblock, which owns no pages and no metadata, and returned success with
nothing written: the data and the names live in the lower filesystem, which was not asked. The
sync the VFS does when the layer is unmounted had the same non-effect.

What was not affected: `fsync` and `fdatasync` on a file or a directory (delegated to the lower
file, `moobl_fsync`), and `sync(2)`, which visits every superblock, the lower's included. The
write sequence of the client contract (`fsync` the file, rename, `fsync` the directory) never
depended on `syncfs`. A client that wrote several files and then called `syncfs` on the mount
to make them durable together was told they were, and after a power loss they might not be.

The defect is older than this series: it is in pull request 57 and in v0.2.0.

## Fix

`moobl_sync_fs` (`layer/main.c`) calls `sync_filesystem` on the lower superblock, under its
`s_umount`, when the VFS asks for a waiting sync. The layer's superblock is marked
`SB_I_SKIP_SYNC`, so `sync(2)` does not sync the lower a second time through the layer. This is
what overlayfs does for its upper filesystem (`ovl_sync_fs`, `fs/overlayfs/super.c`).

Consequences, intended: `syncfs` on the layer syncs the whole lower filesystem, not only the
directory the layer covers (a filesystem has no narrower sync), and unmounting the layer now
waits for that sync.

Alternative: sync only the inodes the layer has open or cached. Rejected: names and directory
entries of the lower are not reachable that way, and `syncfs` promises the filesystem.

## Test

`vm/suite.sh`, power-loss block (dm-log-writes on ext4): a file is written through the layer
with no `fsync`, `syncfs` is called on the layer's root (`layer syncfs <path>`, new mode of
`tests/layer.c`), and a mark `sf` is taken. The log replayed up to that mark must contain the
file with its content, and so must the replay to the end.

## Evidence

- `make checkpatch`: clean.
- `make vm-test` with the fix: `POWER_LOSS_REPLAY_PASS: marks mkfs m1 m2 m4 sf end`, every marker
  through `MOOBFS_LAYER_PASS`.
- The same suite with `layer/main.c` of the previous commit: `MOOBFS_VM_FAIL: lw-check-syncfs`.
- fstests in the guest, lower btrfs, fixed module: `generic/674` passes (`generic/001` passes,
  `generic/215` fails by design, as before).
- Local KASAN (`vm/kasan-local.sh` on the benchmark host, upstream 6.12.112 with KASAN, UBSAN and
  lockdep), which includes the power-loss block: `MOOBFS_KASAN_PASS`, no kernel diagnostic.

## Limitations

- The power-loss test of `syncfs` runs on ext4 only; btrfs and XFS are covered by the delegation
  being the same call, and for btrfs by `generic/674`, not by a replay.
- Freezing the layer (`fsfreeze`) is still not supported; fstests skips those tests.
- The full fstests runs of journals 0075 and 0077 were not repeated with this module.
- Error reporting: a write-back error of the lower is returned by `syncfs` on the layer only as
  far as `sync_filesystem` returns it; the per-superblock error sequence that `syncfs(2)` checks
  (`s_wb_err`) is the layer's own and never advances. A caller that needs to learn of a failed
  write-back must use `fsync` on the file. Not tested.

## Next step

For the maintainer: whether this fix and the security fix of journal 0074 go together as one
small pull request after 57; the four fstests gaps; the exclusion list.

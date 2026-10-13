# 0074 — Security fix: copy and clone kept set-user-ID, set-group-ID and file capabilities

Date: 2026-10-13. Status: implemented; `make vm-test` passes, the new test fails without the fix
and the local KASAN job passes. Local series (after pull request 57). Found while reading the
code to explain a failure of the first fstests run (journal 0075).

## What was wrong

When an ordinary user changes a file, the kernel clears set-user-ID, set-group-ID and file
capabilities, so that write access to a privileged program is not a way to run code with its
privileges. The layer does this for a write, a splice and `fallocate`: it calls
`file_remove_privs` with the caller's credentials before the lower operation.

Two operations that also change the content did not:

- `copy_file_range` (`moobl_copy_file_range`),
- a clone, `FICLONE` and `FICLONERANGE` (`moobl_remap_file_range`).

Both ran the lower call with the mounter's credentials only. The lower filesystem does clear the
bits itself, but only for a caller without `CAP_FSETID`, and the mounter has it. So through the
layer an ordinary user with write access to a set-user-ID file could replace its content and keep
the bit. overlayfs calls `file_remove_privs` at exactly this point (`fs/overlayfs/file.c:501`).

The layer is mounted without execution (`SB_I_NOEXEC`), so the file cannot be run through the
mount; it can be run from the lower directory by whoever has access there, or after the layer is
unmounted. It contradicts promise 2 of ADR-0011: the lower, asked directly, clears the bits.

A related defect, found with fstests `generic/688`: **an ordinary user could not write to a file
that carries file capabilities at all.** The kernel removes `security.capability` on behalf of the
writer (`cap_inode_killpriv`), the layer's attribute handler passed that removal to the lower with
the writer's credentials, and the lower refused (`CAP_SETFCAP`). The write failed with `EIO`, and
`fallocate` with `EPERM`. This one failed closed: nothing was written and the capability stayed.

## Fix

- `moobl_copy_file_range` and `moobl_remap_file_range` (not for a dedupe, which leaves the
  content as it was) take the target's inode lock, call `file_remove_privs` and keep the lock
  across the lower call, as the write path and overlayfs do.
- `moobl_xattr_set` removes `security.capability` on the lower with the mounter's credentials.
  A removal a user asks for is authorized on the layer's inode before the handler runs
  (`cap_inode_removexattr`, `security/commoncap.c:1041-1048`), so only an authorized caller or the
  kernel itself reaches that point.

Recorded as finding F5 in ADR-0011.

## Tests

`tests/layer.c`, as uid 1001 on a descriptor root opened: `copy_file_range`, a clone (on the lowers
with reflink) and `fallocate` into a 06755 file must leave it 0755 on the lower; then, with
`security.capability` set, a write, a `copy_file_range` and a `fallocate` must succeed and the
attribute must be gone.

## Evidence

- `make checkpatch`: clean.
- The tree before the fix, with the new test: `FAIL` at the check of the mode after
  `copy_file_range` (the file was still 06755), `MOOBFS_VM_FAIL: layer-tests-ext4`.
- `make vm-test` with the fix: `LAYER_PASSTHROUGH_TESTS_PASS` six times (ext4, btrfs, XFS, with
  and without hard links; the clone case runs on btrfs and XFS), `HOSTILE_LOWER_PASS` on the
  three, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`.
- Local KASAN (`vm/kasan-local.sh` on the benchmark host, upstream 6.12.112 with KASAN, UBSAN and
  lockdep): `MOOBFS_KASAN_PASS`, no kernel diagnostic.
- fstests `generic/688` (journal 0075), which failed with `fallocate: Operation not permitted`,
  passes with the fix.

## Limitations

- Other content-changing paths were read and already clear the bits (`write`, `splice`,
  `fallocate`); shared writable mappings do not clear them on any Linux filesystem. No systematic
  search was made for other calls that run on the lower with the mounter's credentials and depend
  on a capability check there; fstests found this one by accident, and S3 (syzkaller) will not
  find this class either. A review of every `override_creds` site against "what does the lower
  decide from the caller's capabilities" is the proper follow-up.
- Versions up to v0.2.0-experimental and pull request 57 have the defect.

## Next step

The fstests results themselves (journal 0075).

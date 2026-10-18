# 0117 — Changes through the layer take write access on the lower mount; an idmapped lower is refused

Date: 2026-10-15. Started about 06:05, ended 06:30 (calendar time, local). Status: fixed;
evidence below. Estimated effort: S.

## Origin

An independent source review of 5582e06 (journal 0114, published later) and its verification
found that the layer never takes write access on the lower mount and passes the lower's ids
through unmapped. Recorded in ADR-0011 as findings F11 and F12.

## Findings

- **F11.** A system call that changes a file takes write access on the mount it was made on
  (`mnt_want_write()` in fs/namei.c, fs/open.c, fs/xattr.c): here the layer's mount, never the
  lower's. `layer/main.c` called `vfs_create()`, `vfs_mkdir()`, `vfs_unlink()`, `vfs_rmdir()`,
  `vfs_rename()`, `vfs_link()`, `vfs_symlink()`, `vfs_setxattr()`, `vfs_removexattr()`,
  `notify_change()`, `vfs_fileattr_set()`, `vfs_set_acl()` and `vfs_remove_acl()` on the lower
  without it. Two consequences:
  - a lower given as a read-only bind mount was written anyway (the lower's superblock was
    writable, so `inode_permission()` did not refuse): promise 2 of ADR-0011;
  - with the lower ext4 frozen, any user's create, `chmod` or `setxattr` started a transaction
    on the frozen filesystem: `WARN_ON(sb->s_writers.frozen == SB_FREEZE_COMPLETE)` in
    `ext4_journal_check_start()` (fs/ext4/ext4_jbd2.c:76, 6.12.112), then a wait for the thaw:
    promise 1 (no kernel warning; a crash with `panic_on_warn`). XFS takes `sb_start_intwrite()`
    in `xfs_trans_alloc()` and only waits.

  Data writes were already covered: the backing file is opened with write access on the lower
  mount (`do_dentry_open()`, fs/open.c:884) and `vfs_iter_write()`, `vfs_fallocate()`,
  `vfs_clone_file_range()` and `vfs_copy_file_range()` take the freeze protection themselves.
  The index writes nothing to the lower.
- **F12.** Every delegated call passes `nop_mnt_idmap`, and `moobl_fill_super()` accepted an
  idmapped mount as `lower=`: permission checks and new files used the on-disk ids, not the
  ones that mount shows. The layer's own mount cannot be idmapped (no `FS_ALLOW_IDMAP`), but that
  never covered the lower. This was already noted as R5 of `docs/project-review-2026-10-10.md`
  and left open.

## Decisions

- **Write access on the lower mount around every change**, as overlayfs does with
  `ovl_want_write()` on its upper mount (called from `ovl_create_object()` and the other
  changing operations, fs/overlayfs/dir.c, 6.12.112). New helpers `moobl_want_write()` and
  `moobl_drop_write()`; `moobl_lock_parent()` takes it before the lower parent's lock and
  `moobl_unlock_parent()` drops it after; rename takes it before `lock_rename()`; setattr,
  setxattr/removexattr (the capability path included), `fileattr_set` and the ACL calls take it
  around the lower call. Errors (`EROFS`, and anything else `mnt_want_write()` returns) go back to
  the caller unchanged.
- **Lock order:** the layer's inode locks (taken by the VFS before our inode operation runs) →
  the lower's freeze protection (`sb_start_write()` inside `mnt_want_write()`) → the lower's
  inode locks. This is overlayfs's order (the overlay parent is locked by the VFS when
  `ovl_create_object()` calls `ovl_want_write()`), and the freeze rule only orders a
  superblock's freeze protection before that same superblock's inode locks. The write path
  (`moobl_write_iter()`) holds the layer inode's lock, takes and drops the lower's write access
  inside `file_remove_privs()` → `moobl_setattr()`, then `vfs_iter_write()` takes the lower's
  freeze protection again: never nested. lockdep reported nothing in the runs below.
- **Refuse an idmapped lower at mount** (`is_idmapped_mnt(sbi->lower.mnt)`, `EINVAL`, "lower
  cannot be an idmapped mount"). Alternative considered: pass `mnt_idmap()` of the lower mount
  to every delegated call and to the homogeneity rule of the index; left for a later decision
  with its own tests. Compatibility decision recorded in ADR-0011; no on-disk format changes.

## Tests

New suite step `lowerro` (`vm/suite.sh`), listed in `MOOB_ONLY` and run in full runs:

1. A read-only bind mount of a lower directory as `lower=`: `touch`, `mkdir`, `ln -s`, `mv`,
   `rm`, `rmdir`, `chmod`, `truncate` and `moobfs-attr set` through the layer must fail with
   "Read-only file system", reading must work, and the lower tree (names, mode, size,
   attributes) must be unchanged. The step first checks that the bind mount itself refuses a
   write, so it cannot pass vacuously.
2. The lower ext4 frozen (`fsfreeze --freeze`): a create, a `chmod` and a typed attribute set
   through the layer start in the background; after 2 s none may have completed and the kernel
   log after a marker written to `/dev/kmsg` must have no `WARNING:` or `BUG:`; after
   `fsfreeze --unfreeze` all three complete and are visible on the lower.
3. An idmapped bind mount (`mount --bind -o X-mount.idmap=b:0:10000:65536`, util-linux 2.39 or
   later) as `lower=` must be refused with the message above; skipped with a printed reason
   where the guest's `mount` cannot create one (the busybox initramfs harness).

## Results

On the benchmark host (upstream 6.12.112 with KASAN, UBSAN and lockdep, util-linux 2.41.5 in the
guest):

- `MOOB_ONLY=lowerro sh vm/kasan-local.sh` (tree synced to a fresh directory): exit 0,
  `LOWERRO_PASS: read-only lower mount refused 9 changes with EROFS, lower unchanged; 3 changes
  waited for the thaw of a frozen lower, no kernel warning; idmapped lower: refused`,
  `MOOBFS_KASAN_ONLY_PASS`; 0 warnings from `ext4_jbd2.c:76` in the console log.
- Negative round, `moobl_want_write()` returning 0 and `moobl_drop_write()` doing nothing (the
  idmap refusal kept), same command:
  - full step: fails at `lr-ro-accepted: touch /tmp/moob-layer/d/new`, the read-only lower was
    written;
  - step without its read-only part: fails at `lr-fz-kernel-warning`, with 7 reports of
    `WARNING: CPU: 1 PID: 167 at fs/ext4/ext4_jbd2.c:76 ext4_journal_check_start+0x186/0x240`
    from `moobl_create` → `ext4_create`, `moobl_setattr` → `ext4_setattr` and
    `moobl_xattr_set` → `ext4_xattr_set`.
- `sh scripts/checkpatch.sh`: exit 0. `make lint` (shellcheck 0.11.0, `--severity=warning`, and
  the SPDX check): exit 0.

## Limitations

- Only ext4 was frozen; on btrfs and XFS the change is the same call, not exercised by the step.
- The idmapped case is refused, not supported; the busybox initramfs harness (`make vm-test`)
  skips it.
- The full suite was not run with this change; the KASAN workflow of the pull request is the
  complete run.

## Next step

Merge with the other fixes of journal 0114 in one batch and run the full KASAN workflow once.

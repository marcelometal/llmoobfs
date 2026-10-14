# 0100 — A nested subvolume is recognised by its device, not by its inode number

Date: 2026-10-14. Status: implemented; run in the local VM (results below). Not run under KASAN.

## Goal

Journal 0083 refused a btrfs subvolume below the lower root by testing for inode 256
(`BTRFS_FIRST_FREE_OBJECTID`) on a btrfs superblock. Asked on linux-btrfs (journal 0097), a
btrfs developer answered that the number is btrfs-specific and that the common practice is
`st_dev`: a subvolume is a separate filesystem with its own anonymous device, and every inode in
it reports that device. The reply on the list committed to the change; this journal makes it.

## Change

- `layer/main.c`: the mount records `lower_dev`, the device `vfs_getattr_nosec()` reports for
  the lower root. `moobl_other_device()` returns 1 for a directory whose reported device
  differs, 0 when it does not or the dentry is not a directory, or the getattr error.
- `moobl_lookup()` returns `EXDEV` for such a directory, as before for inode 256, and passes a
  getattr error on. `scan_child()` in `layer/index.c` leaves it out of the build, as before.
- `moobl_nested_subvolume()`, the `BTRFS_SUPER_MAGIC` test and `<linux/btrfs_tree.h>` are gone.

Why the root and not the parent: the reply on the list said "the lower child and its lower
parent". Every directory the layer reaches was itself checked against the root on the way down,
so its parent has the root's device; comparing with the root is the same test with no second
getattr.

Why `vfs_getattr_nosec()`: the check is the layer's own bookkeeping, not an access by the
caller, so it should not be refused or audited by an LSM; the caller's own lookup is checked by
the lower as before. `AT_STATX_DONT_SYNC` because only the device is needed.

Why the reported device and not `i_sb->s_dev`: btrfs subvolumes share one superblock; the
device of a subvolume is `root->anon_dev`, set in `btrfs_getattr()` (`fs/btrfs/inode.c`,
v6.12.112).

Cost: one getattr per directory lookup that misses the dentry cache and per directory scanned
by a build. Not measured.

## Results

- `make vm-test` (Debian 6.12.111 in QEMU), complete: `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS`,
  `MOOBFS_LAYER_PASS` over ext4, btrfs and XFS. The suite's existing subvolume step (journal
  0083: a subvolume created inside a btrfs lower must not be entered through the layer and its
  attribute must not be indexed) passes with the new test.
- Negative run: with `moobl_other_device()` forced to return 0, `MOOB_ONLY=btrfs make vm-test`
  fails at `subvol-entered`. The step therefore depends on the check.
- Module built with no compiler warning; `sh scripts/checkpatch.sh`: exit 0.

## Limitations

- Only btrfs subvolumes were run, and the lowers of the suite (ext4, btrfs, XFS). Other
  filesystems that report another device for a directory that is not a mount point were not
  looked for; such a directory would now be refused with `EXDEV`.
- The anonymous-device pool limit mentioned on the list (too many subvolumes cannot be read)
  is btrfs's and unchanged by this.

## Next step

Answer the linux-btrfs thread with the commit once it is public. Then ADR-0012, second half.

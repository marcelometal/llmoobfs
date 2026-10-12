# 0012 — Phase 2b: full pass-through over ext4, btrfs and XFS, with delegated reflink

Date: 2026-10-10, America/Sao_Paulo. Status: implemented and validated in the test VM.

## Request

Start phase 2b of the M2 specification: rename, links, opaque xattrs, btrfs and XFS as
lower filesystems, delegated reflink. Before that the user asked to fix the attribution of the
commits on GitHub (see its own section).

## Implementation

- `layer/main.c` (now 1 101 lines): `rename` with `lock_rename` and `vfs_rename`
  (includes `RENAME_NOREPLACE` and `RENAME_EXCHANGE` passed to the lower); `link` and `symlink`
  through `vfs_link`/`vfs_symlink`, `get_link` through `vfs_get_link`; mount option `nolinks`
  that returns `EPERM` for both; opaque xattr in all namespaces through a single
  `xattr_handler` with an empty prefix delegating to `vfs_getxattr`/`vfs_setxattr`/
  `vfs_removexattr`, `listxattr` through `vfs_listxattr`; `remap_file_range` delegating to
  `vfs_clone_file_range` and `vfs_dedupe_file_range_one`; `copy_file_range` delegating to
  `vfs_copy_file_range`; `fiemap` calling the lower inode's; symlink inodes with
  their own operations. All with the mounter's credentials in the delegated calls.
- `tests/layer.c` (257 lines): adds rename in five variants, xattr (set, exclusive
  create, replace, list, remove, missing), links and symlinks (or their refusal with `nolinks`),
  `FICLONE` with a check of `FIEMAP_EXTENT_SHARED` on both sides and copy-on-write of the
  lower, `FIDEDUPERANGE` with a byte-by-byte copy that comes to share extents, and
  `EOPNOTSUPP` where the lower has no reflink.
- Harness: ext4 (64 MiB), btrfs (256 MiB) and XFS (300 MiB) images created in the container as
  sparse files and attached to QEMU as virtio disks; module chain resolved from
  `modules.dep` for `virtio_blk`, `ext4`, `btrfs` and `xfs` plus `crc32c_generic`; VM with
  512 MiB; `vm/init` repeats, for each lower, the pass-through test, three `fsx` runs,
  comparison with the lower copy, remount with `nolinks` and a new test; `vm/Dockerfile`
  gains `btrfs-progs` and `xfsprogs`.

## Evidence executed

`make vm-test`, Debian kernel `6.12.111+deb13-amd64`, first run of phase 2b without
fixes. `build/vm/console.log`:

```text
MOOBFS_VM_PASS: shared extents, independent inodes, reads, readonly, remount, 15 malformed images
LAYER_LOWER_PASS: ext4 reflink=no
LAYER_LOWER_PASS: btrfs reflink=yes
LAYER_LOWER_PASS: xfs reflink=yes
MOOBFS_LAYER_PASS: pass-through over ext4, btrfs and xfs; layer tests; fsx 9000 ops each; reflink on btrfs/xfs; nolinks; unload
```

Module chain loaded: `crc32c_generic virtio_blk crc16 mbcache jbd2 ext4 xor raid6_pq
libcrc32c btrfs xfs`. No `BUG`, `Oops`, `WARNING` or `Call Trace` in `dmesg`. Guest time
until shutdown: 39 s under TCG. SHA-256 of `moobfs_layer.ko`:
`8d669c89644cc3d72a81212fe3707347077776d69a35351ec80f4c55f6be08b4`.

Result relevant to the project: the `cp --reflink` equivalent (`FICLONE`) and `FIDEDUPERANGE`
work through the layer over btrfs and XFS without a single line of our own dedup code,
and the forwarded `FIEMAP` shows the shared extents on both sides. It is the
practical confirmation of item 4 of ADR-0007.

## Commit attribution
  
  Decision: all project commits use the identity `Claudio <claudio@studiomoob.com>`;
  this repository was configured for that and the previous history was rewritten on 2026-10-10
  to apply the same identity to the old commits (see journal 0018).
  
  ## Limitations

Kernel without KASAN or `PROVE_LOCKING`; the nesting of the layer's `lock_rename` over the
lower's and the inode locks in `setattr`/`write` were not validated by lockdep. Full xfstests
not executed (it requires building the whole package; left for a separate harness).
`dm-log-writes` not yet applied. Without static git or SQLite in the VM, acceptance with a
real client remains in phase 2e. `mknod` and opening devices remain out.
Permissions: name operations use the caller's credentials with `nop_mnt_idmap`, I/O and
reflink use the mounter's; security review pending before use outside a VM.

## Next step

Phase 2c: typed encoding of `user.moob.*` with validation in `setxattr`, `moobfs-check` and
`moobfs-attr` in user space, fuzzing of the encoding. In parallel, investigate a
separate harness for a kernel with KASAN/lockdep (virtme-ng on an x86 runner).

# 0011 — Phase 2a: minimal stacked layer over ext4, validated in a VM

Date: 2026-10-10, America/Sao_Paulo. Status: implemented and validated in the test VM.

## Request

Start phase 2a of the M2 specification (ADR-0007): minimal wrapfs-style pass-through over a
mature filesystem, with `fsx`. The module's language was decided in this session (below).

## Implementation

- `layer/main.c` (module `moobfs_layer.ko`, mount type `moobfs_layer`, option `lower=`):
  one layer inode per lower inode (`iget5_locked` keyed by the lower inode, like
  ecryptfs), lower path in `d_fsdata`, I/O delegated through `fs/backing-file.c`
  (`backing_file_open/read_iter/write_iter/splice/mmap`, like overlayfs), the mounter's
  credentials stored in the superblock and used in delegated I/O. Operations: lookup, readdir, create,
  mkdir, unlink, rmdir, getattr, setattr (truncate), permission, read, write, shared and
  private mmap, fsync, llseek, fallocate, splice, statfs. No link, symlink or rename (`EPERM` from the
  VFS), no xattr, no index: per the phase's scope.
- Mount refuses: missing `lower=`, nonexistent directory, stacking over another
  `moobfs_layer`, depth above `FILESYSTEM_MAX_STACK_DEPTH`.
- `tests/layer.c`: pass-through acceptance, checking in the lower directory inodes, modes,
  sizes and bytes after each operation.
- `tests/third_party/fsx/`: `fsx.c` and `statx.h` from xfstests (commit `57d71a8`, 2026-03-17),
  with our own `global.h` to compile without the xfstests build. GPL-2.0 license.
- Harness: `make layer`; `vm/in-container.sh` creates a 64 MiB ext4 image with `mke2fs`, resolves
  the ext4 module chain in `modules.dep` and decompresses each one for the VM; `vm/init` runs
  M1 and then phase 2a; `vm/Dockerfile` gains `e2fsprogs`; QEMU timeout 180 s → 600 s.

## Evidence executed

`make vm-test` (Docker, QEMU TCG on an arm64 host), Debian kernel `6.12.111+deb13-amd64`, GCC
14.2.0, QEMU 10.0.13. `build/vm/console.log`:

```text
MOOBFS_VM_PASS: shared extents, independent inodes, reads, readonly, remount, 15 malformed images
LAYER_PASSTHROUGH_TESTS_PASS
All 10000 operations completed A-OK!
All 10000 operations completed A-OK!
All 3000 operations completed A-OK!
MOOBFS_LAYER_PASS: pass-through over ext4, layer tests, fsx 23000 ops, remount, unload
```

Three `fsx` runs (seeds 1, 2 with 512-byte I/O, 3 with a 2 MB file), final file
compared byte by byte with the lower copy, unmount, remount and module unload
without `BUG`, `Oops` or `WARNING` in `dmesg`. SHA-256 of `moobfs_layer.ko`:
`1de9a67d9aa45ee0dced8b9c54a846103ff70c99cf746193977173eeb515f3e4`. The M1 regression passed
unchanged in the same run.

## Attempts and fixes

1. The Debian kernel headers carry the `backing_file_open(const struct file *, …)` API,
   newer than the upstream v6.12 tag (`const struct path *`). The module compiles only against the
   pinned headers; the documentation in `kernel-infrastructure.md` cites upstream and the difference
   is recorded here.
2. ext4 requests the `crc32c` shash through the crypto API, a dependency that `modules.dep` does not record;
   `crc32c_generic.ko` was added to the chain.
3. `vfs_getattr` triggers `WARN_ON_ONCE` when it receives internal query flags; the
   correct variant for a stacked FS is `vfs_getattr_nosec`, as in ecryptfs. Fixed; the warning
   appeared in `dmesg` and made the test fail with `EPERM`.
4. Bug in the test itself: a path function with a single static buffer did `link(a, a)`.
   Fixed with rotating buffers.

## Decision: module in C

Rust was considered and discarded for the module: in 6.12 there are no stable VFS abstractions in
Rust (the Rust filesystems patch set was abandoned in 2024 and never entered the
tree); a stacked FS depends on dozens of VFS helpers whose bindings would have to be
written by hand; and the target Debian kernel is not built with `CONFIG_RUST`. Rust remains an
alternative to evaluate for the user-space tools.

## Limitations

Kernel without KASAN and without `PROVE_LOCKING`; the nesting of the layer's inode locks over those of the
lower was not validated by lockdep. No full xfstests, no btrfs/XFS as the lower, no
dm-log-writes in this phase. Missing `rename` prevents git and SQLite for now (phase 2b).
Credentials: directory operations use the caller's with `nop_mnt_idmap`; only delegated
I/O uses the mounter's. This needs a security review before any use outside
a VM.

## Next step

Phase 2b: rename, unlink of open files already covered, opaque xattrs, btrfs and XFS as
lower filesystems, delegated reflink, and a kernel with KASAN/lockdep via virtme-ng.

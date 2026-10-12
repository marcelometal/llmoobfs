# Mature kernel infrastructure reusable by MoobFS

Date: 2026-10-10. Checked against the Linux **v6.12** source code (upstream tag) and against
`build/vm/kernel.config` of the Debian 6.12.111 kernel used in the VM. Goal: before writing
M2, list what the kernel already offers a filesystem module, so as not to rewrite what
ext4, XFS, btrfs, ocfs2 and GFS2 have used for years. "In the VM" indicates the configuration option
of the target kernel (`=y` built in, `=m` loadable module).

## Reuse (proposed decision for M2)

| M2 need | Infrastructure | Maturity | In the VM | Verified notes |
| --- | --- | --- | --- | --- |
| Metadata journaling and recovery after a crash | **jbd2** (`fs/jbd2/`) | Since 2006; used by ext4 and ocfs2 | `CONFIG_JBD2=m` | `jbd2_journal_init_dev`, `jbd2_journal_load`, `jbd2_journal_start`, `jbd2_journal_flush`, `jbd2_journal_destroy` are `EXPORT_SYMBOL`, hence accessible to an out-of-tree module. ocfs2 is the proof that an FS that is not ext4 can use it. |
| Buffered read and write, writable mmap, truncation, writeback | **iomap** (`fs/iomap/`) | Used by XFS, GFS2, zonefs, erofs, ext4 (DIO) | `CONFIG_FS_IOMAP=y` | Exported with `_GPL`: `iomap_file_buffered_write`, `iomap_page_mkwrite`, `iomap_truncate_page`, `iomap_zero_range`, `iomap_writepages`, `iomap_dirty_folio`. M1 already uses `iomap_read_folio` and `iomap_fiemap`. zonefs (about 2 600 lines) is the smallest in-tree reference of a writable FS over iomap. |
| Share bytes between files through a standard interface | **remap_file_range** (`fs/remap_range.c`) | ioctls `FICLONE`, `FICLONERANGE`, `FIDEDUPERANGE`; used by XFS, btrfs, ocfs2 | VFS, always present | `generic_remap_file_range_prep`, `vfs_clone_file_range`, `vfs_dedupe_file_range` exported. Implementing `.remap_file_range` makes `cp --reflink`, `duperemove` and `fsdupes` work without a tool of our own. `REMAP_FILE_DEDUP` requires the VFS to compare the bytes before sharing, which is exactly the M1 rule. |
| Extended attributes with namespaces and permissions | **VFS xattr** (`fs/xattr.c`, `struct xattr_handler`) | Universal | VFS | `vfs_getxattr`, `vfs_setxattr`, `vfs_listxattr` take care of permission, the `user.`/`trusted.`/`security.` prefixes and system calls. The module only provides per-prefix handlers. |
| Deduplicate identical xattr blocks across inodes | **mbcache** (`fs/mbcache.c`) | Used by ext2 and ext4 | Built in with ext4 | `mb_cache_entry_create`, `mb_cache_entry_find_first/next` exported. Solves the "many memories with the same set of attributes" case without code of our own. |
| Cryptographic verification of immutable content | **fs-verity** (`fs/verity/`) | ext4, f2fs, btrfs | `CONFIG_FS_VERITY=y` | Per-file Merkle tree, verified on read. Candidate to replace the M1 payload SHA-256 for sealed objects, in a later phase. |
| Page cache, dentries, inodes, POSIX locks (`fcntl`), writeback, freeze | Generic VFS | Universal | VFS | SQLite and git need `fcntl` locks and `fsync`; the VFS delivers both to any FS that implements `fsync` correctly. |
| Checksums | `lib/crc32c`, `crypto/sha256` | Universal | yes | Already used by M1. |
| In-memory structures | `lib/btree.c`, maple tree, xarray, rbtree | Universal | yes | For the M2 in-RAM attribute index; an on-disk B+ tree is left for later. |

## Stacked filesystems (addendum 2026-10-10, ADR-0007)

| Need | Infrastructure | In the VM | Notes verified on v6.12 |
| --- | --- | --- | --- |
| Delegate read/write/mmap/splice to a file of the lower FS | `fs/backing-file.c`: `backing_file_open`, `backing_file_read_iter`, `backing_file_write_iter`, `backing_file_mmap`, `backing_file_splice_read/write` | VFS | Exported `_GPL`; used by overlayfs (11 calls in `fs/overlayfs/file.c`) and by FUSE passthrough |
| Reference model of a writable stacked FS | overlayfs (`fs/overlayfs/`, ~12.5 thousand lines) | `CONFIG_OVERLAY_FS=m` | Mounter credentials via `override_creds`, rules on modifying the lower filesystem, support in xfstests (`./check --fs overlay`) |
| Reference model of a 1:1 stacked FS | ecryptfs (`fs/ecryptfs/`, ~8.2 thousand lines) | `CONFIG_ECRYPT_FS=m` | One layer inode per lower inode; xattr passthrough |
| Delegated reflink/dedup | `vfs_clone_file_range`, `vfs_dedupe_file_range_one` | VFS | Called on the lower files; `EOPNOTSUPP` on ext4 |

## Test with (do not write our own harness)

| Need | Tool | In the VM |
| --- | --- | --- |
| Consistency after power loss | `dm-log-writes` + xfstests-style tests `generic/482` | `CONFIG_DM_LOG_WRITES=m` |
| I/O error injection | `dm-flakey` | `CONFIG_DM_FLAKEY=m` |
| POSIX regression | xfstests `generic` group; `fsx` and `fsstress` (in xfstests' `ltp/`) | user space |
| Instrumented kernel | virtme-ng with `CONFIG_KASAN` and `CONFIG_PROVE_LOCKING` | our own kernel |
| Parser fuzzing | libFuzzer/AFL++ over `common/` | host |

## Evaluated and not adopted now

- **Block-level snapshots** (`dm-snapshot`, `dm-thin`, both `=m` in the VM). They give whole-volume snapshots without code in the FS, but they do not give per-file revisions nor stable memory identity, which is what M3 asks for. They remain as a comparison baseline for M3.
- **Generic block allocator**: does not exist in the kernel. ext2 (`balloc.c`, 1 535 lines) is the simplest reference for block and inode bitmaps.
- **Generic on-disk B+ tree**: does not exist. Each FS has its own. `fs/befs/btree.c` (read) and the BFS book are references; M2 starts without a persistent index.
- **Buffer heads for data**: ext2/ext3 use them; M1 already chose iomap. See the open question below.

## Open question of the "full volume" form (ADR-0006, superseded)

There is no in-tree FS that combines **iomap for buffered data** with **jbd2 for metadata**:
ext4 uses jbd2 with buffer heads for buffered data; XFS and GFS2 use iomap with their own log.
Both known combinations work; ours is unprecedented and may require explicit ordering
(data before the metadata commit, equivalent to `data=ordered`) via jbd2 commit
callbacks. Safe alternative if the experiment fails: data through buffer heads in M2, like ext3,
migrating to iomap later. This should be the first M2 prototype, before the final format.

## Code references to read

- `fs/ocfs2/journal.c`: use of jbd2 by an FS that is not ext4.
- `fs/zonefs/file.c`: buffered and direct writes with iomap in a small FS.
- `fs/ext2/{balloc,ialloc,dir,xattr}.c`: bitmap allocation, linear directories, xattr with mbcache.
- `fs/xfs/xfs_reflink.c` and `fs/btrfs/reflink.c`: implementation of `remap_file_range` with copy-on-write.
- `fs/remap_range.c`: what the VFS already does before calling the FS (byte comparison in dedupe).

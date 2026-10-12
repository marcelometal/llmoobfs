# M2 specification — POSIX writable volume with shareable extents

Status: **proposal** (2026-10-10). Nothing here is implemented or tested. Decisions in
[ADR-0005](decisions/0005-memory-model-bfs-reference.md) and
[ADR-0006](decisions/0006-m2-posix-writes.md). Reused infrastructure in
[kernel-infrastructure.md](kernel-infrastructure.md). The M1 format remains described in
[format-m1.md](format-m1.md) and is not changed.

## 1. Scope

In M2: a writable, hierarchical filesystem with metadata journaling, extents
shareable between files with copy-on-write, typed attributes via xattr, an in-RAM attribute
index and query by ioctl, `mkfs`/`fsck` v2 tools, import of M1 images.

Out: snapshots and per-memory revisions (M3), automatic dedup on write, hard links,
symlinks, quotas, POSIX ACLs, encryption, fs-verity, NFS export, user namespaces,
persistent attribute index, direct I/O (returns `EINVAL` in M2).

## 2. Limits

| Item | Limit | Reason |
| --- | --- | --- |
| Block size | 4 096 bytes | Same as M1 and as the x86-64 page size |
| Volume | 64 MiB to 16 GiB | 32-bit block numbers; bounded in-RAM index |
| Inodes | up to 65 536 | In-RAM attribute index rebuilt at mount |
| File size | up to 4 GiB | Extents with 32-bit offsets in blocks |
| Entry name | 1 to 255 bytes, any byte except `/` and NUL | POSIX; ai-memory uses UTF-8 and UUIDs |
| Directory depth | no limit of its own | VFS limit (`PATH_MAX`) |
| xattr per inode | up to 4 KiB in total, up to 64 attributes | One xattr block per inode, like ext2 |
| Journal | 8 MiB to 128 MiB, fixed at `mkfs` | jbd2 requires a contiguous area |

## 3. On-disk layout (v2)

```text
block 0            Superblock A
block 1            Superblock B (copy)
block 2..          Block bitmap
...                Inode bitmap
...                Inode table (256 bytes per inode)
...                Shareable extent table (objects)
...                jbd2 journal
...                Data, directory and xattr blocks
```

All integers little-endian, explicit serialization, reserved zeroed, as in M1.
CRC32C on the superblock, each bitmap block, each inode, each object record and each directory
and xattr block, with the checksum field zeroed during the computation. The journal has
jbd2's own checksums (feature `JBD2_FEATURE_INCOMPAT_CSUM_V3`).

### 3.1 Superblock (4 096 bytes)

Magic `MOOBFSW2`, version 2, volume UUID, block size, block count, inode
count, offsets of each region, journal blocks, feature flags (compat, ro_compat,
incompat, like ext4), state (`clean`/`dirty`/`error`), mount counter, timestamps of
last mount and last write, CRC32C. An unknown incompat flag prevents mounting; an unknown
ro_compat flag mounts read-only.

### 3.2 Inode (256 bytes)

Mode, uid, gid, logical size, links (always 1 for files; directories count `.` and
subdirectories), timestamps in nanoseconds (atime, mtime, ctime, crtime), generation, flags,
xattr block (0 if inline empty), **up to 8 inline extents** `(offset lógico em blocos,
objeto ou bloco físico, comprimento em blocos, flags)`, pointer to an additional extent
block, 96-byte inline xattr area for small attributes, CRC32C.

An extent points either to physical blocks exclusive to the inode (flag `OWN`) or to a shareable
**object** (flag `SHARED`). A write over a `SHARED` extent does copy-on-write: it allocates new
blocks, copies what was not overwritten, switches the extent to `OWN` and decrements the object.

### 3.3 Object (64-byte record in the object table)

ID, starting physical block, length in blocks, logical size in bytes, reference
count, SHA-256 of the logical content (valid only with flag `SEALED`), flags, CRC32C.
An object with `refcount == 0` is freed in the same transaction. `SEALED` is set when the
object is born from `remap_file_range` or import; an object is immutable by construction, since
every write to a shared extent does copy-on-write.

### 3.4 Directories

Linear ext2-style blocks: entries `(inode, tamanho do registro, tamanho do nome, tipo,
nome)`, with `.` and `..`, and a footer with CRC32C per block. Linear search is acceptable up to the
M2 inode limit; htree or B+ tree are left for later, with measurement.

### 3.5 Typed attributes (xattr)

Namespace `user.moob.<name>`. Value serialized as `type (1 byte) + payload`:
`string` UTF-8, `int64`, `double`, `bool`, `time` (ns since epoch, int64), `raw`.
`setxattr` with an invalid type or a size out of bounds returns `EINVAL`. Other namespaces
(`user.*` without the `moob.` prefix, `trusted.`, `security.`) are accepted as opaque bytes, for
compatibility with common tools, but not indexed. Identical xattr blocks across
inodes are shared via `mbcache` with refcount, like ext2/ext4.

### 3.6 Journal

Contiguous area managed by jbd2. Transactions cover bitmaps, inodes, object table,
directories and xattr blocks. File data does not go into the journal; it is forced to disk
before the commit of the transaction that publishes its metadata (`data=ordered` ordering). The
superblock is updated outside the journal with alternating A/B writes.

## 4. Operations

| Operation | Behavior in M2 |
| --- | --- |
| `create`, `mkdir`, `unlink`, `rmdir`, `rename` | jbd2 transactions; `rename` atomic even over an existing target; `RENAME_EXCHANGE` and `RENAME_NOREPLACE` supported |
| `link`, `symlink` | `EPERM` |
| `read`, `write`, `truncate`, `fallocate` (default mode only) | iomap; a write to a `SHARED` extent does copy-on-write |
| `mmap` | Private and shared, read and write (`iomap_page_mkwrite`) |
| `fsync`, `fdatasync` | Forces the inode's data and does `jbd2_journal_force_commit` of the pending transaction; a success return implies durability of that file's metadata and data |
| `syncfs`, `sync`, freeze | Via the VFS `sync_fs` and `freeze_fs` |
| `fcntl` locks, `flock` | Generic VFS ones |
| `setxattr`, `getxattr`, `listxattr`, `removexattr` | Per-prefix handlers; `user.moob.*` validated and indexed |
| `FICLONE`, `FICLONERANGE`, `FIDEDUPERANGE` | `.remap_file_range`: creates or reuses a `SEALED` object, points `SHARED` extents of both files; for `DEDUP` the VFS has already compared the bytes |
| `FIEMAP` | As in M1, with the `SHARED` flag on object extents |
| `QUERY` (ioctl on `.control`) | Predicate: up to 4 terms `(attribute, operator ∈ {=, <, <=, >, >=, prefix}, typed value)` in conjunction; returns up to N inodes with numbers and canonical paths; reads the in-RAM index |
| `GET_INFO` (ioctl) | UUID, version, limits, journal state, counters |
| `O_DIRECT` | `EINVAL` |

`ro` mount: no transaction is opened; the journal is only replayed if `dirty`
(`norecovery` mounts without replaying and refuses writes). Remount `rw → ro` waits for transactions.

## 5. In-RAM attribute index

At mount, after the jbd2 replay, the module walks the inode table and loads the
`user.moob.*` attributes into in-memory structures (`xarray` per attribute, keyed by the encoded
value for ordering; `rbtree` for strings). Mount cost proportional to the number of
inodes, bounded by the ceiling of 65 536. Each `setxattr`/`removexattr`/`unlink` updates the index
within the same code path that writes the transaction, under the inode lock; the consistency
of the index with the disk is guaranteed by rebuilding at the next mount, not by persistence.
Estimated memory: up to 64 attributes × 65 536 inodes × ~64 bytes ≈ 256 MiB in the worst case;
the mount rejects volumes whose index exceeds the configurable budget (default 128 MiB).

## 6. Durability and recovery

- A successful `fsync` guarantees the data and metadata of the file and of the directory entries that
  reference it (like ext4 `data=ordered` with `auto_da_alloc` off; we promise no more).
- After a crash, `mount` replays the journal via jbd2; metadata stays at the last commit. Data
  written without `fsync` may be lost; blocks from another file never appear (ordering).
- An I/O error in metadata aborts the journal (`jbd2_journal_abort`), the volume becomes read-only and
  marks `error` in the superblock. An error in data returns to the caller and marks `AS_EIO` in the mapping.
- `fsck.moobfs` v2: checks superblock, bitmaps against real usage, inodes, extents, objects and
  refcounts, directories, xattr and checksums; replays or discards the journal only with an explicit
  option; **does not repair** in M2, only diagnoses (like M1).

## 7. Tools

- `mkfs.moobfs` v2: creates an empty volume or `--import-m1 image.img` (each M1 file becomes a
  file at the root, M1 objects become shared `SEALED` objects). Refuses to overwrite
  devices or files that already contain a known magic, unless `--force`.
- `fsck.moobfs` v2: JSON output, distinct exit code per problem class.
- `moobfs-dedup` (user space): scans the mount, groups by SHA-256 and calls
  `FIDEDUPERANGE`; it has no access to the format, it only uses standard ioctls. It can be replaced by
  `duperemove`, which serves as a conformance test.

## 8. Invalid inputs that the parser and the module must reject

Superblock with wrong magic/version/checksum or inconsistent sizes; unknown incompat
flags; bitmaps with bits outside the volume; inode with invalid mode, overlapping extents,
extents outside the volume, reference to a nonexistent object or one with `refcount` 0; object with
`refcount` greater than the real references (fsck diagnosis); directory with an entry pointing
to a free inode, name with `/` or NUL, invalid record size, `.`/`..` missing or
wrong, cycle; xattr with invalid type, size out of bounds, duplicate name; journal with
invalid checksum or out-of-order sequence (handled by jbd2, but covered in a test); truncated
volume. Each case has an image generated by `tests/test_images.py` and is tested in the tools and
at mount, like the 15 cases of M1.

## 9. Tests and required evidence

1. `make test`: shared parser, tools, sanitizers, fuzzing of `common/` (versioned
   corpus, bounded execution in CI).
2. `make vm-test`: in addition to M1, creates a v2 volume and runs, in sequence, with evidence recorded
   in `build/vm/`:
   - xfstests `generic` group with exclusions documented per out-of-scope item;
   - `fsx` and `fsstress` for a fixed time;
   - `dm-log-writes`: `rename`/`fsync`/`FIDEDUPERANGE` load, replay at each FUA mark,
     `fsck` and hash comparison;
   - `dm-flakey`: I/O error during commit, check read-only volume and clean remount;
   - invalid images from section 8;
   - **acceptance with a real client**: `git init` + commits, SQLite in WAL with concurrent writer and reader,
     `ai-memory` with `data_dir` on the mount running a captured session and a
     `reindex`; success is a consistent `ai-memory status` and a clean `fsck` after unmounting.
3. Kernel with KASAN and `PROVE_LOCKING` via virtme-ng running the same suite, at least before
   each declared milestone.
4. No result is declared without the command and the summary in `docs/history/`.

## 10. Compatibility

New format, incompatible with M1 (distinct magic). M1 images remain readable by the
M1 module and tools from the Git history and convertible by `--import-m1`. Changes within
v2 use compat/ro_compat/incompat flags, never silent reinterpretation of fields. The UAPI
of the `QUERY`/`GET_INFO` ioctls has its own version number; an unknown command returns
`ENOTTY`.

## 11. Phases and exit criterion

| Phase | Deliverable | Exit |
| --- | --- | --- |
| 2a | iomap + jbd2 prototype on a minimal FS (one flat directory, no objects or xattr) | Decides item 3 of ADR-0006; `fsx` passes; a crash with dm-log-writes recovers |
| 2b | Complete v2 format, `mkfs`/`fsck`, parser in `common/`, invalid images, fuzzing | `make test` with fuzzing; invalid images rejected |
| 2c | Directories, rename, truncate, mmap, fsync; xfstests `generic` with exclusions | Versioned list of passing/excluded tests |
| 2d | Objects, `remap_file_range`, copy-on-write, FIEMAP; `duperemove` works | Dedup of a real ai-memory corpus measured |
| 2e | Typed xattrs, mbcache, in-RAM index, `QUERY` | Oracle: same queries in Haiku's `bfs_shell` |
| 2f | Acceptance with git + SQLite + ai-memory; KASAN/lockdep | Journal in `docs/history/` with commands and numbers |

## 12. Open questions

- Buffered iomap with ordered jbd2 (phase 2a decides).
- `atime` policy (proposal: `relatime` default, `noatime` recommended for memories).
- Whether `QUERY` should return paths or only inodes (paths require reverse lookup; proposal:
  inodes + handle via `open_by_handle_at`, optional and bounded paths).
- Default journal size as a function of the volume.
- Whether `SEALED` objects should get optional fs-verity already in M2 or only in M3.

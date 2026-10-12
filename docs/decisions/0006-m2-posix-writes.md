# ADR-0006 — M2 with POSIX writes over kernel infrastructure

Date: 2026-10-10. Status: **superseded by [ADR-0007](0007-stacked-memory-layer.md)** on
2026-10-10, the same day, before any implementation. Preserved as a record of the "full
volume" form; the requirements of POSIX writes, acceptance with a real client and reuse of
kernel infrastructure remain valid and were carried over to ADR-0007.

## Context

The original proposal (`docs/native-mvp.md` §5) foresaw writes only through a `PUT` ioctl on a
control file, without `write(2)`, `rename`, `unlink` or writable `mmap`. The technical reading
of ai-memory (`docs/research-ai-memory.md`) showed that a real memory client for agents
writes files with `std::fs` (tmp + rename + fsync), maintains a git repository (`.git/` with
mmap) and a SQLite in WAL (`fcntl` locks, `-shm` with writable `MAP_SHARED`). None of these
paths works with ioctl-only writes. The user asked not to rewrite what the kernel
already has and to specify M2 with POSIX writes.

## Decision

1. **M2 is a writable POSIX filesystem**: `create`, `write`, `truncate`, `rename`, `unlink`,
   `mkdir`, `rmdir`, `fsync`, writable shared `mmap`, `fcntl` locks, xattr.
   Hard links and symlinks stay out of M2 (they return `EPERM`), because they are not required by the
   target clients and their absence simplifies reference counting and confinement.
2. **Journaling via jbd2**, metadata mode with data ordering before commit
   (semantics equivalent to `data=ordered`). Do not write our own transactional log.
3. **Data via iomap**, keeping the M1 choice. The iomap + jbd2 combination is unprecedented
   in-tree; the first M2 prototype validates this combination and, if it fails, M2 uses buffer
   heads for data, like ext3, without changing the on-disk format.
4. **Byte sharing through the standard interface** `remap_file_range` (`FICLONE`,
   `FIDEDUPERANGE`) with copy-on-write on shared extents. Automatic dedup on write
   is not part of M2; deduplication is explicit (standard ioctl) or offline (`mkfs` importing,
   scan tool). This preserves the M1 rule: only compared bytes are shared.
5. **Typed attributes of ADR-0005 exposed as xattr** `user.moob.*` via VFS handlers;
   identical xattr blocks shared by `mbcache`.
6. **Attribute index in RAM** rebuilt at mount (volume limited to 16 GiB and 65 536
   inodes); `QUERY` through ioctl on `.control` queries this index. A persistent on-disk index is
   a later decision, with measurement.
7. **New format with its own magic** (`MOOBFSW2`). M1 images are not mountable by the M2
   code nor converted in place; `mkfs.moobfs --import-m1` creates an M2 volume from an
   M1 image. The M1 module remains in the history; no binary compatibility is promised.
8. **Acceptance criterion with a real client**: mount an M2 volume, initialize a git
   repository, create a SQLite in WAL and point ai-memory's `data_dir` at it, running a
   captured session. Without this M2 is not considered complete.

## Alternatives considered

- Keep ioctl-only writes (original proposal): simpler to make durable, but useless
  for any existing client. Rejected; the idempotent `PUT`/`GET_REQUEST` interface
  may come back as an optimization on top of POSIX.
- Our own transactional log (native-mvp §6–7): well designed, but duplicates jbd2 and would require
  years of testing that jbd2 already has. Rejected as implementation; preserved as a document.
- Snapshots via dm-thin instead of revisions in the FS: evaluated for M3, not for M2.
- Rust for Linux: FS abstractions not yet stable in 6.12. Not adopted.

## Consequences

- M2 deliberately takes the form of a "small ext3" (bitmaps, inode table,
  linear directories, jbd2) plus three things ext3 does not have: shareable extents
  with copy-on-write, indexed typed attributes and `QUERY`. The project's justification comes to
  depend on measuring these three things, not on reinventing the rest.
- The shared parser in `common/` grows to cover bitmaps, inodes, directories, xattr and
  journal; fuzzing it becomes a prerequisite.
- xfstests, fsx, fsstress and dm-log-writes become the official harness; `make vm-test` comes to
  run them.
- The `native-mvp.md` proposal §5–7 is superseded by this ADR; §3 (data model) is
  refined by ADR-0005; the rest remains as historical reference.

# M2 specification — memory layer stacked over a mature filesystem

Status: **proposal** (2026-10-10). Nothing implemented or tested. Decisions in
[ADR-0005](decisions/0005-memory-model-bfs-reference.md) and
[ADR-0007](decisions/0007-stacked-memory-layer.md). The previous version of this specification
(full volume, ADR-0006) is preserved in
[spec-m2-full-volume-superseded.md](spec-m2-full-volume-superseded.md).

## 1. What it is

`moobfs` is a kernel module that mounts a **memory view** over a directory of a
mature filesystem:

```sh
mount -t moobfs -o lower=/srv/memory/store,index_budget=128M none /mnt/memory
```

Everything in `/mnt/memory` is a real file or directory in `/srv/memory/store`. The layer
adds: validation of typed attributes, an in-memory attribute index, query by
attribute (`QUERY`), capabilities of the lower filesystem exposed uniformly and, in the following phases,
memory identity and provenance.

Principles:

- **Nothing locked in.** Once MoobFS is unmounted, the lower directory is an ordinary set of files
  with xattrs; `grep`, `getfattr`, `git`, `rsync` and `duperemove` work on it without the layer.
- **Nothing reinvented.** Blocks, journal, directories, mmap, locks, reflink, checksums and
  snapshots belong to the lower filesystem. Git versions content. The layer has no on-disk format of its own
  beyond the encoding of xattr values.
- **Explicit degradation.** What the lower filesystem does not offer is reported, not simulated.

## 2. M2 scope

In: complete passthrough of POSIX operations to the lower filesystem (files, directories, rename,
unlink, truncate, mmap, fsync, locks, splice); typed attributes in `user.moob.*` validated on
write; in-RAM index and `QUERY` by ioctl; delegated reflink and dedup; `GET_INFO` with
capabilities; `moobfs-check` and `moobfs-index` tools; tests over btrfs, XFS and ext4.

Out: memory revisions and identity (M3), temporal query (M3), persistent index,
automatic dedup on write, any change to the lower filesystem outside the mounted directory, support for
more than one lower filesystem, NFS export, non-trivial user namespaces (follows what overlayfs
allows, evaluated case by case).

## 3. Data model

| Concept | Representation in the lower filesystem | Role of the layer |
| --- | --- | --- |
| Memory | Ordinary file | Exposes as a file; validates attributes |
| Collection | Ordinary directory | Passthrough |
| Typed attribute | xattr `user.moob.<name>` = `type (1 byte) + payload` | Validates type, size and name; indexes |
| Opaque attributes | Any other xattr | Passthrough without validation or index |
| Encoding version | xattr `user.moob.format` on the lower root directory, value `1` | Refuses to mount a higher version; migrates a lower version only with a tool |
| Shared content | Reflink of the lower filesystem (btrfs, XFS) | Delegated; `FIEMAP` passes through the lower filesystem's `SHARED` flag |

Types: `string` (valid UTF-8, up to 1 KiB), `int64`, `double`, `bool`, `time` (ns since epoch,
int64), `raw` (up to 4 KiB, not indexed). Name: `[a-z0-9_.]`, 1 to 64 bytes. Up to 64 typed
attributes per file. Limits are experiment ceilings, configurable at compile time.
`setxattr` with an invalid type, a size out of bounds or a name outside the alphabet returns `EINVAL`;
`user.moob.format` is reserved and returns `EPERM` from the layer. Creating the 65th typed attribute
returns `ENOSPC`. The lower filesystem may impose a smaller limit through the size of its xattr block
(ext4 with 1 KiB blocks holds about 36 small entries; with 4 KiB, more than 64), and
that `ENOSPC` propagates without being masked: the effective limit is the smaller of the two.

The layer does not impose a schema. Naming conventions (for example `kind`, `tier`, `agent`,
`session`, `created`, `expires`) live in a recommendation document for clients, not in the kernel.

## 4. Operations

| Operation | Behavior |
| --- | --- |
| lookup, readdir, create, mkdir, unlink, rmdir, rename, truncate, setattr | Passthrough to the lower filesystem with the credentials of the mounter (`override_creds`), as in overlayfs; permissions checked in the layer **and** in the lower filesystem |
| link, symlink | Passthrough if the lower filesystem supports it; mount option `nolinks` to refuse (`EPERM`), recommended for clients such as ai-memory, whose confinement rejects symlinks |
| read, write, mmap, splice, fsync, fallocate, locks | Delegated via `backing_file_*`; `fsync` calls the lower filesystem's `fsync`; durability semantics are those of the lower filesystem |
| `O_DIRECT` | Passthrough; if the lower filesystem refuses, propagates the error |
| getxattr, listxattr, removexattr | Passthrough |
| setxattr on `user.moob.*` | Validates type, size, name and UTF-8; writes to the lower filesystem; updates the index under the inode lock; a validation error returns `EINVAL` without touching the lower filesystem |
| setxattr on other namespaces | Passthrough |
| `FICLONE`, `FICLONERANGE`, `FIDEDUPERANGE` | `vfs_clone_file_range` / `vfs_dedupe_file_range_one` on the lower files; `EOPNOTSUPP` if the lower filesystem does not support it; the VFS has already compared bytes in dedup |
| `FIEMAP` | Passthrough |
| `QUERY` (ioctl on the mounted root directory) | Predicate of up to 4 terms `(attribute, operator ∈ {=, <, <=, >, >=, prefix}, typed value)` in conjunction, result limit; returns `(layer inode number, file handle)` for `open_by_handle_at`, plus the relative path when available by bounded reverse lookup |
| `GET_INFO` (ioctl) | UAPI version, encoding version, limits, index budget and usage, capabilities of the lower filesystem (`reflink`, `dedup`, `data_checksums`, `snapshots`, `xattr`), type of the lower filesystem |

`ro` mount: no write reaches the lower filesystem; the index is built normally.

## 5. Attribute index

- Built at mount by a scan of the lower directory (walk with `iterate_dir` and
  `vfs_getxattr` on the files). Cost proportional to the number of files; memory budget
  `index_budget` (default 128 MiB); if the budget is exceeded, the mount fails with `ENOMEM` and
  a clear message, or mounts without an index with `index=off` (`QUERY` returns `ENOSYS`).
- Structures: per attribute, `xarray` for integers/times/booleans and `rbtree` for strings and
  doubles, keyed by the encoded value; each entry points to the layer inode.
- Updated in `setxattr`, `removexattr`, `unlink`, `rename` (reverse lookup only) and
  `create`, within the same code path, under the inode lock. Not persisted: the next
  mount rebuilds it. This also absorbs modifications made to the lower filesystem from outside while
  unmounted.
- Modifying the lower filesystem **while mounted** is undefined, as in overlayfs; `moobfs-check`
  detects divergence between index and lower filesystem by a comparative scan.

> Note (2026-10-12, journal 0050): with the index off, `QUERY` now returns
> `EOPNOTSUPP` instead of `ENOSYS`; `ENOSYS` is reserved for a nonexistent system call.

## 6. Consistency and durability

- Everything the lower filesystem guarantees, the layer inherits; nothing beyond that. A successful `fsync` means
  what it means in the lower filesystem.
- Content and attributes are not written atomically together (they are distinct operations in the
  lower filesystem). Convention recommended to clients: write content to a temporary file, set
  attributes, `fsync` the file, `rename`, and `fsync` the parent directory. With this order, a
  file visible under the final name has complete content and attributes, and the name survives a
  power loss. Without the directory `fsync`, the `rename` only becomes durable at the next commit
  of the lower filesystem's journal (verified with `dm-log-writes` in phase 2e: the renamed file was not
  present at the mark written right after the `rename`).
- An I/O failure in the lower filesystem propagates to the caller; the layer does not mark state of its own.
- Power loss: the lower filesystem recovers; the layer rebuilds the index.

## 7. Tools (user space)

- `moobfs-check <lower>`: validates the encoding of all `user.moob.*`, format version,
  limits; JSON output; does not repair.
- `moobfs-index <lower>`: builds the index in user space with the same code from
  `common/` and answers queries; serves as an oracle to test the kernel `QUERY` and as a
  utility for those who do not want to mount.
- `moobfs-attr`: typed `get`/`set`/`list` on top of `getfattr`/`setfattr`, for scripts.
- Dedup: `duperemove` or the system's own `cp --reflink`; no tool of our own.
- Import of M1 images: `moobfs-import-m1 image.img <lower>` extracts files and, if the
  lower filesystem supports it, applies `FIDEDUPERANGE` to reproduce the sharing.

## 8. Invalid inputs that must be rejected

At mount: lower filesystem nonexistent, not a directory, is the mount point itself or contains
`user.moob.format` with a higher version; a lower filesystem without xattr support (`user.`) mounts only with
`index=off` and a warning. In `setxattr`: unknown type, payload of the wrong size for the type,
invalid UTF-8 in `string` (includes NUL, overlong sequences, surrogates and code
points above U+10FFFF), `double` NaN, `bool` outside 0/1, name outside the alphabet, reserved
name (`EPERM`), too many attributes (`ENOSPC`). In the scan: a malformed
`user.moob.*` xattr on an existing file is recorded (`moobfs-check` lists it) and the file
stays out of the index; the mount does not fail. In `QUERY`: invalid operator, value type
incompatible with the attribute, more than 4 terms, result limit out of range, structure with
reserved fields not zeroed. Each case has a test in the tools and in the module.

## 9. Tests and required evidence

1. `make test`: `common/` (encoding, predicates, user-space index), tools,
   sanitizers, fuzzing of the encoding and of the `QUERY` parser.
2. `make vm-test`, with evidence in `build/vm/`:
   - xfstests `generic` group over the moobfs mount with btrfs, XFS and ext4 as lower filesystem, following the
     model of `./check --fs overlay`; versioned and justified exclusion list;
   - `fsx` and `fsstress` over the mount;
   - `dm-log-writes` under the lower filesystem, with a `write`/`setxattr`/`fsync`/`rename` load, replay
     at each mark and verification that every file under the final name has complete attributes;
   - `dm-flakey` on the lower filesystem: errors propagate, index coherent after remount;
   - invalid inputs from section 8;
   - `QUERY` compared with `moobfs-index` (same queries, same results) and, for the
     operator semantics, with Haiku's `bfs_shell`;
   - reflink and dedup with `cp --reflink` and `duperemove` over btrfs and XFS; `EOPNOTSUPP` over ext4;
   - **acceptance with real clients that do not know MoobFS**: `git init`, commits and
     `git fsck` on the mount; SQLite in WAL with concurrent writer and reader and `PRAGMA
     integrity_check`; and a memory client of our own (a script that writes markdown pages via
     tmp + `rename` + `fsync`, sets typed attributes and queries via `QUERY`). Success is
     intact content and a clean `moobfs-check` after unmounting. No external agent memory
     project is part of the criterion: they come in M4 as systems to compare.
3. KASAN and `PROVE_LOCKING` via virtme-ng before any declared milestone.
4. M1 keeps running in `make vm-test` as a regression of the concluded experiment.

## 10. Compatibility

The layer has no on-disk format beyond the xattr encoding, versioned by
`user.moob.format`. Incompatible changes to the encoding require a new version, an offline migration
tool and an explicit decision. The ioctl UAPI has its own version; an unknown command
returns `ENOTTY`. M1 images remain readable by the M1 module preserved in the history and
importable by the tool of section 7.

## 11. Phases and exit criterion

| Phase | Deliverable | Exit |
| --- | --- | --- |
| 2a | Minimal passthrough (wrapfs): lookup, readdir, create, read/write via `backing_file_*`, mmap, fsync, over ext4 | **Validated on 2026-10-10** ([journal 0011](history/0011-phase-2a-stacked-passthrough.md)): `fsx` passes and the passthrough test passes in the VM; xfstests `generic` and KASAN are left for 2b |
| 2b | Complete passthrough (rename, unlink, truncate, locks, splice, opaque xattrs); btrfs and XFS as lower filesystems; delegated reflink | **Validated on 2026-10-10** ([journal 0012](history/0012-phase-2b-full-passthrough.md)): `FICLONE`/`FIDEDUPERANGE` work over btrfs and XFS; full xfstests and KASAN/lockdep still pending |
| 2c | Typed encoding, validation, `moobfs-check`, `moobfs-attr`, fuzzing | **Validated on 2026-10-10** ([journal 0013](history/0013-phase-2c-typed-attributes.md)): invalid inputs rejected in the kernel and by the tools; fuzzing in CI |
| 2d | In-RAM index, `QUERY`, `GET_INFO`, `moobfs-index` as oracle | **Validated on 2026-10-10** ([journal 0014](history/0014-phase-2d-index-query.md)): ten queries identical kernel/oracle on ext4, btrfs and XFS; `noindex` and `index_budget` exercised |
| 2e | Acceptance with git + SQLite + our own memory client; dm-log-writes; dm-flakey | **Validated on 2026-10-10** ([journal 0015](history/0015-phase-2e-investigation.md)): two defects of the layer found and fixed; full xfstests and KASAN/lockdep remain pending |

## 12. Open questions

- Reverse lookup to return paths in `QUERY`: keep an inode→dentry map in the layer or
  return only handles (proposal: handles always; paths as an optional extension).
- Behavior with a lower filesystem mounted by another user or with user namespaces (follow what
  overlayfs allows in 6.12 and document it).
- Whether `user.moob.*` should migrate to `trusted.moob.*` when the mount is by root, to prevent
  unprivileged processes from changing attributes outside the layer.
- Attribute naming conventions for clients: a recommendation document to be written together
  with phase 2c, using ai-memory's front matter as the first case.

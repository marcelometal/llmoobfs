# 0014 — Phase 2d: in-memory index and query by attribute (`QUERY`)

Date: 2026-10-10, America/Sao_Paulo. Status: implemented and validated in the test VM.

## Request

Continue to phase 2d of the M2 specification: in-RAM index built at mount,
updated on write operations, `QUERY` and `GET_INFO` through ioctl, and a user-space
oracle to compare answers.

## Implementation

- `include/moobfs_ioctl.h`: UAPI version 1. `MOOB_IOC_QUERY` receives up to 4 terms
  `(name, operator ∈ {eq, lt, le, gt, ge, prefix, exists}, type, value)` in conjunction, a
  limit of up to 4 096 results and a pointer to the output array; it returns
  `(inode, path relative to the mount, flags)` per hit, with `truncated` when there are more.
  `MOOB_IOC_GET_INFO` returns versions, limits, capabilities announced by the lower's type
  (reflink on btrfs/XFS, checksums and snapshots on btrfs, xattr), flags (`nolinks`, index
  disabled, stale paths, incomplete index) and budget usage. Reserved fields
  must be zero; an unknown command returns `ENOTTY`; the ioctls only answer on the mount's root
  directory.
- `layer/index.c` (new, ~640 lines) and `layer/moobl.h`: per attribute name, an rbtree of
  entries sorted by `(type, value, inode)`; per inode, a record with the list of its
  attributes, the path recorded at the scan or at the last write and a pointer to the layer
  inode when it is in cache. Memory budget accounted per allocation (default
  128 MiB, option `index_budget=`; `noindex` disables the index and `QUERY` answers `ENOSYS`).
  Scan at mount: iterative walk of the lower with `iterate_dir` and
  `lookup_one_len_unlocked`, reading `user.moob.*` of files and directories with the
  mounter's credentials; invalid encodings and `raw` do not enter the index. Updates: `setxattr`
  (replaces the entry and records the path), `removexattr`, `unlink` when the last link drops,
  `rmdir`, `rename` (file: new path; directory: marks paths as stale and switches to
  resolving through the dentry cache). Query: ordered scan of the first term's entries
  under a read lock, checking the other terms through the inode's list. `double` sorts by
  a monotonic IEEE 754 key; NaN is refused. A memory failure when updating the index after
  a successful write to the lower does not undo the write: it marks the index as incomplete and
  `GET_INFO` exposes this.
- `tools/moobfs-index.c` + `tools/query-common.h`: oracle that walks the lower directory
  and answers the same queries through the xattr API; `tests/query.c`: ioctl client that
  prints in the same format, plus `info` and `invalid` (thirteen malformed requests that must
  return `EINVAL`, unknown command `ENOTTY`, ioctl outside the root `ENOTTY`).
- `vm/init`: on each lower, builds a tree of six inodes with attributes of all the indexable
  types and compares kernel with oracle in ten queries (equality, conjunction, prefix,
  range, empty, existence, boolean, negative `double`, attribute on a directory, non-indexed
  `raw`); checks `GET_INFO`; exercises rename of a file and of a directory, unlink and
  removexattr; unmounts and remounts (index rebuilt by the scan), mounts with `noindex` and
  with `index_budget=1M`.

## Evidence executed

`make vm-test`, Debian kernel 6.12.111, first run of phase 2d without fixes to the module:

```text
LAYER_LOWER_PASS: ext4 reflink=no
LAYER_LOWER_PASS: btrfs reflink=yes
LAYER_LOWER_PASS: xfs reflink=yes
MOOBFS_LAYER_PASS: … index and QUERY equal to the oracle …
```

No `BUG`, `Oops` or `WARNING`. All kernel queries were identical, byte by byte, to
those of the user-space oracle, on the three lower filesystems, before and after renaming, removing and
remounting. Only fix during the session: a missing `#include` in the test client.

Result for the project: for the first time a Linux mount answers "which files have
`kind = decision` and `score ≥ 40`" through a call to the kernel, without a database beside it. It is
item 1 of ADR-0005 working over btrfs, XFS and ext4 without its own on-disk format.

## Limitations

- Non-persistent index: the mount walks the entire lower; cost proportional to the number
  of inodes, not yet measured for large trees.
- Query by linear scan of the first term's entries; no use of ranges in the
  rbtree yet. Sufficient for M2; measure in M4.
- Paths: after renaming a directory, descendants' paths depend on the dentry
  cache; without cache the response carries only the inode and the `PATH_UNKNOWN` flag.
- Modifying the lower from outside while mounted does not update the index (overlayfs rule).
- Same earlier open items: KASAN/lockdep, full xfstests, dm-log-writes, security
  review of the credentials.

## Next step

Phase 2e: acceptance with a real client (git, SQLite in WAL and ai-memory over the mount),
`dm-log-writes` and `dm-flakey`. Before that, decide how to bring static git and SQLite into the
VM (Debian packages in the container, extracted into the initramfs).

## Addendum (2026-10-10): CI and the phase 2e criterion

CI of commit `0fd38a5` (run 38067281430): tools, sanitizers, fuzzing and native Linux VM
successful. The user decided that ai-memory does not enter the M2 exit criterion: phase 2e
uses git, SQLite and a memory client of our own; external systems are left for the M4
benchmark. Specification and ADR-0007 annotated.

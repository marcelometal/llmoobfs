# Architecture overview

How the MoobFS memory layer is built, in English, with a pointer to the source file or
document behind each statement. This page describes the code on `main` as of 2026-10-10
(M2 phases 2a to 2e and M3 phase 3b). It is a description of an **experimental research
module**, not a compatibility promise: the UAPI is version 1 and may change, and the module
has been tested on one kernel series only (Debian 6.12.111 in QEMU; upstream 6.12.112 under
KASAN/UBSAN/lockdep). The specifications are [spec-m2.md](spec-m2.md) and
[spec-m3.md](spec-m3.md); the design decision is [ADR-0007](decisions/0007-stacked-memory-layer.md).

## 1. A stacked layer, not a volume

`moobfs_layer.ko` is a native kernel module registered as the filesystem type `moobfs_layer`.
It is mounted over a directory of an existing, mature filesystem (the "lower"):

```sh
mount -t moobfs_layer -o lower=/srv/memory[,nolinks][,noindex][,index_budget=<bytes>] none /mnt/memory
```

Every file and directory visible under the mount is a real file or directory in the lower
tree. The layer owns no blocks, no journal and no on-disk format of its own beyond the
encoding of attribute values (`docs/decisions/0007-stacked-memory-layer.md`, items 1 to 3).
The same family of designs is used in-tree by overlayfs and ecryptfs. There is no FUSE and no
user-space daemon ([ADR-0001](decisions/0001-native-filesystem.md)).

Mount contract (`layer/main.c`, `moobl_fill_super` and `moobl_parse_param`):

| Rule | Where |
| --- | --- |
| `lower=` is required and must resolve to a directory | `invalfc(fc, "lower= is required")`, `kern_path(..., LOOKUP_DIRECTORY, ...)` |
| The lower cannot itself be a `moobfs_layer` mount; the kernel's stacking depth limit applies | `s_magic == MOOBL_SUPER_MAGIC` check; `s_stack_depth` |
| Mounting requires `CAP_SYS_ADMIN` in the initial user namespace; the type does not set `FS_USERNS_MOUNT`, so a mount from an unprivileged user namespace fails with `EPERM` | `moobl_fs_type`; tested by `tests/idmap.c userns` |
| Idmapped mounts are refused with `EINVAL` because the type does not set `FS_ALLOW_IDMAP` | `moobl_fs_type`; tested by `tests/idmap.c idmap` |
| `nolinks` refuses `link` and `symlink` creation | `moobl_link`, `moobl_symlink` |
| `noindex` disables the index; `QUERY` and `QUERY2` then return `EOPNOTSUPP` (`ENOSYS` until journal 0050) | `layer/index.c` (`if (!ix->enabled) return -EOPNOTSUPP`) |
| `index_budget` accepts 1M to 4G; the default is 128 MiB | `MOOBL_INDEX_BUDGET_DEFAULT` in `layer/moobl.h`; range check in `moobl_parse_param` |

Modifying the lower tree from outside while the layer is mounted is undefined, as in
overlayfs; changes made while unmounted are picked up by the index rebuild at the next mount
([spec-m2.md](spec-m2.md) §5).

## 2. Dentry and inode mapping

Each layer dentry carries the lower `struct path` in `d_fsdata` (`struct moobl_dentry`,
`layer/moobl.h`). Each layer inode embeds a pointer to the lower inode (`struct moobl_inode`).
`moobl_lookup` resolves the name in the lower directory with `lookup_one_len_unlocked`, then
`moobl_get_inode` obtains one layer inode per lower inode through `iget5_locked`, keyed by the
lower inode pointer. The layer inode reuses the lower inode number (`inode->i_ino =
lower->i_ino`, `layer/main.c`), so `stat` on the mount and on the lower tree report the same
inode numbers; the pass-through test checks this (`tests/layer.c`).

File I/O is delegated through the kernel's backing-file helpers from `fs/backing-file.c`:
`backing_file_open` in `moobl_open`, `backing_file_read_iter`, `backing_file_write_iter`,
`backing_file_splice_read`, `backing_file_splice_write` and `backing_file_mmap`
(`layer/main.c`). Delegated operations on the lower tree run under the mounter's credentials
with `override_creds(MOOBL_SB(sb)->creds)`, as overlayfs does. Permission decisions are not
re-implemented: `moobl_permission` calls `inode_permission` on the lower inode, which runs
the lower filesystem's full check including POSIX ACLs. ACLs are read and written through
`moobl_get_acl`/`moobl_set_acl`, which delegate to `vfs_get_acl`, `vfs_set_acl` and
`vfs_remove_acl` on the lower dentry; `SB_POSIXACL` is inherited from the lower superblock
(`layer/main.c`; the defect that motivated this is recorded in
[journal 0020](history/0020-phase-3b-query-authorization.md)).

`FICLONE`, `FIDEDUPERANGE` and `copy_file_range` are forwarded to the lower files
(`vfs_clone_file_range`, `vfs_dedupe_file_range_one`); they share extents on btrfs and XFS
and fail cleanly on ext4. Reflink and deduplication are therefore properties of the lower
filesystem, not of MoobFS ([journal 0012](history/0012-phase-2b-full-passthrough.md)).

## 3. Typed attributes

A memory's metadata are extended attributes named `user.moob.<name>` on the lower filesystem.
The value is one type byte followed by the payload (`include/moobfs_attr.h`; shared encoder
and validator in `common/attr.c`, compiled into both the module and the tools):

| Type | Payload | Indexed |
| --- | --- | --- |
| `MOOB_T_STRING` (1) | valid UTF-8 without NUL, up to `MOOB_ATTR_STRING_MAX` = 1024 bytes | yes |
| `MOOB_T_INT64` (2) | 8 bytes, little-endian two's complement | yes |
| `MOOB_T_DOUBLE` (3) | 8 bytes, IEEE 754 binary64 little-endian, not NaN | yes |
| `MOOB_T_BOOL` (4) | 1 byte, 0 or 1 | yes |
| `MOOB_T_TIME` (5) | 8 bytes, signed nanoseconds since the epoch | yes |
| `MOOB_T_RAW` (6) | up to `MOOB_ATTR_RAW_MAX` = 4096 opaque bytes | no |

Limits from the same header: the name after the prefix is 1 to `MOOB_ATTR_NAME_MAX` = 64 bytes
in `[a-z0-9_.]` without leading or trailing dots; at most `MOOB_ATTR_MAX_PER_FILE` = 64 typed
attributes per file; the lower root carries `user.moob.format` with the encoding version
(`MOOB_ATTR_FORMAT_VERSION` = 1). `moobl_xattr_set` in `layer/main.c` validates the name and
value before passing the xattr to the lower filesystem; an invalid encoding is refused with
`EINVAL`. Other xattr namespaces pass through unchanged. The user-space tools `moobfs-attr`
(set and get), `moobfs-check` (walk a tree and report invalid encodings and identity
conflicts) and `moobfs-index` (oracle for `QUERY`) live in `tools/`; the encoder is fuzzed by
`tests/fuzz_attr.c` ([journal 0013](history/0013-phase-2c-typed-attributes.md)).

## 4. In-memory index

The index (`layer/index.c`, `struct moobl_index` in `layer/moobl.h`) is built at mount by
walking the lower tree and reading every `user.moob.*` attribute, kept current by the xattr,
unlink, rmdir and rename paths of `main.c`, and never written to disk. Structures:

- an rbtree of attribute names (`struct moobl_ix_name`), one node per distinct name;
- under each name, an rbtree of entries (`struct moobl_ix_entry`) ordered by
  `(type, value, ino)`, so a range or prefix predicate is a tree walk;
- an xarray from inode number to `struct moobl_ix_inode`, which holds the inode's list of
  entries, the mount-relative path recorded at scan or write time and a pointer to the cached
  layer inode when one exists.

Memory is accounted against `index_budget` (`charge()` returns `ENOMEM` when `used + bytes >
budget`). If the budget is exceeded while building at mount, the mount fails; if an index
update fails after a lower write has already succeeded, the layer sets `incomplete`, logs one
warning and reports `MOOB_FLAG_INDEX_INCOMPLETE` in `GET_INFO` until remount. Renaming a
directory sets `paths_stale`, because descendants keep their recorded paths; `QUERY` then
resolves those entries through the dentry cache and reports `MOOB_FLAG_PATHS_STALE`. Rebuild
cost is linear in the number of files; it has not been measured (M4,
[benchmark-plan.md](benchmark-plan.md)). Behaviour near a saturated budget on large trees is
not tested ([testing.md](testing.md) §3).

## 5. `QUERY` and `GET_INFO` (UAPI version 1)

Both ioctls are issued on an open descriptor of the mount root (`include/moobfs_ioctl.h`;
any other directory returns `ENOTTY`, `tests/query.c invalid`).

`MOOB_IOC_QUERY` takes `struct moob_query`: `version` = 1, 1 to `MOOB_QUERY_MAX_TERMS` = 4
terms joined by AND, `limit` from 1 to `MOOB_QUERY_MAX_LIMIT` = 4096, `flags` and `reserved`
zero. Each term names an attribute (without the prefix), an operator (`EQ`, `LT`, `LE`, `GT`,
`GE`, `PREFIX` for strings, `EXISTS` ignoring the value) and a typed value. There is no OR,
no sorting, no aggregation, no full-text and no vector search. Results are
`struct moob_query_result` records with the inode number, a mount-relative path of at most
`MOOB_QUERY_PATH_MAX` = 256 bytes and flags `MOOB_RES_PATH_UNKNOWN` or
`MOOB_RES_PATH_TRUNCATED`; the output fields are `count` and `truncated`. Malformed requests
(wrong version, too many terms, zero limit, unknown flags, nonzero reserved bytes, bad name,
unknown operator, operator or type mismatch, wrong value length, invalid UTF-8) return
`EINVAL`.

Execution (`do_query` in `layer/index.c`): the first term selects the candidate set from its
name's rbtree, candidates are examined in batches of `MOOBL_QUERY_CHUNK` = 256 with the index
read lock held, the remaining terms are checked per candidate, and the lock is released before
authorization so that the layer never sleeps with it. Resumption between batches uses the
ordered key `(type, value, ino)` (`first_after`). `limit` bounds the results copied, not the
candidates examined. Kernel answers are compared with the `moobfs-index` oracle in the VM for
ten predicates and after rename, directory rename, unlink, removexattr and remount, over ext4,
btrfs and XFS ([journal 0014](history/0014-phase-2d-index-query.md)).

Authorization per result (`caller_may_read`, phase 3b): for each candidate the recorded path
is resolved with `vfs_path_lookup` from the caller's own `file->f_path`, hence with the
caller's credentials; the resolved object must belong to this mount and be the indexed inode,
and `inode_permission(MAY_READ)` must succeed. Entries without a verifiable path are omitted.
Unauthorized candidates are omitted silently and never counted in `count` or `truncated`
(their scan time is still observable: about 1.15 µs per hidden candidate, journal 0030);
operational failures (`ENOMEM`, `EFAULT`) return an error. The contract, from
[spec-m3.md](spec-m3.md) §3.1: authorization holds at the instant of the query; a result is
not a grant of access; the client calls `open()` normally and must accept `EACCES` or `ENOENT`
if permissions or names changed afterwards; UAPI v1 promises no absence of a
time-of-check/time-of-use window; after a directory rename, entries without a cached dentry
may be missing from answers until the next mount (a documented false negative, not a leak).
Evidence: two uids, two namespaces, mode bits and POSIX ACLs, remount, a five-second race of
`chmod`/`rename`/`unlink` against `QUERY`, over the three lower filesystems, in the Debian VM
and under KASAN on 6.12.112 ([journal 0020](history/0020-phase-3b-query-authorization.md)).
No external audit and no timing-channel analysis exist.

`MOOB_IOC_GET_INFO` returns `struct moob_info`: UAPI and format versions, the attribute
limits above, capabilities advertised from the lower filesystem type (`MOOB_CAP_REFLINK`,
`MOOB_CAP_DATA_CHECKSUMS`, `MOOB_CAP_SNAPSHOTS`, `MOOB_CAP_XATTR`), the flags `NOLINKS`,
`INDEX_OFF`, `PATHS_STALE`, `INDEX_INCOMPLETE`, the index budget, bytes used, entry and inode
counts, and the lower filesystem type name.

## 6. What is delegated and what the layer adds

| Delegated to the lower filesystem and the VFS | Added by the layer |
| --- | --- |
| Blocks, directories, journaling, crash consistency, `fsync` semantics | Validation of `user.moob.*` on write |
| Mode bits, ownership, POSIX ACLs, the permission decision itself | The in-memory attribute index |
| Reflink, deduplication, data checksums, snapshots (as capabilities) | `QUERY` with per-result authorization, `GET_INFO` |
| Page cache, mmap, locks | Mount options `nolinks`, `noindex`, `index_budget` |

Atomicity between a file's content and its attributes is the lower filesystem's; there is no
transaction covering both. The client convention used in the tests is: write a temporary file,
set the attributes, `fsync`, `rename`, `fsync` the directory (`remember()` in `vm/suite.sh`,
[spec-m2.md](spec-m2.md) §6). Power-loss replays with `dm-log-writes` showed that memories
synced before a mark survive and later ones are absent
([journal 0015](history/0015-phase-2e-investigation.md)).

## 7. Identity convention (M3 phase 3a)

Identity is a client convention, not a kernel feature: the logical key is the pair of string
attributes `user.moob.namespace` and `user.moob.memory_id`; `user.moob.agent` records
authorship and is not part of the key; provenance of a copied memory goes in
`user.moob.origin_namespace` and `user.moob.origin_id` ([spec-m3.md](spec-m3.md) §2). The
kernel does not generate identifiers and does not interpret these attributes. The only
implemented check is in `tools/moobfs-check.c`: two distinct inodes with the same
`(namespace, memory_id)` are reported as a conflict; several paths to the same inode (hard
links) are not; the same id in another namespace is counted as normal. The convention is written
end to end, for any client, in [client-contract.md](client-contract.md) (journal 0070).

## 8. Nothing trapped

Because content is ordinary files and metadata are real xattrs with a documented encoding,
unmounting the layer leaves a plain directory tree: `grep`, `getfattr`, `git`, `rsync` and
backup tools read it without the module, and `moobfs-check` validates it from user space
([ADR-0007](decisions/0007-stacked-memory-layer.md) item 3). The acceptance tests run git and
WAL-mode SQLite unchanged on the mount and compare `fsx` results against the lower copy
([testing.md](testing.md) §3). The index is the only state that exists solely in the layer,
and it is rebuilt from the xattrs at every mount.

## 9. What this page does not claim

No performance, latency or memory figure exists (M4 pending); no kernel beyond the two above
has been tried; xfstests has not been run (only the vendored `fsx`); the code is one author's,
with three kernel-side defects found by its own tests ([0015](history/0015-phase-2e-investigation.md),
[0016](history/0016-kasan-lockdep-harness.md)) and no external review. See
[when-not-to-use.md](when-not-to-use.md) and [faq.md](faq.md).

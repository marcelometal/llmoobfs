# 0020 — Phase 3b: POSIX authorization in `QUERY` (in progress)

Date: 2026-10-10, America/Sao_Paulo. Status: **in progress**. Code on the branch
`m3/3b-query-authz`; this journal exists so the next session can resume without loss.

## Decisions (second round of the external review, accepted)

- Per-candidate authorization with the VFS mechanisms, without a cache of our own: resolve the
  recorded path with `vfs_path_lookup` under the caller's credentials, require that the object
  belong to this mount (`p.mnt` and `d_sb`) and have the indexed inode, apply
  `inode_permission(MAY_READ)`, emit only what is authorized; `count`, `limit` and `truncated`
  count only visible results.
- The result limit does not limit the candidates examined: scan in batches of 256 with
  resumption by the ordered key `(type, value, inode)`, read lock released during the
  resolutions; memory bounded per batch.
- Operational failures (`ENOMEM`, `EFAULT`) return an error; only authorization failures omit.
- TOCTOU contract in UAPI v1: the authorization holds at the instant of the query; the client
  does `open()` normally and accepts `EACCES` or `ENOENT`; no special handles.
- Stale paths after renaming a directory: try a dentry alias, confirm identity, authorize
  through the VFS, omit if there is no verifiable path; no recursive update before measuring;
  false negative documented as a limitation.
- Discarded for now: a per-directory `MAY_EXEC` cache, precomputing directories traversable by
  everyone, checking only `MAY_READ`, authorization by alias only.
- SQLite baselines: database on the same filesystem as the files; W3 now has fixed query shapes
  (not arbitrary combinations), each covered by a composite index with equality before range;
  simple indexes redundant with prefixes of composites are removed; string prefix as a binary
  range with `COLLATE BINARY`; existence as `IS NOT NULL`; `-0.0`/`0.0` avoided by the
  generator; `EXPLAIN QUERY PLAN` gate after `ANALYZE` recording the index and predicates used,
  with documented `SCAN` exceptions; `sqlite3_stmt_scanstatus_v2` only if the SQLite build has
  the option. Carried into `docs/benchmark-harness.md` §4.1; TOCTOU contract and errors in
  `docs/spec-m3.md` §3.1.

## Implemented on the branch

- `layer/index.c`: `do_query` rewritten (`caller_may_read`, `first_after`, batches of 256,
  resumption by key, `ENOMEM` propagated); the ioctl receives the caller's `file`.
- `tests/query.c` and `tools/moobfs-index.c`: `--uid N` option to run as another user.
- `vm/suite.sh`: isolation test with two uids and two namespaces, shared namespace, attribute
  reassignment does not grant access, `chmod` grants immediately, remount, and 600 private
  candidates with 6 authorized (more than one batch); mount root at 0755 because the ioctl
  requires an open descriptor of the root.

## Rounds

1. First version (cap of 4×limit candidates): failed at `iso-query-1001-ext4` because the root
   was at 0711 and the unprivileged client could not open the directory. Fixed.
2. Batched version with resumption: failed at the same point; cause in the harness, the
   initramfs root is born 0700 (inherited from `mktemp -d`) and unprivileged users cannot
   traverse `/`. Fixed in `vm/init` (`chmod 0755 /`, `/tmp` 1777). New run to be attached.

## Pending items to close the phase

Debian harness result; KASAN on the branch; POSIX ACLs in the test (`acl` package in the
image); test of `chmod`/`rename`/`unlink` concurrent with queries; note in `docs/spec-m3.md`
the TOCTOU contract and the error semantics; update `docs/benchmark-harness.md` with the fixed
W3 shapes and the indexes; final journal.
(Update 2026-10-10: everything done except KASAN on the branch, running; see the evidence
sections below.)

## Third round of the external review: accepted decisions and code evidence

**Identity (phase 3a).** Logical key `(namespace, memory_id)`; `agent` is authorship, not part
of the key. The same `memory_id` in different namespaces is two memories; equal content is not
equal identity; reflink is physical sharing, not logical; a hard link is the same inode and the
same xattrs, so it does not serve for two identities. Provenance by client convention:
`user.moob.origin_namespace` and `user.moob.origin_id` (strings). The kernel does not generate
ids nor interpret provenance. `moobfs-check`: normal (same id in distinct namespaces), warning
(same id, identical content, no provenance), error (two inodes with the same
`(namespace, memory_id)`); hard links of the same inode are not duplication; content
comparison optional. No change in the kernel. Pending: formalize in `docs/spec-m3.md` §2 and
implement in the checker with the scenarios listed by the review (copy, reflink, hard link,
missing or invalid provenance, rename, remount).

**Authorization (phase 3b), current evidence.** `moobl_permission` (`layer/main.c:699`)
delegates to `inode_permission(&nop_mnt_idmap, lower_inode, mask)`: this runs the lower's
entire check, including POSIX ACLs via the lower's `generic_permission` and
`security_inode_permission` on the lower inode; the VFS has already called
`security_inode_permission` on the upper inode before reaching the layer. There is no second
DAC check over the upper inode's metadata. `caller_may_read` (`layer/index.c`) uses
`vfs_path_lookup` with the caller's `file->f_path.mnt` and `inode_permission(MAY_READ)` on the
upper inode, which goes through the same `moobl_permission`; therefore the `QUERY` decision is
the same as that of an `open()` through the layer. Known divergence to document: both use
`nop_mnt_idmap`, correct only because the layer does not admit idmapped mounts.

**Idmapped mounts and user namespaces.** `moobl_fs_type` declares neither `FS_ALLOW_IDMAP` nor
`FS_USERNS_MOUNT` (`layer/main.c`): in 6.12, `mount_setattr` with idmapping over a filesystem
without `FS_ALLOW_IDMAP` is rejected by the VFS with `EINVAL`, and mounting without
`FS_USERNS_MOUNT` requires `CAP_SYS_ADMIN` in the initial user namespace, with `EPERM`
otherwise. Decision: do not add a redundant check in the module; document it as a contract and
prove it in the VM (`mount_setattr` must fail with `EINVAL`; `unshare -Ur` + mount must fail
with `EPERM`). Pending: `acl` package in the image, ACL tests (bits deny/ACL grants, bits
grant/ACL denies, traversal by ACL, ACL changed after indexing and during queries), and the
idmap/userns tests in `vm/suite.sh`.

Local run of phase 3b with the initramfs root at 0755 (`make vm-test`, Debian kernel
6.12.111): `ISOLATION_PASS` on ext4, btrfs and XFS, including the scenario of 600 private
candidates with 6 authorized; the whole regression passed (`MOOBFS_ACCEPTANCE_PASS`,
`MOOBFS_LAYER_PASS`), without kernel warnings. Pending to close the phase: ACLs, idmap/userns
in the suite, KASAN on the branch, and merge into `main` by pull request.


## Finding of the third round: POSIX ACLs did not cross the layer

The ACL test requested by the review failed immediately: `setfacl` through the layer returned
"Operation not supported". Cause: in 6.12 the VFS handles `system.posix_acl_access/default`
through the inode's `get_acl`/`set_acl` operations (and the superblock's `SB_POSIXACL` flag),
not through the xattr handlers; the layer only had the generic handler. The earlier claim that
"ACLs reach the lower through xattr passthrough" was wrong and the review was right to demand
proof. Fix: `moobl_get_acl`/`moobl_set_acl` delegating to `vfs_get_acl`/`vfs_set_acl`/
`vfs_remove_acl` on the lower dentry, like ecryptfs, and `SB_POSIXACL` inherited from the
lower. The authorization decision remains delegated to the lower; only writing and reading
ACLs through the layer started to work.

## Final evidence of the phase in the VM (2026-10-10)

`make vm-test` on the branch `m3/3b-query-authz` (commit `fb8f9a1`, Debian kernel 6.12.111),
after the handshake in the user namespace test: `ISOLATION_PASS` on ext4, btrfs and XFS, each
preceded by `IDMAP_REFUSED_EINVAL` and `USERNS_MOUNT_REFUSED_EPERM`; the ACL tests (bits deny
and ACL grants, bits grant and ACL denies, traversal by ACL, ACL changed after the index) and
identity tests (`moobfs-check` with a duplicate on a distinct inode, hard link accepted, same id
in another namespace accepted) passed within the same block; `MOOBFS_ACCEPTANCE_PASS` and
`MOOBFS_LAYER_PASS` without kernel warnings. KASAN on the branch and the merge into `main` by
pull request remain pending.

## Concurrency between permission, name and inode changes and `QUERY` (2026-10-10)

New test in `tests/query.c` (`query --uid N <mount> race <seconds>`) and a corresponding block
in `vm/suite.sh`: for 5 s, root alternates `chmod 0600/0644`, renames and recreates each of
40 files and, every seven rounds, renames the whole directory, while uid 1001 repeats the
`QUERY`. Invariants checked on every response: well-formed result (path zero-terminated,
relative, without `..`), count within the limit, and each path opens with the caller's
credentials or fails only with `ENOENT` (removed or renamed after the response) or `EACCES`
(`chmod` after the response), which is exactly the TOCTOU window accepted in the contract of
`docs/spec-m3.md` §3.1; paths marked `MOOB_RES_PATH_UNKNOWN` (directory renamed) are ignored.
At the end, `chmod 0644` on everything and the `QUERY` must equal the oracle.

Result (`make vm-test`, Debian kernel 6.12.111, `build/vm-test-race.log`):

| Lower | queries in 5 s | results opened | `ENOENT` after the response | `EACCES` after the response |
| --- | --- | --- | --- | --- |
| ext4 | 626 | 27624 | 678 | 35 |
| btrfs | 616 | 27513 | 428 | 17 |
| xfs | 618 | 27447 | 493 | 26 |

No malformed result, no error other than the two expected ones, final response equal to the
oracle on all three, no kernel warnings; `MOOBFS_LAYER_PASS` and `MOOBFS_ACCEPTANCE_PASS`. The
test is about robustness and contract, not performance: the VM runs under emulation (TCG).

## KASAN on the branch (2026-10-10)

Run 38076335715: failed at `acl-set-ext4` because of the environment (workflow without the
`acl` package, without `setfacl` in the virtme-ng guest and without the `idmap` binary); fixed
in `.github/workflows/kasan.yml` and `vm/kasan-guest.sh`. Run 38077926732: ACLs passed; failed
at `contract-idmap-ext4` because the KASAN kernel did not enable `CONFIG_USER_NS`, so
`unshare(CLONE_NEWUSER)` failed in the test. Fix: `CONFIG_NAMESPACES`, `CONFIG_USER_NS` and
`CONFIG_FS_POSIX_ACL` in `vm/kasan.config`. Run 38079499011 (kernel 6.12.112 with KASAN, UBSAN
and lockdep): `MOOBFS_KASAN_PASS`, `ISOLATION_PASS` on ext4, btrfs and xfs with
`IDMAP_REFUSED_EINVAL` and `USERNS_MOUNT_REFUSED_EPERM`, concurrency test with 41 to 75 queries
in 5 s (instrumented kernel is slower), without any kernel diagnostic. Phase 3b is closed as
far as code and tests are concerned; the merge into `main` by pull request remains.

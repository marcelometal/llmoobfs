# 0035 — Phase Q2a: per-domain `QUERY` (UAPI v2), partitions and homogeneity rule

Date: 2026-10-11. Status: implemented and tested in the suite's QEMU VM (Debian kernel 6.12.111,
ext4, btrfs and XFS); performance measurement and timing channel test **not executed** (Azure
host shut down; next host: local Mac mini i5, to be configured by the maintainer).

## Goal

First phase of the specification `docs/spec-query-v2.md` (ADR-0009, alternative E): an in-RAM
index partitioned by permission domain, homogeneity rule, `MOOB_IOC_QUERY2` without
`order_by`, and the v1 restriction.

## Changes

- **Mount:** option `domain_depth=N` (0 to 8, default 1; 0 turns domains off).
  `show_options` only shows it when it differs from 1.
- **UAPI (`include/moobfs_ioctl.h`):** `MOOB_IOC_QUERY2` (`_IOWR(.., 3, struct moob_query2)`),
  version 2, with `order_by`, `order`, `cursor` and `reserved` already in the layout and required to be zero
  in this phase. `struct moob_info` uses part of the reserved space for `domain_depth`,
  `heterogeneous` and `fallback_domains`, without changing the size; new flag `MOOB_FLAG_DOMAINS`.
- **Index (`layer/index.c`):** the name tree is now ordered by (name, domain,
  heterogeneous); each inode record stores its domain and whether it is heterogeneous, and the entries
  change partition when that changes. The mount scan carries depth, domain and the
  result of the rule through the intermediate directories.
- **Homogeneity rule:** as in the specification, with three complements recorded there
  (section "Implementação Q2a"): intermediate directories evaluated by the execute bit; a file
  with `nlink > 1` is heterogeneous; extended ACL detected by `get_inode_acl` with more than three
  entries.
- **Reevaluation:** `setattr` (mode, owner, group), access `set_acl`, `link` and `rename`
  recompute the inode's partition. A permission change on a directory at the domain depth
  or below it puts the domain in **fallback** (per-result check, as in
  v1) until the next mount.
- **`QUERY2`:** on the descriptor of the domain directory; checks `MAY_EXEC | MAY_READ` once
  and returns the homogeneous partition without checking each result (except in fallback); paths
  relative to the domain. Errors: `ENOSYS` without an index, `EOPNOTSUPP` with `domain_depth=0`, `ENOTTY`
  outside a domain directory, `EACCES` without permission, `EINVAL` for invalid or
  not yet supported fields.
- **v1:** with domains on, `EPERM` without `CAP_SYS_ADMIN`; with `domain_depth=0`, unchanged.
- **Renaming:** a directory that would change domain or depth gets `EXDEV` before
  any lock; a file changes partition in the same call.
- **Tools:** `moobfs-index --domain` (rule reimplemented independently, from
  `stat` and `getxattr`); `tests/query.c` with `q2`, `q2errno`, `q2invalid` and `rename`;
  `bench-load --query2 1` (per-domain W3) and `--mount-opts` (W6/W7); `bench/run.sh` with
  `LAYER_MOUNT_OPTS` (default `domain_depth=0`, to keep the v1 campaigns comparable);
  `bench/timing-v2.sh` for test 4 of the specification.
- **Suite:** the existing mounts now use `domain_depth=0` (the v1 isolation tests
  run as an ordinary user); new `QUERY2_PASS` block per lower (see `docs/testing.md`).

## Errors found and fixed

1. First suite run: `oops` in `depth_and_domain` in the `EXDEV` test for a depth
   change. The `rename` target (negative dentry) was exactly at the domain depth,
   and the code read its inode. Fix: a negative dentry has no domain.
2. Review of the `rename` path: `moobl_index_renamed` was called with the source dentry,
   but the VFS only moves the dentry after `->rename` returns; the partition would be computed at the
   old location. Fix: the moved inode is placed by the target dentry (also for
   `RENAME_EXCHANGE`); the path recorded in the index is now the new one, which also fixes
   the path stored by v1 after renaming a file.
3. The timing channel test script would create the domain directories with the layer mounted,
   and their `chown`/`chmod` would put the domains in fallback; the script remounts before measuring.

## Evidence

`make vm-test` (disposable QEMU VM, Docker): `QUERY2_PASS` on ext4, btrfs and XFS, followed by
all the previous markers (`ISOLATION_PASS`, `EQUIV_PASS`, `BENCH_LAYER_PASS`,
`LAYER_LOWER_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`). KASAN: to be run by
CI on the pull request.

Specification tests covered in this phase: 1 (v2 fields that exist in this phase), 2 (form
`kind eq` against the per-domain oracle, on the three lowers), 3, 5, 6 (already brought forward from Q2e) and
9 in part (v1 restricted; KASAN pending from CI). **Not executed:** 4 (timing channel), which
depends on a measurement host.

## Limitations

- A domain in fallback only returns to fast mode at the next mount.
- A file that goes back to having a single name remains heterogeneous until the next change of a
  permission attribute, rename or mount (fail-safe).
- The v2 equivalence against the oracle covers one predicate; the nine forms of the per-domain W3 are left
  for the measurement on the host (`bench-load --query2 1`).
- The concurrency test (8) was not extended to domains.
- Extended ACLs take the file out of the partition (accepted decision).

## Next step

Pull request with KASAN. On the measurement host: `bench/timing-v2.sh` (test 4) and per-domain W3
compared to journal 0029. Then, phase Q2b (`order_by` and `cursor`).

## Update (2026-10-11)

KASAN and lockdep in the CI of pull request 31 (kernel 6.12.112-virtme): `QUERY2_PASS` on ext4,
btrfs and XFS and `MOOBFS_LAYER_PASS`, with no KASAN report; run 38170220257. Merged into main
on the same day.

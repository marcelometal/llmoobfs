# Supported kernels and compatibility policy

Date: 2026-10-13. Status: **accepted** by the maintainer on 2026-10-13 (security roadmap S6,
journal [0080](history/0080-compatibility-policy.md)). It states what is true today and the rules
for what changes. It gathers what [spec-m2](spec-m2.md) §10, [spec-query-v2](spec-query-v2.md),
the README and [distribution-strategy](distribution-strategy.md) §6 already say. Where a rule
needs code that does not exist yet, the rule says so.

There are three interfaces, each with its own version and its own rule.

## 1. Kernels

**Today.** One series, x86-64:

| Kernel | Functional suite | KASAN, UBSAN, lockdep | fstests `quick` |
| --- | --- | --- | --- |
| Debian 6.12.111+deb13-amd64 | yes, every pull request | no (distribution kernel) | yes, three lower filesystems, by hand |
| Upstream 6.12.112, `vm/kasan.config` | yes, every pull request that touches `layer/` or `vm/` | yes | no |
| Anything else | no | no | no |

**Rules.**

1. A kernel is **supported** when the full suite (`make vm-test`) and the KASAN job pass on it in
   CI for every change to `layer/`. Everything else is unsupported, including other 6.12
   builds: the module uses in-kernel interfaces that change between versions without notice.
2. The module refuses to build against a kernel outside the list instead of building with
   warnings. (Not implemented: today the build does not check the version.)
3. A kernel is added by a pull request that adds it to CI and records one fstests `quick` run per
   lower filesystem. It is removed when its upstream series ends or when keeping it needs code the
   maintainer does not want to carry; removal is announced in the changelog one release ahead.
4. Lower filesystems: ext4, btrfs and XFS, on a local block device. A network or FUSE filesystem
   as the lower is unsupported.
5. Architecture: x86-64 only until another one runs the same two jobs.

## 2. Attribute encoding (what is on disk)

**Today.** The only persistent format is the `user.moob.*` encoding, version 1, declared by
`user.moob.format` on the lower root (absent means version 1). The module refuses to mount a tree
that declares a version it does not know.

**Rules.**

1. A tree written by a released version stays readable by every later version. Data is never the
   price of an upgrade.
2. A change that old modules would misread needs a new format number, a specification, tests of
   invalid inputs, an offline migration tool, and an ADR (this is already in `AGENTS.md`).
3. Adding a type or relaxing a limit is such a change. Tightening a limit is not allowed for
   values an earlier version accepted.

## 3. Ioctl interface (UAPI)

**Today.** `MOOB_UAPI_VERSION` 1 (`GET_INFO`, `QUERY`) and `MOOB_QUERY2_VERSION` 2 (`QUERY2`,
`QUERY2_BATCH`), both experimental: the README says they may change with no promise. Every
request carries its version; an unknown version or a reserved field that is not zero returns
`EINVAL`; an unknown command returns `ENOTTY`. `GET_INFO` reports the versions and a capability
mask.

**Rules, from the first release that is not marked experimental.**

1. A structure, command number or meaning that shipped does not change. New behaviour is a new
   command, a new version number in the request, or a bit in a field that was reserved and
   checked for zero.
2. Clients discover features from `GET_INFO` (versions, capabilities, flags), never from the
   module or kernel version.
3. A command is removed only after one release in which it is documented as deprecated and
   `GET_INFO` says so.
4. Until that first release, the interface may change in any release; every change is listed in
   the changelog under its own heading, and the version number in the request changes when a
   request that used to be valid changes meaning.

## 4. Mount options and tools

Mount options follow the UAPI rule: an option that shipped keeps its meaning; an unknown option
refuses the mount. The command-line tools (`moobfs-attr`, `moobfs-check`, `moobfs-index`) keep
their exit statuses and the tab-separated output that scripts read; human-readable text may change.

## 5. Releases

Versions are `MAJOR.MINOR.PATCH`, with `-experimental` while §3 rule 4 applies. A fix for a
defect that affects security or durability gets a patch release of the latest version, and an
advisory when it is a vulnerability under `SECURITY.md`; older versions are not patched.

## Open points

- Rule 1.2 (refusing to build against an unlisted kernel) needs a build-time check, which is a
  code change not made yet.
- Decided on 2026-10-13: no patch release of `v0.2.0-experimental` for now, although it has the
  defects fixed in journals 0074 and 0078.

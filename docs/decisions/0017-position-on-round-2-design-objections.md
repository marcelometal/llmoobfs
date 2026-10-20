# ADR-0017 — The project's position on the design objections of review round 2 (D1 to D3)

Date: 2026-10-20. Status: **proposed**, awaiting the maintainer's acceptance or refusal. Nothing
here is implemented; the commitments in each section are work to do if it is accepted.

## Context

The round-2 review (journal 0155) recorded three design objections that a VFS or filesystem
maintainer would raise. They are not defects: the code does what its specification says. The
maintainer asked for the project's answer to be on record before it is given in public.

Each section states the objection, what is true in it, the position, and what would change the
position. A position the project cannot back with evidence is stated as an open question.

## D1 — The kernel validates the values of `user.*` attributes

**Objection.** The VFS treats `user.*` values as opaque bytes owned by the user
(`xattr_permission()` checks only the file type and the sticky bit). The module refuses a write
to `user.moob.*` whose value does not follow its typed encoding (`layer/main.c`,
`moobl_xattr_set()` and `moobl_typed_check()`), although the writer has the same validator in
user space (`common/attr.c`).

**What is true.** Through the layer, one namespace of `user.*` stops being opaque. Below the
layer, it still is: anything can be written to the lower directory, and the scan leaves out what
does not validate (spec-m2 §3.5).

**Position.** Keep the validation, limited to `user.moob.*`, and state it as the layer's
contract. The reasons:
- The index compares values (equality, ranges, order). A malformed value in it would make
  comparison undefined, and the build and the write paths would have to agree on how to skip it.
- Refusing the write is the only place where the writer learns of the error.
- The kernel already interprets parts of the xattr space it owns: `system.posix_acl_*`,
  `security.*` through the LSMs.

The cleaner form would be a namespace of the module's own, rather than a prefix inside
`user.*`. That needs a change in the VFS core and is out of reach for an out-of-tree module.
This cost is accepted.

**Would change it.** A client that needs to store invalid values under `user.moob.*`. Or a VFS
maintainer's statement that an in-module prefix inside `user.*` is not acceptable even out of
tree; then the index would store malformed values as raw and leave them out of comparisons,
instead of refusing them.

## D2 — The homogeneity rule is a second permission model, evaluated from a snapshot

**Objection.** `QUERY2` answers a homogeneous partition without calling `inode_permission()`
per result. The rule that puts a file in that partition (`reach_ok()`, `target_ok()`,
`place_dentry_above()`, the scan's `inner_ok`) is a model of the kernel's permission check,
computed when the index is built or updated. overlayfs makes two real permission checks at
access time. F9 was a divergence between the rule and `acl_permission_check()`; R2-1 was another.

**What is true.** All of it. The rule is a cache of permission decisions, and every divergence
between it and the kernel's check discloses names and attribute matches. Four findings so far
were in it or in what keeps it current: F9, the round-1 chmod race, R2-1 and R2-2. Two of its
parts had no test until this batch (T3, T4).

**Position.** The homogeneous partition is an optimization. It is acceptable only while it is
shown to give the same answers as a per-result check. The project commits to:
1. Measure the cost of answering every query with per-result checks (the existing fallback
   mode) against the homogeneous partition, by the method of `docs/benchmark-plan.md`. If the
   difference is small, per-result checks become the default and the partition an option.
2. A test-only consistency mode that compares, after each operation, every homogeneous answer
   with `inode_permission()` as each test identity, and fails loudly on a difference.
3. The mutation and differential tests of the security roadmap (S14 and S15): every check of
   the rule must have a test that fails without it.

**Would change it.** The measurement in 1. Or a further divergence found after 2 and 3 exist:
then per-result checks become the default regardless of cost.

## D3 — Whether this belongs in the kernel

**Objection.** The layer passes data through unchanged, unlike ecryptfs, overlayfs or FUSE
passthrough. Its index is a volatile cache rebuilt by a tree walk, which is what `updatedb` and
`tracker` do in user space. `setxattr` raises an `fsnotify` `FS_ATTRIB` event, so a user-space
indexer could be exact. The repository already has a SQLite front end.

**What is true.** The likely upstream answer is "ship the user-space index". The project does
not claim it belongs in the mainline kernel, and never proposed it there.

**Position.** MoobFS is an out-of-tree experiment, and this question is its subject, not a
settled premise (ADR-0007, `docs/when-not-to-use.md`). What the kernel gives that user space does
not:
- the index changes atomically with the write, under the file's `i_rwsem`, so a query never
  sees a value the file no longer has, and no event can be lost or reordered;
- the permission decision is the kernel's, made without a privileged daemon that reads every
  file;
- nothing has to run for the files to stay plain and readable.

Whether that is worth a kernel module is an empirical question. The project commits to building
the user-space comparison: an indexer driven by `fanotify` over the same tree, behind the same
query interface. Then it will publish what each one gets right and wrong: exactness under
concurrent writes, permission answers, cost. If user space is as good, the project says so.

**Would change it.** That comparison, in either direction.

## Consequences

- Accepted: the three positions are what the project answers in public. The commitments
  (measurement of D2.1, the D2.2 mode, S14 and S15, the user-space comparison of D3) enter
  `ROADMAP.md` and `docs/security-roadmap.md` as planned work.
- Refused: the objections stay recorded in journal 0155 without an answer, and the public answer
  says so.

# M3 specification — identity, isolation and query operations

Status: **proposal** (2026-10-10, revised on the same day after an external review; see journal
0019). Nothing implemented. Base decisions: ADR-0005, ADR-0007. Depends on M2 being concluded.

## 1. Diagnosis of what exists (evidence in the code)

| Proposal | Already exists? | Evidence | Change needed | Risk |
| --- | --- | --- | --- | --- |
| Attributes `namespace`, `agent`, `memory_id` as typed strings | Yes, like any `user.moob.<name>` of type `string` up to 1 KiB, name `[a-z0-9_.]` ≤ 64 bytes | `include/moobfs_attr.h` (`MOOB_ATTR_STRING_MAX`, `MOOB_ATTR_NAME_MAX`), validation in `moobl_typed_check` (`layer/main.c`), per-name index in `layer/index.c` | None in the kernel; only a documented convention | none |
| `QUERY` by these attributes, in conjunction, with prefix | Yes | `do_query`, `entry_matches` in `layer/index.c`; phase 2d compared with the oracle (journal 0014) | none | none |
| **`QUERY` respects the caller's permissions** | **No** | `fill_result` (`layer/index.c`) returns the inode and path of every index entry; the only check is `file->f_path.dentry == sb->s_root` in `moobl_root_ioctl`; the index is built with the mounter's credentials (`override_creds` in `moobl_index_build`) | filter results by the caller's credentials | **leak of existence and of paths between users; fix before any multi-user use** |
| Stable identity under `rename` | Partial | the index is per inode; `moobl_index_renamed` only updates the path | none for identity by attribute (the attribute follows the inode) | none |
| Logical revision, history, change cursor | No | volatile index, rebuilt at mount (`moobl_index_build`); no revision counter | see §5 | — |
| Batch query | No | one `QUERY` per call, up to 4 096 results (`MOOB_QUERY_MAX_LIMIT`) | see §4 | — |
| Batch content read | No | results carry a path; the client opens them one by one | see §4 | — |

## 2. Identity model

Logical identity `(namespace, agent, memory_id)` expressed in three ordinary typed attributes,
generated and guaranteed unique **by the client**:

| Attribute | Type | Rule |
| --- | --- | --- |
| `user.moob.namespace` | string, ≤ 128 bytes recommended | logical domain; opaque to the kernel |
| `user.moob.agent` | string, ≤ 128 bytes | identity of the agent that created or updated; opaque |
| `user.moob.memory_id` | string, ≤ 128 bytes; UUID v7 as text recommended | stable identity; survives `rename` and `FICLONE` (a copy carries the attribute, the client decides whether to generate another) |

Logical key: `(namespace, memory_id)`. `agent` is authorship or responsibility, not part of the
key, so that another agent can update a memory without changing its identity. The same
`memory_id` in different namespaces are two distinct memories, which may share origin,
content or blocks; equal content is not equal identity. `FICLONE` and copy create another inode:
when the operation is a memory clone, the client generates a new `memory_id` and records the
origin in `user.moob.origin_namespace` and `user.moob.origin_id` (strings; convention, not
verification). A hard link is the same inode and the same attributes, so it does not serve for two
identities; for that, copy or reflink. `moobfs-check` classifies: normal, the same id in
distinct namespaces; warning, the same id with identical content and no provenance (optional content
comparison); error, two distinct inodes with the same `(namespace, memory_id)`;
several paths to the same inode are not duplication.

The kernel does not validate uniqueness or format beyond the general type and name rules, does not
interpret these attributes as credentials or provenance, and does not change them. Comparison is by bytes.
No new type and no reserved attribute. The alternative of identifiers assigned
by the kernel (`id`, `rev`, `mrev` as reserved attributes, written on each write) was
evaluated and **postponed**: it would cost a `setxattr` on the lower filesystem per change and would create state that the
client cannot reproduce on its own; it only comes back if M4 shows that the client cannot
maintain identity and revision at an acceptable cost.

## 3. Isolation by POSIX and correction of `QUERY`

Invariants: changing `namespace` or `agent` does not grant access; `QUERY` does not reveal
inaccessible memories; the index does not bypass permissions; isolation holds after remount; `rename` does not
change identity; hard links and reflinks do not add privilege; concurrent queries with
permission changes do not leak.

Reference layout (not imposed): `/<ns>/shared/` and `/<ns>/agents/<agent>/`, with POSIX owners,
groups and modes defined by whoever administers the mount. The module provisions nothing.

Change in the kernel, the only one in M3 in this form: in `do_query`, for each candidate entry,
before copying it to the user, resolve the recorded path with the **caller's** credentials
via `vfs_path_lookup(sb->s_root, mnt, path, 0, &p)`; the resolution fails with `EACCES` if
search permission is missing on any directory of the path, and the entry is omitted without error and
without counting in `truncated`. If it resolves, require that the obtained inode be that of the entry (protects
against a stale path) and `inode_permission(MAY_READ)` with the caller's credentials. Without
a recorded path, try the cached dentry alias and apply the same checks;
without an alias, omit. `truncated` now counts only visible results. The index read
lock is released before the resolutions so as not to sleep holding it; the list of candidates is
copied first. Cost: one path resolution per result, bounded by `limit`; measured
in M4 (W3).

Client responsibilities: create directories, users, groups and ACLs; generate
identifiers; decide what to share via `FICLONE` or link.

Tests: in the VM, two uids and two namespaces with distinct modes; cross queries must
omit the other namespace and `truncated` cannot reveal a count; `chmod` during concurrent
queries; remount and repetition; the user-space oracle run as each
uid must match the kernel. Full regression (POSIX, git, SQLite, dm-log-writes,
dm-flakey, KASAN/lockdep). Done-when criterion: identity stable and queryable without a second
authorization policy; zero leakage between users on all lower filesystems.

### 3.1 `QUERY` authorization contract (UAPI v1, semantics)

- Authorization is checked at the moment of the query, with the caller's effective
  credentials, by path resolution in the VFS and by `inode_permission(MAY_READ)` on the resolved
  object, which must belong to the queried mount and have its inode indexed.
- A result does not grant access: the client does `open()` normally and must accept `EACCES`
  or `ENOENT` if permissions or names changed after the query. There is no guarantee of absence
  of a TOCTOU window in v1 and there will be no special handle to close it without a measured need.
- `count`, `limit` and `truncated` refer only to results visible to the caller. The number of
  candidates examined is not bounded by `limit`: the scan goes through all candidates
  in batches, resuming by ordered key.
- **Timing channel (measured on 2026-10-11, journal 0030).** Response time grows about
  1.15 µs per candidate invisible to the caller, with variation below 3%: a caller can
  estimate how many entries of other owners match its query, and test whether some value
  exists anywhere in the mount. The contract guarantees that the returned results and counts
  do not reveal invisible entries; it does not guarantee this against timing measurement. Closing
  the channel requires that the query not traverse entries of other permission domains (ADR-0009).
- Authorization failures omit the result silently. Operational failures (`ENOMEM`,
  `EFAULT`) return an error.
- Entries whose recorded path became stale (renamed directory) are resolved through the
  dentry alias when it exists; without a verifiable path, they are omitted. This may produce
  false negatives until the next mount; it is a documented limitation, not a leak.

## 4. Batch operations: contract, decision conditional on measurement

Two candidates, with different purposes:

- `QUERY_BATCH`: several independent queries in one call, each with the exact semantics
  of `QUERY`, result and error per query, with no atomicity or snapshot among them,
  explicit limit (proposal: 64 queries per call, individual `limit` ≤ 4,096,
  total memory ≤ 64 MiB), permissions checked per result, additive UAPI v2 (new
  command, v1 structures intact).
- `READ_BATCH`: for an already obtained list of inodes, return chosen attributes and the
  first K bytes of content with per-inode permissions, to replace N opens.

Decision: **neither enters before M4 measures** W3 (queries) and W2 (read by
path) against individual calls. If the cost of the kernel call does not dominate, the
implementation is dropped and that is recorded. Mandatory tests if implemented:
unknown version, zero count and above the limit, null and invalid pointer, size
overflow, reserved fields not zeroed, invalid inner query, partial failure, concurrent removal, rename
and `setxattr`, inaccessible results, compatibility with a v1 client, fuzzing
of the parsers.

**Phase 3c, 2026-10-13 (journal 0072).** The query batch exists as `MOOB_IOC_QUERY2_BATCH`
(up to 8 requests; phase Q2d). A batch read of content is dropped: the most it could save is
4.4 µs per file, of which the lower filesystem's own open, read and close are 4.5 µs. What the
measurement did show is that reading the *attributes* of results costs 2 µs per file for one and
16 µs for all, against 0.9 µs per result for the query, while the index already holds the values.
Returning chosen attribute values with each result is proposed for an ADR; nothing is built.

## 5. Logical revision and change cursor: decision

Two distinct concepts: **persistent logical revision** (evolution of a memory) and **enumeration
cursor** (progress of a query, valid at most per mount).

Decision: logical revision and history stay **in the client**, with the mechanisms that already
exist: git for content, a typed attribute `user.moob.rev` (int64) maintained by the client
when it wants to query it through `QUERY`, and `modified`/`ctime` of the lower filesystem for coarse
ordering. Reasons: the index is volatile and does not constitute history; changes made outside the
mount would not be seen by a kernel log; a persistent log in the kernel would duplicate
git; and no client asked for this operation with a measured benefit. The cursor form
evaluated (circular list per mount, `EOVERFLOW` when expired, `CHANGES_SINCE`) stays
documented as an alternative and is only reconsidered if M4 shows that the client's scan
after a session costs more than acceptable. Questions any implementation would have to
answer first: identity and scope of the revision, persistence, ordering, concurrency,
recovery after failure, cursor validity, error codes, integration with git.

**Phase 3c, 2026-10-13 (journal 0072).** Unchanged: revisions and the change cursor stay in the
client. An attribute the client maintains plus the `QUERY2` cursor answer "updated after T" with
equal results up to 60 000 files (journal 0068), a file watcher can maintain that attribute
(journal 0071), and no surveyed system asks its store for changes since a revision.

## 6. Phases

| Phase | Deliverable | Exit |
| --- | --- | --- |
| 3a | Identity convention documented; `moobfs-check` optionally checks uniqueness of `memory_id` per namespace | document and offline check |
| 3b | `QUERY` with the caller's permissions; multi-user test; full regression with KASAN/lockdep | zero leakage; preliminary cost numbers |
| 3c | Decisions of §4 and §5 revisited with the M4 numbers | journal in `docs/history/` |

## 7. Risks and limitations

Resolving paths per result may be expensive in broad queries; mitigation: `limit` and
measurement. Recorded paths become stale after renaming directories; the alias fallback
covers cached entries and the others are omitted until the next mount, a behavior that
must be documented and measured. Nothing here changes UAPI v1.

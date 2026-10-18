# Critical assessment of usefulness and technical value proposition of MoobFS

*Note, 2026-10-18: an analysis as of 2026-10-10, kept as written. The project's current status is in the README ("Status" and "Limitations") and `docs/security-roadmap.md`; several gaps named below have been closed since (syzkaller, fstests, the threat model, the index built on demand).*

Status: analysis (2026-10-10, branch `m3/3b-query-authz`). It does not change ADRs, format, UAPI or
code. Basis: README, ROADMAP, ADR-0005/0006/0007, `docs/spec-m2.md`, `docs/spec-m3.md`,
`docs/benchmark-plan.md`, `docs/benchmark-harness.md`, `docs/security-roadmap.md`,
`docs/research-originality.md` (§4, §5, §7), `docs/distribution-strategy.md`, journals
0014, 0015, 0016, 0020, 0021, `layer/index.c`, `layer/main.c`, `include/moobfs_ioctl.h`.

Labels used on each claim:

- **proven**: there is a test or code reading in the repository that supports it (with the caveat of the
  environment: one author, VM, Debian kernel 6.12.111 and, on demand, 6.12.112 with KASAN).
- **potential**: follows plausibly from the mechanism, but no measurement exists.
- **not demonstrated**: no evidence, or evidence that does not cover the claim.

Central fact, which conditions the whole text: **no performance number has been measured**
(`docs/benchmark-plan.md`, opening). Every claim of a gain in speed, memory or cost is,
at most, potential. Text written with that limit; "estimate, not measured" marks derivations.

---

## 1. Speed and performance

### 1.1 Comparison principle

Only semantically equivalent operations are compared. "Query by attribute" in MoobFS is a
conjunction of up to 4 typed predicates (eq/lt/le/gt/ge/prefix/exists) over `user.moob.*`, with
per-result authorization. The equivalent operation in the baselines is an SQL query with a composite
index over a table maintained by the application (baselines A and B of the plan), and, for "plain
files", `find` + `getfattr` (linear scan). Semantic search, OR and free text do not exist
in MoobFS and are **not part** of any comparison.

Certain structural cost, independent of measurement: the layer inserts a VFS hop into every
operation (upper dentry/inode that delegates to the lower; I/O via `fs/backing-file.c`;
`override_creds` with the mounter's credentials in delegated operations). This cost is
**certain in sign** (positive) and **not measured in magnitude**. No MoobFS operation can
be faster than the same operation directly on the lower, except where it replaces work that the
baseline would do in two steps (file + database).

### 1.2 Per operation

| Operation | Where the layer may win | Where it certainly adds cost | Metrics that would show a relevant gain | Workload |
| --- | --- | --- | --- | --- |
| Create/update a memory (tmp + `setxattr` + fsync + rename + fsync dir) | **Potential**: a single store, no second write (SQLite requires writing the file and then the row; two durabilities). Updating the index is an in-RAM operation under a lock | **Certain**: VFS hop per call; type validation in `setxattr`; `index_put` under `down_write` of the index's global rwsem (serializes attribute writers of all inodes); `record_dentry_path` allocates a page and builds the path on every `setxattr`. Durability is that of the lower, with no gain | p50/p95/p99 of W1 per call; write amplification (`WA_relative` of the plan: the baseline includes WAL + pages + SQLite indexes); CPU per creation | W1, W4 |
| Read by known path | No plausible gain: the data is read from the lower via `backing_file_read_iter` | **Certain**: the extra hop. Magnitude unknown; for small files in the page cache it tends to be proportionally visible, but this is a hypothesis | W2: p50/p99 of `open+read+close` directly versus through the layer, same bytes, warm and cold cache. Expected gain: none; the criterion is "acceptable overhead" | W2 |
| Query on 1 attribute | **Potential**: `QUERY` walks the attribute's rbtree in the kernel, with no process context switch or IPC with a database; one `ioctl` call | **Certain**: per-result authorization (`vfs_path_lookup` + `inode_permission` per candidate, batches of 256) that SQL does not do; `kstrdup(GFP_ATOMIC)` per candidate under the read lock; the scan is linear over the entries of the first term, **without using a range in the rbtree** (journal 0014, "Limitations") | W3 (Q1, Q3, Q6, Q7, Q9): p50/p95/p99 and CPU per query, for selectivities from 0.1% to 50%, with private-candidate ratios of 1:1 to 100:1 | W3 |
| Multi-filter query (2 to 4 terms) | Limited **potential**: one index per attribute (not composite): one term is chosen (the first, not the most selective in the implementation read) and the others are filtered via the inode's list | **Likely disadvantage against SQLite**: SQLite uses a composite index with equality before range and the optimizer picks the plan; the layer scans all entries of the first term. If the first term is not very selective, the cost is O(N) in RAM plus authorization of each candidate that passes the other terms | W3 (Q2, Q4, Q5, Q8): p99 versus SQLite with a composite index, with a mandatory `EXPLAIN QUERY PLAN` gate | W3 |
| Recovery across sessions (remount + first query) | **Potential**: there is no database to open/recover; but the index is **not persistent**, so the first query only answers after the full scan | **Certain**: mount time proportional to the number of files (walk + `listxattr` + `getxattr` per attribute). SQLite opens a file and answers in milliseconds regardless of the number of rows | W6 (cold), W7 (warm): time to the first correct `QUERY`, separating I/O from CPU, at S1..S6. If it grows linearly up to minutes, the argument weakens | W6, W7 |
| Concurrent multi-agent | **Potential**: no database process in between; file reads are concurrent as on the lower; `QUERY` uses a read lock | **Certain**: a single rwsem per mount; any agent's `setxattr` takes the write lock; a broad query holds the read lock per batch, delaying writers. SQLite WAL has one writer at a time, but readers do not block | W8 (1, 10, 100 processes): write throughput and p99 under queries; W5 (concurrent rename/removal) | W5, W8 |
| Mount / index rebuild | None | **Certain**: linear cost, in RAM, without persistence (decision: no persistence before M4) | W6/W7 at S1..S6 and saturation 50..100% of the budget | W6, W7 |

### 1.3 Summary

Proven: correctness against the oracle (tree of 6 inodes) and running git and SQLite WAL unmodified (200 inserts). Not demonstrated: any gain in latency, throughput, CPU or space. For W1, the comparison is only equivalent if durability is matched (SQLite in WAL with `synchronous=NORMAL` may lose the last transactions on a crash; MoobFS requires an explicit `fsync`).

## 2. Memory efficiency

### 2.1 Actual structures (`layer/moobl.h`, `layer/index.c`)

- `moobl_ix_name`: one per distinct attribute name. `rb_node` (24 B) + `rb_root` (8 B) +
  `count` (4 B) + `name[65]`: about 104 B per name. Negligible (dozens of names).
- `moobl_ix_inode`: one per inode with at least one indexable attribute. `ino` (8) + `list_head`
  (16) + `upper` (8) + `path` (8) = 40 B accounted; the slab allocator delivers a
  64 B class. Plus the `xarray` entry (see below).
- `moobl_ix_entry`: one per (inode, indexed attribute). `rb_node` (24) + `list_head` (16) +
  two pointers (16) + `type` and `len` (rounded to 8) = **64 B of header** + payload
  (`int64`/`time`/`double` 8 B; `bool` 1 B; `string` up to 1024 B). `raw` is not indexed.
- Path: `kstrdup`/`kmemdup` of the relative path, length + 1 B, once per inode.
- `xarray` per inode: nodes of 64 slots (about 576 B per node). With dense inode
  numbers the cost is on the order of 9 to 16 B per inode; with sparse inodes (typical on ext4/XFS
  after many creations and removals) it grows, in the worst case to hundreds of B per inode.

**The budget accounts for** `sizeof(struct)` + payload + `strlen(path)+1` (functions `charge`/
`uncharge`). It **does not account for** slab rounding, the `xarray` nodes or the
transient allocations of `QUERY` (`kvcalloc(limit × sizeof(result))`, up to 4096 × ~280 B ≈
1.1 MiB per query, plus 256 paths). Therefore `index_used` in `GET_INFO` **underestimates the actual
RAM**. This difference is a finding to fix or document.

### 2.2 Estimate per inode (estimate, not measured)

Let A be the number of indexed attributes per inode, P the average path (~40 B) and V the average payload
(~16 B, a mix of integers and short strings).

Accounted (what the budget sees): `40 + (P+1) + A × (64 + V)` ≈ `80 + 80·A` B.

| Profile (benchmark-harness §3) | A | Accounted per inode | Estimated actual RAM per inode (slab + xarray) |
| --- | --- | --- | --- |
| 3 attributes | 3 | ~320 B | ~450 to 600 B |
| 8 attributes | 8 | ~720 B | ~1.0 to 1.2 KiB |
| 16 attributes | 16 | ~1.36 KiB | ~1.9 to 2.3 KiB |

Note: a UUID `memory_id` (36 B) and `namespace`/`agent`/`tag` as strings raise V. With V = 40 B,
the 8-attribute profile goes from ~720 B to ~1.0 KiB accounted.

### 2.3 What 128 MiB means (estimate, not measured)

`128 MiB = 134 217 728 B`, in number of inodes by what is accounted:

| A | inodes until the budget saturates (accounted) | inodes with estimated actual RAM |
| --- | --- | --- |
| 3 | ~400 thousand | ~230 to 300 thousand |
| 8 | ~180 thousand | ~110 to 130 thousand |
| 16 | ~95 thousand | ~55 to 70 thousand |

With larger strings (V ≈ 100 B, e.g. verbose `tag` and `kind`), reduce by 30 to 40%. Conclusion
of the derivation: the default budget covers somewhere between **tens and a few hundred thousand
memories**, which **is sufficient** for agent memories at project scale
(thousands to tens of thousands) and **insufficient** for large corpora. Note on the
plan: the plan's S6 scale (50 to 65 thousand inodes) may not reach 100% of the budget with 3 to 8
attributes; to measure saturation it will be necessary to reduce `index_budget` (the option accepts 1M to 4G) or
go beyond S6. This must be decided before running M4.

### 2.4 Behavior near saturation (proven by code reading; actual saturation not measured)

- **Mount**: if the scan overflows the budget, `index_put` returns `-ENOMEM`, the scan
  stops and `moobl_index_build` fails with a warning (`mount with noindex to skip it`): mount
  refused, or `noindex` (QUERY answers `EOPNOTSUPP`; `ENOSYS` until journal 0050). Tested with `index_budget=1M` on a
  tiny tree (journal 0014); with a large workload, not.
- **At run time**: a memory failure when updating the index after a write already done on the
  lower **does not undo the write**: it marks `incomplete` and `GET_INFO` exposes
  `MOOB_FLAG_INDEX_INCOMPLETE`; `QUERY` starts giving **incomplete answers until remount**.
  A `charge` failure when recording a path is silent (`index_set_path` keeps the old name;
  the query falls back to the dentry alias and may omit the result); an entry update failure
  emits a single `pr_warn` and marks `incomplete`. Practical consequence: near 100% the system
  silently loses results instead of failing; the client needs to check `GET_INFO`.
- `GET_INFO` fields: `index_budget`, `index_used`, `index_entries`, `index_inodes`,
  `flags` (`INDEX_INCOMPLETE`, `PATHS_STALE`, `INDEX_OFF`).
- There is no per-user ceiling and no check of cgroup accounting
  (`distribution-strategy` §7: "not verified"). Multiple mounts add up budgets.

### 2.5 Rebuild

Walk of the whole tree + `listxattr` per file + `getxattr` per attribute, linear in the number of files (every file is visited, whether or not it has attributes), with the index's write lock held. Cold cache: dominated by metadata I/O (potentially minutes for hundreds of thousands of files on a slow disk, to be measured in W6); warm cache: CPU (W7).

### 2.6 Comparison with SQLite (without claiming savings)

SQLite keeps indexes on disk; RAM is the page cache (configurable, by default ~2 MiB per
connection) plus the application process, and the operating system caches the file in the page cache.
A row of 8 columns with ~5 to 6 indexes typically takes a few hundred bytes on
disk (estimate, not measured). **It is not possible to claim that MoobFS saves memory.**
Qualitative differences:

- The MoobFS index **must be entirely in kernel RAM** (not pageable, not
  discardable under pressure) and is rebuilt at every mount. SQLite can serve queries
  with a working set smaller than the database and persists without rebuilding.
- MoobFS eliminates the application process as the owner of the index and the duplication of path/value
  between table and file, but **duplicates** the attribute values between xattrs on disk and the index
  in RAM.
- On hosts with plenty of memory and a few tens of thousands of memories, the difference is probably
  irrelevant. On small hosts, non-pageable kernel memory is worse than a discardable
  cache. **Potential disadvantage**, to be measured via RSS + `index_used` versus SQLite's RSS
  at S1..S6.

---

## 3. Security and isolation

### 3.1 What is inherited from the VFS and the lower (not to MoobFS's credit)

- uid/gid, mode bits, POSIX ACLs, directory traversal, `inode_permission`,
  `security_inode_permission` (LSM), `CAP_DAC_*`. The layer delegates: `moobl_permission` calls
  `inode_permission` on the lower inode (journal 0020). **Proven** that file access
  decisions through the layer match those of the lower in the two-uid tests on ext4,
  btrfs and XFS.
- On-disk confidentiality, encryption, data integrity (btrfs checksums), all from the
  lower.
- Root and whoever accesses the lower directory bypass the layer (structural limit, documented).

### 3.2 What MoobFS adds

| Addition | Mechanism | Status |
| --- | --- | --- |
| Per-result authorization of `QUERY`, including `count`/`limit`/`truncated` only over what is visible | `caller_may_read`: `vfs_path_lookup` with the caller's credentials (requires search permission on each directory), same mount and superblock, same indexed inode, `inode_permission(MAY_READ)` | **proven** in a local test (two uids, two namespaces, 600 private candidates and 6 authorized, ACLs, remount) on three lowers; no external audit |
| Type validation of attributes in the kernel | `moobl_typed_check` before touching the lower; `EINVAL`; `user.moob.format` reserved | **proven** (invalid-input tests, encoding fuzzing) |
| Refusal of idmapped mounts and of mounting in a user namespace | `FS_ALLOW_IDMAP` and `FS_USERNS_MOUNT` not declared; `EINVAL`/`EPERM` tested | **proven**; conservative choice, avoids the overlayfs class of CVEs, at the cost of not working in rootless containers |
| Requirement of the mount root for `QUERY`/`GET_INFO` | `file->f_path.dentry == sb->s_root` | **proven**; usability limitation in containers |

### 3.3 Metadata leakage: what `QUERY` could reveal and how it is prevented

A global in-RAM index, without filtering, would reveal: the existence of inaccessible files, their
paths, the number of inodes and the hit count (side channel via `count`/`truncated`).
That was exactly the flaw of the initial specification (`spec-m3` §1: "leakage of existence and of
paths between users"). The mitigation implemented: each candidate is resolved by the VFS with the
caller's credentials and silently omitted if denied; `count` and `truncated` count only
visible ones (**proven** by test). Residual risks **not eliminated**:

- **Timing channel**: query latency grows with private candidates (scan in batches
  of 256 with one path resolution each). A user can infer, by timing, how many
  other users' candidates exist. **Not demonstrated** whether it is exploitable; not tested.
- **Attributes in visible xattrs** follow the file's permissions (lower); there is no additional
  leakage if the file is unreadable, but `QUERY` reveals to the reader the names that match.
- **False negative after a directory rename** (`PATHS_STALE`): does not leak, but omits; documented.
- Handles for `open_by_handle_at` are not returned in the current v1 (the result carries inode and
  path); S11 of the security roadmap requires re-checking if this changes.

### 3.4 TOCTOU contract

A result **does not grant access**. Authorization holds at the moment of the query; the client does
`open()` and must accept `EACCES`/`ENOENT`. There is no special handle. This is correct and honest,
but it means that `QUERY` may list a name that stops being readable right after the answer
(leakage of existence for an instant, not of content).

### 3.5 Concurrency between `chmod`/`rename` and queries

Test added to `vm/suite.sh` on 2026-10-10: 40 files, one process changing `chmod`,
`rename` (file and directory) and `unlink`+re-creation in a loop while `QUERY` runs as uid 1001
for 5 s; it requires well-formed answers and final equality with the oracle. **Run on
2026-10-10 (journal 0020)**: about 620 queries per lower, all results opened
or failing only with `ENOENT`/`EACCES` after the answer (the contract's TOCTOU window), final
answer equal to the oracle on ext4, btrfs and XFS, with no kernel warnings. The absence of leakage under
this concurrency is **proven at the test's scale** (40 files, one writer, emulation);
KASAN/lockdep over the branch remains pending (run triggered, no result
recorded).

## 4. Reliability and persistence

### 4.1 What is delegated to the lower (and therefore neither to MoobFS's credit nor discredit)

Persistence, journal, crash recovery, rename atomicity, data checksums,
reflink, dedup, ACLs. **Proven** (journal 0015): `dm-log-writes` with replay at five
marks (`mkfs`, `m1`, `m2`, `m4`, `end`) showed, on ext4, a clean `e2fsck` and memories up to the
mark intact (content and attributes); `dm-flakey` with write errors propagated the error without
`BUG`/`Oops`/`WARNING` and allowed a clean remount. Limit: **one** workload scenario, ext4 for the
replay, a single VM, `dm-flakey` only on ext4 and only in write-error mode; btrfs and XFS were not
subjected to replay.

### 4.2 File × metadata consistency

Content and xattrs are distinct operations on the lower; there is no transaction. The convention
(tmp + `setxattr` + `fsync` + `rename` + directory `fsync`) yields the final name with complete content and
attributes **if the client follows it**; evidenced with `dm-log-writes`. Clients that
write in place may observe new content with old attributes after a crash. SQLite, with
table and index in the same transaction, **offers atomicity that MoobFS does not offer** between data
and metadata, but not between an external file and the database (the two-store baseline has the
inverse problem: file and row are not atomic with each other). MoobFS trades the "file
and row" problem for "content and xattr", both with the write order under the client's responsibility.

### 4.3 Concurrency and defects already found

Three real defects in the module itself were found and fixed (all found by tests, not
by external review):

1. negative upper dentry keeping `d_fsdata` pointing to the old lower: loss of
   SQLite WAL frames (184 to 188 of 200 rows) — **data loss**;
2. race in `set_nlink` from copying attributes without a lock: `WARNING` in `__destroy_inode` only
   visible on the 2-CPU x86 runner, not in the local VM under TCG;
3. `rwsem`/`xarray` destroyed without initialization in mounts that fail early: only the kernel with
   `DEBUG_RWSEMS` flagged it.

Critical reading: defect 1 was a **silent data loss in an ordinary client**, found
because a demanding client was chosen; 2 and 3 were found by changing the test environment.
This suggests that the rate of residual defects in paths not yet exercised is **non-zero and
unknown**. Missing coverage: full xfstests, syzkaller, long runs, other
kernels.

### 4.4 Lowers, kernel and stacking

ext4, btrfs and XFS: proven in a VM (fsx of 9000 operations each). Kernel: only Debian 6.12.111 and upstream 6.12.112 (on demand); the internal API has already diverged between them. Stacking avoids reinventing the journal and the allocator (proven by design) but brings its own risks: inode aliasing, dentry coherence, mmap and distinct lock domains between access through the layer and through the lower.

## 5. Development agility

### 5.1 Where it reduces complexity (potential)

- **One store instead of two**: files with typed xattrs + `QUERY`, instead of files
  + a database that the application keeps in sync (two writes, a possible inconsistency, a repair
  routine). **Plausible architectural reduction, not measured.**
- **No daemon**: no database process or per-host service. **Proven by design.**
- Any Unix tool reads the data; backup is `rsync`/`git`. **Proven by design and for
  git/fsx**.
- Type validation in the kernel prevents malformed data in the attribute, which SQLite with typed
  columns also prevents (weaker type affinity, but CHECK/STRICT available).

### 5.2 Where it adds complexity (proven)

- Linux-only; a kernel module to install and maintain; mounting requires `CAP_SYS_ADMIN` in the initial
  namespace; no user namespace; no idmapped mount; no container doing the mounting.
- **A simple API is not a simple integration**: `QUERY` is an `ioctl` on the mount root. There is no
  general CLI (`tests/query.c` is a test binary), no language binding, no library.
  A client in Python/Node/Go needs to write the `struct moob_query` structure (~4.4 KiB) and
  do the `ioctl` by hand. A subdirectory with a bind mount **cannot** issue `QUERY`.
- Provisioning (users, groups, modes, ACLs, namespace directories) is the administrator's job;
  no tool exists. Identity `(namespace, memory_id)` is a client convention,
  checked offline by `moobfs-check`.
- Limited query model: conjunction of up to 4 terms, no OR, no ordering, no
  aggregation, no join, no external cursor pagination (resumption is internal). SQLite offers
  full SQL. A need that MoobFS does not cover forces the client back to the database.
- Debugging: no `EXPLAIN`, no interactive shell.

## 6. Use cases

### Case A: shared multi-agent memory on one host, with POSIX isolation

- Real problem: several agents (distinct uids) share a store; each may only see the
  memories that the OS allows it to, including in searches.
- Conventional solution: SQLite/Postgres with an authorization layer in the application (or a
  service), or one directory per agent without cross search.
- Use of MoobFS: one mount per host, `/<ns>/shared/` and `/<ns>/agents/<a>/`, one uid per agent,
  `QUERY` filtered by the caller's credentials.
- Expected benefit (potential): cross query with a **single access policy** (the OS's),
  with no second authorization mechanism to maintain; ACLs work.
- Metric that proves it: W3 with N uids and private-candidate ratios of 1:1 to 100:1 (p99 and CPU);
  zero leakage (already tested at small scale) under W5; cost of maintaining policy equivalence
  in the baseline (lines of authorization code on the SQLite side, measured, not estimated).
- When it is **not** the best choice: when agents run in rootless containers or without
  access to the mount root; when a single trusted application already does the authorization; when the
  corpus exceeds the index budget; when the query requires OR, ordering or aggregation.

### Case B: memory that must remain readable by git/grep/rsync/backup

- Problem: audit, version and restore memories with common tools, without export.
- Conventional: markdown files + git, without search by attribute; or a database with export.
- Use: layer over ext4/btrfs/XFS; unmount and the lower directory is an ordinary tree.
- Benefit (proven by design): there is no format of our own; `moobfs-check` validates. The
  benefit also holds **without the layer** with plain files; what the layer adds is the
  query.
- Metric: restore from `rsync`/`git` followed by a clean `moobfs-check`; time to
  rebuild the index (W6).
- It is not the best choice if queries are rarely needed: plain files + `find` +
  `getfattr` are enough and do not require a module.

### Case C: single-process, single-agent application

- Problem: store an agent's memory.
- Conventional: files or SQLite. A single process that owns the data needs neither POSIX
  authorization nor cross queries.
- **Likelihood of being worth it: low.** The cost of a module, root and installation is not paid back by
  any benefit; SQLite is faster to adopt and more mature. Current recommendation: SQLite
  or files.

---

## 7. Value proposition: matrix

| Claimed benefit | Mechanism | Current evidence | Evidence needed | Baseline | Risk/trade-off | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Faster query by attribute | in-kernel RAM rbtree, one call | correctness against the oracle on a 6-inode tree | W3 p50/p95/p99 at S1..S6, with the `EXPLAIN` gate | composite SQLite A/B | per-result authorization and linear scan may cancel out the gain | not demonstrated |
| Less writing / a single store | xattr + RAM index without a second write | none | W1 and `WA_relative` (diskstats) | SQLite WAL + file | global index lock; durability equal to the lower's | potential |
| POSIX isolation in queries | `vfs_path_lookup` + `inode_permission` per result | tests with 2 uids, ACLs, 3 lowers (journal 0020) | concurrent W5, KASAN, external review, timing channel | authorization in the application | timing channel, TOCTOU, false negative after rename | proven at test scale; phase open |
| No lock-in | real xattrs, ordinary files | design + git/fsx/`moobfs-check` | backup restore with standard tools on a real corpus | plain files (tie); SQLite (file readable by `sqlite3`) | index does not persist | proven (design and tests) |
| No daemon | module in the kernel | design | operate for N weeks without incident | SQLite (also without a daemon, in-process) | failure in ring 0 instead of in the process | proven as design; real advantage only against Postgres |
| Recovery across sessions | remount rebuilds the index | correct remount on a small tree | W6/W7 at S1..S6 | SQLite opens a file | grows linearly; holds the lock | potential (for: no index corruption; against: time) |
| Scale by a 128 MiB budget | `charge` accounting | estimate of ~10^5 inodes | saturation 50..100% measured | SQLite with no RAM limit | silent incomplete answers near the ceiling | not demonstrated |
| Physical sharing of bytes | reflink/dedup of the lower | tests on btrfs/XFS | W-share with a real dataset | cp --reflink / duperemove directly | not to MoobFS's credit | inherited; do not credit |
| Compatibility with real clients | full passthrough | git, SQLite WAL (200 inserts), our own client | ai-memory, mem0, Letta; xfstests | n/a | latent defects (3 found) | proven at small scale |

### 7.1 Negative M4 results and what they invalidate

| Negative result | Argument that falls |
| --- | --- |
| W3 p99 is not better than SQLite with a composite index, in any of the forms Q1..Q9 | The **query speed** argument falls. What remains is the authorization and architecture (no database) argument, which needs to be reassessed in terms of operational cost, not latency |
| W3 only wins on selective 1-term queries and loses on multi-filter | The argument is restricted to that profile; the recommendation becomes "specific cases" |
| W1 overhead > ~the gain from eliminating the second write (`WA_relative` difference ≥ 1 or worse p99) | The **write path** argument falls |
| W2 with overhead that does not hide behind the page cache | The "cheap passthrough" argument falls; it becomes a cost accepted by those who need the query |
| W6 grows linearly up to minutes at S6 (cold cache) | The **recovery across sessions** argument weakens; it requires a persistent index (the decision not to persist falls) |
| 128 MiB cover fewer than ~tens of thousands of memories in the 8/16 profile | The **scale** argument falls for the default budget; it only holds with a larger budget (non-pageable RAM) |
| W5/W8: leakage, `WARNING` or degraded write p99 under broad queries (global lock) | The **concurrent multi-agent** and isolation argument falls until fixed |
| Cost of authorization (query as root versus restricted user) ≫ cost of the index | The "free authorization" gain disappears; it only holds if the cost is lower than an equivalent authorization layer in the application (measure that too) |
| An unmodified real client fails or degrades (ai-memory, Letta, mem0) | The **adoption without modification** argument falls |

---

## 8. Success criteria

Thresholds justified by operational cost and workload profile, not by arbitrary percentages.
All measured in the harness (`benchmark-harness.md`): minimum 7 repetitions, alternating
order, dispersion >20% marks "inconclusive", each table cites commit, command and
`results.csv`.

### 8.1 Technical feasibility

- `bench/equiv` equivalence kernel × SQLite × oracle on 100% of the queries; no truncated
  result counted as complete.
- Zero `BUG`/`Oops`/`WARNING`/KASAN/lockdep across the whole suite and in M4.
- `GET_INFO` consistent with the actual count (difference between `index_used` and actual RAM measured and
  documented, or accounting fixed).
- Exit criterion: the plan's four questions answered by numbers, **including where the
  layer loses**.

### 8.2 Performance advantage

- The threshold is not "X% faster": it is **p99 of a W3 query below the latency budget of the
  agent's call** (the query precedes an LLM call that costs hundreds of ms to
  seconds, so differences of microseconds are irrelevant to the user) **and** no worse
  than the most mature baseline. A relevant gain requires a difference that survives the dispersion
  (p5-p95 across repetitions) in at least two profiles and two scales.
- For writes: `WA_relative` < 1 with equivalent durability, or W1 p99 overhead within
  what the client already tolerates (memory writes are usually asynchronous to the agent's flow).
- For W2: passthrough overhead reported as a ratio with an interval; acceptable if the
  uncertainty is smaller than the normal variation between ext4 and btrfs on the same hardware.
- For W6: mount time at S6 comparable to the time the operator already tolerates for restarting the
  service (e.g. tens of seconds), with an explicit limit in files per second.

### 8.3 Production maturity

Tied to `docs/security-roadmap.md`: S1 (threat model), S2 (xfstests), S3 (syzkaller),
S4 (hostile lower), S5 (static analysis), S6 (disclosure), S8 (hardening), S9 (limits and
DoS), S10 (signing/DKMS); plus continuous KASAN/lockdep on the active branch and a matrix of at least
two kernel series. Without this, no production recommendation.

---

## 9. Technological maturity as an adoption criterion

### 9.1 Principle

Adopting MoobFS has to overcome **not only the performance of the alternatives, but the risk and cost of
introducing a kernel module**. The maturity of SQLite, PostgreSQL, ext4, btrfs and XFS is a
concrete advantage of theirs, not a detail. A small performance gain **does not justify** a
kernel module: the price of a defect in ring 0 (host panic, corruption of the lower
tree) is disproportionate to that of a defect in a process. MoobFS is not credited with what
Linux or the lower already offer, and SQLite is not penalized with a bad configuration: WAL,
composite indexes and `ANALYZE` are assumed, as in `benchmark-harness.md` §4.

### 9.2 Eight factors

**1. Maturity and track record.**
- SQLite: more than two decades, ubiquitous, extensive test suite (TH3, branch coverage
  close to 100%, continuous fuzzing), operation at immense scale. ext4/XFS/btrfs: years of
  production, xfstests, maintainer reports, community fuzzing and distribution CI.
- MoobFS: months, one author, one kernel (6.12.111, plus 6.12.112 on demand), three kernel
  defects found by its **own** tests, no external users, no independent
  reproduction. Proven.
- Verdict: broad advantage for the alternatives.

**2. Risk surface.**
- SQLite: a failure brings down one process (the application's); the data is in a file with a
  documented format and a checking tool (`PRAGMA integrity_check`).
- MoobFS: a failure can cause a host panic, a mount hang, or corrupt/lose data
  in the lower tree (defect 1 was a silent loss). A defect triggerable by an unprivileged user
  is a DoS of the whole host. Blast radius: the host and all services on it.
- Verdict: MoobFS's risk is greater in magnitude and lesser in testing. Proven by the nature of
  ring 0 and by the history of the three defects.

**3. Maintenance.**
- SQLite: stable library, ABI and file format compatible for decades; updated
  with the application's package.
- MoobFS: the kernel's internal API is not stable. There has already been a divergence between 6.12.111 and
  6.12.112 (signature of `backing_file_open`); the 6.x series changed `backing-file`, the
  signature of `inode_permission`/`mnt_idmap` and the ACL API (`get_acl`/`set_acl` to
  `vfs_get_acl`/`vfs_set_acl`, with `struct mnt_idmap`) across several versions. Each distribution
  kernel update requires recompiling (DKMS) and revalidating; DKMS failures leave the host without
  a mount or prevent dependent services from booting. Secure Boot requires signing (MOK) and
  lockdown; S10 of the roadmap is not done. No kernel matrix beyond a single point.
- Verdict: significant ongoing maintenance cost and no people to absorb it.

**4. Security.**
- Reference class: overlayfs and ecryptfs (also stacked) have accumulated relevant CVEs
  (2015-1328, 2016-1576, 2020-16120, 2026-74619 in overlayfs; 2009-2406 and 2014-9922 in
  ecryptfs, according to `docs/history/0017`). This is evidence that stacked systems have a
  historical rate of vulnerabilities, even with years of public review.
- SQLite: failures occur, but contained in the process; permissions and isolation are the OS's over
  a file.
- MoobFS: mitigation by choice (userns and idmap refused) and input validation; but no
  syzkaller, no fstests, no written threat model, no external audit. Host impacted
  on any failure. Auditing/logging: none beyond `dmesg`.
- Verdict: alternatives safer today; MoobFS has an interesting authorization design,
  not yet under adversarial review.

**5. Reliability.**
- SQLite: decades of production, well-studied power-loss recovery, transactional
  atomicity. Lowers: well-studied journal and recovery.
- MoobFS: dm-log-writes at five marks in one VM, ext4; `dm-flakey` on ext4; no prolonged
  run; no index out-of-memory test at scale; persistence reliability is that of the lower,
  but the stacked path (dentries, mmap, locks) does not have the same
  history.
- Verdict: inherited persistence is solid; the layer adds risk, not reliability.

**6. Observability.**
- SQLite: `EXPLAIN QUERY PLAN`, `sqlite3` shell, `PRAGMA`, statistics, broad ecosystem.
  Filesystems: `/proc`, `perf`, tracepoints, `lock_stat`.
- MoobFS: `GET_INFO`, `moobfs-check`, `dmesg`. No latency metrics, no query
  count, no authorization omission rate, no exposed mount time. No tracepoints
  of its own. Diagnosing slowness depends on external `perf`.
- Verdict: MoobFS's observability is minimal; that is a real operational cost.

**7. Total cost.**
- SQLite: install (nothing), update (with the application), monitor (well known), support
  (vast community), training (SQL is universal), incidents (contained).
- MoobFS: install a module (DKMS, signing), update at every kernel, monitor (needs to be
  built), support (one author), training (own UAPI, lower rules, conventions),
  incidents (risk to the host). Plus the cost of the layer's **future operations tools**:
  `moobfs` CLI, provisioning, bindings, packaging, per-kernel test pipeline: all
  nonexistent and the responsibility of the adopter or of the project.
- Verdict: higher total cost today; it must be paid for by a benefit that has not yet been measured.

**8. Reversibility.**
- MoobFS: **strong point proven by design and tests**. Unmounting leaves ordinary files with
  xattrs readable by any tool; there is no format of our own; `moobfs-check` validates. Leaving
  MoobFS is `umount`.
- SQLite: the database file remains readable by any `sqlite3`; reversal is also
  simple, but the content is inside a single file with its own (documented and
  stable) format and the memory data is only read via SQL. Plain files: trivial reversal.
- Verdict: technical tie with plain files; MoobFS beats SQLite on "readable without
  any specific tool", but SQLite is readable by a universal tool. Not a
  decisive advantage.

### 9.3 Mandatory separation of what exists and what does not

| Dimension | Situation |
| --- | --- |
| Measured performance gain | **none** (zero numbers) |
| Demonstrated productivity gain | **none** |
| Actual architectural simplification | **plausible, not measured**: one store instead of files + database; no daemon; single access policy |
| Additional maintenance risk | **certain**: out-of-tree module, one kernel, DKMS, unstable API |
| Additional security risk | **certain**: ring 0 code with ioctl and xattrs accessible to users; no kernel fuzzing, no written threat model, no audit |
| Operational cost | **higher today**: installation, signing, minimal observability, no tools |
| Production maturity | **nonexistent**: one author, one VM, three defects found internally |

### 9.4 Architectural capabilities that could justify adoption even without beating SQLite on every benchmark

1. **POSIX authorization per query result, across many uids, without an authorization layer in the
   application**: the policy is the OS's (mode, group, ACL, traversal), a single one. Reproducing this with
   SQLite requires the application to reimplement permissions or each uid to have its own database
   (losing cross queries). It is worth it if the number of uids/agents is high and the policy changes.
2. **"No lock-in" data, readable by every Unix tool** (git, grep, rsync, backup,
   reflink), with the same collection queryable by attributes. It is worth it when auditing and standard
   tools are a requirement.
3. **No daemon or database process per host**: a module and the filesystem itself.
   It is worth it when the cost of operating a service per host is greater than that of maintaining a module (rare
   with embedded SQLite, but relevant against PostgreSQL).

None of these is performance. Each has to be compared with its cost (a kernel module) and only
counts if there is a scenario that requires it **and** evidence that the implementation is safe.

### 9.5 Decision matrix by scenario

| Scenario | Indispensable evidence before recommending MoobFS | Verdict today |
| --- | --- | --- |
| Lab / research | Complete M4 (W1..W9) with a report that includes where the layer loses; xfstests `quick` over the layer; clean KASAN over the branch; `chmod`/`rename` concurrency test with `QUERY` passing | **Acceptable**: experimenting in a disposable VM is the intended use; recommendation to use it to obtain numbers and feedback, not as storage for valuable data |
| Controlled production, expert users | All of the above, plus: two or more kernels in CI; syzkaller over ioctl/xattr with defined coverage; threat model (S1) and disclosure process (S6); DKMS and signing (S10); independent reproduction of M4; a pilot of months without data loss; minimal observability (metrics, tracepoints); recovery runbook | **SQLite or plain files remain the recommendation today.** MoobFS is only justifiable in this scenario if capability 9.4(1) is a real requirement, with an isolated pilot and a reversal plan (which exists by design) |
| Critical enterprise production | All the criteria of 9.6 satisfied; external security review; contractable support; matrix of supported distribution kernels; a track record of 12+ months without a data-loss bug | **SQLite (or PostgreSQL) and plain files remain the recommendation today, without reservation.** Introducing a single-author kernel module in this context is not defensible |

### 9.6 Closing of the section

**Real advantages of the mature alternatives.** Decades of production and testing (SQLite,
lowers); full SQL (OR, ordering, aggregation, join); stable format portable across
operating systems; no root, no module, works in rootless containers, in hosted CI and
on macOS/Windows; rich observability; broad support and training; failures contained in the
process; atomic transactions over table and index.

**Specific risks of adopting MoobFS.** Host panic or data loss from a module
defect; DoS by an unprivileged user; breakage at every kernel update; dependence on one
author; impossibility of use outside Linux with root; index limited by non-pageable RAM and
without persistence; silent incomplete answers near the budget; minimal observability;
timing side channel not assessed.

**Evidence already present in the repository.** Correctness of `QUERY` answers against an oracle
(minimal scale); isolation by uid/ACL on three lowers (test scale); git, SQLite WAL and
our own client running without modification; crash replay at five marks; disk errors
propagated; refusal of userns/idmap tested; three kernel defects fixed with regression
tests; fuzzing of the encoding parser.

**Validation gaps.** All of M4; full xfstests; KASAN over the current branch and continuous
cycles; syzkaller; `chmod`/`rename` concurrency test with `QUERY` (written, not
run); timing channel; index saturation at scale; more than one kernel; independent
reproduction; external audit; real agent clients; long-running operation.

**Conditions under which MoobFS would be recommendable.** Many uids/agents on the same Linux host
with root available, a need for cross queries by attributes with a single OS access policy,
a requirement for data readable by Unix tools, a corpus within the index budget,
and M4 showing query and write at least equivalent to SQLite, **plus** the criteria
below met.

**Conditions under which SQLite or plain files remain the recommendation.** Single agent or
single trusted application; any scenario without root or without control of the kernel (hosted CI,
rootless, macOS/Windows, serverless); need for OR, ordering, aggregation, text search
or vector search; large corpus; critical enterprise environment; team without the capacity to maintain a
module; any case in which the gain is only a fraction of performance.

**Objective criteria for moving from an experimental project to a trustworthy technology.**
1. xfstests: complete pass (the `generic` and `quick` groups applicable to stacked filesystems),
   with a justified exclusion list, on the three lowers.
2. KASAN/UBSAN/lockdep in continuous CI on the main branch, with no diagnostics for a defined
   period, and syzkaller cycles with `layer/` coverage above a threshold and zero
   open reports.
3. At least two supported kernel series (for example, two LTS) in CI, plus a distribution
   kernel with DKMS and Secure Boot validated.
4. Twelve months of real use without a data loss or corruption bug, in at least two
   independent environments.
5. Independent reproduction of the M4 results by third parties, on other hardware.
6. Items of `security-roadmap.md` completed: S1 to S6, S8, S9, S10 (at minimum).
7. External security and code review of the module, published.
8. Maintainers beyond the original author.

---

## Deliverable

### The five greatest potential advantages

1. Query by attributes whose authorization is the OS's (mode, group, ACL) per result, without a
   separate authorization layer in the application (proven at test scale; cost not
   measured).
2. One store instead of files plus a database, eliminating the second write and the problem
   of keeping both consistent (potential).
3. Data readable by any Unix tool without the layer; reversal via `umount`
   (proven by design and tests).
4. No database process per host (proven by design; real advantage above all against
   database servers).
5. Full reuse of the persistence, reflink, dedup and ACLs of ext4/btrfs/XFS (inherited, not its own
   merit, but it makes the project cheaper).

### The five greatest limitations

1. No performance measurement exists; every speed advantage is a hypothesis.
2. Kernel module: risk of panic and data loss, DoS by an ordinary user, maintenance
   per kernel version; three defects found by its own tests, one of them data loss.
3. Index in RAM only, not persistent, rebuilt by a linear scan; the 128 MiB budget
   covers on the order of 10^5 inodes (estimate); near the ceiling, silent incomplete answers.
4. Limited query (conjunction of up to 4 terms, no OR/ordering/aggregation) and linear scan
   of the first term, without a composite index.
5. Linux with root and initial `CAP_SYS_ADMIN`; no rootless container, userns, idmap; no CLI,
   bindings, provisioning, observability; one author, one kernel.

### Honest comparison

| Criterion | Plain files | SQLite alongside the files (WAL, composite indexes, ANALYZE) | MoobFS |
| --- | --- | --- | --- |
| Query by attribute | `find` + `getfattr`, linear | Composite index, full SQL (proven, mature) | Conjunctive `QUERY`, scan in RAM (correct; speed not measured) |
| Query authorization | POSIX per file, no query | In the application | POSIX per result (proven in tests) |
| Read latency by path | Reference | Reference (file) | Reference + VFS hop (certain, magnitude unknown) |
| Memory write | Reference | File + row (2 writes) | File + xattr + RAM (potentially lower amplification; not measured) |
| Recovery across sessions | Nothing to rebuild | Opens a file | Linear scan at mount |
| Memory | None | Page cache, pageable | Non-pageable index (estimate ~0.3 to 2 KiB/inode) |
| Maturity | Maximum | Decades | Months, one author |
| Failure risk | Minimal | Process | Host |
| Installation | None | Library | Module, root, signing |
| Portability | Total | Total | Linux 6.12 in a test VM |
| Reversibility | Total | High (file with a stable format) | Total (proven) |
| Observability | `ls`, `getfattr` | `EXPLAIN`, shell | `GET_INFO`, `moobfs-check`, `dmesg` |
| Data × metadata atomicity | No | Yes (within the database) | No (client convention) |

### Cases in which MoobFS may win

Many agents with distinct uids on a Linux host with cross queries under a single POSIX policy; a collection auditable by git/grep/rsync and queryable by attributes; markdown-based frameworks that want to avoid a database just for simple filters; hosts with root and a pinned kernel.

### Cases in which it is probably not worth it

Single agent; single trusted owner; environments without root (hosted CI, rootless, serverless, macOS/Windows); need for OR, ordering, aggregation, text or vectors; large corpus; critical enterprise production; teams without the capacity to maintain a module.

### Indispensable M4 measurements

W3 (p50/p95/p99 on all forms Q1..Q9 with the `EXPLAIN` gate, including the isolated cost of
authorization); W1 and `WA_relative` with equivalent durability; W2 (passthrough
overhead); W6/W7 at S1..S6 (cold and warm cache); saturation 50..100% of the budget with
actual RAM versus `index_used`; W5/W8 (concurrency and global lock); an unmodified real client
(ai-memory or equivalent); concurrent `chmod`/`rename` with `QUERY` under KASAN; timing channel
of the authorization.

### Technical value proposition (without unproven claims)

MoobFS is an experimental kernel module that stacks an in-RAM index of typed attributes
over ext4, btrfs or XFS. The files remain ordinary files with xattrs, readable without the
layer. A query via ioctl returns only what the caller would be allowed to open through the
VFS, including via ACL. Persistence, the journal, reflink and dedup belong to the lower. There is no
database process. So far, what has been demonstrated is the correctness of the answers and
isolation at test scale; performance, real scale and prolonged operation have not been
measured.

### Grounded conclusion

The problem that MoobFS intends to solve better than the alternatives is narrow: **query
by attributes with per-result POSIX authorization across many Unix users, keeping the data
as ordinary files, without a separate authorization application and without a database server**. This
proposition is coherent with the design and has evidence of correctness at test scale, but
**is not yet demonstrable as a real advantage**: there is no measurement at all showing that it
costs less (in latency, in writes, in memory, in operation and in risk) than SQLite with
composite indexes plus authorization in the application, and the entry price (a kernel module from one
author, on one kernel, with three defects already found) is high. Today, for any scenario that
is not a lab, **SQLite or plain files are the recommendation**. MoobFS deserves to
continue as research until M4 and the criteria of 9.6; a result in which the layer shows no
advantage on any of the plan's four questions is valid and must change the project's course, as
ADR-0007 already provides.

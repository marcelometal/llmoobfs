# ADR-0009 — Role of `QUERY` and structured index in user space

Date: 2026-10-11. Status: **accepted by the maintainer on 2026-10-11: alternative E, with the
adjustments listed in "Decision"**. Alternative E added at the maintainer's request the same day; the
"Proposal" section describes alternative B, which is rejected as the filesystem's direction and allowed
as an optional layer of a client. Nothing implemented; the specification is in
`docs/spec-query-v2.md`.

## Decision

Alternative E is adopted: the index stays in the kernel and becomes partitioned by permission
domain, without a database engine. Adjustments accepted along with it:

1. **Domain = homogeneous namespace.** Each domain is a namespace directory; every file
   indexed in a domain has the same set of readers as the domain's directory. The layer does not
   index (and flags) what violates the rule, instead of partitioning by reader set.
2. **`QUERY` on the domain's directory**, in a new UAPI version: the kernel authorizes once the
   traversal to the domain with the caller's credentials and searches only in its partition.
3. **"The N most recent"**: ordering by a time attribute with a limit, walking that attribute's
   tree in the partition.
4. **Small intersection** of two or more equality conditions, without a planner.
5. **Two small extensions**: batch query (several conjunctions in one call, for the "or"
   between values) and the "missing or comparison" operator (validity and expiration).

What changes in earlier decisions: item 2 of ADR-0007 continues (the layer provides the index and
`QUERY`), with `QUERY` redefined as a per-domain query; item 3 of ADR-0005 continues (index
maintained by the module), now partitioned. ADR-0008 remains on hold: the partition reduces the
problem it attacked. ADR-0001 does not change. If accepted, it partly replaces
ADR-0005 (item 3, indexes maintained by the module, and the "SQLite beside it" alternative that it
rejected) and redefines `QUERY` within item 2 of ADR-0007. ADR-0001 (native mountable
filesystem) remains in force.

## Context

**Why `QUERY` exists.** ADR-0005 took the Be File System as reference: typed
attributes per file, per-attribute indexes and queries served by the filesystem itself. The
alternative "opaque metadata indexed in user space, SQLite beside it" was rejected
there for "making the native filesystem a mere byte repository", against ADR-0001. ADR-0007
kept `QUERY` as part of what the layer provides and rejected "user-space library or daemon
with xattr and SQLite" for not being a mountable filesystem and for removing the property that
any program queries memories through a kernel interface. The choice was one of design, inspired
by BFS; no measurement supported it, and ADR-0007 itself made the direction conditional on M4.

**What the pilots and the scale series showed** (journals 0025, 0027, 0028; scale series
of 2026-10-11 on the Azure host):

- Query by one selective condition: `QUERY` ties or beats SQLite with a composite index
  (with no results, 5 µs against 19 µs).
- Cost per result: about 1.7 µs against 0.2 to 0.4 µs per row in SQLite, because of
  path resolution with the caller's credentials.
- Queries with more than one condition: `QUERY` only searches by the first condition and tests the
  others on each candidate, at about 1 µs per candidate. The time grows with the size of the
  set: in the shape `namespace = X AND agent = Y`, 34 µs with 1000 memories, 1.1 ms with 10 000
  and 6.95 ms with 60 000, against 15, 33 and 392 µs in SQLite (journal 0029), that is, from 2 to 18 times
  slower depending on the size.
- UAPI v1 limit: at most 4096 results per call, without pagination; with 10 000 memories
  one of the nine shapes is already truncated, and with 40 000 or more, five of the nine.
- Index: 600 bytes accounted per memory with 8 attributes (36 MB with 60 000); rebuild
  at mount of about 19 µs per memory.

Closing these differences inside the kernel would require choosing the most selective condition, crossing
candidates, maintaining statistics, composite indexes and pagination: a query planner. That
is rewriting in the kernel what SQLite has done for two decades, against the principle of journal 0009,
and at a higher cost than in user space (risk of hanging the host, attack surface,
maintenance across kernel versions).

## What only the kernel guarantees, according to the measurements and the tests

1. **Per-result authorization with Linux permissions**, without an authorization layer in the
   application nor an intermediate privileged process.
2. **Consistency without double writing**: the index changes in the same call that writes the attribute.
3. **Nothing locked in**: unmounted, the content remains files with attributes.

## Proposal

Divide the role into three parts, each with the most mature software for it:

- **Kernel (the layer):** validated typed attributes (as today), the current `QUERY` frozen at
  what it already does (up to four terms, search by the first condition, no planner), and two new
  pieces, only if M4 shows a need:
  - **change stream** to keep an external index up to date. First use what Linux already
    has: `fanotify` with `FAN_ATTRIB`, `FAN_CREATE`, `FAN_DELETE` and `FAN_MOVED_*`, reporting
    file identifiers (`FAN_REPORT_FID`). A cursor of our own and durable (in the style of the
    NTFS change journal) only comes in if `fanotify` is not enough, because it does not survive a
    restart and may lose events under load (`FAN_Q_OVERFLOW`, which requires a new scan);
  - **batch authorization**: a call that receives a list of file identifiers and
    returns which the caller can read, with the same contract as phase 3b.
- **User space:** the structured index, queries with several conditions, pagination and
  planning, in a mature engine (SQLite as the first option).
- **The client** queries the index and passes the results through batch authorization before using them.

## The hard point: where the index lives and who can read it

A shared index in user space contains metadata of all agents (paths,
attributes). If any agent can read the index file, per-result authorization stops
protecting the metadata, only the content. The known ways out are:

- **a privileged indexer process** that maintains the index and answers queries on behalf of
  each caller (identifying it by `SO_PEERCRED` and authorizing through the kernel). It is a service, which
  the project has avoided so far;
- **one index per permission domain** (for example, per namespace), with the same permissions
  as the directory it indexes. No service, but no cross-domain queries;
- **keep `QUERY` in the kernel for what needs isolation** and use the external index only
  within a single domain.

The proposal does not choose among them; M4 measures the ones that are viable.

Precedents found in the research (`docs/research-metadata-search.md` and the addendum
`docs/research-metadata-search-addendum.md`):

- **GUFI** (Los Alamos, BSD-3, active): one SQLite database per directory, with the owner and mode of the source
  directory; the query tool runs with the user's real uid on a dedicated server (access
  via ssh to a restricted shell) and the kernel refuses to open what it cannot read, without a
  privileged filter. Attributes that not every reader may see go to per-uid and per-gid databases. Verified
  limits: it does not replicate ACLs, and updating is by periodic full scan (every 4
  hours at LANL), not by events. In 2026 LANL presented the same model for AI context retrieval
  with access control ("Access Controlled AI Context with GUFI").
- **Windows Search:** one index per machine, in a system service, with index files
  readable only by the system and results cut by each file's ACL at query time. It is the
  "privileged indexer that answers on behalf of the caller" form.
- **GNOME LocalSearch (Tracker):** one index per user, in a service of the user themselves,
  fed by unprivileged `fanotify`, with `inotify` as an alternative.
- **Parker-Wood et al. (MSST 2010):** partitioning by change of owner or permission produced 29 479
  partitions with an average of 234 entries on a server with 6.9 million files: "one index per
  domain" may mean many small indexes.

Applied here, the simplest form is one index per namespace, with the permissions of the namespace's
directory, written by a privileged indexer fed by `fanotify` that never answers
queries; each agent reads only the indexes it could read. Open issues: ACLs (not even GUFI solves them),
idmapped mounts, and the window between a `chmod` and the index reflecting the change.

## Alternatives

- **A. Keep as it is.** The current `QUERY`, without growing. Simple; it loses on queries with several
  conditions and with many results, and the 4096 limit stays as a declared restriction.
- **B. This proposal.** Index and planning in user space; kernel with authorization and
  change stream.
- **C. Planner in the kernel.** Statistics, composite indexes, optimizer. Rejected: it is a
  database in the kernel.
- **C'. Small intersection in the kernel, without a planner.** The entries are already sorted by
  (type, value, inode) and full-key search already exists (`first_after`). Two or more
  equality conditions can be crossed by advancing through the lists in parallel (leapfrog), without
  allocation. It attacks exactly the measured case that grows with the size of the set; it does not change the cost
  per result. It is compatible with A and with B; it needs a specification of ordering and resumption before
  code. Haiku's BFS itself does not cross lists: it chooses one condition by a cheap
  score (index size) and tests the others on each candidate, as MoobFS does today with the
  first.
- **E. Index partitioned by permission domain, in the kernel, without SQLite.** The in-RAM index
  comes to have one partition per domain (for example, per namespace, that is, per
  first-level directory chosen at mount). `QUERY` comes to be issued on a descriptor of that
  directory, not of the mount root: the kernel authorizes once the traversal to the directory with the
  caller's credentials and searches only in its partition, checking only `MAY_READ` on each
  file. Added to the small intersection (C'), it solves the three measured problems without a database engine:
  it closes the timing channel (the search never walks entries of other domains), reduces the cost per
  result (the path to the domain has already been authorized) and limits per-candidate growth to the
  size of the domain, not of the mount. It keeps the index updated in the same call that writes the
  attribute and does not require an extra process. It does not offer queries with "or", ordering, aggregation or
  pagination beyond the result limit: the language stays simple by choice. Costs:
  a new UAPI version (`QUERY` on a directory), migration of entries between partitions when a
  directory changes domain by rename, per-partition budget accounting, and the same
  isolation, concurrency and KASAN tests as phase 3b. It remains a data structure, not a
  database: it is close to what BFS did, with the authorization BFS never had.
- **D. Port Haiku's BFS query engine** (OpenBFS, MIT license). Rejected: C++ and Haiku
  kernel API, tied to the BFS on-disk format, and it solves the same problem as
  alternative C, only with code foreign to Linux.

## Mature engines considered for the user-space index

No mature query engine runs inside the Linux kernel: modules cannot use user-space
libraries (libc, allocator, threads, floating point), and the kernel structures
(rbtree, xarray, maple tree, rhashtable) are building blocks, not query engines. The
industry practice for indexing file metadata is a user-space index plus a kernel change
stream: Spotlight with FSEvents on macOS, Windows Search (ESE) with the NTFS USN
journal, GNOME Tracker (SQLite) and KDE Baloo (LMDB) with inotify on Linux.

| Engine | What it offers | Fit here |
| --- | --- | --- |
| SQLite | SQL, composite indexes, planner, transactions, extensive tests | First option: covers queries with several conditions without writing a planner |
| LMDB | Memory-mapped B+ tree, lock-free readers, very mature (OpenLDAP, Baloo) | Faster reads, but it is key-value: composite indexes and planning are up to us |
| RocksDB, LevelDB | LSM trees, write-intensive | No advantage for per-attribute reads; more operations work |
| DuckDB | Columnar analytical queries | Designed for aggregation, not for low-latency point lookups |
| Xapian, Tantivy | Text search | A different problem; stays with the client (ADR-0005, item 5) |

### Correction to alternative E from the literature

The partition unit that closes the timing channel is **the set of who can read**, not a
directory. Singh, Srivatsa and Liu (ICWS 2007) partition files by reader set and
prove it is the minimum necessary; on a real system of 339 466 files, 926 users and 1203
groups, this gave 2132 partitions, and each user queried 6.31 on average. Lethe (Transactions on
Data Privacy, 2015) found 1475 distinct permission sets in 50 000 documents. If a
namespace contains files with different readers, the timing channel comes back within the partition.
Therefore E needs one of two rules: partition by (namespace, reader set), or
namespaces declared homogeneous (every file with the same readers as the namespace's directory),
with the layer refusing or flagging what violates this. The second is simpler and matches the
pattern of real clients (one owner per scope). Another known reduction: a separate index for
what everyone can read (Google's Goods indexes only the datasets readable by all
engineers) and partitions only for what is private.

## Comparison between B and E

| Criterion | E: partitioned index in the kernel | B: SQLite in user space |
| --- | --- | --- |
| Timing channel between domains | Closed | Closed, with one index per domain |
| Complex queries ("or", ordering, pagination) | No | Yes |
| Index updated immediately | Yes, in the same call | Depends on `fanotify` and on rebuilding after event loss |
| Extra process | None | Privileged indexer (or one per user) |
| New kernel code | Medium (partitions, UAPI v2, intersection) | Small (batch authorization) |
| ACLs | Respected, as today | Require a copy or a check in the kernel; GUFI does not solve it |
| Coherence with ADR-0001 | Full | Partial: the index stops belonging to the filesystem |
| Mount with cold cache (journal 0031: 126 s with 60 000 memories on a P6 disk) | Can rebuild per domain, on demand, at the first query to each domain | The SQLite index is already on disk; only lost events require a scan |

Proposed choice criterion: if the queries that real memory clients make are
simple conjunctions over a domain, E serves without a database and without an extra process; if they need
rich queries, B. The two can coexist: E as the filesystem interface, B as an
optional client layer for queries that E does not cover.

**What real clients do** (code reading, `docs/research-client-query-patterns.md`:
ai-memory, mem0, Letta, Graphiti, LangGraph and LangMem, OpenAI Agents SDK):

- The scope is almost always one to three equalities (user, agent, run, workspace) or a
  namespace prefix, imposed by the application. It is exactly the domain of alternative E;
  Graphiti on FalkorDB already uses one graph per group and does one search per group, the shape of E.
- The second filter is time: creation and validity ranges, often "missing or greater than"
  (`expires_at IS NULL OR > now`), which the current language does not express without a sentinel value.
- Every listing orders by time with a small limit (defaults of 3 to 50, maximums of 100 to 200).
  With the per-domain partition, "the N most recent of the domain" becomes walking backwards
  the time attribute's tree of that partition: cheap and natural in E, without a planner.
- "Or" between values appears in 4 of 6 systems (roles, tags, types, groups); in E it requires one
  query per value and a union in the client, or a `QUERY_BATCH`.
- Counts appear only for pagination and administration, not in retrieval.
- The structured filter always runs inside the same engine that ranks by vector or
  text similarity. Neither E nor B replaces that engine: the filesystem index only serves the client as a
  candidate pre-filter or as the source of truth from which the vector index derives, and the
  main value for these clients is permission isolation, not query speed.
- ai-memory is already form B (markdown as source of truth and derived SQLite) and had to
  write the authorization inside the SQL, valid only for reads through its own server.

Reading: E covers the scope and the time ordering with a limit within the domain; it does not cover "or"
without several queries nor the "missing or greater than" validity. These two gaps are small and
specific (batch query; "missing or comparison" operator), much smaller than a
planner.

## What the research adds

- **Restrict before computing.** Büttcher and Clarke (FAST 2005) show that filtering by
  permission after querying leaks information through counts and scores. `QUERY` does not return
  a count or a score for what the caller cannot read, but the timing channel was **measured**
  (journal 0030): about 1.15 µs per invisible candidate, from 18 µs with none to 28 ms with
  20 000, with dispersion below 3%. An agent estimates how many of others' memories match a
  predicate and tests the existence of a value. Only a query that does not walk entries of
  other domains closes the channel: it is the strongest argument in favor of the index partitioned by
  domain, in the kernel or outside it.
- **The change stream already exists** (journal 0030): `fanotify` with a filesystem mark on the
  lower and `inotify` on layer folders receive all changes made through the layer. The
  layer refuses `fanotify` marks with file identifiers because it has no export
  operations.
- **There is no durable change cursor on Linux.** The change journal proposals
  discussed at LSFMM 2018 and 2019 did not become code; of Goldstein's work only
  `FAN_PRE_ACCESS` (6.14) was merged. Lustre has a durable changelog, but reading and clearing it requires root. Before creating one, use `fanotify` and
  rebuild the external index at mount and on `FAN_Q_OVERFLOW`; a cheap step is to expose in
  `GET_INFO` a mount epoch and a monotonic change counter, so the consumer knows
  it lost continuity (requires a UAPI specification).
- **Landlock is not consulted** (reproduced, journal 0030): under Landlock `QUERY` returns
  files the process cannot open, but only names and attribute values that the process would already
  obtain by listing folders and reading attributes, because Landlock does not control xattr reads.
- **`setxattr` generates `FS_ATTRIB`** (`fsnotify_xattr` in 6.12), confirmed on the layer and on the lower
  (journal 0030).

### What the academic survey adds (`docs/research-academic-survey.md`)

- No published work puts an attribute index inside the Linux kernel with per-result POSIX
  authorization, and none measures a timing channel like that of journal 0030. The only search
  interface in the Linux kernel found is a Notre Dame technical report (2013), without an index.
- Leaking counts is accepted practice in industry: Wang et al. (IEEE S&P 2017) extracted terms and
  counts of other customers' documents on GitHub and Xen.do; the current Elasticsearch
  documentation admits that users count documents they cannot read. Only a search that does not go
  through other domains closes the channel.
- The cost per result can fall without partitioning: SPMeta (ICDE 2026) keeps per directory one
  bit per class (owner, group, others) saying whether it reaches the directory, invalidated on `chmod`,
  with a gain of 1.44 to 2.86 times; it does not handle ACLs. It is a complement, not a substitute, for the partition.
- B inherits the "new enemy" problem described in Zanzibar (USENIX ATC 2019): between a `chmod` and
  the external index updating, the index may deliver names and attributes to someone who just lost
  access. If B goes ahead, the client needs to compare a monotonic change counter before
  using the index, and batch authorization must refuse when the index is behind.
- Measurements of batch authorization exist: Bailey, Hawking and Matson (CIKM 2006) measured 0.11 ms
  per local check and recommend per-collection checking and batches of 100 to 1000.

## What M4 needs to measure to decide

- E against A in the W3 shapes restricted to one domain, per scale, including the timing channel with
  entries of other domains present (the experiment of journal 0030), and the cost of renaming
  a directory between domains.
- Which queries a real memory client makes, with the client running over the layer: whether they fit
  in simple conjunctions over a domain.
- B with SQLite against A in the nine W3 shapes, per scale, including the cost of batch
  authorization and the delay between writing an attribute and the external index reflecting the change.
- Whether `fanotify` delivers all attribute events made through the layer (to be confirmed: the
  calls go through the VFS of the layer and of the lower) and the cost of rebuilding the index after
  `FAN_Q_OVERFLOW`.
- Which of the three ways of storing the index is viable without weakening isolation.

## Consequences if accepted

- The current `QUERY` stops growing; only efficiency improvements, without a planner.
- ADR-0008 (reusing per-directory authorization) comes to apply also to batch
  authorization.
- The M3 spec (§5, change cursor) is revised: the change stream stops being "postponed without
  demand" and becomes a necessary part of the architecture, starting with `fanotify`.

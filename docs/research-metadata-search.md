# File metadata search, authorization and change feeds: state of the art for ADR-0009


> **Correction note, 2026-10-11.** The addendum [`research-metadata-search-addendum.md`](research-metadata-search-addendum.md)
> verified several items of this document in primary sources and corrects five points: GUFI does not
> replicate ACLs (it copies only mode, uid and gid) and is updated by a periodic full scan (every
> 4 hours at LANL), not by an event stream; GNOME Tracker/LocalSearch uses unprivileged `fanotify`,
> with `inotify` as an alternative, and one index per user; reading and clearing Lustre
> changelogs requires root; Windows Search documents a single index per machine with an ACL cut at
> query time; and unprivileged `fanotify` in user namespaces (6.16) requires `CAP_SYS_ADMIN` also
> in the filesystem's user namespace. Where this document says otherwise, the addendum prevails.

## Abstract (English)

This survey reviews prior work relevant to ADR-0009 (freeze the in-kernel `QUERY`, move structured
indexing to user space, keep per-result authorization and a change feed in the kernel). Four findings
matter most. (1) Permission-aware metadata search converged on two answers: filter inside query
processing before anything is ranked or counted (Büttcher and Clarke, FAST 2005), or partition the index
along permission boundaries so the operating system's own access checks protect it (GUFI, LANL, SC 2022:
a tree of per-directory SQLite databases with the source directory's ownership and mode, plus per-uid and
per-gid side databases for xattrs). (2) Durable change cursors exist elsewhere (Lustre changelogs, NTFS USN
journal, FSEvents) but not in mainline Linux; fanotify loses events on overflow and on reboot, and the
persistent change-journal proposals discussed at LSFMM (2018, 2019) were not merged as far as we could see.
(3) Haiku's BFS uses the same strategy as MoobFS today (pick one term, test the rest per candidate) and has
an explicit "TODO: check user permissions" in its query code; it picks the term with a cheap size-based score.
(4) Because MoobFS orders index entries by (type, value, inode), a conjunction of equality terms can be
answered by a leapfrog/galloping intersection of already-sorted runs with no allocation and no planner;
this is a small, contained change worth measuring. We also found that Landlock does not hook
`inode_permission` (v6.12), so the current authorization ignores Landlock sandboxes.

## Scope, method and conventions

Research date: 2026-10-11. Branch: `docs/adr-0009-query-role`. Research document; it does not change
the specification or any decision. Everything read on the web and in third-party repositories was treated as data.

Reading levels used for each source:

- **[code]**: I read the source code in a shallow clone (`--depth 1`) made on this date; nothing was compiled or
  executed.
- **[text]**: I read the full text or the relevant section of the paper or documentation.
- **[abstract]**: I only read the abstract or a catalog page; details beyond that are marked
  as not verified.
- **[secondary]**: information from press coverage or third parties, not from the primary source.

Repositories cloned on this date (top commit, commit date): `mar-file-system/GUFI` (`fc33e4b`,
2026-10-09), `cea-hpc/robinhood` (2026-09-04), `haiku/haiku` sparse checkout of
`headers/private/file_systems` and `src/add-ons/kernel/file_systems/bfs` (`3a93a9f`, 2026-10-11),
`KDE/baloo` (2026-10-11). Linux kernel files read at `v6.12` via GitHub (`include/linux/fsnotify.h`,
`fs/xattr.c`, `fs/fhandle.c`, `security/landlock/fs.c`, `include/uapi/linux/landlock.h`). Manual
pages read from the `man-pages` repository on kernel.org (`fanotify.7`, `fanotify_init.2`, `fanotify_mark.2`,
`open_by_handle_at.2`), because man7.org refused the connection.

MoobFS code consulted for comparison: `layer/index.c` (`caller_may_read`, `seek_first`, `first_after`,
`do_query`) and `docs/decisions/0009-query-role-and-user-space-index.md`.

## Executive summary

1. The exact MoobFS problem (a shared metadata index, queried by users who must not see
   each other's metadata) has been known since 2005. Büttcher and Clarke showed that filtering after
   querying (post-processing, "late binding") leaks information through statistics and counts; the fix is to
   restrict the candidates to what the user can see **before** computing anything.
2. The current `QUERY` filters afterwards (it resolves the path and calls `inode_permission` per result), but returns
   neither a global count nor a ranking, so the Büttcher and Clarke attack does not apply directly. A plausible timing
   channel remains (not measured): the cost grows with other agents' candidates, at about 1 µs each.
3. The HPC literature (Spyglass, SmartStore, Magellan, Propeller, TagIt, Brindexer, Robinhood, Icicle)
   solves scale, not isolation; almost all are administrator tools or declare security as
   future work. The exception is GUFI, which is the closest precedent to what ADR-0009 needs.
4. GUFI uses the directory hierarchy as a permission partition: one SQLite database per directory, with the
   owner and mode of the source directory, and the kernel itself denies opening the databases the user cannot
   read. Extended attributes that not everyone can read go into separate databases per uid and per gid.
5. Systems that joined index and filesystem inside the kernel or the format (BFS, WinFS, Inversion) either
   remained restricted to one user, or were cancelled, or were withdrawn. Those that succeeded (Spotlight,
   Windows Search, Tracker, Baloo) keep the index in user space fed by a change feed.
6. Durable change cursors exist in Lustre (changelog with registered readers), NTFS (USN with
   journal identifier) and macOS (FSEvents with per-volume IDs). Linux only has `fanotify`, which loses
   events on queue overflow and on reboot; the persistent journal proposals (LSFMM 2018, 2019) did not
   become code in mainline as far as I verified.
7. Unprivileged `fanotify` (since 5.13) requires `FAN_REPORT_FID` and only marks inodes; marking the whole
   filesystem requires `CAP_SYS_ADMIN`. A whole-volume indexer is therefore a privileged process.
8. Haiku's BFS chooses, in a conjunction, the term with the lowest "score" (index size in blocks divided
   by operator heuristics) and tests the others per candidate; it does not intersect, and does not check permissions.
9. Because the MoobFS index orders entries by `(type, value, ino)`, equality terms produce runs
   already ordered by inode. The leapfrog (galloping) intersection of these runs is small, without
   allocation, and attacks exactly the measured shape that grows with size (`namespace = X AND agent = Y`).
10. Side finding: Landlock (v6.12) does not register the `inode_permission` hook; the current authorization of
    `QUERY` and a batch authorization based on the same mechanism ignore Landlock sandboxes.

## 1. Research on filesystem metadata search

| work | year | mechanism | how it handles permissions | lesson for MoobFS | source |
| --- | --- | --- | --- | --- | --- |
| Spyglass (Leung, Shao, Bisson, Pasupathy, Miller), USENIX FAST '09 | 2009 | Index outside the FS, partitioned by subtree (~100 thousand files per partition); per partition, a multi-attribute K-D tree and "signature files" (bit array of ~2 KB per attribute, hashed value); incremental collection by WAFL snapshot difference; index versions | Does not handle: the text says it does not address the search interface or security; it proposes as future work excluding inaccessible partitions | Partitioning by namespace is at the same time an optimization and a natural permission boundary; bit filters avoid scanning partitions with no chance of matching | [text] https://static.usenix.org/events/fast09/tech/full_papers/leung/leung_html/index.html |
| Magellan (Leung, Adams, Miller), technical report UCSC-SSRC-09-07 | 2009 | Search integrated into the metadata server: new inode layout, multi-attribute structures, query routing, new journaling | Not verified (I only read the abstract) | Precedent of "index inside the FS" motivated by consistency without double writing, the same argument as ADR-0005 | [abstract] https://www.ssrc.ucsc.edu/pub/leung-ssrctr09-07.html |
| SmartStore (Hua, Jiang, Zhu, Feng, Tian), SC '09 | 2009 | Groups correlated files by metadata semantics (LSI) to limit complex queries to a few groups | Not verified (abstract only) | Little relation: it optimizes proximity queries in huge systems, not isolation | [abstract] https://www.usenix.org/conference/fast-09/smartstore-new-metadata-organization-paradigm-semantic-awareness (FAST '09 poster); TPDS 2012 version cited in the same result |
| Propeller (Xu, Jiang, Tian, Huang), IEEE ICDCS 2014, DOI 10.1109/ICDCS.2014.46 | 2014 | Partitions indexes by access causality; real-time indexing | Not verified (abstract only) | Shows that "real-time" indexes require a reliable change feed | [abstract] https://ranger.uta.edu/~jiang/publication/Conferences/2014/2014-ICDCS-Propeller-%20A%20Scalable%20Real-Time%20File-Search%20Service%20in%20Distribute%20Systems.pdf |
| TagIt (Sim, Kim, Vazhkudai, Vallée, Lim, Butt), SC17, DOI 10.1145/3126908.3126929; TPDS 2020 version | 2017 | Indexing and tagging integrated into a shared-nothing distributed FS (GlusterFS and CephFS in the TPDS version) | Not verified (abstract only) | Argues against a decoupled index; the gain (up to 10x) comes from avoiding the external copy | [abstract] https://www.osti.gov/biblio/1423035 |
| GUFI (Manno, Lee, Challa, Zheng, Bonnie, Grider, Settlemyer), SC22 pp. 57:1-57:14; BSD-3 code (LANL LA-CC-15-039) | 2022 | Tree of SQLite databases that mirrors the directory tree; parallel breadth-first query; "rollup" flattens subtrees when permissions match; incremental update by compared scan; optional `user.*` xattrs | The index directory inherits owner and mode from the source directory; a regular user queries with their own credentials and the kernel denies inaccessible databases. Xattrs that not all readers of the directory can read go into external databases per uid (`uid:nobody`) and per gid (`nobody:gid`) | Direct precedent for the per-permission-domain index of ADR-0009 (section 6) | [code and docs] https://github.com/mar-file-system/GUFI (`docs/latex/sections/design.tex`, `rollup.tex`, `xattrs.tex`); [abstract] paper: https://sc22.supercomputing.org/proceedings/tech_paper/tech_paper_pages/pap199.html |
| Brindexer (Paul et al., Virginia Tech and Cray), CCGrid 2020 | 2020 | Level-based partitioning, index in a relational DBMS, Lustre changelog for changes, periodic reindexing | Not verified; the abstract says the main audience is administrators | Same "DBMS + changelog" pattern; I did not find a public repository | [abstract] https://par.nsf.gov/servlets/purl/10167824 |
| Robinhood Policy Engine (CEA/DAM), CeCILL-C (LGPL-compatible), active (last commit 2026-09-04, ~226 stars) | 2004- | Replica of the metadata in MySQL/MariaDB (there is also `sqlite_wrapper.c` in the code); initial scan and continuous reading of Lustre MDT changelogs with `llapi_changelog_start` / `llapi_changelog_clear` | Administrator tool; I did not find a per-user filter in what I read (not verified in depth) | The changelog is only acknowledged (`clear`) after being applied to the database: it is the durable-cursor contract that Linux does not have | [code] https://github.com/cea-hpc/robinhood (`src/chglog_reader/chglog_reader.c`, `src/list_mgr/`) |
| Lustre changelogs | 2008- | Persistent record on the MDT; each reader registers (`changelog_register`, ID `clN`) and acknowledges up to a record number (`changelog_clear`); records stay until all readers acknowledge | Reading the changelog is an administrator operation (I did not verify the exact model) | A forgotten reader fills the MDT (report of billions of accumulated records): a durable cursor has an operational cost | [secondary] https://wiki.lustre.org/Lustre_Rsync_Replication ; https://www.eofs.eu/wp-content/uploads/2024/02/13_olaf_weber_lad18-experience_building_dmf7_on_lustre-olaf_weber-1.0.pdf |
| Icicle (Pan, Chard, Oh et al.), arXiv:2604.10295 | 2026 | Kafka + Flink; ingestion by snapshot and by events (Lustre, IBM Storage Scale); two indexes (discovery and aggregates) | The abstract does not mention access control | Confirms the direction of the field: external index fed by events; still without isolation | [abstract] https://arxiv.org/abs/2604.10295 |

Note on Spyglass and the MoobFS case: Spyglass uses per-partition K-D trees for queries with
several conditions, that is, a multidimensional structure instead of a planner. The cost of keeping K-D trees
balanced under continuous update was not verified here; Spyglass updates in batches per snapshot.

## 2. Permission-aware file search

| work | year | mechanism | how it handles permissions | lesson for MoobFS | source |
| --- | --- | --- | --- | --- | --- |
| Büttcher and Clarke, "A Security Model for Full-Text File System Search in Multi-User Environments", USENIX FAST '05, pp. 169-182 | 2005 | Single text index for the system; UNIX security model | A file is "searchable" if there is a path from the root with execute permission on all directories and read on the file; reading the parent directory is not required (so as not to expose names). Post-processing leaks: with ~2 queries that return a score, an attacker obtains how many files contain a term; with only the order, binary search over files the attacker creates. The secure version restricts each posting list to the visible set before processing, and the statistics become those of the visible set only | The visibility model is the same one MoobFS implements (`vfs_path_lookup` requires execute on directories; `inode_permission(MAY_READ)` on the file). Counts, totals and global scores are the leak vector; the current `QUERY` does not expose them, but any external index with a count, `COUNT(*)` or pagination by total would | [text] https://static.usenix.org/event/fast05/tech/full_papers/buettcher/buettcher_html/index.html |
| same, measured cost | 2005 | TREC4+5-CR corpus (528 thousand files) | Optimized integration: 12-17% slower with everything visible and 20-39% faster with 10% visible; security manager with ~32 bytes per inode in memory, pointed out by the authors as a problem without a convincing solution | Restricting first gets cheaper when each user sees little, which is the case of per-agent memories | same |
| Parker-Wood, Strong, Miller, Long, "Security Aware Partitioning for Efficient File System Search", IEEE MSST 2010, DOI 10.1109/MSST.2010.5496990 | 2010 | Index partitioning that integrates security, in O(n), compared with six methods (including those of SmartStore and Spyglass) | Partitions aligned with permissions; queries touch only partitions the user can see (algorithm detail not verified) | Grouping by permission boundary is cheap and gives good performance; sophisticated clustering does not pay off (according to the abstract) | [abstract] https://www.crss.ucsc.edu/pub/parkerwood-msst10.html |
| Early binding and late binding in enterprise search | 2006- | Early: ACL indexed together with the document and applied as a filter inside the query. Late: candidates verified against the source system afterwards | Early gives correct counts and facets, but goes stale when the ACL changes (reindexing). Late stays correct, but is expensive and distorts counts. Many products combine both | MoobFS today is late binding per result, with the advantage of checking in the kernel itself (no ACL copy). An external index partitioned by domain is early binding through the file boundary itself | [secondary] https://cloud.googleblog.com/2006/12/document-level-security-with-enterprise.html ; https://www.cmswire.com/knowledge-findability/managing-enterprise-search-security/ ; ServiceNow documentation (via search, not read in full) |
| GUFI, "roll in" and "rollup" rules | 2017- | See section 1 | Rollup only if all children have permissions compatible with the parent (o+rx; or same u/g and same modes; etc.). An xattr stays in the directory's database only if everyone who reads the directory can read it (file o+r; or matching u/g; or directory writable by the reader, because that one can chmod) | Explicit, verifiable rules for when metadata of several files can share the same index file | [code and docs] `docs/latex/sections/rollup.tex`, `xattrs.tex` |

Leakage by count and time in MoobFS (analysis, not measured): `QUERY` only sets `truncated` after
authorizing, so the flag does not reveal other people's items. Time, however, grows with the candidates of the first
term, including those the caller cannot see (~1 µs per candidate in the ADR-0009 measurements). An agent that
measures the latency of `agent = X AND namespace = Y` can estimate how many other people's memories match the first
term. It is the timing analog of the Büttcher and Clarke attack. Severity not assessed.

## 3. Indexing in the kernel or integrated into the filesystem: what worked and what did not

| work | year | mechanism | how it handles permissions | lesson for MoobFS | source |
| --- | --- | --- | --- | --- | --- |
| BFS (BeOS) and Haiku; Giampaolo's book, "Practical File System Design with the Be File System", Morgan Kaufmann | 1998/1999 | Per-file attributes; B+ indexes per attribute name on the volume itself; queries and "live queries" served by the FS | Does not check. In `QueryParser.h` (Haiku, MIT license) there is the comment "TODO: check user permissions here - but which one?!", which discusses checking execute on each parent directory and concludes that it is expensive | The reference design of ADR-0005 never solved permissions; MoobFS is already beyond BFS on this | [code] `headers/private/file_systems/QueryParser.h` (Haiku `3a93a9f`), lines ~944-951; book not read in this research |
| BFS, choice of index in a conjunction | same | `Equation::CalculateScore`: score = index size in blocks (`IndexGetSize`), divided by 8 (or by the key size) for `=`, `>`, `>=`, by 2 for others, by a prefix factor for patterns; `!=` scans everything. `Operator::Score` in AND returns the smaller of the two; in OR, the larger. `Query::Rewind` pushes only the lowest-score branch of each AND and both branches of each OR | Does not apply | There is no intersection nor value statistics: one branch is iterated through the index and the others are tested per candidate (`Match`), as in MoobFS. The difference is that BFS picks the branch by a cheap cost instead of always using the first | [code] `QueryParser.h` (functions cited); `src/add-ons/kernel/file_systems/bfs/Query.cpp` (`IndexGetSize` divides the index node size by the block size) |
| WinFS (Microsoft) | 2003-2006 | Relational storage on SQL Server for Windows | Not verified | Cancelled as a separate product in June 2006; parts absorbed into SQL Server and ADO.NET. Primary source (team blog) not located; detailed technical reasons not verified | [secondary] https://redmondmag.com/articles/2006/06/27/microsoft-repurposes-winfs-for-future-products.aspx ; https://www.eweek.com/database/why-microsoft-pulled-winfs-as-a-stand-alone-product/ |
| Inversion File System (Olson), USENIX Winter 1993, pp. 205-218 | 1993 | Files stored as objects in POSTGRES; transactions and "time travel" | From the DBMS (not verified) | Performance of 30% to 80% of the NFS of the time; the FS was removed from POSTGRES95, leaving only "large objects" | [abstract] https://dsf.berkeley.edu/papers/S2K-93-28.pdf ; https://usenix.org/conference/usenix-winter-1993-conference/design-and-implementation-inversion-file-system |
| Semantic File Systems (Gifford, Jouvelot, Sheldon, O'Toole), SOSP '91, DOI 10.1145/121132.121138 | 1991 | Transducers extract attributes; virtual directories interpret the path as a conjunctive query | Not verified | Precedent of query by path; the index was kept outside the storage | [abstract] https://www.mit.edu/~Saltzer/publications/recguides/sfs.html |
| Spotlight and FSEvents (Apple) | 2005- | Per-volume index maintained by a daemon (`mds`); FSEvents writes events to `.fseventsd` at the root of each volume, with IDs that survive reboot when used with the device UUID; flags warn when old IDs stop being valid | The index daemon runs privileged and filters per user (probable; I did not verify in Apple documentation). Büttcher and Clarke conjecture that Spotlight's ranking would be subject to the attack | Index in user space + persistent change feed is the model that succeeded | [partial text] https://developer.apple.com/library/mac/documentation/Darwin/Conceptual/FSEvents_ProgGuide/UsingtheFSEventsFramework/UsingtheFSEventsFramework.html |
| Windows Search and the NTFS USN journal | 2000- | Per-volume USN journal; reading via `FSCTL_READ_USN_JOURNAL` with (journal ID, starting USN); the FS only returns records valid for the given ID | Reading the journal requires administrator. Windows Search's per-user filtering of results was not verified | The documentation says the journal also exists to recover indexing without reindexing the volume after failures; the ID changes when the records may be unusable (the consumer must then rescan) | [text] https://learn.microsoft.com/en-us/windows/win32/fileio/change-journals ; https://learn.microsoft.com/en-us/windows/win32/fileio/using-the-change-journal-identifier |
| GNOME Tracker / LocalSearch + TinySPARQL | 2006- | Per-session daemon (`tracker-miner-fs-3`); SQLite database (TinySPARQL); real-time update | Index per user, in the user's session (only indexes what the user reads) | An index per user is safe and simple, but duplicates work when several users see the same files (the cost pointed out by Büttcher and Clarke). The monitoring mechanism (inotify or fanotify) was not verified | [secondary] https://mankier.com/package/localsearch |
| KDE Baloo (LGPL/GPL, active, last commit 2026-10-11) | 2014- | LMDB (`mdb_env_open` in `src/engine/database.cpp`); monitoring via inotify (`src/file/kinotify.cpp`) | Index per user | Same lesson as Tracker; inotify requires one watch per directory and also loses events on overflow | [code] https://github.com/KDE/baloo |
| btrfs `find-new` and generation numbers | 2008- | Generation per transaction; `btrfs subvolume find-new <subvol> <gen>` lists files changed after the given generation and prints the last generation | Administrator tool (the tree search ioctl requires privilege; not verified in this research) | A persistent monotonic counter is already enough for "what changed since X" without an event journal; it only exists in btrfs | [secondary] https://btrfs.readthedocs.io/en/latest/_sources/btrfs-subvolume.rst.txt |

Why some succeeded and others did not, according to the sources read: those that survived keep the index outside the
on-disk format and outside the kernel, and ask the kernel only for a change feed (FSEvents, USN, inotify). Those
that embedded the index in the storage either got stuck to one system (BFS) or were cancelled and
absorbed by a DBMS (WinFS, Inversion). The declared reasons for the cancellation of WinFS are product reasons, not
technical; I did not find a primary source with a technical analysis.

## 4. Linux kernel facilities for change feeds and batch authorization

| work | year | mechanism | how it handles permissions | lesson for MoobFS | source |
| --- | --- | --- | --- | --- | --- |
| `fanotify` with `FAN_ATTRIB` | 5.1 | Metadata change event; requires a group with handle identification (`FAN_REPORT_FID`). `setxattr`/`removexattr` trigger `fsnotify_xattr`, which generates `FS_ATTRIB` | See the "unprivileged" row | A `user.moob.*` attribute change made by the VFS generates an event. For the stacked MoobFS, the event reaches the layer inode when the user writes through the layer; whether it also reaches the lower inode depends on the layer calling `vfs_setxattr` on the lower (not verified) | [code] `include/linux/fsnotify.h` (v6.12, `fsnotify_xattr`), `fs/xattr.c` (3 calls); [text] `fanotify_mark.2` |
| `FAN_REPORT_FID`, `FAN_REPORT_DIR_FID`, `FAN_REPORT_NAME`, `FAN_REPORT_DFID_NAME`, `FAN_REPORT_TARGET_FID` | 5.1, 5.9, 5.9, 5.9, 5.17 | Events carry the handle of the object, of the parent directory and the entry name | Without a handle the listener only has an opaque identifier; reopening by handle requires privilege (below) | With `DFID_NAME` the indexer receives (directory, name) and can update paths without `open_by_handle_at` in many cases | [text] `fanotify_init.2` |
| Queue overflow and persistence | - | Per-group limit in `/proc/sys/fs/fanotify/max_queued_events` (before 5.13, fixed at 16384); when exceeded, events are discarded and a `FAN_Q_OVERFLOW` is always generated. `FAN_UNLIMITED_QUEUE` requires `CAP_SYS_ADMIN`. Consecutive events of the same object and process can be merged. Nothing persists after reboot | - | The consumer needs a full-rescan path in two cases: overflow and reboot. This already holds for MoobFS's own in-RAM index, rebuilt at mount | [text] `fanotify.7`, `fanotify_init.2` |
| Unprivileged `fanotify` | 5.13 (and 5.10.220) | Allowed without `CAP_SYS_ADMIN`, with limits: no unlimited queue and marks, no permission events, requires `FAN_REPORT_FID`, only marks inodes (mount and filesystem marks forbidden), no pid of other processes. Since 6.16 (according to LWN), whoever has privilege inside a user namespace can mark superblocks and mount namespaces there | - | A regular agent can watch its own directories, but a whole-volume indexer needs privilege (or to own the mount's user namespace, from 6.16 on) | [text] `fanotify_init.2` lines ~513-564; [secondary] https://lwn.net/Articles/1075829/ (Jake Edge, 2026-06-08) |
| `name_to_handle_at` / `open_by_handle_at` | 2.6.39; flags `AT_HANDLE_FID` 6.5, `AT_HANDLE_MNT_ID_UNIQUE` 6.12, `AT_HANDLE_CONNECTABLE` 6.13 | Persistent handle of the object; it can go stale (`ESTALE`) | `open_by_handle_at` requires `CAP_DAC_READ_SEARCH`. Since commit 620c266f (Christian Brauner, May 2024; present in the v6.12 I read), there is an alternative path restricted to directories (`O_DIRECTORY`), for whoever has `CAP_SYS_ADMIN` in the user namespace of the superblock or of the mount and `CAP_DAC_READ_SEARCH` in their own namespace; in that case `vfs_dentry_acceptable` walks up the parents checking whether the caller reaches the object, with a comment that admits it is "racy" and expensive | A batch authorization by handle cannot use `open_by_handle_at` for unprivileged callers. It would have to decode the handle inside the layer and check the path as phase 3b already does. The kernel already has a precedent of "walking up the parents checking reachability" | [text] `open_by_handle_at.2`; [code] `fs/fhandle.c` v6.12 (`may_decode_fh`, `vfs_dentry_acceptable`); [secondary] commit 620c266f via https://github.com/ctrliq/kernel-src-tree/commit/620c266f394932e5decc4b34683a75dfc59dc2f4 |
| Persistent change journal for Linux (Amir Goldstein) | 2018-2019 | LSFMM 2018: NTFS-style journal for backup; Jan Kara suggested an "intent to change" event recorded before the change. LSFMM 2019: implementation over overlayfs in production (CTERA), request for a pre-write hook and subtree filter; Ts'o and Kara pointed out locking complexity | - | No consensus nor code in mainline as far as I read; in 2024-2026 the focus was pre-content events for HSM (`FAN_PRE_ACCESS` merged in 2025). A durable cursor today would be our own code | [secondary] https://lwn.net/Articles/755277/ (2018-06-04); https://lwn.net/Articles/789038/ (2019-05-22); https://lwn.net/Articles/981392/ (2024-07-16) |
| Landlock | 5.13 (ABI 1) to 6.13+ | Unprivileged sandbox LSM; FS rights (`READ_FILE`, `READ_DIR`, `EXECUTE`, `REFER`, `TRUNCATE`, `IOCTL_DEV`...) | In v6.12 the FS hooks are `file_open`, `path_*`, `sb_*`, `file_ioctl` etc.; there is **no** `inode_permission` nor an xattr read hook. `IOCTL_DEV` only covers devices | `inode_permission` does not consult Landlock. An agent in a Landlock sandbox that cannot open `/memories/b` still receives `/memories/b/x` in `QUERY` if the POSIX permissions allow it, and also reads the xattrs. Batch authorization would inherit the same gap | [code] `security/landlock/fs.c` v6.12 (`LSM_HOOK_INIT` list), `include/uapi/linux/landlock.h`; [partial text] https://docs.kernel.org/6.6/userspace-api/landlock.html |

## 5. Query execution techniques for a small conjunctive index

Current state of MoobFS, verified in `layer/index.c`: one rbtree per attribute name, with entries
ordered by `(type, value, ino)`; `seek_first` positions at the start of the first term's range and
`first_after` does a full-key search `(type, value, ino)` in O(log n). Therefore, for an equality
term, the matching entries form a contiguous run **already ordered by inode**, and it is possible to jump
inside it to the first inode greater than or equal to k in O(log n).

| work | year | mechanism | how it handles permissions | lesson for MoobFS | source |
| --- | --- | --- | --- | --- | --- |
| Intersection of sorted lists (Culpepper and Moffat, ACM TOIS 29(1), DOI 10.1145/1877766.1877767) | 2010 | Conjunctive query = k-way intersection of sorted sets of integers; they compare methods over compressed and uncompressed representations; propose a hybrid with bitvectors for dense lists | - | With two equality runs ordered by inode, the leapfrog intersection costs around O(m log(n/m)), with m the size of the smaller one, instead of O(size of the first) | [abstract] https://people.eng.unimelb.edu.au/ammoffat/abstracts/cm10acmtois.html |
| SIMD intersection and "galloping" (Lemire, Boytsov, Kurz) | 2014-2020 | Vectorized galloping (exponential search) over compressed lists; doubles the speed of conjunctive intersections | - | The SIMD gain does not apply to an rbtree (pointers), but galloping does: each jump is a `first_after` | [abstract] https://arxiv.org/abs/1401.6399 (the journal, Software: Practice and Experience, was not confirmed in this research) |
| Choosing the most selective term with a cheap estimate (BFS) | 1999- | Score by index size and operator; no per-value statistics | - | Minimal variant for MoobFS: keep, per attribute name, the number of entries (a counter already cheap to maintain on insert and removal) and start with the term whose index is smaller. It helps when the names have very different sizes; it does not help in `namespace = X AND agent = Y` if both names are in all memories | [code] `QueryParser.h` |
| Composite key | - | Index on `(attribute1, attribute2)`, like the SQLite composite index used in the pilots | - | Solves the measured shape in O(log n + results), but requires declaring which pairs to index (schema, extra memory, format decision). It is the first step of a planner; ADR-0009 alternative C already rejects it | ADR-0009 |
| Signature files / Bloom filters (Spyglass) | 2009 | Bit array per partition and attribute; a negative test discards the partition; no false negatives | - | Useful when there are partitions to skip (for example, by namespace). In a single in-RAM index, the sorted intersection is exact and simpler; the filter would only pay off with per-domain indexes | [text] Spyglass, FAST '09 |
| Bitmaps per value | - | One inode bitmap per value; AND of bitmaps | - | Fast for low-cardinality values (namespace, agent), but costs memory per value and maintenance on every `setxattr`; not verified in any source of this research for the kernel context | - |

Estimated cost of the leapfrog intersection in MoobFS (proposal, not implemented, not measured): for two equality
terms with runs of sizes a and b, the worst case is about 2·min(a, b) O(log n) searches in the rbtree.
In the shape `namespace = X AND agent = Y` with 10 000 memories, if each run has a few hundred
entries, that is a few hundred searches instead of scanning all candidates of the first term. It is
simpler than a planner: a fixed rule ("if there are two or more `EQ` terms, intersect their
runs; the other terms remain as a per-candidate filter"), without statistics, without allocation and without
a UAPI change. The output order would become by inode within the intersection, which changes the semantics of the
resume cursor (`moobl_resume`) and needs a specification.

## Recurring architectures

1. **User-space index + kernel change feed + rescan as a safety net.**
   Spotlight/FSEvents, Windows Search/USN, Tracker and Baloo with inotify, Robinhood and Brindexer with the Lustre
   changelog, Icicle. The kernel or the FS provides "what changed since X"; the index and the planning live in a
   mature engine. All have a rescan path for when the feed fails (journal ID changed,
   overflow, lost reader).
2. **Partition the index along the permission boundary.** GUFI (per directory, with the owner and mode of the
   directory), Parker-Wood et al. (security-aware partitions), the Spyglass proposal to exclude
   partitions, and the per-user indexes of Tracker and Baloo. The permission of the index file plays the role
   of "early binding" without copying ACLs into the index.
3. **Restrict before computing.** Büttcher and Clarke and the early-binding practice: nothing (score,
   count, facet, total of pages) may be computed over items the caller does not see. Late binding
   is only safe if the answer does not expose aggregates.
4. **Index inside the storage, with a minimal query plan.** BFS (one branch per AND, chosen by
   cheap score) and Magellan. It works for one user or administrator; none of the examples read solves
   per-result permissions.

## Implications for ADR-0009

**Which pattern fits.** ADR-0009 is pattern 1 with one extra requirement, isolation between agents,
which only pattern 2 (or a privileged service) solves. The literature confirms that the expensive part (planner,
composite indexes, pagination) is done outside the kernel in all the cases that succeeded. No work read
keeps per-result POSIX authorization inside the kernel for an external index; on this point MoobFS has no
direct precedent.

**How a GUFI-style scheme would solve the metadata leak.** Proposal for M4 to measure, not a
decision:

- One SQLite database per permission domain. In MoobFS the natural domain is the namespace (or
  agent) directory, which already has its own owner and mode. The index file lives in a directory with the same owner, group,
  mode and, if any, ACL as the indexed directory. Whoever does not read the directory does not open the index: the kernel does the
  authorization at open, once, and not per result.
- Files with permissions different from the directory's (the case GUFI solves with per-uid and per-gid
  databases) need an explicit rule. In MoobFS the simplest rule is to forbid by convention that a memory
  have a permission more restrictive than its directory, or to index it in a separate database with the file's owner,
  as GUFI does.
- Cross-domain queries: the client opens all the domain databases it can open and queries each one
  (GUFI does this in parallel, breadth-first). The number of domains per agent defines the cost.
- Who writes the databases: a privileged indexer fed by `fanotify` with a filesystem mark
  (requires `CAP_SYS_ADMIN`). It **writes** with privilege, but does not **answer** queries: the readers are
  regular processes that open SQLite with their own credentials. This avoids the query service with
  `SO_PEERCRED` that ADR-0009 lists as the first way out, keeping a privileged process only on the
  write side.
- Limits: POSIX ACLs, idmap and user namespaces need to be copied to the index files and maintained
  when they change (`FAN_ATTRIB` on the directory); a permission change on the directory has to reach the database
  before the next query, or the index leaks during the window. This window does not exist in the current `QUERY`.
- The kernel's batch authorization remains useful as a second barrier (for example, for files inside the
  domain with a more restrictive permission), and as the only mechanism if the index is shared.

**Whether a durable cursor is justified.** According to the sources, not as a first step. MoobFS's own index
is already rebuilt at mount, so an external index can follow the same rule: rescan at mount and
after `FAN_Q_OVERFLOW`, and follow `fanotify` the rest of the time. Linux has no generic persistent
journal, and Goldstein's attempts show that a journal that is correct across a crash requires
"intent to change" hooks with locking problems. A cheap intermediate step, inspired by the NTFS journal ID
and the btrfs generation: expose in `GET_INFO` a mount epoch (it changes at every mount and at every
index rebuild) and a monotonic counter of attribute changes. With this the consumer detects that it
lost continuity and rescans, without the kernel keeping events. It is a proposal; it requires a UAPI specification. The
cost of rescanning 10 000 or 100 000 memories is the number that decides; it was not measured.

**Whether the in-kernel intersection is a small, contained improvement.** Yes, restricted to equality terms, and
it is worth keeping as a candidate within ADR-0009's "efficiency-only improvements, no planner". The index already
orders by `(type, value, ino)` and already has a full-key search (`first_after`), so the leapfrog
intersection of `EQ` runs reuses what exists. It attacks the measured case that grows with the size of the
set (34 µs with 1 thousand, 1.1 ms with 10 thousand). It does not solve the per-result cost (~1.7 µs, path
resolution), nor ranges, nor pagination. Choosing the term by index size, BFS-style, is even
smaller, but does not help when all names are in all memories. I do not recommend bitmaps, composite
keys nor per-value statistics in the kernel: that is where the planner the ADR rejects begins. Points to
specify first: output order, resume and the interaction with the 4096 limit.

**Authorization gaps to record in the ADR, independent of the choice.** (a) Landlock: `inode_permission`
does not consult it, so `QUERY` and a batch authorization in the same mold ignore Landlock sandboxes (read in the
v6.12 code; not tested in MoobFS). Options: document it as a limit, or also check with the hook that
Landlock uses (`security_file_open` requires a `struct file`, which is expensive per result; not assessed). (b)
Timing channel: cost proportional to other people's candidates. The intersection reduces the amplitude of the signal, but does not
eliminate it. (c) Any external index that exposes `COUNT`, total of pages or ranking needs to restrict
first (Büttcher and Clarke).

## What to measure in M4 from this research

Items that this research adds to the ADR-0009 list, without replacing it:

1. Leapfrog intersection of `EQ` terms versus the current path, in the nine W3 shapes, at 1 thousand, 5 thousand and
   10 thousand memories, with and without KASAN.
2. Cost of opening N SQLite databases per domain and querying each one (N = 1, 10, 100), GUFI-style, versus
   a single database with batch authorization.
3. Window between `chmod`/`setfacl` on a namespace directory and the update of the domain's index file
   by the indexer.
4. Full rescan time of the external index after `FAN_Q_OVERFLOW` and after remount.
5. Amplitude of the timing channel: `QUERY` latency difference between zero and 10 thousand other people's candidates
   in the first term, before and after the intersection.
6. Reproduction in a VM of the Landlock case: an agent with a ruleset without `READ_FILE` on another agent's namespace, calling
   `QUERY` and `getxattr`.

## Main references (by area)

Metadata search:

- Leung, Shao, Bisson, Pasupathy, Miller. Spyglass. USENIX FAST '09.
  https://www.usenix.org/conference/fast-09/spyglass-fast-scalable-metadata-search-large-scale-storage-systems
- Leung, Adams, Miller. Magellan. UCSC-SSRC-09-07, 2009. https://www.ssrc.ucsc.edu/pub/leung-ssrctr09-07.html
- Hua, Jiang, Zhu, Feng, Tian. SmartStore. SC '09.
  https://www.usenix.org/conference/fast-09/smartstore-new-metadata-organization-paradigm-semantic-awareness
- Xu, Jiang, Tian, Huang. Propeller. IEEE ICDCS 2014. DOI 10.1109/ICDCS.2014.46.
- Sim, Kim, Vazhkudai, Vallée, Lim, Butt. TagIt. SC17. DOI 10.1145/3126908.3126929.
- Manno, Lee, Challa, Zheng, Bonnie, Grider, Settlemyer. GUFI. SC22.
  https://sc22.supercomputing.org/proceedings/tech_paper/tech_paper_pages/pap199.html ;
  code https://github.com/mar-file-system/GUFI
- Paul et al. Brindexer. CCGrid 2020. https://par.nsf.gov/servlets/purl/10167824
- Robinhood Policy Engine. https://github.com/cea-hpc/robinhood
- Pan et al. Icicle. arXiv:2604.10295, 2026.

Permissions:

- Büttcher, Clarke. A Security Model for Full-Text File System Search in Multi-User Environments.
  USENIX FAST '05. https://www.usenix.org/conference/fast-05/security-model-full-text-file-system-search-multi-user-environments
- Parker-Wood, Strong, Miller, Long. Security Aware Partitioning for Efficient File System Search.
  IEEE MSST 2010. DOI 10.1109/MSST.2010.5496990.

Filesystem integration:

- Giampaolo. Practical File System Design with the Be File System. Morgan Kaufmann, 1998/1999.
- Haiku, `headers/private/file_systems/QueryParser.h` and `src/add-ons/kernel/file_systems/bfs/Query.cpp`.
  https://github.com/haiku/haiku
- Olson. The Design and Implementation of the Inversion File System. USENIX Winter 1993.
- Gifford, Jouvelot, Sheldon, O'Toole. Semantic File Systems. SOSP '91. DOI 10.1145/121132.121138.
- Microsoft. Change Journals; Using the Change Journal Identifier. learn.microsoft.com.
- Apple. File System Events Programming Guide. developer.apple.com.

Linux kernel:

- Manual pages `fanotify(7)`, `fanotify_init(2)`, `fanotify_mark(2)`, `open_by_handle_at(2)`,
  man-pages repository at git.kernel.org.
- Code v6.12: `include/linux/fsnotify.h`, `fs/xattr.c`, `fs/fhandle.c`, `security/landlock/fs.c`.
- LWN: https://lwn.net/Articles/755277/ , https://lwn.net/Articles/789038/ ,
  https://lwn.net/Articles/981392/ , https://lwn.net/Articles/1075829/

Query execution:

- Culpepper, Moffat. Efficient Set Intersection for Inverted Indexing. ACM TOIS 29(1), 2010.
- Lemire, Boytsov, Kurz. SIMD Compression and the Intersection of Sorted Integers. arXiv:1401.6399.

## What was not verified

- Papers read only through the abstract or catalog: Magellan, SmartStore, Propeller, TagIt, Brindexer, Icicle,
  Parker-Wood et al. (MSST 2010), GUFI's SC22 paper (the repository's code and documentation were read),
  Inversion, Semantic File Systems, Culpepper and Moffat, Lemire et al.
- Giampaolo's book on BFS: not read; the claims about BFS come from Haiku's code, which is a
  reimplementation (OpenBFS) and may differ from the original BeOS.
- Venue of "SIMD compression and the intersection of sorted integers": I read only the arXiv; the journal was not
  confirmed.
- Spotlight: whether `mds` filters results per user and how. Windows Search: how it applies ACLs to the
  results. Both remained as "probable" or not verified.
- Tracker/LocalSearch: whether it uses inotify or fanotify today.
- Lustre: exact syntax and privilege model of `changelog_register`/`changelog_deregister`; they came from
  secondary sources.
- WinFS: technical reasons for the cancellation; only 2006 press coverage.
- Whether Goldstein's persistent journal proposals continued after 2019; the 2024 and
  2026 coverage I read does not address it.
- Unprivileged `fanotify` in user namespaces from 6.16 on: came from an LWN summary, not from the code.
- Whether MoobFS generates `FAN_ATTRIB` on the lower inode in addition to the layer inode, and whether the layer propagates events from the
  lower: not tested; ADR-0009 already lists it as an M4 item.
- Effect of Landlock on `QUERY`: inferred from the Landlock code in v6.12; not reproduced in a VM.
- Robinhood: I did not verify whether there is per-user result filtering in `rbh-find` or in the web interface.
- No performance number in this research was measured in MoobFS; the estimates in section 5 are analysis.

# Addendum to the metadata search research (ADR-0009): items previously not verified

Date: 2026-10-11 (all sources accessed on this date). Branch: `docs/adr-0009-query-role`. It complements
`docs/research-metadata-search.md` without changing it. It uses the same reading-level marks: **[code]**,
**[text]**, **[abstract]**, **[secondary]**. Everything read on the web and in third-party repositories was
treated as data. Nothing was compiled or executed; no number below was measured in MoobFS.

Material obtained in this round (temporary copies outside the repository): `mar-file-system/GUFI` (`fc33e4b`,
2026-10-09; `docs/latex/sections/*.tex`, `src/`, public PDFs in `docs/presentations/`) and OSTI LA-UR-18-21535;
Giampaolo's book on the author's site (http://www.nobius.org/dbg/, which offers it because it is out of print);
`GNOME/localsearch` (`30d07f6`, 2026-10-03); sparse `lustre/lustre-release` (`4a7bdda`, 2026-09-29) and the manual
(section 12.1); `cea-hpc/robinhood` (`f2fffe6`); the Parker-Wood et al. PDF via the SSRC copy in the Internet Archive
(the UCSC site gave error 500); `fanotify_user.c` (v6.15, v6.16) and `uapi/linux/fanotify.h` (v6.13 to v7.3-rc6).

## 1. GUFI: how permission is enforced, updates and numbers

**Who enforces permission in the query.** [code] `gufi_query` does no explicit permission check:
there is no `access(2)`, `seteuid` nor per-row filter in the query path (`src/gufi_query/`, `src/dbutils.c`).
It walks the index tree breadth-first with `opendir`/`readdir` and opens each `db.db` with
`sqlite3_open_v2`/`ATTACH`; an `EACCES` only makes the directory be skipped (`dbutils.c`, handling of
`SQLITE_CANTOPEN`). The one that denies is the kernel of the index server. [text] `docs/latex/sections/deploy.tex`
describes the deployment model: indexes on a dedicated server; `/etc/passwd` and `/etc/group` of the source systems
merged on the server, with shell `/usr/sbin/nologin`; users log in via ssh with host-based
authentication and a restricted shell (`gufi_jail`, via `ForceCommand`) that only accepts GUFI commands. That is, the query
runs **with the user's real uid**; there is no privileged daemon filtering. The 2023 slides (LA-UR-23-24629)
justify it: a flat index with several uids would require "per-row permission checking" or admin-only use.

**Traversal and ancestors.** [code + inference] The index directories receive the mode, owner and group of the
source directory (`gufi_dir2index.c`, `chmod`/`chown`; `utils.c:dupdir`). To start in a subtree the
user needs execute on the ancestors (normal path resolution). To descend, `gufi_query` needs
`opendir`, that is, read **and** execute on each index directory: a directory with only `x` cuts off the
descent, although in POSIX the children could be reachable through a known path. It is more restrictive than the
Büttcher and Clarke model, not less. The rollup rules ([text] `rollup.tex`) also require compatible `rx`
(o+rx; or same u/g and modes; etc.).

**ACLs.** [code] `dupdir` copies only `mode`, `uid` and `gid`; the comment says that xattrs of the index
directory are not needed. `xattr_can_rollin` and the modes of the external databases look only at `st_mode`. I did not
find a copy of `system.posix_acl_*` in `src/`. [text] The 2026 slide (LA-UR-26-24285) lists "ACL/MLS"
among the access-control sources, without detail. Conclusion: in the code read, POSIX ACLs are not
replicated; a file protected only by an ACL is protected only by the copied mode.

**Xattrs with different visibility.** [code] Only `user.*` is extracted. Those that cannot "roll in"
(rules in `xattrs.c:xattr_can_rollin`) go into external databases: per uid, owner `uid:nobody`, mode `0600`; per
gid, owner `nobody:gid`, mode `0040` if the file has `g+r`, otherwise `0000` (only root reads) (`dbutils.c`, ~line
970). The user attaches the ones the kernel lets them open.

**Update model.** [text] There is no event stream. The 2023 slide says LANL does full
scans every 4 hours and that incremental update is possible, but not tested; in 2022 the new index
was published by swapping a symlink. The documented incremental update (`incremental_update.tex`;
LA-UR-26-25065, 2026) scans the whole source tree, marks "suspect" directories by timestamp or by
log file, regenerates only the `db.db` of those directories in a "parking lot" and applies a CRUD diff obtained by
`LEFT OUTER JOIN` between directory listings (renames are detected by inode). The update
latency is therefore that of a scan (hours on large systems).

**Scale and performance.** [text] SDC 2019 (LA-UR-19-29542; one AMD Epyc 7401 server, 512 GB, kernel 3.10,
one NVMe, XFS), "find all files" as a regular user: Lustre with 118 509 899 files and 5.5 M
directories in 134.2 s versus 11 309 s with POSIX tools (84x); NFS home with 13.2 M files in 39.2 s
versus 2 040 s (51x). Index creation for 118.5 M Lustre files in 148.9 s. First version only 4x to 25x;
the gain came from opening databases more cheaply (SQLite with `unix-none` and without thread-safety). 2023: "Scratch 3"
(2.2 M directories, 65 M files), `SELECT uid FROM pentries` with 224 threads took 15.36 s without rollup and
~2.8 s with rollup limited to 100 thousand-500 thousand entries; rollup reduced the directories to walk to 0.04%-0.39%
in three indexes. Indexing TACC's BeeGFS (56.6 M directories, 16 threads) took ~35.75 h. SC22 paper: [abstract]
only; the abstract reports 1.5x to 230x over the state of the art on administrator queries. The full text
(IEEE/ACM, DOI 10.1109/SC41404.2022.00062, paper 57) was not found in open access (OSTI only has a
2022 poster and the 2018 slides).

**New finding.** [text] In June 2026 LANL presented "Access Controlled AI Context with GUFI"
(LA-UR-26-24285): extracted text, FTS5 and vectors (`sqlite-vec`) stored in external databases beside the
metadata, for RAG queries that respect the source permissions. The slide states that the same question asked
by different people will have different answers. No security or performance numbers beyond a
test system with 70 thousand files and 7 thousand directories.

Sources: https://github.com/mar-file-system/GUFI (files cited);
https://www.osti.gov/biblio/1422987 ; https://laro.lanl.gov/esploro/outputs/conferenceProceeding/GUFI-Fast-Secure-File-System-Metadata/9916458628503761 [abstract].

**Implication for ADR-0009.** GUFI confirms the design "the kernel denies opening the domain's database", but
with three differences that the ADR needs to assume explicitly: it runs the query as the user themselves on a
dedicated host, it does not replicate ACLs and it has no live update (the index lags hours behind). An index
fed by `fanotify` with ACL copying and a short window after `chmod` has no precedent in GUFI.

## 2. Giampaolo, "Practical File System Design with the Be File System"

[text] Sections 5.2 to 5.4 and 9.3 of the author's PDF.

- **Choice of index in a query with several terms** (5.3, p. 94): after parsing, the FS descends from the root
  choosing the path with "fewest matches"; it prefers expressions that use the B+tree (for example `size >
  20000`) to expressions with a regex, and also compares the size of the indexes. The author admits that the logic is
  "fairly convoluted" and that the heuristics could improve. The other terms are evaluated by reading the
  attributes of the file itself, without using other indexes. It confirms what Haiku's code showed.
- **`!=` alone scans all files** because files without the attribute match (p. 95-96).
- **Permissions in the results**: the queries section does not mention a permission check; the result is an
  inode converted into a path thanks to the name and the parent directory stored in the inode (p. 97). The absence in the
  text is compatible with Haiku's "TODO", but does not prove BeOS's behavior.
- **Live queries** (p. 98): each index cited in a query receives a tag that points to the query's
  tree; on each modification of the index the FS walks the interested live queries and tests the file. The author
  records "many subtle locking issues" in this index-query-index path.
- **Cost of maintaining indexes** (5.2 and 9.3): the size index is only updated at `close`, because updating it on
  every write cost a significant "performance hit"; above ~10 thousand duplicates the update of the
  size index becomes slow; removing the creation-time index gave a ~20% gain in a
  create/remove benchmark; permissions and owner were not indexed because the cost exceeded the benefit. In the benchmarks,
  BFS without indexes was almost twice as fast as BFS with the three default indexes (lat_fs and PostMark; 17
  transactions/s with indexes).

Source: http://www.nobius.org/dbg/practical-file-system-design.pdf (via http://www.nobius.org/dbg/).

**Implication for ADR-0009.** BFS's own author measured index maintenance in the write path
as double the cost of metadata operations, and deferred updates to `close`. It reinforces keeping in the kernel
only the already existing, cheap index, with no planner and no new indexes; choosing the term by estimated cost is
the only planning technique BFS found worthwhile.

## 3. Windows Search and Spotlight: per-user filtering

**Windows Search** [text], "Windows Indexing Features" (Windows 7/Server 2008 R2, archived): one index per
computer; the service runs as LocalSystem; the index files have an ACL only for Administrators and SYSTEM;
"security trimming" ensures that each user only searches their own files and the shared ones, according to the
ACLs of each file. The document admits that an administrator can read the index. The old Indexing
Service ([text], archived documentation) ran the query in the system context and then removed the
results the user could not read: post-filtering by a privileged service, with the explicit justification
of not revealing even the existence of the document. The SharePoint 2007 model ([secondary] via
search) stores the security descriptor in the index and checks per item at query time.

**Spotlight** [secondary]: one index per volume (`.Spotlight-V100`, according to Eclectic Light) and per-user
filtering at query time. The sentence "Spotlight respects the boundaries of users' personal files—even though there is
only one index per volume" appears quoted in an Apple forum in 2006, without an identified source. I did not find
Apple documentation describing the mechanism.

Sources: https://learn.microsoft.com/en-us/previous-versions/windows/it-pro/windows-7/dd744700(v=ws.10) ;
https://learn.microsoft.com/en-us/previous-versions/windows/desktop/indexsrv/security-summary ;
https://discussions.apple.com/thread/747558 ; https://eclecticlight.co/2021/01/28/spotlight-on-search-how-spotlight-works/

**Implication for ADR-0009.** The two dominant desktop search engines use a shared,
privileged index with an ACL cut at query time, that is, the "query service" that ADR-0009 lists as an
alternative, and not GUFI's partitioning. The precedent exists and is mature, but it concentrates the risk in a
privileged process.

## 4. WinFS: reasons for the cancellation

[text] Quentin Clark, WinFS team blog, "WinFS Update" (2006-06-23) and "Update to the Update"
(2006-06-26): WinFS stopped being delivered as a separate product; the database part went to SQL
Server and ADO.NET; the rest stayed in "incubation". Asked whether WinFS died because of its design, Clark answers
no, but admits several internal design changes and rewrites, and says that making the relational engine
behave and perform like the Windows filesystem required a lot of care, with "super high" performance and
compatibility bars.

[secondary, but from a participant] Hal Berenson, series "WinFS, Integrated/Unified Storage, and Microsoft"
(Feb.-Mar. 2013), who presents himself as general manager of Integrated Storage in the SQL Server organization at the
time of Longhorn: part 4 attributes the fall to the heavy bet on the CLR (removed in the Longhorn "reset"), to the lack
of a committed internal customer and to the lack of a delivery path in Windows; part 3 cites conflicting
requirements between structured and unstructured data, fear of complexity in kernel mode and in the boot
path, and the clash between arbitrary metadata and controlled schema evolution.

Sources: https://learn.microsoft.com/en-us/archive/blogs/winfs/winfs-update ;
https://learn.microsoft.com/en-us/archive/blogs/winfs/update-to-the-update ;
https://hal2020.com/2013/02/25/winfs-integratedunified-storage-and-microsoft-part-3/ ;
https://hal2020.com/2013/03/10/winfs-integratedunified-storage-and-microsoft-part-4/

**Implication for ADR-0009.** The documented technical reasons are the cost of making a relational database
meet the performance and compatibility requirements of a filesystem, and the coupling to an
unstable platform; both support the decision not to bring a relational planner into the kernel.

## 5. GNOME LocalSearch (formerly tracker-miner-fs)

[code] `localsearch` `30d07f6`:

- **inotify or fanotify**: both. With `HAVE_FANOTIFY` (build option `fanotify`, default `auto`),
  `tracker_monitor_new` first tries `TrackerMonitorFanotify`; if it fails, it uses GLib's `GFileMonitor`
  (inotify). Per directory, if it cannot create the handle (for example `EXDEV` on btrfs), it falls back to the
  GLib monitor.
- **fanotify mode**: unprivileged: `fanotify_init(FAN_CLOEXEC | FAN_CLASS_NOTIF | FAN_REPORT_DFID_NAME)`
  and per-directory inode marks (`FAN_MARK_ADD | FAN_MARK_ONLYDIR`) with `FAN_EVENT_ON_CHILD`; events
  include `FAN_ATTRIB`, creation, removal, moves and `FAN_CLOSE_WRITE`. It limits itself to 80% of
  `/proc/sys/fs/fanotify/max_user_marks`; above that, new directories stop being watched and only a
  counter is incremented.
- **Overflow**: I did not find handling of `FAN_Q_OVERFLOW` in the monitor code; from the reading, an
  overflow event (without a `DFID_NAME` record) is simply skipped. I did not verify whether another layer notices the loss.
- **Consistency at startup**: the notifier compares the stored `mtime` and the on-disk `mtime` when scanning the roots
  (`tracker-file-notifier.c`).
- **Permissions**: it runs as a systemd user service (`ConditionUser=!@system`,
  `XDG_SESSION_CLASS=user`), so it indexes only what the user reads, in one index per user.

Source: https://gitlab.gnome.org/GNOME/localsearch (`src/indexer/tracker-monitor.c`,
`tracker-monitor-fanotify.c`, `tracker-file-notifier.c`, `tracker-miner-fs.service.in`, `meson_options.txt`).

**Implication for ADR-0009.** There is a production precedent of unprivileged `fanotify` with per-directory
marks and `DFID_NAME`, exactly the mode available to a regular agent; but it runs into the marks limit
and does not handle overflow. A MoobFS indexer must handle `FAN_Q_OVERFLOW` with a rescan and expose when it
stopped watching something.

## 6. Lustre changelogs and Robinhood

[text] Manual, section 12.1: the administrator registers readers on the MDS (`lctl --device <MDT> changelog_register
--user NAME --mask TYPES`); the system purges up to the highest record processed by all; records are not
purged beyond the point of a registered reader; `lfs changelog` reads and `lfs changelog_clear <mdt> <id>
<endrec>` acknowledges; `changelog_deregister` is equivalent to a full `clear`. The manual describes the changelog as
centralized and "designed to be transactional", and offers it for auditing (with `OPEN`, `GXATR` and `NOPEN` outside
the default mask), with control by nodemap.

[code] `lustre/mdt/mdt_internal.h:mdt_changelog_allow` is required both to open the changelog
(`tgt_llog_open`) and for `CHANGELOG_CLEAR`: the caller needs uid 0, gid 0, `CAP_SYS_ADMIN` and the
nodemap RBAC role `chlg_ops`. Therefore, reading and acknowledging are root operations. `mdd` has idle reader
collection enabled by default (`mdd_changelog_gc = 1`), with an idle limit of 30 days
(`CHLOG_MAX_IDLE_TIME = 2592000`), a limit of idle indexes and a minimum interval of 1 h between collections. I did not read
the function that actually removes the reader.

[code] Robinhood, `src/chglog_reader/`: it only calls `llapi_changelog_clear` in the callback fired after the
operation was written to the database; it groups acknowledgments (`batch_ack_count = 1024` by default, or on reaching the
last record sent); it writes to its own database the last applied record every 10 thousand records or 5 s
(`commit_update_max_delta`, `commit_update_max_delay`) so as not to reprocess after a restart.

Sources: https://doc.lustre.org/lustre_manual.xhtml (section 12.1); https://github.com/lustre/lustre-release ;
https://github.com/cea-hpc/robinhood

**Implication for ADR-0009.** The durable cursor that works in production has three pieces: a registered
privileged reader, explicit acknowledgment after applying, and collection of forgotten readers. None
of them filters events per user; a MoobFS change feed read by regular agents would be something new and
would need its own visibility rule.

## 7. Parker-Wood, Strong, Miller and Long, MSST 2010

[text] Full text (14 p.). The algorithm walks the tree breadth-first and opens a new partition whenever
the permissions of the directory or file are more restrictive than those of the current partition, or when the user
or group changes; thus, whoever accesses one file of a partition accesses all of them. It uses only UNIX permissions; ACLs remain as
future work. Cost O(n) in time and O(log n) in memory (stack of path permissions). Evaluation on
four anonymized metadata collections (SOE/UCSC 6.9 M files; NetApp web 15.6 M, eng 60.4 M, home 268.5
M), with results shown for SOE and web. The comparison with six methods uses partition size, entropy and
information gain; **it does not measure query latency** on a real system (a prototype on Ceph is future
work). On SOE, the security partitioning produced 29 479 partitions with a mean of 234 entries (standard deviation 3 309),
versus 81 partitions of ~85 thousand in greedy DFS partitioning (Table 5; reading of the table extracted from the
PDF, checked by the product partitions × mean ≈ 6.9 M).

Source: https://www.crss.ucsc.edu/pub/parkerwood-msst10.html (error 500 on this date); PDF via
http://web.archive.org/web/20231216195950/https://www.ssrc.ucsc.edu/media/pubs/5503961c7ba8d36fed0d9146a9cb9276ad93f459.pdf

**Implication for ADR-0009.** In real trees, permission boundaries produce many small partitions.
If MoobFS creates one database per permission domain, the number of domains per agent may be large; GUFI's
rollup is the known answer, and item 2 of the M4 measurement list (N databases) becomes more important.

## 8. Unprivileged `fanotify` inside user namespaces

[code] Commit `58f5fbeb367f`, "fanotify: support watching filesystems and mounts inside userns" (Amir
Goldstein, 2025-05-16; applied by Jan Kara), present in v6.16 and absent in v6.15. Filesystem,
mount and mntns marks become accepted if the user has `CAP_SYS_ADMIN` in the user namespace where the group
was created **and** in the user namespace of the superblock (`s_user_ns`) or of the mntns. `fanotify_init` keeps using
`capable()` (initial namespace): a group created inside the namespace still receives `FANOTIFY_UNPRIV`, that is,
limited queue and marks and events necessarily with FID or mount id.

Source: https://github.com/torvalds/linux/commit/58f5fbeb367f ; `fs/notify/fanotify/fanotify_user.c` v6.15 and
v6.16. It corrects the origin of the main document's claim, which came from LWN.

**Implication for ADR-0009.** It only helps if the filesystem was mounted inside the indexer's user namespace
(same `s_user_ns`). For a MoobFS volume mounted by the host, the whole-volume indexer still
needs `CAP_SYS_ADMIN` in the initial namespace.

## 9. Goldstein's work: pre-content events, HSM and journal

[code] `FAN_PRE_ACCESS` was introduced by `4f8afa33817a` (Goldstein, 2024-11-15) and came in through the merge
`fsnotify_hsm_for_v6.14-rc1` (`8883957b3c9d`, 2025-01-23); the UAPI header shows it from v6.14 on and not
in v6.13. There were rollbacks: the pre-content event on page fault was reverted (`955fbe0ef19d`, 2025-03-12) and
pre-content and permission events became disabled by default for pseudo-files
(`711f9b8fbe4f`, 2025-02-03). `FAN_PRE_MODIFY` does not appear in the UAPI header of any version from v6.13 to
v7.3-rc6. [secondary] LWN (2024) describes a branch with `FAN_PRE_ACCESS` and `FAN_PRE_MODIFY`; 2025
messages cite a "fan_pre_modify-wip" branch and an RFC of HSM events for directories. I found nothing
merged that is a persistent change journal.

Sources: https://github.com/torvalds/linux/commit/4f8afa33817a ; https://github.com/torvalds/linux/commit/8883957b3c9d ;
https://lwn.net/Articles/981392/

**Implication for ADR-0009.** What entered mainline serves HSM (fetching content before access), not
recording changes durably. A durable cursor for MoobFS remains our own code; the
proposal of an epoch and counter in `GET_INFO` remains the cheapest step.

## Corrections to the main document

- Item 8 of the summary and the "unprivileged" row of section 4: the 6.16 change is confirmed in the code, but
  requires `CAP_SYS_ADMIN` also in the superblock's `s_user_ns`; "owning the mount's user namespace" should be
  read in that sense.
- Tracker row in section 3: LocalSearch uses unprivileged `fanotify` when available and inotify as an
  alternative.
- Lustre changelogs row: reading and acknowledging require root with `CAP_SYS_ADMIN` and a nodemap role.
- Windows Search row: the per-user filter is documented (ACL cut in a LocalSystem service).
- GUFI row: "incremental update by compared scan" is correct, but LANL's practice
  documented in 2023 is a full scan every 4 hours; ACLs are not replicated in the code read.

## Not yet verified

- Full text of GUFI's SC22 paper: no open copy found (IEEE/ACM); only the abstract and LANL slides.
  The numbers above come from slides, not from the peer-reviewed paper.
- Whether any GUFI deployment replicates ACLs or MLS labels: the 2026 slide mentions it, the code read does not do it.
- Spotlight: per-user filtering mechanism without primary Apple documentation; only forum and blogs.
- Windows Search on Windows 10/11: the documentation read is from the Windows 7 era; I did not confirm the model continues.
- LocalSearch: whether another layer detects event loss after overflow; I only read the monitor.
- Lustre: the function that removes idle readers was not read; the exact effect of the GC (deregister or only
  acknowledge) is inferred from the names and the manual.
- Robinhood: whether `rbh-find` or the web interface filter per user (still not verified).
- Giampaolo's book: I did not read the API chapter (whether the BeOS query call checked permissions outside the
  FS); the claim about permissions is by absence in section 5.3.
- Berenson: the summaries of parts 3 and 4 came from an assisted reading of the page; the original should be reread before
  quoting excerpts.
- LSFMM 2025 and 2026: I did not find a report of a session on a persistent journal or `FAN_PRE_MODIFY`.

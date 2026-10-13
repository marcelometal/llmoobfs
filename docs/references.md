# References

Works, projects and tools that MoobFS builds on, includes, compares against, or used to decide.
Every entry was checked against its primary source on 2026-10-12 (DOI metadata, publisher or
USENIX page, official repository or kernel documentation); "Used for" points to where it mattered
in this repository. Code that ships in the repository keeps its own license and provenance file.

## 1. Code included or pinned in this repository

| Project | Where it comes from | License | Pinned | Used for |
| --- | --- | --- | --- | --- |
| fsx | [xfstests](https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git) (`ltp/fsx.c`, `src/statx.h`) | GPL-2.0 (xfstests project license; the file header carries only the original NeXT copyright) | commit `57d71a8`, 2026-03-17 | random I/O exerciser on each lower filesystem (`tests/third_party/fsx/`, `vm/suite.sh`) |
| replay-log / log-writes | [xfstests](https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git) (`src/log-writes/`) | GPL-2.0 | commit `9b56b94`, 2019-10-24 | power-loss replay test (`tests/third_party/log-writes/`, `POWER_LOSS_REPLAY_PASS`) |
| checkpatch.pl | [Linux stable](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git) `scripts/` ([docs](https://docs.kernel.org/dev-tools/checkpatch.html)) | GPL-2.0 | tag `v6.12.112`, SHA-256 per file | style check of `layer/` and `kernel/` (`scripts/checkpatch.sh`) |
| virtme-ng | [arighi/virtme-ng](https://github.com/arighi/virtme-ng) | GPL-2.0-only | `1.41`, hashes in `vm/kasan-requirements.txt` | KASAN/UBSAN/lockdep kernel runs (`.github/workflows/kasan.yml`) |
| Linux 6.12.112 | [kernel.org](https://cdn.kernel.org/pub/linux/kernel/v6.x/) | GPL-2.0 | tarball SHA-256 | the KASAN kernel and the benchmark kernel (`bench/kernel-build.sh`) |

## 2. Research that shaped decisions

### Design basis

- David K. Gifford, Pierre Jouvelot, Mark A. Sheldon, James W. O'Toole. "Semantic File Systems".
  ACM SOSP 1991, 16-25. https://doi.org/10.1145/121132.121138 — prior art for attribute queries
  over an existing filesystem (README, `docs/research-originality.md`).
- Dominic Giampaolo. *Practical File System Design with the Be File System*. Morgan Kaufmann, 1999.
  https://shop.elsevier.com/books/practical-file-system-design/giampaolo/978-1-55860-497-1 — the
  reference design of typed attributes and per-attribute indexes (ADR-0005; ADR-0009 alternatives
  C' and D).
- Shawn Wanxiang Zhong, Junxuan Liao, Jing Liu, et al. "Don't Let AI Agents YOLO Your Files:
  Information and Control in Agent-Native Filesystems" (YoloFS). ACM SOSP 2026, 353-378.
  https://doi.org/10.1145/3830418.3843858 — the closest prior art in form, a stacked kernel module
  for agents; it bounds what MoobFS may claim (README, `docs/research-originality.md`).

### The domain-scoped query (ADR-0009, `docs/spec-query-v2.md`)

- Stefan Büttcher, Charles L. A. Clarke. "A Security Model for Full-Text File System Search in
  Multi-User Environments". USENIX FAST 2005.
  https://www.usenix.org/conference/fast-05/security-model-full-text-file-system-search-multi-user-environments
  — restrict before computing; the analog of the measured timing channel.
- Aameek Singh, Mudhakar Srivatsa, Ling Liu. "Efficient and Secure Search of Enterprise File
  Systems". IEEE ICWS 2007, 18-25. https://doi.org/10.1109/ICWS.2007.80 — the partition must be
  the reader set: origin of the homogeneity rule.
- Eirini C. Micheli, Giorgos Margaritis, Stergios V. Anastasiadis. "Lethe: Cluster-Based Indexing
  for Secure Multi-user Search". IEEE BigData Congress 2014, 323-330.
  https://doi.org/10.1109/BigData.Congress.2014.54 (journal version: "Permission-based Index
  Clustering for Secure Multi-User Search", Transactions on Data Privacy 8(1), 2015, 29-53) — how
  few permission sets real collections have.
- Liang Wang, Paul Grubbs, Jiahui Lu, et al. "Side-Channel Attacks on Shared Search Indexes". IEEE
  S&P 2017, 673-692. https://doi.org/10.1109/SP.2017.50 — count leakage under post-filtering is
  exploited in practice; only partitioning closes it.
- Peter Bailey, David Hawking, Brett Matson. "Secure Search in Enterprise Webs: Tradeoffs in
  Efficient Implementation for Document Level Security". ACM CIKM 2006.
  https://doi.org/10.1145/1183614.1183686 — the cost of per-document checks.
- Alon Halevy, Flip Korn, Natalya F. Noy, et al. "Goods: Organizing Google's Datasets". ACM SIGMOD
  2016, 795-806. https://doi.org/10.1145/2882903.2903730 — a separate "readable by everyone"
  domain.
- Ruoming Pang, Ramon Caceres, Mike Burrows, et al. "Zanzibar: Google's Consistent, Global
  Authorization System". USENIX ATC 2019, 33-46.
  https://www.usenix.org/conference/atc19/presentation/pang — the "new enemy" risk of a lagging
  external index.
- Yiduo Wang, Linghang Meng, Liang Li, Jie Wu. "Accelerating Metadata Management of DFS via
  Speculative Permission Checking" (SPMeta). IEEE ICDE 2026, 3039-3052.
  https://doi.org/10.1109/ICDE65706.2026.00226 — per-directory reachability bits.
- Brenden Kokoszka, Patrick Donnelly, Douglas Thain. "Search Should Be a System Call". University of
  Notre Dame TR-2013-03, 2013. http://ccl.cse.nd.edu/research/papers/search-tr.pdf — precedent for
  an in-kernel search call issued on a directory.
- Dominic Manno, Jason Lee, Prajwal Challa, et al. "GUFI: Fast, Secure File System Metadata Search
  for Both Privileged and Unprivileged Users". SC22, 2022. https://doi.org/10.1109/SC41404.2022.00062
  — one index per permission domain, queried with the user's own credentials.
- Aleatha Parker-Wood, Christina Strong, Ethan L. Miller, Darrell D. E. Long. "Security Aware
  Partitioning for Efficient File System Search". IEEE MSST 2010.
  https://doi.org/10.1109/MSST.2010.5496990 — one index per domain can mean many small indexes.
- Andrew W. Leung, Minglong Shao, Timothy Bisson, et al. "Spyglass: Fast, Scalable Metadata Search
  for Large-Scale Storage Systems". USENIX FAST 2009.
  https://www.usenix.org/conference/fast-09/spyglass-fast-scalable-metadata-search-large-scale-storage-systems
  — namespace partitioning as both optimization and permission boundary.

### Query plans (phase Q2c, journals 0037 and 0044)

- J. Shane Culpepper, Alistair Moffat. "Efficient Set Intersection for Inverted Indexing". ACM TOIS
  29(1), 2010. https://doi.org/10.1145/1877766.1877767
- Daniel Lemire, Leonid Boytsov, Nathan Kurz. "SIMD Compression and the Intersection of Sorted
  Integers". Software: Practice and Experience 46(6), 2016, 723-749. https://doi.org/10.1002/spe.2326

  These grounded the leapfrog (galloping) intersection of Q2c; measurement then replaced it with a
  scan of the shortest run (journal 0044).

### The index built on demand (ADR-0012)

- C. Mohan, Inderpal Narang. "Algorithms for Creating Indexes for Very Large Tables Without
  Quiescing Updates". ACM SIGMOD 1992, 361-370. https://doi.org/10.1145/130283.130337

  The side-list method of ADR-0012 for building a domain's partition while writes continue.
  Checked on 2026-10-13 (Crossref metadata; the paper itself was not read in full).

### Security roadmap (proposal)

- Wen Xu, Hyungon Moon, Sanidhya Kashyap, et al. "Fuzzing File Systems via Two-Dimensional Input
  Space Exploration" (Janus). IEEE S&P 2019, 818-834. https://doi.org/10.1109/SP.2019.00035
- Seulbae Kim, Meng Xu, Sanidhya Kashyap, et al. "Finding Semantic Bugs in File Systems with an
  Extensible Fuzzing Framework" (Hydra). ACM SOSP 2019, 147-161.
  https://doi.org/10.1145/3341301.3359662

  Precedents for item S3 of `docs/security-roadmap.md` (journal 0017).

- Linux kernel documentation, "Fault injection capabilities infrastructure"
  (`Documentation/fault-injection/fault-injection.rst`, Linux 6.12.112): `failslab`,
  `fail_page_alloc`, the stack-trace filter that confines faults to one module, and the per-task
  `fail-nth` for systematic injection.

  The technique of journal 0096. Read in the source tree of the kernel the tests are built with.

## 3. Systems and kernel facilities

| Name | Reference | Used for |
| --- | --- | --- |
| overlayfs | https://docs.kernel.org/filesystems/overlayfs.html | model of the stacked layer, `override_creds`, the rule against touching the lower filesystem (ADR-0007) |
| eCryptfs | https://docs.kernel.org/filesystems/ecryptfs.html | model of a 1:1 stacked filesystem (ADR-0007) |
| backing_file helpers | [`fs/backing-file.c` at v6.12.112](https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/tree/fs/backing-file.c?h=v6.12.112) | I/O delegation in `layer/main.c` |
| FUSE and FUSE passthrough | https://docs.kernel.org/filesystems/fuse/index.html | the alternative rejected by ADR-0001 |
| xarray, rbtree | https://docs.kernel.org/core-api/xarray.html, https://docs.kernel.org/core-api/rbtree.html | the in-kernel index (`layer/index.c`) |
| iomap | https://docs.kernel.org/filesystems/iomap/index.html | the read-only M1 module (`kernel/main.c`) |
| dm-log-writes, dm-flakey | https://docs.kernel.org/admin-guide/device-mapper/log-writes.html, https://docs.kernel.org/admin-guide/device-mapper/dm-flakey.html | power-loss and I/O-error tests |
| KASAN, UBSAN, lockdep | https://docs.kernel.org/dev-tools/kasan.html, https://docs.kernel.org/dev-tools/ubsan.html, https://docs.kernel.org/locking/lockdep-design.html | the instrumented test kernel |
| fanotify, inotify | https://man7.org/linux/man-pages/man7/fanotify.7.html | change-feed experiment (journal 0030) |
| Landlock | https://docs.kernel.org/userspace-api/landlock.html | confinement experiment (journal 0030), ADR-0008 and ADR-0009 |
| idmapped mounts | https://docs.kernel.org/filesystems/idmappings.html | refused by the layer and tested |
| SQLite | https://www.sqlite.org (public domain) | the benchmark baseline (`bench/sqlite-baseline.sh`, journal 0047) and the WAL acceptance tests |
| Be File System query engine (Haiku) | [`Query.cpp`](https://github.com/haiku/haiku/blob/master/src/add-ons/kernel/file_systems/bfs/Query.cpp), MIT | how BFS chooses an index (ADR-0009 C') |
| GUFI | https://github.com/mar-file-system/GUFI, BSD-3-Clause | per-permission-domain index model (ADR-0009) |
| YoloFS | https://yolofs.github.io/, https://github.com/YoloFS/YoloFS, GPL-2.0 | closest prior art (see §2) |
| Elasticsearch document-level security | [Elastic docs](https://www.elastic.co/docs/deploy-manage/users-roles/cluster-or-deployment-auth/controlling-access-at-document-field-level) | precedent for count leakage (ADR-0009); the current page states that restricted documents still count in the scoring statistics |
| LMDB, RocksDB, LevelDB | https://www.symas.com/mdb, https://rocksdb.org/, https://github.com/google/leveldb | engines weighed for a user-space index (ADR-0009) |

## 4. Agent-memory clients analyzed

| Client | Repository | License | Analyzed |
| --- | --- | --- | --- |
| ai-memory | https://github.com/akitaonrails/ai-memory | MIT | source at `385b81e` and `88f8804` (`docs/research-ai-memory.md`, `docs/research-client-query-patterns.md`) |
| mem0 | https://github.com/mem0ai/mem0 | Apache-2.0 | query patterns at `b7ad69a` |
| Letta | https://github.com/letta-ai/letta | Apache-2.0 | V1 server, branch `archive` at `56ba9c2` |
| Letta Code | https://github.com/letta-ai/letta-code | Apache-2.0 | `271f119` |
| Graphiti | https://github.com/getzep/graphiti | Apache-2.0 | `964bb11` |
| LangGraph store | https://github.com/langchain-ai/langgraph | MIT | `799c2f0` |
| LangMem | https://github.com/langchain-ai/langmem | MIT | `48e3c11` |
| OpenAI Agents SDK sessions | https://github.com/openai/openai-agents-python | MIT | `125efa0` |

## Not included

Works surveyed but not used in a decision (most of `docs/research-academic-survey.md` and the
originality matrix), and tools only planned (syzkaller, LTP, sqlite-vec, USearch). They stay in the
research documents. Two page ranges could not be confirmed and are omitted: Büttcher and Clarke
(FAST 2005) and the SC22 article number of GUFI.

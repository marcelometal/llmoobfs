# Systematic survey of the academic literature for ADR-0009

## Abstract (English)

Systematic literature survey supporting ADR-0009 (role of the in-kernel `QUERY` and the choice between
alternative E, an in-kernel index partitioned by permission domain with `QUERY` issued on a directory,
and alternative B, a per-domain user-space SQLite index fed by fanotify). It extends
`docs/research-metadata-search.md` and its addendum without repeating them. We swept storage and
systems venues (FAST, OSDI, ATC, HotStorage, SOSP, EuroSys, MSST, SC/PDSW, TOS), research groups,
national laboratories (OSTI, HAL), arXiv (2023-2026) and Linux kernel venues, mainly through citation
chasing in OpenAlex from four anchors (Spyglass, Büttcher-Clarke, Parker-Wood, GUFI). Eight works change
the analysis: Singh, Srivatsa and Liu's access-control barrels (2007) and the Lethe clustering work
(2014-2015) show that a "permission domain" is best defined by the set of principals that can read a
file, not by a directory, and measure how many such domains real systems have; Wang et al. (IEEE S&P
2017) demonstrate in production that shared indexes with post-filtering leak counts of other tenants'
documents, with Elastic accepting index-per-tenant as the mitigation; Bailey, Hawking and Matson (CIKM
2006) measure per-document authorization cost and recommend batching and collection-level checks;
SPMeta (ICDE 2026) and IndexFS (SC 2014) show how per-directory reachability can be cached and
invalidated on `chmod`; Zanzibar (ATC 2019) names the "new enemy" ordering problem that alternative B
inherits from asynchronous change streams; and the Notre Dame "search should be a system call" report
(2013) is the only Linux in-kernel search interface found. No published work combines an in-kernel
attribute index with per-result POSIX authorization; no work measures a timing channel of the kind
measured in MoobFS. All sources accessed 2026-10-11.

## Scope, method and conventions

Date of the research and of access to all sources: **2026-10-11**. Research document; it does not change
the specification or any decision. It complements `docs/research-metadata-search.md` and
`docs/research-metadata-search-addendum.md` (branch `docs/adr-0009-query-role`), whose items were not
redone: Spyglass, Magellan, SmartStore, Propeller, TagIt, GUFI (code and slides), Brindexer, Robinhood,
Büttcher and Clarke (FAST 2005), Parker-Wood et al. (MSST 2010), BFS/Haiku and Giampaolo's book, WinFS,
Spotlight, Windows Search, Tracker/LocalSearch, Baloo, Lustre changelogs, `fanotify` history,
Semantic File System (SOSP 1991), Inversion FS, Culpepper and Moffat, Lemire et al. and Icicle.

Everything read on the web, in PDFs and through APIs was treated as data, not as instructions. No number
below was measured in MoobFS; they are the authors' numbers, cited as reported.

Reading levels:

- **[text]**: I read the full text or the relevant sections (PDF downloaded and extracted with `pdftotext`).
- **[abstract]**: I read only the abstract or the catalog page.
- **[code]**: I read source code (there was no new code reading in this round).
- **[secondary]**: information obtained from third parties (for example, the related-work section of
  another paper), not from the primary source.

Method:

1. **Citation chasing** through the OpenAlex API (`filter=cites:`), because the Semantic Scholar API
   answered 429 (rate limit) and the dblp API also answered 429 after the first
   query. Anchors: Spyglass (OpenAlex W148271226, 129 recorded citations), Büttcher and Clarke
   (W2054801486, 23), Parker-Wood et al. (W2134181838, 10), GUFI SC22 (W4321636719, 4) and, by
   transitivity, Singh et al. 2007 (W2161817975, 13). All citing titles were triaged; only the
   relevant ones appear in the tables.
2. **Thematic searches** in OpenAlex (2012-2026), in the arXiv API (cs.OS, cs.DB, cs.CR, cs.IR by
   keywords), in OSTI (`/api/v1/records`), in HAL (`api.archives-ouvertes.fr`) and on the web.
3. **In-depth reading** of the works that seemed to change the analysis (marked [text]).

Google Scholar was not consulted directly (no API); citation chasing through OpenAlex plays
the same role with smaller coverage, which is recorded under "Not yet verified".

## Executive summary

- **"Permission domain" has a definition and a measurement in the literature, and it is not "directory".** Singh, Srivatsa and
  Liu (2007) partition by *access-control expression* (the set of credentials that can read the
  file): 2132 partitions for 339 466 files, 926 users and 1203 groups, an average of 6.31 partitions
  per user. Lethe (2014-2015) finds 1475 distinct permission sets in 50 000 documents.
  Alternative E, as written (partition = first-level directory), only closes the timing channel if
  every file in the partition has the same set of readers; otherwise the channel reappears inside it.
- **The side channel is known, exploited and accepted as inherent to post-filtering.** Wang et al.
  (S&P 2017) extracted terms and counts of other people's documents on GitHub and other services built on
  Elasticsearch; Elastic answered that an index per tenant solves it, but can be expensive; the current
  Elasticsearch documentation warns that the user can "count" inaccessible documents that contain a term.
  (Re-check 2026-10-12: the current document-level security page states that restricted documents still
  count in the global index statistics used for relevance scoring; the quoted "count" wording itself was
  not found again on that page.)
  No work found treats a **timing** channel in a filesystem metadata index; the
  closest is the cross-site search attack by response time (Gelernter and Herzberg, CCS 2015).
- **Authorizing per collection, in batch, and caching reachability per directory** are the measured techniques for lowering
  the per-result cost: Bailey et al. (CIKM 2006, 0.11 ms per local document, 6.4 ms per LDAP query,
  batch recommendation), IndexFS (temporary permission grants per path component) and SPMeta
  (ICDE 2026, reachability bits per directory and u/g/o class, 99.7% of real paths eligible).
- **External indexes inherit the ordering problem between an ACL change and a query** ("new enemy",
  Zanzibar, ATC 2019). Alternative B needs a consistency marker, not only a rebuild
  after `FAN_Q_OVERFLOW`.
- **Vector databases redid the same trade-off in 2024-2026** (index per role versus a common index with
  filter): HoneyBee measures 13.5x lower latency than PostgreSQL row-level security with 1.24x memory.
- **No published work puts an attribute index in the Linux kernel with per-result POSIX
  authorization.** The only precedent of a search interface in the Linux kernel is a technical report from Notre
  Dame (2013), without an index. The MoobFS combination remains without direct precedent.

## 1. Conferences and journals

Scan by citation from the anchors and by thematic searches. Most of the FAST,
SC, MSST and TPDS works that cite Spyglass deal with scale and semantic organization, not with permission.

| Work | Mechanism | Permissions | Reported results | Implication for E or B | Level and URL |
| --- | --- | --- | --- | --- | --- |
| Singh, Srivatsa, Liu. "Efficient and Secure Search of Enterprise File Systems", IEEE ICWS 2007 (long version GIT-CERCS-07-07) | See 1.1 | Partition by credential expression; no filter at query time | See 1.1 | Defines domain by set of readers | [text] https://cercs.gatech.edu/www.cercs.gatech.edu/tech-reports/tr2007/git-cercs-07-07.pdf ; DOI 10.1109/ICWS.2007.80 |
| Bailey, Hawking, Matson. "Secure Search in Enterprise Webs: Tradeoffs in Efficient Implementation for Document Level Security", ACM CIKM 2006 | See 1.2 | Per-document check at query time | See 1.2 | Batch and per-collection check | [text] https://david-hawking.net/pubs/cikm127_bailey.pdf |
| Johnson, Keeton, Morrey, Soules et al. (HP). "From research to practice: experiences engineering a production metadata database for a scale out file system", USENIX FAST 2014 | StoreAll with Express Query: metadata database based on LazyBase (pipeline) beside a distributed FS; REST API for search and tagging | Not stated in the abstract | Experience report; no numbers in the abstract | Industrial precedent of "database beside the FS" with pipeline consistency (close to B) | [abstract] https://dl.acm.org/doi/10.5555/2591305.2591324 |
| Ren, Gibson. TableFS, USENIX ATC 2013 | Stacked FS over a local FS; all metadata in an LSM table (LevelDB); FUSE version | Inherits from the FS (it is not search) | 50% to 1000% over ext4/XFS/btrfs on metadata workloads (abstract) | Precedent of a stacked layer that reorganizes metadata; does not handle query or authorization | [abstract] OpenAlex "TABLEFS" |
| Ren, Zheng, Patil, Gibson. IndexFS, SC 2014 | Incremental partitioning by directory; cache of path components and their permissions on the client with short grants (leases) | Permission mutation waits for the leases to expire | Up to two orders of magnitude over PVFS/Lustre/HDFS on metadata (abstract) | Authorized-traversal cache mechanism applicable to E ("authorize once up to the directory") | [partial text] https://www.pdl.cmu.edu/PDL-FTP/FS/IndexFS-SC14.pdf |
| Zheng et al. DeltaFS Indexed Massive Directories, ACM TOS 2020 | In-stream reorganization of simulation output, indexed | Does not handle | Reads 1740x faster (abstract) | No relation to permission; discarded | [abstract] https://doi.org/10.1145/3415581 |
| Naps, Mokbel, Du. "Toward efficient search for ultrascale storage systems", PDSW 2011; Pantheon, 2011 | Proposes moving DBMS components into the storage system | Not stated | Position paper, no numbers in the abstract | Argument in favor of E (index inside), without evaluation | [abstract] https://doi.org/10.1145/2125636.2125638 |
| Parker-Wood et al. "Examining extended and scientific metadata for scalable index designs", SYSTOR 2013 | Characterizes scientific metadata; suggests columnar storage | Does not handle | Qualitative (abstract) | Little relation | [abstract] https://escholarship.org/uc/item/30b0g251 |
| Ohara. HCTrie, IEEE MSST 2013 | Hypercube trie for hundreds of dimensions | Does not handle | 5 million entries with 100 attributes; beats MySQL on ranges with fewer than 100 dimensions (abstract) | Multi-attribute structure; does not serve the MoobFS conjunctive-equality case better than list intersection | [abstract] https://doi.org/10.1109/MSST.2013.6558431 |
| Frank et al. Percival, IEEE MSST 2015 | Search over secret-shared data, via salted hash in an HSM | Custodians blind to query and result | Under 1 s per search over ~1 million files (abstract) | Different threat model (untrusted server); discarded | [abstract] https://doi.org/10.1109/MSST.2015.7208296 |
| Ismail, Ronström, Haridi, Dowling. ePipe, IEEE/ACM CCGrid 2019 | Bus that produces a consistent, ordered change stream from HopsFS to other engines (Elasticsearch) | Not stated in the abstract | Up to 56x throughput over HDFS INotify; average delay of 330 ms to Elasticsearch on the Spotify workload (abstract) | Shows what B requires from the change stream: guaranteed ordering and delivery, which `fanotify` does not give | [abstract] https://doi.org/10.1109/CCGRID.2019.00020 |
| Koyama, Tatebe. "Lustre Query", ACM HPDC 2026 | Offline monitoring from MDT backups | Administrator tool | Not verified | Discarded | [abstract] https://doi.org/10.1145/3806645.3816126 |
| Bjornson. "A Comparison of Three Open-source File Indexers: Robinhood, GUFI, XDU", 2026 | Comparison with Starfish, `find` and `du` | Not stated in the abstract | Text blocked from download | Possible source of independent GUFI numbers | [abstract] https://doi.org/10.1145/3785462.3815846 |

### 1.1 Singh, Srivatsa and Liu: access-control barrels (2007)

**Mechanism.** [text] "Searchability" is defined as read **and** execute permission along the whole
path and read on the file; directories with execute only count as not searchable (more restrictive
than Büttcher and Clarke, who require only execute). Files are grouped into *access-control barrels*
(ACB): each barrel contains exactly the files with the same credential expression (for example,
"uid bob" or "group students"), and each file belongs to only one barrel. An access hierarchy graph
links each user to the barrels the user reaches; the query is run on all of those barrels and the results
are combined with distributed search algorithms. There is no permission filter at query time. Two
optimizations reduce the number of barrels: duplicating file indexes in dominant barrels, or merging
barrels while accepting a controlled violation of the no-leak property.

**Permissions and leakage.** The text shows the counting attack on the single index with a filter
(count of files with "bankruptcy") and proves that one cannot have fewer barrels without duplicating indexes or
violating the property. POSIX ACLs enter as credential expressions. The theoretical bound on barrels per
user is exponential in the number of the user's groups.

**Results.** Real system with 926 users, 1203 groups, 339 466 files, 23 741 directories:
2132 barrels; barrels per user with maximum 25, mean 6.31, median 4.26 (second company: 21, 5.78,
3.96). Preprocessing 38.7 s (hierarchy) and 263.1 s (barrels), walking only 60% of the tree because
subtrees with a restrictive credential are assigned whole. Indexing 38% faster than the central
index (2902 s versus 4640 s), dominated by the "everyone" barrel. Mean query: 131.12 ms (central) versus
112.89, 130.68 and 149.90 ms combining 2, 5 and 10 barrels.

**Implication.** For E, the natural partition is not the first-level directory, but the set of
readers; the directory only works if the project guarantees (by creation policy) that everything inside it has
the same set of readers. For B, one SQLite database per barrel instead of per namespace is the design
with a minimality proof; the number of databases an agent opens (mean 6 in this data) is the cost.

### 1.2 Bailey, Hawking and Matson: cost of per-document security (CIKM 2006)

**Mechanism.** [text] Search model with document-level security (DLS) in two implementations (Sytadel, a
content management system, and CSIRO's Panoptic search engine, later Funnelback): candidates walked linearly, each one checked against the source system.
**Results.** A local check cost about 0.11 ms per document; an LDAP query to another server,
6.4 ms. With a 1 s budget and 0.25 s of overhead, the model allows 5454 local checks or 93
remote ones. Counting the exact total of visible results is what makes the cost unacceptable; stopping when
a page is complete (20 results) stabilizes the query at 75 to 90 ms. **Authors' recommendations:**
organize documents so that most of the checking is per collection, give up exact counts,
cache, and **check in batch** (one request with 100 to 1000 URIs).

**Implication.** The cost measured in MoobFS (about 1.7 µs per result) is already two orders below the
local check of 2006, but the shape of the problem is the same: alternative E is exactly "per-collection
check" (authorize the traversal up to the directory once), and B's batch authorization is the batch
recommendation. The work also confirms that not exposing a total count is a performance choice, as well as a
security one.

## 2. Research groups

| Group | What was found | Relevance |
| --- | --- | --- |
| UC Santa Cruz SSRC/CRSS | Parker-Wood, dissertation "Improving File Management Through Provenance And Rich Metadata" (2014), whose abstract mentions "secure, effective, and efficient search" [abstract] https://escholarship.org/uc/item/6pc1t0nw ; LiFS (Ames et al., MSST 2005) [abstract]; Percival (MSST 2015) [abstract]; SYSTOR 2013 (table 1) | The dissertation probably extends MSST 2010; the PDF answered 403 and was not read |
| CMU Parallel Data Lab | TableFS, IndexFS, DeltaFS (table 1); Patil et al. "In Search of an API for Scalable File Systems: Under the Table or Above it?" (record in the CMU repository; original venue and year not verified) [abstract]; Soules and Ganger, context-based attribute assignment (CMU-PDL-04-105) [abstract] | Only IndexFS's permission cache with leases is relevant |
| UW-Madison ADSL | Nothing on metadata indexing or search with permissions in the searches made | No result |
| MIT PDOS and successors of SFS | Semantic File System (already covered); HAC, Gopal and Manber (OSDI 1999, Arizona) [abstract]; Logic File System, Padioleau and Ridoux (IRISA/INRIA, 2002-2005) [abstract]; hFAD, Seltzer and Murphy (HotOS 2009, Harvard) [abstract] | No abstract deals with per-result permissions |
| Stony Brook FSL | VT-trees (FAST 2013) and metadata-update works cite Spyglass, but deal with storage structures | No result |
| Microsoft Research | Bhatt et al. "Enterprise AI Must Enforce Participant-Aware Access Control" (arXiv 2509.14608, 2025; MSR India) [abstract] | See section 4 |
| Google | Goods (SIGMOD 2016) [text]; Zanzibar (USENIX ATC 2019) [text] | See 2.1 and 2.2 |

### 2.1 Goods (Halevy et al., SIGMOD 2016)

[text] https://storage.googleapis.com/gweb-research2023-media/pubtools/pdf/45390.pdf (DOI
10.1145/2882903.2903730). Google's dataset metadata catalog (key-value store
plus inverted index). **Permissions:** the text says the catalog indexes more than 26 billion datasets
"although it includes only" those that are readable by all engineers, and that the size should more than double
when the restricted-access ones are indexed. **Implication:** the largest published metadata catalog
avoided the problem by indexing only the public domain. For E and B, a domain "shared by everyone" can
have a single index without risk; the real problem lies in the private domains.

### 2.2 Zanzibar (Pang et al., USENIX ATC 2019)

[text] https://www.usenix.org/system/files/atc19-pang.pdf. Google's global ACL system (Drive,
Calendar, YouTube and others), on Spanner. **Relevant mechanism:** (1) the **"new enemy"** problem:
evaluating an ACL out of order with respect to ACL or content changes, for example applying the old
ACL to new content; solved with *zookies*, tokens that carry a content timestamp and
force the check to use a snapshot at least as new; (2) **Leopard**, an index that flattens nested groups
into sets for fast checks, fed by an ordered change stream (Watch);
(3) the Expand API exists so that clients can build **access-controlled search indexes**, and the
text says that serving search results requires tens to hundreds of checks, with hot spots on
common groups. **Results:** 95th percentile below 10 ms and availability above 99.999% over three
years (paper abstract). **Implication:** B inherits the "new enemy": between a `chmod` and the external index
reflecting the change, the index can return names and attributes to the old reader. Batch authorization in the
kernel solves the content, not the metadata copied into the index. The "monotonic change counter" already
proposed in ADR-0009 for `GET_INFO` is the local equivalent of a zookie; the literature suggests that it
be required in the query, not only exposed. E does not have this problem (index and permission change in the same
call).

## 3. National laboratories and institutes

| Source | What was found | Level and URL | Implication |
| --- | --- | --- | --- |
| LANL, GUFI SC22 paper | Still no open copy: the SC22 page and LARO only have the abstract; the abstract on the USRC/LANL page says the databases are "sharded" so as to enforce POSIX permissions and that the "rollup" lifts information from subtrees with the same permissions | [abstract] https://usrc.lanl.gov/emc3-project-gufi.php | Nothing new beyond the addendum |
| LANL, patent "Grand unified file indexing", US 11 048 699 (2021; Grider, Manno, Poole, Bonnie, Inman) | Tree that replicates the hierarchy, one metadata store per directory, "efficient user-space access controls", summary tables to prune traversal | [abstract] https://www.osti.gov/biblio/1824127 | The GUFI design is patented; the code is BSD-3 (addendum). Patent license not verified |
| LANL, LA-UR-22-21380 (DisrupTECH slides, 2022) | Outreach material; states that GUFI "preserves and honors" permissions | [text] https://www.osti.gov/biblio/1846115 | No new technical data |
| LANL, Jacobi, "Applied Parallel Metadata Indexing" (2012) | GPFS archive of 4+ PB and 170 million files; metadata imported into MongoDB and indexed by attribute; **one table per user with only the records that user can read**, access by credentials; FUSE interface | [abstract] https://www.osti.gov/servlets/purl/1048692 | Precursor of "one index per domain" at LANL itself, with domain = user |
| LANL, "Access Controlled AI Context with GUFI" (LA-UR-26-24285) | Already read in the addendum; OSTI returned no record for that title on this date | — | — |
| SNL, Lofstead, "Metadata Driven Data Management" (2019) and Lawson and Lofstead, "SystemX" (HPDC 2020) | Records without abstract in OSTI | [abstract] https://www.osti.gov/biblio/1641766 ; https://www.osti.gov/biblio/1764669 | Not verified |
| LLNL, Ames, Gokhale, Maltzahn, QFS/Quasar (2009-2010) | Metadata-rich FS with a graph model and a query language derived from XPath; superior ingestion and query comparable to the "FS + relational database" pattern | [abstract] https://www.osti.gov/biblio/975221 | Precedent of query inside the FS; abstract does not talk about permissions |
| ORNL | TagIt (already covered); "Scientific user behavior and data-sharing trends in a petascale file system" (SC 2017) cites Spyglass; MOSIQS (IEEE Access 2021) indexes metadata in persistent memory | [abstract] | No handling of permissions in the abstracts |
| ANL, NERSC | Nothing specific in the OSTI and OpenAlex searches | — | No result |
| CEA (HAL) | Searches in HAL for "metadata search file system", "indexation métadonnées système de fichiers" and "robinhood policy engine" brought no new work on search with permissions (only "Multicriteria File-Level Placement Policy for HPC Storage", 2025, on placement) | https://api.archives-ouvertes.fr | No result |
| INRIA | Logic File System (IRISA) | [abstract] | No handling of permissions in the abstract |
| Jülich, HLRS | Nothing found | — | No result |

## 4. arXiv and access-controlled search

### 4.1 Table

| Work | Mechanism | Permissions | Results | Implication for E or B | Level and URL |
| --- | --- | --- | --- | --- | --- |
| Wang, Grubbs, Lu, Bindschaedler, Cash, Ristenpart. "Side-Channel Attacks on Shared Search Indexes", IEEE S&P 2017, pp. 673-692 | See 4.2 | Filter after scoring | See 4.2 | Partitioning is the accepted mitigation | [text] http://seclab.illinois.edu/wp-content/uploads/2018/01/sp17stress.pdf ; DOI 10.1109/SP.2017.50 |
| Micheli, Margaritis, Anastasiadis. "Lethe: Cluster-Based Indexing for Secure Multi-user Search", IEEE BigData Congress 2014, 323-330; journal version "Permission-based Index Clustering for Secure Multi-User Search", Transactions on Data Privacy 8(1) 2015, 29-53 (titles corrected 2026-10-12, see docs/references.md) | See 4.3 | Index shared only by those who can read everything in it | See 4.3 | How many domains exist and the cost of merging them | [text] https://csl.cse.uoi.gr/wp-content/uploads/lethe-tdp2015.pdf |
| Zerr, Demidova, Olmedilla, Nejdl, Winslett, Mitra. Zerber, EDBT 2008 | r-confidentiality: degree of leakage about terms of inaccessible documents; secret sharing and term merging | Limited trust in a central index | Insertion and search with good performance on real data (abstract) | Wang et al. say it is not clear what guarantee the statistical variant gives | [abstract] https://doi.org/10.1145/1353343.1353380 |
| Bjørklund, Götz, Gehrke. "Search in social networks with access control", 2010 (venue not verified); Bjørklund et al. "Workload-aware indexing for keyword search in social networks", CIKM 2011 | Organization of access-control metadata chosen by workload cost models; HeapUnion operator | Each user sees a unique subset | Up to 2x on the operator and up to 3x over previous work (2011 abstract) | If E grows into "QUERY over several domains", choosing the organization by cost is the precedent | [abstract] https://doi.org/10.1145/1868366.1868370 ; https://doi.org/10.1145/2063576.2063656 |
| Gelernter, Herzberg. "Cross-Site Search Attacks", ACM CCS 2015 | Uses the response **time** of searches, measured from the browser, to extract private information | Bypasses the same-origin policy | Effective attacks with better tools, despite network variance (abstract) | Only timing attack on a search index found; confirms that a timing channel in search is exploitable | [abstract] https://doi.org/10.1145/2810103.2813688 |
| Zhong, Lentz, Narodytska, Szekeres, Rong. HoneyBee, arXiv 2505.01538, 2025 | Overlapping partitions guided by the RBAC structure, with replicas chosen by optimization (memory, latency, recall) | Index per role or common index with row-level security | Up to 13.5x lower latency than PostgreSQL RLS (pgvector, HNSW) with 1.24x memory; partition per role cost 7x the memory of the single index; 90.4% less extra memory than per-role indexes | Same trade-off as B and E in another kind of index; replicating across domains is a measured option | [partial text] https://arxiv.org/abs/2505.01538 |
| Han, Chakraborty, Mehrotra. Veda/EffVeda, arXiv 2605.01342, 2026 | Partitions by role combination into disjoint blocks, organized in a lattice with copying and merging under a memory budget | Query covers only authorized nodes; "impure" nodes pruned by distance bound | Higher throughput with high recall, near the budget (abstract) | Formalizes "domain = role combination", the same as the 2007 barrels | [abstract] https://arxiv.org/abs/2605.01342 |
| Jin, Wu, Hu, Maggs, Zhang, Zhuo. Curator, arXiv 2401.07119, 2024 | Per-tenant clustering tree encoded as a subtree of a common tree | Tenant | Performance of a per-tenant index with the memory of a single index with filter (abstract) | Shows that a common index with per-domain structure is viable; analogous to E with partitions sharing the structure | [abstract] https://arxiv.org/abs/2401.07119 |
| Yalamarthi, Pappachan. "Policy-aware Vector Search", arXiv 2606.19803, 2026 | Vision: formalizes fine-grained access control in vector databases and compares strategies | Fine-grained, by policy | Preliminary | Confirms that the topic remains open in 2026 | [abstract] https://arxiv.org/abs/2606.19803 |
| Bhatt et al. (Microsoft). "Enterprise AI Must Enforce Participant-Aware Access Control", arXiv 2509.14608, 2025 | All content used in training, retrieval or generation must be authorized for **all** participants of the interaction | Deterministic, not probabilistic | Exfiltration attacks demonstrated; deployed in Microsoft Copilot Tuning (abstract) | `QUERY` uses the credentials of one process; an agent that answers several users would need the intersection, which neither E nor B offers | [abstract] https://arxiv.org/abs/2509.14608 |
| Jayaraman et al. "Permissioned LLMs", arXiv 2505.22860, 2025 | Access control overlaid on the answers of a fine-tuned LLM (PEFT per domain) | Per data domain | DDI and UGI metrics on five datasets (abstract) | Another level (model, not index); discarded for the ADR | [abstract] https://arxiv.org/abs/2505.22860 |
| Johnson et al. AIM, arXiv 2609.12320, 2026 | Multi-user agent memory that classifies information as private or public and applies control in the index | Owner-private or public | 96.0% accuracy in visibility classification (abstract) | Same two-domain model as Goods; in MoobFS the classification would be by file mode, not by model | [abstract] https://arxiv.org/abs/2609.12320 |
| Li et al. BudgetLeak, arXiv 2511.12043, 2025 | Membership inference in RAG through the generation budget | — | Beats baselines (abstract) | Side channel in RAG, but of another nature; discarded | [abstract] https://arxiv.org/abs/2511.12043 |

### 4.2 Wang et al.: side-channel attacks on shared indexes (S&P 2017)

**Mechanism of the attacked system.** [text] Multi-tenant Elasticsearch and Solr store documents of
many users in shared indexes (shards), with document frequency (DF) per term
computed over all of them; access control is a filter (by tenant id in the query or a filtered
alias) applied afterwards. **Attack (STRESS):** the TF-IDF score of an attacker's document depends on the
global DF; the attacker maps how many shards exist, places its own documents in each one and, by
co-residence, discovers terms of other people's documents or **how many other people's documents contain a term**.
Demonstrated on GitHub, Xen.do and others; a variant using only the order of results works without
scoring. **Countermeasures discussed:** index per user ("received wisdom" considers it expensive at
scale); Büttcher and Clarke's per-user view; "blind" DFs (public documents only) or DFs from a
public corpus (latency increase of about 1% in microbenchmarks with Lucene 6.3.0, equal ranking
quality). **Elastic's answer:** index per tenant for small deployments, admitting that it
can be prohibitive in large ones; turn off scoring when acceptable. **Current documentation** [text]
https://www.elastic.co/docs/deploy-manage/users-roles/cluster-or-deployment-auth/controlling-access-at-document-field-level :
document-level security does not affect the global scoring statistics, and the user can still
discover terms that exist only in inaccessible documents and count how many contain them.

**Implication.** `QUERY` returns neither score nor count, but the timing channel measured in MoobFS
(about 1.15 µs per invisible candidate) delivers the same information (count of other people's entries that
match a predicate) by another means. The search literature offers no constant-time mitigation;
the accepted mitigation is not to walk entries of other domains, that is, to partition (E, or B with one
database per domain).

### 4.3 Lethe: how many domains exist and how much it costs to merge them (2014-2015)

**Mechanism.** [text] A shared index is confidential if it is used only by users authorized
to search **all** the documents in it. Documents are grouped into "families" by the set of
authorized searchers; similar families are clustered (DBSCAN or K-medoids); each cluster has an
index for the intersection of the searchers and private or additional indexes for the differences, with a
duplication threshold. **Results** (DocuShare: 50 000 documents, 1475 families; Elasticsearch
0.90.1): in the suggested configuration, 11.6 indexes queried per searcher and 7.8 indexes updated per
document; lowering the threshold triples the search cost (11.6 to 35.8) and reduces the update cost by only 41%.
Response time of 17.8 to 21.8 ms with 1 to 16 clients in that configuration; throughput from 37.0 to 339.8
queries/s; configurations with a lot of duplication stayed below 19% of queries within 50 ms because they did not
fit in memory.

**Implication.** Even with partitioning by permission, a real user queries on the order of ten indexes. For
E, this favors a `QUERY` on one directory at a time, leaving the combination of domains to the client;
for B, ten SQLite databases opened per query is the expected cost if the domains are sets of
readers and not namespaces.

### 4.4 Other industrial document-level security systems

| System | Mechanism | Implication | Level and URL |
| --- | --- | --- | --- |
| OpenSearch DLS | DSL query per role, combined by OR across roles; "Lucene-level" (more efficient), "filter-level" and adaptive modes; DLS does not restrict writes | Filter inside the engine; the page asks for simple queries to limit impact, without numbers | [text] https://docs.opensearch.org/latest/security/access-control/document-level-security/ |
| Azure AI Search, per-document control | Filter by identity string or "POSIX-like" ADLS Gen2 ACL copied into the index and compared with the Entra token at query time; ACL changes at the source only take effect after a new sync (indexer run or push) | Early binding with a declared staleness window; it is B's model without correction in the kernel | [text] https://learn.microsoft.com/en-us/azure/search/search-document-level-access-overview |
| PostgreSQL row-level security | Per-row policies; uniqueness constraints and foreign keys ignore RLS; the documentation says to avoid "covert channel"; `leakproof` functions can be evaluated before the policy | A relational index with per-row filter has documented side channels; SQLite has no RLS | [text] https://www.postgresql.org/docs/current/ddl-rowsecurity.html |
| Vespa | Search found no official documentation on document-level security | Not verified | — |

## 5. Indexing or query on the Linux kernel side

| Work | Mechanism | Permissions | Results | Implication | Level and URL |
| --- | --- | --- | --- | --- | --- |
| Kokoszka, Donnelly, Thain. "Search Should Be a System Call", Notre Dame TR-2013-03 | See 5.1 | Permission errors reported per path in the result buffer | Reduction of 5% to 95% in system calls (abstract) | Only precedent of search as a Linux kernel call | [text] http://ccl.cse.nd.edu/research/papers/search-tr.pdf |
| Wang, Meng, Li, Wu (China Telecom). SPMeta, "Accelerating Metadata Management of DFS via Speculative Permission Checking", IEEE ICDE 2026 | See 5.2 | POSIX u/g/o; ACLs not mentioned | See 5.2 | Cuts the per-result cost without partitioning | [text] https://cis.temple.edu/~jiewu/research/publications/Publication_files/ICDE2026-SpeculativePermissionChecking.pdf |
| LSFMM+BPF 2026, Szeredi, "Caching for extended attributes" (LWN, Jake Edge, 2026-06-02) | Common xattr cache (probably a hash table) for FUSE and others, consolidating `simple_xattr` and the NFS cache | Not discussed | No objections; details open | Cache infrastructure, not index; no discussion of search by value | [secondary] https://lwn.net/Articles/1074919/ |
| LSFMM+BPF 2024, Overstreet, "New APIs for filesystems" (LWN) | Request for an iterator interface for xattrs, in the style of `getdents()` | — | Discussion | Iterating xattrs is a prerequisite for a scan in B; it does not exist yet | [secondary] https://lwn.net/Articles/975444/ |
| LSFMM+BPF 2026, "An update on fanotify" | Already covered in the main document | — | — | — | [secondary] https://lwn.net/Articles/1075829/ |
| Basani, "Filesystem-Native Metadata Indexing for Modern Workloads" (2026, Zenodo deposit, journal indicated by OpenAlex: 工程科学与技术) | Metadata indexing utility inside the volume, through a small system-call interface, transactional with the FS | Mentions "access control indicators" as an indexable field | No numbers in the abstract | Proposal close to E, without verifiable evaluation and no code found; treat with caution | [abstract] https://doi.org/10.5281/zenodo.21925184 |

### 5.1 Search as a system call (Notre Dame, 2013)

[text] Proposes `search` (with library variants `opensearch`/`readsearch`) that receives paths and
patterns and returns a buffer of entries (path, optional `stat`, error), like `getdents`. Three
implementations of about 500 lines each: in the Linux kernel (portable system-call layer, using
abstract VFS operations for directory reading and `stat`; the authors say a complete version would
add a search operation to the VFS to delegate to optimized drivers), in a `ptrace` sandbox
(Parrot) and in a user-space distributed FS (Chirp). No index: it is a scan done in the kernel.
Permissions: the search runs with the caller's credentials; a directory without list permission produces an error
recorded in the result (for example, `EPERM` on "Private"), it does not abort the search. Recursion limited to 16
levels (`EOVERFLOW`) by the kernel stack. **Implication:** confirms that the shape "query issued from
a directory, authorized as a normal traversal" (that of E) is natural in the VFS; it also shows that the idea
did not go forward in mainline (no LKML discussion was found).

### 5.2 SPMeta: speculative reachability per directory (ICDE 2026)

[text] Observes that, in five real AI and HPC namespaces, more than 99.7% of paths follow a
"descending permission pattern" (bits that do not increase from owner to group to others along the
path). It stores, per directory, one bit per class (user, group, others) saying whether that class
certainly reaches the directory; with this the path check becomes O(1) in most cases, falling back
to the full check when the pattern does not hold. A `chmod` invalidates the reachability states of the
subtree as a range; rename uses subtree relocation in a trie (MART). Results: remote calls
and database queries go from O(N) to O(1) in most operations; 1.44x to 2.86x aggregate throughput over the
state of the art. ACLs and supplementary groups are not mentioned in the extracted text.
**Implication:** the 1.7 µs per-result cost of `QUERY` comes from path resolution with the
caller's credentials; a per-directory reachability cache, invalidated by `chmod`/`chown`/`rename`,
attacks that cost within alternative A or E without changing the UAPI. It needs care with ACLs, which the
paper does not handle.

## Works that change the analysis of ADR-0009

1. **Singh, Srivatsa and Liu (2007) and Lethe (2014-2015): the domain is the set of readers.** The ADR
   defines E by "first-level directory chosen at mount". The two works show that the
   partition that closes the leak is by credential expression, with a minimality proof (2007) and with a
   measurement of how many domains appear (2132 for 339 thousand files; 1475 for 50 thousand documents). If a
   MoobFS namespace has files with different sets of readers (a `0600` of another uid
   inside a group-readable namespace), the timing channel reappears inside the partition. Ways out: (a)
   E with partitioning by (namespace, set of readers) and QUERY walking only the caller's partitions,
   or (b) declare as an invariant that a namespace is homogeneous and refuse (or account for) files that
   violate it. The ADR needs to choose.
2. **Wang et al. (S&P 2017) and the Elasticsearch documentation: count leakage is inherent to
   post-filtering and was exploited in production.** This strengthens the ADR's argument that only not
   walking other domains closes the channel, and shows that the industry lives with the leak for cost reasons.
   For MoobFS, which measured the channel, documenting the choice (close it with partitioning or accept it as a limitation)
   now has an explicit precedent.
3. **Bailey, Hawking and Matson (CIKM 2006): per-collection and batch checking.** Gives a measured basis to the two
   new pieces of the ADR (authorization once up to the directory in E; batch authorization in B) and to the decision to
   not expose a total count.
4. **SPMeta (ICDE 2026) and IndexFS (SC 2014): the per-result cost can drop without partitioning.** A
   reachability cache per directory and class, invalidated by permission changes, is an alternative C'' smaller than
   E for the problem of 1.7 µs per result. It does not solve the timing channel nor the growth per
   candidate; therefore it complements E, it does not replace it.
5. **Zanzibar (ATC 2019): the "new enemy" is B's risk.** The window between `chmod` and the external index is not
   only delay: it is serving metadata to someone who lost access. The literature solves it with a consistency marker
   required at query time. If B goes forward, the monotonic counter of `GET_INFO` must be compared by the
   client before using the index, and batch authorization must refuse when the index is behind.
   This weighs in favor of E on the "index updated immediately" criterion.
6. **Goods (SIGMOD 2016) and AIM (2026): separate the public domain.** A single index for what is readable
   by everyone and partitions only for the private reduces the number of domains in practice; it applies to E and to B.

They do not change the analysis, although close: HoneyBee, Veda and Curator (confirm the trade-off in vector databases,
without handling side channels); Search Should Be a System Call (interface precedent, with neither index nor
security evaluation); "Participant-aware access control" (points to a gap, that of agents serving
several users, outside the scope of ADR-0009 but worth recording in the security roadmap).

## Sources scanned without relevant result

- **Citers of Spyglass in OpenAlex (129 records):** all triaged; no handling of permissions beyond
  Parker-Wood (MSST 2010, already covered) and GUFI. Examples discarded: SmartCuckoo (ATC 2017), VT-trees
  (FAST 2013), Haystack (OSDI 2010), "Optimizing Every Operation in a Write-optimized File System" (FAST
  2016), "Writes Wrought Right" (TOS 2017), D2-Tree (ICDCS 2018), MIQS (SC 2019), GraphMeta (CLUSTER
  2016), "The case for sampling on very large file systems" (MSST 2014), "Using Property Graphs for Rich
  Metadata Management in HPC Systems" (PDSW 2014), VSFS (PDSW 2014), FAST (SC 2014, approximate search),
  "Just-in-Time Analytics on Large File Systems" (TC 2011), DelveFS (CLUSTER 2020), SANE and Rapport
  (Nebraska), MBFS (J. Supercomputing 2015), "An Integrated Indexing and Search Service for Distributed
  File Systems" (TPDS 2020, version of TagIt), "Efficient Metadata Indexing for HPC Storage Systems"
  (CCGrid 2020, Brindexer, already covered).
- **Citers of GUFI SC22 (4):** SwiftKV (2025), Lustre Query (HPDC 2026), Bjornson (2026), SPMeta
  (ICDE 2026). Only SPMeta is relevant.
- **Citers of Büttcher and Clarke (23) and of Parker-Wood (10):** besides the items in the tables, works on
  search over encrypted data (Information Systems 2018; TIFS 2015), SeDas (TMAG 2013), "Using provenance
  to efficiently improve metadata searching performance" (FGCS 2014), "Privacy preserving document
  indexing infrastructure for a distributed environment" (PVLDB 2008), "Scalable full-text search for
  petascale file systems" (PDSW 2008), "Bring Precision and Access Control to Business Document Search"
  (SNPD 2008) and "Access control for sharing semantic data across desktops" (2007): titles and abstracts only;
  none brings a mechanism beyond those already described.
- **Groups:** UW-Madison ADSL, Stony Brook FSL, Jülich, HLRS, ANL, NERSC: nothing on metadata search
  with authorization.
- **HAL (CEA, INRIA):** three queries, nothing relevant.
- **OSTI:** "GUFI" (30 records, only slides, patent and the poster/outreach); "Access Controlled AI
  Context" and "GUFI retrieval augmented": no record on this date.
- **arXiv:** queries for "access control" with "retrieval-augmented", "permission" with RAG, "side
  channel" with RAG/vector database/search index, "permissioned LLM", "role-based access control" with
  "vector database", "access control" with "agent memory". Out-of-scope items discarded: RAGent
  (policy generation), PrediQL, SmartLLM, FragFuse and SAMEP (agent protocols), "Agent-Assisted
  Side-Channel Attacks on Non-Prefix KV Cache in RAG" (channel in the model's KV cache, not in the index).
- **Kernel:** OpenAlex for "extended attributes indexing Linux kernel", "in-kernel file search index
  Linux VFS", "attribute-based file system query kernel module", "tag-based file system Linux kernel":
  nothing beyond S-VFS (IEICE 2007, title only) and unrelated works. LWN: LSFMM+BPF 2026 index
  without a session on search or xattr indexing.
- **Vespa:** no official page on document-level security found in the search.

## Not yet verified

- **Google Scholar, Semantic Scholar and dblp were not actually used** (no API and 429 answers); the
  citation coverage depends on OpenAlex, which records fewer citations than Scholar. A second
  round with dblp per venue (FAST, OSDI, ATC, HotStorage, SOSP, EuroSys, TOS, 2015-2026) may find
  papers that do not cite the four anchors.
- **Proceedings of HotStorage, EuroSys and OSDI** were not walked page by page.
- **Full text** of Parker-Wood's dissertation (2014; the PDF answered 403), of Büttcher's thesis
  ("Multi-user file system search", 2007), of Bjørklund et al. (2010, 2011), of Zerber (2008), of
  Bjornson (2026; blocked) and of Johnson et al. (FAST 2014).
- **GUFI SC22 paper** still has no open copy found.
- **SPMeta and ACLs/supplementary groups:** the extracted text does not mention them; not verified whether the
  published version handles them.
- **HoneyBee:** only the abstract, introduction and excerpts were read; I did not verify how they measure permission
  updates.
- **License of patent US 11 048 699** relative to GUFI's BSD-3 code.
- **Timing-channel mitigation in indexes** (constant time, noise, rate limits): no source
  found proposes it for metadata search; the claim of absence holds only for the searches made.
- **Vespa** and **Solr**: document-level security mechanism not verified in this round.

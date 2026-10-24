# Known algorithms and their fit to MoobFS

Date: 2026-10-20. Status: research assessment, not an implementation decision.
Source baseline: `02381fba3f0a908623cac52180a39f24e02e98a6`; planning context:
G1–G6 in [ROADMAP](../ROADMAP.md), local commit `8f773e5`.
No algorithm was implemented, simulated or benchmarked in this assessment.
Benefits below are hypotheses unless explicitly attributed to a historical run.

## 1. Start from the actual implementation

In `layer/index.c` at the baseline:

- `moobl_ix_name.entries` is a red-black tree ordered by type, value and inode.
  Its `count` covers the attribute partition, not each equality value's run.
  The index already separates permission domains; an optimization must preserve
  that separation, including statistics used to plan a query.
- Despite its name, `leap_collect` now chooses the shortest remaining equality
  run by advancing the runs together, then scans that run and checks all terms.
  `query_run` resets the choice per call. This applies to unordered queries with
  at least two plain equality terms; ordered queries walk the ordering attribute.
- `domain_lru` scans built domains for the oldest `used_at`; `domain_release`
  drops an entire domain. `moobl_shrink_count` estimates reclaimable memory by
  dividing total indexed memory among built domains; it lacks per-domain byte
  accounting. Existing try-lock and reclaim restrictions are part of correctness.

Two relevant historical results must not be lost:

1. [0044](history/0044-q2-measured-equality-plan.md): tree-seek leapfrog was
   2.7–3.6 times slower than a short first-term scan on tested shapes. The shortest
   run replaced it. That pilot has substantial noise and unclustered data; it is
   evidence against blindly restoring the old plan, not a theorem about leapfrog.
   Its later correction also retracts the original attribution of ordered-query
   cost to per-inode attribute lookup. Do not resurrect that discarded diagnosis.
2. [0103](history/0103-releases-measured-and-cursor-test.md): with space for about
   2.5 of eight domains, a cyclic workload rebuilt every domain on every query.
   This was one host/data set and a limited experiment. No alternative eviction
   policy was measured, and no policy is guaranteed to cure a uniform cycle.

## 2. Candidate assessment

Priority here means a recommendation for a later authorized experiment. It does
not change P0 or authorize any G-item. Rough experiment effort: M = one to three
days; L = one week or more, excluding review/infrastructure delays. These are
scoping estimates, not estimates for production-ready implementations.

| Candidate | Fit and potential benefit | Cost or condition | Recommendation / experiment effort |
| --- | --- | --- | --- |
| Subtree cardinalities in an augmented red-black tree | Reduce the work needed to select the shortest equality run without changing the index family | Counts must remain correct on every mutation; additional bytes and write work; measure whether selection matters first | First tier, current structured queries / M profiling, L kernel experiment |
| 2Q, ARC, S3-FIFO versus current LRU | Retain repeatedly queried domains when one-time accesses pollute residency | Whole domains have different sizes and rebuild costs; cache hit rate alone is insufficient; not a replacement for S9 fairness | First tier, G2 / M trace comparison, L kernel experiment |
| Exact vector scoring over eligible candidates | Simple semantic baseline and a correctness oracle after structured filtering | Linear work in eligible vectors and their dimensions; enumerate all pages | First semantic baseline, G5/G6 / M |
| BM25 plus an inverted text index | Lexical branch for names, technical terms and identifiers that embeddings may retrieve poorly | Tokenization, language, index maintenance and corpus statistics need validation | First retrieval tier, G4/G5 / M |
| Reciprocal Rank Fusion (RRF) | Combine lexical and semantic rankings without assuming comparable raw score scales | Candidate depth and rank ties affect results; cannot recover an item missing from every input list | First fusion baseline, G5 / M |
| Maximal Marginal Relevance (MMR) | Optional reranking to reduce redundant memories in a limited context budget | Diversity may displace useful corroborating facts; similarity is not factual equivalence | After the baseline, client-side G5/G6 / M |
| HNSW with an engine's supported filtering | Approximate vector retrieval if exhaustive scoring exceeds the measured budget | Additional graph memory/build cost, recall trade-off, deletion and filter behavior depend on the engine | Conditional, user space only, G5 / L |
| Roaring compressed bitmaps | Candidate set intersections for repeatedly queried categorical fields | Additional index/mapping, sparse 64-bit identities, write amplification and ordering still need handling | Later, only after a density/size profile / L |

### 2.1 Subtree cardinalities: a contained kernel candidate

Store a subtree entry count and compute the size of an equality interval from
the difference between its boundary ranks. For a balanced tree this makes each
count query logarithmic in tree size; that is an algorithmic expectation, not a
measured MoobFS speedup. It addresses the preliminary run walk, not result
materialization or ordered scans. A per-value counter is an alternative, with
its own allocation and accounting cost. Neither is selected here.

Linux 6.12.112 provides augmentation callbacks for insertion, deletion and
rotation [K1]. The module would still implement and validate the count invariant.
This revisits the alternative already deferred in journal 0044. Start by profiling
selection cost, then compare writes as well as reads; reject the change if its
maintenance cost outweighs the saved query work. Preserve inode result order,
cursor continuation, empty intervals and authorization. An estimate may choose
a slower plan; it must never change the matching set.

Do not generalize this into a full kernel query planner. Adaptive switching
between the existing scan and leapfrog is only worth reopening for a measured
distribution where the old plan wins, with switching overhead included.

### 2.2 Eviction: compare policies before changing reclaim

2Q distinguishes short-lived accesses from reuse [C1]. ARC adapts the balance
between recency and frequency and was formulated for uniform-size pages [C2].
S3-FIFO uses small, main and ghost queues to keep one-hit objects from occupying
the main cache [C3]. These are alternative policies to compare, not three
features to combine.

For MoobFS, replay domain-level accesses with domain sizes, rebuild time and
permission/ownership context. Compare equal total byte budgets including policy
metadata. Add skewed hot sets plus scans, uniform cycles, changing hot sets and
unequal domains. Report total rebuild time, bytes read/freed, memory, p95/p99 and
per-user outcomes, not only hits. Initial replay cannot establish kernel locking
cost or real reclaim behavior.

Prefer 2Q as the first comparator because the project already identifies it;
use ARC and S3-FIFO to check whether extra adaptation or queue design helps.
The kernel's shrinker and list-LRU facilities [K2/K3] are mechanisms, not proof
that any of these policies is implemented or appropriate. The current simple
walk may remain best when domains are few. Size accounting is a prerequisite
for trustworthy comparisons; eviction popularity does not provide fair shares.

### 2.3 Lexical retrieval and fusion: reuse user-space engines

BM25 ranks lexical matches using term frequency and document-length/statistical
information. SQLite FTS5 documents an implementation and column weights [R1].
It is a possible reference engine, not a selected dependency. The existing
MoobFS SQLite virtual table does not thereby become a full-text index: text
indexing remains a separate derived structure with G4's maintenance obligations.

RRF combines positions in ranked lists using reciprocal ranks [R2]. It is a
useful first comparison against lexical-only and vector-only retrieval. Use
stable client identities to merge results; define candidate depth and ties.
Raw lexical and vector scores need not share a numerical scale. Improvement in
the source paper is not a promise for agent memories.

MMR balances relevance with novelty relative to already selected items [R3].
Use it as optional context selection in the client, not as filesystem deduplication
or an unconditional memory deletion rule. Compare equal token budgets and check
whether distinct decisions, dates or supporting evidence are lost. Diverse text
is not necessarily more useful or more correct.

All ranking branches must respect the intended visibility boundary. Global
term statistics can make authorized results depend on hidden documents; filtering
the final rows alone does not establish score/timing confidentiality. Derived
text and vectors need their own access controls. Existing MoobFS threat-model
limits are not removed by any ranking method.

### 2.4 Exact search before HNSW

Score every eligible vector for a small domain as the reference. A full-sort or
bounded top-k selection can produce exact results with deterministic ties; measure
the selection overhead as part of the complete path. The relevant scale is the
number of eligible vectors after filtering, not only the total file count.

HNSW builds a hierarchy of proximity graphs for approximate nearest neighbors
[R4]. Its fit is conditional on exact search missing the declared latency/resource
target. Reuse an existing user-space engine, measure recall against the exact
oracle, and include insertion, model changes, deletes, selective filters and
memory in the comparison. Filter-aware graph traversal is engine-specific [R5];
the original algorithm is not a filesystem authorization mechanism. Neither
global top-k followed by dropping forbidden hits nor truncating QUERY2's first
page is an adequate exact baseline.

### 2.5 Roaring: promising representation, weaker immediate fit

Roaring combines arrays, bitmaps and run containers to represent integer sets
and execute set operations [D1]. A plausible MoobFS experiment is categorical
attributes with sufficiently populated posting sets. Sparse values and frequent
mutation may favor the existing trees.

The paper's 32-bit representation is not a drop-in replacement for the module's
64-bit inode keys. Account for a wider representation or local ID mapping,
reuse/generation handling and all maintenance memory. Range/prefix searches,
attribute projection and ordered cursors remain separate requirements. First
measure densities and intersection costs; do not replace every attribute tree.
User-space SIMD results do not justify using SIMD inside filesystem paths.

## 3. Techniques to defer

- **Bloom/signature filters:** previous metadata-search research already examined
  these. QUERY2 selects one domain and performs exact in-memory lookup, so there
  is no demonstrated expensive cross-partition read to avoid. Any future filter
  is only a negative pruning aid, never an authorization decision; invalidation
  must not introduce false negatives. Reconsider for a measured disk/network
  lookup bottleneck, not as a generic speed feature.
- **Persistent B-trees/LSM trees, Merkle reconciliation and content-defined
  chunking:** outside this focused source review. Persistence, scan/recovery or
  transfer/duplication costs must first justify separate studies. No claim about
  a particular implementation's speed or suitability is made here.
- **Learned indexes and a new general planner:** insufficient workload evidence
  to justify model maintenance or a larger kernel execution surface.
- **Cache popularity as multi-user fairness:** a category error. Per-user
  entitlement, accounting and denial-of-service limits remain S9/G2 decisions.

## 4. Proposed experiment order and stop rules

1. After P0 and a separate authorization, quantify selection work and collect
   representative domain traces. Evaluate the cardinality and eviction hypotheses
   separately. No replacement is justified solely by a paper's benchmark.
2. Under G4/G5, establish lexical-only and exact-vector baselines with the same
   permissions and data versions; compare their RRF combination. Add MMR only
   after relevance and redundancy are measured.
3. Try HNSW only if exact scoring misses a predeclared target; try Roaring only if
   posting-set profiles show a plausible benefit. Keep the existing algorithm
   whenever the evidence is inconclusive.

Use G6's pre-registered workload/acceptance targets, pinned source, identical
datasets, rotated comparison order, repetitions and confidence intervals.
Include empty matches, highly skewed values, dense and sparse sets, full scans,
small limits, paging, concurrent updates and two OS identities. Measure cold
build, warm query, writes, actual memory and tail latency. For retrieval separate
ANN recall (agreement with exact vectors) from relevance to labeled questions
(for example nDCG@k); one does not establish the other.

No semantic-quality result justifies unauthorized output. No faster read
justifies unbounded writes/memory or broken cursors. Any adopted kernel change
still needs its ADR/UAPI assessment, VM regressions, negative tests and immediate
KASAN/lockdep when locking or object lifetime changes. Algorithm experiments
remain unstarted.

## 5. Primary sources and reading limits

Consulted on 2026-10-20. Original papers and official sources were used for the
algorithm descriptions; applicability and priority are this assessment's
inferences. Selected relevant sections were inspected, not every proof,
implementation or experiment in every paper. No candidate algorithm implementation
was executed.

- **K1:** Linux **6.12.112**, [rbtree_augmented.h](https://raw.githubusercontent.com/gregkh/linux/v6.12.112/include/linux/rbtree_augmented.h), augmentation contract/callbacks. Version matches the repository's KASAN build pin.
- **K2:** Same version, [shrinker.h](https://raw.githubusercontent.com/gregkh/linux/v6.12.112/include/linux/shrinker.h), count/scan contract and reclaim state.
- **K3:** Same version, [list_lru.h](https://raw.githubusercontent.com/gregkh/linux/v6.12.112/include/linux/list_lru.h), LRU list interfaces and per-node/memcg organization. Source inspection only, no API change proposed.
- **C1:** Johnson and Shasha, [2Q](https://www.vldb.org/conf/1994/P439.PDF), VLDB 1994. Abstract/design discussion; PDF text has OCR artifacts. Already cited by journal 0103.
- **C2:** Megiddo and Modha, [ARC](https://www.usenix.org/conference/fast-03/arc-self-tuning-low-overhead-replacement-cache), FAST 2003. Official abstract, not a fresh full-paper review. Already cited by journal 0103.
- **C3:** Yang et al., [FIFO Queues are All You Need for Cache Eviction](https://www.cs.cmu.edu/~rvinayak/papers/s3-fifo-sosp-2023-fifo-queues-are-all-you-need-for-cache-eviction.pdf), SOSP 2023, DOI 10.1145/3600006.3613147. Abstract and design/overhead sections.
- **R1:** [SQLite FTS5, BM25](https://www.sqlite.org/fts5.html#the_bm25_function), official documentation, also retrieved via Context7 `/websites/sqlite_docs`. No SQLite version/backend selected. The Robertson/Zaragoza publisher page returned 403 on opening; no full-paper reading is claimed.
- **R2:** Cormack, Clarke and Buettcher, [Reciprocal Rank Fusion](https://cormack.uwaterloo.ca/cormacksigir09-rrf.pdf), SIGIR 2009. Ranking rule and reported experiment scope.
- **R3:** Carbonell and Goldstein, [The Use of MMR](https://www.cs.cmu.edu/~jgc/publication/The_Use_MMR_Diversity_Based_LTMIR_1998.pdf), SIGIR 1998. Abstract and retrieval/summarization discussion.
- **R4:** Malkov and Yashunin, [HNSW](https://arxiv.org/abs/1603.09320), arXiv 1603.09320, revised 2018. Abstract/introduction in the paper PDF; no claim of evaluating an implementation.
- **R5:** [Qdrant filtering](https://qdrant.tech/documentation/search-patterns/vector-search-filtering/), official documentation consulted for assessment 0164; engine-specific filtering trade-offs.
- **D1:** Lemire, Ssi-Yan-Kai and Kaser, [Consistently faster and smaller compressed bitmaps with Roaring](https://arxiv.org/html/1603.06549v4), arXiv 1603.06549v4. Abstract/representation discussion; published gains are not extrapolated to kernel trees.

Thanks to the authors and kernel maintainers for the published algorithms,
measurements and reusable infrastructure. Source availability does not by itself
authorize copying code: assess the chosen implementation's license if adoption
is later proposed.

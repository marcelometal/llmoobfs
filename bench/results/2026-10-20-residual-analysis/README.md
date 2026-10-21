# The remaining QUERY2 difference: reanalysis, not a new benchmark

Date: 2026-10-20. Source revision: `7e446ff` (the fixed module in confirm40), compared with
archived module `m0102`. Reviewed source: `dfe114c`; the query code discussed here is also in
`7e446ff`. [Journal 0147](../../../docs/history/0147-query-residual-investigation.md).

**The p50 residual survives removal of the first query cycle and paired resampling. Its cause
is still unmeasured. A separate, large p99 difference is mostly a first-cycle effect.** No
module was loaded, no benchmark rerun, and no host settings changed for this investigation.

## Data and reproduction

The original `m40.tgz` contains confirm40's raw call records and logs. It remains outside the
repository, under the local handoff's `medicoes/` directory. Its SHA-256 is in
[`reanalysis/input-sha256.txt`](reanalysis/input-sha256.txt). The archive is read in memory,
without extraction. No original evidence is overwritten.

From the repository root, with an accessible copy of that archive:

```sh
python3 bench/results/2026-10-20-residual-analysis/analyze.py \
  /path/to/m40.tgz /path/to/new-output-directory
```

The output directory must not already exist. `analyze.py` uses the repository's existing
percentile, bootstrap and Mann–Whitney implementations, with 10,000 resamples and seed
20261019. It validates all 20,000 calls of m0102/fix40: 40 repetitions per module, 50 calls
per shape, no errors, 253/253/3030 results for the filtered queries and 20/20 for recent.
The original median values reproduce to the published CSV's four decimals.

[`per-repetition.csv`](reanalysis/per-repetition.csv) preserves each repetition's p50, p99,
first call and order position for three selections: all calls; remove each shape's first
call (one full cycle); remove each shape's first five calls. The latter two are sensitivity
analyses, not a replacement campaign or selective removal of slow outliers.
[`comparison.csv`](reanalysis/comparison.csv) includes independent bootstrap intervals and a
paired bootstrap: resample repetition indices jointly for both modules, then recompute the
relative difference of their medians. Pairing accounts for a shared repetition but does not
remove fixed carryover/order effects. A separate RNG seeded identically per row makes these
new intervals deterministic; their endpoints may differ slightly from compare.py's shared
RNG sequence. Full p-values are retained rather than rounded to zero.

## The median residual persists

The figures below compare fix40 with m0102. Intervals are the paired 95% percentile bootstrap
intervals after removing the first cycle. Unit of inference: one repetition, not one call.

| Shape | All-call p50 difference | First cycle removed | Paired 95% interval | Absolute delta after removal |
| --- | ---: | ---: | ---: | ---: |
| Two equalities, 253 results | +1.21% | +1.16% | +0.58..+2.53% | +2.07 us |
| One equality, 253 results | +2.64% | +2.62% | +1.16..+3.56% | +2.98 us |
| One equality, 3030 results | +3.03% | +3.03% | +2.69..+3.65% | +40.92 us |
| Newest 20 of a kind | -0.40% | -0.43% | -0.91..+0.16% | -0.12 us |
| Newest 20 of an agent | -0.50% | -0.50% | -1.25..+0.23% | -0.73 us |

Removing five cycles leaves the filtered differences at +1.19%, +2.60%, +3.06%.
The three filtered all-call p50 Mann–Whitney p-values are approximately 4.14e-6, 4.23e-5,
6.81e-13. Even the conservative threshold 0.05/15 for the original five shapes times three
percentiles would retain them; this observation does not repair dependence or selection bias.
The 40 repetitions fit in approximately **109.4 seconds** from the first m0102 measured call
to the last measured call. They do not establish repeatability across days, reboots or hosts.

## Why the tail was different

`bench/load.c:w3_queries()` opens a new directory descriptor, allocates a fresh user result
array, and executes Q1/Q2/Q3 in that fixed order, 50 times. The discarded warm-up in
`driver-bisect.sh` is **another process**, with another open descriptor. Therefore it warms the
index and backing files, but does not warm the measured process's user array or the fixed
module's per-file result buffer. `moobl_qbuf_get()` must allocate that buffer on the measured
process's first Q1. Both modules also have process-local first-touch costs; Q3 first writes
more of the user result array than Q1/Q2. Allocation and page faults were not traced here.

For filtered Q1, the first call is the maximum in **40/40 repetitions for both modules**.
Its median is 272.82 us (m0102) versus 512.01 us (fix40). With only 50 observations per shape,
interpolated p99 heavily weights the maximum: 51% maximum plus 49% second-highest.

| Shape | Original median of repetition p99, m0102 / fix40 | Difference | After first cycle removed |
| --- | --- | ---: | ---: |
| Filtered Q1 | 232.30 / 357.82 us | +54.03% | +3.33% |
| Filtered Q3 | 1669.19 / 1764.38 us | +5.70% | +3.00% |
| Recent Q1 | 41.94 / 45.17 us | +7.70% | -1.93%, independent CI contains zero |
| Recent Q2 | 227.58 / 258.15 us | +13.43% | -0.96%, independent CI contains zero |

The data establishes a first-cycle association. It does not separate kernel allocation,
user page faults, cache coldness and scheduler effects. It also does **not** explain away the
p50 residual. Future reports should state whether a measurement includes the first query on
an open file, and should report startup and same-descriptor steady state separately.

## Source-level candidates, ranked by fit to the observations

1. **Per-result changes remain after allocation reuse.** Before F7, one 272-byte kernel result
   was repeatedly filled and copied to the caller. After F7 and #101, every result is filled
   in a contiguous kernel buffer, followed by one copy after releasing `query_sem`.
   The kernel destination footprint is now 68,816 bytes for 253 results and 824,160 bytes for
   3030, from a retained capacity of 1,114,112 bytes at limit 4096. Both versions still copy
   the same number of result bytes to user space; the change is buffer footprint and access
   order, **not a newly introduced second copy_to_user pass**. The larger footprint and
   changed locality are plausible candidates, not an attributed timing cost.
2. **The F10 fix adds per-candidate work even when no per-result permission check is required.**
   `query_run()` first drops held dentries and stores `cand[i].ok`, then walks candidates again
   to produce results. `bool ok` at the end of `struct moobl_candidate` changes its x86-64 size
   from 40 to 48 bytes (checked with Clang's x86-64 target and equivalent field declarations).
   With 256 entries, the requested zeroed allocation grows from 10,240 to 12,288 bytes.
   Allocator bucket sizes on the measured kernel were not checked. Allocation happens once per
   query; the extra loop also repeats per chunk. Neither effect was isolated by the old bisect,
   whose pre-#101 module already had a much larger allocation regression masking small costs.
3. **Per-call fixed costs exist but are an incomplete explanation.** The retained-buffer lookup
   takes uncontended spinlocks, and `lsm_labels()`/the added domain-freshness check are on the
   path. Newest-20 calls do not show a positive median difference, while 3030 results cost
   roughly 41 us more. A purely constant lookup cost does not fit the full pattern by itself.
   That does not prove it is zero; other changes may offset it.
4. **Reclaim or a different number of indexed files is not supported by the logs.** Every m0102
   and fix40 run reports the same 7459 inodes, 54472 entries and 4533207 accounted bytes,
   `domain_builds=1`, `domain_releases=0`, `heterogeneous=0`, `fallback_domains=0`. This excludes
   observed domain releases/rebuilds as the explanation for this campaign; it is not a full
   profile of allocator/cache activity or active LSMs.

## Confounders and sensitivity

- Rotation uses only three cyclic orders, not all six permutations. For each module the
  predecessor is fixed whenever it has one, so rotation does not eliminate carryover.
  The old mount-built m0084 scans 60,000 files between the other modules, which build only
  the queried domain. Removing m0084 from a dedicated residual campaign avoids this asymmetry.
- By rotation group (13/14 repetitions), the filtered Q1 delta ranges from +0.04 to +2.22%,
  Q2 from +1.43 to +3.72%, Q3 from +2.75 to +3.29%. These small descriptive subgroups are
  not independent confirmatory tests. Q1 is especially sensitive to grouping; Q3 is steadier.
- First/last 20 repetitions give Q3 +3.07/+3.00%; Q1 +1.89/+1.60%; Q2 +2.33/+1.89%.
  The median of all repetitions is not the average of the subgroup relative differences.
- The recorded policy is `performance` plus turbo disabled. Snapshot CPU MHz spans 797–2000;
  no affinity, per-call migration, thermal counters or effective frequency trace is stored.
  Idle CPUs can have low snapshot values, so these alone do not prove the measured CPU ran
  slowly. Nor do policy settings prove an invariant actual frequency.
- The one-minute load average spans 1.01–1.27. The run began at 02:51:30, near the reported
  fifth fuzzer completion time (02:52, rounded). Without precise completion and process logs,
  overlap cannot be established. The elevated five-/fifteen-minute averages retain prior load.
- No A/A control or repeated independent session exists here. Published module hashes and
  srcversions are preserved; the exact historical source/build mapping of the m0102 binary
  must be recovered before source-level timing is attributed to a particular commit.

## Next experiment, prepared but not run

Use a dedicated disposable KVM guest with the same release kernel/config/compiler for every
variant. Never load the modules on the physical host. Preserve the original data and logs.
First recover the source identity of m0102; build artifacts with full hashes, not only labels.
Run safety tests separately from timing (KASAN changes execution costs).

- Establish an A/A comparison of the same current binary under two labels, then m0102/current
  with at least 36 matched repetitions, balanced AB/BA order, no m0084 in between, across at
  least two sessions. Pin the measured guest worker and document vCPU placement on the host;
  record host frequency/thermal/load evidence without silently changing host controls.
- Keep one descriptor open for warm-up and measured calls. Keep an explicit first-open series
  as a different workload. Rotate Q1/Q2/Q3 order; prefault the user result array for the steady
  series and vary returned count/limit separately (20, 253, 3030 results).
- Isolate source changes with independently built investigative variants: F7 plus cached
  buffer on the earlier source; current source; current source with `ok` placed in existing
  struct padding. Then isolate the two candidate loops while preserving F10 locking and
  pointer lifetime. No proposed variant here is a correctness fix or merge candidate.
- Count time/cycles and page faults separately for allocation, query_run and results_to_user;
  profiling itself needs an A/A overhead check. Only a repeatable controlled difference
  supports attribution. Any resulting code change must pass VM tests, negative regression
  coverage where applicable, and immediate KASAN/lockdep if locks/lifetimes change.

References: existing statistical sources in [`docs/references.md`](../../../docs/references.md)
(Efron 1979, Mann and Whitney 1947); original source/log paths are in
[`2026-10-19-macmini-steady`](../2026-10-19-macmini-steady/README.md). No new external library
or unpublished measurement was used.

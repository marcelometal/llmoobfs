# 0201 — G5 hybrid-retrieval package; mounted gate passed

Date: 2026-10-25 to 2026-10-26. Status: every bound of protocol section 13.6
met on the dedicated Debian host. The G6 final evaluation not started.
Work started: 2026-10-25 about 10:22 America/Sao_Paulo, when the maintainer
accepted section 13. Ended: 2026-10-26 12:44. Wall-clock time, not an
engineering-effort measurement. The package was estimated L. Most of the
calendar time was three full gate runs of about 5.5 h each, two of them lost
(a power outage and a harness error); the code took one working session.

## Goal and decision

Implement the G5 package against [protocol section
13](../g6-evaluation-protocol.md#13-g5-amendment-2026-10-25--proposed-not-accepted),
which the maintainer accepted as written before any download or run (journal
[0200](0200-g5-protocol-amendment.md)), and run its mounted gate. No change to
`layer/`, the UAPI, the disk format or the adapter
`bench/client/langgraph_store.py`.

## Changes

- `bench/client/g5_retrieval.py`: one watcher per uid and path that keeps an
  FTS5 table and the vectors of every memory the uid can read, built on the
  G4 watcher; the searcher (eligible set from `QUERY2` on the MoobFS path, or
  from the metadata table on the plain-files path, then exact vector,
  FTS5 `bm25()` and RRF k = 60); the oracle, which reads no maintained index.
- `bench/client/g5_mounted.py`: the mounted gate. It draws and records the
  sample, plants one adapter file per turn, copies the lower tree to a plain
  directory, starts the watchers and oracles, runs the searches, checks every
  bound of section 13.6 and runs the revocation case.
- `bench/client/g5_report.py`: the recorded, unbounded numbers of section
  13.7 the gate did not print (Q2 relevance, per-type breakdown), from the
  archived raw results. It runs no search.
- `bench/client/g4_derived.py`: the inotify set-up moved into
  `_open_inotify()` so the G5 watcher can reuse it. G4's gate was rerun on
  the host after this change and printed `G4_MOUNTED_PASS` (`g4-rerun.*` in
  the results directory).
- `bench/results/2026-10-25-g5-hybrid-retrieval/`: the gate run, the smoke
  run, both failed attempts, the G4 rerun and the environment.
- Protocol section 13: the acceptance and harness notes. Roadmap, changelog,
  history index and journal 0200's next step.

Implementation choices the protocol left open, made before the gate run:

- The G1 adapter's `put` derives attributes from the value and cannot write
  `T_TIME`. The `date` attribute is therefore a string; no filter of section
  13.4 uses it. `question_id` is part of the value so that it becomes an
  attribute.
- Q3 runs only for the caller's own private domain, as section 13.4 defines
  it; a `team` question has Q1 and Q2 only.
- The three latency runs are three fresh searcher processes over the same
  built indexes, each with a warm pass then a timed pass. The corpus is not
  planted or indexed again per run.
- The usable-results bound is applied to fusion and vector. FTS5 returns
  only rows that match a term, so a lexical list shorter than
  min(10, eligible) is counted (`lexical_below_10`) and not a miss.

## What ran

Dedicated Debian host, kernel `6.12.111+deb13-amd64`, Intel Core i7-2635QM,
16 GB, ext4 lower on a dedicated test disk, `domain_depth=1`,
`index_build_work=65536`. Module built with `make layer` from the layer at
`c793f8d`, the same build as G4. Packages, model and dataset revisions are in
the results directory's `environment.txt`. Command, as root:

```text
python bench/client/g5_mounted.py --ko layer/moobfs_layer.ko --out-dir <dir>
```

Sample: 110 instances (17 knowledge-update, 28 multi-session, 13
single-session-assistant, 7 single-session-preference, 15 single-session-user,
30 temporal-reasoning). 54 017 files: `alice` 24 063, `bob` 25 142, `team`
4 812, planted in 588 s. Before indexing, one `QUERY2` per domain answered
(484, 506 and 504 results in 0.38, 0.33 and 0.05 s).

The gate printed `G5_MOUNTED_PASS` and exited 0 at 12:37 on 2026-10-26.

| Bound | Result | Limit |
| --- | --- | --- |
| MoobFS top 10 vs plain-files top 10, every question, filter, branch, run | 0 differences | 0 |
| Each path vs the oracle | 0 and 0 | 0 |
| Results outside the caller's eligible set | 0 | 0 |
| Short fusion / vector lists; unindexed eligible files | 0 / 0; 0 | 0 |
| Fusion `recall_all@10`, Q1, vs the better branch (lexical, 0.809) | 0.845; paired difference +0.036 [−0.018, 0.091] | ≥ 0.789 |
| Q1 p95, fusion, warm, runs 1 / 2 / 3 (s) | 0.129 / 0.135 / 0.131 | ≤ 0.300 |
| Q3 p95, runs 1 / 2 / 3 (s) | 1.413 / 1.481 / 1.525 | ≤ 2 |
| Revocation: revoked turns returned in flight / after reconciliation | 0 / 0 | 0 / 0 |

`lexical_below_10` was 0. Revocation: uid 1002 set the one evidence turn of
`team` instance `c960da58` to `0600` on both trees. Before, uid 1001's
results held it 6 times across branches and filters; the in-flight search
returned it 0 times and dropped 0 items on `open`, so it did not score the
turn; whether the watcher had already removed it was not recorded. After
reconciliation, 0 on both paths.

Relevance on Q1, 110 questions, mean with 95% paired bootstrap interval:

| Branch | `recall_any@10` | `recall_all@10` | nDCG@10 |
| --- | --- | --- | --- |
| Vector | 0.964 [0.927, 0.991] | 0.791 [0.709, 0.864] | 0.479 [0.419, 0.538] |
| Lexical | 0.973 [0.936, 1.000] | 0.809 [0.736, 0.882] | 0.621 [0.555, 0.688] |
| Fusion | 0.982 [0.955, 1.000] | 0.845 [0.773, 0.909] | 0.602 [0.543, 0.660] |

Recorded, not bounded (section 13.7):

- Q2 (`role` = `user`), `recall_all@10`: vector 0.845, lexical 0.845,
  fusion 0.882; nDCG@10 0.525, 0.610 and 0.618. Q1 `recall_all@10` by type
  (vector / lexical / fusion): multi-session 0.607 / 0.571 / 0.643,
  temporal-reasoning 0.700 / 0.733 / 0.800, knowledge-update 0.941 / 1.000 /
  1.000, single-session-user 0.933 / 1.000 / 1.000, single-session-preference
  0.857 for all three, single-session-assistant 1.000 for all three. All in
  `gate/report.json`; `g5_report.py` reproduced the gate's Q1 numbers exactly.
  The per-type groups have 7 to 30 questions and wide intervals.
- Plain-files path latency, one run: Q1 p50 / p95 0.058 / 0.095 s, Q3
  0.967 / 1.087 s. Not a comparison with the MoobFS path (section 7's method
  was not used).
- Index build, four watchers and two oracles at once, `OMP_NUM_THREADS=2`
  each, on 8 threads: about 17 900 s per uid (27 886 and 28 817 distinct
  contents embedded; 9 745 and 9 838 of them truncated at 256 word pieces,
  about 35%). The oracles' extra 170 embeds per uid are the questions.
- Watcher `VmHWM` about 627 MB; index database 118 to 123 MB per uid and
  path; searcher peak RSS 820 to 835 MB and 151 to 159 s of CPU per run;
  oracle peak RSS 959 to 975 MB.
- Integration burden: the plain-files path needs the metadata table and its
  synchronization in the watcher (`--meta`); the MoobFS path needs neither.

## Failed attempts

All kept in the results directory, none is the gate run.

1. **Power outage** (`attempt1-power*`), 2026-10-25 about 13:00, during index
   build. The host lost power; nothing was measured.
2. **`E2BIG` from `QUERY2`** (`attempt2-e2big*`), 2026-10-25 18:30, at the
   first search after about 5 h of index build. Under the default
   `index_build_work` (32 768 units) `QUERY2` refused the private domains of
   24 063 and 25 142 files, as it refused the 17 000-file domain of issue 118.
   Both domains answered once mounted with `index_build_work=65536`, the
   administrative maximum of
   [ADR-0020](../decisions/0020-configurable-query-build-work-per-mount.md).
   The gate now mounts with that value and runs one `QUERY2` per domain
   before indexing, so the same mistake stops in seconds, not hours. No bound,
   corpus or engine changed; the protocol's harness note records it.

A smoke run on 6 instances (`smoke*`) passed before attempt 1. It checks the
harness only and is not a gate run.

## Limitations

- One host, one lower filesystem (ext4), one dataset in English, one sample,
  one model. The bounds are this workload's, not a guarantee.
- The lexical branch beat the vector branch on this sample, and fusion's gain
  over lexical (+0.036) has an interval that includes 0. The floor of section
  13.6 is met; a claim that fusion is better than lexical is not made.
- A whole private domain of 25 000 turns needs `index_build_work=65536`; the
  default refuses it. That is the documented trade-off of ADR-0020, not a new
  finding; a larger domain needs a split or a decision on the limit, which is
  core-front work (issue 118).
- Q3's p95 of about 1.5 s includes `QUERY2` paging, exact scoring of about
  25 000 vectors and the FTS5 query; the shares were not separated.
- Embedding the corpus took about 5 h on this 2011 CPU with everything else
  running. That cost is the client's and is the same on both paths.
- The in-flight window after a revocation exposes that an item was scored
  (counts, timing), as in G4. Content was not delivered (ROADMAP rule 4).

## Public surfaces

Roadmap and changelog updated. Both READMEs, the site, the FAQ, `SECURITY.md`,
`docs/help-wanted.md` and `docs/compatibility.md` do not state a G5 status and
were left as they were. The module's tested behavior did not change.

## Next step

G6, the final end-to-end evaluation, which combines G1 to G5 in one real
client and still needs G2's latency ceiling. Any approximate vector mode
waits for its own amendment.

## Checks of this commit

Completed 2026-10-26 at 12:44 America/Sao_Paulo. `git diff --check`,
`sh scripts/check-spdx.sh`, `sh scripts/check-retired-claims.sh`
(`RETIRED_CLAIMS_OK`), `python3 scripts/check-site.py .` (12 pages) and
`python3 scripts/site-faq.py --check` passed. A local check resolved 339
relative Markdown destinations, anchors included, in the eight changed
documents. `py_compile` passed for the three G5 scripts; no Python linter is
part of `make lint`. No kernel code changed, so no VM or KASAN run belongs to
this commit; the branch's CI runs them.

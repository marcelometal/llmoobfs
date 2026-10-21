# 0143 — syzkaller: a fifth six-hour run reaches the S3 coverage threshold; the 40-repetition confirmation

Date: 2026-10-19 to 2026-10-20. Status: done.

## syzkaller, fifth run

- Code: branch `syz-coverage` at 7d00e6d, whose `layer/`, `common/` and `include/` are identical
  to `main` at that time (`git diff --stat 7d00e6d origin/main -- layer common include` empty),
  so the layer fuzzed is the one of v0.4.1-experimental minus the query-buffer fix of journal
  0139, which touches only buffer allocation. The descriptions are those of journal 0138's last
  section: the `QUERY2` cursor as resources with a continuing call, typed terms and values, and
  the new seeds.
- Command: `vm/syzkaller-local.sh 21600 module image descriptions run` on the benchmark host,
  2026-10-19 20:52 to 2026-10-20 02:52.
- Result: exit status 0, no crash directory (no report from KASAN, UBSAN or lockdep); 540 919
  executions (about 25 per second), corpus 1 082. The per-function coverage was saved at 02:34,
  twenty minutes before the end, as the fix of journal 0138 intended.

| File | Functions reached | PCs |
| --- | --- | --- |
| `layer/attr.c` | 5 of 7 | 53 of 60 (88 %) |
| `layer/index.c` | 59 of 83 | 1 161 of 1 602 (72 %) |
| `layer/main.c` | 60 of 72 | 393 of 654 (60 %) |

The ioctl and attribute handlers of the S3 threshold (the set listed in journal 0138): **659 of
743 PCs, 88.7 %**, above the threshold of 85 % (78.6 % in the fourth run). Gains where the new
descriptions aimed: `query2_exec` 64 of 75 (41 before), `value_cmp` 24 of 24 (12),
`leap_collect` 36 of 43 (27), `term_check` 23 of 24 (18), `query_run` 186 of 211 (164).

Still not reached: `moobl_xattr_get()`, 0 of 4 PCs, although the attribute calls now take a file
of the layer and `getxattr$moob` by path is enabled. Not explained yet; the suite and the
interoperability test (journal 0140) exercise it, so this is a gap of the fuzzer's programs, to
look at in the descriptions next. `moobl_xattr_set()` is at 38 of 50.

## Confirmation of the fix with 40 repetitions

The measurement of journal 0139 compared the fixed module with 20 repetitions; this one uses 40,
with the same driver, the CPU frequency held, 0084 / 0102 / the fixed module (the tree of pull
request 101 at 7e446ff) in rotated order. Files: `bench/results/2026-10-19-macmini-steady/confirm40/`.

| Shape (p50, µs) | 0084 | 0102 | fixed | fixed vs 0102 |
| --- | --- | --- | --- | --- |
| two equalities | 165.9 | 178.6 | 180.7 | +1.2 % (+0.6..+2.5) |
| one equality, 253 | 102.7 | 113.5 | 116.5 | +2.6 % (+1.0..+3.6) |
| one equality, 3 030 | 1 200.4 | 1 350.8 | 1 391.7 | +3.0 % (+2.7..+3.6) |
| newest 20 of a kind | 27.5 | 28.6 | 28.5 | −0.4 %, no difference detected |
| newest 20 of an agent | 142.0 | 146.7 | 146.0 | −0.5 %, no difference detected |

The 2x slowdown is gone. A residual of 1 to 3 % against 0102 remains on the filtered shapes,
small but outside the intervals; the cost of the per-file buffer lookup and of the fixes merged
between 0102 and pull request 71 (journals 0105 to 0108) are the candidates, not measured
separately. The on-demand index against the index built at mount is confirmed at 7.6 to 12.5 %.

## Status of the stabilization gate

With this run every condition the maintainer set for leaving stabilization holds: the fix batches
tested and merged, KCSAN, negative rounds, a long syzkaller run with no report, and now the S3
coverage threshold. Open: no outside human review; the fair-shares part of F2 (S9).

## Dated qualification — 2026-10-20 (journals 0144–0146)

The measured 88.7% meets S3's numeric aggregate threshold. The written criterion
also requires every reachable function to be exercised, with explained exclusions.
The saved result is still 0/4 PCs for `moobl_xattr_get`; the inspected object has
instrumentation, and no runtime investigation has resolved the discrepancy.
The aggregate alone therefore does not establish the complete S3 criterion.
This qualifies the evidence above without rewriting the maintainer's recorded
stabilization decision. The residual of 1–3% refers to p50; the stored Q1 p99
difference is +54.03%, further examined in journal 0147.

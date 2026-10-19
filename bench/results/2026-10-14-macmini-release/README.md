# ADR-0012, second half: releases measured, steady state against the module built at mount (journal 0103)

Benchmark host (Debian 13, kernel 6.12.111, SATA SSD), the data of
[`2026-10-13-macmini-ondemand`](../2026-10-13-macmini-ondemand/README.md): 60 000 memories of
profile 8 in eight domains (`ns0` to `ns7`, about 7 500 files and 4.5 MB of index each) on
ext4. Modules, by the journal whose code they carry: 0102 (this branch's tip, commit `7dc42e5`),
0098 (index on demand, no releases) and 0084 (index built at mount). One run of each script;
nothing else was meant to be running on the host, which was not verified.

## Releases (`release/`, `bench/release.sh`, module 0102)

A domain released by the shrinker (`index_idle=0`, then `drop_caches`) and queried again, five
repetitions:

| | first build | query on the built domain | query after the release |
| --- | --- | --- | --- |
| page cache kept (`drop_caches=2`) | 2.19 to 2.67 s | 7 to 10 ms | 136 to 147 ms |
| page cache dropped (`drop_caches=3`) | 2.02 to 2.95 s | 6 to 11 ms | 2.07 to 2.56 s |

A release costs a rebuild, and the rebuild costs what a first build costs with the same cache:
about 0.14 s from the page cache, about 2.3 s from the disk, for 7 500 files.

The eight domains queried in turn, two rounds, starting with a cold cache:

| | round 1, each query | round 2, each query | builds, releases | index at the end |
| --- | --- | --- | --- | --- |
| `index_budget` for 2.5 domains (11 333 017 bytes) | 1.95 to 2.63 s | 152 to 173 ms | 16, 14 | 9.1 MB |
| the mount's own budget | 1.93 to 2.83 s | 4 to 10 ms | 8, 0 | 36.4 MB |

Under a budget smaller than the working set, a cyclic access releases each domain just before it
is needed again: every query of round 2 rebuilds. This is the textbook failure of
least-recently-used replacement on a loop larger than the cache; see journal 0103.

## Steady state (`steady-*/`, `bench/ondemand.sh` with `REPS=10`)

`QUERY2` on the built domain `ns3`, ten repetitions of 50 calls; median p50 across the
repetitions and, in brackets, its p5 to p95 range (µs):

| Shape (results) | 0084, at mount | 0098, on demand | 0102, with releases |
| --- | --- | --- | --- |
| two equalities (253) | 118 [114–136] | 128 [121–139] | 128 [120–138] |
| one equality (253) | 75 [73–88] | 86 [82–91] | 83 [79–90] |
| one equality (3 030) | 873 [845–992] | 1 007 [950–1 035] | 992 [933–1 059] |
| newest 20 of one kind | 27 [26–29] | 28 [23–29] | 28 [19–29] |
| newest 20 of one agent | 140 [134–148] | 145 [117–147] | 143 [99–150] |

- 0102 against 0098: within 4 % on every shape. The releases and the tracepoints (disabled) cost
  nothing visible in steady state.
- 0098 and 0102 against 0084: the three filtered shapes have medians 8 to 15 % higher; the two
  "newest 20" shapes are equal (the 13 October run, with three repetitions, had shown them
  faster: not repeated). The ranges overlap on every shape, and the report tool marks every
  row inconclusive (spread above 20 % in some percentile). The two modules differ by every
  commit from journal 0085 to 0098, not only by the on-demand build.

*Note, 2026-10-19 (journal 0134).* Ten repetitions and a spread rule cannot show or exclude a
difference of 8 to 15 %. Under the revised method (`docs/benchmark-plan.md`), this comparison
needs at least 30 repetitions, `bench/compare.py` (confidence interval and test) and the CPU
frequency held still; until then no difference, in either direction, is claimed.

Files: `release/times.txt`, `steady-*/times.txt` (each script's own log), `*/environment.txt`,
`steady-*/results.csv` (from `bench/report.py`). The raw per-call records stay on the host.

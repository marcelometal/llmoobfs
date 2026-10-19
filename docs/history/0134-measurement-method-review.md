# 0134 — Measurement method reviewed: a difference needs a test and an interval

Date: 2026-10-19. Status: method revised and tools written; no campaign rerun yet (the
benchmark host was not reachable in this session).

## Goal

Act on an outside review of how MoobFS measures performance. A researcher in file-system trace
capture and replay answered a request for comments on the report of 2026-10-14
(`bench/results/2026-10-14-macmini-release/`), which showed medians 8 to 15 % apart with
overlapping ranges and asked whether the difference exists. The answer came by private e-mail;
there is no public link.

## What the review said (technical content)

- A percentage difference of medians is not a result. A difference needs a statistical test
  and must be reported with its confidence interval.
- Repetitions: start from about 30. Decide whether to warm up and discard the warm-up, and
  whether to drop caches between repetitions.
- Latency has a tail: plot an ECDF instead of looking only at mean or median.
- Hold the CPU frequency still (turn dynamic frequency scaling off); results also depend on the
  OS version and the machine, so in time run on more than one machine.
- Unverified remarks from an automated reading of the repository: SQLite runs with no POSIX
  permission check while the layer runs with one as an ordinary user, which favours SQLite and
  must be stated with the number; SQLite uses one connection and its default page cache while
  the layer's index is in memory (36 MB in that report), so `cache_size`/`mmap_size` must be
  declared and varied.
- The strongest test is end to end with a real workload and its business metrics; benchmarks
  and traces are for regressions between versions.

## What was checked against the repository

- Warm-up and dropped caches: already in `docs/benchmark-plan.md` ("Method": 60 s warm-up) and
  in the cold scenarios of `bench/ondemand.sh`, `bench/q2-gaps.sh` and `bench/load.c`.
- No test, interval or ECDF existed: `bench/report.py` gives the median and p5-p95 across
  repetitions and marks a row inconclusive above 20 % spread.
- Repetitions: the plan asked for at least 7; the 2026-10-14 steady state had 10.
- CPU frequency: not controlled and not recorded in `environment.txt`.
- SQLite: `bench/sqlbase.sh` sets WAL and `synchronous=NORMAL` only; `cache_size` and
  `mmap_size` are SQLite's defaults and were not stated. The SQLite reports
  (`2026-10-12-macmini-sqlite`, `2026-10-14-macmini-sqlext`) do not say that SQLite checks no
  permission. Both remarks hold.
- End to end: there is no production deployment. The staged test of the first client
  (`bench/client/`) is the closest; that remains the direction.

## Changes

- `bench/compare.py` (new): two systems or two builds, per workload, operation and percentile.
  The unit is one repetition's percentile, so calls within a repetition are not treated as
  independent samples. Gives the medians, the relative difference, its 95 % percentile
  bootstrap interval (10 000 resamples, fixed seed) and the two-sided Mann-Whitney U p-value
  (normal approximation with tie and continuity corrections); "differs" only when the interval
  excludes 0 and p < 0.05; "too few repetitions" below 5 per side; a flag below 30. Writes
  `compare.csv` and `ecdf.csv` (every call, pooled per side, at every 0.1 %). Standard library
  only, like `report.py`, whose reader it reuses.
- `bench/cpu-freq.sh` (new): `fix` sets the `performance` governor on every CPU and turns turbo
  off (`intel_pstate/no_turbo` or `cpufreq/boost`), saving the previous state; `restore` puts it
  back; `show` prints one line, now part of `environment.txt` in `bench/run.sh` and
  `bench/ondemand.sh`.
- `docs/benchmark-plan.md`: dated revision of "Method" (test and interval, 30 repetitions for a
  reported comparison, ECDF, CPU frequency, SQLite's permission check and cache stated and
  varied, more than one machine before generalizing, end to end as the strongest evidence).
- `bench/results/2026-10-14-macmini-release/README.md`: dated note that no difference is
  claimed until the comparison is rerun under the revised method.
- `bench/smoke.sh`: runs `compare.py` on its two repetitions and checks it refuses a verdict.
- `docs/benchmark-harness.md`, `docs/references.md` (Efron 1979; Mann and Whitney 1947; the
  kernel's cpufreq documentation).

## Tests

- `compare.py` on synthetic records, 30 repetitions per side: B 10 % slower → "differs" on p50,
  p95 and p99 (p50: +9.4 %, interval +7.9 % to +10.9 %); the same distribution on both sides →
  "no difference detected" on all three (one p-value of 0.035 with an interval containing 0,
  which the combined rule does not call a difference); 3 repetitions → "too few repetitions".
- Mann-Whitney p-values equal SciPy 1.x `mannwhitneyu(..., method="asymptotic",
  use_continuity=True)` to six decimals on three samples, one with ties.
- `bench/cpu-freq.sh` passes shellcheck; on a machine without cpufreq, `show` says "absent". Not
  run on the benchmark host.
- `make bench-smoke` in a Debian 13 container: `BENCH_SMOKE_PASS`, now ending "compare refuses a
  verdict on 2".

## Limitations and next step

- The 8 to 15 % question stays open: rerun the steady state of `bench/ondemand.sh` with 30
  repetitions, the frequency fixed, and `compare.py`, on the benchmark host.
- The SQLite comparisons are to be rerun with `cache_size`/`mmap_size` varied and the
  permission asymmetry stated next to every number.
- Many rows are tested at once: with 5 shapes and 3 percentiles, a 5 % level gives false
  positives; the interval rule reduces but does not remove that. A correction for multiple
  comparisons is not applied.

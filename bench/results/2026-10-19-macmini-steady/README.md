# Steady-state QUERY2 under the revised method; a regression found and fixed (journal 0139)

Benchmark host (Mac mini Server 2011, i7-2635QM, Debian 13, kernel 6.12.111+deb13-amd64), the
data of `2026-10-13-macmini-ondemand` (domain `ns3`, about 7 500 files of profile 8 on ext4).
Method of `docs/benchmark-plan.md` (revision of 2026-10-19): CPU frequency held by
`bench/cpu-freq.sh` (governor `performance`, turbo off, restored at the end), a fresh insmod and
mount per module and repetition, one discarded warm-up pass, module order rotated every
repetition, `bench/compare.py` (median of per-repetition p50, 95 % bootstrap interval,
Mann-Whitney U). Shapes: `v2-dom` Q1 two equalities (253 results), Q2 one equality (253), Q3
one equality (3 030); `v2-recent` Q1/Q2 the newest 20 of one kind / one agent. User-space tools
of `main` at 7292cee for every module.

## main-run: 40 repetitions, modules 0084, 0102 and main (7292cee)

p50 medians, µs, with the relative difference and its 95 % interval against 0084:

| Shape | 0084 (index at mount) | 0102 (on demand) | main |
| --- | --- | --- | --- |
| v2-dom Q1 | 166.2 | 178.4 (+7.4 %, +6.7..+7.9) | 330.9 (+99.2 %, +97.7..+100.5) |
| v2-dom Q2 | 103.7 | 113.9 (+9.9 %, +8.5..+12.4) | 253.8 (+144.8 %, +142.9..+148.7) |
| v2-dom Q3 | 1 200.8 | 1 348.8 (+12.3 %, +11.7..+13.0) | 1 576.1 (+31.3 %, +30.6..+32.1) |
| v2-recent Q1 | 27.4 | 28.6 (+4.3 %) | 29.1 (+6.3 %) |
| v2-recent Q2 | 141.0 | 146.6 (+4.0 %) | 146.2 (+3.7 %) |

Every row "differs" (p < 1e-11, interval excludes 0).

## bisect: 12 repetitions, 0102 and the module of each merge that touched layer/ since it

v2-dom against 0102: fcf6dbc (#67) and c8801dc (#70) within ±1.2 % with intervals containing
0; 6502d46 (#71) +84 % / +123 % / +16 % (Q1/Q2/Q3); every later merge the same as #71.

## fix: 20 repetitions, 0102, main and main with the per-file result buffer

| Shape | 0102 | main | fix |
| --- | --- | --- | --- |
| v2-dom Q1 | 178.2 | 331.0 | 180.4 (+1.2 % vs 0102, +0.3..+3.4) |
| v2-dom Q2 | 116.7 | 256.1 | 118.3 (+1.4 %, -0.2..+3.3, no difference detected) |
| v2-dom Q3 | 1 351.8 | 1 572.2 | 1 401.9 (+3.7 %, +2.6..+4.1) |
| v2-recent Q1 | 28.6 | 29.2 | 28.4 (-0.8 %) |
| v2-recent Q2 | 145.8 | 145.8 | 145.8 (no difference detected) |

Fewer than 30 repetitions in the bisect and fix runs: they locate and confirm; the 40-repetition
run is the measurement. Files: `*/environment.txt` (kernel, module hashes, CPU frequency before,
during and after), `*/runs.txt` (one line per run: srcversion, mount time, frequency, load,
GET_INFO), `*/cmp-*.csv` (compare.py output), `driver.sh` and `driver-bisect.sh` (the runs). The
raw per-call records stay on the host.

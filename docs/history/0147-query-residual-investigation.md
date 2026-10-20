# 0147 — The QUERY2 residual survives first-cycle removal; the tail has a startup effect

Date: 2026-10-20. Status: investigation in progress; existing evidence reanalysed, no new module
benchmark or code fix.

## Goal and scope

Investigate the unexplained +1–3% median latency difference between module 0102 and the #101
fix in journal 0143, in parallel with the independent review. Follow handoff sections 2/2.1,
read the relevant ADRs and journals 0134/0139/0143, and isolate work in branch
`codex/performance-residual-investigation`, based on `syz-fifth-run` at `dfe114c`.
No kernel module, host setting or existing evidence was changed. The source review compares
0102-era source (`7dc42e5`) with the measured fix (`7e446ff`); the old binary's precise build
mapping remains to be recovered before causal attribution.

## Evidence and findings

The local `m40.tgz` archive contains the original per-call records. The new reproducible
[analysis and report](../../bench/results/2026-10-20-residual-analysis/README.md) reads it without
extraction, checks all 20,000 calls from m0102/fix40, records its SHA-256 and retains 1200
per-repetition summaries plus comparison/sensitivity tables. Results equal the published
confirm40 medians at their recorded precision.

- After removing each shape's first call, filtered p50 differences remain +1.16%, +2.62%,
  +3.03%, and paired 95% bootstrap intervals exclude zero. Removing five cycles also leaves
  the residual. The first-cycle hypothesis therefore does not explain the median residual.
- Filtered Q1's p99 difference is +54.03%, not the +1.21% median difference. The first call is
  the slowest in all 40 repetitions of each module. Removing the first cycle reduces that
  p99 difference to +3.33%. The warm-up uses a separate process/descriptor; it cannot warm
  the measured process's retained result buffer or user array. Startup effects must be
  distinguished from same-descriptor steady state. Allocation/page faults were not profiled.
- The 3030-result delta is about 41 us and stable across time halves and rotation groups;
  the small Q1 delta is more sensitive to order. Three cyclic orders leave fixed predecessor
  effects; m0084's much larger mount scan is another possible carryover source.
- Source candidates include the new large kernel destination footprint, the extra candidate
  pass, and a struct size increase from 40 to 48 bytes on x86-64. Both versions already copy
  every returned byte to the caller. The qbuf lookup is per call and alone does not fit the
  result-count pattern. These are hypotheses, not measured costs.
- No logged domain release/rebuild or indexed-size difference explains the result. CPU policy
  was recorded, but affinity, effective frequency, thermals and exact concurrent processes
  were not. The 40 repetitions cover only about 109 seconds, not independent daily sessions.

## Decisions and alternatives

Keep the original +1–3% observation with its limitations; do not declare it explained or
optimize a security fix on these correlations. Do not rerun the historical driver: it unloads
host modules and changes host controls, whereas current rules require a dedicated VM. A
controlled VM experiment is specified in the report: A/A, balanced AB/BA without m0084,
same-descriptor warm-up, separate startup series, count/limit variation, source-isolated
variants and profiling. No experiment is claimed executed.

## Verification actually performed

- Ran `analyze.py` on the preserved archive: validated 20,000 successful calls, 40 matched
  repetitions, and expected counts (253/253/3030 and 20/20); produced all result CSVs.
- Compared all 10 unfiltered p50/p99 medians against the original confirm40 CSVs: agreement
  within 0.0001 us, their published precision.
- Clang `-target x86_64-linux-gnu -x c -fsyntax-only` with equivalent candidate field declarations
  and static size assertions confirmed 40/48 bytes. This is an ABI layout check, not a module
  build or runtime test; allocator bucket sizes were not checked.
- `git diff --check` passed. No `vm-test`, KASAN or new Linux benchmark ran in this work unit.

## Limitations and next step

Raw calls remain in the local archive outside Git; committed summaries and archive hash make
this round auditable but require that archive to regenerate. Confidence intervals remain
conditional on one host/session/data set, and the sensitivity tests are exploratory.
The residual's cause remains open. Next: recover historical build provenance, coordinate an
idle dedicated guest, then execute the controlled experiment before proposing a module change.

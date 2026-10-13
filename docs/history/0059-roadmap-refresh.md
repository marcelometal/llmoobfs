# 0059 — Roadmap brought up to date and work order agreed

Date: 2026-10-12. Status: done (local series, published later with the following steps).

## Why

`ROADMAP.md` still said that the benchmark harness did not exist and listed batch operations as
unbuilt, while ADR-0009 (phases Q2a to Q2e), the Mac mini campaigns, the first real-client test and
the tooling work had landed. Planning from it would start from a wrong state.

## Decision

From here, work advances as a series of local steps (journals per step, `make vm-test` and
`vm/kasan-local.sh` before publishing) in an order agreed with the maintainer, taken from items
already defined in the roadmap, `docs/security-roadmap.md` and `docs/client-test-plan.md`:

1. S1 threat and credential model (ADR), then community action 15 (independent review).
2. Real-client test, stage 2: `QUERY2` with ai-memory's data and query shapes.
3. Remaining M3: phases 3a and 3c.
4. S4 (hostile lower tree) and S2 (fstests).
5. Community actions 17–18 and the rest of S6.

The M4 report stays planned: questions 1 and 4 of the benchmark plan have evidence, question 2 is
partial and question 3 is not measured.

## Changes

- New "done" rows: M3 verifications (0030), ADR-0009 `QUERY2`, M4 harness and first campaigns,
  real-client stage 1, tooling and static analysis.
- "Planned" rewritten in the agreed order; the batch row now says `QUERY2_BATCH` exists and only
  `READ_BATCH` waits for M4.
- Correction: only "Tools and sanitizers" and "Native Linux VM" are required status checks on
  `main`. The KASAN workflow runs on every `layer/` and `vm/` pull request and the process in
  `docs/testing.md` requires a green run, but branch protection does not enforce it (community
  action 18 stays open).

## Next step

Item 1: the S1 ADR.

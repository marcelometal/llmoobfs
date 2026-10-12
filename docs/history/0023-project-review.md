# 0023 — Review of implementation, plans and next experiment

Date: 2026-10-10. Status: completed as analysis; fixes and reproductions pending.
Base: `36eb64b`, branch `m3/3b-query-authz`.

## Goal and result

Review maturity, architectural direction, code and evidence before widening the
scope. Report: [project review](../project-review-2026-10-10.md).

ADR-0007 kept as the direction. Recommended: close the correctness and authorization of QUERY,
measure a reduced M4 and demonstrate memory across sessions. These are recommendations, with no
new architecture decision or authorization of implementation/publication.

Findings by reading: the transition to RAW leaves the previous indexed entry; a path
truncated before authorization can disappear from the query; lookup failures are
converted into absence; the checker omits traversal failures. Risks to reproduce:
identity across Btrfs subvolumes and use of an idmapped lower. Recorded limitations
of scanning, budget, portable metadata and status divergences in the documents.

Primary sources consulted for Btrfs, SQLite, OverlayFS and summaries of MemGPT,
LongMemEval and A-MEM. Proposed implications separated from measured results.

## Validation and limits

`make test BUILD=build/review-20261010`: 60 attribute checks and 9 Python tests
passed. No new run of the module/VM in this review. KASAN of the branch, run
38079499011, still in progress at the time of the check. Earlier Linux results identified
as historical evidence, not rerun tests.

Only the review documentation changed. `docs/community-plan.md`, already unversioned
on arrival, preserved. No push, public issues or changes to the site.

## Next step

Turn R1–R3 into Linux regressions and small fixes; reproduce R4/R5 before
claiming support for those configurations. Update status after KASAN actually completes.

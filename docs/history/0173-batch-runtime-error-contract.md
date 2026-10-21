# 0173 — QUERY2_BATCH runtime-error contract (issue 112)

Date: 2026-10-21. Status: investigation in progress; local preparation only.
Started: 2026-10-21T12:47:52.803847+00:00. Calendar time, not measured labor effort.

## Goal and scope

While PR 124 runs CI, prepare a separate small stabilization unit for issue 112.
Do not change or publish the PR 124 branch, the module, UAPI or filesystem format.
No push, issue closure or new PR is authorized for this unit.

## Source evidence and decision

`do_query2_batch()` in `layer/index.c` validates all requests first, then calls
`query2_one()` in order. Each successful call copies its results to userspace.
The request headers are copied back only after all requests have succeeded.
An execution failure can therefore leave earlier arrays populated without
the corresponding output counts or cursors. A final header-copy fault can
also leave outputs partially written. This matches the round-2 finding R2-9.

Spec section 4.3 promises no output for validation failures, but does not give
clients a rule for execution failures. Clarify the existing behavior: only
a zero return makes batch output usable; on error discard all output arrays,
counts, truncation flags and cursors from that call. There is no rollback or
partial-success protocol. Retry as a fresh request or from a cursor of a
previous successful call, after addressing the cause.

Alternative: copy completed headers on an execution failure. Defer that design:
it needs an explicit partial-success interface and rules for partial header
copies. The selected clarification preserves current behavior and ABI.
No new architecture or policy is introduced, so no new ADR is needed.

## Validation plan and limitations

Prepare a deterministic dedicated-VM regression with readable result arrays:
a later request uses an invalid results address after a valid earlier query.
Require EFAULT and prove an earlier result was actually copied, while its
header stayed unchanged in this particular path. Compare with a successful
single query; include a successful batch and validation-before-execution
control. No allocator fault injection or dedicated remote host is needed.
This observes the existing non-atomic copy behavior, not a guarantee about
which output bytes remain unchanged after every possible error.

Implementation and runtime validation are pending. PR 124 KASAN/VM results
must not be attributed to this separate unit. Issue 112 remains open.

## First implementation

Add `tests/batch-errors.c` through the shared build list. Run it as uid/gid1001
without supplementary groups in the existing static batch fixture, before
and after fallback on each lower. Use a PROT_NONE mapping for a later result
and read-only request headers for the final-copy error; neither needs fail-nth.
Compare the earlier copied result with a nonempty successful single query.
Require no writes on validation failure and successful reuse with reinitialized
requests after both execution-copy errors. These bounded observations are not
a promise that arbitrary errors leave particular bytes unchanged.

Strict static Linux compilation with `-Wall -Wextra -Werror` passed. Whitespace,
retired claims, 12 site pages and FAQ checks passed before the final surface
update; rerun them for the final tree. Dedicated-VM execution is still pending.

Public surfaces: spec/client contract, testing/development guides, site guide,
both READMEs and changelog/index updated. Roadmap, ADRs, FAQ, compatibility,
SECURITY and generated journey pages remain unchanged because this clarifies
an existing error contract, without a feature, support or milestone change.
No new external source or mechanism is introduced; evidence is this repository's
implementation and journal 0038, not a new third-party algorithm.

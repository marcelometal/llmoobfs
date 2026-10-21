# 0173 — QUERY2_BATCH runtime-error contract (issue 112)

Date: 2026-10-21. Status: PR 125 merged after combined-head CI (dated update below).
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

## Complete native-VM result and final local state

The frozen `d51ec27` snapshot passed `make vm-test` on the pinned Debian
6.12.111+deb13-amd64 guest: exit 0, 2026-10-21T12:50:27.535792Z to
2026-10-21T12:59:47.608748Z, 560.073 seconds (9 min 20 s). All 1,530 archived
source hashes were unchanged after execution. M1, acceptance and layer final
markers are present, with no `MOOBFS_VM_FAIL`. Modules ran only in the guest.

Each of `BATCH_VALIDATION_ATOMIC_PASS`, `BATCH_LATE_RESULT_FAULT_PASS`,
`BATCH_HEADER_FAULT_PASS` and `BATCH_RUNTIME_ERRORS_PASS` appears six times:
before and after fallback on ext4, btrfs and XFS. The positive single-query
reference was nonempty, so an empty result could not hide the copy errors.
The valid reinitialized batch succeeded after the two copy failures.

`make test` passed 60 attribute checks and nine image tests; ShellCheck 0.10.0,
SPDX, `make checkpatch`, retired claims, the 12-page site and FAQ consistency
passed. Casefold and stalled-FUSE tests executed; idmapped lower remains a
coverage exclusion in this native harness. No allocator injection, concurrent
batch-copy campaign or new KASAN result is claimed. This unit does not change
the production module and is not an external audit.

Only this journal changed after the executed snapshot; rerun documentation
checks, not the full VM, for this evidence completion. PR 124 remains unchanged
and its CI does not validate this new unit. Keep the prepared branch local and
issue 112 open until review and authorized publication/integration. Before a
future push, reconcile main (including PR 124 if merged) without overwriting
the other sessions, then run the applicable combined checks. Private manifests,
console log, receipt and PR draft are retained outside Git.

Locally completed: 2026-10-21T12:59:47.608748Z (test completion; calendar time,
not measured labor effort). Publication and merge are not authorized for this unit.

## Publication authorization — 2026-10-21

After local review, the maintainer explicitly authorized pushing this unit and
opening its PR, using GitHub CI for KASAN. This supersedes the earlier local-only
publication restriction for this batch; no new sanitizer result is claimed.
KASAN and other CI must validate the new published head. Merge is reserved
for separate authorization. Preserve PR 124's head and other sessions.

The pre-publication fetch found main still at `712e0dd`; no remote branch or
PR already existed for `codex/issue112-batch-errors`. The production source
and executed tests remain identical to the full native-VM snapshot `d51ec27`;
only this journal changed afterward. Recheck remote refs before the single
normal push. Issue 112 remains open pending authorized integration.

## Integration with PR 124 — 2026-10-21

The maintainer authorized merging PRs 124 and 125 once CI and KASAN pass.
All 12 checks passed on each original head: `7b2b798` (124) and `9fa6103`
(125). Their KASAN/lockdep runs 39601849588 and 39604618943 completed with
the final full-suite marker, no VM/environment failure marker, and the expected
new permission/release or batch-copy checks respectively. The repository's
`kdiag` check also passed on both retained dmesg captures. Configuration flags
were verified in job logs; the artifact itself did not retain `.config`. CPU
mitigation notices containing WARNING inside their text are distinguished
from kernel warnings by the existing checker; no checker change was made.

PR 124 was merged normally as `4b3a3282e469c989b2b21ed87fcc5dcdd0b56c81`.
Issue 114 remains partial: unprivileged allocation faults are still deferred.
Strict main protection requires PR 125 to include that updated base. Merge
it normally into this branch, preserving the signed commits and both test
sets. Resolve only overlapping changelog, history-index and site-guide additions
by retaining both contributions; back up the conflicts before resolving them.
No production source, UAPI, format or sanitizer configuration changes.

The original-head KASAN results are historical evidence, not a result for the
combined integration. After the integration push, require new complete CI
on the combined head before merging 125, without an admin bypass. Preserve
other worktrees and their pre-existing changes. No local dedicated-host run
is attempted; use the authorized GitHub CI for this publication/integration.

Integration pre-push checks passed: strict static Linux compilation of both
`batch-errors` and `permission-gaps`; five existing harness-verdict regression
groups in the pinned Linux container; whitespace, retired claims, all 12 site
pages and FAQ consistency. The two helper sources are byte-identical to their
individually validated heads. The full combined VM/KASAN result is still pending
GitHub CI; these checks do not substitute for it.

## Combined-head result — 2026-10-21

PR [125](https://github.com/studiomoob/llmoobfs/pull/125) passed the required
checks on its combined head `cf6b773`, including KASAN/lockdep with
`BATCH_RUNTIME_ERRORS_PASS` and the permission/release checks inherited from
PR 124. The retained console ended in `MOOBFS_KASAN_PASS` without a VM or
environment failure marker. The maintainer-authorized normal merge created
`f214bd26f7c2588fda5cae8ac8061736f39e0251` on `main`. Issue 112 was
still open immediately after the merge; it was reconciled and closed on
2026-10-21 after the maintainer requested issue updates. This result does
not alter the API's discard-on-error contract.

# 0176 — Publish the review batch and preserve its pause checkpoint

Date: 2026-10-21. Status: **in progress**.

## Goal and decision

The maintainer authorized a push and pull request for the locally validated
issues 110 and 111, asked that the related documentation and plan reflect the
work, and asked to pause new activities after this publication unit. Keep the
two corrections together in one PR. Do not merge the PR or close issues 110
and 111 before published-head CI and review. Preserve the independent issue
112 and 114 records rather than treating their merged changes as part of this
new correction.

## Prepared evidence and changes

Journal 0174 records the root-path race, its negative round and validation;
journal 0175 records the index-budget race, its negative round and validation.
The integrated local `make vm-test` passed on ext4, btrfs and XFS at `b91be9b`
with all tracked source hashes unchanged; KASAN/UBSAN/lockdep passed on the
same kernel/layer code at `ed9e273`. The later local commit `7c7d744` changes
only the factual journal. The local worktree was clean before this
documentation update.

`ROADMAP.md` now names the completed local evidence, the published-head CI
gate and the order on resumption. `docs/security-roadmap.md` distinguishes
the two targeted fixes from S9 fair sharing and the wider S14–S21 test plan.
The separate Dependabot PR 75 has nine passing checks on an older head but is
behind `main` under strict status checks; its update is a distinct maintainer
decision. Issue 112 was merged by PR 125 and closed after its body recorded
the result. Issue 114's checklist now reflects completed T5/T6/T7/T8/T10/T11;
it remains open with T9's unprivileged allocation-fault check pending.

## Publication and issue state

Pending push, PR creation, updates to issues 110/111 and exact-head CI. Record the
actual PR URL, head, checks and any limitations here when known. Pause new
implementation after the requested publication and status updates.

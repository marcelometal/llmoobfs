# 0176 — Publish the review batch and preserve its pause checkpoint

Date: 2026-10-21. Status: **published for review; exact-head CI pending**.

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
After the documentation edits, `make test` passed 60 attribute checks and
nine image tests; `make checkpatch`, `scripts/check-spdx.sh`,
`scripts/check-retired-claims.sh`, `scripts/check-site.py` and
`scripts/site-faq.py --check` passed. A complete local `make lint` was not
available because this host has no `shellcheck`; the standalone SPDX check
ran, and PR CI must supply the ShellCheck verdict. No module, test or harness
source changed after the complete integrated VM run in journal 0175.
The separate Dependabot PR 75 has nine passing checks on an older head but is
behind `main` under strict status checks; its update is a distinct maintainer
decision. Issue 112 was merged by PR 125 and closed after its body recorded
the result. Issue 114's checklist now reflects completed T5/T6/T7/T8/T10/T11;
it remains open with T9's unprivileged allocation-fault check pending.

## Publication and issue state

The normal push created remote branch `codex/issue111-over-budget` at initial
published head `44fff3e`; [PR 126](https://github.com/studiomoob/llmoobfs/pull/126)
targets `main` at `f214bd2`. The bodies of issues 110 and 111 now link the
PR and local evidence; both remain open. Issue 112 was closed as completed
after PR 125, while issue 114's body marks T5/T6/T7/T8/T10/T11 complete
and leaves T9 unchecked and the issue open. The PR 75 dependency update was
read-only in this batch; its branch remains behind `main` and its prior nine
green checks are not a verdict on a refreshed head.

At PR creation, required CI had begun on `44fff3e`; the documentation-only
follow-up that records this PR and pause checkpoint will start a new exact-head
run. Do not attribute either prior local KASAN or an earlier CI run to that
final head. No merge, issue 110/111 closure or new implementation is part of
this publication. Resume by checking exact-head CI, reviewing the PR and
deciding the merge; keep issue 114 T9 and PR 75 as separate future work.

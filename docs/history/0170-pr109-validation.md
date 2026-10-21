# 0170 — PR 109: integration and remaining validation

Date: 2026-10-20. Status: in progress locally; no push authorized.

Started: 22:42 America/Sao_Paulo on 2026-10-20. End pending.
Calendar time, not measured labor effort.

## Goal and scope

Resume PR 109 from `09fc102` in an isolated worktree. Merge public `main`
(`d797a68`) without losing either history, correct stale status records, and
complete the pending validation of R2-3, R2-4, R2-6, R2-8 and R2-10 before review.
Do not push, merge the GitHub PR or publish an advisory in this preparation.

## Integration and decisions

- Local merge `b8202b0` preserves both parents. The only conflict was the history
  index; entries 0155, 0167 and 0168 are all retained. Add the missing 0156 index
  entry as well. No kernel-code conflict and no change to another checkout.
- Journal 0156's original table predates later evidence. Preserve the old record
  and reconcile it with a dated addendum after validation. ADR-0017 is accepted
  (commit `7a5adda`); its commitments remain planned, not implemented.
- Use dedicated Linux VMs, the pinned Linux 6.12.112 source/configuration and the
  existing fail-nth mechanism and stack filter. Run negative mutations in another
  worktree; a compilation error is not a successful negative test.
- Add an actual rename with targeted path-allocation failure and dentry eviction
  for R2-6, with an uninjected control. Unknown paths are permitted by the API;
  stale paths are not.
- Add a directory-removal race for R2-10. Function tracing observes the scan's
  first file after listing; the existing scan-delay parameter holds the window.
  No production test hook is added. These two tests run in the KASAN faults step.

## Evidence and experiments

- Initial integrated `MOOB_ONLY=faults sh vm/kasan-local.sh` passed with
  `MOOBFS_KASAN_ONLY_PASS`. This is a subset, not a complete sanitizer run.
- First R2-3 mutation failed compilation: removing its callers left `rec_drop`
  unused under `-Werror`. Remove the unused helper in the negative branch and
  retry; that compiler failure is not regression evidence.
- New tests and negative rounds: in progress. Logs remain outside Git.
- R2-3 negative at `905c4e7`: the existing `setxattr-then-move` check detects
  fault 2, where the moved file is missing from the destination answer. It ends
  with `unexpected=1` and the harness fails. The unmodified integrated faults
  step passed; this is executed regression evidence, not a compiler failure.
- First R2-8 mutation (`3f46981`) unexpectedly passed: prior operations had already
  created the heterogeneous `kind` name node in the shared domain, so changing
  permissions could reuse it without the allocation this check needed to fail.
  Give each chmod attempt a fresh built domain. This retains the public syscall
  scenario while requiring the transition to allocate its new partition's node.

## Limitations and next step

Finish the positive/negative runs, reconcile journal 0156 and the public records,
then run the complete VM and local sanitizer suites for the final test changes.
Retain the existing deferred issues; this unit does not implement ADR-0017's
planned mechanisms or claim a new security review. Review before any push.

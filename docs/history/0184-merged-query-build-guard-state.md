# 0184 — Reconcile the merged issue-118 guard and retain its open policy question

Date: 2026-10-24. Status: local documentation correction complete;
publication pending.
Work started and ended: 2026-10-24. Labor time was not measured.

## Goal and observed state

After the maintainer authorized merging PR 135, reconcile current-state
documentation with the published experimental guard without claiming that
its residual availability costs were resolved. The PR's exact head
`94dc454` passed eleven reported checks, including Native Linux VM and
KASAN/lockdep, and merged as `2396c16` on 2026-10-24. The merge tree equals
the validated head tree (`git diff --quiet 94dc454 2396c16` exited zero).
The sole local worktree fast-forwarded to `origin/main`; issue 118 stayed open.
These are observations of the earlier PR and Git state, not new module tests.

## Decision and alternatives

The implementation is now on `main`; describe the limits as implemented and
validated, while keeping the numerical policy experimental. A valid
17,000-file domain returned `E2BIG` under the fixed 32,768-work-unit cap;
the denial saved only a few milliseconds and a small amount of measured
I/O in the controlled near-threshold comparison of journal 0182. The
successful 60,000-page client bulk replay covered ten roughly 6,000-page
domains on one host, not larger projects or slower lower storage. The cap
counts operations started by the layer, not strict CPU time or physical
I/O. Fixed-window bursts, a full UID table and legacy whole-mount `QUERY`
remain residual availability paths. These limits are not resolved by the
green CI result or the merge.

Retain the current guard while issue 118 stays open for the maintainer's
numerical-policy decision and broader workload evidence. Raising the cap
now could admit more legitimate domains but also more unprivileged scan
work; the existing measurements do not choose a safe universal threshold.
Removing the cap restores the original repeated-scan exposure. Treating
the cooperative deadline as a hard resource cap would overstate the code's
contract. No module or UAPI change is made in this documentation unit.

## Changes and validation

Update the English and Portuguese READMEs, security and use guidance,
security roadmap, site guide, ADR-0019 status, journal 0182 status, history
index and changelog to distinguish the merged guard from the open policy
question. Preserve the original dated local-checkpoint text in ADR-0019 and
journal 0182; add dated status notes rather than rewriting that history.
Add the superseded current-state wording to `docs/retired-claims.txt`.

`git diff --check`, `sh scripts/check-retired-claims.sh`,
`python3 scripts/check-site.py .` and
`python3 scripts/site-faq.py --check` exited zero. The site check covered
twelve HTML pages. No module or VM test was run for these documentation-only
edits; the eleven green PR checks belong to the prior module change.

## Limitations and next step

No new client workload, lower device or module test has been run for this
documentation correction. Decide with the maintainer whether the 17,000-file
denial is acceptable for the experimental contract, or whether a revised
numerical policy and its validation are needed. Keep issue 118 open until
that choice is explicit; S9 fair index-memory sharing is separate.

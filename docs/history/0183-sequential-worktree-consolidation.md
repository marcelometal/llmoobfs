# 0183 — Consolidate the local work and carry completed roadmap research forward

Date: 2026-10-24. Status: local documentation integration; not published.

## Goal and decision

The maintainer ended parallel local activities and chose one active worktree.
Complete or explicitly defer the existing work before starting another activity.
The completed client-roadmap assessment (journals 0164–0166) is carried forward;
the older QUERY3 specification remains a preserved proposal, not an implementation
or part of this integration. Issue 118 remains the next technical investigation.

## Changes and evidence

PR 130's exact head `e7c5660` had all reported checks successful and was
mergeable against `40645e2`. With the maintainer's approval, it merged into
`main` as `de957ba` on 2026-10-24. This is a remote CI result and merge
observation, not a test performed by this integration.

The local worktree inventory contained 17 entries before this step. Sixteen
linked worktrees were clean and removed after their branch tips were recorded;
the local branches and commits were retained. Two uncommitted old journal copies
and one history-index edit in the primary checkout were compared with the
fuller, later versions already on `main`, archived outside Git with the diff,
and removed or restored file by file. Ignored human-readable test logs were
copied outside Git and checked by hash before the worktrees were removed.
A verified bundle of local branch tips provides a separate recovery copy.
One worktree remains.

The three signed commits from the completed roadmap branch were cherry-picked
onto the post-PR-130 `main` in their original order. Conflicts in the changelog,
roadmap, history index and references were resolved by retaining the newer
current-state entries and the 0164–0166 proposal records. The original branch
remains intact. No kernel code, UAPI or disk format changed.
Journal 0182 remains on the unpublished issue-118 branch and is deliberately
not listed in this branch's history index yet.

## Validation and limitations

`git diff --check origin/main...HEAD`, `sh scripts/check-retired-claims.sh`,
`python3 scripts/check-site.py .` and `python3 scripts/site-faq.py --check`
passed. The site check covered 12 HTML pages. At this initial checkpoint,
no module, VM or benchmark test had been run for this documentation-only
integration. The roadmap items are proposals;
they have not been implemented or validated as product behavior. Issue 118 is
still local and its resource-limit policy is not approved for publication.

*Validation update, 2026-10-24.* `make vm-test` completed with exit status 0
in the Docker/QEMU Debian 6.12.111 test environment. The console contained
`MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS` and `MOOBFS_LAYER_PASS` once each,
with no `MOOBFS_VM_FAIL`. This exercises the unchanged module from `main`;
it does not validate the proposed G-items or replace a sanitizer run on a
future code change.

## Next step

Review and publish the roadmap documentation only with separate authorization.
After that unit closes, continue issue 118 in the same worktree, preserving its
local commits and negative-control evidence. Keep QUERY3 deferred until its
priority is explicitly reconsidered.

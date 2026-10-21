# 0172 — Permission and independent-oracle gaps of issue 114

Date: 2026-10-21. Status: locally validated within the restricted scope; push and PR authorized. T9 remains partial; batch KASAN is pending GitHub CI. Merge requires separate authorization.
Started: 2026-10-21T12:10:07.953095+00:00. Elapsed calendar time, not measured labor effort.

## Scope and decisions

Continue the remaining T5, T6, T8, T9 and T10 test gaps in a separate worktree
from `712e0dd`. The maintainer authorized a local batch of corrections, tests
and documentation for review before any push. No publication, issue edits or
merge are authorized in this preparation. Preserve the other worktrees.

T7/T11 were integrated by PR 123 at `712e0dd`, after all 12 published-head
checks succeeded; PR 122's TeleAI commits were included by the same merge.
Issue 114 remains open: its unchecked list is historical remote state, not
evidence that T7/T11 are still unimplemented.

Start with decisive tests: open a read-only/non-searchable domain as the actual
unprivileged caller before testing QUERY2's execute check (T5); assert both
count and `truncated` after permission filtering (T6); compare attributed
directories with lower-kernel access under that identity (T8). Fixed hostile
encoding fixtures must have expected membership independent of `common/attr.c`
(T10). T9's file-chmod regression already exists; investigate the remaining
unprivileged query coverage under faults and after index release separately.

No new product feature, format, UAPI or resource policy is intended. Do not
change correct behavior merely to satisfy a test; a discovered defect needs
its own evidence and minimal correction in this batch.

## Evidence and remaining work

No new test result is claimed yet. Read the exact implementations of
`query2_target`, `query_run`, `target_ok`, the existing fail-nth checks and the
release/rebuild suite. Plan positive controls and local module mutations that
must make each new decisive assertion fail. Run modules only in dedicated VMs;
freeze execution inputs before each validation.

Pending: implementation, positive/negative runs, full VM validation, relevant
lint/style checks, surface inventory, signed local commits and maintainer
review. KASAN of PR 123 is baseline evidence, not validation of this new batch.

## First implementation and compilation

Add `tests/permission-gaps.c`, built through the shared VM test list, and
`MOOB_ONLY=permgaps` in the existing suite (all three lower filesystems).
The helper forks and drops supplementary groups and all uid/gid slots to 1001.
T5 checks successful opens and the lower's execute refusal before QUERY2 and
QUERY2_BATCH; T6 checks 70 denied candidates plus exact-limit, overflow and
empty controls in v1 and four QUERY2 plans. T8 compares attributed directories
with lower `getxattr`, including a readable/non-searchable final directory.
T10 uses 18 fixed byte/mode/owner fixtures without linking the common validator.

Strict static compilation in the pinned Debian test image passed with
`gcc -static -O2 -Wall -Wextra -Werror -Iinclude tests/permission-gaps.c`.
This is compilation only. Dedicated-VM and mutation results remain pending.

## Initial targeted VM result and T9 extension

Frozen snapshot `ccdedd5` passed `MOOB_ONLY=permgaps make vm-test`, exit 0,
with `PERMISSION_GAPS_PASS` on ext4, btrfs and XFS. The snapshot's tracked
source hashes are unchanged after execution. This was a targeted run ending
in `MOOBFS_ONLY_PASS`, not a complete-suite or sanitizer result.

Extend T9 with uid 1001 queries before a real shrinker release and after the
rebuild, plus six unprivileged fail-nth sweeps (fresh/built domain, plain,
ordered and batch queries). Every fault child opens its own thread's control
fd as root, then irreversibly drops groups/uid/gid before opening the domain
and issuing any query. Error, success and disarmed retry must agree with the
actual lower's access decision. A sweep must inject faults and terminate.

The exact Linux 6.12.112 `proc_fail_nth_write` implementation was consulted:
it uses the task named by the open descriptor and has no write-time capability
check. This differs from the adjacent `make-it-fail` handler. Source access on
the dedicated host timed out; the pinned stable maintainer's source mirror
provided the exact implementation. No host address is recorded here.
T9's new runtime paths and all negative mutations remain unvalidated.

## Scope adjustment: dedicated host unavailable

The maintainer is away from the dedicated host and directed this continuation
to work only on what can be validated locally. Stop SSH attempts. Preserve the
unprivileged fault-injection candidate at local branch
`codex/issue114-unprivileged-faults-draft` (`1e77ad5`); remove its unexecuted
entry point and KASAN invocation from the review batch without deleting its
history. Retain the release/rebuild coverage, which runs in the local VM.
T9 remains partial until the fault candidate is validated later. This batch
must not claim all of issue 114 closed or a new sanitizer result.

The native VM launched from `1e77ad5` was interrupted to validate the smaller,
final source snapshot instead. Preserve the interrupted log; it is not a passed
run or negative evidence. No frozen execution source was edited.

## Local-only review scope and public surfaces

Final review scope retains T5/T6/T8, bounded independent T10 fixtures and T9's
release/rebuild queries. No production module, common validator, UAPI or
configuration change. The deferred fault candidate is preserved separately.

Update the testing/development guides, both READMEs, site guide, security
roadmap and changelog to distinguish fixed independent fixtures from the
randomized shared-validator oracle, and unprivileged release from root-only
fault injection. S14/S15 stay planned. No new milestone, compatibility,
support, disclosure or research position changes the generated journey pages,
FAQ, SECURITY, ADRs, main roadmap or help requests; retain their state and
check the site/FAQ rather than inventing a new claim of stabilization.

## Full native VM validation of the local-only batch

Frozen source `866257f` passed `make vm-test` on Debian 6.12.111+deb13-amd64,
exit 0, from 12:20:29 to 12:30:27 UTC on 2026-10-21: 597.82 seconds elapsed.
All 1,530 tracked source hashes matched afterwards; no frozen source was edited.
The final M1, acceptance and layer markers are present; no VM failure marker.

`PERMISSION_GAPS_PASS` and `UNPRIV_RELEASE_PASS` executed on ext4, btrfs and
XFS. Each release check observed eight real domain releases and tested uid
1001 before release and after rebuild. Casefold and FUSE scenarios executed;
the native idmapped-lower case remains explicitly not tested. No allocation
faults or new sanitizer run are claimed by this result.

`make test` passed the host attribute checks and nine image tests. ShellCheck
0.10/SPDX, `make checkpatch`, retired claims, the 12-page site, FAQ consistency
and whitespace passed. Negative module controls are still in progress; a
compiler/setup failure will not count as a successful negative control.

## Decisive negative controls

Five normal signed commits on the separate negative worktree each differ from
`866257f` in exactly one production source file. None is merged into the review
branch. Run the same frozen tests, with a single module mutation, in the pinned
Debian VM. Backups preceded each file-by-file mutation reset; no directory
reset, shared-history rewrite or change to another session was used.

| Gap | Local mutation commit | Mutation | Expected runtime failure observed |
| --- | --- | --- | --- |
| T5 | `e32e2e0` | Remove MAY_EXEC from query2_target | `T5 QUERY2 execute check decisive after successful open` |
| T6 | `5cff699` | Let a denied candidate set truncated after the visible limit | `T6 hidden candidates do not set truncated` |
| T8 | `0a30f68` | Admit attributed directories without the target read check | `T8 attributed directory differs from lower getxattr` |
| T10 | `8f08495` | The shared validator accepts bool payload 2 | `T10 independent encoding/permission membership` |
| T9, release only | `62b5c30` | Bypass caller_may_read for non-root callers | `T9 unprivileged release/rebuild permission answer` |

T5/T6/T8/T10 used `MOOB_ONLY=permgaps make vm-test`; T9 used
`MOOB_ONLY=release make vm-test`. All five launchers returned 2 after a real
guest assertion failure, with the M1 pass marker present, the expected helper
failure and VM failure marker present, and no partial-pass marker. All frozen
tracked source hashes remained unchanged. These are runtime failures, not
compiler errors. Each stops on its first failing ext4 scenario; do not claim
negative runs on btrfs/XFS. The positive complete run covers all three lowers.

## Final scope, limitations and next step

The final locally reviewed tree changes only tests, the shared test-build list,
the VM suite and documentation. No production module/common validator/UAPI,
kernel configuration, badge or citation changes. No production correction was
needed for the scoped fixtures; this is not evidence that other defects are absent.
After the executed `866257f` snapshot, only this journal's factual evidence
changed. A full VM rerun for that documentation-only completion is unnecessary.

T5/T6/T8 and the fixed independent T10 coverage have positive and decisive
negative evidence. The random hostile oracle still shares the common validator;
18 independent cases do not replace it with a universal independent parser.
T9 is partial: the new unprivileged control tests a shrinker release/rebuild;
allocation-fault queries in the enabled suite remain root-only. The deferred
six-case fault candidate is preserved at `1e77ad5` on the local draft branch,
not enabled or tested by this batch. No KASAN is claimed for the new batch.
The maintainer's local-only constraint permits review now; sanitizer validation
remains pending before a future authorized publication.

Retain native/negative logs, source manifests, interrupted attempt, draft PR
and proposed issue reconciliation outside Git in
`MoobFS-local/revisao/issue114-permissions-0172/`. Do not push, create a PR,
edit/close issue 114, merge or contact anyone during this preparation. T7/T11
were already integrated by PR 123; the new scope remains local until approved
and integrated. Other sessions' branches, commits and pre-existing files are
checked against the initial preservation receipt.

Locally completed: 2026-10-21T12:38:11.754431+00:00. Calendar time, not measured labor effort.

## Publication authorization — 2026-10-21

After the local review, the maintainer explicitly authorized pushing this batch
and opening a PR, with T9 partial and KASAN supplied by GitHub CI on the new
published head. This supersedes the preparation-stage local-only restriction
and the requirement to wait for local KASAN before publication for this batch.
The dedicated Linux host remains out of scope. No new KASAN result is claimed;
CI must validate the published head. Merge remains subject to separate approval.

The executed source snapshot remains `866257f`; only this journal changed
after its complete positive VM run and the five decisive mutation controls.
Preserve the recorded interrupted attempt and deferred allocation-fault candidate.
T9 is still partial, issue 114 stays open, and the PR references rather than
closes it. Recheck the remote main, remote branch and duplicate PRs before
publication; preserve the other sessions' worktrees and uncommitted files.

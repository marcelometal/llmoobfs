# 0172 — Permission and independent-oracle gaps of issue 114

Date: 2026-10-21. Status: investigation and local implementation in progress.
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

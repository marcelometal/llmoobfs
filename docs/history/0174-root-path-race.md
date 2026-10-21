# 0174 — A domain renamed while its build waits no longer stays fresh under the old name (issue 110)

Date: 2026-10-21. Original local status: reproduced, fixed and validated locally, including a complete
KASAN/UBSAN/lockdep run on the dedicated host; ready for the maintainer's review. No push, pull
request, merge or issue edit at that point. See the publication update below.

## Goal

Reproduce issue 110 (R2-5 of journal 0155) in a dedicated VM; if confirmed, prepare a minimal
fix with a regression test, a negative round and the documentation, in local signed commits,
for review before any push. Branch `codex/issue110-root-path-race` from `4b3a328` (PR 124
merged). Journal number 0173 belongs to the issue 112 batch (PR 125, still open at the start);
this is 0174.

## The defect

`query2_pin()` took the domain directory's name with `dentry_path_raw()` on the layer dentry,
then `domain_ensure()` waited for the domain's build mutex and, under the index lock, recorded
`dom->top_gen = ix->top_gen`. A rename of the domain directory (or of one above it) in between
raised `top_gen` before that snapshot. The build then recorded every name under the old prefix
and the generation that already counted the rename, so `domain_fresh()` held. QUERY2 answers
are unaffected (`scope_path()` strips the domain components); the mount-wide v1 QUERY resolves
the recorded names of files whose layer inode is not cached and omitted all of them until the
domain was rebuilt for another reason. `domain_ensure_all()` has the same order: the names of
the domains come from a scan made before each `domain_ensure()`.

## Reproduction

`tests/rootpath-race.c` (new) and suite step `rootpath`, on ext4, btrfs and XFS: 10 000 files
planted on the lower in domain `d`, 50 of them with `user.moob.kind`, caches dropped, layer
mounted with `domain_depth=1`. Process B issues the mount-wide QUERY, which builds `d` and holds
its build mutex. Process A issues QUERY2 on an open descriptor of `d`, takes the name and waits
for the mutex. When A has been seen in state `D` for five consecutive milliseconds and B has not
returned, `d` is renamed to `e` through the layer. Then the mount-wide QUERY must return the 50
files, each under `e/` and resolving to its inode.

The mutex wait widens a window that otherwise lasts microseconds; the module is not changed for
the test. An attempt that misses the interleaving (A not seen waiting, B already returned, or
`domain_builds` showing no build by A after B) exits 3, is retried up to three times on a cold
remount, and never counts as a pass.

Unfixed module (`4b3a328` sources), local Docker/QEMU VM, Debian 6.12.111+deb13-amd64:

| Run | Lower | Result |
| --- | --- | --- |
| reproduction, test `15d0dff` | ext4 | A in `D` after 6 ms; B 2.243 s; rename at 0.588 s; 4 builds; then 0 of 50 results and no build by the QUERY: `ROOTPATH_RACE_FAIL` |
| diagnostic copy, btrfs only | btrfs | 0 of 50, no build by the QUERY: `ROOTPATH_RACE_FAIL` |
| negative round `1297710` (final test) | ext4 | A in `D` after 7 ms; B 2.075 s (EAGAIN); rename at 0.380 s; then 0 of 50: `ROOTPATH_RACE_FAIL`, `MOOBFS_VM_FAIL: rootpath-race-ext4`, exit 2 |

The defect is confirmed dynamically: the domain is left fresh with the old prefix and the v1
QUERY omits every uncached file. The confidence "likely for the window" of journal 0155 is
replaced by a reproduction (with a widened window).

## Fix

`layer/index.c`, commit `8319ff5`:

- `rename_gen()` samples `ix->top_gen` under the index read lock. `domain_ensure()` takes that
  sample as a new argument `gen` and records it in `dom->top_gen` instead of the current value.
  A rename after the sample leaves the build stale; `query2_pin()` already loops (up to eight
  times) and builds it again with a new sample and name.
- `query2_pin()` takes the sample first and then the name **from the lower dentry**, relative
  to the lower root (`lower_rel_path()`, two pages). The rename hook `moobl_index_renamed()` runs
  inside `->rename()`, after the lower `vfs_rename()` moved the lower dentry and before the VFS
  moves the layer dentries (`vfs_rename()` in `fs/namei.c` of 6.12.112 calls `d_move()` after
  `->rename()`). A name read from the layer dentry after the sample could still be the old one;
  the lower name read after the sample is the new one, or the sample predates the rename.
- `domain_ensure_all()` samples once before its scan reads the domains' names and passes the
  sample to every `domain_ensure()`. A v1 QUERY that races a rename this way answers from a
  stale domain (names from the dentry cache, as after any rename during a build), and the next
  query builds the domain again; before the fix the domain stayed fresh with wrong names.

Prior art: the read side of a sequence counter — sample the generation, then read what it
protects — as path lookup does against `rename_lock`, already cited for `where_is()` (journal
0098, `docs/references.md`). No new mechanism, no UAPI, format or option change.

Alternatives not taken: reading the name inside `domain_ensure()` after the snapshot (the layer
dentry has the same hook-before-`d_move()` order, so it only narrows the window); failing the
build with `EAGAIN` when the sample is already old (saves one wasted build after a rename, but
adds an error path for a rare case).

## Validation after the fix

| Check | Result |
| --- | --- |
| `MOOB_ONLY=rootpath make vm-test`, sources of `a54ac31` + fix | `ROOTPATH_PASS` on ext4, btrfs, XFS; each: A in `D`, rename during B's build, 5 builds in the race (A builds once with the old sample, sees the domain stale, builds again), then 50 of 50 under `e/`, no build by the QUERY; `MOOBFS_ONLY_PASS`, exit 0, 18:16:54–18:18:37 UTC; tracked sources unchanged after the run |
| First fixed attempt, btrfs | B failed with `EAGAIN` (explained below); test changed to allow it (`a54ac31`) |
| `MOOB_ONLY=rootpath make vm-test` at `78ab4d1` (final suite step) | `ROOTPATH_PASS` on ext4, btrfs, XFS, exit 0, 18:37:14–18:39:17 UTC; 4 builds in the race on each lower and one build by the checking QUERY (which domain was not recorded); B answered on XFS and failed with `EAGAIN` on ext4/btrfs; 50 of 50 under `e/` everywhere |
| `make checkpatch` | no warning |
| `make test` (macOS host) | 60 attribute checks, 9 image tests |
| Retired claims, site (12 pages), FAQ consistency, whitespace | passed |
| Complete `make vm-test`, executables of `8319ff5` (docs uncommitted) | exit 0, 18:24:50–18:36:40 UTC; `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`, `ROOTPATH_PASS` on the three lowers, no `MOOBFS_VM_FAIL`; casefold and FUSE stall ran; idmapped lower `not tested`; tracked sources unchanged after the run |
| ShellCheck 0.10 on `vm/` and `scripts/`, SPDX, `sh -n` | SC2034 (unused loop variable) in the new step, fixed in `78ab4d1`; then clean |
| Complete `vm/kasan-local.sh` on the dedicated host, `git archive` of `8319ff5` (1 531 files) | exit 0, 18:19:58–19:12:55 UTC (52 min 57 s), Linux 6.12.112 with KASAN, UBSAN and lockdep: `MOOBFS_KASAN_PASS` with no kernel diagnostic, `ROOTPATH_PASS` on ext4/btrfs/XFS, `RENAMEPATH_FAULT_PASS`, `DEADSCAN_PASS`, `FAULT_INJECTION_PASS` (22 operations, 324 calls, 302 faults, unexpected=0), `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`; every snapshot file unchanged on the host afterwards. The fix adds an index-lock read section with no other lock held; lockdep reported nothing. `78ab4d1` differs from the snapshot only by one `echo` in the `rootpath` step |

`EAGAIN` from the lock holder: with per-result checks the v1 QUERY releases `query_sem` between
chunks and fails with `EAGAIN` when a domain was taken down meanwhile (`query_repin()`, the
documented v1 answer). The waiting QUERY2 takes the stale domain down, so this can happen with or
without the fix; the negative round shows it on the unfixed module too. The holder's role is only
to keep the build mutex; the decisive check is the query after the race.

## Development attempts (preserved outside Git)

Not results: (1) 20 000 files exhausted the inodes of the VM's ext4 lower (ENOSPC at f016370);
(2) the liveness probe reaped child B with `waitpid(WNOHANG)`; (3–5) the guest kernels give no
function names in `/proc/<pid>/stack` (empty) or `/proc/<pid>/wchan` ("0"), so the wait could not
be seen and the attempts were inconclusive; the process state `D` (`TASK_KILLABLE` of
`mutex_lock_killable()`) is used instead, with the build counter as a check after the fact.
Logs, source hashes, diagnostic copies, the negative worktree and the host helper are in
`MoobFS-local/revisao/issue110-0174/`.

## Limitations

- The reproduction widens the window with the build mutex; the unwidened window was not hit.
- Only a rename of the domain directory itself at depth 1 is tested, not one of a directory
  above a deeper domain, nor a rename racing the scan of `domain_ensure_all()`.
- The negative round stops at its first failing lower (ext4); the unfixed btrfs result comes
  from a diagnostic copy, XFS was not run unfixed.
- After a rename the waiting QUERY2 builds the domain twice; no measurement was made.

## Next step

The maintainer's review of branch `codex/issue110-root-path-race`. Publication, the issue update
and the merge need explicit authorization; GitHub CI (KASAN included) must pass on the published
head, which this local run does not replace; the issue is closed only after the merge. Issue 111
was worked in parallel on a branch stacked on this one (journal 0175).

## Publication update — 2026-10-21

The maintainer authorized one combined push and PR for issues 110 and 111.
This correction is included in [PR 126](https://github.com/studiomoob/llmoobfs/pull/126)
from `codex/issue111-over-budget`, together with journal 0175. Issue 110's
body now links the PR, negative reproduction and positive local VM/KASAN
evidence. The issue remains open; required checks on the published head,
review and the merge decision remain pending. The original local-only status
above records the state before publication.

# 0175 — A concurrent build no longer hides an index-budget refusal (issue 111)

Date: 2026-10-21. Original local status: **correction complete; unpublished at that point**. The
decisive negative round, corrected VM suite on the integrated branch and
KASAN/UBSAN/lockdep run have finished.

## Goal and decision

Issue 111 (R2-7 in journal 0155) describes a transient wrong refusal: when one domain
build exceeds the index budget, `charge()` sets the mount-wide `over_budget` flag. A
concurrent build clears it before the first build checks whether to release an older
domain. `QUERY2` can then return `ENOMEM` although the requested domain fits alone.

Replace the resettable flag with `ix->refusals`, a monotonically increasing counter
under the existing index lock. Each build samples it before scanning and compares it
after an `ENOMEM` result. A later build cannot erase an earlier refusal. This changes
neither the UAPI nor the budget policy. A per-build flag carried through every charge
site was an alternative, but would require more invasive plumbing. A refusal by a
different build can cause an extra, bounded release attempt when this build fails
allocation for another reason; it cannot make an incomplete answer successful.

## Changes and test design

- `layer/index.c` and `layer/moobl.h`: count refused charges and compare a build's
  sample instead of clearing and reading `over_budget`.
- `tests/overbudget-race.c`, `vm/build-tests.sh` and `vm/suite.sh`: make two attributed
  domains fit the budget individually but not together. Alternate `QUERY2` between
  them while four other domains are rebuilt after renames. Every alternating query
  must avoid `ENOMEM` and unexpected errors. The test tolerates `EAGAIN` under
  concurrent rename activity. It runs until at least 30 queries or a sixfold
  time cap, and prints the counts of `EAGAIN`, `ENOMEM` and other errors.
- The test now handles a failed `fork()` before signaling child PIDs. This prevents
  `kill(-1, SIGTERM)` if a fork fails under memory pressure.
- `vm/in-container.sh`: raise the TCG guest guard from 1,200 to 2,400 seconds after
  an otherwise progressing complete run reached the old guard during the btrfs
  release step. The timeout remains a hung-guest guard, not a pass condition.

The branch includes the issue 110 correction in its ancestry and integrates
current `main`, including the issue 112 batch-error tests. Their separate
journals are 0174 and 0173; the final combined suite exercises all three.

## Evidence

| Run | Result |
| --- | --- |
| Negative branch `neg/issue111-over-budget-unfixed` at `f46cc15`, final test with the old `layer/` | On ext4, 30 alternating queries, two `ENOMEM`; `OVERBUDGET_RACE_FAIL`, `MOOBFS_VM_FAIL`, VM launcher exit 2. The test stops at the first failing lower; no negative claim for btrfs or XFS. |
| Corrected branch, targeted `MOOB_ONLY=overbudget make vm-test` before the fork guard | ext4 30, btrfs 30 and XFS 31 alternating queries, zero `ENOMEM` or other errors; `OVERBUDGET_PASS` on all three, `MOOBFS_ONLY_PASS`, exit 0. |
| First complete local `make vm-test` | Guest reached `RELEASE_PASS: ext4`, then the 1,200-second QEMU guard expired during btrfs release. Launcher exit 2. This is **not** a complete pass or a kernel failure diagnosis. |
| Complete local `make vm-test` with the 2,400-second guard, before integrating current `main` | Launcher exit 0; `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS` and `MOOBFS_LAYER_PASS`; `ROOTPATH_PASS` and `OVERBUDGET_PASS` on ext4, btrfs and XFS. For issue 111, each lower answered 30 alternating queries with zero `EAGAIN`, `ENOMEM` or other errors. |
| Clean integrated `make vm-test` at `b91be9b` | Ran 2026-10-21 17:56:39–18:14:28 -0300; launcher exit 0; all three full-run markers, `ROOTPATH_PASS` and `OVERBUDGET_PASS` on ext4, btrfs and XFS, plus six `BATCH_RUNTIME_ERRORS_PASS` markers. Issue 111 answered 32/30/49 alternating queries on ext4/btrfs/XFS, with zero `EAGAIN`, `ENOMEM` or other errors. No `MOOBFS_VM_FAIL` or environment-fail marker. All 1,536 tracked-file hashes in the pre-run source manifest matched after completion. |
| Dedicated-host KASAN/UBSAN/lockdep, archived `ed9e273` snapshot | Launcher exit 0; `ROOTPATH_PASS` and `OVERBUDGET_PASS` on all three lowers; `MOOBFS_KASAN_PASS`, no diagnostic matched in `dmesg.txt`. The kernel and layer sources are identical between `ed9e273` and the integrated `b91be9b`; the later tree additionally contains the batch-error tests from `main` and test/harness documentation edits. |
| `make checkpatch` after the fork and timeout guard edits | Exit 0. `git diff --check` and `sh -n vm/in-container.sh` also passed. |
| Integrated static/documentation checks | `make checkpatch`, `scripts/check-retired-claims.sh` and `scripts/check-site.py` exited 0; the latter reported 12 valid HTML pages. |

The detailed local logs and source manifests are under
`MoobFS-local/revisao/issue111-0175/`, outside Git. The negative round uses the
same test and suite step as the positive commit `ed9e273` before the two guard
edits above; the branch comparison found no test or suite diff. The complete
run began at 2026-10-21T20:26:38Z and ended at 20:54:52Z. A second QEMU
invocation was inadvertently started concurrently for about 30 seconds at its
start, then stopped; the inherited run continued alone. The hashes of
`layer/`, the relevant test and the VM scripts matched its recorded source
manifest after completion. Five documentation hashes changed because the
already tested issue 110 journal was merged into this branch at the end of the
run; the tested code did not change. The subsequent integrated run had no
concurrent local VM and its complete tracked-file manifest matched.

## Limits and next step

The race is sampled across repeated queries, not forced by a scheduler hook.
The negative round proves it can occur on ext4 in this VM and this fixture;
the positive finite runs do not prove it cannot recur under every schedule.
No performance comparison or broad review was done. KASAN tested the same
kernel/layer code as the integrated branch but did not include its later
batch-error test; that test passed the clean integrated native VM suite and
the published issue 112 CI before this work. A GitHub PR, PR CI on the final
commit, merge and issue closure have not been performed. Publication and issue
closure need separate authorization.

## Publication update — 2026-10-21

The maintainer authorized the push and one combined PR for issues 110 and
111. The correction and its regression are in
[PR 126](https://github.com/studiomoob/llmoobfs/pull/126); issue 111's body
now links the PR, negative reproduction and local VM/KASAN evidence. The
issue remains open while required checks on the published head and review
are pending. No PR merge, issue closure or additional implementation is
claimed by this update.

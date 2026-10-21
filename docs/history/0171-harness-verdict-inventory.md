# 0171 — Preserve harness verdicts and state the actual test inventory

Date: 2026-10-21. Status: complete locally; review and publication gates remain.

First recorded timestamp: 2026-10-21 09:07:29 UTC; preparation began earlier.
Calendar time, not measured labor effort. The exact start was not measured.

## Goal and decisions

Prepare issue 114 T7/T11 on top of `f5c131a`, without modifying PR 109,
publishing or implementing the other test gaps. Include the already prepared
TeleAI README documentation locally for one proposed future publication. Keep one local batch with small
signed commits. No module runs on the host.

`race2` printed its summary before checking the forked query process. Wait for
that process first and only let the successful parent print a summary. Capture
command output and status before displaying it in the POSIX shell harness;
`pipefail` is not portable. Apply this to race2, race, buildrace and the
benchmark-oracle correctness check. Preserve racer stop/wait and scan-delay reset
on failures. Other query/grep pipelines are outside this narrow correction.

## Validation and experiments

- Five controlled userspace regression groups pass in the Linux Docker image
  under both Debian `/bin/sh` (dash) and BusyBox `sh`: failing race2/race
  producers with plausible output, failing buildrace, failing equivalence, and
  the real query program's forked-child verdict, each with a successful control.
  The shell tests execute the suite's actual command lines with substitute
  producers; stop-file cleanup is asserted on query failure.
- The child test compiles `tests/query.c` and `common/attr.c`, then uses a
  test-only `LD_PRELOAD` library to supply empty successful ioctl pages and to
  make the child's real `_exit` terminate with status 9. The actual fork and
  `waitpid` run. Failure must be nonzero with no race2 summary; success has
  exactly one summary from the parent. This is not module/query correctness
  evidence: ioctl is simulated and no module is loaded in this container test.
- Negative branch `neg/issue114-verdict`, final commit `d07b8f5`, restores only
  `tests/query.c` and `vm/suite.sh` from `f5c131a` while retaining the regressions.
  All five groups fail at runtime: four producers reach the synthetic success
  marker despite status 9; the child-failure case emits two race2 summaries
  before the parent reports its failed child. No compiler error is counted.
- The first fixture attempted to compile its ioctl mock without the required
  ioctl/variadic declarations and failed to build. Add the headers and correct
  the variadic signature. This is fixture repair, not negative evidence.
  An earlier negative checked the positive fork control first and detected
  duplicate summaries; reorder to isolate child failure first and inspect the
  captured equivalence log (the old grep pipeline consumes its stdout).
  Preserve the earlier logs rather than treating them as the final negatives.
- `make lint` passes via Docker ShellCheck 0.10.0 plus the SPDX checker;
  `make checkpatch`, shell syntax, retired claims, 12 site pages and generated
  FAQ consistency pass. Host `make test` passes 60 attribute checks and nine
  image tests. No new KASAN/lockdep run was started for local preparation.

Reproduce the controls without a module in the existing test image:

```sh
docker run --rm --platform linux/amd64 -v "$PWD:/work" -w /work \
  moobfs-m1-test python3 tests/test_harness_verdicts.py
docker run --rm --platform linux/amd64 -e SHELL_UNDER_TEST='busybox sh' \
  -v "$PWD:/work" -w /work moobfs-m1-test python3 tests/test_harness_verdicts.py
make vm-test
```

The same first command in the negative worktree must fail all five groups for
those runtime signatures. The container stage of `make vm-test` now invokes the
regressions before the module build. The first full VM run began before that invocation was added. Editing the
launcher while its shell was blocked in QEMU shifted the unread tail: the guest
printed every success marker, then the outer shell reported an unterminated
quote and `make vm-test` returned 2. Both macOS and Debian `sh -n` accept the
saved file. This is a preparation error, not a passing full command or negative
module evidence. Preserve its console/launcher logs and repeat `make vm-test`
with the launcher frozen for the entire execution. Do not reuse guest markers
to call the failed outer command a pass. A second attempt began with the scripts
unchanged at `ab8b71a`, then was stopped early when the execution procedure was
improved: run a `git archive` snapshot of a signed commit in an exclusive
directory outside the checkout. Preserve this interrupted attempt too; it is
not a failed-module negative or a completed suite. Compare tracked source hashes
with that commit before and after execution. Do not edit the running snapshot.
The final repetition below uses this frozen source.

The frozen repetition completed successfully: `make vm-test` returned 0 on
Debian `6.12.111+deb13-amd64`, x86-64 QEMU TCG (two CPUs, 512 MiB). Snapshot
`9be48961da32fec8b9d4af2f613a3bbec98796cb` ran from 09:23:48 to 09:33:03 UTC
on 2026-10-21: 555.1 seconds of elapsed calendar time, including container/build
work, not measured labor. All 1,527 archived source files matched the commit
before execution and retained their SHA-256s afterwards.

- `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS` and `MOOBFS_LAYER_PASS` are present;
  no `MOOBFS_VM_FAIL` is present. The launcher performed its final marker checks
  and returned 0, unlike the invalid first attempt.
- The three lowers passed QUERY2 races, build races and release/rebuild checks.
  `PERMMATRIX_STEP_PASS` covered 12 seeds; `BIGDIR_PASS` and `XATTRREAD_PASS`
  followed. The final module unload and kernel-log check passed under this
  harness; this is not a sanitizer run or a security proof.
- `CASEFOLD_REFUSED_PASS` and `STALL_PASS` actually ran; neither was skipped.
  The pinned Debian config has `CONFIG_FUSE_FS=y`. Avoid assuming that Debian
  always skips the FUSE scenario.
- `LOWERRO_PASS` explicitly says `idmapped lower: not tested`: read-only/freeze
  checks passed, while the idmapped-lower case did not execute. `ipc` is only
  built/staged. The KASAN-only allocation-fault scenarios were not executed.

Raw logs, source manifest, receipts, reverse patch and both earlier attempts
remain outside Git in `MoobFS-local/revisao/issue114-0171/`. Both negative commits
are local only. `PR-DRAFT-EN.txt` and `RESUMO-PT.txt` prepare the combined local
proposal; nothing was sent.

## T11 inventory and public surfaces

Clarify `docs/testing.md` and `docs/development.md`: a nonzero QEMU exit is an
infrastructure failure, while zero also occurs after guest failure poweroff.
The outer harness requires final markers and absence of `MOOBFS_VM_FAIL`.
Document `CASEFOLD_SKIPPED`, `STALL_SKIPPED`, and `LOWERRO_PASS`'s idmap suffix
as coverage exclusions; the full aggregate marker does not erase them.
`ipc` is compiled/staged, but never invoked by this suite. Landlock/notify are
standalone helpers outside its shared build list. Allocation-fault tests are
KASAN-only despite being compiled for Debian. Correct the stale 600-second guard
to the implemented 1200 seconds. Add a dated factual note to journal 0155;
preserve its original review rather than silently rewriting it.

Inventory walked: update the two READMEs and the English-only site test guide
with the same verdict/coverage limits; update the changelog, journal/index,
references and retired-claim list. No policy/support/architecture change affects
SECURITY, compatibility, when-not-to-use, FAQ, roadmap, ADRs or generated journey
pages, so their existing state is retained. GitHub issue 114 and external posts
are deliberately untouched: this is an unpublished local subset, and the other
test gaps remain open. No remote closure or publication is authorized.

## Local integration and continuity

After the frozen run, verify that `main` merge `2d15455` has exactly the file tree
of `f5c131a`. Normal local merge `fd06147` incorporates its ancestry without a
content change. Then verify that local `codex/teleai-readme` and open PR 122 both
remain at `40aef8e`; merge that branch locally as `3e2dd5e`. Preserve both parents
and both signed TeleAI commits. The only conflicts were the changelog and history
index: retain every entry, with 0169, 0170 and 0171 in chronological order.
The TeleAI worktree was not modified; PR 122 remains open.

Both READMEs now include the prepared TeleAI discovery row and the T11 coverage
limits. The listing was accepted manually at TeleAI commit `749a08c`; it is
editorial inclusion, not review or endorsement. All five badge lines and the
entire Zenodo citation section in each README, plus `CITATION.cff`, remain
byte-for-byte identical to the frozen source. The six files changed by the
TeleAI merge are documentation only; executable sources remain identical to
`9be4896`. Do not repeat VM tests for ancestry or documentation-only changes.

Separate baseline continuity: PR 109 was integrated at `2d15455` on 2026-10-21
09:23:48 UTC after all 11 checks succeeded on `f5c131a`. Its full
[KASAN run 39571198859](https://github.com/studiomoob/llmoobfs/actions/runs/39571198859)
reported `MOOBFS_KASAN_PASS`; the final allocation-fault step covered 22 operations,
324 calls and 302 faults with `unexpected=0`. That is evidence for PR 109, not
KASAN validation of this later T7/T11 batch.

## Scope and next step

T5/T6/T8/T9/T10 and the remaining issue 114 work stay open; this batch is only
partial work on issue 114. PR 122 stays open too. Propose one future publication
containing these corrections, tests and prepared documentation. Only after a
future combined PR is integrated should PR 122 be closed as superseded; do not
close it or issue 114 during local preparation. The PR draft uses a partial
reference, with no instruction to close the whole issue.

No kernel logic, UAPI, feature or resource policy changes. New batch
KASAN/lockdep before publication and required CI on the eventually published
head remain pending; maintainer approval for push/merge is separate. External
human review remains an unfulfilled factual limitation, not a gate that stops
continued experimental stabilization (maintainer clarification, 2026-10-21).
No external review is claimed and no production-readiness statement changes.

Final documentation checks include retired claims, the 12-page site, FAQ/source
consistency and whitespace. Source-hash comparisons, DCO and ancestry checks
accompany the local receipt. No push, PR creation, remote merge, issue closure,
external message or advisory publication was performed by this preparation.

Records closed: 2026-10-21 09:35:15 UTC. Calendar time, not measured labor effort.

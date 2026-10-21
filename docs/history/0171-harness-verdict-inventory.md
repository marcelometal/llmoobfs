# 0171 — Preserve harness verdicts and state the actual test inventory

Date: 2026-10-21. Status: in progress; local preparation only.

First recorded timestamp: 2026-10-21 09:07:29 UTC; preparation began earlier.
Calendar time, not measured labor effort. The exact start was not measured.

## Goal and decisions

Prepare issue 114 T7/T11 on top of `f5c131a`, without modifying PR 109,
publishing or implementing the other test gaps. Keep one local batch with small
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
  FAQ consistency pass. Full `make vm-test` is in progress; no final VM result
  is claimed yet. No new KASAN/lockdep run was started for local preparation.

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
regressions before the module build. The full VM run already underway began
before that invocation was added; the identical regression file is separately
executed above, and the staged `query.c`/suite correction is unchanged.

Raw logs and the reverse patch are outside Git in
`MoobFS-local/revisao/issue114-0171/`. Both negative commits are local only.

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

## Scope and next step

T5/T6/T8/T9/T10 and the remaining issue 114 work stay open. No kernel logic,
UAPI, feature or new policy changes. Finish the full VM evidence, retain its
actual skips and final markers, then review the signed local batch. KASAN/CI
publication gates remain for a later authorized publication; historical green
runs of PR 109 are not attributed to this continuation.

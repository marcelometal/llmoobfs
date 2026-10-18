# 0127 — KCSAN runs in CI

Date: 2026-10-18. Started 15:30, ended 16:00 (calendar time, local). Status: workflow written;
first run on the pull request's merge (it runs on demand and weekly, not on pull requests).
Estimated effort: S.

## Why

The data-race run of security roadmap S8 (journal 0115) needed the benchmark host, which is
often off; its filtered run was still pending. The KASAN workflow has used KVM on GitHub's x86
runners since pull request 74, so a KCSAN kernel can be built and booted there too.

## Change

`.github/workflows/kcsan.yml`: the job of `kasan.yml` with `vm/kcsan.config` instead of
`vm/kasan.config`, the suite steps `buildrace release` by default (selectable on dispatch), on
demand and weekly, not a required check. `vm/kasan-guest.sh` already names the detectors in its
final marker and judges KCSAN reports with `vm/kdiag.sh`: a report fails the run only when one of
its racing accesses is in `moobfs_layer`; the kernel's own races are printed on a
`KCSAN_OUTSIDE` line. `scripts/check-pins.sh` checks the workflow's kernel pin against
`kasan.yml`; `docs/development.md`, `docs/testing.md` and the security roadmap (S8) say so.

## Limitations

- KCSAN samples: a clean run says no race was observed in these steps.
- The negative round of the filter (an unmarked racy access in the module must fail the run) is
  still to be done; it fits the benchmark host or a dispatch from a branch.

## Next step

Dispatch the workflow after merge and record the result here.

*Note, 2026-10-18 (first run, 39143981872, on `main` after pull request 84).* Passed in 18
minutes: `MOOBFS_KASAN_ONLY_PASS: steps buildrace release under KCSAN and lockdep with no kernel
diagnostics`, with `BUILDRACE` and `RELEASE_PASS` on ext4, btrfs and XFS. `KCSAN_OUTSIDE: 32
report(s) with no racing access in moobfs_layer (10 reached through it)`: the kernel's own races,
printed and not judged. The negative round of the filter (an unmarked racy access added to the
module on branch `neg/kcsan-filter`, which must fail the run) was dispatched the same day; its
result goes into journal 0131.

# 0123 — How comparable projects get test hardware; a big-endian run and an arm64 job

Date: 2026-10-18. Started 09:10, ended 10:20 (calendar time, local). Status: research recorded
(`docs/research-test-infrastructure.md`); two CI additions written, not yet run. Estimated effort:
S.

## Goal

The maintainer asked to do everything that raises the project's maturity, to research how others
with the same problem solve it today, and to record it all, the journey included. The problem: one
maintainer, one benchmark machine that is often off, a module outside the kernel tree, numbers
from a single host, one kernel series, one architecture.

## What was learnt

In `docs/research-test-infrastructure.md`, with sources. In short: OpenZFS runs its functional
tests in QEMU VMs on GitHub runners, one image per distribution; bcachefs runs its own small fleet
and a dashboard; fstests has prebuilt appliances meant partly for research filesystems; upstream
test services cover the kernel tree only. Free resources: GitHub's arm64 runners for public
repositories, IBM's s390x cloud for open source projects by request, OpenBenchmarking.org for
volunteers' machines. Bare-metal testbeds (Grid'5000, whose open access program is closed;
Chameleon, CloudLab, RNP) and research credits open through an academic partner. Recorded as a
correction: an earlier search snippet had suggested Grid'5000's open access was available; the
program's own page says it is closed.

## Decisions

- Test what exists on more targets before anything else; nothing here adds a feature
  (stabilization rule of 2026-10-14, reaffirmed 2026-10-18).
- **Big-endian run** (`ci.yml`, job "Tools and sanitizers"): the attribute encoding's unit tests and
  the image tools, cross-compiled for s390x and run under QEMU user-mode emulation. The encoding is
  little-endian on disk and assembled byte by byte (`moob_attr_get64`/`moob_attr_put64`); the run
  keeps it so.
- **arm64 job** (`kasan-arm64.yml`): the KASAN/lockdep job on GitHub's arm64 runners, on demand and
  weekly, not a required check, until it is known whether those runners give QEMU KVM and how long
  a run takes.
- **Through an academic partner**: the follow-up to the professors contacted on 2026-10-14 will
  pair a research topic of the site with the testbed it could use.
- **Requests the maintainer can make**: IBM's LinuxONE open source cloud (s390x), AWS credits for
  open source.

## Alternatives

- A matrix in `kasan.yml` instead of a second workflow: rejected, it renames the required check.
- A build check against other kernel series (6.18, the next long-term series): useful, left for a
  next step; the module uses interfaces of 6.12 (`backing_file_open()` with a user file) and is
  expected not to build on older series.

## Results

Not run yet: both additions run on the pull request (big-endian) and on demand (arm64).

## Next step

Run both; record the arm64 timing and whether KVM is there; then the build check against 6.18.

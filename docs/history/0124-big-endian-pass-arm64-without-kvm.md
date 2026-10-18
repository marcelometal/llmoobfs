# 0124 — Big-endian tests pass; GitHub's arm64 runners have no KVM

Date: 2026-10-18. Started 10:40, ended 11:10 (calendar time, local). Status: big-endian run
passing on every pull request; arm64 job changed to run without KVM, not yet run again.
Estimated effort: S.

## Results of journal 0123's additions

- **Big-endian (s390x, QEMU user mode)**, job "Tools and sanitizers", first run on pull request 78
  (run 39120088604): `attr encoding tests: 60 checks passed` and the image tests `Ran 9 tests ...
  OK`. The encoding and the image tools behave the same on a big-endian target. It covers user
  space only; the module has not run on a big-endian kernel (an s390x VM was requested from IBM's
  LinuxONE open source program on 2026-10-18).
- **arm64 KASAN job**, first run on `main` (run 39120531299): stopped at "Let QEMU use KVM" in
  under a second, because `ls -la /dev/kvm` failed: **GitHub's arm64 runners expose no
  `/dev/kvm`**. Nothing was built.

## Decision

The arm64 job continues without KVM, the guest under emulation (TCG), and runs by default only the
suite step `ext4` (selectable when dispatching), with a 300-minute limit, until a run says how
long emulation takes. It stays outside the required checks. Alternatives: an arm64 machine with
KVM (a cloud instance or a donated host), recorded in `docs/research-test-infrastructure.md`.

## Public surfaces updated (rule of journal 0122)

The site's status page, both languages: the row on kernel versions now names the big-endian run
and the arm64 measurement in progress. README and help-wanted unchanged: they do not state the
architecture coverage.

## Next step

Dispatch the arm64 job; record its duration and result here or in the next journal.

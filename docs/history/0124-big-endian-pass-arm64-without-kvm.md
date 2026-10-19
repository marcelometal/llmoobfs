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

*Note, 2026-10-18 (third arm64 run, 39141437064, after pull request 81).* Without `/dev/kvm`
the job built the KASAN kernel and booted it under TCG, about 20 minutes in total. The guest
panicked 9 seconds into boot, before the module was loaded: "kernel stack overflow" in
`modprobe`. The trace is a recursion in the kernel's netfs read-retry path while reading the 9p
root of virtme-ng (`netfs_retry_reads` → `netfs_rreq_terminated` → `netfs_rreq_assess` →
`netfs_retry_reads`, repeating, then `p9_client_read`). It is a kernel issue of 6.12.112 under
KASAN on arm64, not a MoobFS one. The stable changelogs 6.12.105 to 6.12.113 list no change to
that retry path, so moving the pin does not help. The job stays exploratory and outside the
required checks. Options, none taken yet: a root filesystem image instead of the 9p root, or an
arm64 host with KVM.

*Note, 2026-10-18 (cause of the arm64 panic).* The recursion is a known netfs issue, reported by
syzbot in October 2024 (the same netfs_rreq_assess → netfs_retry_reads → netfs_rreq_terminated
cycle through the 9p read path). 6.12.112 carries the upstream workaround, "netfs: Work around
recursion by abandoning retry if nothing read" (the `NETFS_SREQ_NO_PROGRESS` checks in
`fs/netfs/read_collect.c`). Its author said in that thread that the workaround still recurses
when each retry reads a little. That fits a 9p root under TCG with KASAN's larger stack frames.
Mainline later moved read collection to a work item, so `netfs_retry_reads()` no longer calls
`netfs_rreq_terminated()` itself; that rework is not in 6.12. Options for the job: a root
filesystem image instead of the 9p root, or a later kernel series for the arm64 run only.

*Note, 2026-10-19 (correction).* The stack overflow was not the cause of the boot failure but a
consequence: virtme-ng 1.41's `virtme-ng-init` is built for x86-64 only, the arm64 kernel could
not execute it (error -8, run 39235747683), and the `modprobe` that overflowed on the 9p root is,
by its timing, the one the kernel starts to look for a binfmt handler. See journal 0137.

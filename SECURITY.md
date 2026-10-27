# Security policy

MoobFS is experimental kernel code developed by a single maintainer. There is no
stable release, no production security guarantee and no stable UAPI. Run it
only in a disposable VM with synthetic data. This policy describes how to report
a problem and what to expect; the longer-term plan is in
[docs/security-roadmap.md](docs/security-roadmap.md) (items S1 to S13, with implemented tests and remaining gaps recorded per item).

## Supported versions

| Version | Supported |
| --- | --- |
| `main` | Use this. Fixes land here only; there are no backports and no maintenance branches. |
| `v0.4.1-experimental` | Source snapshot of 2026-10-20, not supported: fixes F1 to F12 except fair shares of F2, but contains review 0144 R1/R2, F13 (review 0151) and F14 with the other defects of review round 2 (journal 0155); their corrections (journals 0149, 0150, 0152, 0156) are newer than this tag and land on `main` only. |
| `v0.4.0-experimental` | Source snapshot, not supported. Its queries are about twice as slow as they should be (a buffer allocated per query, journal 0139). |
| `v0.3.0-experimental` | Source snapshot, not supported. It carries defects fixed since, security ones included (findings F6 to F12 of ADR-0011; see `CHANGELOG.md`). |
| `v0.1.0-experimental`, `v0.2.0-experimental` | Source snapshots, not supported; they carry more of the same defects. |

## How to report a vulnerability

Report privately. Do **not** open a public issue, pull request or discussion.

1. Preferred: [GitHub Security Advisories](https://github.com/studiomoob/llmoobfs/security/advisories/new)
   (private vulnerability reporting for this repository).
2. Alternative: email **claudio@studiomoob.com**. For large or sensitive
   material, first ask by email how to transfer it.

Please include, as far as you can:

- the commit SHA you tested;
- the kernel version (`uname -r`) and whether it was the harness kernel
  (Debian `6.12.111+deb13-amd64` or `6.12.113+deb13-amd64`, or upstream 6.12.112 under KASAN)
  or another;
- the lower filesystem (ext4, btrfs, XFS, other) and the exact mount options
  (`lower=`, `nolinks`, `noindex`, `index_budget=`);
- the relevant `dmesg` excerpt (`BUG`, `WARNING`, `KASAN`, `UBSAN`, lockdep);
- a minimal reproducer (commands, a small `tests/*.c` change or a `vm/suite.sh`
  step), with synthetic data only: no real agent memories, credentials or
  personal data;
- the impact you believe it has (crash, hang, corruption, data loss, information
  disclosure across users, privilege escalation).

## What counts as a vulnerability

Within scope, any of the following triggerable by an unprivileged local user or
by a process with ordinary access to the mount:

- kernel crash, hang or warning (denial of service counts, as it does for
  in-tree filesystems);
- out-of-bounds access, use-after-free, uninitialized memory or any sanitizer
  report;
- information leak between users through `QUERY`, `GET_INFO` or attribute
  handling, including through counts or error codes;
- silent data loss or corruption on the lower filesystem;
- privilege escalation.

Mounting already requires `CAP_SYS_ADMIN` in the initial user namespace, so
behaviour that only a mounter can trigger is a bug, not an escalation, unless it
crosses into another user's data.

## Scope

In scope:

- the stacked layer module (`layer/`), including the `QUERY` and `GET_INFO`
  ioctls (`include/moobfs_ioctl.h`) and the typed-attribute validation;
- the shared attribute encoding and image parser (`common/`, `include/`);
- the user-space tools (`tools/`: `mkfs.moobfs`, `fsck.moobfs`, `moobfs-attr`,
  `moobfs-check`, `moobfs-index`);
- the VM harness (`vm/`) when a defect affects whoever runs it, for example
  writing outside `build/` or loading a module on the host;
- the read-only M1 module (`kernel/`), kept as a regression.

Out of scope:

- defects in the lower filesystems (ext4, btrfs, XFS), in the Debian or upstream
  kernel, in QEMU, Docker or virtme-ng that are unrelated to the layer; report
  them to those projects (for the kernel, follow
  `Documentation/process/security-bugs.rst`). If a MoobFS test exposes such a
  defect we will help with the report;
- attacks that require already having `CAP_SYS_ADMIN` on the host;
- the project website (`site/`) content, unless it serves active code.

## Response targets

These are targets for a single volunteer maintainer, not guarantees:

| Step | Target |
| --- | --- |
| Acknowledge the report | within 7 days |
| Initial assessment (valid, in scope, severity) | within 30 days |
| Status update to the reporter | at least every 30 days until closed |
| Fix | no fixed deadline; agreed with the reporter case by case |

## Coordinated disclosure

- The report stays private until a fix is in `main`. The reporter and the
  maintainer agree on the disclosure date; disclosure may happen earlier if the
  reporter agrees, or if the issue becomes public elsewhere.
- Since there are no supported releases (the `-experimental` tags are source snapshots), "fixed" means a commit on `main` with a test in
  `vm/suite.sh` or `tests/` that reproduces the problem and passes afterwards.
- When the defect affects anyone running the module, we request a CVE through
  GitHub's CNA from the advisory. No CVE is expected for test-only or
  documentation code.
- Credit is given in the advisory, in the journal entry (`docs/history/`) and in
  `CHANGELOG.md`, with the reporter's consent and in the form they choose.
- There is no bug bounty or other financial reward.

## Current boundaries

The [review of 2026-10-20](docs/history/0144-independent-review.md) found two defects beyond
ADR-0011: stale backing-file flags after `F_SETFL` (R1), and operational lower `getxattr`
errors skipped during indexing (R2). Both affect `v0.4.1-experimental`. Corrections are
on `main`. The full integrated VM suite and local
KASAN/UBSAN/lockdep run at `49098aa` passed, and the required pull-request
checks passed before that batch merged. R1 synchronizes mutable lower flags before delegated I/O
([0149](docs/history/0149-sync-backing-file-flags.md)). R2 propagates operational read errors
through the existing build/adoption failure paths, while preserving the exclusions for
missing, oversized or malformed attributes ([0150](docs/history/0150-propagate-index-xattr-read-errors.md)).
R1 has a VM reproduction and negative evidence, and both clearing and setting append passed
on ext4, btrfs and XFS in the integrated run. R2 has positive and negative tests injecting the
value-read result; its regression also passed in that full run together with the acceptance
and layer suites. The R2 test does not induce an actual lower-filesystem I/O fault. Consult
those journals for validation scope. The local batch sanitizer gate passed on Linux 6.12.112.

The parser checks image structure and hashes, but checksums are not
authentication. The module validates at mount, not continuously against external
backing-device writes. Neither encryption nor authorization beyond ordinary VFS
access is provided. `QUERY2` answers from the partition of a domain directory the
caller can traverse and read, holding only files every traverser can read; with
domains on, the v1 `QUERY` is limited to `CAP_SYS_ADMIN` (`docs/spec-query-v2.md`).
The VM suite, the KASAN/lockdep workflow and the attribute fuzzer are not a
security audit; no outside human audit has taken place. Two independent source reviews by AI
assistants (journals 0111 and 0114) found findings F9 to F12, fixed through private advisories. A third
(journal 0151, at `v0.4.1-experimental`) found that a chmod landing during the first build of a domain
left the file in the domain's `QUERY2` answers to callers who can no longer read it; reproduced in
the VM and fixed in journal 0152, with a suite test and a negative round. The threat and credential
model is [ADR-0011](docs/decisions/0011-threat-and-credential-model.md)
(accepted 2026-10-12): adversaries, promises, non-promises, unprivileged surfaces and the
findings F1 to F14, of which part of F2 (fair shares of the index budget) is open. [docs/security-roadmap.md](docs/security-roadmap.md) lists
fstests, syzkaller, hostile-lower handling and the other open items.

Known availability limit (issue 118): an authorized, unprivileged `QUERY2`
can build a whole domain with the mounter's credentials, again after a
directory rename. The mount's memory budget does not cap the CPU or lower
I/O of that scan. A bounded KVM probe measured the work
([journal 0182](docs/history/0182-query-build-resource-cost-investigation.md)).
The maintainer chose the resource-limit direction; `main` includes
experimental work and rebuild-frequency limits
([ADR-0019](docs/decisions/0019-bound-query-build-work-and-frequency.md))
and a per-mount `index_build_work` choice of 1,024–65,536 units, default
32,768 ([ADR-0020](docs/decisions/0020-configurable-query-build-work-per-mount.md),
[PR 138](https://github.com/studiomoob/llmoobfs/pull/138)).
They do not strictly cap CPU time or physical I/O, nor provide fair index
memory shares. The default still denies the measured valid 17,000-file
domain with `E2BIG`; 65,536 completed that fixture in a dedicated VM.
A later change stops a budget release from dropping a domain
last built by another filesystem uid: the query returns `ENOMEM` when the
remainder is too small, and one uid can still fill the whole budget
([ADR-0021](docs/decisions/0021-budget-release-keeps-the-builders-domain.md),
[journal 0192](docs/history/0192-same-fsuid-budget-release.md)).
That host regression passed, and the opt-in native-KVM probe passed on the
dedicated host. The steps `release`, `overbudget` and `buildcostevict`
also passed there under KASAN, UBSAN and lockdep. The complete KASAN, UBSAN
and lockdep suite of this change then exited 0 (`MOOBFS_KASAN_PASS`). The
complete native-KVM suite of this change was not run. Local VM and KASAN
suites and all eleven PR checks passed for the earlier guard.
Issue 118 remains open for a real slow lower device, a large
single-project client and broader-workload validation. Avoid untrusted
multi-user query workloads while this issue is open.

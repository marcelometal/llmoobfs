# 0192 — Budget release stays with the builder's filesystem uid

Date: 2026-10-25. Status: host regression passed, the opt-in native-KVM
probe passed, the steps `release`, `overbudget` and `buildcostevict`
passed under KASAN, and the complete KASAN, UBSAN and lockdep suite then
exited 0 (`MOOBFS_KASAN_PASS`). G2 is not complete.
Work of the recorded host runs: 05:56 to 06:02 America/Sao_Paulo. Wall-clock
of those runs, not an engineering-effort measurement. ADR-0021's estimate
is M. The G2 roadmap estimate remains L.
The records are numbered 0188–0192 and ADR-0021. Main already has journals
0184–0187 and ADR-0020.

## Goal

Stop a query from releasing a domain built by another filesystem uid when
the shared `index_budget` is too small for both, and return `ENOMEM` with
no result list. Keep the same-uid release, so one uid can still fill the
budget. Do not reserve a fraction. Do not set a latency ceiling.

## Decision

[ADR-0021](../decisions/0021-budget-release-keeps-the-builders-domain.md).
A budget release may drop only a domain last built by the same filesystem
uid (`current_fsuid()` sampled before `override_creds()`). The shrinker
may still drop any idle domain. Rejected: a reserved share, which would
contradict the property in protocol section 9; the old cross-uid release,
which journal 0191 recorded; keying the filter on the real uid.

The first build of the filter used `current_uid()`. The client harness
calls `setresuid(0, uid, 0)`, so the real uid stays 0 for both users. That
build still released the first domain. The filter was then changed to the
filesystem uid. `build_admit()` still counts the real uid, as ADR-0019
implemented.

## What ran

Dedicated Debian host, kernel `6.12.111+deb13-amd64`, ext4,
`index_budget=1M` (`1048576`), `domain_depth=1`, 2 000 files per domain,
four scalars each. `bench/client/g2_share.py`. The module was rebuilt with
`make layer` against that kernel's headers. The compiler log showed no
warning.

Negative round, the module from journals 0189–0191, before this rebuild.
Exit 2. After uid 1001: count 2000, `domain_builds=1`,
`domain_releases=0`, `index_used=840600`. After uid 1002: count 2000,
errno none, `domain_builds=2`, `domain_releases=1`, `domains_built=1`,
`index_used=836600`. The script printed `G2_SHARE_FAIL` because uid 1002
released uid 1001's domain. It stopped before the same-uid half.

Positive round, after the filesystem-uid filter. Exit 0,
`G2_SHARE_PASS`, about 78 seconds including publication. That duration is
not a latency result.

- after uid 1001: count 2000, errno none, `domain_builds=1`,
  `domain_releases=0`, `index_used=840600`;
- after uid 1002: no count, errno 12 (`ENOMEM`), `domain_builds=1`,
  `domain_releases=0`, `domains_built=1`, `index_used=840600`;
- uid 1001 again: count 2000, errno none, `domain_builds=1`,
  `domain_releases=0`;
- same filesystem uid, two domains: both searches returned 2000,
  `domain_releases` went from 0 to 1 and `domain_builds` from 1 to 2.

`scripts/checkpatch.sh` (pinned `checkpatch.pl` from Linux v6.12.112)
exited 0 on the tracked `layer/` and `kernel/` sources.

After the pass the drill mounts were gone. The module was unloaded. The
lower trees `g2-0188-cross` and `g2-0188-same` remain on the host's ext4
test volume, next to the `g2-0187` trees. Users alice (1001), bob (1002)
and group moobteam remain.

## Follow-up, later the same morning

The opt-in guest step ran. Wall-clock of the two guests, about 06:12 to
06:14 America/Sao_Paulo, not an engineering-effort measurement. Each guest
was native KVM, `MOOB_ONLY=buildcostevict`, Debian kernel 6.12.111, two
vCPUs, in a separate directory from a full suite that was already running
on the same host. The probe was niced and left that suite running.
`make vm-test` (the container harness) was not used.

Positive module, script exit 0. The guest printed
`QUERY_BUILD_DENIED errno=ENOMEM phase=second-user`, then
`BUILD_COST_CROSS_USER_HELD: uid 1002 got ENOMEM; uid 1001 stayed built; budget=1762216`,
then `MOOBFS_ONLY_PASS: steps buildcostevict (not a complete run)`.
The following query by uid 1001 took 75496 ns. The assertions that
`domain_builds` did not increase and `domain_releases` stayed 0 are what
allow that line to be printed.

Negative round, the same guest step against `layer/index.c` and
`layer/moobl.h` from `HEAD` (the module before this filter). Exit 1.
The helper printed `QUERY2 succeeded, expected ENOMEM` and the guest
printed `MOOBFS_VM_FAIL: bce-second-user`. The passing sources were
restored afterward and the layer was rebuilt.

The first harness attempt stopped before the guest: the generated script
still created its stage directory under `/work`. That is a copy error,
not a module result. The repeat used the directory of this probe.

## Follow-up, KASAN the same morning

The Debian full suite in the other tree finished with `MOOBFS_VM_PASS`,
`MOOBFS_LAYER_PASS` and `MOOBFS_ACCEPTANCE_PASS`. This probe then ran in
its own directory. virtme-ng 1.41 was installed in a venv there. The
pinned KASAN kernel `6.12.112` was already built in the host cache.
`MOOB_ONLY="release overbudget buildcostevict" sh vm/kasan-local.sh`
exited 0. Wall-clock of that guest, about 06:27 to 06:49
America/Sao_Paulo, not an engineering-effort measurement. A full KASAN
suite of the other tree was also running on the same host; this run did
not stop it.

The guest printed `MOOBFS_KASAN_ONLY_PASS: steps release overbudget
buildcostevict under KASAN, UBSAN and lockdep with no kernel diagnostics
(not a complete run)`. `RELEASE_PASS` is intact for ext4 and XFS. The
btrfs summary line in the console is garbled
(`queUNPRIV_RELEASE_PASS`); the step continued through XFS, so the suite
did not treat btrfs as a failure. `OVERBUDGET_PASS` is intact for ext4,
btrfs and XFS. `buildcostevict` printed `QUERY_BUILD_DENIED errno=ENOMEM
phase=second-user` and `BUILD_COST_CROSS_USER_HELD` with budget 1762216.
Uid 1001's following query took 323043 ns.

## Complete KASAN suite

After the other tree's guest had left the host, `sh vm/kasan-local.sh`
ran with `MOOB_ONLY` empty on the same dedicated host. The tree matched
this commit's layer sources. Kernel 6.12.112, virtme-ng 1.41, two vCPUs
and 3 GiB. Wall-clock about 07:35 to 08:32 America/Sao_Paulo, not an
engineering-effort measurement. The script exited 0. The console records
`MOOBFS_KASAN_PASS`, `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS` and
`FAULT_INJECTION_PASS: operations=22 calls=324 faults=302 unexpected=0`.
`RELEASE_PASS` and `OVERBUDGET_PASS` are intact for ext4, btrfs and XFS,
including a clean btrfs `RELEASE_PASS` line. No `MOOBFS_VM_FAIL`.
`buildcostevict` is not part of this suite. No qemu guest was left running.

## What did not run

The complete native-KVM suite and a latency sample were not run for this
change. The change does not take a new lock or reorder the existing ones.

## Next step

G2 stays open: the latency ceiling is unset, and a reserved share is not
this decision. The local complete KASAN suite passed. The pull-request
KASAN workflow remains the merge gate, and the change is not published.
G4, G5 and the G6 final evaluation stay unbound.

# 0187 — Per-mount query-build work policy

Date: 2026-10-25. Status: implementation merged by PR 138; issue 118 open.
Work started and ended: 2026-10-25. Labor time was not measured.

## Goal and decision

After PRs 136 and 137 merged the measurements and their publication
record, the maintainer asked to continue the issue-118 numerical-policy
work. The established 32,768-unit default denies a valid 17,000-file
domain very late in its build on the measured host. The real client
workload passed only for much smaller per-project domains, so it does not
justify raising the default for every mount. ADR-0020 proposes a bounded
`index_build_work` mount option, 1,024–65,536 units, with the existing
32,768-unit default and all other guards unchanged. The maintainer approved
this compatibility decision for PR on 2026-10-25 after local VM validation.

## Initial plan and evidence boundary

The implementation will store the choice in the per-mount index, use it
at both name enumeration and other work charges, display a non-default
option, and compare it on reconfiguration. A dedicated-VM regression will
test malformed mount options, the default denial, successful opt-in on
17,000 attributed files, a smaller configured allowance, remount rules,
and unchanged error/output behavior. The production module will be built
against the exact Debian 6.12.111 host headers. No real slow device or
single 17,000-file ai-memory project was tested in journals 0185–0186;
this round must not claim either result.

The exact host's common header defines `fsparam_u32`, `fs_parse` and
`struct fs_parse_result` under
`/usr/src/linux-headers-6.12.111+deb13-common/include/linux/fs_parser.h`.
The configured build directory is
`/usr/src/linux-headers-6.12.111+deb13-amd64`. This was read before
changing the mount parser; it does not by itself validate the new code.

## First implementation round — 2026-10-25

The mount parser now accepts a bounded `index_build_work` unsigned value,
stores it in the per-mount index, shows a non-default value, and compares
it on new mount-API reconfiguration. The scan job captures that immutable
allowance and uses it for both name enumeration and other work charges,
including later batches, retries and moved-directory scans. The default
remains 32,768. `tests/reconf.c` checks same-value acceptance and a
changed-value refusal. The opt-in `MOOB_ONLY=buildwork` guest step plants
17,000 attributed files, tests invalid values and both bounds, and checks
default denial versus an opted-in full answer. `git diff --check`,
`sh -n vm/suite.sh` and `make checkpatch` exited zero. Module compilation
and the guest regression remain pending; no pass is claimed for them yet.

## Dedicated-host validation round — 2026-10-25

The clean dedicated-host checkout was at `51a46d3` with no QEMU process.
A Git bundle requiring that commit and `2396c16` was SHA-256 verified on
both machines (`23fd9ea7eeea4a91c4d3ba436b7183e94f69602b0dc786a38d1f9f62ef6f4205`)
and a single host checkout switched to branch commit `a68a827` without a
public push. `make layer` against the exact `6.12.111+deb13-amd64`
headers and static builds of `tests/reconf.c` and
`tests/query-build-limit.c` exited zero. The host checkout stayed clean.

The host's earlier native-KVM wrapper reused fixed `build/vm` image names.
To preserve prior evidence, a fresh Git archive of `a68a827` was unpacked
under `build/issue118-build-work-0187/source`, outside the tracked files.
A private copy of the wrapper was changed only to `cd` into that export,
put its temporary stage there and use a 256 MiB ext4 disk rather than
64 MiB for 17,000 attributed files. `sh -n` passed on that private copy.
`MOOB_ONLY=buildwork` then exited zero and emitted
`BUILD_WORK_OPTION_PASS` plus `MOOBFS_ONLY_PASS`. Its assertions covered
invalid option values, default and minimum denials with untouched query
output, a full answer at 65,536, mount-option display and the new mount-API
remount contract. This is a targeted opt-in step, not a complete suite.

An initial attempt to create a second isolated export for the full suite
failed at `mkdir` because its parent directory did not exist; it created
no VM or test image. Creating that previously absent parent fixed setup.
The full suite was then started from a distinct Git archive and private
wrapper, preserving the targeted image and log. It exited zero with
`MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS` and `MOOBFS_LAYER_PASS` on
Debian 6.12.111. This full run exercises the unchanged default policy;
the new upper-bound path is exercised by the opt-in step above. The full
KASAN/UBSAN/lockdep run was started from a third isolated Git export and
passed on the pinned upstream 6.12.112 kernel. `sh vm/kasan-local.sh`
exited zero and emitted `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`
and `MOOBFS_KASAN_PASS` with no kernel diagnostics. The cached kernel
configuration matched `vm/kasan.config`; the virtual machine ran with
KVM. Another VM was active on the host for part of this run, so elapsed
time is not used as performance evidence. The full KASAN suite used the
default work limit; the 65,536-unit path was covered by the native opt-in
run above.

## Result and next step

The implementation is locally validated at commit `a68a827` and remains
on branch `codex/issue118-mount-work-policy-0187`. The measured
17,000-file attributed domain completes only when the administrator opts
into the higher work allowance; existing mounts retain 32,768. No disk
format or ioctl ABI changed. The actual cost on a real slow lower device
and one real 17,000-file client project remain untested. The upper bound
is an operator policy choice, not a universal resource guarantee; the
ten-second cooperative deadline and admission limits still apply.
The maintainer explicitly approved the range/default choice and push/PR
on 2026-10-25. `make test` then exited zero on macOS: 60 attribute checks
and nine image tests passed. Push the branch, open the PR and run its
required CI. Issue 118 stays open for the broader evidence. No push or PR
had occurred when this journal update was committed.

## Post-merge status, 2026-10-25

PR 138's exact head `6621b21` passed twelve reported checks, including
Native Linux VM and KASAN/lockdep, then merged as `b7ae1b8`. The merge
tree equals that validated head. The preceding sections preserve the
pre-publication checkpoint; journal 0193 records the current-state
reconciliation. Issue 118 remains open for the untested lower-device and
single-project client shapes.

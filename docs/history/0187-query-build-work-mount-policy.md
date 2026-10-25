# 0187 — Per-mount query-build work policy

Date: 2026-10-25. Status: investigation and implementation in progress.
Work started: 2026-10-25. End and labor time: not yet recorded.

## Goal and decision

After PRs 136 and 137 merged the measurements and their publication
record, the maintainer asked to continue the issue-118 numerical-policy
work. The established 32,768-unit default denies a valid 17,000-file
domain very late in its build on the measured host. The real client
workload passed only for much smaller per-project domains, so it does not
justify raising the default for every mount. ADR-0020 proposes a bounded
`index_build_work` mount option, 1,024–65,536 units, with the existing
32,768-unit default and all other guards unchanged. This is a proposed
compatibility decision until implementation and VM validation complete.

## Plan and evidence boundary

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

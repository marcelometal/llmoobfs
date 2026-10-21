# 0151 — Source review of d110d4e

Date: 2026-10-20. Status: complete.

## Scope

Review exactly `d110d4e63e18eb009ef385ff0dc7856c0393ef14`
(`v0.4.1-experimental`): credentials, index concurrency and lifetime, QUERY2
cursors and per-open buffers, ioctl input, typed attributes, stacking edges,
and separately design objections. Exclude ADR-0011 F1–F12, its non-promises,
the known lists in issues 16/17, and reviews 0111/0114.

The shared checkout is not modified. This dedicated worktree starts at the
target commit; module sources remain unchanged. Later local findings 0144 R1
(mutable file flags) and R2 (ignored xattr read failures), and corrections
0149/0150, are treated as existing work, not new discoveries.

## Investigation

Read the complete layer implementation, shared UAPI and attribute validator,
the threat model, index construction/reclamation decisions, relevant prior
reviews, and the two issue bodies. A candidate race in `index_load()` repeats
attribute reads after `ld.dirty`, but retains the caller-supplied homogeneity
placement. A regular-file chmod during a build may therefore be overwritten
by the scan. This is confirmed: in `layer/index.c`, `scan_file()` computes
`fpl.hetero` before `index_load()` (`1300–1303`); `index_load()` observes the
permission update through `ld.dirty`, rereads the attributes, but stores the
unchanged `pl` (`1121–1130`). For a permission change, `moobl_index_perm_changed()`
marks the loader dirty and finds no index record yet (`2085–2089`). The build
then stores the old homogeneous classification. A subsequent `QUERY2` uses
the built homogeneous partition without per-result permission checks.

The VM reproduction was run in the project test container and disposable QEMU
guest. Control: complete the build, revoke mode to `0600`, then query as uid
1001; the indexed equality query returns zero and `getxattr` fails with
`EACCES` (`errno=13`). Race: set `scan_delay_us=1000000`, let uid 1001 start a
first query on a fresh domain, then chmod to `0600` and change the typed value
while the scan sleeps. The trace records `moobfs_scan_reread`; the later
equality query still returns the file (`after=1`) although `getxattr` fails
with `EACCES`. The focused run ends `REVIEW_VM_DONE`. Reproduce with
`docker run --rm --platform linux/amd64 -v "$PWD:/work" moobfs-m1-test sh tests/review-d110d4e-vm.sh`.

No new finding was confirmed in the remaining requested areas: delegated
operations' credential overrides and placement of permission decisions; index
locks, domain build and release; QUERY2 resume keys and per-open buffers;
ioctl bounds, reserved fields, pointer copies, cursor MAC and output clearing;
typed attribute validation; and the listed stacking and rename cases. This
review therefore reports one new finding. Findings already recorded after the
tag in journals 0144 and 0149–0150 remain excluded as prior work.

## Validation and limitations

The focused VM used Debian kernel `6.12.111+deb13-amd64`; the project-local
Linux 6.12.112 source extracts were consulted for kernel-dependent review
questions. The reported race is in module state and was directly observed in
the VM; its conclusion does not rely on an unchecked 6.12 kernel behavior.
No module fix, push, PR, remote deployment or publication was made. The two
standalone review harness files preserve the reproduction; the normal VM suite
was restored after its temporary one-off step.

## Result

One new security finding is reported below, with source lines from the exact
target commit and a disposable-VM reproduction. No standalone design opinion
was strong enough to report; the finding is an implementation defect in
QUERY2's security contract, not an objection to keeping this filesystem in
kernel space.

## Finding

### F1 — A chmod during domain construction leaves a stale homogeneous entry

- Type: security.
- Severity: high: after read permission is revoked, a query can continue to
  disclose the file's name and matching typed-attribute result to a user who
  cannot open the file.
- Location at `d110d4e`: `layer/index.c:1300–1303`, `1121–1130`, and
  `2085–2089`.
- Confidence: high; source path and the race/control were reproduced in the
  disposable VM.
- Interleaving: CPU 0 computes `fpl.hetero=false` for a readable file and
  starts `index_load()`. CPU 1 chmods it to `0600`; the permission hook marks
  the active loader dirty before an index record exists. CPU 0 rereads the
  attributes, clears `dirty`, and inserts them using the stale `pl`. The
  domain becomes built with a homogeneous entry. A later uid-1001 `QUERY2`
  returns the name and match while direct `getxattr` is denied.
- Reproduction: run `tests/review-d110d4e-vm.sh` through the project test
  container. The control reports `after=0`; the race reports `after=1`,
  `errno=13`, and `moobfs_scan_reread`.

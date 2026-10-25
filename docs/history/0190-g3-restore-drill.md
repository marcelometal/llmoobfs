# 0190 — G3 identity-map restore drill passed

Date: 2026-10-24. Status: the registered drill passed. A non-identity UID map
and the other filesystems are not part of this pass.
Work started: 2026-10-24 18:52 America/Sao_Paulo, after the G1 mounted gate.
The drill process, including the host build of `moobfs-check`, finished at
18:54. Wall-clock time, not an engineering-effort measurement. The roadmap
estimate for G3 remains M. This run does not close every recovery scenario
in the distribution strategy.

## Goal

Run the G3 bounds in
[the protocol](../g6-evaluation-protocol.md) on the dedicated Debian host.
Copy the lower tree with the existing `rsync -aHAX --numeric-ids` command.
Do not add an archive format and do not change `layer/` or the UAPI.

## What ran

Host: Debian, kernel `6.12.111+deb13-amd64`, the module built earlier the
same evening for the G1 gate. `moobfs-check` was compiled on that host with
`make attr-tools`. The source was the G1 ext4 lower, still populated: 7 022
files, which is the 7 000-file corpus minus the ten keys uid 1002 deleted,
plus `salary.json`, thirty concurrent `team` files and one publication
temporary left by the prefix test. The restore added one sentinel after the
comparison, so the destination then held 7 023 files.

`bench/client/g3_restore.py` refused to copy a mount and refused a
destination that already had an entry. The copy itself is
`rsync -aHAX --numeric-ids`. The drill removed only directories whose names
contain `g3-0186`, then called that script.

The process printed `G3_MOUNTED_PASS` and exited 0, in this order:

- `G3_RESTORE_OK`
- `G3_PLAIN cat and getfattr read the restored tree`
- `G3_QUERY2 answers and salary denials match`
- `G3_CHECK illegal type byte exit 2`
- `G3_FORMAT marker 2 refused`
- `G3_SECOND_RUN refused`

The comparison covered file bytes, every `user.moob.*` value, POSIX ACL
attributes when present, and numeric owner, group and mode, for every path
below the lower root. `QUERY2` answers for uid 1001 and uid 1002 on the
three domains matched the pre-restore set, and `salary.json` stayed absent
for both. `moobfs-check` on the restored tree exited 0. A separate tree with
one attribute whose type byte was 9 made `moobfs-check` exit 2. A lower root
whose `user.moob.format` was the decimal text `2` made `mount` fail. The
second run exited 2 before `rsync`, and the sentinel and a sampled file were
unchanged.

After the pass the G1 and G3 mounts were not present. `rmmod moobfs_layer`
succeeded. The lower trees and the users `alice`, `bob` and the group
`moobteam` remain on the host.

## What did not run

No btrfs or XFS copy. No UID/GID rewrite. No comparison against
`moobfs-index`. No VM, KASAN or benchmark. No new format. The script does
not repair a tree; `moobfs-check` still only reports.

## Changes

`bench/client/g3_restore.py`, `bench/client/g3_mounted.py`, this journal,
the history index, the changelog, the roadmap status, the protocol's G3
note, the distribution-strategy restore bullet, and follow-ups on journals
0188, 0189 and 0165.

## Next step

G2 stays on the S9 track. G4, G5 and the G6 final evaluation stay unbound
until a dated amendment sets their ceilings before a run.

## Follow-up, 2026-10-24 evening

Journal [0191](0191-g2-eviction-baseline.md) recorded the cross-user
release of that module. It does not implement a share and does not pass G2.

Follow-up, 2026-10-25: journal [0192](0192-same-fsuid-budget-release.md)
stopped that release on the host. G2 stays open.

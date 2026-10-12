# 0069 — Two review findings fixed: the set-user-ID crash and the mounter's capability

Date: 2026-10-12. Status: implemented; `make vm-test` passes and both new tests fail without their
fix; local KASAN **not run yet**. Local series.

## Goal

Fix the two code findings of the review in journal 0066 (`docs/research-stacked-fs-kernel-rules.md`),
R-CRED-3 and R-CRED-2, and document the third, R-INODE-4. Each finding was checked against the code
and the kernel v6.12 sources before any change.

## R-CRED-3: a write to a set-user-ID file crashed the kernel

Confirmed. `moobl_write_iter` calls `file_remove_privs`; for a caller without `CAP_FSETID` the VFS
then calls `->setattr` with `ATTR_KILL_SUID`/`ATTR_KILL_SGID` and the `ATTR_MODE` it derived from
them (`fs/attr.c:446-460`). `moobl_setattr` copied that request to `notify_change` on the lower
inode, which refuses the pair with `BUG()` (`fs/attr.c:442-444`).

Wider than reported: the same request arrives through `truncate` and `ftruncate`, and through
`chown` of such a file for **every** caller, root included, because `chown_common` always asks to
clear set-user-ID on a non-directory.

Fix: clear `ATTR_MODE` when either kill flag is set, before the lower call, which derives the mode
again; the pattern of `fs/overlayfs/inode.c:55-56`. Recorded as F3 in ADR-0011 (promise 1).

## R-CRED-2: the mounter credentials kept `CAP_SYS_RESOURCE`

Confirmed in part, and the review's remedy does less than it said. `moobl_fill_super` now lowers
the capability in the credentials used for delegated work, as `fs/overlayfs/super.c:1442` does.
That closes the quota hard limit (`fs/quota/dquot.c:1285`) and the reserve of a lower whose
reserve owner is not root.

It does **not** close the case the review reproduced. ext4 grants its reserve when the caller's
fsuid equals the reserve owner or the caller has the capability (`fs/ext4/balloc.c:650-652`). The
mounter credentials have fsuid 0 and the default reserve owner is uid 0, so an ordinary user
writing through the layer still spends the reserve. overlayfs with a root mounter has the same
residue. Recorded as F4 in ADR-0011: promise 2 does not hold for this case.

Alternative considered: run delegated writes with the caller's fsuid. Rejected for this step: it
changes the credential model of ADR-0011 (data operations use the mounter's credentials after the
caller's open was authorized) and needs its own decision.

## R-INODE-4: inode numbers repeat across btrfs subvolumes

Documentation only, as the review recommended: a limitation in `README.md` and `README.pt-BR.md`.
No mapped inode space is proposed.

## Changes

- `layer/main.c`: the guard in `moobl_setattr`; `cap_lower` in `moobl_fill_super`.
- `tests/layer.c`: as uid 1001, a write and an `ftruncate` on a 06755 file, then a `chown` of it;
  the mode must become 0755 on the layer and on the lower. New mode `layer reserve`: an ordinary
  user fills the layer until `ENOSPC` and at least three quarters of the lower's reserve must
  remain free.
- `vm/suite.sh`: runs `layer reserve` on ext4 mounted with `resuid=4242`, where only the capability
  could spend the reserve.
- Dated notes in the review document (R-CRED-2 corrected, R-CRED-3 and R-INODE-4 closed), in
  ADR-0011 (F3, F4) and in `docs/adoption-risks.md`, whose "no residual once fixed" for R-CRED-2
  was wrong.

## Evidence

- `make checkpatch`: clean.
- `make vm-test` with both fixes: `LAYER_PASSTHROUGH_TESTS_PASS` six times (ext4, btrfs, XFS, with
  and without hard links), `LAYER_RESERVE_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`, no
  kernel diagnostic.
- Same tree without the `setattr` guard: `kernel BUG at fs/attr.c:444!`, call trace
  `moobl_setattr ← notify_change ← moobl_write_iter`, then `FAIL` at the new check and
  `MOOBFS_VM_FAIL: layer-tests-ext4`.
- Same tree without `cap_lower`: `MOOBFS_VM_FAIL: rs-reserve` (the user's fill left less than three
  quarters of the reserve free).

The three runs used the Debian 6.12.111 kernel in QEMU, in a container on the development machine.

## Limitations

- KASAN and lockdep (`vm/kasan-local.sh`) have not run on this change; the benchmark host was busy
  with the stage 2 scale. Required before the series is published.
- The reserve test covers ext4 with a non-root reserve owner only. The quota case is argued from
  the source, not tested: the VM image has no quota tools.
- The default-`resuid` reserve case is open (F4).
- Other entry points that could pass a forbidden attribute pair to the lower were not searched
  beyond `setattr`; syzkaller (S3) is the systematic answer.

## Next step

Local KASAN on this tree once the benchmark host is free; then S4 and S2, which exercise promises 1
and 2 systematically.

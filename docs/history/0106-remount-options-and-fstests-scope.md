# 0106 — Remount: options refused through the new mount API; the fstests port stays local

Date: 2026-10-14. Status: implemented; tested in the local VM (results below).

## Remount

The review of journal 0105 said that a remount with new options was accepted and ignored. Read
again before changing anything: `reconfigure_super()` (`fs/super.c`, Linux 6.12.112) returns
success when the filesystem has no `reconfigure` operation, and the layer had none, so the
options were parsed into a fresh context and dropped. overlayfs (`ovl_parse_param()` in
`fs/overlayfs/params.c`) does the same on purpose for the old `mount(2)` API, which passes the
current options again on every remount, and refuses any option through the new API ("No changes
allowed in reconfigure"). So the old behaviour matched overlayfs for `mount(2)`; it differed only
for the new API, where a change was accepted and dropped.

- `moobl_parse_param()` follows overlayfs: on reconfigure, options are ignored through the old
  API and refused through the new one. Read-only and read-write remounts are the VFS's and work
  as before.
- `tests/reconf.c` (new): `fspick()` and `fsconfig(FSCONFIG_SET_STRING, "index_budget")` on a
  layer mount must fail with `EINVAL`.
- `vm/suite.sh`, per lower filesystem: `remount,ro` refuses a write, `remount,rw` allows it
  again, the change through the new API is refused, and `/proc/mounts` shows no new option.
  Until now no remount of the layer was tested at all, only unmount and mount.

## fstests and out-of-tree filesystems

An fstests maintainer answered a note of thanks: fstests does not add out-of-tree filesystems to
its upstream support matrix; keeping a fork for an out-of-tree project is fine; general fixes to
`fsx` or the replay tools are welcome as patches on the fstests list. Consequence: the port in
`vm/fstests/moobfs_layer.patch` stays in this repository, and a fix this project makes to the
vendored `fsx` or `replay-log` goes upstream as a patch.

## Results

- `MOOB_ONLY=ext4 make vm-test` (Debian 6.12.111, QEMU): pass, with the new remount checks.
- Negative run, the reconfigure check disabled: fails at
  `remount-fsconfig-change-accepted-ext4`.
- `sh scripts/checkpatch.sh`: exit 0; shellcheck at the CI's severity: clean.

## Limitations

- What libmount sends on `mount -o remount,ro` was not traced; the remount checks pass with the
  guest's `mount`.
- The suite runs the new checks on the three lower filesystems in a complete run; only ext4 was
  run for this journal. KASAN: with the next batch. Note of 2026-10-14: the complete run of journal
  0108 passed them on ext4, btrfs and XFS.

## Note of 2026-10-14: the KASAN job failed, and why

The KASAN workflow of pull request 71 failed at `remount-fsconfig-change-accepted-ext4`. The
KASAN harness (`.github/workflows/kasan.yml` and `vm/kasan-local.sh`) builds the test binaries
from its own list, and `tests/reconf.c` (this journal) and `tests/sqlq.c` (journal 0108) had been
added only to the local VM's build (`vm/in-container.sh`): in the KASAN guest the check ran a
program that did not exist, and the failure read as "accepted". Not a defect of the module. Both
are now in the two lists. The test binaries are listed in three places; one list shared by the
three is a cleanup left open.

## Next step

The KASAN batch with journals 0105 and 0106.

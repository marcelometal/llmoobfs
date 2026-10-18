# 0118 — Remount with util-linux 2.41: options repeated with their current value are accepted

Date: 2026-10-15 to 2026-10-17. Started 2026-10-15 06:30, ended 2026-10-17 17:50 (calendar time,
local; the work stopped for about two days on a usage limit). Status: fixed and checked by hand in
the KASAN guest; the suite step and checkpatch still to run (see Results). Estimated effort: S.

## Finding

The KASAN harness on the benchmark host (Debian trixie, util-linux 2.41.5, kernel 6.12.112)
failed at `MOOBFS_VM_FAIL: remount-ro-ext4`: `mount -o remount,ro` of the layer returned 32 with
`fsconfig() failed: moobfs_layer: No changes allowed in reconfigure`. The GitHub runner and the
busybox harness (`make vm-test`) passed the same check.

Traced in a guest with `LIBMOUNT_DEBUG=hook,cxt,optlist` (no strace on the host): libmount
applies the mount's mountinfo line to the request ("trying to apply mountinfo"), opens the
superblock with `fspick`, and sends every filesystem option of that line again with `fsconfig`
before `FSCONFIG_CMD_RECONFIGURE`:

    syscall 'fspick' [success]
    fsconfig(name="lower" value="/tmp/lo")
    syscall 'fsconfig' [failed: Invalid argument]
    mesg: 'e moobfs_layer: No changes allowed in reconfigure'

This is `hook_reconfigure_mount()` and `configure_superblock()` in `libmount/src/hook_mount.c`
(read at v2.41.2 and v2.39.3; both versions send the options in this way). Journal 0106 made the
layer refuse every option on reconfigure through the new API, so any `mount -o remount` with a
libmount that uses the new API failed. Why the runner's util-linux 2.39 passed was not verified:
its libmount may be built without the new-API mount path.

## Decision

An option given on reconfigure through the new mount API is accepted when it equals the
mounted value and refused with `EINVAL` and a message naming it when it differs. Prior art:
NFS, which compares the options of a remount with the mounted ones and refuses a difference
(`nfs_compare_remount_data()`, `fs/nfs/super.c`, Linux 6.12.112), because mount programs pass
every option again. `lower=` is compared as the directory it resolves to (`kern_path`, then
`path_equal` with the mounted lower), since mountinfo prints it with `seq_path`; `nolinks` and
`noindex` are the same only when the mount has them; `index_budget`, `domain_depth` and
`index_idle` are compared as parsed values, after the same range checks as at mount.

- **Kept from journal 0106:** a change is never accepted and dropped; through the old `mount(2)`
  API options on remount are still ignored, as overlayfs does.
- **Alternative rejected:** ignore every option on reconfigure, as through the old API; it would
  accept a change and drop it, what journal 0106 removed.
- **Compatibility:** no format change; `mount -o remount,ro|rw` works with libmount's new-API
  path; a request that changed an option kept failing and still fails, now with
  "<option> cannot be changed on remount" instead of "No changes allowed in reconfigure".

## Changes

- `layer/main.c`: `moobl_reconf_same()`; `moobl_parse_param()` parses the option on
  reconfigure too and then compares it.
- `tests/reconf.c`: four changes refused (`index_budget=64M`, `nolinks`, `lower=/`,
  `domain_depth=7`); the current `lower`, `domain_depth` and `index_budget=128M` accepted, each
  followed by `FSCONFIG_CMD_RECONFIGURE`. Now `reconf <layer> <lower> <domain_depth>`.
- `vm/suite.sh`: calls it with the lower and `domain_depth=0` of that mount.

## Results

- Before the fix, guest of the KASAN kernel on the benchmark host, util-linux 2.41.5:
  `mount -o remount,ro` → 32, `No changes allowed in reconfigure` (the trace above).
- With the fix, same guest: `remount,ro` → 0 and `touch` refused; `remount,rw` → 0 and `touch`
  works; `mount -o remount,index_budget=64M` → 32, "index_budget cannot be changed on remount";
  `reconf /tmp/la /tmp/lo 0` → `RECONF_PASS`.
- Not executed: `MOOB_ONLY=ext4 sh vm/kasan-local.sh` (the suite step with the new `reconf`), and
  `sh scripts/checkpatch.sh` and shellcheck on this change; the work stopped before them and the
  benchmark host was not reachable when it resumed. They run with the batch.

## Limitations

- A `lower=` that mountinfo prints unreachable from the caller's namespace (a lower outside the
  caller's root) does not resolve to the mounted directory and is refused as a change, so
  `mount -o remount` from such a namespace still fails; a remount with the old API works.
- Only ext4 was run for this journal; btrfs and XFS take the same check in a complete run.
- The util-linux of the GitHub runner was not inspected.

## Next step

The KASAN batch of the pull request with this change.

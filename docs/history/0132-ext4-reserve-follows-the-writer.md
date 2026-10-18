# 0132 — ext4's reserve is granted as to the writer directly (finding F4 completed)

Date: 2026-10-18. Status: in progress. The change is committed; the KASAN runs (the change, and
the negative round on `main`) are dispatched. Estimated effort: S.

## Why

Finding F4 of ADR-0011 stayed partly open since journal 0069. The mounter credentials lost
`CAP_SYS_RESOURCE`, but ext4 also grants its reserved blocks to the task whose fsuid is the
reserve owner (`ext4_has_free_clusters()`, `fs/ext4/balloc.c:650` in 6.12.112), uid 0 by
default. Data writes run with the mounter's credentials, so an ordinary user writing through the
layer could spend the reserve meant for root, against promise 2. The maintainer approved the fix
on 2026-10-18, as a correction of a written promise, not a feature.

## Prior art

overlayfs lowers `CAP_SYS_RESOURCE` with the comment "Never override disk quota limits or use
reserved space" (`fs/overlayfs/super.c`), and has the same gap with a root mounter and the
default `resuid`. The technique used here is overlayfs's own for creations
(`ovl_create_or_link()`). It switches to the mounter's credentials, calls `prepare_creds()`, then
sets fsuid and fsgid.

## Change

- `layer/main.c`: an open file of the layer is a `struct moobl_file`, holding the lower file and
  the last write credentials built.
  - `moobl_wcred_get()` returns the mounter's credentials when the writing task has the
    mounter's fsuid, fsgid and groups. Otherwise it returns a copy of them with the writer's
    fsuid, fsgid and groups, kept on the file while the same identity writes.
  - These credentials are used by `write_iter`, `splice_write`, `fallocate`, clone and dedupe,
    and `copy_file_range`. ext4 reserves delayed-allocation blocks when the write is made, so
    the writer at write time is what counts. That matches a direct write, including a
    descriptor opened by root and written by another user.
  - Reads, `mmap` (page faults run with the faulting task's credentials), `fsync` and directory
    reads keep the mounter's credentials. Namespace operations already ran with the caller's.
- Test, suite step `reserve`: the existing check now runs twice. First with `resuid=4242`, then
  with the default reserve owner. In both, uid 1001 fills the lower through the layer and at
  least 3/4 of the reserve must remain. Expected on `main`: the second run fails.
- Surfaces: README in both languages (the workaround is no longer needed), SECURITY, FAQ and its
  site page, security roadmap, and a note in ADR-0011. The old wording goes to
  `docs/retired-claims.txt`.

## Evidence

- The module builds with `W=1` against the Debian 6.12 headers, with no warning.
- `scripts/checkpatch.sh`: no findings.
- shellcheck at every severity: no findings.
- The FAQ page matches its source; retired claims: OK.

## Limitations

- Not run yet; KASAN on the branch and the negative round are pending.
- A write by a new identity on a file costs one credential allocation. Repeated writes by the
  same identity reuse it.
- Only ext4 has a reserve owner among the tested lowers. btrfs and XFS were not affected.

## Next step

Record both runs here, then a pull request.

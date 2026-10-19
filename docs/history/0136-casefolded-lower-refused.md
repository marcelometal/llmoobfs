# 0136 — A casefolded lower directory is refused (issue #95)

Date: 2026-10-19. Status: written; the test and the fix wait for the KASAN job and a negative
round.

## Goal

Issue #95. The layer's dentry cache compares names byte for byte (`moobl_dops` has no `d_hash`
or `d_compare`), and `moobl_fileattr_set()` already refused toggling `FS_CASEFOLD_FL` through the
layer for that reason. A directory that was casefolded on the lower before the mount, or outside
the layer, was not refused: the lower answers `note.md` for `Note.md`, so two layer dentries
could stand for one lower entry, and a negative layer dentry could hide a file.

## Prior art

overlayfs in v7.3-rc7 requires every layer to share one casefold state: checked when a layer is
added (`fs/overlayfs/params.c:298-316`) and on lookup, for the parent and the child ("parent wrong
casefold", "child wrong casefold", `fs/overlayfs/namei.c:254-270`). The alternative, propagating
casefolding with `generic_ci_d_hash()`/`generic_ci_d_compare()` on the layer's dentries, would be
new behaviour (a feature) and would have to match the lower's encoding and strictness; it is not
taken.

## Changes

- `layer/main.c`: `moobl_casefolded()`; `moobl_lookup()` refuses with `EOPNOTSUPP` a name inside a
  casefolded lower directory and a casefolded lower directory itself; the mount refuses a
  casefolded lower root ("lower cannot be a casefolded directory"). A rate-limited warning names
  the directory.
- `layer/index.c`: a domain build skips a casefolded directory, which lookups refuse, so queries
  do not answer for files the layer cannot open.
- `tests/layer.c`: `cfsetup` (on a lower made with `-O casefold`: a casefolded `ci` with
  `Note.md`, an ordinary `plain`) and `cfcheck` (`plain` works; `ci`, `ci/note.md`, `ci/Note.md`
  and a create in `ci` fail with `EOPNOTSUPP`).
- `vm/suite.sh`: step `casefold`, marker `CASEFOLD_REFUSED_PASS` (or `CASEFOLD_SKIPPED` without
  casefolding); also a domain build over that lower and the refused root mount.
- `vm/kasan.config`, `vm/kcsan.config`, `vm/bench.config`: `CONFIG_UNICODE=y`, so the KASAN job
  runs the step instead of skipping it.
- `docs/testing.md`: the marker.

## Tests

To run: the KASAN job of the pull request, and a negative round with the layer change undone and
the test kept, which must fail at `cf-check`.

## Limitations

ext4 only in the test; f2fs and tmpfs lowers are not exercised. A directory casefolded on the
lower after the layer looked it up keeps its layer dentry until it is revalidated or evicted;
ext4 casefolds only an empty directory, so no name inside it is cached then, but a negative
dentry could be.

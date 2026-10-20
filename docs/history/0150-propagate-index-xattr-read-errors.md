# 0150 — Propagate attribute-read errors while building the index

Date: 2026-10-20. Status: completed locally, pending batch integration and
KASAN/lockdep. Work started at approximately 06:37 and ended at 06:57 BRT on
2026-10-20 (20 minutes of calendar time, including VM wait; hands-on effort was
not separately metered).

## Goal

Correct R2 from the independent review (journal 0144). A `vfs_getxattr()`
failure after `listxattr()` must not produce a successful, incomplete domain
index. This is a correctness fix to the existing query contract, not a format or
UAPI change.

## Evidence and decision

Before this change, `attrs_read()` in `layer/index.c` skipped every `n <= 0`
return. `index_load()` then accepted the reduced set of attributes. On a first
scan `domain_ensure()` could mark the domain built; on adoption of a new hard
link or renamed file, `index_adopt()` received success instead of marking the
domain lost. The query specification says a lower scan error must fail the
build and a later query must retry (`docs/spec-query-v2.md`, lines 291–297).

The correction skips `-ENODATA`, which is possible if an attribute disappears
between listing and reading its value, and `-ERANGE`, which means its value at
read time exceeds the `MOOB_ATTR_VALUE_MAX + 1` scan buffer. A concurrent
increase in size has the same treatment; a concurrent layer write causes the
loader to retry, while direct lower changes are outside the live-update
contract. It also skips values with invalid typed encodings as before. It
returns other negative `vfs_getxattr()` results, including `-EIO` and `-ENOMEM`, to
the existing callers. The existing build failure path drops the domain; the
existing adoption failure path marks a built domain lost for a later rebuild.

The test uses a root-writable, test-only `scan_getxattr_errno_once` module
parameter to return one chosen error after a typed name has been listed. This
isolates the value-read result from lower-filesystem behavior; the test does
not claim that an actual ext4 I/O fault was produced. The parameter follows
the existing test-only `scan_delay_us` pattern. Linux 6.12.112 `fs/xattr.c`
was inspected for the return path (recorded in `docs/references.md`).

## Changes and validation

- `layer/index.c`: keep `-ENODATA` and `-ERANGE` as skipped values; propagate
  operational read errors.
- `vm/suite.sh`: `xattrread` checks `-EIO` and `-ENOMEM` on first builds, retries,
  failed adoption into a built domain, and an `-ENODATA` between list and get.
- `docs/references.md`: exact kernel source consulted.
- `make checkpatch`: passed on 2026-10-20.
- `git diff --check` and `sh -n vm/suite.sh`: passed on 2026-10-20.
- First `make vm-test`: failed at `hostile-mismatch-s1-k0-btrfs` before reaching
  the new test. The first correction propagated `-ERANGE` from an oversized
  lower value; the existing hostile test expects that value to be excluded.
  The correction was narrowed to preserve that rule. This is a failed
  experiment, not a passing VM run.
- First targeted `MOOB_ONLY=xattrread make vm-test`: failed at
  `xr-adopt-rebuild`. The test linked the incoming file, making `nlink > 1` and
  therefore excluding it from homogeneous `QUERY2` by the established rule in
  `docs/spec-query-v2.md` section 3. It did log the intended failed adoption,
  but the expected query result was wrong. The test now renames a single-link
  file from an unbuilt domain into the built one.
- Revised targeted `MOOB_ONLY=xattrread make vm-test`: passed with
  `XATTRREAD_PASS` and `MOOBFS_ONLY_PASS`; the latter is a partial-suite marker.
- Negative branch `neg/xattr-read-test-only` at `7c8682b` has the same test and
  test-only injection but retains the old `n <= 0` handling. Its targeted VM
  run failed as expected with `XR_BUILD_EIO: q2 0` and
  `MOOBFS_VM_FAIL: xr-build-eio` (exit 2). No query error was returned.
- Full `make vm-test` after the `-ERANGE` adjustment: passed (exit 0) on the
  Debian 6.12.111 QEMU VM. The console includes `HOSTILE_LOWER_PASS` for ext4,
  btrfs and XFS, then `XATTRREAD_PASS`, `MOOBFS_ACCEPTANCE_PASS` and
  `MOOBFS_LAYER_PASS`. This was a complete suite run, not `MOOB_ONLY`.
- Final `make checkpatch`, `git diff --check` and `sh -n vm/suite.sh`: passed.

## Limits and next step

The test-injected error exercises the module's value-read branch and the
propagation path, not lower ext4 fault timing. `-ERANGE` is treated as a value
too large for this module even if it grew after the list operation; the
established direct-lower-change limitation applies. A real lower-I/O fault
remains an optional separate integration test. This correction did not change
locking or object lifetime; the batch's KASAN/lockdep gate was not run here.
Next, integrate it with the local review batch and run that gate before
publication.

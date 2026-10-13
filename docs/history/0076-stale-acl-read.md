# 0076 — Fix: an ACL read through the layer could be the one before the last change

Date: 2026-10-13. Status: implemented; `make vm-test` passes, the new test fails without the fix
and the local KASAN job passes. Local series (after pull request 57). This closes the one failure of
the first fstests run that journal 0075 left unexplained, `generic/318`.

## What was wrong

The VFS remembers in an inode the ACL it read through `->get_acl` (`__get_acl`,
`fs/posix_acl.c`), and expects the filesystem to update or drop that copy when the ACL changes.
The layer's ACLs are the lower's: `get_acl` and `set_acl` delegate to the lower dentry and nothing
dropped the copy kept in the layer's inode. After a change, `getfacl` or
`getxattr("system.posix_acl_access")` through the layer could return the earlier ACL until the
inode left the cache or the layer was remounted. `generic/318` showed it: an ACL set from inside a
user namespace was visible only after the remount.

It did not affect access decisions. `moobl_permission` asks the lower inode, which has the
current ACL; only what is read back through the layer was stale.

## Fix

The layer's inodes are created with `i_acl` and `i_default_acl` set to `ACL_DONT_CACHE`, so the
VFS asks the lower every time. overlayfs does the same (`fs/overlayfs/inode.c:908`).

Alternative: drop the cached copy in `set_acl`. Rejected: an ACL changed directly on the lower, or
by another path, would still be remembered, and the lower already caches it.

## Test

`tests/layer.c`: four changes of the access ACL of a file through the layer, each followed by a
read through the layer and a read on the lower; both must return the ACL just set. Skipped with a
note when the lower is mounted without POSIX ACLs.

## Evidence

- `make checkpatch`: clean.
- The tree before the fix, with the new test: `FAIL` at the comparison of the ACL read through the
  layer, `MOOBFS_VM_FAIL: layer-tests-ext4`.
- `make vm-test` with the fix: every marker through `MOOBFS_LAYER_PASS`.
- fstests, eight ACL tests re-run with the fix (`generic/099 105 237 307 318 319 375 444`): seven
  pass, `generic/318` among them; `generic/099` still fails because it runs a file from the
  filesystem, which the mount refuses by design.
- Local KASAN (`vm/kasan-local.sh` on the benchmark host, upstream 6.12.112 with KASAN, UBSAN and
  lockdep): `MOOBFS_KASAN_PASS`, no kernel diagnostic.

## Limitations

The same class, "the VFS caches something the lower owns", was not searched for beyond ACLs.
Attributes are copied from the lower after each operation (`moobl_copy_attr`), which the suite
exercises; other cached state was not reviewed.

## Next step

For the maintainer (journal 0075): btrfs and XFS under fstests, and the four gaps.

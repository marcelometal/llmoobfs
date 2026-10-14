# 0104 — SELinux: first investigation (security roadmap S12)

Date: 2026-10-14. Status: three rounds run on the benchmark host; two defects found and fixed;
KASAN and the local suite with the fixes are recorded below.

## Goal

S12 of `docs/security-roadmap.md`: the module has never run with SELinux enforcing. Find out
whether it works there, and whether a confined process can reach through the layer a file that
the policy denies it directly. Done when the result is recorded and either the defects are
fixed or the README and `docs/when-not-to-use.md` say SELinux enforcing is not supported.

## Prior art read (round 1)

- **How SELinux labels a filesystem it has no rule for** (`security/selinux/ss/services.c`,
  `security_fs_use()`, Linux 6.12.112): the type is looked up in the policy's `fs_use_*`
  rules, then in its `genfscon` rules; with neither, the superblock gets
  `SECURITY_FS_USE_NONE` and the unlabeled SID, and every inode under it is unlabeled.
  Prediction: with a stock policy every file seen through the layer is `unlabeled_t`, so a
  confined process is denied everything, and only unconfined processes work.
- **How the reference policy handles overlayfs** (SELinux reference policy, release
  2_20250213, `policy/modules/kernel/filesystem.te`, line 40):
  `fs_use_xattr overlay gen_context(system_u:object_r:fs_t,s0);`. The overlay's inodes take
  their label from the `security.selinux` attribute, which overlayfs reads from the real
  file. The same rule for `moobfs_layer` would make each file seen through the layer carry the
  label of its file on the lower filesystem. Not tried yet.
- **How SELinux's own test suite tests overlayfs** (SELinuxProject/selinux-testsuite,
  `tests/overlay/` and `policy/test_overlayfs.te`): a mounter domain, a client domain, and file
  types with full access, read-only access, mapping access and no access; the client's access
  through the overlay is compared with what the policy allows. The test of this investigation
  is to follow that structure, with `moobfs_layer` in place of overlay.
- Why the question is real for MoobFS: data operations on an open file run with the mounter's
  credentials (ADR-0011), so SELinux checks the mounter's context, not the caller's, on the
  lower file at that point. `moobl_permission()` calls `inode_permission()` on the lower inode
  with the caller's credentials, which runs SELinux's inode check for the caller against the
  lower file's label at open. Whether that covers every path (open, mmap, ioctl, xattrs,
  directory reads) is what the test has to show.

## Plan

1. A guest of the benchmark host's kernel (Debian 6.12.111, SELinux compiled in, AppArmor
   active by default) booted with SELinux enforcing and the Debian reference policy; the
   module loaded; ext4 lower.
2. Stock policy: mount, the suite's basic steps as root (unconfined), and a confined process.
3. A policy module with `fs_use_xattr moobfs_layer` and a test module after
   `test_overlayfs.te`: types readable, read-only, and not accessible to a client domain.
   Each operation tried on the lower file directly and through the layer; the answers must
   agree.
4. Negative check: the test must see a denial on the lower file directly, or it proves
   nothing.

## Environment as built

`vm/selinux-local.sh` (host) and `vm/selinux-guest.sh` (guest), with the policy modules in
`vm/selinux/`. A virtme-ng guest of the host's kernel (Debian 6.12.111) booted with
`security=selinux`; the Debian reference policy (2:2.20250213) built on the host, where SELinux
stays inactive, and loaded in the guest through selinuxfs. Things learnt on the way, each a
failed attempt first:

- Linux 6.12 loads policy versions up to 33; Debian 13 builds 34 (`policydb version 34 does
  not match my version range 15-33`). `policy-version = 33` in `/etc/selinux/semanage.conf`.
- selinuxfs takes the policy in one write; `cat` wrote it in pieces (`avtab: truncated entry`).
- The guest's root is the host's tree over 9p, without labels: kernel modules from it are
  refused once the policy is loaded (`module_load` on `unlabeled_t`), so ext4 and the layer are
  loaded first; the client runs a static busybox copied to a tmpfs and labelled `bin_t`; the
  test domain may search unlabelled directories (an allowance of the test only).
- The client's output to the test's pipe was denied silently until the test domain was given
  the pipe.

The test domain follows `test_overlayfs.te`: `moobtest_client_t`, and files of types it may
read and write (`moobtest_rw_t`), only read (`moobtest_ro_t`) or not touch (`moobtest_none_t`),
under a directory it may use. For each of read, stat, append and list on each file, the client
runs the operation on the lower file and through the layer; the exit statuses must agree. Then it
creates one file each way, and runs a `QUERY2` that matches all three files.

## Rounds

**Round 1, stock policy** (no rule for `moobfs_layer`): every file through the layer is
`unlabeled_t`, as predicted. The client is refused everything through the layer, including
five operations it may do on the lower files and the creation of a file. Nothing is allowed
through the layer that is refused directly. The layer is unusable for confined processes, not
unsafe for them; unconfined processes are not affected.

**Round 1, with `vm/selinux/moobfs_layer.cil`** (`fs_use_xattr moobfs_layer`): each file
through the layer carries its lower file's label; all twelve operations agree; the file created
through the layer gets the label the policy's type transition gives (`moobtest_rw_t`), as the
one created directly.

**Round 2, the client reaches each file first.** In round 1 the files had been listed by root
before the client touched them. With the client first, the kernel logged
`inode_doinit_use_xattr: getxattr returned 13 for dev=moobfs_layer` at every try on the file
it may not read: SELinux reads a new inode's label through the layer's `getxattr` handler,
which read the lower with the caller's credentials, and the lower refused them. Access was still
refused, and the label was right once another process reached the file, so nothing leaked; but
a confined process could fill the kernel log, and the inode stayed unlabelled meanwhile.
**Fixed:** `security.*` is read from the lower with the mounter's credentials, as overlayfs does
in `ovl_xattr_get()`; a caller's own read of such an attribute is still checked by the VFS
against the layer's inode, which carries the same label. Every other attribute is read with the
caller's credentials as before. With the fix the message is gone and the labels are right with
the client first; with the fix undone the test fails at that check (negative run).

**Round 3, `QUERY2`.** The three files carry the same attribute; root's query names all three.
The client's query, with the label rule, also named all three, including the file whose label
denies the client even `getattr`: the client learned the file's name, inode number and that its
attribute matched, which it cannot learn on the lower filesystem directly (listing the
directory gives the name, not the attributes). This breaks promise 3 of ADR-0011 and is
recorded there as **finding F6**. Cause: the homogeneity rule (`reach_ok()` in
`layer/index.c`) reads mode bits and ACLs only, so a partition can hold files the policy hides.
v1 `QUERY` was not affected: it checks every result with `inode_permission()`.
**Fixed:** when an LSM keeps labels for the layer's inodes (`security_inode_listsecurity()`
names one: SELinux, Smack; AppArmor and Landlock keep none), every `QUERY2` result is checked for
the caller with the path walk and `inode_permission(MAY_READ)` already used after a permission
change. The client's answer is now the two files it may read; with the check disabled the test
fails at `QUERY2 names a file the client may not even getattr (F6)` (negative run). With the
stock policy the query is refused at the domain directory, as every other access.

## Results

- `sh vm/selinux-local.sh` on the benchmark host, final code: `MOOBFS_SELINUX_RESULT: stock
  differ=5 denied_directly=7` and `MOOBFS_SELINUX_RESULT: fsuse differ=0 denied_directly=7`,
  with the `QUERY2` answer `rw`, `ro`. Negative runs: each fix undone alone fails at its own
  check.
- `make vm-test`, complete, with the first fix: pass (`MOOBFS_LAYER_PASS`,
  `MOOBFS_ACCEPTANCE_PASS`). With both fixes: see the note below when recorded.
- `sh scripts/checkpatch.sh`: exit 0.

## Limitations

- **The shipped policies have no rule for `moobfs_layer`.** Without `vm/selinux/moobfs_layer.cil`
  (or the same line in a site policy), confined processes cannot use the layer at all.
- **Under SELinux every `QUERY2` result is checked**, so queries cost what they cost after a
  permission change in the domain. Not measured.
- Covered: read, stat, append, directory listing, creation, label of new files, `QUERY2`. Not
  covered: mmap, ioctls other than the queries, attribute writes, rename, link, execution,
  `QUERY2_BATCH` (it runs the same function), a policy with MLS or MCS categories, Smack.
- One kernel (Debian 6.12.111), one lower filesystem (ext4), the Debian reference policy.
- A guest whose root has no labels, with the allowances listed above; a distribution booted
  with SELinux from the start (Fedora) is still untried.

## Next step

Record the KASAN run with both fixes. Then the README and `docs/when-not-to-use.md` say what is
needed under SELinux, and S12 can be closed or narrowed to what is not covered.

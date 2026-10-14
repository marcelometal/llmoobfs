# 0099 — The credential model was described as overlayfs's; it is not

Date: 2026-10-14. Status: correction of records; no code in this journal.

## What came back

The question sent to linux-unionfs on 2026-10-13 (planned in journal 0084, unanswered in journal
0097: which decisions does a lower filesystem take from the caller during a data operation, that
a stacked filesystem must take before switching to the mounter's credentials) was answered by an overlayfs maintainer. Thread:
"[RFC] Stacked fs, mounter creds: caller checks in the lower fs" on linux-unionfs.

1. **The premise was wrong.** The message said MoobFS "follows the overlayfs credential model:
   name operations run with the caller's credentials, data operations on an open file with the
   mounter's". Overlayfs makes no such split. Every access is checked twice: (a) the caller
   against the overlay inode, DAC and MAC; (b) the mounter against the real inode, DAC and MAC.
   The real operation runs with the mounter's credentials, and LSM hooks such as
   `security_file_permission()` on a write see the mounter. What the message described is
   closer to the UNIX DAC model, checked at open and not at each access.
2. **No complete list exists.** The checks we listed (`file_remove_privs`, dropping
   `CAP_SYS_RESOURCE`, and so on) are the places where the generic VFS falls short of enforcing
   the model, not the list of every use of the caller's credentials; most of the enforcement is
   generic VFS code. The maintainer did not know of another case offhand, and said some
   behaviours (`CAP_SYS_RESOURCE`, `mprotect`) are hard or impossible to reproduce exactly.
3. **The expected behaviour is stated by equivalence.** `overlayfs.rst` says an overlay mount
   should give the same access as `cp -a /lower /upper; mount --bind /upper /merged`.
4. **Terminology.** In overlayfs "lower" is an immutable layer and the writable one is "upper";
   MoobFS uses "lower" as eCryptfs does, for the one writable filesystem underneath. "Real" or
   "backing" avoids the confusion.

Checked against v6.12.112 before recording: `Documentation/filesystems/overlayfs.rst`,
"Permission model" (the two checks, the `cp -a` equivalence); `fs/overlayfs/dir.c` calls
`ovl_override_creds()` around create, unlink, rename and the other name operations;
`fs/backing-file.c` calls `vfs_iter_write()` under `override_creds()`, and `rw_verify_area()`
in `fs/read_write.c` calls `security_file_permission()`.

## Corrections

- `docs/decisions/0011-threat-and-credential-model.md`: dated note under "Credentials (as
  implemented)". The description of what MoobFS does stays; "this is the overlayfs model" is
  withdrawn. A second note under "Alternatives considered": the rejected alternative is what
  overlayfs does, and "the source of most overlayfs credential bugs" was never checked and is
  withdrawn as a reason. The decision stands on the remaining reason.
- `docs/research-stacked-fs-kernel-rules.md`, section 1: dated note on its first sentence.
  R-CRED-4 already described the overlayfs two-check model correctly.
- The RFC text cannot be corrected; the reply to the thread does it.

## What MoobFS does, stated precisely

- Name operations: checked against the caller only, on the layer inode and again by the lower
  filesystem, which also runs its LSM hooks for the caller. Nothing checks the mounter. Since
  mounting needs `CAP_SYS_ADMIN` in the initial user namespace, the overlayfs check (b) would
  rarely refuse anything under DAC; under an LSM policy that confines the mounter it could, and
  MoobFS does not apply it.
- Data operations on an open file: the caller is checked when the file is opened (and the VFS
  runs the LSM hooks for the caller on the layer's file); the backing file is opened and used
  with the mounter's credentials, so the lower's LSM hooks see the mounter.

## Decisions

- Keep the model; describe it as MoobFS's own. No change to the code.
- Proposal, not implemented: a differential test after the overlayfs equivalence. MoobFS has
  no copy-up, so the equivalent is a bind mount of the lower: the same operations, run by an
  unprivileged user through the layer and through `mount --bind <lower> <dir>`, must give the
  same results and errors, except where the layer is documented to differ (`user.moob.*`
  typing, the reserved `user.moob.format`, `EXDEV` across domains). This would test promise 2
  of ADR-0011 directly instead of by a list of checks.
- The S12 run under SELinux (security roadmap) must cover the difference above: which
  credentials the lower's policy sees for name and for data operations.
- The term stays "lower" in the code and the documents; `docs/architecture-overview.md` gets
  no rename. A reader coming from overlayfs is warned in the reply; a glossary line can follow
  with the next documentation pass.

## Limitations

- The reply to the thread is drafted, not sent; sending is the maintainer's action.
- The differential test is a proposal.

## Next step

Send the reply with the correction. Then the remaining items of journal 0098.

# 0066 — Stacked-filesystem rules from kernel sources, audited against the layer

Date: 2026-10-12. Status: research and audit by reading, with three items reproduced in a
disposable VM; no code changed; not committed to a shared branch in this session. Local series.

## Goal

Before the next security roadmap items (S2 fstests, S3 syzkaller, S4 adversarial lower), collect
from kernel v6.12 primary sources the rules a stacked filesystem must follow — credentials
(`override_creds`), locking and lock order, dentry/inode lifetime, ioctl/UAPI (layout, compat,
reserved-field validation), xattrs, hostile-lower handling, memory limits — and record, for each,
the exact source (file and version) and whether `moobfs_layer` complies, does not, or was not
verified. Document-only; code must not change.

## Sources and method

Kernel read at `v6.12` (torvalds), lines re-checked against `v6.12.112` (the stable series the
KASAN harness builds; `Makefile` `SUBLEVEL = 112`). For every cited file the two tags agree on the
cited lines; `fs/attr.c` and both `fs/ecryptfs/*` are byte-identical between them. Primary sources:
`Documentation/` (filesystems: vfs, locking, directory-locking, path-lookup, porting, mount_api,
overlayfs, ecryptfs, fuse; process: botching-up-ioctls, submit-checklist, submitting-patches,
coding-style, deprecated, howto; driver-api/ioctl; userspace-api/ioctl/ioctl-number;
core-api/memory-allocation; kbuild/modules; security: credentials, self-protection, siphash;
RCU: rcubarrier, stallwarn) and the in-tree code of overlayfs, ecryptfs, FUSE passthrough and the
VFS files they rest on. KSPP Recommended Settings included as asked, labelled as a community source
(kspp.github.io / kernsec.org), not kernel.org `Documentation/`.

Two checks beyond reading:

- **UAPI layout by compilation.** The five ioctl structs compiled to identical sizes on x86_64,
  i386, aarch64 and armv7 (clang `-ffreestanding` cross `-fsyntax-only`, `_Static_assert` on
  `sizeof`/`offsetof`): no 32/64 skew, no implicit tail padding, so `compat_ptr_ioctl` is correct.
- **Disposable QEMU VM** (kernel 6.12.111+deb13-amd64, module built from this tree at d39bbb8) to
  turn three code readings into evidence.

## Result

Document written: `docs/research-stacked-fs-kernel-rules.md` (English; indexed in `docs/README.md`).
Most reviewed rules comply or are the known-open roadmap items. Three divergences, most severe
first:

- **R-CRED-3 — user-triggerable kernel crash (denial of service).** `moobl_setattr`
  (`layer/main.c:774-797`) forwards `ATTR_KILL_SUID|ATTR_KILL_SGID` together with `ATTR_MODE` to
  `notify_change`, the combination forbidden by `BUG()` at `fs/attr.c:444`. overlayfs
  (`fs/overlayfs/inode.c:55-56`) and ecryptfs (`fs/ecryptfs/inode.c:974-975`) strip `ATTR_MODE` at
  that point; the layer does not. **VM:** as uid 1001, `echo x > s; chmod 4755 s; echo y >> s`
  oopsed — `kernel BUG at fs/attr.c:444!`, `RIP: notify_change+0x4d4`, trace through
  `moobl_setattr ← moobl_write_iter`. Contradicts ADR-0011 promise 1 and `SECURITY.md`; a hard
  panic on a KSPP kernel with `panic_on_oops`. Fix is a one-line `lower_ia.ia_valid &= ~ATTR_MODE`
  guard, not applied here (document-only task).
- **R-CRED-2 — reserved-space / quota bypass.** Mounter credentials keep `CAP_SYS_RESOURCE`;
  overlayfs drops it on purpose (`fs/overlayfs/super.c:1442`). **VM:** uid 1001 writing through the
  layer drove the lower ext4 below its root reserve, which a direct write denied. Low severity.
- **R-INODE-4 — `st_dev`/`st_ino` not unique across lower objects.** By design, no `xino`
  equivalent, and undocumented for MoobFS. **VM:** two files in different btrfs subvolumes both
  reported `29:257`. overlayfs documents this (`overlayfs.rst:20-34`) and offers `xino`.
  Recommend a documented limitation note. Not a crash (internal inode keying is by lower-inode
  pointer).

Partial/absent against the sources, already tracked: R-MEM-1 index budget is one pool for the
mount and allocations are not `__GFP_ACCOUNT` (ADR-0011 F2, S9); R-SUBMIT-3 allocator fault
injection and the full PREEMPT/!SMP lockdep matrix not run; R-LOCK-5 no explicit `cond_resched`
in the mount scan (large-tree stall not tested).

## Discarded / corrected along the way

- A documentation defect in the repo: `docs/security-roadmap.md:69` cites the kernel doc "Adding
  New Filesystems"; `Documentation/filesystems/adding-new-filesystems.rst` returns 404 at
  `v6.12`..`v6.14` and exists only on `master`. The claim (fstests is standard infrastructure) is
  still traceable there, but the citation is anachronistic for the pinned kernel. Recorded in the
  document's references note; the roadmap text itself was not edited in this session.
- A stray 19-line partial draft (`docs/research-stacked-fs-rules.md`), left by an interrupted
  earlier draft, was removed; the finished document is `research-stacked-fs-kernel-rules.md`.

## Evidence

- VM run: module built in the `moobfs-m1-test` container from this tree, booted under QEMU TCG;
  R-CRED-3 oops, R-CRED-2 reserve drop and R-INODE-4 st_ino collision all observed in one boot.
  Scratch scripts and console log kept outside the repository (throwaway).
- UAPI cross-ABI size asserts passed on four targets.

## Limitations

The audit is by source reading plus three targeted VM reproductions; it is not fstests, syzkaller
or fuzzing (S2/S3/S4/S7), which remain open. R-CRED-3's fix is unverified because no code was
changed. Compliance of the "not verified" rows was not demonstrated on a running kernel.

## Next step

Open the one-line R-CRED-3 fix as the first code item of the series (with a regression test:
write to a suid file through the layer as an unprivileged user, expect success and no oops), run it
under `make vm-test` and local KASAN, then revisit R-CRED-2 and the R-INODE-4 documentation note.
Threat-model text (ADR-0011) and `SECURITY.md` should reference this audit once the fix lands.

# 0017 — Use on macOS and security posture compared to the market

Date: 2026-10-10, America/Sao_Paulo. Status: survey and decisions; no code change. Sources
consulted listed in each section.

## Use on macOS

Decision: there is no native support on the Mac. The layer is a Linux kernel module
(`layer/moobfs_layer.ko`) over ext4, btrfs or XFS. macOS uses XNU, third-party kexts are
deprecated and require lowering security on Apple Silicon; FSKit is in user space, the approach
ADR-0001 rules out.

Ways to use it on the Mac:

- Host tools (`tools/`) compile and run on macOS with `make test`; they mount nothing.
- Linux VM on the Mac (real use): today `make vm-test` with Docker and emulated x86-64 QEMU; for
  actual use, an arm64 VM (Lima, UTM, OrbStack) with the module compiled against the VM's
  kernel, the lower on an ext4/btrfs/XFS disk inside the VM, and the agent running inside the VM.
- Mac as a client of the VM over SSH, sshfs, NFS or SMB: `QUERY` and `GET_INFO` are ioctls and
  do not cross these protocols; NFS export is out of M2 scope. It would need a service inside
  the VM, along the lines of the in-house memory client from the M2 acceptance.
- "Nothing locked in": the unmounted lower is files with xattrs; `rsync -X` or git carry them to
  APFS, and `moobfs-check` validates offline on the Mac. The index and the query exist only in
  the Linux kernel.
- Putting the lower in a Mac folder via virtiofs does not work: APFS has no reflink in the
  btrfs/XFS sense and xattrs over virtiofs with a macOS host are not guaranteed.

Native support on the Mac would be a new ADR (FSKit or kext), not an extension.

## Current security posture

Survey of what the repository has (verified in `layer/`, `.github/workflows/ci.yml`,
`vm/`, `docs/testing.md`):

- Validation at the edge: `QUERY` rejects a zero limit or one above the cap, and non-zero flags
  and reserved fields (`layer/index.c`); `copy_to_user` with a fixed size; typed attributes
  validated before touching the lower (type, size, name alphabet, UTF-8).
- Permissions in two layers (`inode_permission` on the lower plus the lower's own check) and
  `override_creds`, as in overlayfs.
- KASAN, UBSAN and lockdep in a dedicated workflow (`vm/kasan-guest.sh`), failing on any
  `BUG`, `Oops`, `WARNING` or KASAN report in `dmesg`; ASan and UBSan on the `common/` code.
- libFuzzer over the attribute encoding, time-limited in CI.
- Module loaded only in a disposable QEMU VM; index memory budget (`index_budget`).

Gaps identified: a written threat model; syscall fuzzing (syzkaller with descriptions of the
ioctls); treating the lower as untrusted input in the mount scan; keeping `FS_USERNS_MOUNT`
off while user namespaces are out of scope and saying so in the ADR; `SECURITY.md`, sparse,
smatch and checkpatch in CI; module signing and Secure Boot for end users. "Guardrails" in the
LLM sense do not belong in the layer: it is storage; content policy stays in the client.

## Market references and literature

Sources consulted on 2026-10-10 (web search; summaries, not full reading):

- Kernel documentation, "Adding New Filesystems": fstests is the standard infrastructure and its
  use is strongly recommended; it requires a documented way to test and detect regressions; the
  filesystem must handle corrupted input without hanging or bringing down the kernel; fsck
  strongly recommended; custom ioctls in separate patches with justification.
- Janus (IEEE S&P 2019) and Hydra (SOSP 2019), Georgia Tech: joint image and syscall fuzzing;
  Janus found 90 bugs in eight kernel filesystems, 32 CVEs; Hydra found 155 bugs including
  post-crash inconsistency and POSIX violations. LFUZZ (FSU, 2024) extends Janus.
- Debate on linux-fsdevel and LWN (2018, 2023, 2025) about malicious images: maintainers do not
  promise that mounting a hostile image is safe; Dave Chinner considers a user-triggerable crash
  a security problem (DoS) even without an exploit; the syzbot bug queue exceeds fixing
  capacity; isolation in a VM (libguestfs) is the approach considered safe.
- bcachefs: before the merge in 6.7, the author reported fstests and an own suite under
  lockdep+KASAN, preempt and gcov (82% of lines); removed from the tree in 6.18 and now DKMS.
- overlayfs: CVE-2015-1328, CVE-2016-1576, CVE-2020-16120 and CVE-2026-74619, all tied to
  unprivileged mounting via `FS_USERNS_MOUNT` and user namespaces; distributions that leave the
  flag off avoid the whole class. The history of `override_creds=off` shows that the mounter's
  credential model needs to be written down to be reviewable.
- eCryptfs (stacked layer): CVE-2014-9922 (escalation through a large filesystem stack with
  overlayfs), CVE-2009-2406 (overflow in a metadata parser), and most CVEs in the user-space
  mount helper.
- xattr: CVE-2026-63833 (ntfs3, direct setxattr on reserved names gives euid 0), CVE-2010-2946
  (JFS, name prefix bypasses namespace restriction), CVE-2019-19319 (ext4, setxattr after a
  forged image causes out-of-bounds write), CVE-2016-7097 (setgid preserved on setxattr).
- OpenZFS (out-of-tree module): `ztest` as user-space stress with fault injection; a 2026 fix
  for pool manipulation by unprivileged userns.
- syzkaller: syzlang descriptions in `sys/linux/*.txt` with manual `.const` or `syz-extract`;
  there is no official flow for out-of-tree modules, but the pattern is a local checkout.

Recorded conclusion: MoobFS's current set (validation at the edge, KASAN/UBSAN/lockdep,
libFuzzer, isolated VM) is what comparable projects had at the experimental stage. What is
missing to reach the market level: fstests, syzkaller over the ioctls and `setxattr`, fuzzing
of the lower as hostile input, a written threat and credential model, `FS_USERNS_MOUNT`
explicitly off, and a disclosure process.

## Security roadmap

Decision: consolidate the techniques in a roadmap of their own. Created
`docs/security-roadmap.md` with the verified current state, eleven items S1 to S11 with
precedent, steps and done-when criterion, and what stays out of scope. Cross-cutting line
added in `ROADMAP.md` and entry in `CHANGELOG.md`.

## Next step

No code changed. First item of the security roadmap: S1, threat and credential model ADR.
Other candidates: a short threat model ADR; syzlang descriptions for `QUERY` and `GET_INFO`;
running fstests (`quick` group) over the layer in the VM.

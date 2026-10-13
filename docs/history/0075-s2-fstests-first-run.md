# 0075 — S2: fstests over the layer, first run (group `quick`, ext4)

Date: 2026-10-13. Status: port written, one run on one lower filesystem; btrfs and XFS not run;
no exclusion list yet. Local series (after pull request 57).

## Goal

Security roadmap S2: fstests over the layer, group `quick`, with pass, fail and not-run recorded
per lower filesystem and the exclusions justified. This journal covers the port and the first
lower; it stops there so the maintainer can decide how far to take the rest.

## Port

fstests does not know the layer. `vm/fstests/moobfs_layer.patch` adds the type in the eight places
where the suite already handles filesystems without a block device (virtiofs, 9p): the device
names are plain strings, "mkfs" of the scratch filesystem is fstests' own file removal, and there
is no consistency check. The lower is passed in the mount options.

Alternative: model the port on overlayfs, where fstests itself mounts the base filesystems.
Rejected for now: it is ten times the patch, and the base filesystems are set up by our script
anyway.

`vm/fstests-local.sh` and `vm/fstests-guest.sh` run it in a virtme-ng guest of the host's own
kernel, so a crash stays in the guest and the distribution kernel the layer targets is the one
tested. Two things had to be worked around in the guest: `vng -r` needs `busybox-static`; and the
guest's root reads the host's files as the user who started it, so `/etc/gshadow` is unreadable
and `useradd` fails. The accounts fstests needs are written directly into the guest's `/etc`.

Decision: fstests is not part of `make vm-test`. That VM is an initramfs with busybox and static
binaries; fstests needs a full userland. It stays a separate target on a KVM host, like the local
KASAN job.

## Result

fstests v2026.09.22, Debian kernel 6.12.111, module of 81348ee, lower ext4, `-g quick`: 647 tests
in 95 minutes of guest time, no kernel diagnostic. **178 passed, 451 not run, 18 failed.** Table
and per-test list in `vm/fstests/README.md` and `vm/fstests/results-ext4-quick.txt`.

The 18 failures, by cause:

- **10 by design:** the layer is mounted without execution and without device nodes, and these
  tests run a file from the filesystem, ask `mmap` for execute permission, or call `mknod`.
- **2 do not apply:** a `dax` mount option the layer refuses, and two mounts over one lower.
- **4 are gaps:** the file-attribute calls behind `lsattr` and `chattr` are not passed to the
  lower (2), and the access-time mount options of the layer's own mount have no effect (2).
- **1 was a defect**, `generic/688`, and reading the code for it uncovered a worse one in the same
  family; both are fixed in journal 0074. Re-run with the fix: 17 of the 18 fail.
- **1 is open:** `generic/318`, ACLs written from inside a user namespace read back with different
  entries and mask. Not understood. It may be identifier mapping of ACL entries.

Of the 451 not run, 159 need reflink or dedupe, which the lower ext4 does not have, and 103 need a
block device under the filesystem. A smaller set names what the layer does not offer:
`mknod`/`mkfifo`, `O_TMPFILE`, `O_DIRECT`, the `chattr` flags, idmapped mounts, NFS export.

## What the run was worth

Two findings neither the project's suite nor the review of journal 0066 had: the privileges kept
by `copy_file_range` and a clone, and the failed writes to files with capabilities. The first is
a security defect present in every version so far. It was not caught by a failing fstests test:
`generic/688` failed for the second, and the first was found by reading the surrounding code.

## Limitations

- One lower filesystem, one run, the distribution kernel without KASAN.
- 451 of 647 tests did not run; on btrfs or XFS about 160 more would.
- `generic/215` reported "No space left on device" in the full run and not alone; unexplained.
- `generic/318` is unexplained.
- No exclusion list: the classification above is by reading each difference once.
- The four gaps are not fixed.

## Next step

For the maintainer to decide: run btrfs and XFS (about two hours each) and investigate
`generic/318` first, since it may be a defect; then the four gaps, of which the file-attribute
calls are a small change and the access-time options need a decision on whose mount options count.

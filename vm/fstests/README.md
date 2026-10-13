# fstests over the layer

Security roadmap S2 (journals [0075](../../docs/history/0075-s2-fstests-first-run.md) and
[0077](../../docs/history/0077-s2-fstests-btrfs-xfs.md)).
`vm/fstests-local.sh` runs [fstests](https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git) over
the layer in a virtme-ng guest of the host's running kernel, so that a crash stays in the guest.

## How

1. On a Debian host with KVM, as in `vm/kasan-local.sh`: the build dependencies of fstests
   (`automake libtool libtool-bin gawk indent libacl1-dev libaio-dev libattr1-dev libcap-dev
   libgdbm-dev liburing-dev uuid-dev uuid-runtime xfslibs-dev attr acl bc dbench dump quota psmisc
   fio`), `busybox-static` for `vng -r`, and the headers of the running kernel.
2. `git clone --depth 1 --branch v2026.09.22
   https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git ~/fstests && make -C ~/fstests -j8`
3. From the repository root: `sh vm/fstests-local.sh ~/fstests ext4 -g quick` (lower filesystem,
   then any arguments of `./check`). Console in `build/fstests/console-ext4.log`, fstests' own
   results in `build/fstests/tree/results/`.

`moobfs_layer.patch` is the whole port: fstests learns the type in the eight places where it
already handles filesystems without a block device (virtiofs, 9p). The "devices" are two names,
`moobtest` and `moobscratch`; each is a layer mounted with `lower=` on a directory of a base
filesystem, with `domain_depth=0`. "mkfs" of the scratch filesystem is fstests' own file removal.
The guest gets the accounts fstests needs written directly into its private `/etc`.

## Result: group `quick`, three lower filesystems

fstests v2026.09.22, Debian kernel 6.12.111, 647 tests per run, no kernel diagnostic in any run.
Per test: [`results-ext4-quick.txt`](results-ext4-quick.txt),
[`results-btrfs-quick.txt`](results-btrfs-quick.txt), [`results-xfs-quick.txt`](results-xfs-quick.txt).

| | ext4 | btrfs | XFS |
| --- | --- | --- | --- |
| module of commit | 81348ee | 7b42ffe | 7b42ffe |
| guest time | 95 min | 118 min | 117 min |
| passed | 178 | 271 | 298 |
| not run | 451 | 360 | 333 |
| failed | 18 | 16 | 16 |

The ext4 run is the first one, before the fixes of journals 0074 and 0076; its two extra failures
(318, 688) pass on btrfs and XFS with the fixed module, and passed on ext4 when run alone after
each fix. The full ext4 run was not repeated.

### The failures

| Tests | What happens | Verdict |
| --- | --- | --- |
| 080, 141, 215, 446 | `xfs_io`'s `mmap` without flags asks for execute permission; the mount refuses it with `EPERM` | By design: the layer is mounted without execution (`SB_I_NOEXEC`). `mmap -rw` works |
| 099, 126, 452 | the test runs a file that lives on the filesystem | By design, same reason |
| 633, 696, 697 | `mknodat` fails with `EPERM` inside a larger test | By design: no device nodes or FIFOs through the layer |
| 503 (ext4, XFS) | mounts with `-o dax`, which the layer refuses as an unknown option, and does not skip | Does not apply. Skipped on btrfs, which has no collapse-range |
| 732 | mounts the same "export" twice and renames between the two mounts | Does not apply: two layers over one lower directory is outside the contract (ADR-0007) |
| 169, 420 | `FS_IOC_FSGETXATTR` returns `ENOTTY` | **Gap, closed** in journal 0079: the file-attribute calls (`lsattr`, `chattr`, `xfs_io lsattr`) are passed to the lower. Pass now |
| 003, 120 | `noatime`, `relatime`, `strictatime` on the layer's mount have no effect | **Limitation, by decision** (journal 0079): access times follow the lower's mount options. To be excluded |
| 674 (btrfs) | after a dedupe and a remount of the layer, free space has not grown | **Defect, fixed** in journal 0078: the dedupe is done, but `syncfs` and the unmount of the layer did not sync the lower, so btrfs had not committed. Passes now |
| 424 | `statx` did not report append-only, immutable, compressed, no-dump | Seen once 169 and 420 could run. **Closed** in journal 0079; passes now |
| 079 | one sub-test creates a device node | By design. It was skipped before journal 0079 for the missing `chattr` |
| 318 | an ACL read back right after it was changed was the earlier one, until a remount | **Defect, fixed** in journal 0076 (the VFS kept a copy the layer never dropped); passes now |
| 688 | `fallocate` by an ordinary user on a file with capabilities failed with `EPERM` | **Defect, fixed** in journal 0074; passes now |

`generic/215` also printed "No space left on device" for a 2 MiB write in the full ext4 run and
not when run alone; the cause was not found, and it did not happen on btrfs or XFS.

The same 15 tests fail on the three lowers (003, 080, 099, 120, 126, 141, 169, 215, 420, 446, 452,
633, 696, 697, 732); 503 and 674 depend on the lower as noted.

### Why tests do not run

On ext4, 451: most are features the lower ext4 or a stacked filesystem does not have, skipped by
fstests itself: reflink and dedupe (159), a block device under the filesystem (103:
device-mapper targets, a sized `mkfs`), encryption (23), quotas (23), the exchange-range call
(18), shutdown (15), swap files (13), fs-verity (10).

On btrfs and XFS no test is skipped for reflink or dedupe: 110 tests that do not run on ext4
pass on btrfs, and 120 on XFS. Everything that passes on ext4 passes on XFS. On btrfs, 16 tests
that pass on ext4 are skipped because btrfs has no collapse-range or insert-range and one
`fiemap` option.

A smaller set names what the layer does not offer and a client may meet, on any lower:
`mknod`/`mkfifo` (12), idmapped mounts (5), `O_TMPFILE` (4), NFS export (4), the `chattr` flags,
freezing (2), a maximum ACL count declared to fstests, and everything that needs a block device
or a sized `mkfs` under the filesystem.

## Not done

The `auto` group, a run under the KASAN kernel, a repeat of the full ext4 run with the current
module, and `make vm-test` integration. An exclusion list is not written yet: the table above is
its draft.

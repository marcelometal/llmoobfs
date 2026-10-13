# fstests over the layer

Security roadmap S2 (journal [0075](../../docs/history/0075-s2-fstests-first-run.md)).
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

## Result: group `quick`, lower ext4

fstests v2026.09.22, Debian kernel 6.12.111, 95 minutes of guest time, no kernel diagnostic.
Per test: [`results-ext4-quick.txt`](results-ext4-quick.txt).

| | Tests |
| --- | --- |
| ran | 647 |
| passed | 178 |
| not run | 451 |
| failed in the first run | 18 |
| failed after the fixes of journals 0074 and 0076 | 16 |

### The failures

| Tests | What happens | Verdict |
| --- | --- | --- |
| 080, 141, 215, 446 | `xfs_io`'s `mmap` without flags asks for execute permission; the mount refuses it with `EPERM` | By design: the layer is mounted without execution (`SB_I_NOEXEC`). `mmap -rw` works |
| 099, 126, 452 | the test runs a file that lives on the filesystem | By design, same reason |
| 633, 696, 697 | `mknodat` fails with `EPERM` inside a larger test | By design: no device nodes or FIFOs through the layer |
| 503 | mounts with `-o dax`, which the layer refuses as an unknown option, and does not skip | Does not apply |
| 732 | mounts the same "export" twice and renames between the two mounts | Does not apply: two layers over one lower directory is outside the contract (ADR-0007) |
| 169, 420 | `FS_IOC_FSGETXATTR` returns `ENOTTY` | **Gap:** the file-attribute calls (`lsattr`, `chattr`, `xfs_io lsattr`) are not passed to the lower |
| 003, 120 | `noatime`, `relatime`, `strictatime` on the layer's mount have no effect | **Gap:** access times follow the lower's mount options, not the layer's |
| 318 | an ACL read back right after it was changed was the earlier one, until a remount | **Defect, fixed** in journal 0076 (the VFS kept a copy the layer never dropped); passes now |
| 688 | `fallocate` by an ordinary user on a file with capabilities failed with `EPERM` | **Defect, fixed** in journal 0074; passes now |

`generic/215` also printed "No space left on device" for a 2 MiB write in the full run and not
when run alone; the cause in the full run was not found.

### Why 451 do not run

Most are features the lower ext4 or a stacked filesystem does not have, and are skipped by
fstests itself: reflink and dedupe (159; they would run over btrfs or XFS), a block device under
the filesystem (103: device-mapper targets, a sized `mkfs`), encryption (23), quotas (23), the
exchange-range call (18), shutdown (15), swap files (13), fs-verity (10).

A smaller set names what the layer does not offer and a client may meet: `mknod`/`mkfifo` (10),
idmapped mounts (5), `O_TMPFILE` (4), NFS export (4), the `chattr` flags (10), `O_DIRECT` (2), freezing
(2), a maximum ACL count declared to fstests (1).

## Not done

btrfs and XFS as lower filesystems (which enable the reflink tests), the `auto` group, a run under
the KASAN kernel, and `make vm-test` integration. An exclusion list is not written yet: the table
above is its draft.

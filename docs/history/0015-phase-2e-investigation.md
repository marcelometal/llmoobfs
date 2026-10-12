# 0015 — Phase 2e: acceptance with real clients and investigation of lost rows in SQLite

Date: 2026-10-10, America/Sao_Paulo. Status: **completed and validated in the test VM** after
the sequence of rounds described below; kept in full as a record of the investigation.

## Decisions in this stage

- No external memory project enters the M2 criterion; all of them enter M4 as benchmark
  objects. Specification and ADR-0007 annotated (see journal 0014).
- Acceptance with git, SQLite in WAL and an in-house memory client (tmp + attributes + fsync +
  rename + directory fsync convention), plus `dm-log-writes` and `dm-flakey`. SQLite is not
  part of the architecture; it is the most demanding client available for a filesystem and it
  verifies its own content.
- The data flow diagram goes to the site together with the plan.
- The history records decisions, errors and discarded hypotheses; clarification questions and
  answers do not (rule adjusted in `AGENTS.md` on 2026-10-10).

## Phase 2e harness

- `tests/third_party/log-writes/`: xfstests `replay-log` vendored (commit `9b56b94`,
  2019-10-24, GPL-2.0) to replay the `dm-log-writes` log up to a mark.
- `vm/Dockerfile`: `git`, `sqlite3`, `dmsetup`. `vm/in-container.sh`: the `stage_bin` function
  copies dynamic binaries and their libraries (`ldd`) into the initramfs; `git`, `sqlite3`,
  `dmsetup`, `mke2fs`, `e2fsck` now exist in the VM; two more virtio disks (scratch and a
  64 MiB log); modules `dm-mod`, `dm-log-writes`, `dm-flakey` in the chain.
- `vm/init`: acceptance section with git (init, three memory commits, `fsck --strict`,
  `cat-file`), QUERY over the repository, the SQLite experiment (below), power-loss replay at
  five marks and error injection with `dm-flakey`. `tests/ipc.c`: inter-process primitives
  (conflicting `fcntl` locks, `F_GETLK`, `F_SETLKW`, shared mapping in both directions,
  truncate and remapping).

## Results so far (Debian kernel 6.12.111, QEMU VM)

| Test | Result |
| --- | --- |
| git over the layer: init, 3 commits, `fsck --strict`, content via `cat-file` | passed |
| `QUERY` over the git repository | passed |
| `tests/ipc.c` on ext4 directly and on the layer | passed on both |
| SQLite WAL, concurrent writer + reader, ext4 directly (control) | 200/200 |
| SQLite WAL, writer only, layer | 200/200 |
| SQLite WAL, concurrent writer + reader, layer | 184 to 188 of 200, repeatable |
| SQLite rollback (`journal_mode=DELETE`), writer + reader, layer | 200/200 |
| `integrity_check` in all cases | ok |

Rows lost in one round: 13, 27, 42, 45, 49, 58, 73, 80, 87, 130, 141, 145, 181, 188,
193. Isolated and scattered; the writer received success on every insert; no lock error
printed (the SQLite CLI returns 1 on error, verified in the container).

## Errors found along the way and fixes

1. First round: "database is locked" because the SQLite CLI has no `busy_timeout`;
   fixed with `.timeout 10000`. The error itself proved that `fcntl` locks cross the
   layer.
2. `alias` inside the initramfs script is not expanded by the busybox shell; replaced by a
   function.
3. "Crossed" variants (writer through the layer, reader directly on the lower) are invalid by
   design: each path has its own POSIX lock domain (layer inode versus lower inode).
   Discarded; recorded so as not to repeat them.

## Hypotheses, discards and root cause

- Discarded: basic `fcntl` locks (ipc passes; rollback passes). Discarded: basic shared
  mapping (ipc passes). Discarded: loss through a silent client error.
- The `unix-excl` variant (wal-index on heap, exclusive locking) gave 200/200, but that only
  removes the concurrency; it proves nothing about `mmap`. The `unix-dotfile` variant does not
  support WAL (no shm methods) and was discarded as an invalid experiment.
- **Root cause (defect 1, data loss):** when a name is removed and recreated through the layer
  without anyone having opened it, the upper dentry becomes negative but its `d_fsdata`
  keeps pointing to the old lower dentry (kept alive by the layer's reference, with `nlink`
  0). The following `create` created the new file in the lower, but `open` then opened the
  old, already deleted lower. Two processes could use different `-shm`/`-wal` files for the
  same name; a checkpoint erased the other's frames. SQLite's WAL deletes and recreates
  `-wal` and `-shm` constantly, which is why only it exposed the problem; rollback mode also
  opened the old journal, but that does not alter reads. Fix in `moobl_instantiate`: when
  instantiating a negative dentry, replace the lower dentry in `d_fsdata` with the newly
  created one. Regression test in `tests/layer.c` (unlink + recreate with and without an old
  descriptor open; the same for directories). Result after the fix: the six SQLite variants
  at 200/200.
- **Defect 2 (kernel warning, only on x86 CI):** `WARNING at fs/inode.c:294 __destroy_inode`,
  `s_remove_count` mismatch, caused by concurrent calls to `set_nlink` on the same upper
  inode via `fsstack_copy_attr_all` in unlocked paths (`getattr`, `d_revalidate`,
  post-write). Reproduced on the GitHub runner (2 CPUs, no emulation), not in the local VM
  under TCG. Fix: `moobl_copy_attr` copies mode, owner, times and size but not `nlink`;
  `nlink` changes only in name operations, which the VFS serializes by the parent directory
  lock. Pattern inherited from ecryptfs, which does the same copy; worth investigating whether
  the problem exists there.

## Harness errors in the phase (in addition to the previous ones)

4. `$5` inside a shell function is the function's argument, not the caller's; the SQLite VFS
   arrived empty. 5. `busybox --install -s /bin` overwrote the real `mke2fs` with the busybox
   applet; real binaries now live in `/tools`, ahead in `PATH`.

## Harness errors in the phase (continued)

6. Debian's busybox ash is compiled with `PREFER_APPLETS`: a command whose name matches an
   applet (`mke2fs`) runs the applet even with `/tools` ahead in `PATH`; real binaries are now
   called by absolute path. 7. A `sed` that did not match the line made the suite not run and
   the analyzed log be the one from the previous run; detected by the identical timestamps.
8. `dmsetup suspend` on a device that is returning errors fails to flush; switching the table
   back to `linear` uses `--noflush --nolockfs`.

## Durability finding (not a layer defect)

On the first `dm-log-writes` replay the renamed file did not exist at the mark recorded right
after the `rename`. This is the POSIX rule: `rename` is only durable after `fsync` of the
parent directory or the journal commit. The client convention in the specification (§6) now
includes the directory `fsync`; the test client syncs after the `rename`.

## Final result (Debian kernel 6.12.111, QEMU TCG)

```text
LAYER_LOWER_PASS: ext4 reflink=no
LAYER_LOWER_PASS: btrfs reflink=yes
LAYER_LOWER_PASS: xfs reflink=yes
SQLITE_VARIANTS: lower=0 alone=0 layer=0 rollback=0 excl=0 again=0
ACCEPTANCE_CLIENTS_PASS
POWER_LOSS_REPLAY_PASS: marks mkfs m1 m2 m4 end
IO_ERROR_INJECTION_PASS
MOOBFS_ACCEPTANCE_PASS: git, sqlite wal, memory client, dm-log-writes replay, dm-flakey
MOOBFS_LAYER_PASS: … index and QUERY equal to the oracle …
```

- git: repository on the layer, three commits, `fsck --strict` clean, correct content.
- SQLite WAL: 200/200 in all variants, including concurrent writer and reader on the
  layer; `integrity_check` ok.
- `dm-log-writes`: replay onto a zeroed disk up to `mkfs`, `m1`, `m2`, `m4` and `end`; at each
  mark, `e2fsck -fn` clean after journal recovery, memories up to the mark intact (content and
  attributes), later ones absent, `QUERY` with the right count.
- `dm-flakey` with `error_writes`: ext4 errors in `dmesg` as expected, no `BUG`/`Oops`/
  `WARNING`; after the table switch and `e2fsck`, clean remount, earlier content intact,
  `moobfs-check` clean.
- No kernel warning in the local VM with the two fixes. CI of commit `9a37b88` (run
  38070138824, x86 runner with 2 CPUs, where the `nlink` race appeared): tools,
  sanitizers, fuzzing and native VM successful, zero occurrences of `WARNING:` in the log.

## What phase 2e proved

Clients that do not know MoobFS exists, including the most demanding of them, run unchanged
over the layer, over ext4, btrfs and XFS, and survive power loss and disk errors with the
lower's behavior. Two real defects were found and fixed by these tests; none of the earlier
tests would have caught them.

## Limitations

Small SQLite load (200 inserts) and a single concurrency pattern; no full xfstests;
no KASAN/lockdep; `dm-flakey` only in "write errors" mode over ext4; the I/O-with-errors test
does not check the behavior over btrfs and XFS.

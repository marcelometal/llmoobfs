# 0016 — Harness with KASAN and lockdep on our own kernel; data flow diagram on the site

Date: 2026-10-10, America/Sao_Paulo. Status: **in progress**; closed when the first workflow
run completes.

## Request

"Seguir" ("Continue") after phase 2e closed. Items chosen: the small site pending item (data
flow diagram) and the heaviest M2 pending item, validation with KASAN and lockdep.

## Changes

- `site/plan.html`: SVG diagram of a memory's path (write on the left, query and read on the
  right), with an accessible description; validated by `scripts/check-site.py`.
- `vm/suite.sh` (new): the layer suite extracted from `vm/init`, parameterized by binaries
  directory, tools directory, optional modules and five block devices. `vm/init` now defines
  them and includes the suite; the Debian harness was rerun after the refactoring and passed in
  full (`MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`).
- `vm/kasan.config`: fragment with inline generic KASAN, UBSAN, `PROVE_LOCKING`,
  `DEBUG_ATOMIC_SLEEP`, `DEBUG_LIST`, `DEBUG_VM`, ext4/btrfs/XFS built in, device mapper
  with `dm-log-writes` and `dm-flakey`, virtio-blk.
- `vm/kasan-guest.sh`: runs the suite inside the virtme-ng guest (root = host checkout, tools
  from the runner's own Ubuntu via symlink) and fails on any kernel diagnostic in `dmesg`.
- `.github/workflows/kasan.yml`: `workflow_dispatch` and weekly; kernel `linux-6.12.112` from
  kernel.org with pinned SHA-256 (`164dc9d1…ed03`), pinned `virtme-ng 1.41`, 150-minute
  timeout, artifacts with console, `dmesg` and `.config`.

## Decisions

- Our own kernel only in this workflow; the Debian harness remains the reproducibility
  reference and runs on every push. The KASAN workflow is heavier and does not block pushes.
- The upstream 6.12.112 kernel differs from Debian 6.12.111 (which carries backports, such as
  the `backing_file_open` signature); the module is compiled against each kernel's headers, so
  both are legitimate test targets. API divergences between the two will be recorded.

## Evidence and fixes

- Run 38071014790: the 6.12.112 kernel with KASAN compiled in about ten minutes on the runner,
  booted, the five virtio disks appeared and the module loaded. Harness failure: the virtme-ng
  guest root is read-only and the suite created `/lower` and `/mnt/layer`. Fix:
  parameterized mount points (`LOWER`, `LAYER`), on tmpfs in the guest.
- Run 38072014663: KASAN and lockdep active (35 lines in `/proc/lockdep_stats`). **Real defect
  found**: `DEBUG_RWSEMS_WARN_ON(sem->magic != sem)` in `up_write`, coming from
  `moobl_index_destroy` called by `moobl_kill_sb` on mounts that failed before
  `moobl_index_init` (no `lower=`, nonexistent directory, stacking over the layer itself).
  The rwsem and the xarray were destroyed without having been initialized; the Debian kernel,
  without `DEBUG_RWSEMS`, did not flag it. Fix: initialize the index right after allocating
  the superblock, before any error path, and have `moobl_index_destroy` act only if
  initialized. Second failure, harness: the parameterization did not cover options with a comma
  (`lower=/lower,noindex`), fixed.
- Next run attached below.

Note of 2026-10-10: after the history rewrite (journal 0018), the "KASAN and lockdep" workflow
was run again on `main` (run 38074631812, kernel 6.12.112-virtme): `MOOBFS_KASAN_PASS`, with
`LAYER_LOWER_PASS` on ext4, btrfs and xfs, `MOOBFS_ACCEPTANCE_PASS` (git, SQLite WAL, memory
client, `dm-log-writes` replay, `dm-flakey`) and no kernel diagnostic. The regular CI (run
38074616460) also passed.

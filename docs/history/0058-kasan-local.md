# 0058 — The KASAN job runs locally on a KVM host

Date: 2026-10-12. Status: implemented and run once on the benchmark host; the workflow on pull
requests stays the required check.

## Goal

Shorten the KASAN/lockdep feedback loop. In CI each run rebuilds the debug kernel and runs the
suite under QEMU; recent runs took 35 to 75 minutes, about 56 of them in the suite (PR 51).

## Decision

`vm/kasan-local.sh` repeats `.github/workflows/kasan.yml` on any Linux host with KVM: the same
kernel version and SHA-256 (read from the workflow, so they cannot drift), `vm/kasan.config`, test
binaries, disk images, `vm/kasan-guest.sh` and the single retry on a boot-time diagnostic. The
kernel is built once into `~/.cache/moobfs-kasan` and rebuilt only when `vm/kasan.config` changes.
It runs as a normal user in the `kvm` group, with virtme-ng from `vm/kasan-requirements.txt` in a
virtualenv.

Alternative considered: a self-hosted GitHub runner on the same host. Rejected for now: it would
run pull-request code from outside contributors on a machine in the maintainer's network.

## Evidence

Host: Mac mini (Intel Core i7-2635QM, 4 cores and 8 threads, 16 GB), Debian 13, `/dev/kvm`. Tree:
`fix/rename-over-deleted-name` at e82ab64.

- Kernel 6.12.112-virtme with KASAN built in about 28 minutes (first run only).
- First run failed after the kernel build: `mke2fs` is in `/usr/sbin`, outside a normal user's
  `PATH`. The script now appends `/usr/sbin:/sbin`.
- Second run, kernel from the cache: `MOOBFS_KASAN_PASS` on the first attempt in 17.5 minutes from
  start to end (module, tests, images and the whole suite), with `kasan: KernelAddressSanitizer
  initialized` in `dmesg` and none of the diagnostic patterns.

## Limitations

One host and one run. A local pass does not replace the workflow: the required check still runs on
the final revision of each pull request.

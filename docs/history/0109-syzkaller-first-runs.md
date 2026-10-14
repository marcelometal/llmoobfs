# 0109 — syzkaller over the layer: setup and first runs (security roadmap S3)

Date: 2026-10-14. Status: **in progress**; a six-hour run is going on the benchmark host.
Updated at each round.

## Goal

S3 of `docs/security-roadmap.md`: fuzz the layer's mount, ioctls, typed attributes and name
operations with syzkaller, under KASAN, UBSAN and lockdep, and keep reproducers of whatever it
finds. Precedent named by the roadmap: Janus and Hydra, which found hundreds of defects in kernel
filesystems by fuzzing.

## What was built

Read first: syzkaller's `docs/linux/setup_ubuntu-host_qemu-vm_x86-64-kernel.md`,
`docs/linux/kernel_configs.md` and `docs/syscall_descriptions.md`.

- `vm/syzkaller/kernel.config`: the options those documents name (KCOV with comparisons, no
  KASLR, configfs and securityfs for the image, debug information, hang detectors), merged with
  `vm/kasan.config` on top of `defconfig` and `kvm_guest.config`, for the pinned kernel of the
  KASAN job (6.12.112), in its own cache (`~/.cache/moobfs-syz`).
- `vm/syzkaller/moobfs.txt` and its extracted constants `moobfs.txt.const`: syzlang descriptions
  of the layer's mount with its options (`fs_options`, as `mount$tmpfs` does), the four ioctls
  with their structures laid out as the C compiler lays them out (sizes checked against
  `include/moobfs_ioctl.h`), typed attribute writes and reads with valid and broken names and
  encodings of every type.
- `vm/syzkaller/seeds/`: two seed programs that mount the layer, write typed attributes, rename,
  and issue `QUERY2`, `QUERY2_BATCH`, `GET_INFO` and `QUERY`; checked by `syz-db pack`.
- `vm/syzkaller/image-setup.sh`: the module in syzkaller's Debian image, loaded at boot.
- `vm/syzkaller-local.sh`: kernel, module, image, descriptions and run, step by step; saves the
  manager's log, the crashes and a summary in `build/syzkaller/`.

syzkaller itself is a checkout of its main branch at `8fb0ae89890497b4` (2026-10-14), built with
Go 1.26.9 (checksum checked against go.dev).

## Errors on the way

Each a failed attempt first, then the change:

- The module did not link against the fuzzing kernel: only `bzImage` and `modules_prepare` had
  been built, so `Module.symvers` was missing; the kernel's modules are built now.
- syzkaller's generator refused two comments that began with the word `include` ("confusing
  comment faking a directive"); rephrased. `make generate` needs `clang-format` and `flatc` for
  syzkaller's own code; `make descriptions` regenerates only the system call descriptions.
- First design: the layer mounted in the image at a fixed path, with fixed paths in the
  descriptions. syzkaller's executor runs every program in a private root, where that path does
  not exist, and it turned the layer's calls off. Now each program mounts the layer itself, on a
  directory it created, with a relative `lower=`; this also lets the fuzzer vary the mount and its
  options. tmpfs needs `CONFIG_TMPFS_XATTR` for the attributes.
- Enabling `openat` and `setxattr` in the manager enabled every variant of them (devices,
  cgroups, sysfs, overlay attributes): the first run spent its time elsewhere. The list now holds
  the layer's variants and generic calls without variants.
- The per-function coverage page answers only once the fuzzer has triaged its first programs; a
  loop on the host saves it every 30 minutes.

## Results so far

| Run | Executions | Corpus | Programs that mount the layer | ... and query | Crashes |
| --- | --- | --- | --- | --- | --- |
| 15 min, no seeds, every `openat` variant | 15 736 | 382 | 6 | 3 | 0 |
| 30 min, seeds, restricted list | 78 055 | 844 | 326 | 109 | 0 |

In the 30-minute run the corpus reaches every ioctl (`QUERY2` 39 programs, `QUERY2_BATCH` 34,
`QUERY` 56, `GET_INFO` 24), typed attribute writes and removals (246), renames (69), hard links
(101) and unmounts (34). No report from KASAN, UBSAN or lockdep. Per-function coverage of
`layer/` is not measured yet.

## Next step

The six-hour run, started 17:36; then per-function coverage of `layer/`, any crash reproduced and
kept in `tests/`, and the threshold the roadmap asks for.

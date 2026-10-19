# 0137 — arm64 under emulation: the guest boots and the ext4 step passes under KASAN

Date: 2026-10-19. Status: the arm64 job (`kasan-arm64.yml`, exploratory, not a required check)
boots the KASAN kernel under TCG and passed the suite step ext4 once (run 39265856488,
`MOOBFS_KASAN_ONLY_PASS`). Branch `arm64-boot`; no pull request. Estimated effort: M.

## Goal

Get the arm64 job past boot, so the layer suite runs on arm64 under KASAN (journal 0124: the guest
panicked 9 s into boot with "kernel stack overflow" in `modprobe`).

## Findings, in the order the runs gave them

1. **The boot failure was the guest's init, not the 9p root by itself.** The wheel of virtme-ng
   1.41 (`virtme_ng-1.41-py3-none-any.whl`, the one pinned in `vm/kasan-requirements.txt`) carries
   `virtme/guest/bin/virtme-ng-init` built for x86-64 only (`file`: "ELF 64-bit LSB pie
   executable, x86-64"). virtme-ng uses it whenever guest and host architectures match. On arm64
   the kernel cannot execute it, and the panic line of run 39235747683 says so: "Requested init
   .../virtme-ng-init failed (error -8)". Before failing, the kernel asks `modprobe` for a binfmt
   handler; in run 39141437064 (journal 0124) that `modprobe` (PID 58, 1.4 s after "Run
   .../virtme-ng-init as init process") was read from the 9p root and overflowed the stack in
   the netfs read-retry recursion. The recursion is real, but it was reached only through this.
   Fix: `vng --no-virtme-ng-init`, the bash `virtme-init` of the same package.
2. **The root is shared over virtio-fs.** With the Ubuntu package `virtiofsd` (1.10.0) installed,
   virtme-ng shares the root over virtio-fs instead of 9p ("VFS: Mounted root (virtiofs
   filesystem)"). A `--rwdir` is always 9p in virtme-ng 1.41, so the job uses `--rw` and no
   `--rwdir`: `build/` is reached through the virtio-fs root, written as the runner user, and
   `/tmp` stays a guest-only overlay. Whether 9p alone would now work was not tested; virtio-fs
   keeps the module and the test programs off the netfs read path.
3. **Disk order is reversed.** QEMU's arm `virt` machine gives virtio-mmio devices to the guest
   in the reverse order of the command line: in run 39238405300 `lower-ext4.img` (64 MiB) became
   `vde` and `vda` was `log.img` ("EXT4-fs (vda): VFS: Can't find ext4 filesystem",
   `MOOBFS_VM_FAIL: lower-mount-ext4`). The job passes the disks in reverse, so
   `kasan-guest.sh` sees the order of x86 unchanged.
4. **User-space crashes under emulation.** With the three fixes above, the suite starts, the
   module loads, the lower ext4 mounts and `LAYER_PASSTHROUGH_TESTS_PASS` is printed. Then a
   test program dies:
   - run 39241490729: "Segmentation fault" of `moobfs-attr set ... mix int64 -3`,
     `MOOBFS_VM_FAIL: q-mix-set-6-ext4`, nothing in dmesg;
   - run 39258531656 (fatal signals logged): "query[348]: unhandled exception: undefined
     instruction in query[1a800,400000+8a000]", `MOOBFS_VM_FAIL: insp-cleanup-query-ext4`, an
     earlier step than in the run before.
   The same `query` built with the same compiler (gcc 13.3.0, Ubuntu 24.04, arm64) has at
   0x41a800 an SVE store (`st1b {z0.b}, p0, [x3]`) inside glibc's `__memset_a64fx`, 0x100 bytes
   into the function. The guest CPU is a Cortex-A57 (MIDR 0x411fd070, no SVE), so glibc never
   selects that memset: control flow went wrong. `moobfs-attr` with the same arguments runs
   cleanly on native arm64 (a container on an Apple-silicon host), and it makes no call that
   copies data from the module into its memory. No KASAN, UBSAN or lockdep report accompanied
   either crash. All test programs are static and mapped at 0x400000, which fits a stale
   user mapping between processes. The guest log shows "kernel page table isolation forced ON
   by KASLR" (a Cortex-A57 does not need KPTI).
   - run 39262287911, booted with `kpti=0` ("kernel page table isolation forced OFF by kpti
     command line option"): "moobfs-index[312]: unhandled exception: DABT (lower EL), ESR
     0x0000000092000006, level 2 translation fault in moobfs-index[1ab30,400000+87000]",
     `MOOBFS_VM_FAIL: insp-typed-query-ext4`. `moobfs-index` is the user-space oracle; it reads
     the lower tree directly and does not use the layer. In the same build, 0x41ab30 holds `bl
     __brk` in `__sbrk`, an instruction that cannot raise a data abort: what ran at that address
     was not the file's instruction. KPTI is ruled out.
5. **From a guest tmpfs the programs run correctly.** Run 39265856488 copies the test programs
   (everything in `build/vng` but the disk images) to `/run` in the guest, checks them against
   SHA-256 sums written by the host before boot ("ARM64_BIN_COPY: checksums equal to the host")
   and binds the copy over `build/vng`. The ext4 step then passes: `LAYER_PASSTHROUGH_TESTS_PASS`,
   `RECONF_PASS`, `QUERY2_PASS: ext4`, `ISOLATION_PASS: ext4`, `EQUIV_PASS: 9 shapes`,
   `BENCH_LAYER_PASS: ext4`, `HOSTILE_LOWER_PASS: ext4, 3 trees`, `LAYER_LOWER_PASS: ext4
   reflink=no`, `MOOBFS_KASAN_ONLY_PASS: steps ext4 under KASAN, UBSAN and lockdep with no kernel
   diagnostics (not a complete run)`. The bytes read over virtio-fs were right, so the fault is in
   executing from virtio-fs pages under TCG (the page cache of the FUSE mapping, or QEMU's
   translation of those pages), not in the files. Which of the two was not established.

## Changes (CI only; `layer/` untouched; `kasan.yml` and the shared scripts unchanged)

`.github/workflows/kasan-arm64.yml`: installs `virtiofsd` and checks its version; `vng` with
`--rw` instead of `--rwdir`, `--no-virtme-ng-init`, the disks in reverse order; before the guest
script the guest logs fatal signals (`kernel.print-fatal-signals`, `debug.exception-trace`) and
runs the test programs from a checked tmpfs copy; `CONFIG_VIRTIO_FS` in the configuration check;
a warning when the root was not virtio-fs (matched on "Mounted root (virtiofs filesystem)": the
printed kernel command line has no `rootfstype=`, so the first form of the check warned wrongly
in run 39265856488). `kpti=0` was tried and removed. Checked with actionlint 1.7.7; no shell
script changed.

## Runs (workflow_dispatch on `arm64-boot`)

| Run | Change | Result |
| --- | --- | --- |
| 39235747683 | virtio-fs root | root on virtio-fs; init failed, error -8 |
| 39238405300 | + bash init | boots; script starts at 170 s; lower-mount-ext4 (disk order) |
| 39241490729 | + disks reversed | LAYER_PASSTHROUGH_TESTS_PASS; SIGSEGV in moobfs-attr |
| 39258531656 | + fatal signals logged (after merging `main` at 2808f5c) | SIGILL in query |
| 39262287911 | + `kpti=0` | SIGSEGV (translation fault) in moobfs-index |
| 39265856488 | `kpti=0` removed; programs from a guest tmpfs | ext4 step passes, 16 min |

Each run: about 20 minutes of kernel build, 2.5 minutes of guest boot under TCG (udev settle
alone about 60 s). The passing run took 36 minutes in all, 16 of them in the suite step.

## Limitations

One passing run of one suite step: btrfs, XFS, the fault-injection block and the other steps
have not run on arm64, and a single pass does not show the crashes are gone for good. The guest's
`dmesg.txt` now stays in the guest's tmpfs, so the evidence artifact holds only the console
(which carries the guest's output and kernel log). The exact cause of the crashes from virtio-fs
(guest kernel or QEMU) is not established; nothing was reported upstream. Emulation only: no
arm64 host with KVM was used.

## Next step

A pull request from `arm64-boot`, then dispatches with more steps (`btrfs xfs`, `faults`) to
learn what fits the 300-minute limit; update the site's status row on architectures when a
result is merged (rule of journal 0122).

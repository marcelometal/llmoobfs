# 0042 — New measurement host: Mac mini Server 2011 with Debian 13

Date: 2026-10-11 and 2026-10-12. Status: host prepared and validated with the complete suite; no
performance measurement yet.

## Context

The Azure VM used in journals 0026 to 0031 was shut down by the maintainer. The new host is a
local machine of the maintainer. Address, user and key are not recorded here.

## Hardware and system

| Item | Value |
| --- | --- |
| Machine | Mac mini Server Mid 2011 |
| CPU | Intel Core i7-2635QM, 4 cores, 8 threads, 2.0 GHz |
| Memory | 16 GB DDR3 1333 |
| System disk | 500 GB HDD, 2.5", rotational (Seagate ST9500420ASG) |
| Test disk | OCZ Vertex 4 128 GB SSD, SATA 6 Gb/s; SMART: passed, remaining life 92%, 0 reallocated sectors |
| System | Debian 13.7, default kernel `6.12.111+deb13-amd64` (the same as the suite's VM) |
| Test volumes | three 20 GB GPT partitions on the SSD: ext4, btrfs, XFS, formatted by `bench/host-setup.sh` |

Differences from Azure: a different CPU (Sandy Bridge, 2011) and local disks (SATA SSD instead of
a P6 managed disk). Absolute numbers are not comparable with journals 0026 to 0031; the
comparisons of the Q2 series will be made within this host, including a new v1 baseline
measurement. No custom kernel: Debian's default kernel is already from the validated 6.12 series.

## Preparation

1. The SSD had macOS (APFS, 64 GB) and an NTFS partition (55 GB). Before wiping it, with
   the maintainer's authorization, a complete compressed image was made on the HDD (`zstd -3`):
   128 GB read in 902 s, a 23.8 GB image, verified by the equality between the SHA-256 of the disk
   read and that of the decompressed image (total verification of 1 592 s). Restore instructions
   are next to the image, on the host itself.
2. The host clock was about 49 minutes ahead and without NTP; `systemd-timesyncd` was
   installed and synchronization turned on before any measurement.
3. `sudo` and the kernel headers (`linux-headers-$(uname -r)`) did not come with the minimal installation;
   they were installed. `bench/host-setup.sh` does not install the headers when the kernel is the
   distribution's; left as an improvement.
4. First run of `bench/host-setup.sh`: failed before compiling, because the script wrote
   the compilation log into `build/` without creating the directory (fresh copy of the tree, without `build/`).
   Fixed with `mkdir -p` (commit on the Q2d branch).

## Evidence

Second run of `bench/host-setup.sh`, with the tree of branch `m3/query-v2-q2d` (Q2a to Q2d):
`HOST_SETUP_PASS` on kernel `6.12.111+deb13-amd64`, with `QUERY2_ORDER_PASS`,
`QUERY2_MISSING_BATCH_PASS`, `QUERY2_LEAPFROG_PASS`, `QUERY2_PASS`, `ISOLATION_PASS`, `EQUIV_PASS`,
`BENCH_LAYER_PASS` and `LAYER_LOWER_PASS` on ext4, btrfs and XFS, `MOOBFS_ACCEPTANCE_PASS` (git,
SQLite WAL, memory client, `dm-log-writes` replay, `dm-flakey`) and `MOOBFS_LAYER_PASS`;
no `BUG:`, `Oops:` or `WARNING:` line in `dmesg`. This is the first run of the Q2 series on
real hardware, outside a VM.

## Next step

Measurements on this host: v1 baseline (W3 of the series of journal 0029), per-domain W3 with Q2,
"N most recent", intersection (Q2c against Q2b, alternating modules) and the timing channel test
(`bench/timing-v2.sh`).

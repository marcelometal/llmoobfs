# Azure pilot, scale S1 (2026-10-11)

**A procedure check, not an M4 result.** 1000 memories, profile 8, 3 repetitions, 4 processes,
on Premium SSD P6 disks with credit bursting. Run with the harness at commit `b580488`, which
still executed each W3 shape once per repetition and did not separate shapes in the report;
both were fixed afterwards (journal 0025). Use these files to check the procedure and to
compare with later campaigns, never as a conclusion.

| Item | Value |
| --- | --- |
| Host | Azure Standard_D4as_v4, 4 vCPU AMD EPYC 7763, 16 GB, used as the host (no nested VM) |
| Kernel | `6.12.112-moobfs-bench-moobfs-bench` (`bench/kernel-build.sh`, upstream tarball with the KASAN workflow's SHA-256) |
| Disks | three Premium SSD P6 64 GB, host cache None, one per filesystem; baseline and layer of each filesystem on the same disk |
| Systems | A-ext4, B-btrfs, X-xfs (plain directory plus SQLite baseline); C-ext4, C-btrfs, C-xfs (moobfs_layer over a directory of the same disk) |
| Command | `BIN=build/host sh bench/run.sh --out build/bench/pilot-s1 --scale 1000 --profile 8 --reps 3 --procs 4 --plain A-ext4=... --layer C-ext4=...:...` |

Files:

- `environment.txt`, `run.log`, `report.txt`, `results.csv`, `results.json`: campaign output.
  `environment.txt` says `kvm: absent` because the host is itself a cloud VM without nested
  virtualization; that field only matters for the nested-VM path.
- `rep*/<system>/`: equivalence (`equiv.txt`), `moobfs-check` output, `GET_INFO` for the layer,
  `/proc/diskstats` before and after.
- `calibration/fio-*.json`: 4 KiB random writes each followed by fsync, 30 s per disk.
- `gate/suite.log`, `gate/suite-dmesg.txt`: the full suite on this host and kernel
  (`HOST_SETUP_PASS`).
- `manifest.sha256`: hash of the dataset manifest, regenerable with
  `bench-gen --seed 1 --inodes 1000 --profile 8`.

The raw latency records (`*.rec`), the SQLite baseline databases and the manifest are in
[`2026-10-11-azure-pilot-s1-raw.tgz`](https://github.com/studiomoob/llmoobfs/releases/download/bench-evidence/2026-10-11-azure-pilot-s1-raw.tgz) in the GitHub release
`bench-evidence` (SHA-256
`1755901556b389e24bfcdf8ace1caaff55bc69d82b2377c010136bb00d26985f`). Originally attached to the pre-release `bench-2026-10-11-azure-pilot-s1`; that tag remains.

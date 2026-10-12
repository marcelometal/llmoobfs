# Remount time at 25,000 to 60,000 memories (2026-10-11)

Complements the scale series (`../2026-10-11-azure-query-scale/`), whose W7 had no value from
25k on. Same host and disks (Premium SSD P6 with credit bursting, likely depleted after the
series), module from `main` with the fixes of journals 0027 and 0028, loader with the W7 fix
(a truncated first answer counts as the index being ready). For each scale: dataset written
with `bench-load` W1 (seed 1, profile 8), then `bench-load --workload W7 --repeat 5` (warm page
cache) and `--workload W6 --repeat 3` (page cache dropped before each mount), first query
`kind eq string decision score gt int64 500`.

`summary.txt` has median and all runs per scale; `info-*.txt` has `GET_INFO` after the runs.
Raw records and manifests: [`2026-10-11-azure-remount-raw.tgz`](https://github.com/studiomoob/llmoobfs/releases/download/bench-evidence/2026-10-11-azure-remount-raw.tgz) in the GitHub release
`bench-evidence` (SHA-256
`396983b28a34df1cad30d5729c5dab541b0bd6850167cc290de6b8688ed41f04`). Originally attached to the pre-release `bench-2026-10-11-azure-remount`; that tag remains. Interpretation:
journal 0031. The W6 numbers depend on the disk tier and its burst state; they are
an upper bound for this disk, not a property of the layer alone.

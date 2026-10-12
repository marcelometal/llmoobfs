# Azure pilot 2, scale S1 (2026-10-11)

**A pilot, not an M4 result.** Same host, kernel and disks as
[pilot 1](../2026-10-11-azure-pilot-s1/README.md): Azure Standard_D4as_v4, kernel
`6.12.112-moobfs-bench-moobfs-bench`, three Premium SSD P6 disks with credit bursting. Harness
at commit `d131a9b`, after the method fixes of journal 0025: every W3 shape runs 50 times per
repetition, query records carry the shape (Q1–Q9), SQLite returns `ino` and `path` like
`QUERY`, and `QUERY` is also measured as a regular user (`nobody`, systems `C-*+u`).

Command: `W3_REPEAT=50 BIN=build/host sh bench/run.sh --out build/bench/pilot2-s1 --scale 1000
--profile 8 --reps 5 --procs 4` with the same six systems as pilot 1. Calibration with
`bench/calibrate.sh` is in `calibration/`.

Files: `environment.txt`, `run.log`, `report.txt`, `results.csv`, `results.json`,
`calibration/`, `rep1..rep5/<system>/` (equivalence, `moobfs-check`, `GET_INFO`, diskstats),
`manifest.sha256`. Raw records, SQLite databases and the manifest are in
[`2026-10-11-azure-pilot2-s1-raw.tgz`](https://github.com/studiomoob/llmoobfs/releases/download/bench-evidence/2026-10-11-azure-pilot2-s1-raw.tgz) in the GitHub release
`bench-evidence` (SHA-256
`d2ad07362d97597a855193fb0c530fd56a74403f5319ddec015507461cac5261`). Originally attached to the pre-release `bench-2026-10-11-azure-pilot2-s1`; that tag remains.

Interpretation is in journal 0025 ("Piloto 2"). In short: every equivalence passed; W1 is
bounded by the disk's fsync and equal with and without the layer; the layer adds a few
microseconds to reads and roughly 14 to 28 µs to attribute writes; `QUERY` is 2 to 9 times
slower than the SQLite baseline per shape at this scale, explained by a fixed per-call cost, a
linear scan of the first term and about 2 µs per returned result for path resolution and
authorization.

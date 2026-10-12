# QUERY scale series, ext4, 1000 to 60,000 memories (2026-10-11)

**A pilot-grade series, not an M4 result.** Azure Standard_D4as_v4, kernel
`6.12.112-moobfs-bench-moobfs-bench`, Premium SSD P6 with credit bursting (affects W1 only; W3 runs
from memory). Module and harness from commit `f4f98b0` (main with the QUERY fast path and RCU-walk
revalidation, plus the query-only mode). Systems: A-ext4 (plain directory plus SQLite with
composite indexes) and C-ext4 (moobfs_layer over a directory of the same disk), plus C-ext4+u
(QUERY as an unprivileged user).

Command per scale S: `QUERY_ONLY=1 W3_REPEAT=50 BIN=build/host sh bench/run.sh --out
build/bench/scale-S --scale S --profile 8 --reps 3 --procs 4 --plain A-ext4=... --layer
C-ext4=...:...` for S in 1000, 5000, 10000, 25000, 40000, 60000 (`scale.status` has the end time of
each).

Per scale directory `sS/`: `environment.txt`, `run.log`, `report.txt`, `results.csv`,
`results.json`, `rep*/<system>/` (equivalence, `moobfs-check`, `GET_INFO` after W1 and after W7),
`manifest.sha256`. Raw latency records, SQLite databases and manifests (123 MB):
[`2026-10-11-azure-query-scale-raw.tgz`](https://github.com/studiomoob/llmoobfs/releases/download/bench-evidence/2026-10-11-azure-query-scale-raw.tgz) in the GitHub release
`bench-evidence`
(SHA-256 `c35cd91fe1ce6f3e6a23ae5974a566daf5bd05cbb70fbf09e729280e3570a7ae`). Originally attached to the pre-release `bench-2026-10-11-azure-query-scale`; that tag remains.

Known issues in this series:
- Shapes whose answer exceeds 4096 results are truncated by the QUERY UAPI v1; they show as
  `err=50` for C-ext4 and are not comparable. Equivalence for them covers SQLite against the
  oracle only (`EQUIV_TRUNCATED`).
- W7 (remount until the first QUERY) used the widest shape as its first query; from 25,000
  memories on that answer was truncated and the loader recorded the mount as an error, so W7 has
  no value for 25k, 40k and 60k. Fixed afterwards (a truncated first answer now counts as the
  index being ready).

Interpretation: journal 0029.

# Azure pilot 3, scale S1 (2026-10-11)

**A pilot, not an M4 result.** Identical to [pilot 2](../2026-10-11-azure-pilot2-s1/README.md)
(same host, kernel, disks, dataset seed, command and harness), except that the layer module
was built from commit `b85d041`: the harness of PR 20 merged with the `QUERY` fast path of
journal 0027 (seek to the first candidate, early stop, results copied one at a time). The only
intended difference between pilots 2 and 3 is therefore the `QUERY` implementation.

Files as in pilot 2: `environment.txt`, `run.log`, `report.txt`, `results.csv`, `results.json`,
`calibration/`, `rep1..rep5/<system>/`, `manifest.sha256`. Raw records, SQLite databases and
the manifest: [`2026-10-11-azure-pilot3-s1-raw.tgz`](https://github.com/studiomoob/llmoobfs/releases/download/bench-evidence/2026-10-11-azure-pilot3-s1-raw.tgz) in the GitHub release
`bench-evidence` (SHA-256
`912ed03d1f5d708dabf2fed2eac89e5dac5a1ae67e9800ca78b1c47c0ef4968e`). Originally attached to the pre-release `bench-2026-10-11-azure-pilot3-s1`; that tag remains.

Interpretation is in journal 0027. In short: the fixed per-call cost and the linear scan are
gone (a query with no match went from 54 µs to 4.9 µs; a 3-result query from 141 µs to 34 µs),
and what remains is about 2 µs per returned result for path resolution and authorization.
Non-query workloads are unchanged from pilot 2, as expected.

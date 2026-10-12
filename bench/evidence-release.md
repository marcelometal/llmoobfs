**Raw data from benchmark campaigns. This is not a software release of MoobFS**: it has no
binaries, and no performance claim comes with it. The only source snapshot is the
`v0.1.0-experimental` tag.

Each archive holds the raw records of one campaign: per-call latency records (`*.rec`), SQLite
baseline databases, dataset manifests and, for the profiles, `perf.data`. The summaries (reports,
CSV and JSON results, environment, equivalence checks) and how each campaign ran are versioned in
[`bench/results/`](https://github.com/studiomoob/llmoobfs/tree/main/bench/results), one directory per
campaign. The reading of the results is in the technical journal. Every campaign listed here is
pilot-grade on a single cloud VM. None of them is an M4 result.

| Archive | Campaign (`bench/results/…`) | Journal | Original tag | Bytes | SHA-256 |
| --- | --- | --- | --- | --- | --- |
| `2026-10-11-azure-pilot-s1-raw.tgz` | `2026-10-11-azure-pilot-s1` | 0025 | `bench-2026-10-11-azure-pilot-s1` | 3765327 | `1755901556b389e24bfcdf8ace1caaff55bc69d82b2377c010136bb00d26985f` |
| `2026-10-11-azure-pilot2-s1-raw.tgz` | `2026-10-11-azure-pilot2-s1` | 0025 | `bench-2026-10-11-azure-pilot2-s1` | 6316433 | `d2ad07362d97597a855193fb0c530fd56a74403f5319ddec015507461cac5261` |
| `2026-10-11-azure-pilot3-s1-raw.tgz` | `2026-10-11-azure-pilot3-s1` | 0027 | `bench-2026-10-11-azure-pilot3-s1` | 6325871 | `912ed03d1f5d708dabf2fed2eac89e5dac5a1ae67e9800ca78b1c47c0ef4968e` |
| `2026-10-11-azure-pilot4-s1-raw.tgz` | `2026-10-11-azure-pilot4-s1` | 0028 | `bench-2026-10-11-azure-pilot4-s1` | 7258828 | `392a5c76f65e3018bf0f3403830710b9e39dccedacb2bb948129f0be62bbfde5` |
| `2026-10-11-azure-profile-q7.tgz` | `2026-10-11-azure-profile-q7` | 0028 | `bench-2026-10-11-azure-profile-q7` | 1093253 | `6357fa1c96d344b2bebe29b4397fcc08e7d893541fb637d331ceb16cd2d3f805` |
| `2026-10-11-azure-query-scale-raw.tgz` | `2026-10-11-azure-query-scale` | 0029 | `bench-2026-10-11-azure-query-scale` | 129437302 | `c35cd91fe1ce6f3e6a23ae5974a566daf5bd05cbb70fbf09e729280e3570a7ae` |
| `2026-10-11-azure-remount-raw.tgz` | `2026-10-11-azure-remount` | 0031 | `bench-2026-10-11-azure-remount` | 8216797 | `396983b28a34df1cad30d5729c5dab541b0bd6850167cc290de6b8688ed41f04` |

The original tags remain in the repository. Each one marks the commit of the branch it came
from, as when it was a separate pre-release. Journal 0041 records how the archives moved here.

Check an archive after downloading it:

```sh
curl -fLO https://github.com/studiomoob/llmoobfs/releases/download/bench-evidence/2026-10-11-azure-remount-raw.tgz
sha256sum 2026-10-11-azure-remount-raw.tgz   # compare with the table above
```

New campaigns are appended to this release with `scripts/bench-evidence-release.sh add`. That
command checks the size and SHA-256 of each archive after upload.

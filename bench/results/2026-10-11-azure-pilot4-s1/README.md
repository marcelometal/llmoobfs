# Azure pilot 4, scale S1 (2026-10-11)

**A pilot, not an M4 result.** Identical to pilot 3 (host, kernel, disks, seed, command,
harness), except that the layer module was built from commit `28f6efa`: the measurement tree
of pilot 3 plus the RCU-walk dentry revalidation of journal 0028. A first attempt aborted on the
equivalence check because a profiling dataset had been left under the ext4 layer root
(journal 0025); no numbers came from it.

Files as in pilots 2 and 3. Raw records, SQLite databases, manifest and the post-fix `perf.data`:
[`2026-10-11-azure-pilot4-s1-raw.tgz`](https://github.com/studiomoob/llmoobfs/releases/download/bench-evidence/2026-10-11-azure-pilot4-s1-raw.tgz) in the GitHub release
`bench-evidence`
(SHA-256 `392a5c76f65e3018bf0f3403830710b9e39dccedacb2bb948129f0be62bbfde5`). Originally attached to the pre-release `bench-2026-10-11-azure-pilot4-s1`; that tag remains. The post-fix
inclusive profile is `../2026-10-11-azure-profile-q7/perf-children-rcu.txt`.

Interpretation in journal 0028: the path walk now stays in RCU-walk, and queries returning many
results are 12 to 16% faster than in pilot 3 (per-result cost from about 2.0 to 1.7 µs); queries
with few results and plain reads change little.

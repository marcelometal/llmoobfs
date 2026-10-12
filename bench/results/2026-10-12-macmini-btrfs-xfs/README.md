# QUERY2, v1 and SQLite on btrfs and XFS — Mac mini, 2026-10-12

Same host, dataset parameters and scripts as `../2026-10-12-macmini-q2/` and
`../2026-10-12-macmini-sqlite/`, on the btrfs and XFS partitions of the test SSD (journal
0042). Module: branch `m3/query-v2-recent` (journal 0048). Driver: `multi-fs.sh` (runs
`bench/q2-campaign.sh` then `bench/sqlite-baseline.sh` on each file system; log in
`multi-fs-2026-10-12.log`). Analysis: journal 0049.

| File | Content |
| --- | --- |
| `campaign-<fs>.csv` | v1 as root and as uid 1001, QUERY2 on `ns3`, `ns1` and "N most recent" as uid 1001 |
| `sqlite-<fs>.csv` | SQLite baseline, statements Q1..Q13 as in `../2026-10-12-macmini-sqlite/README.md` |
| `equivalence-<fs>.txt` | v1 and QUERY2 return the same inodes for Q3 and Q4 |

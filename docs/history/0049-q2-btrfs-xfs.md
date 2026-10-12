# 0049 — `QUERY2`, v1 and SQLite on btrfs and XFS

Date: 2026-10-12. Status: measured on the host of journal 0042; results in
`bench/results/2026-10-12-macmini-btrfs-xfs/`.

## Goal

Journals 0044, 0047 and 0048 measured only on ext4. Repeat the campaign of v1 against v2 and the
SQLite baseline on the btrfs and XFS partitions of the same SSD, with the module from the optimization of journal 0048.

## Results (p50; same 60 000 memories, profile 8, seed 1)

| Question | System | ext4 | btrfs | XFS |
| --- | --- | --- | --- | --- |
| namespace = ns3 and agent = a07 (253) | v1 as user | 4.14 ms | 3.71 ms | 2.75 ms |
| | SQLite | 178 to 476 µs | 178 to 474 µs | 172 to 407 µs |
| | v2 | 119 to 144 µs | 145 µs | 145 µs |
| namespace, kind and created (1 021) | v1 as user | 5.14 ms | 4.35 ms | 3.32 ms |
| | SQLite | 1 729 µs | 1 719 µs | 1 867 µs |
| | v2 | 569 to 644 µs | 684 µs | 697 µs |
| 20 most recent, kind = note | SQLite | 47 µs | 47 µs | 68 µs |
| | v2 | 19 to 26 µs | 24 µs | 17 µs |
| 20 most recent, agent = a07 | SQLite | 279 µs | 273 µs | 278 µs |
| | v2 | 102 to 141 µs | 122 µs | 90 µs |

The v2 ext4 values are those of journal 0048 (two passes); the btrfs and XFS ones, from one
campaign of 3 repetitions each. v1 and v2 returned the same inodes in the two comparable
forms on the three filesystems.

## Conclusions

- v2 came out ahead of SQLite in all the forms measured, on the three filesystems.
- v2 practically does not depend on the lower filesystem: the index is in RAM and, without
  per-result checking, nothing is read from disk. v1 does depend on it (2.7 to 4.1 ms in Q3), because
  it resolves the path of each result on the lower.
- SQLite also barely varies, because the entire database fits in the cache.

## Limitations

- One campaign per filesystem on btrfs and XFS (three repetitions); noise of the order of 20%
  on values of tens of µs.
- Warm cache, no concurrent writes; the creation of the 60 000 memories (W1) was not analyzed
  here.

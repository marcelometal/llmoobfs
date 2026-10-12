# 0047 — `QUERY2` against SQLite on the same hardware

Date: 2026-10-12. Status: measured on the host of journal 0042; results in
`bench/results/2026-10-12-macmini-sqlite/` and `bench/results/2026-10-12-macmini-q2/`.

## Goal

The question that journals 0043 and 0044 left open: with the adjustments of the Q2 series, how does
`QUERY2` compare with the harness's SQLite baseline (alternative B of ADR-0009), on the same machine and
on the same data. On Azure (journal 0029), SQLite answered Q3 in 0.39 ms and v1 in 6.95 ms,
but those were a different host and different code.

## Method

`bench/sqlite-baseline.sh` (new): database from `bench/sqlbase.sh` with the harness's composite
indexes, loaded from the same 60 000 files of the campaign of journal 0044, on the same SSD;
queries through the library (`bench-sqlclient`), 3 repetitions of 50 executions. The v2 forms
were written in SQL with the domain as the `namespace` column (`extra.sql`). Both sides
return the inode number and path of each result.

## Results (p50; v2 with the revised plan of journal 0044)

| Question | SQLite | `QUERY2` | Who wins |
| --- | --- | --- | --- |
| namespace = ns3 and agent = a07 (253) | 178 to 476 µs | 127 to 162 µs | v2, by 1.1 to 3.7 times |
| namespace = ns3, agent = a07 and kind = note (98) | 509 µs | 88 to 114 µs | v2, by 4.5 to 5.8 times |
| namespace = ns1, kind = fact and created ≥ T (1 021) | 1 729 µs | 710 to 723 µs | v2, about 2.4 times |
| 20 most recent of ns3 with kind = note | 47 µs | 180 to 191 µs | SQLite, about 4 times |
| 20 most recent of ns3 with agent = a07 | 279 µs | 1 638 to 1 801 µs | SQLite, about 6 times |

The SQLite range in the first row comes from two executions of the same SQL command at different
positions in the cycle: 476 µs right after queries that return 24 000 and 6 000 rows (cold CPU
cache), 178 µs after a similar query. v2 was measured in cycles of small forms.
The fair comparison lies between the two extremes.

## Conclusions

- **Equality and range filters within a domain:** v2 came out ahead of SQLite with
  composite indexes in all the forms measured. This reverses the picture of v1, which lost by 10 to
  18 times on Azure.
- **"N most recent":** SQLite wins. It has a composite index (`namespace, kind, created`)
  that delivers the first 20 already in order, and in the `agent` case it combines index and ordering. v2
  walks the domain's `created` tree and tests the terms on each entry, at about 2.8 µs
  per entry (journal 0044). This is the main remaining weak point, and the candidate optimization
  remains the same: compact per-inode attributes for the term test.
- **Broad forms** (tens of thousands of results): SQLite returns everything in one call (Q1
  with 24 595 rows in 46 ms); v2 limits each call to 4096 results and paginates by cursor.
  The cost of paginating an entire response of that kind was not measured.
- **Cost of maintaining the database:** loading SQLite from the files took 4 s for 60 000
  memories, but a side SQLite needs to stay synchronized with each write, which v2 does in its
  own write path. That cost does not enter this table; it is the argument of ADR-0009.

## Limitations

- Only ext4, one domain per form, no concurrent load nor writes during the queries.
- The position of each form in the cycle affects the CPU cache on both sides; the SQLite and
  v2 cycles were not the same.
- SQLite was used with the harness's indexes, without fine-tuning of `PRAGMA` beyond WAL and
  `synchronous=NORMAL`.

## Next step

Optimize the per-inode term test (the bottleneck of "N most recent") and measure again; measure the
complete pagination of a broad form; repeat on btrfs and XFS.

## Correction note (2026-10-12)

The explanation of the cost of "N most recent" above (about 600 entries at about 2.8 µs per
entry, attributed to the search for the attribute in the inode's list) was wrong. The main cause was the
scan gathering 256 candidates even for a request of 20, which walked about 7 500
entries at about 0.23 µs. Diagnosis, fix and new measurements in journal 0048.

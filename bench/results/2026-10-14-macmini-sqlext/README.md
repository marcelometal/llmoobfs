# The moobfs SQLite virtual table measured (journal 0108)

`python3 bench/sqlext.py build/host/moobfs.so /srv/moob/udx-layer/ns3 /srv/moob/ext4/udx-lower/ns3 20`
on the benchmark host (Debian 13, kernel 6.12.111, SQLite 3.46.1, the module of journal 0102):
one domain of the data of journal 0098 (7 459 memories of profile 8 on ext4), warm, median of 20
runs. Each statement runs on the virtual table (no database: `QUERY2` and `getxattr`) and on an
ordinary in-memory SQLite table holding the same metadata, the "SQLite next to the files"
arrangement; the answers are compared first, and all are equal. Raw output: `run.txt`.

| Statement (rows) | virtual table | ordinary table |
| --- | --- | --- |
| newest 20, `ORDER BY created DESC LIMIT 20` | 0.24 ms | 2.5 ms |
| newest 20 of one kind | 0.25 ms | 1.1 ms |
| two equalities (53) | 0.12 ms | 0.7 ms |
| a bool and a range, counted | 0.15 ms | 0.6 ms |
| OR across two columns (1 714) | 1.9 ms | 1.7 ms |
| IN list of two kinds (4 544) | 6 ms | 2.9 ms |
| newest 20 ordered by created then path | 16 to 78 ms | 1.1 to 2.6 ms |
| `GROUP BY kind` | 71 to 74 ms | 2.7 ms |

What the kernel answers (selective filters, the newest N on a required column) is faster than the
ordinary table. What reads a column on every row (grouping, sorting by two columns, sorting a
whole IN list) pays one `getxattr` per row, about 10 µs here. One host, one data set, one run of
the script; nothing else running on the host was intended, not verified.

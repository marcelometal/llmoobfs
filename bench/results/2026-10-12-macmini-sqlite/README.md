# SQLite baseline on the QUERY2 dataset — Mac mini, 2026-10-12

Same host, dataset and files as `../2026-10-12-macmini-q2/` (60 000 memories, profile 8, seed 1),
read from the lower ext4 directory; the database lives on the same SSD file system, outside the
layer root. SQLite 3.46.1 (Debian), schema and composite indexes of `bench/sqlbase.sh`, WAL,
timed through the library by `bench-sqlclient`. Analysis: journal 0047.

Command: `sh bench/sqlite-baseline.sh --manifest <q2-campaign>/manifest.txt --target /srv/moob/ext4/root --db /srv/moob/ext4/sqlite/base.db --queries <q2-campaign>/q-v1.txt --extra-sql extra.sql --out <dir>`

| Statement | Content |
| --- | --- |
| Q1..Q9 | the nine W3 shapes of `bench/queries-w3.txt` (`queries.sql`) |
| Q10 | `namespace = ns3 AND agent = a07 AND kind = note` |
| Q11 | `namespace = ns3 AND agent = a07` (same SQL as Q3, later in the cycle) |
| Q12 | `namespace = ns3 AND kind = note ORDER BY created DESC LIMIT 20` |
| Q13 | `namespace = ns3 AND agent = a07 ORDER BY created DESC LIMIT 20` |

Load: 60 000 rows in 4 s (`load.txt`). Query plans in `plans.txt`: only Q9 (`weight IS NOT NULL`)
scans the table.

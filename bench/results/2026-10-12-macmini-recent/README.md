# "N most recent" optimization — Mac mini, 2026-10-12

Same host and dataset as `../2026-10-12-macmini-q2/` (60 000 memories, profile 8, domains
`ns0`..`ns7`). `compare.csv` comes from
`sh bench/q2c-compare.sh ... --module before=<shortest-run module> --module after=<this branch> --module before2=... --module after2=...`:
the module of journal 0044 against the module of journal 0048, each loaded twice on the same
data, same QUERY2 shapes (`*-recent` rows: ordered by `created` descending, limit 20, with
`kind = note` (Q1) and `agent = a07` (Q2)). The ordered and paged answers of the new module were
checked against `moobfs-index --domain --order` on the host (14 of 14 equal). Analysis: journal
0048.

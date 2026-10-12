# QUERY2 against v1, and the equality plans — Mac mini, 2026-10-12

Host: journal 0042 (Mac mini Server 2011, Debian 13.7, kernel `6.12.111+deb13-amd64`, ext4 on a
SATA SSD). Dataset: `bench/gen --seed 1 --inodes 60000 --profile 8`, written with W1 directly
under the layer root, so `ns0`..`ns7` are the domains (`domain_depth=1`). Index after the load:
60 000 inodes, 437 882 entries, 36.4 MB (`info.after-w1.txt`). Analysis: journal 0044.

| File | Content | Command |
| --- | --- | --- |
| `campaign.csv` | v1 as root and as uid 1001 (nine shapes), QUERY2 on `ns3` and `ns1` as uid 1001; 3 repetitions of 50 calls | `sh bench/q2-campaign.sh --layer /srv/moob/layer-ext4 --lower /srv/moob/ext4/root --out <dir>` |
| `equivalence.txt` | v1 and QUERY2 return the same inodes for Q3 (253) and Q4 (1021) | same run |
| `compare1.csv` | QUERY2 shapes under the Q2b module and the leapfrog module (Q2c, commit `17de2f6`), reloaded on the same data | `sh bench/q2c-compare.sh ... --module q2d=<ko> --module q2b=<ko> --module q2d2=<ko>` |
| `compare2.csv` | the same under Q2b, leapfrog and the shortest-run plan (journal 0044), each loaded twice | `sh bench/q2c-compare.sh ... --module q2b=<ko> --module leapfrog=<ko> --module shortest=<ko> --module q2b2=<ko> --module leapfrog2=<ko> --module shortest2=<ko>` |
| `campaign-q-*.txt`, `compare2-q-*.txt` | the query lines of each group | |
| `compare2-info-*.txt` | GET_INFO and remount time (about 0.71 s for 60 000 memories, warm) per module | |

`campaign.csv` has no data for `v2-recent`: the first `bench-load` counted every top-20 answer
as an overflow error (fixed in the same branch); the "N most recent" numbers come from the
`*-recent` rows of `compare1.csv` and `compare2.csv`. Shapes Q1, Q2, Q6, Q7 and Q9 of v1 pass the
4096-result limit at this scale and are recorded as errors, as in journal 0029.

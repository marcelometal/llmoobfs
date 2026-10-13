# The user-space baseline against the layer — Mac mini, 2026-10-13

Host and module as in `../2026-10-13-macmini-q2-posix/` (ext4 on a SATA SSD, kernel
`6.12.111+deb13-amd64`, layer code of `v0.3.0-experimental`). Analysis: journal 0094.

Command: `sudo env BIN=build/host sh bench/userspace/compare.sh <plain dir> <layer mount> <its
lower> <out dir> 60000`, with an empty plain directory and a fresh layer over an empty lower on
the same partition. One run; three repetitions of the query step.

| File | Content |
| --- | --- |
| `report.txt` | W1 and W4 as the writer sees them on both, and the five query shapes on both (medians of three repetitions of 50 calls) |
| `baseline.txt` | status of the baseline index right after the writes, the follower's counters, its memory, bytes of index on disk |
| `watch-counters.txt` | the follower's counter lines |
| `equal.txt` | result counts of the oracle, the baseline and the layer for three shapes |
| `layer-info.txt` | `GET_INFO` of the layer after the load |
| `q-ns3.sql`, `q-recent.sql` | the SQL the baseline ran |

Not in the files: after `ANALYZE` on the baseline's database the same queries gave the same
medians within 3 % (journal 0094).

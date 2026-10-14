# ADR-0012, first half: the index built per domain on demand, measured (journal 0098)

`bench/ondemand.sh`, run once with each module on the same mount and data: 60 000 memories of
profile 8 in eight domains (`ns0` to `ns7`, about 7 500 files each; `ns3`, the one queried,
has 7 459) on ext4, SATA SSD,
benchmark host (Debian 13, kernel 6.12.111). `old/` is the module that was loaded before
(code of commit `db8f75b`, journal 0084, index built at mount); `new/` is the working tree of
journal 0098. One run each; the steady-state part has three repetitions of 50 calls.

| | index built at mount | built on demand |
| --- | --- | --- |
| mount, cold cache | 19.6 s, 20.0 s | 0.31 s, 0.31 s |
| mount, warm cache | 0.76 s, 0.76 s | 0.004 s, 0.005 s |
| first `QUERY2` on one domain (7 459 files), cold | 0.020 s, 0.011 s | 2.70 s, 2.68 s |
| first `QUERY2` on one domain, warm | 0.006 s, 0.005 s | 0.100 s, 0.097 s |
| second `QUERY2` (3 030 results) | 6 to 8 ms | 6 ms |
| index memory with one of eight domains in use | 36.4 MB (all eight) | 4.5 MB |
| mount-wide `QUERY` after mount, cold | 0.13 s | 19.7 s |
| mount-wide `QUERY` after mount, warm | 0.003 s | 0.74 s |
| index after everything is built | 36 443 423 bytes, 437 882 entries, 60 000 inodes | the same |

Steady-state `QUERY2` on the built domain, median of the three repetitions (µs):

| Shape (results) | at mount | on demand |
| --- | --- | --- |
| two equalities (253) | 122 | 127 |
| one equality (253) | 76 | 86 |
| one equality (3 030) | 908 | 952 |
| newest 20 of one kind | 28 | 19 |
| newest 20 of one agent | 142 | 99 |

Read with care: the report tool marks every row inconclusive (three repetitions), the two
modules differ by more than this change (every commit from journal 0085 to 0098), and that
nothing else was running on the host was intended, not verified. The first three rows move by 5 to 13 %, inside the spread
between repetitions of the old module for the second; the last two are faster, for a reason
not investigated.

Files: `times.txt` (the script's own log), `environment.txt`, `results.csv` (from
`bench/report.py`). The raw per-call records stay on the host.

# Timing channel, QUERY2 against v1 — Mac mini, 2026-10-12

Spec test 4 of `docs/spec-query-v2.md`, the domain-scoped counterpart of journal 0030 §3.
Host and conditions: journal 0042 (Mac mini Server 2011, Debian 13.7, kernel
`6.12.111+deb13-amd64`, ext4 on a SATA SSD). Code: branch `m3/query-v2-q2d`, commit `17de2f6`
(module built by `bench/host-setup.sh`). Analysis: journal 0043.

Setup: uid 1001 owns 10 files `kind = tc` in domain `dA` (0700); uid 1002 owns N files with
the same attribute in domain `dB` (0700). Uid 1001 runs `kind eq string tc`.

| File | What | Command |
| --- | --- | --- |
| `sweep.csv` | QUERY2 on `dA`, N = 0, 1000, 5000, 20000, 400 calls each, one repetition per N | `sh bench/timing-v2.sh --layer /srv/moob/layer-ext4 --lower /srv/moob/ext4/root --out <dir>` |
| `v1-contrast.csv` | v1 QUERY on the root as uid 1001 at N = 20000, remounted with `domain_depth=0`, 3 repetitions of 100 calls | `bench-load --workload W3 --target <layer> --root <layer> --uid 1001 --queries queries.txt --repeat 100` |
| `v2-repeat.csv` | QUERY2 on `dA` at N = 20000, 3 repetitions of 400 calls, for the run-to-run spread | `bench-load --workload W3 --target <layer>/dA --root <layer>/dA --query2 1 --uid 1001 --queries queries.txt --repeat 400` |

`queries.txt` holds the single line `kind eq string tc`. Columns follow `bench/report.py`.

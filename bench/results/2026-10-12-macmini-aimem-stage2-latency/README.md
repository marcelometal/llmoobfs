# Stage 2 of the real-client test: latency, two small scales

Journal [0065](../../../docs/history/0065-client-stage2-latency.md). Pilot-grade, one run per
scale, **not an M4 result**.

- Host: Mac mini (i7-2635QM, 16 GB), Debian 13, kernel 6.12.111, lower ext4 on an SSD; module of
  `main` with the rename fix; ai-memory 385b81e (2.6.3); SQLite 3.46.1.
- `3x50/`: `sudo sh bench/client/stage2.sh OUT 3 50`, every file from that run.
- `10x200/`: `sudo sh bench/client/stage2.sh OUT 10 200`. `latency.txt` is a second pass of
  `bench-aimem-latency --repeat 200 --cold 10` over the same pages after remounting the layer with
  the same options, made with the final tool (the first pass lacked the `run_p50_us` column and
  gave the same picture).

Files: `workload.txt` (pages written through MCP), `mirror.txt`, `info.txt` (`GET_INFO` as root),
`compare.txt` (answers), `latency.txt` (p50/p95/p99 in µs per mode, shape and side; `open_p50_us`
and `run_p50_us` split the cold samples), `modes.txt`, `env.txt`. The per-sample CSV files stay on
the host.

In `10x200` the database holds 1 905 pages in 11 projects, not 2 000 in 10: ai-memory's scheduled
maintenance ran during the write (journal 0065).

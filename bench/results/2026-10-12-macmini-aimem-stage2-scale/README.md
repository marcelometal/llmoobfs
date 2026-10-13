# Stage 2 of the real-client test: scale, 2 000 to 60 000 pages

Journal [0068](../../../docs/history/0068-client-stage2-scale.md). Pilot-grade, one run per scale,
**not an M4 result**.

- Host: Mac mini (i7-2635QM, 16 GB), Debian 13, kernel 6.12.111, lower ext4 on an SSD; module of
  `main` with the rename fix; ai-memory 385b81e (2.6.3); SQLite 3.46.1. Ten projects per run,
  ai-memory's scheduled maintenance off.
- `write-plain-10x200/`: `sudo PLAIN=1 sh bench/client/stage2.sh OUT 10 200`, the client's wiki on
  plain ext4, workload only.
- `mcp-10x200/`, `mcp-10x600/`: `sudo sh bench/client/stage2.sh OUT 10 200` (and `600`), pages
  written through the client's MCP tool.
- `bulk-10x600/`, `bulk-10x2000/`, `bulk-10x6000/`: `sudo BULK=1 sh bench/client/stage2.sh OUT 10 N`,
  page files written directly and indexed by `ai-memory reindex`. `updated_at` is then correlated
  with the page family; see the journal before quoting `pinned` or `rules` from these.

Files as in the latency campaign, plus `write-cost.txt` (mean time per page of each project, in
write order) for the MCP runs. The per-sample CSV files stay on the host.

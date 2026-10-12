# 0064 — Real-client test, stage 2: tools and first small run

Date: 2026-10-12. Status: tools written; small run passed (3 projects, 150 pages); scale and
latency not measured yet. Local series.

## Tools

- `bench/client/aimem-workload.py`: the W-AIM workload through ai-memory's own MCP
  `memory_write_page`, deterministic per seed: notes, decisions, gotchas, procedures, concepts,
  rules and pinned slots; 15 % of pages expired, 20 % expiring later, 10 % pinned, four tiers.
- `bench/client/aimem-mirror.py`: copies `family`, `tier`, `kind` (ai-memory's own
  `page_kind_expr`), `pinned`, `updated_at` and `expires_at` from ai-memory's SQLite into
  `user.moob.*` on each page file.
- `bench/client/aimem-compare.py`: six shapes per project, ai-memory's SQL against `QUERY2` on the
  project directory: recent, pinned, rules, settled kinds (`QUERY2_BATCH`), stale episodic and
  incremental sync (cursor, pages of 7). Equal pages in equal order, equal `updated_at` as a set.
- `bench/client/stage2.sh`: mounts the layer with `domain_depth=2` as ai-memory's `wiki`, runs
  `ai-memory serve` (zero-LLM) under `umask 077`, then workload, mirror and comparison.

## Decisions and corrections during the run

- **`umask 077`.** ai-memory writes pages through temporary files (mode 0600) and creates
  directories 0755 by default; under the homogeneity rule every page would then be heterogeneous and
  absent from `QUERY2`. A private memory runs with directories 0700 and files 0600, which satisfies
  the rule. The run confirmed it: `heterogeneous=0`, page modes 600.
- ai-memory's database is `<data_dir>/db/memory.sqlite`, not `<data_dir>/memory.sqlite`.
- The stale shape first returned nothing on both sides (every page was written minutes before, none
  older than the 7-day cutoff), which proves nothing. When no page is older than the cutoff, the
  comparator now uses the project's median `updated_at`: the same query with a cutoff that splits
  the pages. The comparator also prints result counts per shape so an empty agreement is visible.

## Evidence

Mac mini, kernel 6.12.111, module of `main` with the rename fix, ai-memory 385b81e
(`bench/results/2026-10-12-macmini-aimem-stage2-small/`):

- 150 pages written, none refused (10 s through MCP); 150 mirrored, 75 µs per page (p50).
- `GET_INFO`: 150 inodes, 811 entries, 82 KiB of index, 0 heterogeneous, 0 domains in fallback.
- 18 checks (6 shapes × 3 projects), 0 differences; results per shape: recent 60, pinned 18,
  rules 17, settled 45, stale 16, incremental 60.

## Limitations

150 pages, one run. Latency of the SQL against `QUERY2` is not measured yet (the comparator runs
the `query` client as a process per call, which measures process start, not the query). The mirror
runs offline after the server stops; a sidecar that follows writes live is not built.

## Next step

A latency tool that runs each shape in one process on both sides; then the scales up to 60 000
pages.

# Real memory client test — plan

Status: **proposal** (2026-10-12). Nothing below has been run.

## Question

The QUERY2 series answered "is the query fast and safe" (journals 0043 to 0049). It did not answer
"does an agent memory system gain anything by running on MoobFS". This plan measures that with an
existing client, unchanged first, then with its structured recall served by `QUERY2`.

## Client: ai-memory

[ai-memory](https://github.com/akitaonrails/ai-memory) (MIT, Rust) keeps memories as Markdown files
with YAML front matter under `<data_dir>/wiki/<workspace>/<project>/<kind>/*.md`, and a derived
SQLite index (`docs/research-ai-memory.md`). Its structured queries are mapped in
`docs/research-client-query-patterns.md` §1: scope by workspace and project, `is_latest`,
`expires_at IS NULL OR > now` ("missing or ..."), `kind IN (...)`, `ORDER BY updated_at DESC
LIMIT n`. Those are exactly the shapes of QUERY2 (`MOOB_TERM_MISSING_OK`, batch, `order_by`).
It has no storage abstraction (`rusqlite` and `std::fs` called directly), so stage 3 needs a fork.

Mapping: mount the layer on `<data_dir>/wiki` with `domain_depth=1`, so each workspace directory is
a domain; front matter fields become typed attributes (`kind`, `tier`, `pinned`, `expires_at`,
`agent`, `tags`, `updated_at`).

## Stages

1. **Transparency (no code change).** Run ai-memory with `<data_dir>/wiki` on the layer and on plain
   ext4, same machine, same scripted session replay (capture, consolidate, recall, commit). Measure
   page write latency, `git commit`, watcher reconciliation and recall latency; check that its own
   test suite passes with the data directory on the layer. Done when: same results on both, and
   the overhead of the layer on this workload is measured.
2. **Structured recall on QUERY2 (side adapter).** A small tool mirrors front matter into
   `user.moob.*` attributes at write time (from the watcher events, as a sidecar). Replay ai-memory's
   structured queries (recent pages, kind filters, expiry, handoff state) both through its SQLite and
   through QUERY2, compare answers and latency, and measure what the sidecar costs per write against
   what ai-memory spends keeping SQLite in sync. Full-text and vector search stay in SQLite.
   Done when: equal answers on a replayed workload, latencies and write costs reported.
3. **End to end (fork).** Replace the structured part of `recent_pages_for_project` and the scope
   and expiry predicates of search with QUERY2 calls in a fork, and run the client's own LongMemEval
   harness to check that recall quality is unchanged while latency and sync cost change.

## Risks and open points

- The data set: ai-memory's harness and LongMemEval give a workload; its size must reach at least
  the 60 000 memories used in journal 0044 to be comparable.
- Building ai-memory runs third-party build scripts (about 396 thousand lines of Rust); it is done
  on the dedicated benchmark host only, at a pinned commit.
- Its watcher does not tell its own writes from others'; the sidecar must avoid write loops.
- Multi-user read filters (`readable_repository_sql`) have no direct QUERY2 equivalent beyond the
  domain; stage 2 compares single-workspace queries first.

# 0063 — Real-client test, stage 2: design from ai-memory's code

Date: 2026-10-12. Status: design proposed (local series step 3a); nothing run.

## Goal

Turn stage 2 of `docs/client-test-plan.md` ("structured recall on `QUERY2`") into a design that
matches what ai-memory actually stores and queries, before writing tools.

## How

A read-only pass over ai-memory 385b81e (the commit pinned on the benchmark host): front matter
handling (`wiki/src/markdown.rs`, `core/page.rs`, `core/okf.rs`), the write path
(`wiki/src/wiki.rs`, `atomic.rs`), the store (`store/src/reader.rs`, `ops.rs`, migrations) and the
LongMemEval harness (`evals/src/retrieval`).

## Findings that changed the plan

- `updated_at` and `is_latest` are SQLite columns, not front matter; the plan said otherwise and
  carries a dated correction.
- Page directories are workspace and project UUIDs; the first path segment is the page family.
- The LongMemEval replay creates only session pages and uses only scope, `is_latest` and
  not-expired around BM25. It does not exercise the structured queries stage 2 is about, so stage 2
  needs its own workload.
- There is no storage abstraction (about 150 inline-SQL methods on a concrete reader), which
  confirms that stage 3 needs a fork.

## Decisions (proposed)

Projects as domains (`domain_depth=2`); workload through ai-memory's own `memory_write_page`;
a mirror that writes the front matter keys and the store's `updated_at` as typed attributes; six
query shapes taken from `reader.rs` (recent, pinned, rules, settled kinds, stale episodic,
incremental sync); exact answer comparison with ties compared as sets; latency, mirror cost and
index memory. Details and alternatives are in the plan.

## Next step

Write the workload driver and the mirror, run a small scale on the benchmark host, then scale.

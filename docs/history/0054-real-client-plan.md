# 0054 — Plan for a test with a real memory client

Date: 2026-10-12. Status: proposal written (`docs/client-test-plan.md`); nothing run.

## Goal

After the QUERY2 series, the next question is whether an agent memory system gains anything on
MoobFS, not only whether isolated queries are fast. The maintainer approved designing the test and
a prototype.

## Decisions

- Client: ai-memory, because it already keeps memories as Markdown files with front matter and a
  derived SQLite index, and its structured queries match QUERY2's shapes (journal 0033 research).
- Three stages, cheapest first: transparency with no code change, structured recall served by
  QUERY2 through a sidecar, then an end-to-end fork. Stage 1 needs only a build on the benchmark
  host.

## Next step

Build ai-memory at a pinned commit on the Mac mini host and run stage 1 (requires the maintainer's
go-ahead to build and run third-party code on that host).

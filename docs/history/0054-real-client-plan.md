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

## Stage 1a result (2026-10-12)

The first stage 1a run found a layer bug: renaming over a just-deleted name returned `EINVAL`
(journal 0057, fixed on `fix/rename-over-deleted-name`). With the fixed module both runs pass the
same 1 147 tests with no failure (`bench/results/2026-10-12-macmini-aimem-stage1a/`). Stage 1b on
the old module was stopped before its layer half and is rerun with the fixed module.

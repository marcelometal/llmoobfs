# 0166 — Known algorithms assessed against the current index and client roadmap

Date: 2026-10-20. Status: research complete; experiments and adoption not started.
First recorded timestamp: 16:14 America/Sao_Paulo; completion recorded below.
Wall-clock timestamps do not measure active engineering effort.

## Goal and result

Study established algorithms and identify where they could benefit MoobFS,
grounded in current code, prior experiments and the G1–G6 plan. Main was fetched
and remains `02381fb`; this work follows local planning commit `8f773e5`.

`docs/research-algorithm-fit.md` maps subtree cardinalities, cache replacement,
exact vector scoring, BM25, RRF, MMR, HNSW and Roaring to concrete costs, risks,
prerequisites and experiments. The immediate research candidates are selecting
an equality run more cheaply and avoiding repeated domain rebuilds. Client
retrieval starts with lexical/exact-vector baselines and RRF; ANN and alternative
set representations require measured need. No algorithm/backend is selected.

## Evidence and rejected assumptions

- Read `layer/index.c` (tree structures, equality plan, query cursor state,
  domain eviction and shrinker accounting), journals 0044/0103, ADR-0012,
  prior metadata-search studies and existing references.
- `leap_collect` is now a shortest-run scan despite its historical name. The
  pilot rejected repeated tree-seek leapfrog; its results must not be described
  as current leapfrog performance. The corrected ordered-query diagnosis in
  0044 is preserved, not reused as a current bottleneck.
- The release pilot demonstrates thrashing in one cyclic workload, not proof
  that 2Q/ARC/S3-FIFO fixes it. Whole domains have unequal costs; fairness is a
  separate policy. Actual per-domain memory accounting is a prerequisite.
- Read relevant Linux 6.12.112 headers from the stable maintainer's source;
  confirmed this version is pinned by `.github/workflows/kasan.yml`.
- Consulted original papers/official abstracts and SQLite FTS5 documentation
  through Context7. Primary links and partial-reading limits are explicit.
  The BM25 publisher page returned 403; use official FTS5 documentation instead
  of claiming the full paper was read. A PDF text search for 2Q's queue spelling
  found no match; no precise pseudocode transcription is claimed.

## Changes and decisions

Add the research document and its documentation index entry, link it from the
planned roadmap section, update references/changelog and this history index.
This is research supporting planning, not a new accepted algorithm or architecture
decision. Existing ADRs, scope and feature priorities remain in force. Implementation
is still deferred. The main checkout and other worktrees are unchanged.

Public-surface inventory: roadmap, research index, history, references and changelog
are updated. README EN/PT, site, FAQ, SECURITY and compatibility claims do not
change: no new behavior, tested support or achieved performance is announced.
No outside contact, dependency installation, push or PR is part of this work.
Context7 ran through `npx`; no project dependency was added.

## Validation and next step

Completed 2026-10-20 at 16:19 America/Sao_Paulo. Documentation checks passed:

- `git diff --check`: no whitespace errors.
- `sh scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`.
- `python3 scripts/check-site.py .`: 12 HTML pages checked successfully.
- `python3 scripts/site-faq.py --check`: generated FAQ matches its source.
- A Python local-link check over the seven changed Markdown files resolved all
  288 local targets; external URLs and fragment anchors were outside this check.

No simulation, benchmark, new test, kernel build or VM was run. Historical
results retain their original limits. Next: select a bounded experiment only
after the existing correctness work and explicit maintainer authorization.

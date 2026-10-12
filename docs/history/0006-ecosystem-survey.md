# 0006 — Publication of ADR-0005 and survey of the free software ecosystem

Date: 2026-10-10, America/Sao_Paulo.

## Request

Publish the ADR-0005 commit on GitHub and survey free software tools, libraries, modules and
projects that help MoobFS, so as not to solve problems that already have a solution.

## Result

- `git push origin main` published `b2fb037` (ADR-0005); the two required checks were
  triggered on GitHub.
- `docs/ecosystem.md` (new) organizes the survey by problem: instrumented VM, fuzzing,
  crash-consistency, kernel structures, BFS in user space, memory layers for
  agents and vector search. Each entry gives license, activity and intended use.

## Derived decisions (proposals, not implemented)

Prefer `dm-log-writes` + xfstests over our own replay; user-space fuzzing of the
shared parser before syzkaller; virtme-ng for a kernel with KASAN/lockdep; Haiku's `bfs_shell`
as the oracle for the `QUERY` of ADR-0005; mem0/Letta/Graphiti/cognee treated as
clients, not competitors.

## Evidence

Web searches and GitHub API queries (license, stars, last push) on 2026-10-10.
No tool was executed; no integration was made.

## Limitations and next step

Comparisons of memory layers come from vendors. The PDF of Giampaolo's book was not
verified. Next step: start item 1 of `docs/ecosystem.md` (fuzzing of `common/`).

## Addendum (2026-10-10): interactive plan page on the site

Request: turn the input/processing/output drawing into a site page and publish it.
Created `site/plan.html` (no dependencies, minimal JavaScript, works without JS with all
boxes visible), styles in `site/style.css`, navigation links in `index.html` and `guide.html`.
Validation: `python3 scripts/check-site.py` (3 pages), `git diff --check`, local preview at
1280 px (three columns, milestone filter, detail panel) and 375 px (one column), both without
measured horizontal overflow. The page marks only M1 as validated; the rest is proposal.

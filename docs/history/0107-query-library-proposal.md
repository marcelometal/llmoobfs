# 0107 — Proposal: richer queries in a user-space library (ADR-0013)

Date: 2026-10-14. Status: proposal written; waiting for the maintainer's decision.

## Goal

The next item of the order agreed on 2026-10-13: IN lists, disjunctions and counts for clients,
without growing the kernel interface.

## What was done

- Read again the survey of client query patterns (`docs/research-client-query-patterns.md`): IN
  lists and disjunctions are the needs `QUERY2` does not meet; "missing or" and ordering with a
  limit already are.
- Prior art read: PostgreSQL's combination of index scans, MySQL's index merge union, Oracle's
  OR expansion (`docs/references.md`).
- `docs/decisions/0013-user-space-query-library.md`, **proposed**: a C library over
  `QUERY2_BATCH`; up to eight AND-branches; duplicates excluded as OR expansion does, free for IN
  lists because a file holds one value per attribute; ordered merges that read the order
  attribute of each compared result, which makes projection of values its third use case; no NOT.

## Decision needed

The three questions at the end of ADR-0013.

## Next step

After the decision: the oracle extended to an OR of branches, then the library, test first.

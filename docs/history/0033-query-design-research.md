# 0033 — Research for the decision on the role of `QUERY` (ADR-0009)

Date: 2026-10-11. Status: concluded (research; ADR-0009 remains a proposal).

## Goal

Gather external evidence for ADR-0009, requested by the maintainer: what the literature and
mature projects did with metadata indexes and permissions, and which queries real memory clients
make.

## Documents

- `docs/research-metadata-search.md` and the addendum `docs/research-metadata-search-addendum.md`:
  state of the art in file metadata search with permissions (GUFI, BFS and Giampaolo's book,
  Windows Search, Spotlight, WinFS, Tracker/LocalSearch, Baloo, Robinhood and Lustre changelogs,
  Parker-Wood, `fanotify`), with each source marked as code, text, abstract or secondary.
- `docs/research-academic-survey.md`: a scan of conferences (FAST, ATC, SOSP, MSST, SC,
  CIKM, S&P, ICDE), groups (UC Santa Cruz, CMU PDL, Notre Dame, Google, Microsoft) and labs
  (LANL, OSTI) with citation tracking through OpenAlex; lists the sources with no result.
- `docs/research-client-query-patterns.md`: a reading of the code of ai-memory, mem0, Letta,
  Graphiti, LangGraph/LangMem and OpenAI Agents SDK, with the commits read.

## Decisions reflected in ADR-0009

1. Alternative E (index partitioned by domain in the kernel, without SQLite) was added at the
   maintainer's request, with a table comparing E and B.
2. The partition that closes the timing channel is the set of readers, not the directory: E must
   partition by (namespace, readers) or declare homogeneous namespaces and enforce that.
3. Real clients isolate by one to three equalities or a namespace prefix, order by time
   with a small limit and filter inside their own vector engine; E covers the scope and "the N most
   recent of the domain", and has two small gaps ("or" between values; "missing or greater than").
4. If B goes ahead, the external index needs a monotonic change counter to avoid the
   "new enemy" problem (Zanzibar).

## Limitations

Parts of the sources only as abstracts (marked in the documents); Semantic Scholar and dblp
rate-limited the requests, and citation tracking used OpenAlex; the GUFI paper (SC22) was not
open access. This journal was numbered 0033 because 0032 is already in use on
branch `community/dx-batch1`, from another work session.

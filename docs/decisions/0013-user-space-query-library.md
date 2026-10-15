# ADR-0013 — Richer queries through a mature engine: a SQLite virtual table over QUERY2

Date: 2026-10-14. Status: **accepted** by the maintainer on 2026-10-14, with a point of attention:
SQLite is the engine unless something better is found, and the choice is to be revisited in the
future. Prototype: journal 0108. Revised the same day before acceptance:
the first draft proposed a planner library of our own; at the maintainer's request mature
engines and kernel precedents were studied first (`docs/research-query-frontends.md`), and the
draft is kept below as a rejected alternative.

## Context

`QUERY2` answers up to four ANDed terms with one ordering, in one domain, with the kernel
deciding who sees what. The survey of client query patterns
(`docs/research-client-query-patterns.md`) found three needs outside that: lists of values
(`kind IN (...)`), disjunctions of whole conditions, and counts. The order agreed with the
maintainer on 2026-10-13 puts richer queries in user space, after ADR-0012 and before packaging,
with the kernel interface unchanged.

## Decision (proposed)

1. **No planner of our own.** Richer queries go through SQLite, as an optional loadable extension
   for MoobFS that implements a virtual table (`moobfs`): one row per file of a domain, the rowid
   the inode number, one column per attribute the query names, plus the path. The extension turns
   the constraints SQLite hands it into `QUERY2` requests; an `IN` list, received whole
   (`sqlite3_vtab_in`), becomes one `QUERY2_BATCH`; an ordering the kernel already gives is
   declared consumed. SQLite plans the rest: disjunctions, sorting, limits, counts, grouping.
2. **No database.** The extension keeps no file and no copy: data stay in the files, the index in
   the kernel, and every row comes from a `QUERY2` the kernel authorized for the caller. Column
   values are read with `getxattr` as the caller, only for the columns the query uses
   (`colUsed`).
3. **Framework adapters translate, they do not plan**: the filter documents of the agent stores
   (Chroma's `where`, Qdrant's `must`/`should`, mem0, LlamaIndex, LangGraph) become SQL for the
   extension. The LangGraph adapter of journal 0089 moves onto it.
4. **The kernel interface is unchanged.** Direct `QUERY2` stays the primary interface; the
   extension is for those who want SQL.
5. **Before acceptance, a prototype settles three points** (study, "Open"): whether SQLite applies
   its OR union to a virtual table; what an ordered disjunction with a limit costs (SQLite gathers
   every branch and sorts, reading the order column); whether the clients' Python builds can load
   extensions.

## Prior art this builds on

- SQLite's virtual table mechanism and query planner (`xBestIndex`, `sqlite3_vtab_in`,
  `orderByConsumed`, `colUsed`; the OR optimization by union of rowids).
- osquery: operating-system state as SQLite virtual tables, tables reading `EQUALS`/`IN`
  constraints from their query context to avoid generating every row, columns marked
  `required`. The same arrangement, in production for years.
- PostgreSQL's combination of index scans, MySQL's index merge union and Oracle's OR expansion:
  how engines answer OR with several scans, and the cost of ordering afterwards.

## Alternatives considered

- **A planner library of our own** (the first draft): OR as up to eight AND-branches in one
  `QUERY2_BATCH`, duplicates excluded as OR expansion does, ordered merges reading the order
  attribute. Rejected: it rewrites, less well, what SQLite's planner does, and every client would
  still need a query syntax.
- **DuckDB**: its C API pushes only the projection into a table function, so it would read every
  row of a domain and filter after; a C++ extension API exists and was not studied.
- **Apache DataFusion**: the clearest pushdown contract (filters marked exact or inexact, limit,
  projection), but a large Rust dependency; worth a table provider only when a Rust client asks.
- **Polars IO plugins**: predicate and projection pushdown, but documented as unstable.
- **Disjunctions in the kernel** (a new query version): the kernel's own precedent is ftrace's
  event filter, which compiles `&&`/`||`/`!` into a short-circuit branch program; for IN lists over
  ranges, nftables' PIPAPO sets. Held: it grows the reviewed interface, and the engine above may
  make it unnecessary. To be reconsidered if the prototype shows ordered disjunctions too slow.
- **Projection of values in `QUERY2` results**: would spare the extension its `getxattr` reads;
  held until the prototype measures them (journal 0097 kept projection for a third use case).

## Questions for the maintainer

1. Accept SQLite through a virtual table as the engine for richer queries, optional, with the
   kernel interface unchanged?
2. Prototype first (the three points above), and decide on its numbers?

## How it will be judged

Answers equal to the oracle for IN lists, disjunctions, ordered queries and counts, across
pages; no row the caller could not get from `QUERY2` directly; the cost of the ordered shapes of
the survey measured against SQLite holding the metadata itself.

## Consequences

Clients get SQL, IN and OR with the kernel still deciding visibility, and this project writes an
adapter instead of a planner. The README must say plainly that the extension is a query language
over the files, not a database next to them. SQLite becomes an optional dependency of the clients
that want SQL.

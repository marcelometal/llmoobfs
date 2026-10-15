# Query front-ends over QUERY2: what mature engines offer (study for ADR-0013)

Date: 2026-10-14. Status: research; read from official documentation, no prototype yet.

## Question

ADR-0013 first proposed a library of our own to compose IN lists, disjunctions, ordering and
counts over `QUERY2`. The maintainer asked whether a mature, current engine already does this,
before writing a planner. The need: a client expresses a richer filter; something plans it as
`QUERY2` calls (the kernel stays the only authority on who sees what); the data stay in the
files, with no database copy.

## Embeddable engines that push work down to an external source

| Engine | What reaches the source | Notes |
| --- | --- | --- |
| SQLite virtual table | Constraints `col OP value` (`=`, `<`, `<=`, `>`, `>=` and others), the whole right side of an `IN` at once (`sqlite3_vtab_in`), the `ORDER BY` (the module may declare it already satisfied, `orderByConsumed`), the columns used (`colUsed`); `xBestIndex` may be called several times while the planner compares costs | For an OR of separately indexable terms SQLite uses the union of each term's rows by rowid ("MULTI-INDEX OR"); documented for ordinary tables, **not confirmed for virtual tables**. Public domain; already inside Python and the clients that use SQLite |
| DuckDB, C API table functions | Projection only (`duckdb_table_function_supports_projection_pushdown`) | The C API documentation names no filter pushdown: DuckDB would read every row and filter after. A C++ extension API exists; not read here |
| Apache DataFusion (Rust) | Filters, each declared `Exact`, `Inexact` or `Unsupported` by the provider (`supports_filters_pushdown`), the projection and a limit (not pushed past an inexact filter) | The clearest pushdown contract of the four; a large dependency, natural only for Rust clients |
| Polars IO plugins (Python) | Projection, a predicate the plugin must apply, a row limit | The documentation marks the API **unstable** |

## Proof that the pattern is mature: osquery

osquery exposes operating-system state as SQL tables through SQLite virtual tables. A table
reads the query's predicates from its `QueryContext` (for example the `EQUALS` values of a
column, all of them for an `IN`), uses them to avoid generating every row, and may declare a
column `required` (a warning when it is missing from the predicate) or `optimized`. This is the
same arrangement MoobFS needs: SQL over a source that is not a database, the source doing the
selective part.

## What agent-memory clients already speak

Their stores take a filter document, not SQL:

- Chroma: `where` with `$eq`, `$gt`, `$gte`, `$lte`, `$in`, `$nin`, `$and`, `$or` (and array
  operators).
- Qdrant: `must` (AND), `should` (OR), `must_not`, with `match` (one value or `any` of a list),
  `range`, `is_empty`, `is_null`.
- mem0, LlamaIndex and the LangGraph store have their own forms of the same
  (`docs/research-client-query-patterns.md`); the LangGraph adapter of journal 0089 already
  translates its filter into `QUERY2`.

## Expression languages

CEL (Common Expression Language): non-Turing-complete, safe, fast, meant for filters and
policies. It evaluates expressions; it does not plan a disjunction into index scans.

## Precedents inside the kernel

For the alternative of disjunctions in the kernel:

- **ftrace event filters** (`Documentation/trace/events.rst` §5, `kernel/trace/trace_events_filter.c`,
  Linux 6.12.112). A user writes predicates combined with `&&`, `||`, `!` and parentheses; the
  kernel compiles the expression into a linear program of predicates, each with a value on which
  to branch and a target, so that evaluation short-circuits (the method is explained in the
  comment above the parser, with a worked example). It decides whether one item matches; it does
  not plan index scans. It is the model for evaluating the rest of a disjunctive condition on each
  candidate, if that ever moves into the kernel.
- **nftables PIPAPO sets** (`net/netfilter/nft_set_pipapo.c`, Linux 6.12.112): classification of an
  item against sets of ranges over several concatenated fields, after Ligatti 2010,
  Rottenstreich 2010 and Kogan 2014 as cited in the file. An IN list over one attribute does not
  need it: the index is ordered per attribute, so a list is a few tree seeks.
- BPF programs as user-supplied filters were not considered further: unprivileged BPF is turned
  off on most distributions (`kernel.unprivileged_bpf_disabled`; not checked per distribution
  here), and the query must stay open to ordinary users.

## Conclusion

No library found does "a disjunction over an external index, with the source's authorization"
by itself. The mature, current pattern is an engine plus a connector that receives the
pushed-down work; for this project the lightest and longest-proven is SQLite's virtual table, as
osquery uses it. Proposed arrangement, with no planner written here:

- one engine: an optional SQLite extension for MoobFS (virtual table; rowid = inode number, so
  the union of an OR removes duplicates; `IN` lists answered by one `QUERY2_BATCH`);
- framework adapters translate their filter document into SQL, nothing more;
- a DataFusion table provider only if a Rust client asks for one;
- the kernel interface unchanged.

## Open, to be settled by a prototype

1. Whether SQLite applies the OR union to a virtual table (`EXPLAIN QUERY PLAN` on a test
   module).
2. What an ordered disjunction with a limit costs: SQLite collects every branch's rows and sorts,
   reading the order column of each, the same price PostgreSQL documents for combined indexes.
3. Whether extension loading is available in the clients' Python builds (some builds disable
   it).

## Sources

- SQLite: "The Virtual Table Mechanism" (https://www.sqlite.org/vtab.html), `sqlite3_vtab_in`
  (https://www.sqlite.org/c3ref/vtab_in.html), `sqlite3_index_info`
  (https://www.sqlite.org/c3ref/index_info.html), "The SQLite Query Optimizer Overview", OR
  terms (https://www.sqlite.org/optoverview.html), "EXPLAIN QUERY PLAN"
  (https://www.sqlite.org/eqp.html); read through Context7 on 2026-10-14.
- DuckDB: C API, table functions (https://duckdb.org/docs/current/clients/c/table_functions.html).
- Apache DataFusion: `TableProvider` (https://docs.rs/datafusion/latest/datafusion/catalog/trait.TableProvider.html).
- Polars: `register_io_source` (https://docs.pola.rs/api/python/stable/reference/api/polars.io.plugins.register_io_source.html).
- osquery: "Creating tables" and "osquery SDK" (https://osquery.readthedocs.io/en/stable/development/creating-tables/).
- Chroma: metadata filtering (https://docs.trychroma.com/docs/querying-collections/metadata-filtering).
- Qdrant: filtering (https://qdrant.tech/documentation/concepts/filtering/).
- CEL (https://cel.dev/).
- Linux 6.12.112: `Documentation/trace/events.rst`, `kernel/trace/trace_events_filter.c`,
  `net/netfilter/nft_set_pipapo.c`.

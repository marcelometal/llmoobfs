# ADR-0013 — Richer queries are composed in a user-space library over QUERY2 batches

Date: 2026-10-14. Status: **proposed**; not accepted, nothing implemented.

## Context

`QUERY2` answers up to four ANDed terms with one ordering, in one domain, with the kernel
deciding who sees what. The survey of client query patterns
(`docs/research-client-query-patterns.md`) found three needs outside that: lists of values
(`kind IN (...)`, a tag "any of"), disjunctions of whole conditions, and counts. "Missing or
greater than" was met by `MOOB_TERM_MISSING_OK` and ordering with a limit by `order_by`
(journals 0037, 0044). The order agreed with the maintainer on 2026-10-13 puts a user-space
library composing `QUERY2` batches after ADR-0012 and before packaging, keeping the kernel
interface as it is.

## Decision (proposed)

1. **A C library**, `libmoobq` (`lib/`, header `include/moobq.h`), with no kernel change. It
   issues `QUERY2_BATCH` and reads attributes with `getxattr`, as the caller, so the kernel's
   authorization stays the only one. A Python binding follows, for the LangGraph store
   (journal 0089); other languages call the C library.
2. **A query is an OR of up to eight AND-branches** (one `QUERY2_BATCH` holds eight requests),
   each branch within today's four terms. `attr IN (v1, ..., vn)` is written once and expanded
   into branches. A query that expands to more than eight branches is refused (`E2BIG`), not
   split silently.
3. **Duplicates are excluded the way OR expansion does it**: a result of branch *j* is kept only
   if it matches none of branches *1..j-1*. Branches that differ in the value of one attribute are
   disjoint by construction, since a file holds one value per attribute name (`docs/spec-m2.md`);
   the common case, an IN list, needs no check. Otherwise the library reads the result's
   attributes and evaluates the earlier branches on them. Memory stays bounded: no set of seen
   files is kept across pages.
4. **Ordered with a limit**: each branch runs with the same `order_by` and the library merges the
   branches' streams by (value, inode), keeping one page per branch. `QUERY2` returns no values,
   so the library reads the order attribute of each result it compares. That costs one
   `getxattr` per result read, up to branches x limit per page. Returning the value from the
   kernel ("projection") is the alternative below; this would be its third use case.
5. **Cursors**: one library cursor holds the kernel cursor of every branch plus, for an ordered
   merge, the key of the last result returned. It is bound to the query like a kernel cursor.
6. **Counts** page through the answer and count, at the cost of reading it all. No aggregation
   in the kernel.
7. **Not offered**: NOT (it needs the set of every file of the domain), joins, full text,
   similarity. Results are not a snapshot: branches run one after the other, as a batch already
   is (`include/moobfs_ioctl.h`).

## Prior art this builds on

- PostgreSQL, "Combining Multiple Indexes" (documentation, current): an OR of conditions becomes
  separate index scans whose results are ORed; "any ordering of the original indexes is lost",
  so `ORDER BY` needs a separate sort. Here the merge keeps the order instead, at the cost of
  reading the order key.
- MySQL 8.4, "Index Merge Optimization": union and sort-union of several range scans for `OR`;
  sort-union "must first fetch row IDs for all rows and sort them before returning any rows".
  Point 4 avoids that by merging streams that are already ordered.
- Oracle, OR expansion (SQL Tuning Guide 18c, "Query Transformations"; the optimizer team's
  article on OR expansion): a disjunction is rewritten as `UNION ALL` branches, and a later
  branch carries `LNNVL` of the earlier conditions so that no row is returned twice. Point 3 is
  that rule, evaluated in user space.
- The k-way merge of ordered streams with a heap, for point 4 (any algorithms text; to be cited
  precisely with the implementation).

## What has to be got right

- **Equality with an oracle** for every shape: IN lists, mixed branches, ordered and not, with
  and without `MISSING_OK`, across pages; the oracle (`moobfs-index`) extended to evaluate an OR
  of branches on the lower tree.
- **No result the caller may not see**: the library must not learn more than the caller could;
  `getxattr` runs with the caller's credentials on files the kernel already returned.
- **A file that changes between branches** may be missed or, when it moves from one value to
  another, returned under its old value: the same "not a snapshot" as a batch, to be stated.
- **Cost**: getxattr per compared result in ordered merges; measured against SQLite for the IN
  shapes of the survey before any claim.

## Alternatives considered

- **OR in the kernel.** A new `QUERY3` with disjunctions: the kernel would do the union and
  could merge without reading attributes. Rejected for now: it grows the interface that has to
  be reviewed, and the batch already gives one system call per page.
- **Projection of the order value in `QUERY2` results** (the result record gains the value):
  makes the ordered merge free of `getxattr`. A change of the interface; held until this
  library shows how much the reads cost (journal 0097 kept projection for a third use case).
- **Keeping a set of seen inodes** for deduplication: memory grows with the answer; rejected.
- **Each client composes its own ORs**: what happens today; every client repeats the same code
  and the same mistakes.

## Questions for the maintainer

1. Accept the library as the place for OR and IN, keeping the kernel interface unchanged?
2. C with a Python binding first, or Python first and C later?
3. Measure point 4 first and decide projection on numbers, or plan projection now?

## Consequences

Clients get IN and OR with the kernel still deciding visibility; ordered disjunctions cost reads
the kernel does not do for them, until projection is decided. A new component, in user space,
with its own tests and versioning.

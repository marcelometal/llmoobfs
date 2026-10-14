# 0108 — Prototype: SQL over a domain through a SQLite virtual table (ADR-0013)

Date: 2026-10-14. Status: prototype implemented and tested in the local VM; measured on the
benchmark host. ADR-0013 accepted with a review point (below).

## Decision of the maintainer

On 2026-10-14, after the study of `docs/research-query-frontends.md`: build the prototype "rich
and mature"; if nothing better than SQLite is found, use SQLite, **with a point of attention to
revisit that choice in the future**. Recorded in ADR-0013.

## What was built

- `sqlite/moobfs_vtab.c`: a SQLite virtual table module (`moobfs`), loadable (`make sqlite-ext`,
  `build/host/moobfs.so`, entry `sqlite3_moobfs_init`) or linked in (`-DSQLITE_CORE`). One table
  per domain; a row is a file carrying the key (first column) and every `required` column; rowid
  = inode; columns `path` and the declared attributes. `xBestIndex` hands to the kernel
  `=`, `<`, `<=`, `>`, `>=`, `IS NOT NULL` (up to four terms with the key's and required
  columns' existence), an `IN` list whole (`sqlite3_vtab_in`, one request per value, eight per
  `QUERY2_BATCH`), and an `ORDER BY` on a required column with its `LIMIT`. The plan travels as
  readable text, which `EXPLAIN QUERY PLAN` shows. A constraint is declared handled only when its
  value is a constant of the column's type; others are checked again by SQLite. A column under a
  pushed equality is answered from the request without reading the file. SQLite 3.38 or later is
  checked at load.
- `tests/sqlq.c`: runs statements on the virtual table and on an ordinary table filled from the
  lower filesystem and compares the rows; or runs one statement as a given user.
- `vm/suite.sh`, step `sqlext`: 120 files with attributes of every type, missing values, a file
  without the key, non-ASCII strings; 30 statements (IN, OR within and across columns, ranges,
  `IS NULL`, `!=`, `LIKE`, ordering on nullable and required columns, `OFFSET`, counts, `GROUP BY`,
  a self-join, mixed types such as `score = '5'`); a private file of another user in another
  domain, never returned; the same count for root and an ordinary user.
- `bench/sqlext.py` and `bench/results/2026-10-14-macmini-sqlext/`.

## The three open points of ADR-0013

1. **SQLite applies its OR union to a virtual table:** `EXPLAIN QUERY PLAN` shows `MULTI-INDEX OR`
   with one scan per branch, each with its own conditions handed to the kernel.
2. **Cost** (benchmark host, 7 459 files, warm): the newest 20 rows 0.24 ms against 2.5 ms on an
   ordinary SQLite table with the same metadata; two equalities 0.12 against 0.7 ms; OR across
   columns about equal. Statements that read a column of every row (grouping, two-column sort,
   sorting an IN list) pay one `getxattr` per row, about 10 µs: 16 to 78 ms against 1 to 3 ms.
   This is the third use case journal 0097 waited for before returning values with results
   (projection); not decided here.
3. **Loading from Python:** Debian 13's Python 3 loads the extension (SQLite 3.46.1).

## Errors found on the way

- The first run printed the plan as binary bytes (`idxStr` carried a struct); changed to text.
- The text parser looked for the space in the whole plan instead of the clause: a plan with
  `IS NOT NULL` failed with "internal plan"; fixed, the step passes.
- An IN list was checked again by SQLite by reading every row's attribute (50 ms for 4 544 rows);
  answering columns under a pushed equality from the request brought it to 6 ms.
- The benchmark's first "newest 20" broke ties by path, which forces SQLite to sort; the shape
  clients use orders by one column and is the kernel's.

## Results

- `MOOB_ONLY=sqlext make vm-test`: `SQLEXT_PASS: 30 statements equal to the ordinary table;
  MULTI-INDEX OR in 2 plans; private file never returned`.
- Complete `make vm-test` with the prototype: pass (`SQLEXT_PASS`, `MOOBFS_LAYER_PASS`,
  `MOOBFS_ACCEPTANCE_PASS`).
- `make sqlite-ext` with `-Wall -Wextra -Werror`: clean on the benchmark host.

## Limitations

- Prototype: not packaged, no Python binding beyond `load_extension`, the LangGraph adapter not
  moved onto it yet.
- One domain per table; not a snapshot; files without a known path come with `NULL` columns.
- The equality of answers is shown on two data sets (the suite's and one benchmark domain), with
  ties in ordering avoided by the data or by the statements.
- Point of attention (maintainer, 2026-10-14): SQLite is the engine for now; the choice is to be
  revisited, for instance if projection lands, if a client needs another engine, or if a better
  embeddable engine appears.

## Next step

The maintainer's review of the prototype; then packaging it with the module, and the LangGraph
adapter on top of it.

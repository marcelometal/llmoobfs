# SQL over a MoobFS domain, without a database

`moobfs_vtab.c` is a SQLite extension: a virtual table over one domain of a MoobFS mount. It keeps
no database file and copies nothing. Every row comes from a `QUERY2` the kernel answers for the
calling process, so the kernel still decides who sees what; column values are read from the
files' attributes. SQLite only parses, plans and combines (OR, IN, sorting, LIMIT, counts,
grouping). Status: prototype (ADR-0013, journal 0108).

## Build and load

```sh
make sqlite-ext                       # build/host/moobfs.so; needs libsqlite3-dev >= 3.38
```

```python
import sqlite3
db = sqlite3.connect(":memory:")
db.enable_load_extension(True)        # some Python builds disable extensions (macOS system Python)
db.load_extension("build/host/moobfs.so")
db.enable_load_extension(False)
db.execute("""CREATE VIRTUAL TABLE temp.mem USING moobfs('/mnt/memory/ns1',
              'kind string', 'created time required', 'agent string', 'score int64')""")
for path, created in db.execute(
        "SELECT path, created FROM mem WHERE kind IN ('note', 'decision') AND agent = 'a07' "
        "ORDER BY created DESC LIMIT 20"):
    print(path, created)
```

In the `sqlite3` shell: `.load build/host/moobfs`.

## The table

- `USING moobfs('<domain directory>', '<attribute> <type>' [, ...])`. Types: `string`, `int64`,
  `double`, `bool`, `time` (nanoseconds since the epoch), `raw` (a blob, never filtered by the
  kernel). At most 63 columns.
- A row is a file of the domain that carries the **first** attribute (the key) and every attribute
  declared `required`; at most four such attributes. The rowid is the inode number. The columns
  are `path` (relative to the domain directory) and the declared attributes, `NULL` when absent.
- Declare `required` an attribute every file of interest carries and that you order by (a
  creation time, typically): only then can `ORDER BY` on it, with its `LIMIT`, be the kernel's.

## What goes to the kernel

`EXPLAIN QUERY PLAN` shows it, e.g. `SCAN mem VIRTUAL TABLE INDEX 0:kind IN ?1; agent = ?2;
created EXISTS; ORDER BY created DESC; LIMIT ?3`.

- `=`, `<`, `<=`, `>`, `>=` and `IS NOT NULL` on attribute columns, up to the four terms of a
  `QUERY2` (the key's and required columns' existence included); further conditions are SQLite's.
- An `IN` list, whole: one request per value, eight per `QUERY2_BATCH`.
- `OR` between conditions on different columns: SQLite runs one scan of the table per branch and
  unites the rows by rowid ("MULTI-INDEX OR"), so no file comes twice.
- `ORDER BY` one required column (or the key), ascending or descending, with the `LIMIT`.
- A condition is reported handled only when its value is a constant of the column's type; any
  other is checked again by SQLite on each row, with SQLite's own type rules.

## Cost

Measured on the benchmark host (journal 0108, 7 459 files in one domain, warm): a selective
filter or the newest 20 rows take 0.1 to 0.3 ms, faster than the same query on an ordinary SQLite
table holding the metadata. What reads a column of every row (grouping, sorting by two columns,
sorting a whole IN list) pays a `getxattr` per row, about 10 µs: tens of milliseconds for a
domain of this size.

## Limits

- One domain per table; a domain directory at the mount's `domain_depth`.
- Not a snapshot: a statement may issue several `QUERY2` calls, as files change.
- Files the kernel cannot name (`path` unknown or truncated) appear with `NULL` columns.
- SQLite 3.38 or later.

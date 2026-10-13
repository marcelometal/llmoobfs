# 0072 — M3 phase 3c: batch reads and the change cursor revisited with numbers

Date: 2026-10-13. Status: decisions recorded; one proposal for the maintainer; no code of the
layer changed. Local series.

## Goal

`docs/spec-m3.md` §4 and §5 deferred three things until measurements existed: a batch of queries,
a batch read of results, and a change cursor in the kernel. Phase 3c revisits them. The rule set
on 2026-10-12 applies: build something only for a need that more than one surveyed system has
(`docs/research-client-query-patterns.md`), never for the one client tested.

## Measurement

§4 said: decide after measuring reads by path against individual calls; "if the cost of the kernel
call does not dominate, the implementation is dropped". No such number existed for the step after a
query, so `bench/readcost.c` measures it: `QUERY2` returns N files of a domain, and the tool times
what a client then does with them. Project data set, domain of 7 459 files of about 2.8 KB with
seven typed attributes, warm cache, medians of 499 rounds
(`bench/results/2026-10-12-macmini-readcost/`):

| Step, for 20 results | Total | Per file |
| --- | --- | --- |
| the `QUERY2` call | 17 µs | 0.9 µs |
| open, read to the end, close, through the layer | 121 µs | 6.0 µs |
| the same on the lower ext4 directly | 89 µs | 4.5 µs |
| the data copy alone (`pread` on open descriptors) | 33 µs | 1.7 µs |
| one named attribute (`fgetxattr`, file already open) | 40 µs | 2.0 µs |
| every typed attribute (`flistxattr` and 7.4 `fgetxattr`) | 328 µs | 16.4 µs |
| the same attributes on the lower ext4 directly | 307 µs | 15.3 µs |
| three empty system calls per file | 25 µs | 1.2 µs |

With 200 results every per-file figure is the same within 5 %; the query falls to 0.4 µs per
result.

## Decisions

**`QUERY_BATCH`: built.** It exists as `MOOB_IOC_QUERY2_BATCH` since phase Q2d (journal 0038) and
stage 2 used it for "kind in a set"; lists of values are among the needs the survey found outside
a plain conjunction (roles, tags, kinds, groups, categories).

**A batch read of content: not built, and dropped as a kernel feature.** The most it could save
is the part of a read that is not the data copy: 4.4 µs per file, of which 4.5 µs per file is what
the lower filesystem itself charges for open, read and close; the layer adds 1.6 µs. A generic
kernel interface for batching those calls exists (`io_uring`); not measured here.

**The values of attributes with the results: a real gap, proposed, not built.** A result carries
an inode number and a path. A client that needs one field of each result must open each file and
ask for it: for 20 results, about 87 µs of open and close plus 40 µs, seven times the query. For
every field it is about 415 µs, twenty-four times the query. Almost all of that is the extended
attribute calls of the lower filesystem (15.3 of 16.4 µs per file is ext4's own), so it is not the
layer's overhead; but the index already holds every indexed value in memory, and the query is
right there. By §4's own test the kernel call dominates.

This does not look like one client's need: the survey describes every system sending its filter
to an engine that returns records (SQL or a vector store), not identifiers to look up one by one.
That is a reading of the survey, which did not ask the question; the ADR should check it per
system. It also changes how stage 2 reads:
the SQL side returned a column of each row in the times of journal 0068, and would return more
columns at little extra cost, while the `QUERY2` side returned paths. Where a client needs the
fields, the comparison of journal 0068 understates what the layer costs today.

Proposal, for an ADR before any code: the caller names a few attributes in the request and each
result carries their values (a projection), answered from the index. It is a UAPI change, so it
needs a specification, invalid-input tests and a compatibility decision (`AGENTS.md`). Open
questions for that ADR: how many attributes and how many bytes per result, what a missing or
unindexed (`raw`) attribute returns, whether results become variable-length, and whether the
record for long traversals should shrink at the same time (journal 0068 found the 272-byte record
is what makes ordered traversals slower than SQLite).

**The change cursor and logical revisions: still in the client.** Three pieces of evidence since
§5 was written: an "updated after T, oldest first" traversal works today with an attribute the
client maintains and the `QUERY2` cursor (stage 2's `incremental` shape, equal answers up to 60 000
files); a watcher that only reads file events kept that attribute in step using the file's mtime,
with the same order as the client's own clock (journal 0071); and the survey found no system that
asks its store for "changes since a revision", only ordering by time with a limit and a cursor.
The questions §5 lists for a kernel log (persistence, changes made behind the mount, recovery) are
unanswered and nothing measured asks for one.

## Changes

`bench/readcost.c` (built by `make bench-tools`); dated notes in `docs/spec-m3.md` §4 and §5; the
roadmap row of M3 closed and the projection listed under "Depends on validation";
`docs/adoption-risks.md` risk 13 points here.

## Limitations

One host, one data set, warm cache only: with a cold cache the disk dominates every row and a
batch would not help. Opening by path relative to the domain directory; a client that keeps
descriptors open pays less. `io_uring` and the cost of SQLite returning more columns are argued,
not measured. The projection is a proposal; its benefit is an upper bound taken from the cost it
would remove, not a measurement of an implementation.

## Next step

M3 is closed except for the proposal above, which goes with the other open proposals. Next in the
confirmed order: S4 (the lower tree as hostile input) and S2 (fstests).

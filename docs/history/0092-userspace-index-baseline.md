# 0092 — The alternative without a kernel module: a per-domain SQLite index kept by inotify

Date: 2026-10-13. Status: **built and tested for equal answers in a container; nothing measured**.
Branch `worktree-agent-ab2a7565c8b7500e4` on top of `main` (`c5d31f8`), local commits only
(`c8aa86b`, `7f01f48`, `89b322a` and those after them, the last adding this journal). Not pushed.

## Goal

The project never built the obvious alternative to its kernel layer: typed attributes as ordinary
`user.moob.*` extended attributes on a plain filesystem, an index kept in user space for each
domain directory, and the index file protected by the directory's permissions. ADR-0009 rejected
that design ("alternative B") on argument; `bench/sqlbase.sh` (journal 0047) is a different thing,
one database loaded from the generator's manifest, with no follower and no boundary per domain.

This journal builds that alternative as an honest baseline, checks that it gives the oracle's
answers, and writes down where it differs from the layer in both directions. It measures nothing:
the numbers belong to the dedicated benchmark host, and the commands for it are at the end.

## What was built (`bench/userspace/`)

| File | What it is |
| --- | --- |
| `udx.h` | schema, walk, homogeneity rule, and the operation everything is made of: "make the rows of this path equal to what is on disk now" |
| `udx-index.c` | `udx-index <root> <depth>`: one full pass; creates or repairs `<domain>/.udx.db` for every directory at that depth |
| `udx-watch.c` | `udx-watch [--settle-ms N] <root> <depth>`: the same pass at start, then follows inotify |
| `udx-query.c` | `udx-query [--require-live] [--order NAME TYPE asc\|desc] [--limit N] <domain dir> [terms...]`, plus `--status` and `--sql` |
| `test.sh` | the test below |

`make userspace-tools` builds the three programs (one target added to the top-level `Makefile`;
they link `common/attr.c` and the system SQLite). They reuse the project's encoding and validation
(`moob_attr_validate`) and the oracle's term parser (`tools/query-common.h`) unchanged.

**Schema, one database per domain.** `entry(id, path, ino, kind, nlink, ok, trav)` has one row per
file or directory below the domain; `attr(eid, name, type, v)` has one row per attribute a query
can see. `v` uses SQLite's own typing: an integer for int64, time and bool, an integer *order key*
for double, a BLOB for string. `meta` holds the schema version, `state`, the time of the last
complete pass and a count of inotify overflows.

- One row per attribute with a typed value, rather than one wide row per file: attribute names are
  the client's and not known in advance, which is also why the layer indexes per attribute.
- One value column instead of one column per type: `(name, type)` are fixed by every term, so a
  single index `attr_value(name, type, v, eid)` is covering for every operator and for `--order`,
  and values of different storage classes are never compared with each other.
- Doubles are stored as the IEEE bits mapped to an order-preserving integer, not as REAL. SQLite
  compares `-0.0` and `0.0` as equal; the oracle orders by bits. The test has both values. The
  cost: a double is not readable in the `sqlite3` shell without converting it back.
- Strings and paths are BLOBs: the oracle compares bytes, and a file name is not text.
- `attr`'s primary key `(eid, name)` serves the other terms of a conjunction; `UNIQUE(path)` serves
  updates and subtree ranges; `entry_ino` and the partial index `entry_linked` serve hard links.
- Positive terms and the order attribute are joins, so SQLite's planner picks the driving term
  from `ANALYZE` statistics; a "missing or" term is a filter. The plans of the nine W3 shapes are
  printed by the test: eight start from `attr_value`; Q9 (`weight exists`) walks `entry` in inode
  order and probes `attr` by key, the planner's choice for a term a third of the entries satisfy
  when the output is ordered by inode anyway. The `attr` table is never read whole.

**What is indexed** is what `moobfs-index --domain` lists, because the outputs are compared byte
for byte: every non-symlink entry below the domain, directories included (the request said
regular files; the oracle lists directories, so they are in); any `user.moob.<name>` of up to 64
bytes whose value passes `moob_attr_validate` and is not raw; and `ok = 1` only for entries that
pass the homogeneity rule of `docs/spec-query-v2.md` §3 as the oracle evaluates it (mode bits,
owner, group, access ACL, link count, every directory on the way).

**The follower is state-based.** Every inotify event becomes a path to look at again; a batch is
sorted, deduplicated and applied in one transaction per domain, and each path ends equal to the
disk at that moment, including "gone". Order of events and events per write do not matter, which
is the pitfall journal 0071 met. It watches `IN_ATTRIB`, create, delete and both sides of a
rename, one watch per directory, added before the directory is read. A directory moved away has
its watches dropped; one that arrives is walked. A change of mode on a directory re-evaluates its
subtree; on the domain directory, the whole domain.

**How a reader can tell.** The watcher holds an exclusive `flock` on every database it maintains;
`udx-query --status` prints `watcher=running|absent state=synced|resync overflows=N`, and
`--require-live` exits 75 unless the lock is held and the state is `synced`.

## Decisions and alternatives

- **WAL, `synchronous=NORMAL`.** Readers do not block the writer. A crash can lose the last
  transactions; the files are the truth and the next pass puts them back. Rollback-journal mode
  was rejected: it creates and deletes a file in the watched directory on every commit.
- **0600, owner of the domain directory.** As asked. A root indexer gives the file to the
  directory's owner, and again to a new owner when the directory changes hands (tested). Wider modes were not built; see the differences below.
- **Batching window of 10 ms** (`--settle-ms`), so the several `setxattr` calls of one logical
  write cost one refresh. It is a floor on staleness and is the first thing to vary on the host.
- **A removed `.udx.db` is not recreated.** The first version recreated it; that would make
  `rm -rf` of a domain fail with "directory not empty". The domain stays unindexed until
  `udx-index` or a restart, the watcher logs it, and a query fails.
- **fanotify was not used.** A filesystem-wide mark would remove the one-watch-per-directory limit
  and see hard links made elsewhere, but needs `CAP_SYS_ADMIN`, which is the privilege this
  baseline is meant to do without. Not built, not tested.
- **No ADR.** This is a measurement baseline, not a change of direction; ADR-0009 stands.

## Errors found while building it, and their fixes

1. `llistxattr` with a zero-size buffer returns the size needed, not `ERANGE`; the first pass
   crashed on the first file with attributes. The buffer is now sized before the names are read.
2. **The watcher woke itself, without end.** As root, SQLite calls `fchown` on the `-wal` and
   `-shm` files each time it opens them, which is an `IN_ATTRIB` event in the watched directory.
   With the queue lowered to 16 events, the full pass after an overflow produced more than 16 such
   events, overflowed again, and looped (20 full passes before the test stopped the watcher;
   during them `--status` said `watcher=absent state=resync`). Fix: the `-wal` and `-shm` files
   persist across closes, and `fchown` is switched off through SQLite's system-call table, with
   the owner set once by `udx_open`. What remains: creating a database is a handful of events, so
   a pass that creates databases with a tiny queue overflows once more (the run below shows two
   overflows for one provoked) and then settles.
3. **A hard link whose other name was in a removed domain.** When a domain disappeared, the rows
   that would have said which inodes lost a name went with it, and a file left with one name
   stayed unlisted. Fix: dropping a domain makes the watcher look again at every name recorded
   with more than one link.
4. `srand()` in mawk does not make `rand()` repeatable, so two runs of the test applied different
   changes. The test now carries its own generator; the same seed gives the same changes.

## Evidence

Run on this laptop (Docker 29.8.2, arm64, kernel 7.0.14-linuxkit), image `ubuntu:24.04`,
privileged container, data set on a **loop-mounted ext4 image** (tmpfs was the fallback and was
not needed). Command:

```
sh bench/userspace/test.sh
```

It builds everything, generates 3 000 memories with `bench-gen --profile 16` and writes them with
`bench-load W1` (the project's own generator and loader; no substitute was needed), adds by hand
what the generator does not produce (attributes on a directory, `-0.0` and `0.0`, an empty and a
non-ASCII string, invalid UTF-8, a wrong-size int64, a raw value, another type under a queried
name, a hard link, a file and a directory closed to the group, a symlink, a FIFO), gives `ns1` to
uid 1001, and compares `udx-query` with `moobfs-index --domain` on 37 query shapes per domain:
the nine of `bench/queries-w3.txt` and 28 with `--order`, `--limit`, "missing or" operators,
prefix, ranges and names that do not exist. Output lines of the last run (exit status 0):

```
udx-test: built: udx-index udx-query udx-watch with gcc (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, sqlite 3.45.1, kernel 7.0.14-linuxkit
udx-test: filesystem: ext4 on a loop device (ext4 rw,relatime)
udx-test: udx-index: 8 domains, 3270 entries, 42904 attribute rows, 46190 rows written
UDX_COMPARE_OK after udx-index: 296 comparisons (37 query shapes x 8 domains), 25367 result lines, all equal
udx-test: second pass, nothing changed (two meta rows per domain): udx-index: 8 domains, 3270 entries, 42904 attribute rows, 16 rows written
udx-test: plan Q1: SEARCH a0 USING COVERING INDEX attr_value (name=? AND type=? AND v=?);SEARCH e USING INTEGER PRIMARY KEY (rowid=?);USE TEMP B-TREE FOR ORDER BY;
udx-test: plan Q9: SCAN e USING INDEX entry_ino;SEARCH a0 USING PRIMARY KEY (eid=? AND name=?);
udx-test: bench-sqlclient accepted the nine statements of udx-query --sql (procedure check only)
udx-test: authorization: owner (uid 1001) gets 167 lines, equal to the oracle as uid 1001; uid 1002 is refused (udx-query: /work/mnt/t/ns1/.udx.db: unable to open database file); the oracle as uid 1002 lists 167 lines, because the directory is 0755
udx-test: no watcher: watcher=absent state=synced synced_ns=1791917889365493339 overflows=0
udx-test: UDX_WATCH_READY domains=8 watches=268 settle_ms=10
udx-test: watcher running: watcher=running state=synced synced_ns=1791917889448906589 overflows=0
udx-test: W4 burst (9000 attribute writes): marker visible after 113 ms
UDX_COMPARE_OK after the W4 burst: 296 comparisons (37 query shapes x 8 domains), 25370 result lines, all equal
udx-test: applied 400 changes (seed 7): attr-rm=26 replace=33 delete=37 new-dir=30 chmod=41 rename=50 attr-set=89 create=28 new-domain=2 rename-dir=10 link=26 attr-rawset=28
udx-test: marker visible after 16 ms
UDX_COMPARE_OK after 400 random changes: 370 comparisons (37 query shapes x 10 domains), 24992 result lines, all equal
udx-test: applied 400 changes (seed 8): attr-rm=22 replace=26 delete=29 new-dir=29 chmod=38 rename=56 attr-set=100 create=30 new-domain=2 rename-dir=9 link=30 attr-rawset=29
udx-test: marker visible after 26 ms
UDX_COMPARE_OK after 400 more random changes: 444 comparisons (37 query shapes x 12 domains), 24749 result lines, all equal
udx-test: read-your-writes: 20 of 20 queries issued right after the write did not see it; visible after 11 11 14 ms (min, median, max; includes starting two processes; settle 10 ms; not a measurement)
udx-test: removed the domain nsnew7_330 with the watcher running
udx-test: renamed the domain ns2 to ns2-renamed and moved ns3/a00 (7 files) to ns4/from-ns3
UDX_COMPARE_OK after removing a domain, renaming a domain, moving a directory across domains and giving a domain away: 407 comparisons (37 query shapes x 11 domains), 24740 result lines, all equal
udx-test: ns5 given to uid 1002 with the watcher running: database is 600 1002, uid 1002 gets 145 lines, equal to the oracle as uid 1002
udx-test: udx-watch: domains=11 watches=425 events=7981 batches=47 refreshed=4437 max_batch=1757 rows_written=57232 overflows=0 resyncs=0 cpu_ms=263
udx-test: UDX_WATCH_READY domains=11 watches=425 settle_ms=10
udx-test: applied 200 changes (seed 9): attr-rm=8 replace=14 delete=18 new-dir=17 chmod=21 rename=27 attr-set=53 create=13 new-domain=2 rename-dir=2 link=10 attr-rawset=15
udx-test: watcher stopped with SIGSTOP, 200 changes not indexed, and a reader is told: watcher=running state=synced synced_ns=1791917896090191134 overflows=0
udx-test: while it is stopped 186 of 407 comparisons differ
udx-test: after the overflow: watcher=running state=synced synced_ns=1791917897319609551 overflows=2
UDX_COMPARE_OK after an inotify queue overflow and the full pass it forces: 518 comparisons (37 query shapes x 14 domains), 24584 result lines, all equal
udx-test: udx-watch: domains=14 watches=464 events=34 batches=0 refreshed=0 max_batch=0 rows_written=2223 overflows=2 resyncs=2 cpu_ms=161
udx-test: applied 100 changes (seed 10): attr-rm=11 replace=4 delete=6 new-dir=12 chmod=7 rename=15 attr-set=25 create=10 new-domain=1 rename-dir=1 link=1 attr-rawset=7
udx-test: no watcher, 100 changes: 145 of 518 comparisons differ, as they must; status: watcher=absent state=synced synced_ns=1791917897319609551 overflows=2
udx-test: UDX_WATCH_READY domains=15 watches=484 settle_ms=10
UDX_COMPARE_OK after restarting the watcher (its first pass repairs what was missed): 555 comparisons (37 query shapes x 15 domains), 24643 result lines, all equal
udx-test: KNOWN_GAP: ns0/a07/m18.md got a second name outside the watched tree; no event reaches the watcher; 10 of 555 comparisons differ (the oracle and the layer's rule drop a file with two names, the index still lists it)
udx-test: udx-watch: domains=15 watches=484 events=6 batches=1 refreshed=0 max_batch=4 rows_written=1170 overflows=0 resyncs=0 cpu_ms=52
udx-test: UDX_WATCH_READY domains=15 watches=484 settle_ms=10
UDX_COMPARE_OK after removing the outside link and a full pass: 555 comparisons (37 query shapes x 15 domains), 24643 result lines, all equal
udx-test: udx-watch: domains=15 watches=484 events=0 batches=0 refreshed=0 max_batch=0 rows_written=45 overflows=0 resyncs=0 cpu_ms=48
UDX_COMPARE_OK one watcher per user: udx-index, udx-watch and udx-query all as uid 1001 on its own domain, 37 comparisons with the oracle as uid 1001, all equal
udx-test: as uid 1001: UDX_WATCH_READY domains=1 watches=63 settle_ms=10 / udx-watch: domains=1 watches=63 events=3 batches=1 refreshed=2 max_batch=2 rows_written=20 overflows=0 resyncs=0 cpu_ms=6
UDX_TEST_PASS: udx-index and udx-watch equal to moobfs-index --domain on 37 query shapes per domain, on ext4 on a loop device; 3000 memories, 1100 random changes, a W4 burst, an inotify overflow, a watcher restart (known gap shown: 10 comparisons differ after a hard link from outside)
```

(Left out: the two lines of the package installation and seven of the nine plan lines. The
`cpu_ms` and row counters of the watcher are printed for the host; here they are not results.)

Also run earlier in the session, before the last three steps were added to the test (the plans,
the change of owner, the per-user watcher), all `UDX_TEST_PASS`: seed 7 twice with 1 200 changes
per round (identical change counts, so the generator repeats), seed 21 with 1 200, and seed 33
with 6 000 memories and 1 200. The default run above was repeated six times as steps were added; one of the six failed, on a
first version of the plan check that rejected any `SCAN` and so Q9's plan (see the schema notes),
and no comparison differed in it.
`shellcheck --shell=sh --severity=warning bench/userspace/test.sh` is clean.

What the comparison does to the oracle's output: lines for the index's own files (`.udx.db`,
`-wal`, `-shm`) are removed, because the oracle walks the directory and lists them under "missing
or" terms. Nothing else is filtered.

**The milliseconds above are not results.** They come from a shell loop that starts two processes
per probe, in a container on a laptop, with a 10 ms batching window. They show that an answer lags
a write and by what order here; how much on real hardware is for the host.

## How this baseline differs from the kernel layer

Statements about the layer are from its documents and code as cited; nothing about the layer was
run here.

### What the baseline does not give

1. **No validation at the time of the write.** The layer validates the value in `setxattr` and
   refuses it (`layer/main.c`, `moob_attr_validate` before `vfs_setxattr`; client contract §4).
   Here anyone who can write the file can store anything under `user.moob.*`: the test stored
   invalid UTF-8 and a one-byte int64 (`attr-rawset`), the filesystem accepted them, and the index
   left them out, as the oracle does. The writer gets no error; a bad value is discovered by
   whoever reads it, and name rules (`[a-z0-9_.]`) are not enforced at all.
2. **Answers lag writes; there is no read-your-writes.** The layer updates its index inside the
   same `setxattr` call (`moobl_index_set`, `layer/main.c`). Here 20 of 20 queries issued right
   after a write missed it; the write showed after 10 to 34 ms across the runs; after a burst of
   9 000 attribute writes the last one showed 107 to 119 ms later. The floor is the batching
   window; the ceiling is unbounded under sustained writes (the watcher applies batches of up to
   100 000 paths). A client that needs its own write back must wait and poll, or not use the
   index for that read.
3. **Between a permission change and its refresh, the old answer stands.** The same lag applies to
   `chmod`, `chown` and hard links: for that interval a query still lists the path and inode of a
   file that no longer passes the rule. For a file, the layer re-evaluates in the call that changes
   the mode, owner, group or ACL (spec §3).
4. **If the watcher is not running, nothing updates the index, and plain `udx-query` still
   answers.** With no watcher, 145 of 518 comparisons were wrong after 100 changes. A reader can
   tell only by asking: `--status` showed `watcher=absent`, and `--require-live` refuses.
5. **A watcher that is alive but not keeping up cannot be told from a healthy one.** Stopped with
   `SIGSTOP`, 186 of 407 comparisons were wrong while `--status` said `watcher=running
   state=synced`. The lock proves a process exists, not that it has applied anything. The same
   holds for an ordinary backlog.
6. **Lost events are detected, and cost a full pass.** `IN_Q_OVERFLOW` marks every domain
   `resync`, then walks everything again; answers are stale meanwhile. Verified with the queue
   lowered to 16. The default queue (`fs.inotify.max_queued_events`, 16 384 in inotify(7)) is one
   queue for all the domains a watcher follows.
7. **A hard link made outside the watched tree is never seen** (no event is delivered to a
   directory watch for a link count change). The file should drop out of the answer and stays in
   it until the next full pass: 10 comparisons differed. This errs toward listing too much.
8. **Who can ask is narrower.** The layer answers anyone with read and execute on the domain
   directory (spec §4.2). The 0600 database answers only its owner: uid 1002 was refused where
   the oracle as uid 1002 lists 167 entries of a 0755 domain. Following the directory's group and
   other bits is possible in principle, was not built, and is not simple with WAL (a reader needs
   the `-shm` file).
9. **The index is a file inside the domain.** It shows up in listings and in walks; whoever can
   write the domain directory can delete or replace it, which for a group-writable directory means
   one member can change what another is answered. The layer's index is not reachable by path.
10. **The trust the watcher needs.**
    - *One watcher for everybody* must read every file of every domain, so it runs as root or
      with `CAP_DAC_READ_SEARCH`, and it writes database files into directories users control.
      It is a privileged component of the kind ADR-0009 lists as what the kernel avoids, and a new
      attack surface: planted symlinks or hard links under the database's names. The code refuses
      a database that is not a single-link regular file of the right owner and opens without
      following links; this was **not audited or attacked**. The homogeneity rule, evaluated by
      the watcher, is what keeps a root-built index from telling a domain's owner about files the
      owner cannot read; with the lag of point 3.
    - *One watcher per user* avoids all of that and was run: index, watcher and reader as uid
      1001 on its own domain, 37 comparisons equal to the oracle as that user. No privilege is
      involved and the index can hold nothing its owner could not read. What it gives up: one
      process and one set of watches per user, and no answer for anyone else, so a domain shared
      by a group has no shared index.
11. **Operating limits the layer does not have:** one inotify watch per directory
    (`fs.inotify.max_user_watches`), one open descriptor per domain for the lock, a process that
    has to be started, supervised and restarted.
12. **Not built:** the cursor and the batch call of `QUERY2`. The oracle has neither, so the
    comparison covers `--order` and `--limit` only. Results are not capped at 4 096 and paths
    are not truncated at 256 bytes, which differs from the UAPI but not from the oracle.

### What the baseline gives that the layer does not

1. **No kernel module.** Nothing runs in ring 0; a bug is a wrong or stale answer or a dead
   process, never a host crash (journal 0069 records one in the layer). No pinned kernel series,
   no build against kernel headers.
2. **No privilege to deploy, in the per-user form.** The layer needs `CAP_SYS_ADMIN` in the
   initial user namespace to mount (README, limitations). The tools here ran as uid 1001 with no
   capability. A rootless container was not tried.
3. **A persistent index.** The layer's index is in RAM and rebuilt by a linear scan at every
   mount. Here a second `udx-index` on an unchanged tree wrote 16 rows (two bookkeeping rows per
   domain), and `udx-query` answers from the file without any process or scan. To be fair about
   it: the watcher's start still walks the tree and reads every attribute to find what changed
   while it was down, and until that ends the persistent answer may be stale.
4. **Pageable memory.** The layer's index is non-pageable kernel memory under `index_budget`.
   This one is a file behind the page cache.
5. **SQL.** More than four terms, `OR`, aggregates, joins and a planner with statistics, which
   ADR-0009 names as what would have to be rewritten in the kernel.
6. **Portability.** Any filesystem with `user.*` attributes and inotify. Only ext4 was run.

## Limitations of this work

- Equal answers only. No latency, throughput, memory or space figure here means anything.
- One filesystem (ext4 on a loop device), one architecture, 3 000 to 6 000 memories, 8 to 15
  domains, changes applied one at a time by a shell script. No concurrent writers, and no queries
  compared *while* changes were in flight.
- The watcher finds a domain by a linear search and keeps 32 databases open; neither was
  exercised beyond 15 domains.
- Moving a directory re-reads its whole subtree instead of rewriting a path prefix.
- Access ACLs are handled as the oracle does (any access ACL fails the rule); no ACL was set in
  the test.
- Entries of other kinds than regular file and directory are indexed because the oracle lists
  them; one FIFO was in the test.
- The security of a root watcher was reasoned about, not tested.

## Commands for the benchmark host (not run)

Same data set, same seed and scale as `bench/q2-campaign.sh`, the baseline on a plain directory
of the same filesystem type as the layer's lower. `$BIN` holds the built tools, `$PLAIN` is an
empty directory, `$LAYER` the mounted layer of the Q2 campaign, `$OUT` a new directory.

```
make BUILD=$BIN attr-tools bench-tools userspace-tools
$BIN/bench-gen --seed 1 --inodes 60000 --profile 8 --out $OUT/manifest.txt

# writes, as the writer sees them: W1 and W4 with the watcher running, against the layer
# (the watcher creates each domain's database when W1 creates the directory)
$BIN/udx-watch $PLAIN 1 > $OUT/watch.out 2> $OUT/watch.err &
mkdir -p $OUT/rep1/udx $OUT/rep1/layer
$BIN/bench-load --manifest $OUT/manifest.txt --target $PLAIN --workload W1 --procs 4 --out $OUT/rep1/udx/w1
$BIN/bench-load --manifest $OUT/manifest.txt --target $PLAIN --workload W4 --procs 4 --out $OUT/rep1/udx/w4
$BIN/bench-load --manifest $OUT/manifest.txt --target $LAYER --workload W1 --procs 4 --out $OUT/rep1/layer/w1
$BIN/bench-load --manifest $OUT/manifest.txt --target $LAYER --workload W4 --procs 4 --out $OUT/rep1/layer/w4
# what the watcher spent, which the writer's records do not show: counters and cpu_ms
kill -USR1 $(pidof udx-watch); tail -1 $OUT/watch.err
grep -E 'VmRSS|VmHWM' /proc/$(pidof udx-watch)/status
du -cb $PLAIN/*/.udx.db* | tail -1          # against index_used of: $BIN/query $LAYER info

# equal answers first, on that data set
$BIN/moobfs-index --domain $PLAIN/ns3 namespace eq string ns3 agent eq string a07 > $OUT/eq-oracle.txt
$BIN/udx-query $PLAIN/ns3 namespace eq string ns3 agent eq string a07 | cmp - $OUT/eq-oracle.txt

# queries: the shapes q2-campaign.sh times with QUERY2 on ns3, through the harness's SQLite client
chown -R 1001:1001 $PLAIN/ns3        # the running watcher re-evaluates ns3 and gives its database to uid 1001
while read -r q; do $BIN/udx-query --sql $PLAIN/ns3 $q; done < $OUT/q-ns3.txt > $OUT/q-ns3.sql
$BIN/udx-query --sql --order created time desc --limit 20 $PLAIN/ns3 kind eq string note > $OUT/q-recent.sql
mkdir -p $OUT/rep1/udx-ns3 $OUT/rep1/udx-recent $OUT/rep1/v2-ns3
setpriv --reuid 1001 --regid 1001 --clear-groups $BIN/bench-sqlclient --db $PLAIN/ns3/.udx.db --sql $OUT/q-ns3.sql --out $OUT/rep1/udx-ns3/w3 --repeat 50
setpriv --reuid 1001 --regid 1001 --clear-groups $BIN/bench-sqlclient --db $PLAIN/ns3/.udx.db --sql $OUT/q-recent.sql --out $OUT/rep1/udx-recent/w3 --repeat 50
$BIN/bench-load --workload W3 --target $LAYER/ns3 --root $LAYER/ns3 --query2 1 --uid 1001 --queries $OUT/q-ns3.txt --repeat 50 --out $OUT/rep1/v2-ns3/w3
python3 bench/report.py --out $OUT/report $OUT/rep1

# start-up: time to UDX_WATCH_READY on an existing index, against W7 of the layer (mount plus first query)
```

`$OUT/q-ns3.txt` is the file `q2-campaign.sh` writes (three lines). Things to keep in mind when
reading such numbers: `bench-sqlclient` times prepare plus step for each statement, and
`bench-load` times one ioctl; `udx-query --sql` writes the values into the statement, so the
planner sees them, unlike the bound parameters `udx-query` itself uses; `bench-sqlclient` opens
the database read-write and, as root, wakes the watcher once per open. Freshness has no
instrument yet: the test's marker is a shell loop and is too coarse for a host.

## Next step

Take these commands to the dedicated host when it is available, on the data set of the Q2
campaign, and before that write an in-process freshness probe (set an attribute, query until it
shows, one record per probe, in the format of `bench/common.h`), since lag is the number this
baseline most needs and the only one with no tool. `docs/history/README.md` and `CHANGELOG.md`
were left for the maintainer to merge.

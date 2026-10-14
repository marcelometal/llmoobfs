# 0098 — ADR-0012, first half: the index of a domain is built by the first query on it

Date: 2026-10-13 and 14. Status: implemented; tested in the VM, under KASAN, UBSAN and lockdep
on the benchmark host, and measured there. Not merged: the maintainer decides after reading
this. No outside reader has seen the code, which ADR-0012 asks for if one is available.

## Goal

Items 1, 2, 5 and 6 of ADR-0012: nothing is scanned at mount; a domain's part of the index is
built by the first query on it, while the domain stays writable; a write to a domain that is
not built does not touch the index; the mount-wide `QUERY` builds what is missing; `GET_INFO`
says how much is built. Items 3 and 4 (releasing domains under memory pressure, the budget
releasing the least recently used) are not in this journal.

## What was built

`layer/index.c` (most of the change), `layer/main.c`, `layer/moobl.h`,
`include/moobfs_ioctl.h`. Specification: `docs/spec-query-v2.md`, section 9.

- **States.** `struct moobl_domain` per domain directory, plus one for what belongs to no
  domain: not built, being built, built. A mutex per domain serializes its builders.
- **Build.** The mount-time scan became `domain_ensure()`: the same walk and the same
  homogeneity rule, started at a domain directory. `QUERY2` and `QUERY2_BATCH` call it after
  the authorization check, so a caller who may not query a domain cannot make it be built.
- **`GET_INFO`.** Flag `MOOB_FLAG_ON_DEMAND` (0x40), `domains_built`, `domain_builds`, in
  space that was padding or reserved. The counters now cover the built domains.
- **Tools and tests.** `tests/query.c` gains `build` (the mount-wide `QUERY`, which builds
  everything) and prints the new fields; the suite calls it before reading counters.
  `tests/buildrace.c` and the suite step `buildrace` (below). `tests/failnth.c` gains the
  first query on a domain nobody queried, so every allocation of a build is failed in turn,
  and `vm/kasan-guest.sh` compares the domains built under faults with the oracle.
  `bench/ondemand.sh` measures.

## Prior art used

- The online index build of C. Mohan and I. Narang (SIGMOD 1992, doi:10.1145/130283.130337):
  build while updates continue. For attribute writes the variant without a side file is used
  (writers update the index directly; the scan must not overwrite them with an older reading);
  for directory renames a side list, applied before the build ends, as in their side-file
  variant.
- The dcache's `rename_lock` sequence counter (`include/linux/seqlock.h`, `fs/dcache.c`, Linux
  6.12.112), the way path lookup and `d_path` read a chain of parents consistently.
- `docs/references.md` is updated with both as implemented.

## How the build stays correct while the domain is written

What follows is the mechanism as it stands. Each part was added after a test showed the one
before it was not enough; the section after this one lists those failures.

1. The scan announces the file it is reading (`struct moobl_loader`), reads its attributes
   from the lower filesystem with no lock held, and replaces the file's entries under the
   index lock only if no write path touched the same file meanwhile (`index_touched()`),
   reading again otherwise. A file is absent from the index or whole in it.
2. A directory renamed during the build is recorded in the domain's side list
   (`struct moved_dir`) and scanned again, in the order of the renames, before the domain is
   declared built; the check that the list is empty and the change of state happen under one
   hold of the lock.
3. A file renamed (or hard-linked) while its record may be partial is read whole: in a domain
   being built, a write creates the record of a file the scan has not reached with only the
   attribute written.
4. Where a file is (depth and domain directory) is one consistent reading under `rename_lock`
   (`where_is()`), and a write computes its placement again if a rename or link completed
   meanwhile (`place_and_lock()`).
5. A write to an inode with no link left is not indexed.
6. A directory renamed in a built domain makes the domain stale; the next query takes it down
   and builds it again, which also recomputes the homogeneity of the files below the
   directory. Queries hold `ix->query_sem` for reading while they run, so a domain is not
   taken down under a running query.

## Defects found on the way, with their fixes

All in code of this journal unless marked.

| Found by | Defect | Fix |
| --- | --- | --- |
| fault injection on the build | Under an allocation failure a build could succeed and leave a file out: a name that could not be recorded, or an ACL that could not be read, was tolerated. 11 of 41 faults "succeeded anyway". | A build fails on either; 3 of 41 remain and the domains built under faults equal the oracle. |
| negative run | The first race test passed with the concurrent-write check removed: the window lasts microseconds and no write landed in it. | Test-only module parameter `scan_delay_us` widens the window; `scan_retries` proves writes landed; the step fails if none did. |
| race test | **In `main` since journal 0096:** the walk from a file to its domain read the depth and the ancestor in two passes; a rename between them returned the wrong directory, the write was taken for one outside every built domain, and the index was not updated. Before on-demand building the same race could file an entry under the wrong partition. | `where_is()` under `rename_lock`. |
| race test | The check compared one counter bumped by every update of the mount. Under a steady write load no build ever finished (`EAGAIN` 200 times in a row). | Conflict per file. |
| race test | A directory rename restarted the whole build; with renames arriving faster than a build, it never finished. Before that, the test's own directory rename changed depth and was refused with `EXDEV`, so the path had never run. | Side list; the test renames between siblings. |
| race test | After a directory rename, files not in the inode cache had no name in results (the documented limit "until the next mount"). | A stale domain is built again by the next query. |
| race test, 3 of 3 repetitions | A file renamed during a build with a partial record stayed partial (missing from answers). | Item 3 above. |
| race test, 1 of 3 | **Older than this work:** a write to a file unlinked a moment before left a record under the old name for ever (an extra answer). | Item 5 above. |
| reading the code | **Older than this work, not confirmed by a test in isolation:** a directory moved, inside one domain, below a directory that is more private did not take its files out of the answers given to other users. | Item 6 above. The suite has a test for it that passes; no negative run isolates it, because the negative that removes item 6 fails earlier, on names. |

Errors of the harness: one green run of the race test was taken as passing and the next three
runs failed; since then a change to this code counts as tested after five repetitions.

## Tests run

- `make vm-test` complete, locally, with the final code: `MOOBFS_ACCEPTANCE_PASS`,
  `MOOBFS_LAYER_PASS` and `BUILD_RACE_PASS` on ext4, btrfs and XFS.
- `MOOB_ONLY=buildrace make vm-test`, five repetitions with the final code: 15 of 15 passes
  (ext4, btrfs, XFS), 24 domains each, 3 158 to 8 371 concurrent writes per run, 23 to 72
  readings repeated per run, no `EAGAIN`.
- Negative runs of that step, each with one protection removed, each failing at the first
  try: the per-file check (one answer too many), item 3 (one missing), item 5 (one too many).
  Earlier negatives, same result: item 6 (names differ), the order of the side list (one too
  many; this run does not establish that the order was the cause).
- `sh vm/kasan-local.sh` complete on the benchmark host with the final code, kernel 6.12.112
  with `vm/kasan.config`: `MOOBFS_KASAN_PASS`, `BUILD_RACE_PASS` on the three filesystems,
  `FAULT_INJECTION_PASS` (284 faults in 302 calls, none unexplained), no kernel diagnostic.
  The code under that run differs from the committed code by comments only. After two loops
  of the test scripts were rewritten for shellcheck, the `buildrace` step was run again
  locally and `buildrace` with `faults` under KASAN: both pass.
- `sh scripts/checkpatch.sh`: exit 0.

## Measured

`bench/results/2026-10-13-macmini-ondemand/`, 60 000 memories in eight domains on ext4, the
module of journal 0084 against this one, one run each:

| | built at mount | built on demand |
| --- | --- | --- |
| mount, cold | 19.6 s, 20.0 s | 0.31 s |
| mount, warm | 0.76 s | 0.004 s |
| first query on one domain (7 459 files), cold / warm | 0.02 s / 0.006 s | 2.7 s / 0.10 s |
| index memory with one of eight domains in use | 36.4 MB | 4.5 MB |
| mount-wide `QUERY` right after mount, cold / warm | 0.13 s / 0.003 s | 19.7 s / 0.74 s |
| index once everything is built | 36 443 423 bytes | 36 443 423 bytes |

ADR-0012 predicted 0.1 s warm and 2.3 s cold for a domain of 7 500 files; measured 0.10 s and
2.7 s. Steady-state `QUERY2` latency on the built domain: three shapes 5 to 13 % slower, two
faster, all marked inconclusive by the report tool (three repetitions; the modules differ by
more than this change). ADR-0012 required that steady state not get worse: not shown either
way by this run.

## Limitations

- **The cost moved, it did not go away.** The first query on a domain pays its scan, and the
  first mount-wide `QUERY` pays what the mount used to.
- **A directory rename inside a built domain costs a rebuild of that domain** at the next
  query; a rename of a domain directory or of one above costs it for every domain. Not
  measured beyond the first-query numbers above. A workload that renames directories and
  queries in turns would rebuild every time; `domain_builds` in `GET_INFO` shows it.
- **A build can still give up** (`EAGAIN`): 16 readings of one file each overtaken by a write
  to that same file, or directories renamed for 64 rounds. Seen only with the test's
  artificial delay at 300 µs under the first, coarser check.
- **Nothing releases a built domain yet**, so memory only grows until unmount, and the budget
  rule is unchanged: a build that does not fit fails with `ENOMEM` for its callers, and an
  update that does not fit still marks the index incomplete for everyone.
- `scan_delay_us` and `scan_retries` are test hooks in the production module. S13 (tracepoints)
  is meant to replace the second.
- Homogeneity after a permission change on a directory is still handled by the fallback of
  journal 0035, not by a rebuild; the two mechanisms now coexist.
- The partition of a file with several hard links is still not recomputed when one of its
  names goes (open since journal 0083).
- The suite mounts with `domain_depth` 0 and 1 (the default); the race and fault tests use
  depth 1 only. Depths above 1 were not run with this code.
  One benchmark host, one data set, one run.
- `README.md`, the site and several documents still say that every file is scanned at mount.
  They are not changed here: the numbers they quote are those of the released module, and
  they should change with the merge.

## Next step

The maintainer's decision on merging. Then the second half of ADR-0012 (shrinker, budget,
tracepoints of S13), and the subvolume test by device (journal 0097).

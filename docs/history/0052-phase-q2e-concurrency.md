# 0052 — Phase Q2e: QUERY2 under concurrency (spec test 8)

Date: 2026-10-12. Status: implemented and tested in the suite VM (Debian 6.12.111, ext4, btrfs and
XFS) and on the Mac mini host; KASAN by CI on the pull request.

## Goal

The last phase of `docs/spec-query-v2.md`. Test 6 (renames across domains) already landed with Q2a
(`EXDEV` for directories, repartition for files), so Q2e is test 8: the race of journal 0020
applied to `QUERY2`, including `chmod` of the domain directory during queries.

## Changes

- `tests/query.c`: mode `race2 <seconds>` on a domain directory. It pages `QUERY2` with limit 7 and
  cycles three plans (plain scan of `kind = note`, equality plan with `kind` and `ns`, ordered by
  `created` descending). Every result must be NUL-terminated, relative, without `..`, with a
  consistent `path_len`, and openable with `openat` on the domain descriptor, unless it vanished
  (`ENOENT`), was denied (`EACCES`) or was recreated under the same name (different inode) after
  the answer. `ESTALE` (the domain switched to fallback during paging) restarts the pagination;
  `EACCES` from the ioctl (domain directory closed by `chmod`) is counted per episode.
- `vm/suite.sh`: block `QUERY2_RACE_PASS`. Domain `dr` with 40 files (`kind`, `ns`, `created`),
  remount so the domain starts homogeneous, then a racer for 5 s: mode changes, rename and back,
  unlink and recreate with attributes, every fifth file moved to domain `dr2` and back, and every
  seventh round `chmod 0711` then `chmod 0755` on `dr`. Afterwards: leftovers of an interrupted
  rename or move are tidied, the domain must be in fallback, and the answer, paged by 3 or not,
  must equal `moobfs-index --domain`.

## Error found and fixed (in the test)

The first run passed on all three file systems but reported about 85% of "paginations" with the
domain closed, although the racer closes it for one `chmod` out of every seven rounds. Checked on
the host: without a race, `QUERY2` answers `EACCES` at 0711 and succeeds again at 0755, and the
layer reports the right mode; with and without the domain `chmod`, the race read the same number
of pages (about 73 900). The count was an artifact: a refused call returns in microseconds, so
the reader retried thousands of times inside each window of about 1 ms, and each retry counted as
a pagination. `race2` now counts complete paginations only, and closed-domain episodes separately
from refused calls. Host check after the fix: 642 racer rounds, 91 closed episodes (642 / 7 =
91.7), 12 290 complete paginations, 73 945 pages, no malformed result.

## Evidence

`make vm-test` with the corrected client: `QUERY2_RACE_PASS` on ext4, btrfs and XFS (1 116 to
1 135 complete paginations, about 6 800 pages and 44 000 openable results each, 16 to 18
closed-domain episodes, no malformed result), with every earlier marker up to `MOOBFS_LAYER_PASS`.
No `ESTALE` restart happened: the fallback switches once, at the first directory `chmod`, and stays
on until remount, so a pagination rarely spans the switch.

## Limitations

- Five seconds of one interleaving per file system; the absence of races is not proven.
- Keyset paging allows an entry whose key changes between pages to appear twice or not at all;
  the test does not check duplicates across pages, only the final answer at rest.
- The `ESTALE` restart path was not exercised by this run (see Evidence).

## Next step

With Q2a to Q2e done, the QUERY2 series of ADR-0009 is complete. Next: a test with a real memory
client, to measure the gain in actual use rather than in isolated queries.

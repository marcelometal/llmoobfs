# 0103 — Releases measured; a cursor tested across a release

Date: 2026-10-14. Status: measured on the benchmark host; test run in the local VM (results below).

## Goal

Two items left open by journal 0101: what a released domain costs when it is queried again,
and steady-state latency, which ADR-0012 said must not get worse and journal 0098 could not
show either way. And one claim made from the code only: a `QUERY2` cursor resumes after its
domain was released and built again.

## Changes

- `bench/release.sh` (new): on one mount, a domain released by the shrinker and queried again,
  with the page cache kept and dropped; then every domain queried in turn, two rounds, under an
  `index_budget` that holds a few domains and under the mount's own.
- `bench/ondemand.sh`: `REPS` sets the repetitions of the steady-state part (default 3, as
  before).
- `vm/suite.sh`, step `release`: twelve files in one domain carry a value longer than the
  cursor's inline space (96 bytes) and two short ones. On the mount with `index_idle=0`, four
  paginations take a first page of five, release every built domain through `drop_caches`,
  check that the release counter moved, and fetch the remaining pages with the cursor; the
  pages together must equal the whole answer, in its order. Shapes: ordered by the long value,
  ascending and descending; unordered; an intersection of two equalities.

## Results

`bench/results/2026-10-14-macmini-release/`, 60 000 memories in eight domains on ext4 (the data
of journal 0098), modules of journals 0102, 0098 and 0084:

- **A release costs a rebuild, at the price of a first build with the same cache.** For 7 500
  files: 136 to 147 ms with the page cache kept, 2.07 to 2.56 s with it dropped; a query on the
  built domain takes 6 to 11 ms.
- **A budget smaller than the working set, accessed in a loop, rebuilds on every query.** With
  room for 2.5 of 8 domains, the second round of queries over the eight costs 152 to 173 ms each
  (16 builds, 14 releases), against 4 to 10 ms when all fit.
- **Steady state, ten repetitions:** the module with releases equals the one without (within 4 %
  on five shapes). Against the module built at mount, the three filtered shapes have medians 8
  to 15 % higher and the two "newest 20" shapes are equal; the ranges between repetitions
  overlap on every shape. ADR-0012's condition is therefore still not shown: the difference is
  consistent in direction, smaller than the spread, and the modules differ by fourteen journals
  of changes.

Test, local VM (Debian 6.12.111, QEMU), `MOOB_ONLY=release make vm-test`: `RELEASE_PASS` on ext4,
btrfs and XFS, with the four cursor paginations across a release; 40 to 44 releases in the race
block. Negative run: with the cursor key replaced at each release (a cursor tied to the built
state), the step fails at `rl-cursor-long-asc-ext4`, so the check sees the release between the
pages. The test is deterministic, so one run per filesystem.

Why the cursor survives, from the code: `query2_pin()` builds the domain and holds the query
semaphore before `cursor_decode()` reads it, and the cursor carries a value and an inode number
under a per-mount key, none of which a release changes.

## Prior art for the loop

Least-recently-used replacement evicts every page of a loop larger than the cache just before
it is needed; 2Q (Johnson and Shasha, VLDB 1994), LIRS (Jiang and Zhang, SIGMETRICS 2002) and
ARC (Megiddo and Modha, FAST 2003) were designed against that, and the kernel's own page cache
keeps an active and an inactive list for the same reason. Not adopted here: the domains are few,
a domain is rebuilt from the page cache in about 0.15 s, and the administrator can raise
`index_budget`; `domain_builds` and `domain_releases` in `GET_INFO`, and the tracepoints, show
the loop when it happens. Recorded in `docs/references.md`.

## Limitations

- One host, one data set, one run of each script; nothing else running on the host was
  intended, not verified.
- The shrinker was still driven by `drop_caches`, not by real memory pressure.
- The cursor test pages through twelve files in one domain on a mount with no other activity;
  a release racing with the middle of a `QUERY2` call is excluded by the semaphore, not tested.
- The steady-state gap against the module built at mount is not explained; a profile of one
  shape on both modules would say whether it is the on-demand checks on the query path or
  another of the commits in between.

## Next step

KASAN of the branch tip through the pull request. Then the profile above, if the gap matters
to a client; the user-space query library; packaging.

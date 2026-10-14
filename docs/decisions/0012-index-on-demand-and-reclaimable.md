# ADR-0012 — The index is built per domain on demand and can be released under memory pressure

Date: 2026-10-13. Status: **accepted** by the maintainer on 2026-10-13. Items 1, 2, 5 and 6 are
implemented and measured in journal 0098 (2026-10-14), which also records where the
implementation departs from the sketch below; items 3 and 4 are not implemented. Agreed
order: fault injection in the test suite first (journal 0095), then this. It changes how
[ADR-0009](0009-query-role-and-user-space-index.md) builds the index, not what it answers.

## Context

Two costs of the layer come from one design choice, the index built whole at mount and kept
whole in kernel memory (M4 report, journal 0088):

- **Mount time.** Every file is scanned before any query is answered: 0.7 s warm and 18.5 to
  19.3 s cold for 60 000 files on a SATA SSD, 126 s cold on a cloud disk, linear in the files.
- **Memory.** 530 to 607 bytes per file by the index's own count, which cannot be paged out, for
  every domain whether anybody queries it or not. At the budget (128 MiB by default) the index is
  marked incomplete for everyone until the next mount (ADR-0011, finding F2).

The comparison with an index kept in user space (journals 0092 and 0094) showed these two as
the points where that alternative is plainly better: its index survives a restart and its memory
is pageable. The maintainer's direction of 2026-10-13 is to keep the index in the kernel and
remove these costs with kernel techniques.

## Decision

1. **A domain's partition is built when it is first needed**, not at mount. Mount reads the lower
   root and returns. Each domain is in one of three states: not built, being built, built. The
   first `QUERY2` on a domain builds its partition by scanning that domain's subtree, with the
   rule and the validation of today's mount scan; other callers of the same domain wait for it.
2. **An attribute write updates the index only if the file's domain is built.** For a domain that
   is not built there is nothing to keep current; the write is validated as today and goes to the
   lower filesystem.
3. **Built partitions can be released.** The module registers a shrinker. Under memory pressure
   the kernel asks it for memory, and it drops the partitions least recently queried, which
   return to "not built". A later query rebuilds them.
4. **The budget becomes a limit on what is built**, enforced by releasing the least recently
   used partitions, instead of a point after which the index is incomplete for all. A single
   domain larger than the budget still fails, and says so to its own callers only.
5. **The mount-wide `QUERY`** (administrators, or `domain_depth=0`) needs every partition; it
   builds what is missing first. Its cost is then explicit to the caller who asks for it.
6. `GET_INFO` reports how many domains are built and flags that the counters cover those only.

No change to the attribute encoding, to `QUERY2` requests or replies, or to what a query
returns. A new flag bit and two fields of `GET_INFO` taken from reserved space.

## Prior art this builds on

Nothing in this decision is a new technique. Each part follows something already solved, and the
implementation should reuse the kernel's facility rather than write its own.

| Part | Known solution | Where (verified on 2026-10-13) |
| --- | --- | --- |
| Releasing memory when the kernel asks | The shrinker interface: a cache registers a `count_objects` and a `scan_objects` callback and reclaim calls them | `include/linux/shrinker.h` (`shrinker_alloc`, `shrinker_register`), Linux 6.12.112 |
| Keeping "least recently used" cheaply and per NUMA node and memory cgroup | `list_lru`, as the dentry and inode caches do (`prune_dcache_sb` calls `list_lru_shrink_walk`) | `include/linux/list_lru.h`, `fs/dcache.c`, `fs/inode.c` |
| A filesystem that releases an in-memory map of its own under pressure and rebuilds it from disk on demand | The btrfs extent-map shrinker, which walks inodes and drops extent maps that can be read again | `fs/btrfs/extent_map.c` (`btrfs_extent_map_shrinker_worker`) |
| Building an index while the data keeps changing | Online index build: scan, record the changes made meanwhile in a side list, apply the list at the end. C. Mohan and I. Narang, "Algorithms for Creating Indexes for Very Large Tables Without Quiescing Updates", SIGMOD 1992 | doi:10.1145/130283.130337 |
| Readers that must not see a half-built state, without a lock on the fast path | A sequence counter that a reader samples and re-checks, as path lookup does against renames | `include/linux/seqlock.h`; `rename_lock` in `fs/dcache.c` |
| Seeing what the shrinker does while testing | The shrinker debugfs interface, which can also be asked to scan | `Documentation/admin-guide/mm/shrinker_debugfs.rst` |

The side-list algorithm of Mohan and Narang is the "per-domain generation" described below; the
paper's two variants differ in whether writers insert into the new index directly during the
build, which is the choice the implementation has to make and justify.

## What has to be got right

- **A scan that runs while the domain is being written.** Today's scan runs before anyone can
  write. The new one must not miss a write that lands during it, nor index a state that a rename
  or an unlink has already undone. The proposal is a per-domain generation: writes during
  "being built" are recorded and applied after the scan, and the scan's own reads are
  re-validated against that list. This is the part that needs the most review and tests.
- **Lock order.** Releasing a partition from the shrinker runs in reclaim context, where the
  index lock must not be taken in a way that can deadlock (journal 0082). The shrinker may only
  try the lock and give up.
- **First-query latency.** The caller of the first query on a domain pays the scan: by the
  measured per-file costs, a domain of 7 500 files is about 0.1 s warm and 2.3 s cold on the SSD.
  A way to ask for a build ahead of time (reading a domain directory is a candidate trigger)
  must exist so that a service can warm what it needs.
- **Thrashing.** A machine short of memory can release and rebuild the same partitions in a
  loop. The shrinker needs a floor (recently used partitions are kept) and a counter in
  `GET_INFO` so that it is visible.

## Alternatives considered

- **A persistent index on the lower filesystem**, validated at mount. It removes the scan and,
  kept in the page cache, the unpageable memory too. Not chosen first: it adds an on-disk format
  with its own compatibility promise (`docs/compatibility.md`, section 2) and the problem of an
  index that disagrees with files changed while the layer was not mounted. It stays the next
  step if on-demand building is not enough, and this decision does not close it.
- **Building in the background after mount.** Queries are answered sooner only for domains
  already reached, memory is still all unpageable, and it adds a kernel thread.
- **Leaving the index in user space** (journals 0092, 0094). It gives up validation at write
  time, an answer consistent with a write, and sharing between users without a privileged
  process. Deferred by the maintainer to the end, as a no-module mode behind the same interface.
- **Keeping things as they are.** The two costs grow with every file and are the first thing an
  operator meets.

## How it will be judged

- Answers equal to the oracle on the three lower filesystems after on-demand builds, after
  releases forced by the suite, and under a race of writes, renames and unlinks against a build.
- The KASAN and lockdep job, with the reclaim priming of journal 0082, with no diagnostic.
- Measured on the benchmark host against today's module: mount time, first-query time per
  domain size, steady-state query latency (must not get worse), memory with one of eight
  domains in use.

## Consequences

Mount becomes immediate and memory follows use. The cost moves to the first query on each
domain and to more states in the index code, which is the code where this week's defects were
found. The change is larger than any made to the index since ADR-0009 and should not be merged
without a reader from outside, if one is available by then.

## Note of 2026-10-14: what the implementation did differently (journal 0098)

The text above is kept as decided. Three points changed while implementing items 1, 2, 5 and 6:

- **"A per-domain generation" became a conflict per file.** One counter per mount, then the
  idea of one per domain, made a build re-read on any write; under a steady write load no build
  finished. The scan now announces the file it is reading and re-reads it only if that same
  file is written.
- **Directory renames use a side list**, the other variant of the cited paper, and after the
  build they make the domain stale: the next query builds it again. The decision did not
  foresee that a rename also changes the homogeneity of the files below the directory.
- **"Readers that must not see a half-built state"** are kept out by a semaphore that queries
  hold for reading, not by a sequence counter on the fast path. The sequence counter the table
  cites (`rename_lock`) is used for another purpose: reading where a file is.

The warm-up trigger ("reading a domain directory is a candidate") is not implemented; the
mount-wide `QUERY` warms everything, and a `QUERY2` warms one domain.


## Note of 2026-10-14: items 3 and 4 (journal 0101)

Implemented as decided, with these choices:

- **No `list_lru`.** The least recently queried domain is found by walking the domains of the
  mount. The domains are few, and the index memory is not charged to a memory cgroup, so the
  per-node, per-cgroup lists of `list_lru` would buy nothing here.
- **The floor** is the mount option `index_idle` (seconds, default 60), as agreed on
  2026-10-13. It applies to the shrinker only; a build over the budget may release a domain
  used a moment ago, since the budget is a limit the administrator set.
- **The mount-wide `QUERY` never releases** to make room: it needs every domain at once.
- **What the shrinker reports as freeable is an estimate.** The index does not count memory per
  domain; per-domain accounting would make the count exact and was left for later.
- The counter of releases is in `GET_INFO` (`domain_releases`); `domain_builds` became 32-bit
  to make room, before either was published.

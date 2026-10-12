# 0028 — `QUERY` profile and dentry revalidation compatible with RCU-walk

Date: 2026-10-11. Branch: `m3/rcu-revalidate`. Status: implemented, validated in the VM and
measured on the Azure host (pilot 4).

## Question

After journal 0027, the remaining cost of `QUERY` was about 2 µs per result. The question was
whether Linux already offers a mechanism to reduce that cost without weakening the
authorization, instead of us inventing a cache of our own.

## Measurement

On the Azure host (benchmark kernel with `function_graph`, kprobes, `perf` and `bpftrace`),
`perf` sampling of 3000 runs of shape Q7 (513 results), evidence in
`bench/results/2026-10-11-azure-profile-q7/`. Breakdown of the time inside `do_query`:
path resolution with `vfs_path_lookup` 56%; `do_query` itself (scan, filters, path assembly)
12%; copy to the user 7%; `getname_kernel` (copy of the path as text, with a 4 KB clear) 5%;
final permission check, `kstrdup`, `kfree` and `path_put`, the rest.

Inside path resolution appear `__d_lookup`, `try_to_unlazy_next`, `dput` and `step_into`, which
belong to the VFS reference mode (ref-walk), and almost nothing of `__d_lookup_rcu` (0.4%
against 2.9%). The cause is in `moobl_d_revalidate`, which returned `-ECHILD` whenever called
with `LOOKUP_RCU`. Because of that, every path resolution that goes through the layer abandons
the lockless mode (RCU-walk) right at the first component. The same holds for any `open`
through the layer, which explains part of the overhead measured in W2.

## Alternatives considered

- Per-directory authorization cache, within a query: it would reduce the number of walks, but
  it is a mechanism of our own and touches the phase 3b authorization contract. Discarded while
  the VFS's own mechanism is not in use.
- `open_by_handle_at`: requires `CAP_DAC_READ_SEARCH` and does not check the directories of the
  path. Not suitable for authorizing an ordinary caller.
- Landlock and AppArmor: they are access policies, they do not speed up the check.

## Decision

Use the VFS's own RCU-walk. `moobl_d_revalidate_rcu` answers "valid" without locks, without
references and without sleeping, only in the simple case: the lower has no revalidation of its
own (local ext4, btrfs and XFS do not), the lower dentry is still hashed, and the upper dentry
agrees with it (positive and mapped to the same linked lower inode, or both negative). In any
other case it returns `-ECHILD` and the VFS redoes it in ref-walk, which runs the full check as
before. The lower dentry swap in `moobl_instantiate` (fix from journal 0015) now publishes the
pointer with `WRITE_ONCE`; the reader uses `READ_ONCE`. Safety comes from `moobl_dentry` being
freed with `call_rcu` and from the lower dentries only being freed after a grace period. The
attributes are not copied in RCU mode; `getattr` and the name operations already update them.
The authorization contract does not change: the same permissions are checked, without locks.

## Security gap recorded (out of the scope of this change)

The per-result check of `QUERY` uses `vfs_path_lookup` and `inode_permission`. This covers DAC,
ACLs and inode-based security modules, but not those that decide by path at open time
(AppArmor, Landlock). A process confined by them could see in the results a file it would not be
able to open. It goes to the security roadmap.

## Tests

`make vm-test` (Debian 6.12.111): `ISOLATION_PASS` and `BENCH_LAYER_PASS` on the three lowers,
`MOOBFS_ACCEPTANCE_PASS` (including the SQLite WAL case with removal and recreation, which is
the dentry swap scenario) and `MOOBFS_LAYER_PASS`; concurrency test with about 600 queries per
lower, all well formed. KASAN and lockdep: through the pull request workflow.

## Measurement on the Azure host (pilot 4 and repeated profile)

Module from commit `28f6efa` (pilot 3 tree plus this change). Repeated profile of shape Q7
(`bench/results/2026-10-11-azure-profile-q7/perf-children-rcu.txt`): `__d_lookup_rcu` went from
0.4% to 3.3% of the samples, `__d_lookup` and `try_to_unlazy_next` disappeared, and path
resolution fell from 56% to 43% of the time of `do_query`. The loop of 3000 Q7 queries took
4.81 s, stable over three rounds.

Pilot 4 (`bench/results/2026-10-11-azure-pilot4-s1/`), 30 equivalences passed. Medians on
ext4, as root, against pilot 3:

| Shape | Results | SQLite | Pilot 3 | Pilot 4 |
| --- | --- | --- | --- | --- |
| Q8 | 0 | 19 µs | 4.9 µs | 4.9 µs |
| Q3 | 3 | 16 µs | 34 µs | 33 µs |
| Q4 | 23 | 26 µs | 73 µs | 66 µs |
| Q5 | 70 | 43 µs | 144 µs | 126 µs |
| Q2 | 100 | 56 µs | 217 µs | 190 µs |
| Q6 | 212 | 99 µs | 403 µs | 339 µs |
| Q9 | 291 | 78 µs | 544 µs | 460 µs |
| Q1 | 390 | 90 µs | 744 µs | 636 µs |
| Q7 | 513 | 224 µs | 1049 µs | 914 µs |

btrfs and XFS follow the same pattern (Q7 976 and 915 µs; Q3 33 µs; Q8 4.9 µs). W2 on ext4 went
from 12.9 to 12.5 µs (9.3 µs without the layer); W4 without relevant change.

Reading: a real gain, but a modest one. The per-result cost fell from about 2.0 to 1.7 µs,
against 0.2 to 0.4 µs for SQLite. What remains of path resolution is work that RCU-walk does
not eliminate: assembling the path as text, copying it again in `getname_kernel`, walking four
components, calling `moobl_permission`, which repeats `inode_permission` on the lower inode, at
each component, and the final check on the file. For queries that return hundreds of results,
`QUERY` remains 4 to 7 times slower than SQLite with a composite index.

## Next step

Decide, by ADR, whether per-result authorization may reuse the decision for each directory
within a single query, instead of resolving the full path of each result. This would reduce the
per-result work to a check on the file, but it changes how the phase 3b contract is fulfilled
and needs isolation and concurrency tests of its own.

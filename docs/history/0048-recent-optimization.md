# 0048 — Optimization of "N most recent" in `QUERY2`

Date: 2026-10-12. Status: implemented, tested in the suite's VM and measured on the host of journal 0042. Branch `m3/query-v2-recent`, on top of the Q2d one.

## Goal

Journal 0047 showed SQLite ahead of v2 in "N most recent": 47 µs against 185 µs with
`kind = note`, and 279 µs against 1.7 ms with `agent = a07` (a filter that matches about 3% of the
domain). Maintainer's request: optimize.

## Diagnosis

`perf` on the host, with the selective query repeated 2 000 times: 47% in `inode_matches_all`
(walks the attribute list of each inode), 21% in the scan loop, 13% in `rb_prev`, 9% in
`strcmp`. Reading the code showed the main cause, which the profile only suggested: without
per-result checking, the scan gathered candidates in batches of 256 even when the request
was for 20. With a 3% filter, gathering 256 candidates means walking practically the entire domain
(about 7 500 entries) to use 20. The estimate of journal 0044 (about 600 entries at
2.8 µs) was wrong: it was about 7 500 entries at about 0.23 µs.

## Changes (`layer/index.c`)

1. **Batch limited to what is missing:** outside fallback mode, each batch gathers at most the
   results still missing plus one (that one only serves to mark `truncated`). Applies to the three
   plans (simple, ordered and equality scan). In fallback mode the batch stays
   at 256, because candidates can be rejected by the permission check.
2. **Attribute per name node:** within a domain, all candidates are in the homogeneous
   partition, so each term corresponds to a name node; the search in the inode's list compares
   pointers instead of `strcmp` (`entry_find_node`). v1, which walks all partitions,
   keeps comparing names.
3. **Resume key copied once per batch**, from the last entry examined, instead of on every
   entry.

Alternative considered and not done: for ordered queries with a selective equality
term, choose the N best of that term's run with a small heap. With
change 1, the ordered scan already came in below SQLite; the extra plan is left for when a measured
case calls for it.

## Results (p50, same responses; two passes per module)

| Form in domain ns3 | Before (journal 0044) | After | SQLite (journal 0047) |
| --- | --- | --- | --- |
| 20 most recent, `kind = note` | 189 / 183 µs | 19 / 26 µs | 47 µs |
| 20 most recent, `agent = a07` | 1 779 / 1 727 µs | 102 / 141 µs | 279 µs |
| namespace and agent (253) | 166 / 166 µs | 119 / 144 µs | 178 to 476 µs |
| agent and kind (98) | 115 / 114 µs | 82 / 101 µs | 509 µs |
| ns1: namespace, kind and created (1 021) | 695 / 698 µs | 569 / 644 µs | 1 729 µs |

"N most recent" became 7 to 17 times faster and moved ahead of SQLite in both
forms (about 2 to 2.5 times). The other forms gained 10% to 30% from the pointer
comparison.

## Evidence

- Host: ordered responses (first 20, in both directions) and complete pagination with limit 7
  (including `kind = note`, with more than 400 pages), and unordered pagination with limit 50, equal
  to `moobfs-index --domain`: 14 of 14. `q2c-compare.sh` also confirmed identical responses
  between the modules.
- Suite VM (`make vm-test`): `QUERY2_ORDER_PASS`, `QUERY2_MISSING_BATCH_PASS`,
  `QUERY2_LEAPFROG_PASS` and `QUERY2_PASS` on ext4, btrfs and XFS, and all the previous markers
  up to `MOOBFS_LAYER_PASS`. KASAN: by CI on the pull request.

## Limitations

- The pointer comparison assumes that all entries of an inode are in its partition; this
  holds except after a memory failure while changing the partition, in which case the index is already
  marked as incomplete.
- Measured only on ext4, with a warm cache and without concurrent writes.

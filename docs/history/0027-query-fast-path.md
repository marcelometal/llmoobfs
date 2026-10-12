# 0027 — `QUERY`: seek from the first candidate and results copied one by one

Date: 2026-10-11. Branch: `m3/query-fast-path`. Status: implemented, validated in the VM and
measured on the Azure host (pilot 3).

## Origin

M4 pilot 2 (journal 0025, `bench/results/2026-10-11-azure-pilot2-s1/`) showed `QUERY` 2 to 9
times slower than SQLite with a composite index, per shape, and reading `do_query` broke the
cost down into three parts: a fixed cost of about 50 µs per call, a linear scan of the entries
of the first term and about 2 µs per returned result.

## Decisions

1. **No vector the size of the limit.** `do_query` allocated with `kvcalloc` and zeroed a vector
   of `limit` results (up to 4096 × 280 bytes, about 1.1 MB) on every call. Now each authorized
   result is assembled in a single buffer and copied directly into the caller's vector with
   `copy_to_user`, outside the index lock. The UAPI does not change: the vector, `count` and
   `truncated` have the same meaning. On error (`EFAULT`, `ENOMEM`) the content of the caller's
   vector is undefined, as it already was by omission in the contract.
2. **Seek from the first candidate and stop at the last.** The entries of an attribute are
   ordered by (type, value, inode). `seek_first` positions the scan at the first entry that can
   match (equality, greater, greater or equal and prefix: first with a value not lower than the
   term; less and less or equal: first of the term's type) and `past_end` ends the scan when no
   entry ahead can match (type greater than the term's, value beyond the bound, end of the run
   of strings with the prefix). Existence still walks everything. Resumption between batches of
   256 candidates uses the resumption key as before.
3. **The per-result cost stays.** Resolving the path with the caller's credentials and checking
   the permission is the phase 3b contract (journal 0020); nothing changes there.

## Tests

New block in `vm/suite.sh`: one attribute (`mix`) with strings, int64, double, bool and time
across 14 files, and 23 queries covering all operators, including bounds, empty results and
prefixes that exhaust the run, each equal to the `moobfs-index` oracle, plus exact counts. The
existing tests keep covering resumption between batches (600 candidates), isolation,
concurrency and the nine W3 shapes against SQLite and the oracle. `make vm-test`
(Debian 6.12.111): `ISOLATION_PASS` and `BENCH_LAYER_PASS` on ext4, btrfs and XFS,
`MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`. KASAN: through the pull request workflow.

## Next step

Merge after the pull request's KASAN run; then, the full campaign on disks without bursting.

## Measurement on the Azure host: pilot 3 (2026-10-11)

Same host, kernel, disks, seed, command and harness as pilot 2; the module was compiled from
commit `b85d041` (harness of PR 20 with this branch). Evidence in
`bench/results/2026-10-11-azure-pilot3-s1/`; raw package in the pre-release
`bench-2026-10-11-azure-pilot3-s1`. The 30 equivalences passed (kernel, SQLite and oracle).

W3 per shape, median, ext4 (btrfs and XFS follow the same pattern: Q8 4.9 µs, Q3 34 µs, Q7 from
1052 to 1100 µs):

| Shape | Results | SQLite | `QUERY` pilot 2 | `QUERY` pilot 3 | Ordinary user, pilot 3 |
| --- | --- | --- | --- | --- | --- |
| Q8 | 0 | 19 µs | 54 µs | 4.9 µs | 4.8 µs |
| Q3 | 3 | 15 µs | 141 µs | 34 µs | 34 µs |
| Q4 | 23 | 26 µs | 178 µs | 73 µs | 74 µs |
| Q5 | 70 | 43 µs | 248 µs | 144 µs | 150 µs |
| Q2 | 100 | 56 µs | 323 µs | 217 µs | 224 µs |
| Q6 | 212 | 99 µs | 502 µs | 403 µs | 412 µs |
| Q9 | 291 | 78 µs | 608 µs | 544 µs | 561 µs |
| Q1 | 390 | 91 µs | 843 µs | 744 µs | 770 µs |
| Q7 | 513 | 224 µs | 1136 µs | 1049 µs | 1086 µs |

Reading: the fixed cost and the linear scan disappeared. With no results, `QUERY` answers in
less time than the baseline SQLite (which prepares the statement on every call). The remainder
is nearly linear in the number of results, at about 2 µs per result against 0.2 to 0.4 µs for
SQLite, and corresponds to assembling the path, resolving it with the caller's credentials,
checking the permission and copying 280 bytes to the user. Selective queries are 2 to 3 times
slower than SQLite; queries that return hundreds of results, 4 to 8 times. W1, W2 and W4 did not
change relative to pilot 2 (W1 9.84 ms with the layer against 9.86 ms without; W2 12.9 against
9.3 µs; W4 24.5 against 10.1 µs).

Candidates for reducing the per-result cost, still without a decision: authorize each directory
once per query instead of resolving the full path per result (many results share directories),
and avoid copying the path with `kstrdup` under the lock. Either one touches the phase 3b
authorization contract and requires its own decision, isolation tests and KASAN before going in.

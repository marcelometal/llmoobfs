# ADR-0008 — `QUERY` authorization with per-directory reuse within a query

Date: 2026-10-11. Status: **proposal on hold**. Maintainer decision on 2026-10-11: keep
authorization as it is and reevaluate this proposal after M4 shows whether queries that
return hundreds of results are part of real use. Nothing implemented.
It does not replace an earlier decision; it refines how the authorization contract of phase 3b
(journal 0020, `docs/spec-m3.md` §3.1) is fulfilled.

## Context

Since phase 3b, each `QUERY` result is authorized by resolving the path relative to the mount
root with `vfs_path_lookup`, under the caller's credentials, and checking `MAY_READ` on the
file with `inode_permission`. The result only enters the response, and the count, if the caller
could open it at that instant; the window between the response and the open is accepted in the contract.

The pilots on the Azure host (journals 0025, 0027 and 0028) measured the remaining cost at about
1.7 µs per result, against 0.2 to 0.4 µs per row in SQLite with a composite index. The profile with
`perf` shows that this cost comes from redoing, for each result, the whole path: building the
path as text, copying it again in `getname_kernel`, walking each component (with
`moobl_permission` repeating `inode_permission` on the lower inode) and checking the file. In a
typical set, hundreds of results share few directories (in the pilot, 1000
memories in 256 directories), and the check of each directory is repeated for each result.

## Proposal

Within **a single** `QUERY` **call**, keep the traversal decision of each directory already
checked and reuse it for the following results:

1. For each candidate, walk up the dentry chain from the file's parent to the mount
   root, under `rcu_read_lock` and with the rename `seqlock` (`rename_lock`) to detect
   concurrent changes, checking `MAY_EXEC` on each directory with `inode_permission`.
2. Each approved directory enters a set local to the call (dentry address); on finding
   an already approved directory, the walk stops there. Refused directories are also remembered.
3. Check `MAY_READ` on the file, as today.
4. If the `seqlock` indicates a rename during the walk, or any step cannot be done without
   blocking, the candidate falls back to the current path (full `vfs_path_lookup`). The current path
   remains the correctness reference.
5. The set is discarded at the end of the call; nothing is kept between calls or between
   processes.

## Invariants the change must keep

- No result, nor count, that the caller could not open at that instant (outside the
  window already accepted between response and open).
- Same credentials: the caller's at the time of the call, with `nop_mnt_idmap`.
- Same scope: only paths reachable from the root of this mount; a dentry outside it is
  refused.
- A permission change on a directory during the query cannot grant access that the
  current check would deny at any instant of the call. Reusing a positive decision
  made before a concurrent `chmod` within the same call is equivalent to the already
  accepted window; this needs to be written into the contract and tested.

## Alternatives

- **Keep as it is.** Correct and simple; per-result cost documented. It is the option if M4
  shows that queries with hundreds of results are not real use.
- **Cache across calls.** Discarded: it would require invalidation at every `chmod`, `rename` and ACL
  change, and would open room for stale decisions.
- **Authorize only the file.** Discarded: it ignores directory traversal and would break the
  per-directory isolation tested in phase 3b.
- **Remove the paths from the response.** Out of scope: it would change UAPI v1.

## Tests required before accepting

- All current blocks of the suite (isolation with two users, ACLs, 600 candidates in several
  batches, concurrency with `chmod`, `rename` and removal) passing without change.
- New focused concurrency test: `chmod` and rename of intermediate directories in a loop
  while an ordinary user queries, checking at each response that every returned path
  opens or fails only through the accepted window, and that no result from a denied directory appears.
- Equivalence with the oracle running as the same user, in all W3 shapes.
- KASAN and lockdep without diagnostics.
- Measurement on the Azure host, per shape, against pilot 4.

## Related gap, outside this decision

Security modules that decide by path at open (AppArmor, Landlock) are not
consulted by the current check nor by the proposal. It is left to the threat model ADR (S1
of the security roadmap).

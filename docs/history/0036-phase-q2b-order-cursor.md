# 0036 — Phase Q2b: ordering and cursor in `QUERY2`

Date: 2026-10-11. Status: implemented and tested in the suite's QEMU VM (Debian kernel 6.12.111,
ext4, btrfs and XFS); KASAN by CI on the pull request; measurement **not executed** (no
measurement host: the Azure VM is shut down and the local Mac mini is yet to be configured).

## Goal

Phase Q2b of `docs/spec-query-v2.md`: `order_by` with `ASC`/`DESC` and a pagination `cursor`,
for the most common query of real clients ("the N most recent", journal 0033).

## Decisions

- **Explicit ordering type** (`order_type`, taken from the padding; the structure does not change
  size). Alternative: order all entries of the name, of all types, in index
  order (type first); discarded because it would mix times with integers and would give no way to
  reject "wrong type", which the specification requires.
- **Ordering attribute missing in the domain** returns an empty response, not `EINVAL`: absence
  is not an input error. Test 1 of the specification was read this way; recorded there.
- **Key-based cursor** (type, value, inode of the last result delivered), authenticated with
  SipHash and a random per-mount key, together with a digest of the query that includes the
  domain's fallback state. Alternative: store a pointer or a position counter in the cursor;
  discarded because it does not survive changes to the tree and would allow forged cursors
  pointing to internal state. The cursor is not a secret: forging one only allows resuming from another
  position in the same domain, to which the caller already has access; the authentication serves to
  detect a cursor from another query or mount, and a corrupted one.
- **Long values** (more than 96 bytes) are retrieved from the inode's current entry; if it changed
  type, size or prefix, `ESTALE`.
- **Without `order_by`, `nterms` remains ≥ 1; with `order_by`, it can be 0** ("the N most recent"
  of the whole domain).
- The engine (`query_run`) was generalized: it walks the scan attribute's tree in both
  directions, resumes strictly after (or before) a key and returns the key of the last
  result. v1 uses the same path, without a cursor, with unchanged behavior.

## Changes

- `include/moobfs_ioctl.h`: `MOOB_ORDER_ASC`, `MOOB_ORDER_DESC`, `order_type`, the cursor
  contract in the comment.
- `layer/index.c`: `inode_matches_all`, `last_before`, `seek_type`, cursor (`query_hash`,
  `cursor_encode`, `cursor_decode`), `order_check`, `do_query2` with ordering and cursor.
- `layer/moobl.h`: per-mount cursor key.
- `tests/query.c`: `q2` with `--order`, `--limit`, `--pages`, `--cursor-in`, `--cursor-out`;
  `q2invalid` with the ordering fields, garbage cursor (`ESTALE`) and missing attribute.
- `tools/moobfs-index.c`: `--order NAME TYPE asc|desc` and `--limit N`.
- `bench/load.c`: v2 W3 with `--order NAME:TYPE:asc|desc` and `--limit N`.
- `vm/suite.sh`: `QUERY2_ORDER_PASS` block (see `docs/testing.md`).

## Evidence

`make vm-test`: `QUERY2_ORDER_PASS` and `QUERY2_PASS` on ext4, btrfs and XFS, and all the
previous markers up to `MOOBFS_LAYER_PASS`. The suite passed on the first run. The
`bench-load` with `--order` was only compiled; it was not executed.

## Limitations

- Pagination under concurrent changes was not tested; the documented semantics is that of
  key-based pagination.
- A long-value cursor depends on the current value of the inode.
- No measurement: the cost of "N most recent" with selective terms is proportional to the entries
  walked in order until the limit is filled, and this has not yet been measured.

## Next step

Q2b PR after the Q2a merge. On the measurement host: the timing channel test, the per-domain W3
and "N most recent". Then, phase Q2c (intersection by leapfrog).

## Update (2026-10-11)

The `Lint` job of pull request 32 failed: shellcheck (SC2209) read the literal assignment
`t=time` in the suite as an assignment of a command's output. Fixed with quotes in commit
`vm: quote a literal assignment...`; shellcheck 0.10.0 with `--severity=warning` passed
locally on the scripts in `vm/` and `scripts/` before the new push. The local `make lint` does not run
inside the container from a worktree, because `git ls-files` does not find the repository;
the files were passed directly.

KASAN and lockdep in the CI of pull request 32 (kernel 6.12.112-virtme), run 38173795652:
`QUERY2_ORDER_PASS` and `QUERY2_PASS` on ext4, btrfs and XFS and `MOOBFS_LAYER_PASS`, with no report.
The suite step took 50 minutes (22 in Q2a), due to the cost of the paginated queries in the
instrumented kernel. The merge was refused due to a conflict with main (tools batch 2 from the other
session, journal 0039, only in the changelog and in the journal index); resolved by keeping both
entries, which requires a new CI run before the merge.

CodeQL, which entered main with batch 2 from the other session, flagged two "unbounded" writes
(`cpp/unbounded-write`) in `strcpy` of command-line arguments, in `tools/moobfs-index.c`
and `bench/load.c`. Both cases were already preceded by a size check (false
positives), but were replaced by `memcpy` with the size checked against the destination, which
makes the bound explicit; the same pattern in `tests/query.c`, which CodeQL does not compile, was also
replaced.

Later update (2026-10-12): the exception above ended up not being used. Before the merge, main
also received PR 42 (CodeQL triage, with changes in `bench/`) and PRs 43 to 48 from the DX side;
since the change was no longer documentation only, the branch was updated with the final main, the
changelog resolved by keeping both entries, and the merge awaits a new KASAN run.

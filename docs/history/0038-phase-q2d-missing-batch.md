# 0038 — Phase Q2d: "missing or ..." terms and batch in `QUERY2`

Date: 2026-10-11. Status: implemented and tested in the suite's QEMU VM (Debian kernel 6.12.111,
ext4, btrfs and XFS); KASAN by CI on the pull request; measurement **not executed** (no
measurement host).

## Goal

Phase Q2d of `docs/spec-query-v2.md`: the `MOOB_TERM_MISSING_OK` flag (validity and expiration of
the "no deadline or deadline in the future" kind, journal 0033) and `MOOB_IOC_QUERY2_BATCH` (the "or" between
values, such as several tags or types, in one call).

## Decisions

- **Batch with complete requests** instead of results concatenated into a single array (deviation
  recorded in the specification): each request keeps its own `limit` and its own cursor. The
  concatenated alternative would require splitting a single limit among requests and a composite
  cursor.
- **Full validation before executing:** one invalid request rejects the whole batch, and nothing is
  written back. Alternative: execute up to the first error; discarded because it would leave
  partial responses that are hard to interpret.
- **No single snapshot:** each request acquires the index lock separately, like a
  sequence of `QUERY2` calls with a single permission check. Holding the lock for the whole
  batch would block writes for longer without a gain that clients ask for.
- **Flags byte:** the term's `reserved[0]` is now called `flags` (same place and
  size). v1 rejects any flag.
- **"Missing" follows the index:** a different type is present (does not match); an invalid or raw value
  is not indexed and counts as missing. The oracle was adjusted to the same rule.
- **Scan driver:** the first term without the flag, in a copy of the terms used only to
  execute; the cursor digest is still computed over the request as it came. `EXISTS` with the
  flag is rejected. Without `order_by`, at least one term without the flag (§4.1).
- **Syntax in the tools:** operator ending in `?` (`gt?`, `eq?`). The suite turns off shell
  filename expansion (`set -f`) in the block, because `gt?` is a glob pattern.

## Changes

- `include/moobfs_ioctl.h`: `flags` in the term, `MOOB_TERM_MISSING_OK`, `struct moob_query2_batch`,
  `MOOB_QUERY2_BATCH_MAX`, `MOOB_IOC_QUERY2_BATCH`.
- `layer/index.c`: `term_check` with flags allowed per version; "missing or ..." matching;
  `do_query2` split into `query2_target`, `query2_validate` and `query2_exec`;
  `do_query2_batch`.
- `tools/query-common.h`: operator with `?`. `tools/moobfs-index.c`: missing rule.
- `tests/query.c`: `q2batch`, `q2batcherrno`, invalid cases for flags and batch, and v1
  rejecting flags.
- `vm/suite.sh`: `QUERY2_MISSING_BATCH_PASS` block (see `docs/testing.md`).

## Evidence

`make vm-test`: `QUERY2_MISSING_BATCH_PASS`, `QUERY2_LEAPFROG_PASS`, `QUERY2_ORDER_PASS` and
`QUERY2_PASS` on ext4, btrfs and XFS, and all the previous markers up to `MOOBFS_LAYER_PASS`,
on the first run. Shellcheck 0.10.0 (`--severity=warning`) with no findings in the scripts.

## Limitations

- No measurement; the batch saves calls and permission checks, but each request still
  walks its own part of the index.
- Batch under concurrent changes was not tested.

## Next step

Q2d PR after the Q2b and Q2c merges. Phase Q2e (renames across domains and concurrency
test with domains); the `EXDEV` part and the file partition change already went
into Q2a.

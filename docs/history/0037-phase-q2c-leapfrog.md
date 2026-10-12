# 0037 — Phase Q2c: intersection by leapfrog in `QUERY2`

Date: 2026-10-11. Status: implemented and tested in the suite's QEMU VM (Debian kernel 6.12.111,
ext4, btrfs and XFS); KASAN by CI on the pull request; measurement **not executed** (no
measurement host).

## Goal

Phase Q2c of `docs/spec-query-v2.md` (§4.4): queries with two or more equality conditions,
the `namespace AND agent` form of real clients (journal 0033), should not cost the size
of the first term's run.

## Decisions

- **Where it applies:** `QUERY2` without `order_by`, with two or more `EQ` terms. With `order_by`, the
  scan continues in the requested order (§4.4). v1 does not change, so it keeps serving as a baseline.
- **Who drives:** the first `EQ` term in the order of the request. Alternative: choose the term with the
  shortest run; discarded in this phase because it would require counting the runs (or keeping
  per-value counters) and because the leapfrog already limits the work to the shortest run, with
  a logarithmic search per step. It remains a possible optimization, to be measured.
- **Search instead of linear advance:** each term searches its tree for the first inode greater than or
  equal to the target. Simpler and with the same bound on steps; an iterator with local advance
  would be a constant-factor optimization.
- **Cursor:** results come out in inode order within the value of the driving term, so the
  resume key is (driver value, last inode), the same key form as in the previous
  phases; no change to the UAPI.
- **Plan verification in the test:** without a counter exposed in the UAPI, the suite verifies the plan
  by the order of the response (client option `--raw`): with a first `EXISTS` term whose
  values decrease with the inode, the simple scan would respond in decreasing inode order
  and the intersection responds in increasing order. Alternative: a candidate counter in
  `GET_INFO`; discarded so as not to widen the UAPI only for testing.
- **Measurement against Q2a/Q2b:** load on the host the modules of the two versions, alternately,
  and run the same per-domain W3 (`bench-load --query2 1`) with the `namespace AND agent` form.

## Changes

- `layer/index.c`: `add_candidate` (extracted from the scan loop), `seek_eq`, `leap_collect`,
  fields `leap`, `nleap` and `leap_idx` in the query scope, plan selection in `do_query2`.
- `tests/query.c`: `q2 --raw`.
- `vm/suite.sh`: `QUERY2_LEAPFROG_PASS` block (see `docs/testing.md`).

## Evidence

`make vm-test`: `QUERY2_LEAPFROG_PASS`, `QUERY2_ORDER_PASS` and `QUERY2_PASS` on ext4, btrfs and
XFS, and all the previous markers up to `MOOBFS_LAYER_PASS`, on the first run. The
plan verification passed on the three lowers (inodes increasing with file creation).

## Limitations

- No performance measurement.
- The driver is chosen by position in the request, not by the size of the run.

## Next step

Q2c PR after the Q2b merge. Phase Q2d: batch and `MISSING_OK`.

## Note (2026-10-12)

Measured on the host of journal 0042, the leapfrog described above was replaced by a scan
of the shortest run among the `EQ` terms, chosen by a parallel walk; numbers and
justification in journal 0044. The plan verification by the order of the response remains valid
(the revised plan also responds in increasing inode order).

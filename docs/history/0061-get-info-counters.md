# 0061 — `GET_INFO` counters only for administrators (ADR-0011, F1)

Date: 2026-10-12. Status: implemented; `make vm-test` passes; KASAN pending (local series).

## Problem

ADR-0011 finding F1: any process that can open the mount root got from `MOOB_IOC_GET_INFO` the
index totals (`index_used`, `index_entries`, `index_inodes`, `heterogeneous`,
`fallback_domains`), which sum every user's memories. Polling them shows another user's write
rate, a leak through counts that `SECURITY.md` lists as a vulnerability.

## Decision

The counters are filled only for `CAP_SYS_ADMIN` in the initial user namespace and are zero for
everyone else. A new flag, `MOOB_FLAG_COUNTERS` (0x20), marks a reply that carries them, so a
tool can tell "zero" from "hidden". Static fields (UAPI and format versions, limits, lower type,
capabilities, `index_budget`, `domain_depth`) and the state flags (index off, paths stale, index
incomplete, domains) stay public: clients need them to interpret query results. The check uses
`ns_capable_noaudit`, so an ordinary `GET_INFO` does not log a denial.

Alternative: per-caller counters (only the caller's own entries). Rejected for now: it needs an
owner index the layer does not keep, and no client asked for it.

## Changes

- `layer/index.c` (`do_get_info`), `include/moobfs_ioctl.h` (flag and comment).
- `tests/query.c`: `info` prints `counters=shown|hidden`.
- `vm/suite.sh`: in the demo, root sees the counters and uid 1001 gets `counters=hidden` with zero
  counters. Two older checks compared the whole flags word as text (`flags=0` after a remount, and
  `flags=0x2` with `noindex`, which only matched as a prefix); both now test the bits they mean.
- Dated revision notes in `docs/spec-m2.md` and `docs/spec-query-v2.md`; ADR-0011 accepted with F1
  marked fixed; `SECURITY.md` and `docs/README.md` updated.

## Evidence

`make checkpatch` clean. `make vm-test`: the first run failed at `bench-remount-flags-ext4` (the
old whole-word check, now root's reply carries 0x20); after the fix, every marker through
`DEMO_PASS`, `MOOBFS_ACCEPTANCE_PASS` and `MOOBFS_LAYER_PASS`.

## Compatibility

A UAPI behaviour change for unprivileged callers: fields that were filled are now zero. The UAPI is
experimental (no stability promise) and no known client reads these counters; the project's own
tools run them as root.

## Next step

KASAN for the local series on the benchmark host before publishing.

# 0102 — Tracepoints for the index (security roadmap S13)

Date: 2026-10-14. Status: implemented; run in the local VM (results below). Not yet run under
KASAN; the KASAN job of the pull request runs it.

## Goal

S13, accepted on 2026-10-13 to arrive with the second half of ADR-0012: make visible what the
on-demand index does (builds, releases, files read again, the index marked incomplete), and
replace the test-only parameters of journal 0098 where possible.

## Change

- `layer/moobl_trace.h` (new), system `moobfs`, the pattern of `samples/trace_events` in Linux
  6.12.112 (`CFLAGS_index.o := -I$(src)` in `layer/Makefile`, `CREATE_TRACE_POINTS` in
  `layer/index.c` only). Four events:
  - `moobfs_domain_build`: device, domain, result, attempts, domains released to make room,
    bytes the index grew by, nanoseconds; one per build, success or failure;
  - `moobfs_domain_release`: device, domain, `by=budget` or `by=shrinker`, bytes freed;
  - `moobfs_scan_reread`: device and inode of a file read again because a write touched it
    during the reading;
  - `moobfs_index_incomplete`: device and error, the first time an update fails after the
    lower change (`index_mark_incomplete()` now gathers the three places that set the flag).
- The module parameter `scan_retries` is removed; `moobfs_scan_reread` replaces it.
  `scan_delay_us` stays: it changes timing, which no tracepoint can do.
- The events carry inode numbers and sizes, which ADR-0011 (finding F1) keeps from other
  users. tracefs is mounted with mode 0700 by default, so only root reads them. Not a stable
  interface.

## Test

- `buildrace` counts the readings repeated from `moobfs_scan_reread` instead of the parameter.
- `release`: after the four queries under the budget, four `moobfs_domain_build` events with
  `err=0` and at least two `moobfs_domain_release` with `by=budget`; after `drop_caches`, four
  with `by=shrinker`; the tracefs root has mode 700.

## Results

Local VM, `MOOB_ONLY="release buildrace" make vm-test`: `BUILD_RACE_PASS` on ext4, btrfs and XFS
(readings repeated, as seen by the tracepoint: 62, 23, 37) and `RELEASE_PASS` on the three.

Negative runs: with `trace_moobfs_scan_reread()` removed, `buildrace` fails at
`br-no-write-landed-in-the-window-ext4`; with the reason of a release inverted, `release` fails
at `rl-trace-budget-releases-ext4`.

`sh scripts/checkpatch.sh`: exit 0; shellcheck at the CI's severity: clean.

## Limitations

- No dynamic debug (`pr_debug`) was added; no message was found that a tracepoint does not
  already give.
- The tracepoints were read with the tracefs text interface only, not with perf or BPF.
- Root-only access is checked by the mode of the tracefs root, not by reading as another user.

## Next step

Measurements of ADR-0012 on the benchmark host with the second half in place (steady-state
latency, rebuild cost after a release), then the publication of the branch as a pull request.

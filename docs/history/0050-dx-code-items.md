# 0050 — Pending code items received from the developer experience batch

Date: 2026-10-12. Status: implemented on the branches of the Q2 series; checkpatch at zero and mandatory.

## Context

When closing the developer experience plan (`docs/dx-plan.md`, journals 0039 to 0046),
the other session handed over four pending items that touch code or the Q2 harness. They were fitted
into the branches that already touch the same files, so as not to open a pull request competing with the
Q2 queue (each update of main forces a new KASAN run).

## Changes

1. **`qsort(NULL, 0, ...)` in the oracle** (`tools/moobfs-index.c`, Q2d branch): only sorts when
   there are results. Reproduced beforehand with `make bench-smoke` under ASan and UBSan with
   `-fno-sanitize-recover=all`.
2. **False pass in `bench-smoke`** (found while reproducing item 1): `bench/equiv.sh`
   piped the oracle into `cut` and `sort`, and the status checked was that of `sort`; with the oracle
   aborted by the sanitizer, the empty response coincided with the expected one and the test printed
   `BENCH_SMOKE_PASS`. The oracle's status is now checked separately. Confirmed: with the
   old oracle, `bench-smoke` under sanitizers fails (code 2); with the fixed one, it passes with
   no report.
3. **`bench-smoke` under ASan and UBSan in CI** (job "Tools and sanitizers", Q2d branch): compiles
   with `-fsanitize=address,undefined -fno-sanitize-recover=all` and fails on the status and on
   any sanitizer report in the output.
4. **`kasan.yml`**: the two negations (`! grep ...`) that were not the last command of a
   `bash -e` step, and therefore did not fail it (SC2251), became `if grep ...; then exit 1; fi`.
5. **checkpatch** (branch `m3/query-v2-recent`, where `layer/index.c` is in its final form): there were
   30 warnings on that branch (14 on main plus those of the Q2 code). 28 fixed: blank lines
   in `kernel/main.c`; octal permissions in `reach_ok`; block comments in kernel
   style; the mount scan and the copy of a result extracted into functions
   (`scan_child`, `scan_push_dir`, `scan_file`, `put_result`) to remove the deep indentation.
   No behavior change: `make vm-test` passed (`MOOBFS_VM_PASS`, `QUERY2_*` blocks,
   `DEMO_PASS`, `MOOBFS_LAYER_PASS`).

## Decision: `EOPNOTSUPP` instead of `ENOSYS`

With `noindex`, v1 and v2 returned `ENOSYS`; for checkpatch, `ENOSYS` only means "nonexistent system
call". The maintainer chose to switch to `EOPNOTSUPP` (alternative: keep it and
exclude only that rule in `scripts/checkpatch.sh`). Compatibility decision: the UAPI is
declared experimental and no test depended on the old code, but a client that tested for
`ENOSYS` needs to test for `EOPNOTSUPP` instead. With domains off, v2 already returned
`EOPNOTSUPP`; to distinguish the two cases, the client reads `GET_INFO` (`MOOB_FLAG_INDEX_OFF`,
`MOOB_FLAG_DOMAINS`). The suite now checks the code: v1 with `noindex` fails with
"Operation not supported" and v2 returns 95 (`EOPNOTSUPP`).

With this, checkpatch is at 0 errors and 0 warnings on that branch; `scripts/checkpatch.sh` exits with
1 on any warning (checked in both directions), and the step no longer has `continue-on-error`
in the `Lint` job (item S5 of `docs/security-roadmap.md`). The old specifications receive a dated
note instead of a rewrite.

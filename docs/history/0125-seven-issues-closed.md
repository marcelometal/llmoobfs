# 0125 — Seven open issues closed: fuzz seeds, compiler matrix, pins, tool diagnostics, man pages, accessibility, SPDX

Date: 2026-10-18. Started 11:20, ended 12:30 (calendar time, local). Status: written, checked
locally where the host allows; CI of pull request 81 pending. Estimated effort: M.

## Decision

The maintainer decided not to hold issues back for collaborators who may not come: every open
issue that is solidity, test infrastructure, documentation or polish is closed by the project now;
only feature issues wait for the end of stabilization (#7, JSON output, stays open). Two issues
wait for the stabilization batches to merge, since they touch `vm/suite.sh`: #5 and #72.

## Changes and local evidence

- **#9** `tests/fuzz-corpus/gen-seeds.py` writes 59 seeds named by type and boundary (empty,
  maximum and maximum plus one, invalid UTF-8 forms, NaN and infinities, bool outside 0/1, raw of
  4096 and 4097 bytes, invalid names, format markers of 9 and 10 digits). Every seed ran once
  through `tests/fuzz_attr.c` under ASan and UBSan on macOS: `ran 64 seeds`, no report. Not done:
  minimizing with libFuzzer's `-merge=1` (no libFuzzer on the macOS toolchain); the CI fuzz step
  runs with the new corpus.
- **#13** CI job "Compiler warnings" over gcc 12 and 14, clang 16 and 18: `make tools attr-tools
  bench-tools` with `-Werror`.
- **#14** `scripts/check-pins.sh`: run locally, `PINS_OK`, and kernel.org's `sha256sums.asc` agrees
  with the pinned SHA-256 of linux-6.12.112. In the Lint job; how to update pins together in
  `docs/development.md`.
- **#6** `moobfs-attr` and `moobfs-check`: `--help`, one error style, exit status in
  `docs/tools.md`. The tools are Linux-only; checked on macOS only for errors other than the xattr
  API signatures.
- **#8** four mdoc pages in `man/`; `mandoc -Tlint -Wwarning man/*` clean; `make man-lint` in CI. A
  claim of the first draft was corrected against `common/attr.c` before committing: names allow
  `_` and `.`, not `-`.
- **#12** `docs/accessibility.md`; contrast computed from the OKLCH tokens, every text pair AA or
  better; `scripts/check-site.py` enforces the page structure: `Site links valid: 12 HTML pages;
  lang, one h1, landmarks, skip link, alt text checked`.
- **#15** `scripts/check-spdx.sh` covers every C, shell and Python source; six headers added;
  `SPDX_OK`.
- Also: the arm64 job's KVM step no longer fails on runners without `/dev/kvm` (journal 0124).

## Limitations

- No screen reader or browser accessibility tool was run.
- The compiler matrix and the man-page lint run for the first time on the pull request.

## Next step

Merge after CI; then #5 and #72 on top of the stabilization batches.

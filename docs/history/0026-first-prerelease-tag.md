# 0026 — First pre-release tag: v0.1.0-experimental

Date: 2026-10-10. Branch: `release/v0.1.0-experimental` on top of `main` (commit `455f1c2`).

## Decision

Create the annotated tag `v0.1.0-experimental` and the corresponding GitHub release marked as a
pre-release, without binaries, without packaging and without support, as decided in the SEO plan
(journal 0024, maintainer's decisions). The tag is a snapshot of the source with M1, M2 (phases
2a to 2e), M3 phase 3b and the M4 harness, plus the community and SEO files.

## Changes

- `CHANGELOG.md`: the "Unreleased" section is renamed `v0.1.0-experimental — 2026-10-10`,
  with an opening paragraph; the entries do not change.
- Texts that said "no release" (README in English and Portuguese, FAQ in `docs/` and on the
  site, `when-not-to-use.md`, `SECURITY.md`, `CONTRIBUTING.md`, `site/index.html`) now say
  "source pre-release tag only", keeping the experimental character and the "do not use in
  production".

## Evidence

The checks of this branch's PR (tools, Lint, VM suite) are the validation; the tag is created on
the merge commit in `main`, with the release notes derived from `CHANGELOG.md`.

## Next step

M4 measurements on accelerated hardware (`docs/bench-accelerated-guest.md`); no new tag before
the M4 report.

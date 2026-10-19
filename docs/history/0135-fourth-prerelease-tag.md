# 0135 — Fourth pre-release tag: v0.4.0-experimental

Date: 2026-10-19. Branch: `release/v0.4.0-experimental` on top of `main` (`7292cee`).

## Decision

Tag `v0.4.0-experimental` and publish a GitHub pre-release, without binaries, packaging or
support, as the maintainer approved on 2026-10-19. `v0.3.0-experimental` predates the security
fixes F9 to F12 (journals 0116 and 0117) and the fixes of F2 (ADR-0016, journal 0130) and F4
(journal 0132); nobody should be pointed at a tag with known security defects (the rule of
journal 0087).

A new minor version, not a patch: the index is built on demand and released (ADR-0012), and
`GET_INFO` gained three counters and a flag in reserved space (the structure keeps its size).

## Changes

- `CHANGELOG.md`: the "Unreleased" entries become the `v0.4.0-experimental — 2026-10-19` section,
  with an opening summary; the entries do not change. An empty "Unreleased" heading stays on top.
- `layer/main.c`: `MODULE_VERSION("0.4.0")`.
- `CITATION.cff`: version and release date.
- `README.md`, `README.pt-BR.md`, `SECURITY.md`, `CONTRIBUTING.md`, `docs/compatibility.md`,
  `docs/faq.md` (and `site/faq.html` from it), `site/index.html`: name the new tag; tags up to
  `v0.3.0-experimental` are said to carry defects fixed since.
- `docs/retired-claims.txt`: the wordings that said the last tag predates security fixes.

## Evidence

The checks of this branch's pull request are the validation, the KASAN job included (the branch
touches `layer/main.c`). The tag is created on the merge commit in `main`; the release notes are
the opening summary of the changelog section. Since `v0.3.0-experimental`: three six-hour
syzkaller runs (the third on the layer code before F2, journal 0133; a fourth, with F2 and F4,
running on the benchmark host), KCSAN in CI (journal 0127), negative rounds (journal 0131).

## Limitations

A source snapshot of a research project: one kernel series (6.12), x86-64, tested in a VM; no
outside human review. Open: fair shares of the index budget (S9). Issue #95 (a lower directory
already casefolded is not refused) was opened the same day and is not fixed in this tag; it is
not reproduced yet.

## Next step

Record the fourth syzkaller run; fix issue #95; rerun the steady-state comparison under the
method of journal 0134.

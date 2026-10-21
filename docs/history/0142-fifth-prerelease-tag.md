# 0142 — Fifth pre-release tag: v0.4.1-experimental

Date: 2026-10-20. Branch: `release/v0.4.1-experimental` on top of `main`.

## Decision

Tag `v0.4.1-experimental` and publish a GitHub pre-release, as the maintainer approved on
2026-10-19. `v0.4.0-experimental` (journal 0135) was tagged on 7292cee, which carries the query
slowdown found and bisected in journal 0139: every `QUERY`/`QUERY2` allocated a buffer for the
requested limit, about 1.1 MB at the maximum, so filtered queries ran 1.3 to 2.5 times slower
than before pull request 71. Nobody should be pointed at a tag with a known defect (the rule of
journal 0087). A patch version: the UAPI and the on-disk conventions do not change.

## What it carries since v0.4.0

- The fix of journal 0139 (pull request 101): the result buffer is kept with the open file.
- The casefold refusal (journal 0136, pull request 97), merged after the v0.4.0 tag.
- Tests: the interoperability test with common xattr tools (journal 0140, pull request 99; first
  CI run 39352397079 passed) and the typed-attribute limits and refusals (journal 0141, pull
  request 103).
- arm64 under emulation (journal 0137, pull request 98): the ext4, btrfs, XFS and fault-injection
  steps passed under KASAN (runs 39265856488, 39271760023, 39300702939, 39343759274).
- syzkaller: the fourth six-hour run (journal 0138) and the descriptions for the weak handlers;
  the fifth run's result, if it ended before the tag, in its own journal.

## Changes

- `CHANGELOG.md`: the "Unreleased" entries become the `v0.4.1-experimental` section with an
  opening summary; an empty "Unreleased" heading stays on top.
- `layer/main.c`: `MODULE_VERSION("0.4.1")`; `CITATION.cff`.
- `README.md`, `README.pt-BR.md`, `SECURITY.md` (a row for each of the two tags),
  `CONTRIBUTING.md`, `docs/compatibility.md`, `docs/faq.md` (and `site/faq.html`),
  `site/index.html`: name the new tag; tags up to v0.4.0 are said to carry defects fixed since.
- `docs/retired-claims.txt`: the wordings that named v0.4.0 as the last tag.

## Evidence

The pull request's checks, KASAN included (the branch touches `layer/main.c`). The tag goes on
the merge commit; the release notes are the opening summary of the changelog section.

## Limitations

Same as journal 0135: one kernel series (6.12), x86-64 tested in a VM, arm64 only under emulation
in an exploratory job; no outside human review. Open: fair shares of the index budget (S9).

*Correction, 2026-10-20 (journal 0143).* The descriptions for the weak handlers were not in this
tag: branch `syz-coverage` was merged after it, with the fifth run.

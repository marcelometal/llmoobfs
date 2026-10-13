# 0086 — The last two Portuguese documents are in English

Date: 2026-10-13. Status: done locally; pull requests 46 and 47 are still open for the
maintainer to close. Local series (after pull request 60).

## What

`docs/security-roadmap.md` and `docs/benchmark-harness.md` were the two documents left in
Portuguese after ADR-0010. An outside contributor had opened pull requests 46 and 47 with English
translations of both, made from commit `403ebdb`. They could not be merged as they were: they
conflict with `main` in `docs/README.md`, and they add `name.en.md` beside the original, which
ADR-0010 rejects (one copy, translated in place).

## Decision

Use the contributor's text in place of each original, with credit in the commit
(`Co-authored-by`) and in `docs/README.md`.

- `benchmark-harness.md`: the original had not changed since `403ebdb`; the translation replaces
  it as it is, without its first line ("the Portuguese original prevails").
- `security-roadmap.md`: the only changes to the original since `403ebdb` were dated status
  notes. They were carried over under S2, S4, S5 and S6, brought up to today, and one was added
  under S1.

Three other documents still said that some document was in Portuguese
(`architecture-overview.md`, `when-not-to-use.md`, `bench/results/README.md`); corrected.

## Evidence and limits

- No accented Portuguese text remains in the two files (`grep` for accented letters: none).
- The translations were read in part, not compared line by line with the originals. The
  section count of the roadmap matches (S1 to S11).

## For the maintainer

Close pull requests 46 and 47 with thanks and a pointer to this commit, and close issues 38 and
39. The roadmap's "current state" table is dated 2026-10-10 and is older than the status notes
under each item; rewriting it is left for when S2 closes.

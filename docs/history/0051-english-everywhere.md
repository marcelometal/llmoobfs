# 0051 — The repository moves to English (ADR-0010)

Date: 2026-10-12. Status: translation done on branch `docs/english-everywhere`; pull request to be
opened after the QUERY2 code pull request (49) lands.

## Goal

The maintainer decided that everything in the repository is in English, existing Portuguese text
included ("essa é a regra e o que tem que ser feito" ("that is the rule and what has to be done")),
after a proposal to keep old journals in Portuguese with an English index. Recorded in ADR-0010 and
in the language policy of `CONTRIBUTING.md`.

## Decisions

- Translate in place: same file names, so links keep working; no Portuguese copies; the originals
  stay in the git history. `README.pt-BR.md` stays as a courtesy mirror.
- Translations by the outside contributor are used instead of new ones: `docs/ai-workflow.md` now
  holds the reviewed translation from PR 43 (the separate `ai-workflow.en.md` is removed; the
  Portuguese original had not changed since the translated commit). `docs/security-roadmap.md` and
  `docs/benchmark-harness.md` stay Portuguese here and take the translations of PRs 46 and 47 once
  their review fixes are in.
- Thousands are grouped with a space ("60 000"), as in the journals and benchmark results.
- Portuguese placeholders inside inline code are prose and were translated (`<nome>` -> `<name>`);
  real identifiers were not. Where the text quotes a script's syntax, the script was checked first
  (`add FILE...`, `race <seconds>` were already English in the code).
- Direct quotes of the maintainer stay in Portuguese with the English in parentheses.

## Method

83 files (about 10 500 lines) were found by a word-frequency classifier; one more,
`docs/seo-geo-plan.md`, was half English and was found later by a stricter scan. Six parallel
batches plus one file, each with the same brief and glossary (`lower filesystem`, `journal`,
`run`, `fallback`, `Done when`, and so on). Every translated file was compared with its original
by a script: numbers (decimal comma normalized), links, inline code, headings, table rows, list
items, block quotes and code fences, plus a scan for leftover Portuguese. Every flag was resolved by
hand: most were inline-code spans regrouped by translated prose, thousands written in the English
way, or ordinals ("65.º" -> "65th").

## Corrections made during review

- Two gendered pronouns that the Portuguese did not carry ("she", "him") became neutral wording.
- Quoted section titles in `docs/seo-geo-plan.md` aligned with the translated headings of
  `docs/value-assessment.md`.
- `docs/research-originality.md` §9 held the same positioning text in Portuguese and in English;
  only the English one is kept.
- A quote of `docs/benchmark-plan.md` now uses its English text; "leapfrog (leapfrog/galloping)"
  became "leapfrog (galloping)".
- `docs/history/README.md`, `docs/README.md` and the README tables now list every document as
  English, except the two awaiting the contributor's pull requests.

## Limitations

- The translation is faithful by method, not proofread line by line by a native speaker.
- Section names quoted across documents were aligned where found; others may remain.
- Issues 35, 36 and 37 (translations offered to newcomers) are covered by this branch and will be
  closed by its pull request with a note.

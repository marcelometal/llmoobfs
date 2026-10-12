# ADR-0010 — English is the language of the repository

Date: 2026-10-12. Status: accepted. Supersedes the "Language policy" section of `CONTRIBUTING.md`
as written in ADR-0004 and the contributor-experience batch (journal 0039).

## Context

Until now, documents under `docs/` could be written in Portuguese or English, journals stayed in
the author's language, ADRs and specifications were translated only on outside demand, and the
Portuguese original prevailed. In practice most design material (23 documents, 8 ADRs and 50
journals, about 10 500 lines) was Portuguese, while the project is aimed at kernel and agent
developers who mostly read English. Outside contributors started translating documents one by
one (PR 43, PRs 46 and 47).

## Decision

The maintainer decided that everything in the repository is in English: code, comments, commit
messages, issues, pull requests, documentation, specifications, ADRs, journals in
`docs/history/`, and the agent instructions in `AGENTS.md` and `CLAUDE.md`.

- Existing Portuguese files are translated in place: same file name, English content, so links
  keep working. The Portuguese text remains in the git history; no Portuguese copy is kept,
  except `README.pt-BR.md`, which stays as a courtesy mirror of the English README.
- Translations must be faithful: no claim, status, number, qualifier or decision is changed.
  Historical journals keep their dates, their order and their dated correction notes; translation
  does not rewrite what was decided or measured.
- New documents and journals are written in English from the start.
- Translations already offered by outside contributors (open PRs) are used instead of new ones.

## Alternatives considered

- Keep historical journals in Portuguese with an English index. Rejected by the maintainer: the
  rule is English everywhere.
- Keep each Portuguese original next to its translation (`name.pt-BR.md`). Rejected: two copies
  drift; git history already preserves the original.

## Consequences

- One documentation pull request translates the existing material; it lands after the QUERY2
  code pull requests, because every merge into `main` restarts the KASAN run of open code pull
  requests (branch protection requires up-to-date branches).
- Conversation between the maintainer and an assistant may be in any language; what is committed
  is English.

# 0169 — Confirmed TeleAI discovery link in the READMEs

Date: 2026-10-20. Status: complete locally; awaiting maintainer review before push.

Calendar window: approximately 22:35–22:39 America/Sao_Paulo on 2026-10-20.
Elapsed calendar time, not measured labor effort.

## Goal and decisions

Add the confirmed TeleAI Awesome Agent Memory discovery link to the external
records table in both READMEs. Use a dedicated worktree and branch based on public
`main` at `d797a68`; leave the result for maintainer review before any push.

The public list at commit `749a08cfe25ad23dda05565d1a11b444608b7786`, identical to
its `main` when checked, includes MoobFS in the collapsed **Emerging projects**
section. Its entry describes experimental agent-memory storage with typed
attributes, permission-scoped queries, AI assistance and disposable-VM evaluation.
The new README row retains the experimental qualification and links to the list.
Do not encode a position number, star count or mutable maturity threshold.

Preserve all five existing badges, the Zenodo citation and the other external
records. Add a plain link rather than a badge: list inclusion is discovery, not
an independent review, integration, institutional sponsorship or certification.
No architecture decision changes; ADR-0002 and ADR-0010 cover this documentation
record and its faithful Portuguese mirror.

## Changes and public surfaces

Update `README.md`, `README.pt-BR.md`, `docs/references.md`, `CHANGELOG.md` and the
history index, with this journal. Sources are public; correspondence, submission
receipts and contact history stay outside Git.

The public-surfaces inventory was checked. Site and FAQ have no external-records
table to synchronize. Technical status, test evidence, security, compatibility,
roadmaps and citation metadata do not change. No code, tests or workflows change.

## Validation and limits

Executed in the isolated worktree:

- `git diff --check`: clean.
- `sh scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`.
- `python3 scripts/check-site.py .`: all 12 HTML pages passed the existing
  link and accessibility checks.
- `python3 scripts/site-faq.py --check`: the generated FAQ matches its source.
- One-off comparison against `d797a68`: removing the single new table row from
  each README reproduces the original byte for byte; all five badge lines and
  `CITATION.cff` are unchanged. Both language versions link to the same list and
  retain the experimental qualification.
- Public `main` reconfirmed at `d797a68` after the checks. The journal number was
  checked against the other local worktrees; the highest existing number was 0168.

No new kernel/VM test, benchmark or audit was run for this documentation change.
This listing does not change the project's experimental or review status.

## Next step

Present the signed local commit for maintainer review. Recheck public `main`
before any later authorized push. No push, pull
request, merge or message is authorized by this preparation.

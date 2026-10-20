# 0168 — Zenodo snapshot DOI badge and citation in the READMEs

Date: 2026-10-20. Status: complete locally; awaiting maintainer review before push.

Calendar window: 20:20–20:21 America/Sao_Paulo on 2026-10-20.
Elapsed calendar time, not a measured labor estimate.

## Goal and scope

Make the published experimental source snapshot easy to identify and cite from
both READMEs. Work in a dedicated worktree based on public `main` at `2993df2`.
No module, UAPI, license, supported-kernel or release change.

## Decisions and changes

- Preserve the four existing badges. Add the official Zenodo DOI badge pointing
  to `10.5281/zenodo.23514721`, the version-specific source archive.
- Add the citation displayed by the published record: Studio Moob, 2026, the
  snapshot title, version `0.4.1-experimental+git.02381fba`, software type, Zenodo
  and the DOI. Keep the original citation title and metadata in the Portuguese
  mirror; translate the surrounding explanation.
- Identify the archived commit, `02381fba`, and distinguish it from the earlier
  `v0.4.1-experimental` tag and the evolving `main`. Keep `CITATION.cff` unchanged:
  it describes the tag, whereas this DOI describes a later source snapshot.
- Use the version DOI so citations identify reproducible sources. The separate
  all-versions DOI, `10.5281/zenodo.23514719`, is not the citation target here.
- Update references, changelog and history index. No new ADR is needed; ADR-0002
  and ADR-0010 cover the history and faithful language mirror.

The public record at <https://zenodo.org/records/23514721> was inspected for its
published version, citation and badge image source. Deposit preparation material,
receipts and private correspondence remain outside Git.

## Validation

Executed in the isolated worktree:

- `git diff --check`: clean.
- `sh scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`.
- `python3 scripts/check-site.py .`: all 12 HTML pages passed existing link and
  accessibility checks.
- `python3 scripts/site-faq.py --check`: generated FAQ matches its source.
- One-off comparison against `2993df2`: each README retains its four original
  badge lines and every byte outside the added badge and citation section;
  `CITATION.cff` is byte-identical. Both language versions cite the same archive
  version, commit and version-specific DOI.
- The published page displays the expected citation and the official DOI badge
  image is loaded successfully (193 by 20 pixels).

No new kernel/VM tests, benchmark or audit. The DOI is a source citation, not a
technical certification or a new release.

## Public surfaces and next step

Both README languages, references, changelog and history index are updated in this
unit. The three existing external-record entries retain their scope and wording.
The site and FAQ have no DOI citation to replace; security, compatibility, testing
and roadmap documents describe unchanged technical behavior. The existing
`CITATION.cff` is retained for the distinct tag metadata explained above. No
Zenodo record or archived file is modified.

Next: maintainer review of the signed local commit. Recheck `main` before later
publication. No push, pull request or merge is authorized by this preparation.

## 2026-10-20 — Publication follow-up

The maintainer authorized pushing this branch and opening a pull request, while
reserving merge for a later explicit approval. This replaces the preparation-only
publication restriction above; the README content and citation scope are unchanged.

The remote `main` was reconfirmed at `2993df2`, the preparation base. The isolated
branch was clean, only the six expected documentation files differed, whitespace
checks passed, `CITATION.cff` was unchanged and no pull request used this head
branch. No other session's checkout or uncommitted work was incorporated. The
documentation checks above remain valid for the unchanged badge and citation.
Next: push this branch and open the pull request; no merge is authorized.

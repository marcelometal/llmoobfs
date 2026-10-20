# 0167 — Verified external project records in the READMEs

Date: 2026-10-20. Status: complete locally; awaiting maintainer review before publication.

Calendar window: 17:39–17:41 America/Sao_Paulo on 2026-10-20.
This records elapsed calendar time, not a measured labor estimate.

## Goal and scope

Keep the existing badges and add a small, accurate set of confirmed public project
records to the README and its Portuguese mirror. Prepare the documentation in an
isolated worktree based on public `main` at `02381fba`; publication awaits review.
No module, interface, license, release or supported-kernel change.

## Decisions and changes

- Keep the four existing badge lines byte for byte: CI, GPL-2.0-only, OpenSSF
  Scorecard and OpenSSF Best Practices. Add ordinary text links near the end of
  both READMEs, before the citation section, to keep the opening compact.
- Link only the verified Anitya, Open Hub and XERJ project pages. State what each
  record establishes and its current limit: experimental pre-release tracking
  without a distribution package mapping, a public entry with code analysis
  incomplete, and a selected source snapshot with license review pending and no
  installable corpus yet, respectively.
- These are metadata and discovery records. They do not change the technical
  status or imply an independent kernel review. Keep the existing experimental
  notice and link back to the status section in each language.
- Preserve existing citation metadata. Add no unconfirmed persistent identifier,
  new badge, institutional endorsement or integration claim.
- Update `docs/references.md`, this index and `CHANGELOG.md`. No new architectural
  decision is needed; ADR-0002 and ADR-0010 govern the journal and language mirror.

Private preparation material, service-check evidence and submission tracking stay
outside Git. This journal records the documentation change, not an outreach log.

## Validation

Executed in the isolated worktree:

- `git diff --check`: clean.
- `sh scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`.
- `python3 scripts/check-site.py .`: 12 HTML pages passed the link and accessibility checks.
- `python3 scripts/site-faq.py --check`: the generated FAQ matches its source.
- One-off comparison against `02381fba`: each README retains exactly the original
  four badge lines and all previous bytes outside the inserted section. The two
  sections link to the same three verified URLs and their own status anchors.

Service pages were checked in the browser; an HTTP response alone was not treated
as proof of registration. Those checks are dated observations, not a promise that
external metadata will stay unchanged. No kernel/VM tests, benchmark or audit
executed for this documentation-only change.

## Public surfaces and next step

Both README languages are updated together. The references, changelog and history
index describe the same change. The site, FAQ, security policy, roadmap, testing
guide and compatibility documents do not state these external records, and no
technical status changed, so they need no text change. No external page is edited.

Next: maintainer review of the signed local commit. Recheck the external records
and current `main` before any later publication. No push, pull request or merge
is authorized in this unit.

## 2026-10-20 — Publication follow-up

The maintainer authorized pushing this branch and opening a pull request, with
merge reserved for a later explicit approval. This supersedes the preparation-only
publication restriction above; it does not change the content or scope of the
README records.

Before publication, the remote `main` was still `02381fba`, the preparation base.
The isolated branch was clean, its six changed files were documentation only,
`git diff --check 02381fba..HEAD` passed and no existing pull request used this
head branch. The documented checks above remain the validation for the unchanged
README content; no new kernel tests were run. Other sessions' checkouts and work
were not incorporated. Next: push the branch and open the pull request; no merge.

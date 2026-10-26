# 0205 — Security operations from the OSTIF best-practices guide (S22)

Date: 2026-10-26. Status: documented on branch `claudiofp/SECURITY-HARDENING`; not merged.
Work started about 20:05 UTC and ended about 20:40 UTC on 2026-10-26. This is calendar time,
not a measured effort; the estimate in roadmap item S22 is S.

## Goal and decision

An inquiry to OSTIF about scoping a review was answered: the project is outside the
widely used infrastructure they can help now. They pointed to their security best-practices
guide. This batch checks the guide (chapters 2 to 7) against the repository and fills the gaps
that belong to the project.

Already in place, so not changed: private vulnerability reporting, `SECURITY.md` with response
targets, coordinated disclosure, CVE and no-bounty terms, the threat model (ADR-0011), CodeQL,
Scorecard, Dependabot version updates, secret scanning with push protection, sanitizers and
fuzzing.

Added:

- **Safe harbor** in `SECURITY.md`, adapted from the disclose.io safe-harbor terms (CC0). It
  covers the MoobFS code only, not GitHub, the site's hosting or Studio Moob's other systems.
- **`site/.well-known/security.txt`** (RFC 9116) with the two contacts, the policy link and an
  `Expires` six months ahead. `pages.yml` sets `include-hidden-files: true`, because the pinned
  `upload-pages-artifact` otherwise drops dot-directories. `scripts/check-site.py`, already run
  in CI and before each Pages deploy, now fails on a file without `Contact` or with a missing or
  passed `Expires`.
- **`docs/security-operations.md`**: access, incident procedure, advisory sources,
  third-party components and SBOM export, and the milestones that call for more security work.
  Linked from `SECURITY.md` and `GOVERNANCE.md`.
- Roadmap item S22, the `SECURITY.md` row of `docs/public-surfaces.md` and section 13 of
  `docs/references.md`.

Discarded: publishing `security.txt` at `studiomoob.github.io/.well-known/`, the location RFC
9116 names. That root belongs to the hosting organization, which also has private, unrelated
projects; a file there would speak for all of them. The maintainer decided to change nothing at
organization level. The file therefore sits under the project path, where scanners that look only
at the domain root will not find it, and `SECURITY.md` stays authoritative. For the same reason
the organization-wide two-factor requirement was not switched on (every member had two-factor
authentication on 2026-10-26, by the organization's `2fa_disabled` member filter, which returned
no one).

## Evidence

Read from the GitHub API on 2026-10-26 (`collaborators`, `collaborators?affiliation=direct`,
`orgs/.../members?role=admin`, `actions/secrets`, `keys`, `environments`,
`branches/main/protection`, `private-vulnerability-reporting`, `security_and_analysis`,
`vulnerability-alerts`, `dependency-graph/sbom`): the three repository admins are the three
organization owners, with no direct collaborators; no Actions secrets; no deploy keys; one
environment, `github-pages`; the three required checks; administrators not enforced; signatures
not required; private reporting enabled; secret scanning and push protection enabled; Dependabot
alerts enabled and security updates disabled; the SBOM export listed 19 packages (actions and
the Python packages of `vm/kasan-requirements.txt`).

Local checks, all on 2026-10-26: `python3 scripts/check-site.py` printed 12 valid HTML pages and
`security.txt valid until 2027-04-26`; `sh scripts/check-retired-claims.sh` printed
`RETIRED_CLAIMS_OK`; `sh scripts/check-pins.sh` printed `PINS_OK`; `sh scripts/check-spdx.sh` and
`git diff --cached --check` exited 0; `rhysd/actionlint:1.7.7` accepted `pages.yml`. The archive
step of the pinned action was reproduced with GNU tar in the pinned `debian:trixie-slim` image:
with its default exclusion `--exclude=.[^/]*` no `.well-known` entry is archived; without it,
`./.well-known/security.txt` is. The Pages deploy itself runs only after a merge to `main` and has
not run.

## Limits and next step

- Nothing was run in a VM; no kernel or UAPI code changed.
- The subscriptions to linux-cve-announce and the Debian tracker are the maintainer's action;
  the document lists the sources and does not claim they are watched.
- Branch numbering: journal 0204 and references section 12 are taken by the C5 branch
  (`claudiofp/C5-LIBRARY`), not yet merged; this batch uses 0205 and section 13.
- `security.txt` expires on 2027-04-26; CI fails after that date until it is renewed.
- After review the maintainer approved Dependabot security-update pull requests for this
  repository only; enabled the same day (`PUT repos/studiomoob/llmoobfs/automated-security-fixes`,
  read back as `enabled: true`). Deferred: a workflow attaching the SBOM to releases (the tags are
  source snapshots; the on-demand export suffices until a real release) and enforcing protection
  for administrators (with one maintainer it would block repository maintenance; revisit with a
  second maintainer).
- Next: the pull request for this batch, approved by the maintainer.

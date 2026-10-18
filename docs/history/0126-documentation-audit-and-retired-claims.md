# 0126 — Audit of every current-state document; retired claims checked in CI; outreach per published unit

Date: 2026-10-18. Started 12:30, ended 14:10 (calendar time, local). Status: audit done, fixes
committed, two checks added to CI. Estimated effort: M.

## Why

The maintainer read in the README "one kernel series (6.12), tested in a VM only; no outside
review yet. The current tag is v0.3.0-experimental" and asked whether it was true, and for a
complete audit: what is history stays, what states the current state must be current. The rule
of journal 0122 existed, but nothing enforced it, and the README, the site and the FAQ had fallen
behind several merges.

## What was found stale (16 current-state files)

- The last tag was recommended (`v0.3.0-experimental`, "the one to use") although it predates the
  fixes of findings F6 to F12: README (both languages), SECURITY, CONTRIBUTING, FAQ, site.
- "No syzkaller", "xfstests has not been run", "no threat-model document", "SELinux has never been
  loaded", "not yet measured" (Q2a to Q2d were measured in journals 0043, 0044, 0047, 0048), "no
  outside review" without saying that two source reviews by AI assistants took place.
- "The index is rebuilt at mount" after ADR-0012 made it per domain on first query.
- The README's implemented table stopped at phase Q2d; the FAQ described only the first `QUERY`
  on the mount root and said the only tag was `v0.1.0-experimental`; dates "as of 2026-10-10" and
  "2026-10-13".

History was left as written: journals, ADRs (apart from status lines), dated research and the
two dated analyses (`docs/value-assessment.md`, `docs/adoption-risks.md`), which got a note
pointing to the current status.

## Changes

- README and README.pt-BR (status, limitations, implemented table, measured phases); SECURITY
  (supported versions: `main`; findings F1 to F12); CONTRIBUTING; FAQ rewritten in ten answers;
  site landing page (status, facts, roadmap), guide (limits, KASAN cadence); help-wanted,
  when-not-to-use, architecture overview, security roadmap (S1 status), ROADMAP, testing.
- `scripts/site-faq.py`: `site/faq.html` is now generated from `docs/faq.md`; `--check` in CI.
- `docs/retired-claims.txt` and `scripts/check-retired-claims.sh`: 20 phrases that were true once;
  CI fails when one appears in a current-state document. Its first run found four more stale
  passages the manual audit had missed (a list on the landing page, a marker table in
  `docs/testing.md` and in the guide, a sentence of SECURITY).
- AGENTS.md: keeping surfaces in step includes closing what is done or obsolete and retiring the
  old wording of a changed fact; every published unit gets its outreach (English and Portuguese)
  in the same round. `docs/public-surfaces.md` names the two checks.

## Evidence

`sh scripts/check-retired-claims.sh`: `RETIRED_CLAIMS_OK`; `python3 scripts/site-faq.py --check`:
matches; `python3 scripts/check-site.py`: 12 pages valid.

## Limitations

- The retired list catches only wordings someone added to it; a new stale sentence in new words
  still needs the walk of the inventory.
- A new tag (`v0.4.0-experimental`), approved by the maintainer on 2026-10-18, is to be made after
  this merges; until then the documents say to build from `main`.

## Next step

Tag `v0.4.0-experimental` with release notes; then issues #5 and #72.

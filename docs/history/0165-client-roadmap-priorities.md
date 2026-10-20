# 0165 — Client roadmap priorities and validation gates

Date: 2026-10-20. Status: documentation complete; all implementation deferred.
First recorded work timestamp: 2026-10-20 16:12 America/Sao_Paulo.
Documentation checks completed: 2026-10-20 16:13 America/Sao_Paulo.
These are wall-clock timestamps, not an engineering-effort measurement.

## Goal and decision

Add the proposals from assessment 0164 to ROADMAP with priorities, dependencies,
estimated effort and verifiable acceptance criteria. The maintainer authorized
planning explicitly and excluded implementation. The roadmap now records that
distinction; passing existing gates does not authorize a feature batch.

P0 retains current correctness/security work. P1 contains G1 client integration,
G2 predictable shared operation and G3 portable recovery. P2 contains G4 the
derived-index lifecycle followed by G5 hybrid retrieval. G6 is cross-cutting:
define the protocol before implementation and execute the final end-to-end
evaluation after its dependencies. Estimates use the existing S/M/L convention.

This refines planned delivery and validation without replacing architecture:
ADR-0007 keeps the native stacked filesystem; ADR-0013 supplies the existing
query frontend direction; ADR-0014 remains separately accepted and unimplemented;
M3 keeps revisions/change tracking in the client. S9 and distribution acceptance
are referenced, not duplicated as new discoveries or marked complete.
No new ADR is necessary for this planning update: no architecture, UAPI, format,
backend or compatibility decision is changed. Future changes still require one.

## Acceptance design

- Require reference equality for supported structured queries and complete
  enumeration for the exact semantic-search oracle, including multiple pages.
- Exercise two OS identities, private/shared domains, fallback permissions,
  budget saturation, publication failures, lost events and completed revocation.
- Recover content and typed metadata together into a new tree and compare query
  answers and access denials; preserve non-overwrite defaults.
- Measure whole-workflow usefulness against the same search engine with files
  and a user-space metadata index; publish losses as well as gains.
- Fix numeric workload targets and comparison criteria before implementation and
  before observing results. Unspecified bounds cannot satisfy completion.
- Choose an implementation batch only after explicit maintainer authorization;
  this turn adds no prototype, package, executable, test or benchmark campaign.

## Changes and continuity

ROADMAP, CHANGELOG and the history index are updated. Journal 0164 receives a
dated addendum pointing to the adopted planning step; its original assessment is
preserved. Work is isolated in `codex/roadmap-gap-analysis`, following local
commit `2376c18`. Public main was fetched and remains `02381fb` at this check.
Journal 0165 was unused across the local worktree history inventories.

The institutional branch and concurrent correction branches are not integrated
or changed. The new section explicitly preserves their separate tracks.
The public-surface inventory was reviewed: README EN/PT, site, FAQ, SECURITY,
compatibility and help-wanted remain unchanged because no implemented behavior,
test result, security status, support commitment or call for contributors changes.
ROADMAP is the canonical location for these priorities; existing links point to it.
No external publication, issue, PR or outreach is part of this change.

## Validation and next step

`git diff --check` passed. `sh scripts/check-retired-claims.sh` returned
`RETIRED_CLAIMS_OK`; `python3 scripts/check-site.py .` passed for 12 HTML pages;
`python3 scripts/site-faq.py --check` confirmed the generated FAQ is current.
A local Python check resolved 231 relative Markdown destinations in the changed
documents, checked their explicit heading anchors, and confirmed all six G-items
and the implementation gate are present.
No kernel, VM, retrieval, restore or performance tests are claimed.
Next: review the concrete roadmap locally; implementation remains deferred.

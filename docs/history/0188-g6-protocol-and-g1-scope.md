# 0188 — G6 evaluation protocol and the G1 scope, before any package

Date: 2026-10-24. Status: protocol written; G1 package and G3 drill not started.
Work started: 2026-10-24 18:17 America/Sao_Paulo, when the maintainer accepted
this batch. Wall-clock time, not an engineering-effort measurement. The writing
itself is effort S (hours). The G1 package remains the existing estimate L, and
the G3 drill remains M.

## Goal and decision

The maintainer accepted the first evolution batch in this order: the G6
protocol, then the G1 client package, then the G3 recovery drill. This unit is
only the protocol and the scope. No adapter, kernel, UAPI, disk format or
measurement changes.

`docs/g6-evaluation-protocol.md` fixes the client, the corpus and the pass/fail
counts before any run. The client is the existing LangGraph `BaseStore` adapter
in `bench/client/langgraph_store.py`, pinned to langgraph 1.2.15 and
langgraph-checkpoint 4.3.0. The corpus is 1 000 files in each of two private
domains (uid 1001 and uid 1002) and 5 000 files in one shared domain, plus one
heterogeneous file. The preselected G1 objective is four integration-burden
counts at zero, together with zero answer differences against `InMemoryStore`.
Latency, memory, derived-index freshness and relevance have no ceiling. G2, G4,
G5 and the G6 final evaluation therefore cannot pass on this document.

G3's identity-map restore bounds are registered in the same protocol so they
exist before a restore run. The drill is not started. No new archive format is
chosen. S9 and issue 118 are untouched.

No new ADR. ADR-0007, ADR-0013 and ADR-0014 stay as they are. The comparison
method of journal 0134 is reused and no new source is added.

## Changes

The protocol, this journal, the history index, the changelog, the roadmap
status of G1, G3 and G6, the documentation index, a pointer in the client
contract, a cross-reference in `docs/references.md`, and a dated addendum on
journal 0165.

Public surfaces checked and left as they were: both READMEs, the site, the FAQ,
`SECURITY.md`, `docs/help-wanted.md` and `docs/compatibility.md`. The module's
tested behavior, support and status did not change. The roadmap is the plan
surface and was updated.

## Validation and limitations

Completed 2026-10-24 at 18:23 America/Sao_Paulo. `git diff --check` passed on the
tracked edits. The two new files were still untracked, so they were checked
separately for trailing whitespace and tabs; none were found.
`sh scripts/check-retired-claims.sh` returned `RETIRED_CLAIMS_OK`.
`python3 scripts/check-site.py .` passed for 12 HTML pages.
`python3 scripts/site-faq.py --check` confirmed the generated FAQ is current.
A local link check resolved 337 relative Markdown destinations in the nine
changed files. No module build, VM, equality run, restore drill or benchmark
was run. The historical index-size figure cited for the budget drill remains
the pilot in journal 0088. If 5 000 files fit in a 1 MiB budget on the current
module, that G1 case stays unmet and the count is not adjusted after the result.

## Next step

Implement the G1 package in the adapter and its test, against the protocol,
without changing `layer/` or the UAPI. The G3 drill script follows. G2 stays
on the S9 track.

## Follow-up, 2026-10-24

Journal [0189](0189-g1-adapter-package.md) wrote the adapter package and ran
the host checks. The mounted gate is still open, so this protocol's G1 item
is not complete.

## Follow-up, 2026-10-24 evening

The mounted gate passed. Journal 0189 records the counters, the `0664`
correction and the limits. G3 is still not started.

## Follow-up, 2026-10-24 evening — G3

Journal [0190](0190-g3-restore-drill.md) passed the identity-map drill
registered in this protocol. G2, G4, G5 and the final evaluation stay
unbound.

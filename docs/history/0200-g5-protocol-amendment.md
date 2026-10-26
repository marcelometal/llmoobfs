# 0200 — G5 protocol amendment proposed, before any package

Date: 2026-10-25. Status: amendment proposed; not accepted; nothing run.
Work started: 2026-10-25 10:20 America/Sao_Paulo, when the maintainer said to
go on after G4. Wall-clock time, not an engineering-effort measurement. The
amendment is effort S (hours). The G5 package keeps its estimate L.

## Goal and decision

G4 passed (journal [0195](0195-g4-derived-index-package.md)), so G5 is next
by dependency. ROADMAP rule 2 requires its relevance and latency targets
before implementation, so this unit is the amendment only, as for G4.

[Section 13 of the protocol](../g6-evaluation-protocol.md#13-g5-amendment-2026-10-25--proposed-not-accepted)
proposes:

- labels from LongMemEval (`longmemeval_s_cleaned`, MIT), 110 non-abstention
  instances drawn with seed 1 and stratified by type, one adapter file per
  turn, split over `alice`, `bob` and a shared `team`;
- engines fixed before the comparison: `all-MiniLM-L6-v2` scored exactly,
  SQLite FTS5 `bm25()`, and reciprocal rank fusion with k = 60;
- two paths: the MoobFS path (`QUERY2` answers the filter) and the same
  engines over plain files with a SQLite metadata table; an oracle that reads
  no maintained index;
- bounds: the two paths and the oracle give identical top 10 for every
  query, 0 results outside the caller's eligible set, revocation as in G4,
  min(10, eligible) results, fusion's `recall_all@10` at least the better
  branch's minus 0.02, and warm latency p95 at most 300 ms for one instance
  and 2 s for a whole 25 000-turn domain, in each of 3 runs.

## Alternatives and justifications

- **The G1 synthetic corpus with the stand-in model.** Rejected for G5. It
  has no relevance labels, and the stand-in model of section 12 makes no
  relevance claim.
- **LoCoMo, BEIR.** Not chosen. LongMemEval is a long-term memory benchmark
  for chat assistants with evidence labels at session and turn level, which
  is the target workload of the roadmap, and its data is MIT.
- **One file per session.** Rejected. Sessions are longer than the model's
  256 word pieces, so most of a session would never be embedded. A turn is
  closer to one memory.
- **An absolute recall floor.** Not set. No measurement of these engines on
  this sample exists, and a floor chosen after a run is not allowed. The
  floor is relative: fusion must not do worse than its own branches. Absolute
  numbers are published either way.
- **HNSW or another approximate engine now.** Deferred. Exact scoring is the
  oracle, and research-algorithm-fit section 2.4 tries an approximate engine
  only if the exact path misses a declared target.
- **FTS5 statistics from the filtered rows.** Rejected. The maintained index
  holds what the uid can read; statistics from the filtered rows would make
  the maintained index and the oracle disagree by construction, and would
  change with each filter.
- **Comparing speed between the two paths.** Not claimed. That needs the
  method of section 7. The latency bound is absolute and for the MoobFS path
  only.

No new ADR: no layer interface or decision changes. The derived indexes stay
in the client (client contract section 8, M3 section 5).

## Changes

Section 13 of the protocol and a note in its section 11; this journal; the
history index; the changelog; the G5 row of the roadmap; `docs/references.md`.

Public surfaces checked and left as they were: both READMEs, the site, the
FAQ, `SECURITY.md`, `docs/help-wanted.md` and `docs/compatibility.md`. None
states a G5 status.

## Validation and limitations

No download, model, index or run exists for G5.
The bounds bind only once accepted. The sample is drawn from one dataset in
English; nothing here claims another language or domain. The latency bounds
are for the dedicated host's 2011 CPU and include query embedding there.

## Next step

The maintainer accepts, changes or rejects section 13. On acceptance:
install the pinned packages and model on the dedicated host only, draw and
record the sample, extend the G4 watcher with the real model and the FTS5
table, write the mounted test and run it three times.

## Checks of this commit

Completed 2026-10-25 at 10:21 America/Sao_Paulo. `git diff --check`,
`sh scripts/check-retired-claims.sh` (`RETIRED_CLAIMS_OK`),
`python3 scripts/check-site.py .` and `python3 scripts/site-faq.py --check`
passed; relative Markdown links of the six changed files resolved, anchors
included. No download, build or run.

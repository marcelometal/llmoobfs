# 0194 — G4 protocol amendment proposed, before any package

Date: 2026-10-25. Status: amendment proposed; not accepted; nothing run.
Work started: 2026-10-25 09:48 America/Sao_Paulo, when the maintainer chose
to write the G4 protocol before any G4 code. Wall-clock time, not an
engineering-effort measurement. The amendment is effort S (hours). The G4
package keeps its estimate L.

Number: pull request 140, open when this was written, records the merge of
pull request 139 as journal 0193. This journal is therefore 0194.

## Goal and decision

Continue the evolution track outside the kernel. G2 is open and stays with
the S9 track, which another line of work is maturing. G4 is the next item by
dependency: it needs G1, whose mounted gate passed (journal 0189). ROADMAP
rule 1 considers P1 before P2. The maintainer chose to write the G4 protocol
now, while G2 stays open, and to start G4 code only after accepting it. Rule
2 requires the bounds before implementation is authorized, so this unit is
the amendment only.

[Section 12 of the protocol](../g6-evaluation-protocol.md#12-g4-amendment-2026-10-25--proposed-not-accepted)
proposes:

- the derived index: one vector per memory, per OS identity, in a `0700`
  cache outside the data directory, entries without value bytes;
- identity `(namespace, key)`, content version SHA-256, and a deterministic
  stand-in model `hash64-v1`/`hash64-v2` so a model change can be tested
  without a relevance claim;
- a state-based inotify watcher with a full pass at start and on
  `IN_Q_OVERFLOW`, the design of journal 0092;
- eleven scenarios, E1 to E11, from update to two publishers of one key,
  including a real queue overflow and writes made on the lower tree;
- bounds: inventory equal to a fresh scan per uid, nothing obsolete served,
  revocation and cache protection, live freshness p99 at most 1 s and maximum
  at most 5 s, recovery to equality within 60 s, each in 3 runs.

## Alternatives and justifications

- **Stat (mtime, size) as the content version.** Rejected. A change inside
  one timestamp tick with the same size keeps an obsolete vector; this is
  the racy-clean case git documents. The corpus is small enough to hash.
- **`user.moob.memory_id` as the identity.** Not introduced. LangGraph's
  identity is `(namespace, key)`, and a rename outside the adapter is a new
  key for LangGraph. Adding an attribute would change `put` for one client.
- **A real embedding model.** Deferred to G5. G4 tests that vectors follow
  the files, which a deterministic model shows without a download or a
  relevance number.
- **A derived index in the data directory, or shared between uids.**
  Rejected. It would break B4 of section 6 and would need its own access
  rule. One cache per uid keeps the eligible set equal to what that uid can
  open.
- **fanotify.** Not used. The layer refuses fanotify with file identifiers
  (journal 0030). inotify on the layer directory is the path already
  measured.
- **Simulated event loss.** Rejected. E8 lowers
  `fs.inotify.max_queued_events` before the watcher starts and stops the
  watcher, so the kernel's own overflow event is what the test sees. In
  Linux 6.12.112, `fsnotify_insert_event()` in `fs/notify/notification.c`
  queues one overflow event once `q_len` reaches the group's `max_events`,
  which `inotify_init1()` takes from that sysctl.

The bounds are workload choices. The live bound is based on journal 0071's
sidecar (17.9 ms p99, 306 ms maximum, attributes only), with room for
hashing and embedding in Python. No G4 number was seen before writing them.

No new ADR. The amendment does not change a layer interface or a decision.
ADR-0013 and ADR-0014 stay as they are. M3 section 5 stays: history in the
client, no kernel change cursor.

## Changes

Section 12 of the protocol and dated notes in its sections 10 and 11; this
journal; the history index; the changelog; the G4 row and section text of
the roadmap; `docs/references.md` (fsnotify source, feature hashing,
SHA-256, git's racy-clean note).

Public surfaces checked and left as they were: both READMEs, the site, the
FAQ, `SECURITY.md`, `docs/help-wanted.md` and `docs/compatibility.md`. None
states a G4 status. The module's tested behavior did not change.

## Validation and limitations

No adapter code,
watcher, VM, host run or measurement exists for G4. The bounds are proposed
and do not bind until accepted. The amendment was written while pull request
140 was open; that pull request also edits the roadmap and the history
index, so one of the two will need a merge of those lines.

## Next step

The maintainer accepts, changes or rejects section 12. On acceptance, write
the watcher and reconciler and the mounted test in `bench/client/`, then run
E1 to E11 on the dedicated Debian host. G5 waits on G4.

## Checks of this commit

Completed 2026-10-25 at 09:51 America/Sao_Paulo. `git diff --check` passed.
`sh scripts/check-retired-claims.sh` returned `RETIRED_CLAIMS_OK`.
`python3 scripts/check-site.py .` passed for 12 HTML pages.
`python3 scripts/site-faq.py --check` confirmed the FAQ is current. A local
check resolved 301 relative Markdown destinations, anchors included, in the
six changed files. No module build, VM or host run.

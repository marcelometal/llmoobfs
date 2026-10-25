# 0195 — G4 derived-index package; mounted gate passed in three runs

Date: 2026-10-25. Status: G4 bounds of protocol section 12 met in three runs
on the dedicated Debian host. G5 and the G6 final evaluation not started.
Work started: 2026-10-25 09:55 America/Sao_Paulo, when the maintainer
accepted section 12. Ended: 2026-10-25 10:15. Wall-clock time, not an
engineering-effort measurement. The package was estimated L; the code, the
test and the three runs took one working session, because the watcher design
was reused from journal 0092 and the host, corpus and adapter from G1.

## Goal and decision

Implement the G4 package against [protocol section
12](../g6-evaluation-protocol.md#12-g4-amendment-2026-10-25--proposed-not-accepted),
which the maintainer accepted as written before any run (journal
[0194](0194-g4-protocol-amendment.md)), and run its scenarios. No change to
`layer/`, the UAPI, the disk format or the adapter `bench/client/langgraph_store.py`.

## Changes

- `bench/client/g4_derived.py`: the stand-in model (`hash64-v1`, `hash64-v2`),
  the fresh-scan oracle, the per-uid cache, the exact derived search and the
  watcher. Standard library only; inotify through `ctypes`.
- `bench/client/g4_mounted.py`: the mounted gate, scenarios E1 to E11, the
  protected-cache check and the raw-output file. It reuses the G1 helpers for
  users, mount, domains and corpus.
- `bench/results/2026-10-25-g4-derived-index/`: the three runs, the two failed
  attempts and the environment.
- Protocol: the acceptance note in section 12 and a result note in section 11.
- Roadmap, changelog, history index and a pointer in the client contract.

Implementation choices the protocol left open, made before the runs:

- **Reconciled** is detected without a sentinel file. The watcher writes
  `idle_at` to its state only after a `select` with no data and `FIONREAD`
  zero. A change whose system call returned before time T was queued by then,
  so an `idle_at` later than T means it has been applied.
- **Reflected**, for F1, is the time the watcher made the entry durable:
  after the domain file's `rename` and the `fsync` of `applied.log`.
- F1's writes are back to back, with no pacing, which is the harsher case.
- A rename is a delete and a new key for LangGraph. The watcher reuses a
  vector dropped in the same batch with the same SHA-256 and model, so E4
  re-embeds nothing.
- One file per domain in the cache, rewritten atomically. All cache files are
  text (names, hex digests, hex vectors), so the "no value or note string"
  check is a plain substring search without false hits from binary data.
- The watcher runs on `/usr/bin/python3 -I` from a copy outside the
  operator's home, because uid 1001 cannot enter that home to execute the
  venv. The test process keeps the pinned venv.

## What ran

Dedicated Debian host, kernel `6.12.111+deb13-amd64`, Intel Core i7-2635QM,
ext4 lower, `domain_depth=1`, default `index_budget` (128 MiB). Module built
with `make layer` from the layer at `c793f8d` (no warning). Test process in
the G1 venv (Python 3.13.5, langgraph 1.2.15, langgraph-checkpoint 4.3.0).
Each run mounts afresh, plants the 7 000-file corpus and `salary.json`
(about 70 s), starts one watcher per uid and runs the scenarios in order.

```text
python bench/client/g4_mounted.py --ko layer/moobfs_layer.ko --label N --out runN.json
```

All three runs printed `G4_MOUNTED_PASS` and exited 0. In each run, 22
inventory comparisons with a fresh scan found 0 missing, 0 extra, 0 SHA-256,
0 model and 0 vector differences.

| Bound | Run 1 | Run 2 | Run 3 | Limit |
| --- | --- | --- | --- | --- |
| F1 uid 1001, 300 changes: p50 / p95 / p99 / max (s) | 0.084 / 0.103 / 0.108 / 0.114 | 0.052 / 0.087 / 0.098 / 0.113 | 0.118 / 0.169 / 0.444 / 0.454 | p99 ≤ 1, max ≤ 5 |
| F1 changes never reflected | 0 | 0 | 0 | 0 |
| E7 recovery after SIGKILL (s) | 0.785 | 0.790 | 0.775 | ≤ 60 |
| E8 recovery after overflow (s), overflows | 0.765, 1 | 0.784, 1 | 0.766, 1 | ≤ 60, ≥ 1 |
| E9 model switch (s) | 1.077 | 1.047 | 1.051 | ≤ 60 |
| E10 restart after lower-tree writes (s) | 0.775 | 0.794 | 0.786 | ≤ 60 |

Uid 1002's F1 figures follow uid 1001's within 0.01 s in every run.

Same in all three runs:

- Start pass, 6 000 eligible files: 0.82 to 0.87 s per uid. `salary.json`
  out of uid 1001's index and in uid 1002's.
- E4: 50 renames, 0 re-embeds for either uid.
- E5: right after each chmod, uid 1001's search scored the revoked item and
  dropped it because `open` failed (10 of 10); none was returned. After
  reconciliation 0 entries and 0 results for the 10 files; uid 1002 kept them.
- E6: 0 `team` entries for both uids while `team` was `2700`; 4 990 back for
  uid 1001 after `2770` (5 000 − 100 deleted + 100 new − 10 revoked).
- E8: the queue of 64 overflowed once; the sysctl was restored to 16 384.
- E9: during the switch, the three searches found 4 991 `team` entries still
  on `hash64-v1` and returned none of them; 0 items were scored with v1.
- E10: the 20 files written on the lower tree were visible through the mount,
  and the running watcher saw 0 of them. The start pass reconciled them.
- E11: the final name was one complete file, and each uid's entry matched it.
  In all three runs the last publication was uid 1002's (`dup-bob-29`). The
  degree of interleaving of the two publishers was not measured.
- Cache: each uid's `open` of the other's cache directory and files failed. 0
  of 5 674 canonical values and 0 of 1 372 note strings were found in the
  cache files.

Recorded, not bounded: cache size about 5.58 MB (uid 1001) and 5.01 MB (uid
1002); watcher `VmHWM` about 31.5 MB. The `embeds` counter in the state is
per watcher process, so the 28 to 31 at the end counts only the last
restart's work, not the run's.

## Failed attempts

Both before any of the three runs, both in the harness, and both kept in the
results directory.

1. The watcher, started as uid 1001, could not execute the venv Python under
   the operator's home (`EACCES`). Fixed by running the watcher on the
   system Python from a readable copy.
2. After F1 and the E3 inventories passed, a call to the derived-search
   check lacked its uid argument (`TypeError`). Fixed by giving it uid 1001
   as default. That attempt's F1 for uid 1001 was p99 0.378 s and maximum
   0.401 s, all 300 reflected. It is not one of the three runs.

## Limitations

- One host, one lower filesystem (ext4), one corpus size, one writer at a
  time for F1. The bounds are this workload's, not a guarantee.
- A model switch is all at once: entries are persisted at the end of the full
  pass, so for about one second a search under `team` returns nothing rather
  than mixing models. That meets the bound; it is a property to keep in mind
  for G5 on a larger corpus.
- inotify on the mount does not see writes made on the lower tree. Only a
  start or overflow pass reconciles them.
- The in-flight window after a revocation exposes that an item was scored
  (counts, timing). Content was not delivered. This is the known limit of
  ROADMAP rule 4, not a new finding.
- The stand-in model says nothing about relevance.
- No KVM guest and no repetitions beyond the three: the F1 numbers are this
  host's and are not a comparison with another store.

## Public surfaces

Roadmap and changelog updated. Both READMEs, the site, the FAQ, `SECURITY.md`,
`docs/help-wanted.md` and `docs/compatibility.md` do not state a G4 status and
were left as they were. The module's tested behavior did not change.

## Next step

G5: select the user-space lexical and vector engines with the bounded
comparison of ROADMAP G5, and register its relevance floor and recall@k as a
dated amendment before any run. G2 stays with the S9 track.

## Checks of this commit

Completed 2026-10-25 at 10:15 America/Sao_Paulo. `git diff --check`,
`sh scripts/check-spdx.sh`, `sh scripts/check-retired-claims.sh`
(`RETIRED_CLAIMS_OK`), `python3 scripts/check-site.py .` (12 pages) and
`python3 scripts/site-faq.py --check` passed. A local check resolved 315
relative Markdown destinations, anchors included, in the eight changed
documents. `py_compile` passed for both new scripts; no Python linter is
part of `make lint`.

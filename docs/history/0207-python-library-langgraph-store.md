# 0207 — Python library, phase 2: LangGraph store, G1 gate carried over (C5 done)

Date: 2026-10-27. Status: done on a local branch; with it, roadmap item C5 meets its three
acceptance criteria. Work started: 2026-10-27 about 08:05 America/Sao_Paulo, after the maintainer
asked this session to carry on with the pending items. Ended: 2026-10-27 08:43.
Wall-clock time, not an engineering-effort measurement. C5 was estimated M–L; phase 1 and 2
together took about one working day.

## Goal and decisions

The first acceptance criterion of C5 is "G1's validation carried over to the package". Phase 1
(journal 0204) left it open. This phase puts the LangGraph store on the library and runs G1's
mounted gate on it.

The gate had one case the library answers differently by design. Without the module, G1's
adapter returns every file the caller can open, so the gate required uid 1002 to see its own
`salary.json` (mode `0600` in the shared `team` domain), although `QUERY2` never returns it to
anyone. The library's fallback applies the kernel's rules (ADR-0023) and leaves it out. The
maintainer chose, before the run, a dated amendment of protocol section 5 for the library's
store: without the module `salary.json` is absent for uid 1001 and uid 1002, uid 1002 can still
open it by path, and 0 paths the caller cannot open. The adapter keeps the original wording, and
its recorded runs stand. Alternatives offered: a "what the caller can open" mode in the library
for the store (not chosen: the two backends would give different answers again), or leaving C5
at phase 1.

## Changes

- `python/src/moobfs/langgraph.py`: `MoobStore` (`langgraph.store.base.BaseStore`) on
  `moobfs.publish` and `moobfs.Domain`, with the G1 adapter's files: `<namespace>/<key>.json`
  with `{value, created_at, updated_at}`, `user.moob.v.<key>` with every number a double, and
  `user.moob.lg.updated`. It exports the names G1's gate uses (`PublicationStopped`,
  `MoobQueryError`, `RESOURCE_ERRNO`, `T_STRING`, `layer_mounted`), and `stats` with the
  adapter's keys. The filter semantics are LangGraph's, as in the adapter. The code started as
  a copy of the project's own adapter, relicensed under the library's LGPL by the maintainer's
  choice of ADR-0023.
- The library: `Domain.stats` counts `QUERY2`/`QUERY2_BATCH` calls and walks (`kernel_query`
  and `kernel_batch` take a `stats` dict); `publish` takes a test-only `_step` hook called after
  each step of the write sequence. A stop before the rename leaves the old file and removes the
  temporary one. The G1 adapter left the temporary file behind in that case.
- `python/pyproject.toml`: extra `langgraph` (`langgraph>=1.2`).
- `python/tests/test_langgraph.py`: the store against `InMemoryStore` for twelve filters
  (`$ne`, lists, nested objects, `3` and `3.0`, a missing field) on three namespaces with deletes,
  paging and `list_namespaces`; a publication stopped at each step before the rename; files
  written by the adapter and the store read by both, with identical attribute bytes. Skipped
  without `langgraph`, so not in CI.
- `bench/client/g1_mounted.py`: `--store adapter|package` (default `adapter`, the recorded
  runs) and the fallback check of the amendment for `package`.
- Protocol section 5 (the amendment), ADR-0023 decision 7 (note), ROADMAP C5 row (done),
  `python/README.md`, `docs/testing.md`, the changelog and the history index; journal 0205 got its
  dated note on the Pages deploy of PR 152 (`security.txt` answered HTTP 200).

## What ran

Dedicated Debian host, kernel `6.12.111+deb13-amd64`, ext4 lower, module built with `make layer`
from this branch (the layer is unchanged since `c793f8d`). A fresh venv with the packages of the
G1 venv (`pip freeze`: langgraph 1.2.15, langgraph-checkpoint 4.3.0, langchain-core 1.7.0) and the
library installed from a copy with `pip install --no-build-isolation --no-deps`, setuptools
84.0.0 by hash.

- `python/tests` against the installed package, as an ordinary user: 21 tests, OK, one root-only
  case skipped; the three `test_langgraph.py` tests passed.
- G1's mounted gate, as root, `--store package`, three complete runs in a row, about 2 min 10 s
  each: each printed `G1_MOUNTED_PASS`. Same in all three: every filter shape of the protocol on
  `alice`, `bob` and `team` equal to `InMemoryStore`; the whole `team` domain (5 000 files) in at
  least two `QUERY2` calls; pages of 100 equal to slices of the whole answer; `salary.json`
  absent for uid 1001, uid 1002 and root while mounted; the confined search without the mount
  root; the 30 concurrent publications complete; the fallback of the amendment; the 1 MiB budget
  refused with `ENOMEM`. `team` files were `0664`, owner 1001, group `moobteam`. In-flight misses
  of the concurrent case, recorded and not bounded: 80, 87 and 143. After planting, `GET_INFO`
  read `used=2883508`, 7 000 inodes, 3 domains built, and the test process's `VmHWM` was about
  61 MB.

Results: [`bench/results/2026-10-27-g1-python-library/`](../../bench/results/2026-10-27-g1-python-library/README.md).

## Limitations

- One host, ext4, the pinned LangGraph of G1; `test_langgraph.py` does not run in CI, which has
  no LangGraph.
- The in-flight misses are not compared with the adapter's: its runs did not record the number
  in the journal, and the case does not bound it.
- The library stays experimental (0.x) and is not on PyPI.

## Next step

Issue #145: tick C5. The next consumable item is C6 (DKMS, packages and signing, prepared in
VMs) or C7 (an agent adapter on the library), in the maintainer's order.

## Checks of this commit

Completed 2026-10-27 at 08:43 America/Sao_Paulo. `git diff --check`,
`sh scripts/check-spdx.sh`, `sh scripts/check-retired-claims.sh` (`RETIRED_CLAIMS_OK`),
`python3 scripts/check-site.py .`, `python3 scripts/site-faq.py --check` and `py_compile` of the
package, its tests and `g1_mounted.py` passed. A local check resolved 351 relative Markdown
destinations, anchors included, in the ten changed documents. The unit tests passed on macOS
(the Linux and LangGraph ones skipped). No kernel code changed.

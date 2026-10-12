# Issue and pull request labels

Labels have four groups: area, difficulty, priority and status. One area label
per issue is the norm; difficulty and priority are optional for proposals;
status labels record a gate, not progress. The `bug` label applied by the bug
report template is kept as is.

Rules:

- Every performance issue is born with `status/needs-evidence` and
  `area/bench-m4`, and keeps `status/needs-evidence` until an evidence file with
  command, kernel, hardware and repetitions exists in the repository.
- `status/blocked-on-m4` marks decisions that `docs/distribution-strategy.md`
  section 8 and `docs/value-assessment.md` section 9 condition on the M4
  benchmark (`QUERY_BATCH`, `READ_BATCH`, index persistence, public release).
- `status/review-request` is applied by the review-request template; closing
  such an issue records that the question was answered, not that the code was
  audited.
- `difficulty/good-first-issue` is reserved for tasks verifiable on the host or
  in ordinary CI, without loading the module. Such issues also carry GitHub's
  default `good first issue` label, because the repository's `/contribute` page,
  GitHub search and external aggregators look for that exact name.
- `help wanted` (GitHub default) marks every open issue that an outside
  contributor or reviewer can take; it is removed when someone is assigned.
- Pull requests receive `area/*` labels automatically from the paths they
  change (`.github/labeler.yml`, workflow `labeler.yml`). The bot only adds
  labels; a wrong one is removed by hand and stays removed unless a later push
  matches it again. Issues are labeled by their templates or by hand.
- `priority/p0` is for data loss, kernel crash or hang, or a leak between users;
  these usually go through `SECURITY.md` first.

## Scheme

| Label | Color | Meaning |
| --- | --- | --- |
| `area/kernel-layer` | `#B60205` | The stacked layer module (`layer/`): VFS operations, mount contract, attribute validation. |
| `area/index-query` | `#D93F0B` | The in-memory attribute index and the `QUERY`/`GET_INFO` ioctls (`layer/index.c`, `include/moobfs_ioctl.h`, oracle `moobfs-index`). |
| `area/tools` | `#FBCA04` | User-space tools (`tools/`): `mkfs.moobfs`, `fsck.moobfs`, `moobfs-attr`, `moobfs-check`, `moobfs-index`. |
| `area/tests-vm` | `#0E8A16` | Tests and the VM harness (`tests/`, `vm/`, `Makefile` targets, CI jobs). |
| `area/kasan` | `#006B75` | The KASAN/UBSAN/lockdep kernel, `vm/kasan.config`, `vm/kasan-guest.sh`, `.github/workflows/kasan.yml`. |
| `area/docs` | `#1D76DB` | Documentation: `docs/`, READMEs, policies, journals and ADRs. |
| `area/site` | `#0052CC` | The GitHub Pages site (`site/`, `scripts/check-site.py`, `pages.yml`). |
| `area/bench-m4` | `#5319E7` | The M4 benchmark harness and methodology (`docs/benchmark-plan.md`, `docs/benchmark-harness.md`, future `bench/`). |
| `area/security` | `#000000` | Security roadmap items (S1 to S11), hardening, disclosure process. Vulnerabilities themselves go through `SECURITY.md`. |
| `difficulty/good-first-issue` | `#7057FF` | Needs no kernel work; verifiable on the host or in CI; files and done criterion listed in the issue. |
| `good first issue` | `#7057FF` | GitHub default, kept for discovery; always paired with `difficulty/good-first-issue`. |
| `help wanted` | `#008672` | GitHub default; open to an outside contributor or reviewer. |
| `difficulty/intermediate` | `#008672` | Needs the VM harness or touches shared code (`common/`, tools with oracle comparison). |
| `difficulty/kernel-expert` | `#E99695` | Touches `layer/` or kernel semantics; needs VM, KASAN and the external review rule. |
| `priority/p0` | `#B60205` | Data loss, crash, hang or cross-user leak. Drop everything else. |
| `priority/p1` | `#D93F0B` | Wrong result, failing test, blocked contributor. |
| `priority/p2` | `#FEF2C0` | Everything else: improvements, documentation, cleanups. |
| `status/needs-evidence` | `#BFD4F2` | Claim or request not yet backed by a reproducible measurement or log. |
| `status/blocked-on-m4` | `#C5DEF5` | Decision deferred until the M4 benchmark results exist. |
| `status/review-request` | `#F9D0C4` | A delimited question packaged for an independent reviewer (template `review_request.yml`). |

## Reproduce with the GitHub CLI

Run from a checkout with `gh` authenticated against the repository. `--force`
updates color and description when the label already exists.

```sh
#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
mk() { gh label create "$1" --color "$2" --description "$3" --force; }
mk area/kernel-layer  B60205 "Stacked layer module (layer/): VFS ops, mount contract, attribute validation"
mk area/index-query   D93F0B "In-memory index, QUERY/GET_INFO ioctls, moobfs-index oracle"
mk area/tools         FBCA04 "User-space tools in tools/"
mk area/tests-vm      0E8A16 "Tests, VM harness, Makefile targets, CI jobs"
mk area/kasan         006B75 "KASAN/UBSAN/lockdep kernel and workflow"
mk area/docs          1D76DB "Documentation, READMEs, policies, journals, ADRs"
mk area/site          0052CC "GitHub Pages site and its checks"
mk area/bench-m4      5319E7 "M4 benchmark harness and methodology"
mk area/security      000000 "Security roadmap items and disclosure process (vulnerabilities: SECURITY.md)"
mk difficulty/good-first-issue 7057FF "No kernel work; verifiable on the host or in CI"
mk "good first issue"         7057FF "Paired with difficulty/good-first-issue; listed on /contribute"
mk "help wanted"              008672 "Open to an outside contributor or reviewer"
mk difficulty/intermediate     008672 "Needs the VM harness or touches shared code"
mk difficulty/kernel-expert    E99695 "Touches layer/ or kernel semantics; VM, KASAN and external review"
mk priority/p0 B60205 "Data loss, crash, hang or cross-user leak"
mk priority/p1 D93F0B "Wrong result, failing test, blocked contributor"
mk priority/p2 FEF2C0 "Improvements, documentation, cleanups"
mk status/needs-evidence  BFD4F2 "Not yet backed by a reproducible measurement or log"
mk status/blocked-on-m4   C5DEF5 "Deferred until the M4 benchmark results exist"
mk status/review-request  F9D0C4 "Delimited question packaged for an independent reviewer"
```

Creating the labels on GitHub is a separate step from this document; record the
date it was done in the journal.

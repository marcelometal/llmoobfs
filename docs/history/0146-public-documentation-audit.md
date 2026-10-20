# 0146 — Public documentation reconciled with code and recorded measurements

Date: 2026-10-20. Status: corrections and local documentation checks complete; integration and
publication pending. Review base: `syz-fifth-run` at `dfe114c`, including the pending pull
request 105 evidence. Branch: `codex/public-docs-audit`.

## Goal and scope

Audit the public surfaces listed in `docs/public-surfaces.md` against the current source and
the recorded experiments, alongside an independent source review. The documentation work uses
an isolated worktree. No module, UAPI, format, test harness or benchmark logic changes.

## Findings and corrections

| Finding | Evidence checked | Correction |
| --- | --- | --- |
| README and landing page say no repetitions with intervals exist and call older per-operation results measurements of the current module | Journals 0088, 0134, 0139 and 0143; stored `confirm40` results | Distinguish historical SQLite/client pilots from the later 40-repetition module comparison; state the unresolved 1–3 % p50 residual and the measured on-demand cost, without treating this as a fresh SQLite result |
| FAQ and research page say SQLite still wins short "N most recent" queries by 4–6x | Journal 0048, its before/after table; journal 0088, question 1 | Identify that result as pre-optimization; keep the separate, still-losing long traversal from the real client |
| `when-not-to-use` says speed and operation overhead are unmeasured; says near-budget answers can be incomplete | Journal 0088; ADR-0016; `layer/index.c`: `domain_lost`, `domain_fresh`, `domain_ensure`; journal 0130 | State measured pilots and missing measurements separately; failed updates trigger rebuilding or an explicit error, while fair sharing and full-scale saturation remain gaps |
| FAQ says a directory rename leaves false negatives until remount | `layer/index.c`: `moobl_index_renamed` marks `paths_stale`, `domain_fresh` rejects it, `domain_pin`/`domain_ensure_all` rebuild before queries; journal 0098 | Describe rebuild on the next query, retaining TOCTOU and direct-lower-write limitations; add a dated correction to spec-m3 rather than rewriting the original specification |
| ROADMAP presents completed work as future, and ADR-0012's opening status contradicts its implementation addendum | Journals 0068, 0070, 0072, 0073, 0084, 0088, 0091, 0094, 0098, 0101, 0102; ADR-0014 | Preserve the agreed order, label completed work and remaining gaps; add a dated status correction to ADR-0012 |
| Help page asks for completed tooling tasks and describes the LangGraph adapter as still being written | Journals 0089 and 0125; `bench/client/langgraph_store.py`, `man/`, CI and tool documents | Point to the existing adapter and invite concrete documentation/reproduction findings instead of reserving closed tasks |
| SECURITY refers to S1–S11 as mostly unimplemented; FAQ implies KASAN executes for every PR | Security roadmap S1–S13; `.github/workflows/kasan.yml` changed-path gate | Update the scope and distinguish a passing required job from an executed sanitizer suite |
| The S3 aggregate target is conflated with its complete acceptance criteria | Journal 0143 records 659/743 PCs (88.7 %) but `moobl_xattr_get` 0/4; security roadmap also requires each reachable function to be exercised | Preserve the measured aggregate result and explicitly leave the function-level evidence gap unresolved; do not change the maintainer's stabilization decision |

The memory limit descriptions also distinguish `index_budget` from the per-open-file query
buffer in `moobl_qbuf_get` (`layer/main.c`): up to 4096 records of 272 bytes are retained until
close, outside the index accounting. This is an existing implementation detail documented in
journal 0139, not a newly measured memory result.

The `confirm40/cmp-m0102-fix40-v2-dom.csv` table also shows why the residual must be called
**p50**, not all latency: Q1 p99 increases by 54.03 % (95 % interval +51.12 to +55.21 %).
README and landing page now state this tail result alongside the p50 summary. It is a stored
result inspected here, not a new measurement or a causal explanation.

## Decisions and alternatives

- Update current-state documents directly; preserve historical specifications and ADRs with
  dated correction notes. The dated value assessment and old journals remain historical.
- Keep the M4 numbers with their measured module and workload scope. Removing every old number
  would lose useful evidence; presenting them as the current tag would overstate it.
- Use the existing FAQ and journey generators, updating English and Portuguese together.
  No site redesign or new documentation framework.
- Add retired phrases to the existing check. This detects recurrence of known stale wording,
  not all semantic inconsistencies; the manual code-and-journal comparison is still necessary.

## Validation actually run

From the audit worktree:

```sh
python3 -m py_compile scripts/site-journey.py
python3 scripts/site-journey.py .
python3 scripts/site-faq.py
sh scripts/check-retired-claims.sh
python3 scripts/site-faq.py --check
python3 scripts/check-site.py .
git diff --check
```

The generator produced the journey pages; FAQ generated 60 elements and then matched its
source. Retired-claims check reported `RETIRED_CLAIMS_OK`; the site check validated 12 HTML
pages, links and its structural accessibility checks. Diff whitespace check was clean.
One temporary editing command had a Python quoting error before execution and changed no
files; it was corrected before the successful generator and checks above.

Negative round: branch `neg/public-docs-audit`, based on `dfe114c`, contains only the updated
retired-claims list with the old prose. `sh scripts/check-retired-claims.sh` exited 1 and
reported the stale statements, including unmeasured speed, incomplete budget answers and the
old research/landing-page status. The same check passes with these corrections. No negative
kernel run is applicable to this documentation-only change.

## Limits and next step

- No new VM, KASAN, fuzzing, benchmark or browser-render test was run by this audit. Reported
  runtime results are attributed to their existing journals. This is not independent
  reproduction of those experiments.
- The inventory's repository surfaces were reviewed. GitHub issue bodies, Discussions,
  releases, advisories and external posts were not changed or published; their live wording
  has not been independently audited here. The published website remains unchanged until a
  reviewed merge/deployment.
- `moobl_xattr_get` coverage and the residual performance cost are separate investigations;
  this audit does not resolve either cause. No new architectural decision is introduced.
- The coordinating review integrates this commit, adds the history index and CHANGELOG entry,
  and submits one reviewed batch when authorized. Reconcile any new runtime evidence before
  publication, especially the S3 function criterion.


## Addendum, 2026-10-20 — unresolved findings from review 0144

The coordinating reviewer supplied a Linux 6.12.112/ext4 VM reproduction: after opening with
`O_APPEND` and clearing it with `fcntl(F_SETFL)`, `pwrite` at offset zero changed direct ext4
`AAAA` to `ZAAA` (4 bytes), while the layer produced `AAAAZ` (5 bytes); `F_GETFL` showed append
clear on both. The backing open retains its initial flags (`layer/main.c`). Code inspection
also confirmed that `attrs_read()` (`layer/index.c`, the `n <= 0` branch after `vfs_getxattr`)
drops operational errors such as `EIO`/`ENOMEM` along with invalid encodings. The latter has
no dynamic reproduction with a failing lower filesystem yet. Neither finding is fixed.

Current limitations now state both with their different evidence levels: README EN/PT,
SECURITY, FAQ and its generated page, when-not-to-use, the landing page and generated status
pages in both languages. Links target the coordinator's journal 0144, to be integrated in the
same batch. The budget-recovery description is qualified so it does not imply the separate
lower-read problem is fixed by ADR-0016. Remaining duplicate README limitation bullets about
three repetitions, rename-until-remount and the aggregate S3 target were also synchronized
with the corrected primary sections.

No new runtime test was run by this documentation agent. The VM result above is the
coordinating review's evidence, to be recorded in journal 0144. Documentation generation and
validation were rerun; the negative test from the earlier audit is not repeated because these
new statements record open defects, not executable fixes. No publication occurred.


## Addendum, 2026-10-20 — local R1/R2 correction batch prepared

The coordinator requested the public-state update for the next local batch. Reviewed the
updated local handoff/rules and repository instructions, commit `fb84050` and journal 0149
for R1, and the R2 working diff plus journal 0150 in the independent correction worktree.
The R1 source synchronizes mutable backing-file flags before delegated I/O; R2 propagates
operational `getxattr` errors while retaining the exclusions for `ENODATA`, oversized values
(`ERANGE`) and invalid encodings. No module source was changed by this documentation work.

README EN/PT, SECURITY, FAQ and generated copy, when-not-to-use, landing page and generated
status EN/PT now distinguish **implemented local corrections** from the released tag, which
still contains R1/R2. Journal 0144 is preserved as the original review. They link to journals
0149 and 0150 for validation details. The wording explicitly leaves integrated validation,
batch KASAN/lockdep and publication pending. It does not claim a completed full R2 suite or
a real lower-filesystem I/O-fault reproduction. The R2 targeted test injects the value-read
result; the initial code-reading evidence and later test evidence must not be conflated.

At this documentation snapshot, R1's clear-append regression had positive and negative VM
results and a full VM pass before the set-append assertion was added; the added assertion
awaited the integrated run. R2 had a targeted positive and expected negative failure, while
its full VM run was still in progress. These results belong to the correcting agents' journals;
no runtime test was performed by this documentation agent. The old unqualified wording that
neither fix exists was added to retired claims; historic records remain unchanged.

Local checks after regeneration: `python3 scripts/site-journey.py .`,
`python3 scripts/site-faq.py`, `sh scripts/check-retired-claims.sh`,
`python3 scripts/site-faq.py --check`, `python3 scripts/check-site.py .`, and
`git diff --check`. All completed successfully: the FAQ has 61 generated elements and matches
its source, `RETIRED_CLAIMS_OK`, 12 valid HTML pages, and no whitespace errors. The coordinator
must integrate this documentation with both source corrections and reconcile the pending validation wording
against the actual final runs before publication. No push, pull request or external update.

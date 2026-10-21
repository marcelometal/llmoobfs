# 0170 — PR 109: integration and remaining validation

Date: 2026-10-20. Status: complete locally; awaiting maintainer review before push.

Validation window: approximately 22:42–23:50 America/Sao_Paulo on 2026-10-20.
Elapsed calendar time including builds and waits, not measured labor effort.

## Goal and scope

Resume PR 109 from `09fc102` in an isolated worktree. Merge public `main`
(`d797a68`) without losing either history, correct stale status records, and
complete the pending validation of R2-3, R2-4, R2-6, R2-8 and R2-10 before review.
Do not push, merge the GitHub PR or publish an advisory in this preparation.

## Integration and decisions

- Local merge `b8202b0` preserves both parents. The only conflict was the history
  index; entries 0155, 0167 and 0168 are all retained. Add the missing 0156 index
  entry as well. No kernel-code conflict and no change to another checkout.
- Journal 0156's original table predates later evidence. Preserve the old record
  and reconcile it with a dated addendum after validation. ADR-0017 is accepted
  (commit `7a5adda`); its commitments remain planned, not implemented.
- Use dedicated Linux VMs, the pinned Linux 6.12.112 source/configuration and the
  existing fail-nth mechanism and stack filter. Run negative mutations in another
  worktree; a compilation error is not a successful negative test.
- Add an actual rename with targeted path-allocation failure and dentry eviction
  for R2-6, with an uninjected control. Unknown paths are permitted by the API;
  stale paths are not.
- Add a directory-removal race for R2-10. Function tracing observes the scan's
  first file after listing; the existing scan-delay parameter holds the window.
  No production test hook is added. These two tests run in the KASAN faults step.

## Evidence and experiments

- Initial integrated `MOOB_ONLY=faults sh vm/kasan-local.sh` passed with
  `MOOBFS_KASAN_ONLY_PASS`. This is a subset, not a complete sanitizer run.
- First R2-3 mutation failed compilation: removing its callers left `rec_drop`
  unused under `-Werror`. Remove the unused helper in the negative branch and
  retry; that compiler failure is not regression evidence.
- Logs remain outside Git; the negative outcomes below were checked against
  their specific runtime failure signatures.
- R2-3 negative at `905c4e7`: the existing `setxattr-then-move` check detects
  fault 2, where the moved file is missing from the destination answer. It ends
  with `unexpected=1` and the harness fails. The unmodified integrated faults
  step passed; this is executed regression evidence, not a compiler failure.
- First R2-8 mutation (`3f46981`) unexpectedly passed: prior operations had already
  created the heterogeneous `kind` name node in the shared domain, so changing
  permissions could reuse it without the allocation this check needed to fail.
  Give each chmod attempt a fresh built domain. This retains the public syscall
  scenario while requiring the transition to allocate its new partition's node.
- R2-8 retry at `219840c`, with the revised test: fault 2 drops one entry but
  leaves the inode count unchanged. The test reports `unexpected=1`; the harness
  fails. This closes the earlier false negative of the shared-domain fixture.
- R2-4 mutation `c0be08b` is detected by the create and mkdir agreement checks
  (faults 5 and 12): each leaves a name visible on only one side. Link's error
  recovery is not independently isolated by that negative run.
  Inspection of the exact Linux 6.12.112 `iget5_locked()` explains this limit:
  it first calls `ilookup5()` and allocates only on a cache miss. The link's
  positive source dentry already holds the matching upper inode used by
  `moobl_get_inode()`, so this path reuses it. Allocation failure cannot force
  upper-inode creation to fail under that precondition. Keep the defensive
  recovery; do not add an artificial production hook or claim a link-specific
  negative result. The interpretation is limited to this kernel and path.
- R2-6 mutation `c10969f` preserves the old path after the confirmed fault and
  dentry eviction (`path=a flags=0`); the test fails with `r26-stale-path`.
  Its positive control returns `b`; the fixed fault case returns `PATH_UNKNOWN`.
- R2-10 mutation `7a8bd7f`: the observed removal is followed by query failure
  (`errno=ENOENT`), and the harness fails at `r210-dead-directory`. The fixed
  run returns success and zero results after the same observed scan/removal.
- Full `make vm-test` at test source `b969623`: exit 0, Debian
  `6.12.111+deb13-amd64` in a disposable QEMU VM. Includes the three lower
  filesystems, `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS` and
  `MOOBFS_LAYER_PASS`. Both new test programs compile through the shared build
  list here; their fault/trace scenarios execute in the separate KASAN harness.
- `make test` on macOS: all nine host tests pass. This is separate from Linux
  module execution. Checkpatch, shell syntax, retired claims, site (12 pages),
  FAQ/source consistency, whitespace and preservation comparisons pass.
- Full `sh vm/kasan-local.sh` at test source `b969623`: exit 0,
  `MOOBFS_KASAN_PASS`, Linux 6.12.112 with KASAN/UBSAN/lockdep in a dedicated
  x86-64 KVM VM (two CPUs, 3 GiB). The three lower filesystems, construction
  races, domain release/rebuild, client acceptance, power-loss replay, I/O
  errors, 12 permission-matrix seeds and the remaining suite steps passed.
  No kernel diagnostic was reported by the harness.
- Final fault step: `operations=22 calls=321 faults=299 unexpected=0`;
  `RENAMEPATH_FAULT_PASS` and `DEADSCAN_PASS`. In the injected rename, the
  post-eviction result has an empty path and `flags=1` (`PATH_UNKNOWN`); the
  control returns `b`. The synchronized directory-removal query returns
  `rc=0 errno=0 count=0`. These are positive results on the final fixture,
  including the revised R2-8 chmod preparation.
- Eight executed source/configuration files were SHA-256 compared with the
  local preparation and matched. Module/UAPI/common sources remain identical
  to PR 109 at `09fc102`. Main's five badge lines, external records, Zenodo
  citation and `CITATION.cff` are preserved. Public `main` and PR 109's branch
  were reconfirmed at `d797a68` and `09fc102` after execution.

## Reproduction and evidence

On a dedicated Linux host with the pinned environment, at test source `b969623`:

```sh
MOOB_ONLY=faults sh vm/kasan-local.sh  # focused positive run; not a full suite
sh vm/kasan-local.sh                 # full sanitizer suite
make vm-test                        # full Debian/QEMU suite; requires Docker
```

For a negative round, use a separate worktree at the mutation commit listed
above and run `MOOB_ONLY=faults sh vm/kasan-local.sh`. Expect a nonzero exit and
the specific regression signature; a build error or unrelated failure is not
evidence. The negative commits are retained locally pending publication review.

Raw logs, source hashes, module hashes and the evidence manifest are retained
outside Git in `MoobFS-local/revisao/pr109-validation-0170/`. Versioned evidence
is this summary and the executable regressions; no credentials or environment
dump is committed. Later commits in this unit only close the documentation.

## Limitations and next step

The five pending regression cases now have positive and negative evidence with
the hard-link boundary explained above. The new R2-6/R2-10 scenarios are ext4,
root, on one kernel; they do not cover every rename failure, namespace race or
architecture. Tests are evidence under those conditions, not a security proof.

Issues 110–119, ADR-0017's planned mechanisms and the separate syzkaller coverage
investigation remain open. No new security review, performance measurement,
release, advisory publication or CI run on this unpublished continuation is
claimed. Site/FAQ and security/compatibility text retain the PR's existing
technical state; only the test inventory, status reconciliation and references
needed changes. README metadata from `main` is preserved by the merge.

Next: maintainer review of the signed local continuation. Before an authorized
push, recheck both `main` and remote `review2-batch`; update the existing PR
without rewriting history or touching its original checkout. Required PR CI
must run on that later published head. No push or GitHub merge has been made.

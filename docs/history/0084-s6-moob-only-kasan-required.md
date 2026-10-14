# 0084 — S6 closed: `MOOB_ONLY`, KASAN ready to be a required check, review requests sent

Date: 2026-10-13. Status: `MOOB_ONLY` implemented and tested in the Debian VM; the workflow
change is untested until the pull request that carries it runs; the branch-protection change is
**not made yet**. Local series (after pull request 59).

## `MOOB_ONLY` (community action 17)

`vm/suite.sh` runs only the named steps when `MOOB_ONLY` is set: `ext4`, `btrfs`, `xfs`
(everything of `run_lower` for that lower filesystem), `demo`, `clients`, `powerloss`,
`ioerror`, `reserve`. It reaches the two guests by the kernel command line (`vm/init`,
`vm/in-container.sh`, `vm/run.sh`) and by the environment (`vm/kasan-guest.sh`,
`vm/kasan-local.sh`).

Decision: a partial run can never be taken for a complete one. It ends in `MOOBFS_ONLY_PASS`
(or `MOOBFS_KASAN_ONLY_PASS`), not in `MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS` or
`MOOBFS_KASAN_PASS`; the harness accepts the partial marker only when `MOOB_ONLY` was set by the
caller, and CI never sets it. An unknown step name fails the run.

Evidence (`make vm-test`, Debian 6.12.111 in QEMU):

- `MOOB_ONLY="btrfs reserve"`: 101 s, `MOOBFS_ONLY_PASS: steps btrfs reserve (not a complete run)`.
- `MOOB_ONLY="bogus"`: `MOOBFS_VM_FAIL: unknown MOOB_ONLY step bogus`.
- No `MOOB_ONLY`: 261 s, the three complete markers as before.

Not run: `MOOB_ONLY` under the KASAN harness.

## KASAN as a required check (community action 18)

Decision (maintainer): make "Layer suite under KASAN/lockdep" a required status check of `main`.

Obstacle: the workflow started only on pull requests touching `layer/` or `vm/`. A required
check that never starts leaves a documents-only pull request blocked forever. Change: the job
starts on every pull request; its first step compares the pull request with its base, and the
heavy steps run only when something under `layer/`, `vm/`, `include/`, `common/`, `tests/`, the
`Makefile` or the workflow itself changed. Otherwise the job passes at once and says so in its
summary. The list is wider than before on purpose: the module compiles `common/attr.c` and the
headers of `include/`, and the suite is built from `tests/`.

Order: the protection rule is turned on after this workflow is on `main`, not before, for the
same reason. Until then the rule in `CONTRIBUTING.md` (a green run is required) is kept by hand.

## Outside review requested (community action 15)

Issues 16 and 17 carry the questions, the files to read, the known limitations, and the commit
under review. Learnt on the way: the kernel lists reject HTML mail.

Nothing has been reviewed yet. A reply is recorded as an issue comment and a journal entry.

Note of 2026-10-14: a description of where the request was posted was removed from this
section by the maintainer's decision: the repository records the technical content of outside
answers, not outreach activity (`AGENTS.md`).

## State of S6

`SECURITY.md` (done before), the compatibility policy (journal 0080, accepted), `MOOB_ONLY` and
the workflow change (this journal). Left: turning the protection rule on, and the build-time
refusal of an unlisted kernel (compatibility rule 1.2).

## Next step

Publish this series; turn the rule on; the complete fstests run with the exclusion list (S2);
the M4 report.

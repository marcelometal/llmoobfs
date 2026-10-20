# 0153 — The review correction batch integrated with the chmod-during-build fix

Date: 2026-10-20. Status: done locally; pull request opened, merge awaits KASAN in CI and the maintainer.

## Goal

Bring the local correction batch of review 0144 (journals 0144 to 0150: R1, `F_SETFL` and the
backing file's append flag; R2, attribute-read errors during a build) together with the fix of
review 0151 (journal 0152, pull request 106). Then publish it as one pull request.

## Decisions

- The batch continues in this repository's own worktree, on branch `review-corrections-batch`,
  from the batch's last commit `2cbb9fe`. The previous worktree of the batch is left as it was.
- Pull request 106 is merged into the batch (`93a13e6`) instead of waiting for it on `main`.
  Its commits are the same either way, so the merge of `main` into the batch after 106 lands
  adds nothing new. The recommended order is 106 first, then this batch.
- The code merged without conflict (`layer/index.c`, `vm/suite.sh`, `vm/build-tests.sh`).
  The batch changes how attribute reads fail; 0152 changes where the file's homogeneity is
  decided. Four status texts conflicted (`README.md`, `README.pt-BR.md`, `SECURITY.md`,
  `docs/history/README.md`). They were combined so that every surface states the three findings
  together: R1 and R2 of review 0144, and the finding of review 0151. The surfaces 0152 had left
  for this batch now name the 0151 finding too: `ROADMAP.md`, `docs/faq.md` and
  `site/faq.html`, `docs/security-roadmap.md`, `docs/when-not-to-use.md`.
- `scripts/check-retired-claims.sh` caught a retired claim ("tem todas as correções até essa
  data") in the combined Portuguese status. Both READMEs now say which fixes the tag contains.
- R1 and R2 are correctness defects, so they get no advisory. The 0151 finding has the private
  advisory opened for pull request 106, to be published after its merge.

## The vm-test guard

The first complete `make vm-test` of the integrated tree stopped after `STALL_PASS`: QEMU was
killed by `timeout 600` in `vm/in-container.sh`. There was no failed assertion and no kernel
diagnostic. Every step up to `stall` had passed, including `FCNTL_FLAGS_PASS` and
`BUILD_RACE_PASS` with the chmod check on ext4, btrfs and XFS. The two remaining steps then
passed alone (`MOOB_ONLY="bigdir xattrread"`: `BIGDIR_PASS`, `XATTRREAD_PASS`, 73 s).

The 600 s dates from phase 2a (`9afc275`, 2026-10-10), when the suite was a fraction of its
present size. The run is in Docker under TCG on an arm64 host, and each batch adds steps. The
guard exists to stop a hung guest, not to bound the run, so it becomes 1200 s (`9da99c3`).
The CI workflows have their own limits and are not changed.

The following complete run took 517 s, build included, so the integrated suite does not always
exceed 600 s. On this host it runs close to the old limit, and the first run crossed it.
The cause of that slower run was not measured; load from other work on the host is the likely
one. The new guard gives margin; it does not make the suite faster.

## Evidence

- `make checkpatch`: clean on the integrated tree.
- First complete `make vm-test`: killed by the 600 s guard after `STALL_PASS`; every step up to
  there passed. `MOOB_ONLY="bigdir xattrread"`: both passed.
- Complete `make vm-test` with the 1200 s guard (`9da99c3`): exit 0 in 517 s.
  `FCNTL_FLAGS_PASS` (R1) and `BUILD_RACE_PASS` with `chmod during a build: absent` (0152) on
  ext4, btrfs and XFS; `XATTRREAD_PASS` (R2); `BIGDIR_PASS`; `MOOBFS_ACCEPTANCE_PASS`;
  `MOOBFS_LAYER_PASS`.

## Limitations

- The R2 tests inject the attribute-read result; they do not reproduce a physical I/O fault
  of the lower filesystem (journal 0150).
- The local KASAN/UBSAN/lockdep gate of the batch ran at `49098aa`, before the merge of 0152.
  The integrated tree is checked under KASAN by the pull request's CI.
- Still open: the `moobl_xattr_get` coverage gap (0145) and the 1 to 3 % query residual
  (0147).

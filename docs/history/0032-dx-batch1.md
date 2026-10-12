# 0032 — Developer experience on GitHub: plan and batch 1

Date: 2026-10-11. Branch: `community/dx-batch1` from `main` (`1d23b43`). Status: batch 1
completed; the GitHub settings were applied by the maintainer (see the notes in
Limitations).

## Goal

Make the repository easier to find and to use for first-time arrivals, complementing
`community-plan.md` and `seo-geo-plan.md`. The diagnosis, the three batches and what was left
out of scope are in [`docs/dx-plan.md`](../dx-plan.md). The maintainer approved the plan and the
execution of batch 1.

## Decisions

| Topic | Decision | Brief justification |
| --- | --- | --- |
| Discovery labels | Use the standard labels `good first issue` and `help wanted` alongside our own labels | `/contribute`, GitHub search and aggregators recognize only the exact name |
| Local lint | `make lint` becomes the single source; the CI `Lint` job calls it | The local command and CI must not diverge |
| SPDX | Check moved to `scripts/check-spdx.sh`, still restricted to C | Issue #15 (shell and Python) is left for newcomers; the script makes the task easier |
| CODEOWNERS | `* @claudiofp`, without requiring code owner approval | It only routes review requests; the approval rule stays in `GOVERNANCE.md` |
| Citation | `CITATION.cff` with entity author "Studio Moob" | It is a research project; the entity avoids recording unnecessary personal data |
| "Stale" bot | Do not adopt | It automatically closes newcomers' issues |

## Changes

- New: `docs/dx-plan.md`, `.github/CODEOWNERS`, `CITATION.cff`, `scripts/check-spdx.sh` and
  this journal.
- `Makefile`: `help` and `lint` targets. `lint` runs `shellcheck --shell=sh --severity=warning`
  on the tracked `vm/*.sh` and `scripts/*.sh`, and then `scripts/check-spdx.sh`.
- `.github/workflows/ci.yml`: the shellcheck and SPDX steps of the `Lint` job were replaced by
  `make lint`. The DCO step did not change. The old `ls vm/*.sh build/*.sh` found nothing in
  `build/`, which is ignored by Git; the new list uses `git ls-files` and includes `scripts/`.
- `docs/labels.md`: rules and script for `good first issue` and `help wanted`.
- `docs/development.md`: documents `make help` and `make lint`, replacing the sentence "There is
  no `help` or `lint` target yet".

## Evidence

Commands run in this session, on macOS (Apple Silicon):

- `sh scripts/check-spdx.sh`: passed. The set of files is identical to that of the old CI
  `find`, checked with a `diff` of the two sorted lists.
- `docker run --rm -v "$PWD:/mnt" -w /mnt koalaman/shellcheck:v0.10.0 --shell=sh
  --severity=warning vm/in-container.sh vm/kasan-guest.sh vm/run.sh vm/suite.sh
  scripts/check-spdx.sh`: exit code 0. CI uses the `shellcheck` from Ubuntu 24.04's apt
  (a different version); the first real run of `make lint` in CI will be the pull request's.
- Local `make lint`: failed with the expected message, because `shellcheck` is not installed on
  the host.
- `make help`: prints the list of targets.
- `make test`: `attr_test` and 9 image tests, OK.
- `docker run --rm -v "$PWD:/app" -w /app citationcff/cffconvert:2.0.0 --validate`: "Citation
  metadata are valid according to schema version 1.2.0".
- The `community/profile` API reports `issue_template: null`, but the "Community standards"
  page in the browser shows the eight items as present. The API does not recognize YAML issue
  forms; nothing was changed.

No kernel test was run: nothing in `layer/`, `kernel/` or `vm/` changed.

## Limitations

The GitHub writes of this round were denied by the session's permission control and were not
applied. They are left for the maintainer:

```sh
R=studiomoob/llmoobfs
# secret scanning, push protection and branch deletion after merge
gh api -X PATCH repos/$R --input - <<'JSON'
{"delete_branch_on_merge": true,
 "security_and_analysis": {"secret_scanning": {"status": "enabled"},
                           "secret_scanning_push_protection": {"status": "enabled"}}}
JSON
# discovery labels
for n in 5 6 8 12 13 15; do gh issue edit $n -R $R --add-label "good first issue,help wanted"; done
for n in 7 9 14 16 17 18; do gh issue edit $n -R $R --add-label "help wanted"; done
# pinned issues (maximum of three)
for n in 16 17 5; do gh issue pin $n -R $R; done
```

The welcome discussion was drafted outside the repository. Publishing and pinning it (pinning
only exists in the web interface) is left to the maintainer.

Note of 2026-10-11: the maintainer applied the settings command. Checked with
`gh api repos/studiomoob/llmoobfs`: `delete_branch_on_merge: true`, `secret_scanning` and
`secret_scanning_push_protection` as `enabled`. Still pending: the labels on the issues, the
pinned issues and the welcome discussion.

Note of 2026-10-11: in PR #27, the `Lint` job (with `make lint` and the `shellcheck` from Ubuntu
24.04's apt) and the `Tools and sanitizers` job passed on the first run.

Note of 2026-10-11: renumbered from 0031 to 0032 when bringing `main` into the branch, because
journal 0031 (remount time) entered `main` first. The `good first issue` and `help wanted` labels
and the pinned issues (#16, #17, #5) were applied by the maintainer; checked with
`gh issue list --label` and the GraphQL query `pinnedIssues`.

Note of 2026-10-11: the maintainer published and pinned the welcome discussion
[#29](https://github.com/studiomoob/llmoobfs/discussions/29), "Welcome to MoobFS — start here",
in Announcements. Checked with the GraphQL query `pinnedDiscussions`. With this, all items of
batch 1 are applied. In PR #27, after merging `main`, the `Lint`, `Tools and sanitizers` and
`Native Linux VM` jobs passed.

## Next step

Merge PR #27. Batch 2: devcontainer and Codespaces with measurement of the time and disk of
`make vm-test`, OpenSSF Scorecard, CodeQL, `checkpatch.pl` as a warning, labeler and a
first-contribution message.

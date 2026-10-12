# 0039 — Developer experience on GitHub: batch 2 (tooling)

Date: 2026-10-11. Branch: `community/dx-batch2`, created from `origin/main` (`0a7bf54`,
merge of PR #27) and rebased, before publication, onto `8a4f1db` (merge of PR #24). Status: implemented; the Codespaces measurement, the OpenSSF Best Practices enrollment
and two repository settings remain pending (see Limitations).

## Goal

Execute batch 2 of [`docs/dx-plan.md`](../dx-plan.md): devcontainer, OpenSSF Scorecard,
CodeQL for the user-space C, `checkpatch.pl` as a warning, and path-based labels with a
first-contribution message. Nothing in `layer/`, `kernel/` or the format changed.

Numbering: `origin/main` and all remote branches were checked; the highest was 0034 (journals
0033 and 0034 from PR #24, which entered `main` during this round). This journal got 0035 (see the renumbering note at the end).

## Decisions

| Topic | Decision | Summary justification |
| --- | --- | --- |
| Devcontainer image | `mcr.microsoft.com/devcontainers/base:3.0.10-ubuntu24.04`, pinned by digest | Same Ubuntu version as the CI runners: `shellcheck` 0.9.0, gcc 13 and clang 18 the same as in CI |
| Docker in the devcontainer | Feature `docker-in-docker:4` (Moby, without Compose) | `vm/run.sh` only needs `docker build` and `docker run` without privilege; the QEMU guest remains the only place where the module is loaded |
| Devcontainer tools | `.devcontainer/post-create.sh` installs via apt, without third-party features | Short, readable list; `libclang-rt-18-dev` was added after `make fuzz` failed without it |
| `hostRequirements` and the "Open in Codespaces" button | Do not declare or advertise yet | The plan requires measuring time, disk and minimum machine in a real Codespace first |
| Scorecard | `push` to `main` and a weekly schedule, `publish_results: true`, SARIF to code scanning | Only these triggers are supported; publishing requires the badge and restricts the workflow to approved actions |
| Permissions | `contents: read` at the top of each new workflow; write only in the job that needs it | Least privilege; Scorecard forbids write at the workflow level |
| CodeQL | Advanced workflow, `build-mode: manual`, `make tools attr-tools bench-tools`, `security-extended` query suite | For C, only what the compiler sees is extracted; the manual build limits the analysis to `tools/`, `common/`, `bench/` and the headers and tests they compile. The "default setup" is off in the repository, so there is no conflict |
| `checkpatch.pl` | `scripts/checkpatch.sh` + `make checkpatch`, a step with `continue-on-error` in the `Lint` job | The local command and CI are the same; a warning because the baseline still has 14 warnings |
| Source of `checkpatch.pl` | Stable tag `v6.12.112` (the same as the KASAN kernel), from git.kernel.org with the `gregkh/linux` mirror on GitHub as an alternative, SHA-256 verified | Both servers deliver the same bytes; the hash makes the source irrelevant for integrity |
| Labeler and greeting | `pull_request_target`, without checkout of the PR code | Needed to label and comment on PRs from forks; safe because nothing from the PR is executed |
| Removed labels | `sync-labels: false` | The bot only adds; the maintainer's manual choice is not undone |
| Dependabot | `devcontainers` ecosystem in addition to `github-actions` | Updates the devcontainer feature as is already done with the actions |
| CodeQL for workflows (`actions`) | Not included | Outside the request; a candidate for a cheap next step (`build-mode: none`) |

Versions pinned by commit SHA (annotated tags resolved to the commit), checked with
`git ls-remote` on 2026-10-11:

| Action | Version | Commit |
| --- | --- | --- |
| `ossf/scorecard-action` | v2.4.4 | `2d1146689b8cda280b9bc96326124645441f03bc` |
| `github/codeql-action` (`init`, `analyze`, `upload-sarif`) | v4.38.3 | `24c54180a607b1449ed407dd24f251e4e9147c8d` |
| `actions/labeler` | v7.0.0 | `bf12e9b00b37c5c0ca2b87b79b2daf7891dbda13` |
| `actions/first-interaction` | v3.1.0 | `1c4688942c71f71d4f5502a26ea67c331730fa4d` |
| `actions/checkout`, `actions/upload-artifact` | v7.0.1, v7.0.2 | same as in `ci.yml` |

Documentation consulted with `npx ctx7@latest` (`/ossf/scorecard`, `/websites/codeql_github`,
`/devcontainers/features`, `/websites/containers_dev`) and, for the details Context7 did not
bring, the `README.md` and `action.yml` of the actions themselves at the pinned tag. One finding: the README
of `actions/first-interaction` v3.1.0 shows `issue-message`/`pr-message`, but the `action.yml`
declares `issue_message`/`pr_message`; the workflow uses the names from `action.yml`.

## Changes

- New: `.devcontainer/devcontainer.json`, `.devcontainer/post-create.sh`,
  `.github/workflows/{scorecard,codeql,labeler,first-interaction}.yml`, `.github/labeler.yml`,
  `scripts/checkpatch.sh` and this journal. All with an SPDX line.
- `Makefile`: `checkpatch` target and a line in `make help`.
- `.github/workflows/ci.yml`: `make checkpatch` step in the `Lint` job, with `continue-on-error`.
- `.github/dependabot.yml`: `devcontainers` ecosystem.
- `README.md`: OpenSSF Scorecard badge next to the CI badge. `README.pt-BR.md` has no
  badges and was not changed.
- `docs/development.md`: devcontainer section (without advertising Codespaces), `make checkpatch` and
  correction of an old sentence that said CI runs `shellcheck` only on `vm/*.sh`.
- `docs/labels.md`: labeler rule. `docs/dx-plan.md`: status of batch 2.

Labeler mapping: `layer/**` and `kernel/**` → `area/kernel-layer` (there is no dedicated label
for the M1 module in `kernel/`; this is the closest); index and `QUERY` files →
`area/index-query`; `tools/**` and `common/**` → `area/tools`; tests, `vm/`, `Makefile`,
lint scripts, `.devcontainer/` and `ci.yml` → `area/tests-vm`; KASAN files →
`area/kasan`; `docs/**`, root `*.md` and `CITATION.cff` → `area/docs`; site → `area/site`;
`bench/**` and `docs/bench*.md` → `area/bench-m4`; `SECURITY.md`, `docs/security-roadmap.md`,
`codeql.yml` and `scorecard.yml` → `area/security`.

## Evidence

Commands executed in this session (macOS host, Apple Silicon):

- `sh scripts/checkpatch.sh` on the host: hashes checked; exit code 1 because of
  warnings. Baseline: **0 errors, 14 warnings** in 7 files — `kernel/main.c` 9
  (`LINE_SPACING`), `layer/index.c` 4 (2 `DEEP_INDENTATION`, 1 `ENOSYS`, 1
  `BLOCK_COMMENT_STYLE`), `layer/main.c` 1 (`BLOCK_COMMENT_STYLE`); the other four clean.
- `docker run rhysd/actionlint:1.7.7`: no problems in the new workflows nor in the changed
  `ci.yml`. Only finding, predating this batch: SC2251 in `kasan.yml` (see Next step).
- `docker run koalaman/shellcheck:v0.10.0 --shell=sh` (default severity) on
  `scripts/checkpatch.sh` and `.devcontainer/post-create.sh`, and `--severity=warning` on all the
  `make lint` scripts: exit code 0.
- Local devcontainer, with `npx @devcontainers/cli@0.80.1` and Docker Desktop (arm64; **not a
  Codespace**, which is x86-64):
  - `devcontainer build`: `"outcome":"success"`.
  - `devcontainer up` built and started the container, and the inner `dockerd` became ready, but
    the CLI stopped after "Container started" without running the `postCreateCommand`; the cause was not
    investigated. `post-create.sh` was then run with `docker exec` (16 s).
  - Inside the container, on a copy of the tree with its own Git index (the worktree points to a
    `.git` outside the mount): `make test` OK (9 tests); `make lint` OK with `shellcheck`
    0.9.0; `make checkpatch` with the same 14 warnings; `make bench-smoke` with
    `BENCH_SMOKE_PASS`; `make fuzz FUZZ_TIME=10` failed for lack of
    `libclang_rt.asan-aarch64.a` and passed after adding `libclang-rt-18-dev`; `make
    test` with ASan/UBSan (clang) OK.
  - `make vm-test` inside the container, via docker-in-docker: passed (see the note below).
- The workflows `scorecard.yml`, `labeler.yml` and `first-interaction.yml` do not run on this PR
  (only on `main` or with the definition from the base branch). `codeql.yml` and the checkpatch step
  run on the PR itself; the results are in the notes below.

No kernel test was affected: nothing in `layer/`, `kernel/`, `vm/` or the format changed.

## Limitations

- **Codespaces not measured.** No Codespace was created in this session (it is a billed resource
  on the maintainer's account). The machines available for the repository, according to `gh api
  repos/studiomoob/llmoobfs/codespaces/machines`, are `basicLinux32gb` (2 cores, 8 GiB,
  32 GiB of disk) and `standardLinux32gb` (4 cores, 16 GiB, 32 GiB). The numbers of the
  local devcontainer above do not apply to Codespaces (different architecture, amd64 emulation in
  `docker build`). To measure, the maintainer can run:

  ```sh
  gh codespace create -R studiomoob/llmoobfs -b main -m basicLinux32gb
  # inside the Codespace:
  df -h / /var/lib/docker; nproc; free -g
  /usr/bin/time -v make vm-test 2>&1 | tail -25
  df -h / /var/lib/docker; docker system df; du -sh build
  ```

  With the numbers (total time, peak disk, whether the 2-core machine is enough), record a
  note in this file, declare `hostRequirements` and only then put the button in the README.
- **Scorecard**: the badge shows "no data" until the first run on `main`; the
  `api.scorecard.dev` API responded 404 for the repository on 2026-10-11.
- **GitHub settings** (writes denied in this session; left to the maintainer). The
  default `GITHUB_TOKEN` permission is `write` and Actions can approve PRs, which
  Scorecard penalizes (Token-Permissions). The workflows already declare their own permissions:

  ```sh
  gh api -X PUT repos/studiomoob/llmoobfs/actions/permissions/workflow \
    -f default_workflow_permissions=read -F can_approve_pull_request_reviews=false
  ```

- **OpenSSF Best Practices**: enrollment at <https://www.bestpractices.dev> requires the
  maintainer to log in on the site; it was not done.
- The `checkpatch.pl` downloaded from git.kernel.org may be blocked by cgit's anti-bot
  protection; in that case the script tries the mirror on GitHub. If both fail, the step fails as a
  warning and does not block the PR.

Note from 2026-10-11: `make vm-test` inside the local devcontainer (docker-in-docker, Docker
Desktop on an Apple Silicon Mac with 12 CPUs and 8 GiB for the Docker VM, amd64 image under
emulation) finished with exit code 0 and the markers `MOOBFS_VM_PASS`, `MOOBFS_ACCEPTANCE_PASS` and
`MOOBFS_LAYER_PASS` (Debian 6.12.111 guest in QEMU/TCG). Total time of `make vm-test`: 6 min
02 s, including building the image from scratch in the inner Docker. Space: image
`moobfs-m1-test` with 1.29 GB (`docker system df` in the inner Docker) and `build/` with 294 MB. This
shows that the devcontainer can run the suite via docker-in-docker, but **these are not Codespaces
numbers**: a different host architecture and a different machine; the Codespaces measurement remains
pending.

Note from 2026-10-11: on PR #30, first run, `Lint`, `Tools and sanitizers`,
`Native Linux VM` and `Analyze C (tools, common, bench)` (with the `CodeQL` check) passed. In `Lint`, the
warning step downloaded the three `checkpatch.pl` files, checked the hashes ("OK") and listed
the same baseline warnings; the job finished green, as expected from
`continue-on-error`. CodeQL (CLI 2.27.2, `security-extended`) recorded the analysis of
`refs/pull/30/merge` with 0 results; the list of extracted files was not inspected beyond
the build log.

Note from 2026-10-11: renumbered from 0035 to 0039 when bringing `main` into the branch. Journal
0035 (phase Q2a) entered `main` through PR #31, and the branches `m3/query-v2-q2b` to
`m3/query-v2-q2d` already use 0036 to 0038.

Note from 2026-10-12: the maintainer applied the `GITHUB_TOKEN` permissions command. Checked with
`gh api repos/studiomoob/llmoobfs/actions/permissions/workflow`:
`default_workflow_permissions: read` and `can_approve_pull_request_reviews: false`. Still
pending: the Codespaces measurement and the OpenSSF Best Practices enrollment.

Note from 2026-10-12: OpenSSF Best Practices enrollment done by the maintainer (project 15408,
<https://www.bestpractices.dev/projects/15408>), without granting the OAuth app access to the
organizations. The coordinating session prepared the justifications from the repository and
filled in the form in the maintainer's browser, with the personal answers
(`know_secure_design`, `know_common_errors`) given by the maintainer. Result, checked at
`https://www.bestpractices.dev/projects/15408.json`: `badge_level: passing`, 100%, obtained on
2026-10-12T12:03Z. There are 60 criteria Met, 5 N/A (no keys, passwords, network protocol or
cryptographic randomness) and 2 Unmet, both only suggested: `test_most` (coverage not measured)
and `crypto_call` (SHA-256 implemented in `common/hash.c`). `static_analysis_fixed` was
answered after the CodeQL triage (journal 0045, PR #42): 14 alerts closed as
`fixed` in the analysis of `main` (`b19780b`) and 21 dismissed with justification; no CodeQL
alert open. Badge added to `README.md`. The Codespaces measurement remains pending.

## Next step

Measure `make vm-test` in a Codespace and decide on the button. Bring the 14 `checkpatch.pl` warnings to zero and
make it mandatory (item S5 of `docs/security-roadmap.md`). Fix in `kasan.yml` the
`! grep -q '^MOOBFS_VM_FAIL:'` that is not the last command of the step: with `bash -e`, a negation
outside a condition does not stop the script (SC2251), so this test does not fail the step on its own;
today the absence of `MOOBFS_KASAN_PASS` covers the case.

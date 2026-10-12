# 0041 — Developer experience on GitHub: batch 3 (showcase)

Date: 2026-10-11. Branch: `community/dx-batch3`, created from `origin/main` (`34f175b`,
merge of PR #30). Status: implemented in the repository; consolidating the releases, uploading the
social preview image and creating the translation issues are left to the maintainer (writes on
GitHub denied in this session; see "Maintainer actions").

## Goal

Execute batch 3 of [`docs/dx-plan.md`](../dx-plan.md): README with a real demonstration and
quickstart before the experimental-stage warning, social preview image, an index of `docs/`
with the language of each document, and a single destination for the benchmark evidence. Nothing in
`layer/`, `kernel/` or the format changed.

Numbering: `origin/main` (last 0039), all remote branches (the
`m3/query-v2-q2b` to `q2d` ones use 0036 to 0038) and the local branches were checked. Number 0040 was left free for
the next phase of the `m3/query-v2-*` series, which has been reserving numbers in sequence; this journal
uses 0041. Finding: the local branch `codex/kernel-index-reuse-research` (main checkout, not
published) has a `0039-kernel-index-reuse-research.md`, which collides with the 0039 of `main` and
needs to be renumbered before being published.

## Decisions

Maintainer decisions, made before this session: a single release for the benchmark
evidence; demonstration in text with real output, not a GIF or asciinema; social preview uploaded
by hand; translation issues drafted here and created by the maintainer.

| Topic | Decision | Summary justification |
| --- | --- | --- |
| Source of the demonstration | New block in `vm/suite.sh`, after the three lowers, on ext4, with prefix `DEMO| ` and marker `DEMO_PASS` | The suite did not print a readable sequence; the block has assertions, so the README output is also a test and CI (including KASAN, which runs on PRs that touch `vm/`) re-executes it |
| Mount options in the demonstration | Default (`domain_depth=1`), `QUERY2` on a domain directory; v1 only as a contrast (refused to the user, answered to root) | Reflects the behavior of `main` after phase Q2a |
| Preparing the domains | `mkdir`/`chown`/`chmod` of the directories on the lower ext4 before mounting | Changing the mode of a domain directory with the layer mounted puts the domain in fallback until the remount (spec-query-v2 §3, Q2a); preparing beforehand avoids this without a remount in the script |
| User who queries | `query --uid 1001`, as in the rest of the suite | There is no `su` or `setpriv` in the initramfs; the test client switches uid before the call, and the README says so |
| Excerpt in the README | The `DEMO| ` lines without the prefix and without the serial console `CR`, omitting only the final `GET_INFO` call (command and output entirely) | Short block before the warning; no output line changed; the journal keeps the complete sequence |
| Experimental warning | Same content in a short list, right after the quickstart; facts dated 2026-10-11 | Maintainer's request; see "Updated facts" |
| Evidence release | Tag `bench-evidence`, pre-release, `--latest=false`, created as a draft, published only after checking the attachments through the API | Does not appear as "Latest"; the draft tolerates immutable releases at creation time |
| Evidence manifest | The table in `bench/evidence-release.md` (release text) is also the manifest read by the script | A single source for name, size and SHA-256; the published text and what the script checks do not diverge |
| Verification after upload | Size and digest through the GitHub API and a new download from the public URL, with local `sha256` | Maintainer's requirement; the download checks what a reader receives, not only what the API declares |
| Deleting the old pre-releases | Only after all attachments are checked; `gh release delete` without `--cleanup-tag`, and the script checks that each tag still exists | The tags mark the commit of each campaign |
| New campaigns | `add` subcommand of the same script, rule in `docs/benchmark-harness.md` §7.1 and `bench/results/README.md` | Prevents a new campaign from going back to creating its own pre-release |
| Social preview | Versioned SVG in `docs/assets/`, script with `rsvg-convert`, generated PNG | `docs/assets/` is not published on Pages, so the site does not change; the site's colors and mark |
| Index of `docs/` | `docs/README.md` in English, by group, with language and one line per document | It is the entry point for newcomers; the README and README.pt-BR point to it |
| Translations | File `<name>.en.md` next to the original, which prevails (CONTRIBUTING, language policy) | Explicit convention in the issues to avoid divergent proposals |

## Changes

- `vm/suite.sh`: demonstration block (function `demo`, prefix `DEMO| `, assertions, marker
  `DEMO_PASS`). `docs/testing.md`: the marker's line.
- `README.md`: opening sentence, "See it run" section with the real output, "Quickstart" with four
  commands, compact experimental warning right below, badges kept (including the
  Scorecard one), link to the index of `docs/`. Outside the top, only what contradicted `main`: "At a
  glance" and "How it works" now describe the per-domain `QUERY2`; "Not implemented" and
  "Limitations" no longer say there is no measurement at all; `DEMO_PASS` in the list of markers;
  two lines in the documentation map.
- `README.pt-BR.md`: mirror of the top and of the same corrections; it also gained the phase Q2a line in
  the evidence table, which was missing.
- `bench/evidence-release.md` (release text and manifest), `scripts/bench-evidence-release.sh`
  (SPDX, POSIX `sh`), `bench/results/README.md` (index of the campaigns and rule for new ones), and the
  seven `bench/results/*/README.md` with the new link to the attachment, the name of the original pre-release
  and, in `profile-q7`, the missing SHA-256.
- `docs/benchmark-harness.md` §7.1: where a campaign's artifacts go.
- `docs/README.md` (index), `docs/assets/social-preview.svg`, `docs/assets/social-preview.png`,
  `docs/assets/render-social-preview.sh`.
- `docs/dx-plan.md` (status of batch 3), `CHANGELOG.md`, `docs/history/README.md`.

Journals 0025, 0027 and 0029 still cite the pre-releases by name, on purpose: they were
the destination on the date they were written, and the tags remain.

### Updated facts in the warning

| Before (2026-10-10) | Now (2026-10-11) | Source |
| --- | --- | --- |
| "the UAPI v1 (`QUERY`, `GET_INFO`) may change" | v1 and v2 may change; with domains on (default), v1 is for administrators only | Journal 0035, `docs/spec-query-v2.md` |
| "no performance numbers have been measured (M4 pending)" | Only pilot-level measurements on a cloud VM; M4 not done; no claims | `bench/results/`, journals 0025 to 0031 |
| "three kernel-side defects" (0015, 0016) | Defects found and fixed by the tests and reviews themselves, citing 0015, 0016 and 0035, without a count | Journal 0035 lists an `oops` and a `rename` error in the Q2a code before the merge |
| README.pt-BR: "Sem versão lançada" ("No version released") | Only the source pre-release tag `v0.1.0-experimental` | Journal 0026 |

## Demonstration

Complete sequence, from the `DEMO| ` lines of `build/vm/console.log` (prefix and `CR` removed),
run of 2026-10-11 described in "Evidence":

```text
# (prepared on the lower ext4: alice/ uid 1001 0700, bob/ uid 1002 0700, team/ 0755)
# mount -t moobfs_layer -o lower=/lower none /mnt/layer
# moobfs-attr set /mnt/layer/alice/cache.md kind string decision
# moobfs-attr set /mnt/layer/bob/deploy.md kind string decision
# moobfs-attr set /mnt/layer/team/style.md kind string decision
# moobfs-attr set /mnt/layer/team/salary.md kind string decision
# moobfs-attr list /lower/alice/cache.md
kind	string	decision
# stat -c '%A %u %n' /mnt/layer/team/*
-rw------- 1002 /mnt/layer/team/salary.md
-rw-r--r-- 0 /mnt/layer/team/style.md
# query --uid 1001 /mnt/layer/alice q2 kind eq string decision
16	cache.md
# query --uid 1001 /mnt/layer/team q2 kind eq string decision
18	style.md
# query --uid 1001 /mnt/layer/bob q2 kind eq string decision
open: Permission denied
# query --uid 1001 /mnt/layer kind eq string decision
MOOB_IOC_QUERY: Operation not permitted
# query /mnt/layer kind eq string decision
16	alice/cache.md
17	bob/deploy.md
18	team/style.md
19	team/salary.md
# query /mnt/layer info
uapi=1 format=1 name_max=64 string_max=1024 raw_max=4096 per_file=64
domain_depth=1 heterogeneous=1 fallback_domains=0
lower=ext4 caps=0x8 flags=0x10 budget=134217728 used=1050 entries=4 inodes=4
```

Reading: `team/salary.md` (0600, owner 1002, in a root 0755 domain) does not satisfy the
homogeneity rule and stays out of the partition; that is why Alice does not see it, and Bob would not see it either
through `QUERY2` (Q2a fail-safe). `GET_INFO` still reports `uapi=1`: the version field did not
change in Q2a.

## Benchmark releases

Inventory on 2026-10-11 (`gh release list`, `gh release view --json assets`): **seven**
`bench-2026-10-11-*` pre-releases, not eight as the request said, plus `v0.1.0-experimental`.
None is a draft; none is immutable. The seven attachments have distinct names, so they fit in a single
release. Sizes from 1.09 MB to 129.4 MB, 162.4 MB in total; the SHA-256 published by GitHub
(`digest`) match the ones cited in `bench/results/*/README.md` (the one for `profile-q7` was not
cited and was added).

`scripts/bench-evidence-release.sh` (default `--dry-run`):

- `migrate`: downloads each attachment from the original pre-release into `build/bench-evidence/download/` and
  checks size and SHA-256 against the table; creates `bench-evidence` as a draft if it does not exist;
  uploads what is missing (an attachment already present is only accepted if identical; never uses `--clobber`);
  checks size and digest through the API; publishes; downloads again from the public URL and checks; only then
  deletes the seven pre-releases and checks that each tag is still on the remote.
- `add FILE...`: for new campaigns; requires a line in the table, checks the local file,
  uploads, checks through the API and the public URL, and updates the release text from the file.

## Social preview

`docs/assets/social-preview.svg` → `social-preview.png` (1280×640, 86 684 bytes) with
`rsvg-convert` 2.62.3 (Homebrew). Content: the site's mark, "MoobFS", "Linux filesystem layer for
LLM-agent memory", one sentence about what the layer does, and the band "Experimental research project ·
GPL-2.0 · no FUSE · run it in a disposable VM". No performance claim. Checked
scaled down to 400×200: title, subtitle and band legible. The font comes from the system (Arial or
Helvetica, with DejaVu Sans as an alternative), so another computer may generate a slightly
different PNG.

## Index of `docs/` and translations

`docs/README.md` lists the 31 documents at the root of `docs/` (checked by script: none
missing) and points to `decisions/`, `history/`, `bench/results/` and `assets/`. Entry documents only in Portuguese, chosen for translation issues: the
`QUERY2` specification (default interface since Q2a), the M2 specification, ADRs 0007 and
0009, the security roadmap (cited by issue #16), the benchmark harness and
`ai-workflow.md`. Commands for the maintainer (labels as in `docs/labels.md`: `good first
issue` together with `difficulty/good-first-issue`):

```sh
mk() {
    gh issue create -R studiomoob/llmoobfs --label "good first issue" --label "help wanted" \
        --label area/docs --label difficulty/good-first-issue --label priority/p2 \
        --title "$1" --body-file -
}

mk 'Translate docs/spec-query-v2.md (domain-scoped QUERY2, UAPI v2) to English' <<'EOF'
`docs/spec-query-v2.md` specifies `MOOB_IOC_QUERY2`, the query a client uses by default since phase Q2a (journal 0035): permission domains, the homogeneity rule, the ioctl contract and the phases Q2a to Q2e. It exists only in Portuguese, so anyone writing a client or reviewing the layer has to read it through a machine translation.

**Task.** Add `docs/spec-query-v2.en.md`, an English translation of the current file.
- First line: "Translation of `docs/spec-query-v2.md` at commit `<sha>`. The Portuguese original prevails." (see the language policy in CONTRIBUTING.md).
- Keep the status words exactly: proposal, accepted, implemented, and the dated "Implementação Q2a" notes that record how the code differs from the text.
- Do not add or soften claims. Keep the section numbers, so links like §3 still work.
- Add the file to `docs/README.md` with language EN.

No kernel knowledge is needed beyond reading carefully; ask in the issue if a sentence is ambiguous. Sign off your commit (DCO).
EOF

mk 'Translate docs/spec-m2.md (stacked memory layer) to English' <<'EOF'
`docs/spec-m2.md` is the specification of the stacked layer that the code implements (pass-through over ext4, btrfs and XFS, typed `user.moob.*` attributes, the RAM index and `QUERY`). Newcomers are pointed to it from the README, but it exists only in Portuguese. `docs/architecture-overview.md` (English) describes the code; it does not replace the specification.

**Task.** Add `docs/spec-m2.en.md`.
- First line: "Translation of `docs/spec-m2.md` at commit `<sha>`. The Portuguese original prevails."
- Keep "proposal" versus "implemented" exactly as the original says, and keep section numbers.
- Add the file to `docs/README.md` with language EN.

Sign off your commit (DCO).
EOF

mk 'Translate ADR-0007 and ADR-0009 to English' <<'EOF'
Two decisions explain most of the current design: ADR-0007 (MoobFS as a layer stacked over a mature filesystem) and ADR-0009 (the attribute index stays in the kernel, partitioned by permission domain). Both are in Portuguese only.

**Task.** Add `docs/decisions/0007-stacked-memory-layer.en.md` and `docs/decisions/0009-query-role-and-user-space-index.en.md`.
- First line of each: "Translation of `<file>` at commit `<sha>`. The Portuguese original prevails."
- Keep the status line, the alternatives and the dated maintainer decisions as they are; do not summarise them away.
- Update the "Decisions and history" row of `docs/README.md`.

ADR-0009 is long (about 300 lines); a pull request with ADR-0007 alone is welcome. Sign off your commits (DCO).
EOF

mk 'Translate docs/security-roadmap.md to English' <<'EOF'
`docs/security-roadmap.md` lists the security and robustness items S1 to S11 (threat model, xfstests, syzkaller, hostile lower filesystem, kernel lint, disclosure, fuzzing, hardening, resource limits, module signing). Review requests such as #16 refer to it, and security reviewers are the readers least likely to read Portuguese.

**Task.** Add `docs/security-roadmap.en.md`.
- First line: "Translation of `docs/security-roadmap.md` at commit `<sha>`. The Portuguese original prevails."
- Keep the S-numbers and the state of each item ("a fazer", done, proposal) exactly.
- Add the file to `docs/README.md` with language EN.

Sign off your commit (DCO).
EOF

mk 'Translate docs/benchmark-harness.md (M4 harness specification) to English' <<'EOF'
`docs/benchmark-harness.md` specifies the M4 benchmark harness in `bench/`: dataset generator, SQLite baselines and equivalence, latency records, statistics, and (§7.1) where campaign artefacts go. The campaign results in `bench/results/` are in English, but the specification behind them is only in Portuguese.

**Task.** Add `docs/benchmark-harness.en.md`.
- First line: "Translation of `docs/benchmark-harness.md` at commit `<sha>`. The Portuguese original prevails."
- Translate numbers and units as they are; add no performance statement.
- Add the file to `docs/README.md` with language EN.

Sign off your commit (DCO).
EOF

mk 'Translate docs/ai-workflow.md (AI-assisted development rules) to English' <<'EOF'
`docs/ai-workflow.md` (26 lines) explains how AI-assisted work is governed in this repository: the shared rules in `AGENTS.md`, and how Codex, Claude Code and Cursor read them. CONTRIBUTING.md welcomes AI-assisted contributions, so this page matters to newcomers, but it is only in Portuguese.

**Task.** Add `docs/ai-workflow.en.md`.
- First line: "Translation of `docs/ai-workflow.md` at commit `<sha>`. The Portuguese original prevails."
- Add the file to `docs/README.md` with language EN.

A short first contribution. Sign off your commit (DCO).
EOF
```

## Evidence

Commands executed in this session (macOS host, Apple Silicon, Docker 29.8.2):

- `make vm-test`: exit code 0 in 6 min 21 s, with the Docker image built from scratch.
  Guest Debian 6.12.111+deb13-amd64 in QEMU 10.0.13 (TCG). Markers: `MOOBFS_VM_PASS`,
  `QUERY2_PASS`, `ISOLATION_PASS`, `BENCH_LAYER_PASS` and `LAYER_LOWER_PASS` for ext4, btrfs and
  xfs, `DEMO_PASS`, `ACCEPTANCE_CLIENTS_PASS`, `POWER_LOSS_REPLAY_PASS`,
  `IO_ERROR_INJECTION_PASS`, `MOOBFS_ACCEPTANCE_PASS` and `MOOBFS_LAYER_PASS`. The
  demonstration output is above.
- `docker run koalaman/shellcheck:v0.10.0 --shell=sh --severity=warning vm/suite.sh` and, at the
  default severity, `scripts/bench-evidence-release.sh`: code 0.
- `scripts/bench-evidence-release.sh` (simulation): downloaded the seven attachments (20 s), checked
  size and SHA-256 of each against the table and listed the writes it would make (create the draft,
  seven uploads, seven deletions). `add` with a file not in the table: refused; with a downloaded
  attachment: checked and upload listed; with a copy one byte larger: refused by size;
  unknown argument: refused.
- `docs/assets/render-social-preview.sh`: PNG 1280×640 (`magick identify`), checked
  visually at full size and scaled down.
- The block of `gh issue create` commands above, executed with `gh` replaced by a function
  that prints the arguments: six calls, five labels each, complete titles and bodies.
- Other results (lint, SPDX, CI) are in the notes below.

## Limitations

- **`--apply` was not executed**, because writes on GitHub are denied in this session. Only the
  simulation path and the downloads were tested; creating the draft, the uploads, the
  verification through the API and the public URL, and the deletions run for the first time with the
  maintainer. The script assumes that `gh release view <tag>` finds the release while still a draft; this
  was not exercised here.
- Until `--apply`, the new links in `bench/results/*/README.md`, `bench/results/README.md` and
  in the READMEs respond 404. That is why `--apply` must run before the merge of this PR (from
  the branch, because the script reads `bench/evidence-release.md` locally).
- The `bench-evidence` release is recent and will appear above `v0.1.0-experimental` in the list of
  releases; marked as pre-release and `--latest=false`, it does not become "Latest". The list goes from
  eight entries to two.
- The demonstration uses the test client `query`, not a tool for users; the README
  says so. It does not measure time or show anything beyond the four files.
- The social preview image depends on the system fonts of the machine that generates it.

## Maintainer actions

1. Releases (before the merge, from the branch `community/dx-batch3`):

   ```sh
   scripts/bench-evidence-release.sh            # simulation: checks downloads and lists writes
   scripts/bench-evidence-release.sh --apply    # creates, uploads, checks and only then deletes the seven pre-releases
   ```

   Optional: `--target <sha>` pins the commit of the `bench-evidence` tag (default: `main`).
2. Social preview: Settings → General → Social preview → Edit → Upload an image →
   `docs/assets/social-preview.png`.
3. Translation issues: run the `gh issue create` commands from the previous section.

Note from 2026-10-11 (review by the coordinating session): the paragraph introducing the demonstration
said that Alice receives in `team/` "só o arquivo que pode ler" ("only the file they can read"), suggesting a per-caller
filter. By the homogeneity rule (`docs/spec-query-v2.md` §3), `salary.md` stays out of the
partition of `team/` for any caller; `QUERY2` does not check permission per result. The
two READMEs now explain this. Demonstration output and tests unchanged.

Note from 2026-10-12: the three maintainer actions were done and checked by the coordinating
session.

- Releases: before the merge of PR #33, the session ran the simulation of
  `scripts/bench-evidence-release.sh` in a worktree of the branch. The seven files were downloaded and
  checked against the manifest, and the writes listed. Then the maintainer ran `--apply`.
  Checked with `gh release list` and `gh release view bench-evidence`:
  - two releases remain, `bench-evidence` (pre-release, published) and `v0.1.0-experimental`;
  - the seven attachments have name, size and SHA-256 equal to those in `bench/evidence-release.md`
    (comparison by `diff`);
  - the seven `bench-2026-10-11-*` tags still exist.

  The seven `releases/download/bench-evidence/...` links in the repository respond 200, and no
  link points to the old releases. `bench-evidence` appears above
  `v0.1.0-experimental` because it is more recent; neither of the two is "Latest".
- Translation issues: created as #35 to #40 from the commands in the section above, each with
  `good first issue`, `help wanted`, `area/docs`, `difficulty/good-first-issue` and `priority/p2`.
- Social preview: image uploaded by the maintainer. The API shows
  `usesCustomOpenGraphImage: true`, and the image published by GitHub has the same SHA-256 as
  `docs/assets/social-preview.png`.

Note from 2026-10-12: the site now uses the same image in `og:image` and `twitter:card`
(`summary_large_image`) on the four pages; the image was copied to
`site/social-preview.png`, because Pages publishes only `site/`. `python3 scripts/check-site.py`:
"Site links valid: 4 HTML pages". The plan was closed in section 4 of `docs/dx-plan.md`.

## Next step

None within batch 3. Remaining items of the plan (Codespaces, release signing) and the
hand-offs to the Q2 session are in section 4 of `docs/dx-plan.md`.

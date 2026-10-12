# Plan for receiving external contributions

Status: **proposal** (2026-10-10). Nothing in this document is implemented; it is a plan. It does
not create any compatibility, deadline or support commitment. Origin: inventory of the repository
on the branch `m3/3b-query-authz`. Where the information was not verified, the text says "to be confirmed".

Goal: prepare MoobFS, an experimental native stacked layer for LLM agent memory,
to receive contributions from Linux, filesystem, security and
performance developers, without overstating its maturity and without claiming unproven advantages. Project
constraints that apply to everything below: all work is recorded in `docs/history/`, architectural
decisions in `docs/decisions/`, and the project is not replaced by FUSE or by a user-space
API (ADR-0001, `AGENTS.md`).

## 0. Current status

The inventory showed that **several expected files already exist**. The plan focuses on the gaps and on
aligning what exists, not on recreating it.

| Item | Exists? | Where | Gap |
| --- | --- | --- | --- |
| License | Yes | `LICENSE` (GPL v2); `SPDX GPL-2.0-only` in the sources (e.g. `layer/main.c`) | Confirm that every new file (scripts, tests) has SPDX; no automatic check |
| README | Yes, EN and PT-BR | `README.md`, `README.pt-BR.md` | No experimental-stage banner at the top; describes M1 first and the layer (M2/3b) afterwards; no "how to contribute by area" section |
| ROADMAP | Yes, 17 lines | `ROADMAP.md` | Single milestone table; does not separate "done with evidence" from "dependent on validation"; no per-item links to journal/spec |
| AGENTS/CLAUDE | Yes | `AGENTS.md`, `CLAUDE.md` (imports AGENTS) | Written for agents; a human contributor needs a summary (it is in `CONTRIBUTING.md`) |
| CHANGELOG | Yes | `CHANGELOG.md`, "Unreleased" section | No per-entry format policy |
| CONTRIBUTING | Yes, 49 lines | `CONTRIBUTING.md` | Says there is **no DCO and no CLA**; does not explicitly require a VM for changes in `layer/`, does not require KASAN, does not define double review or a squash policy; English and Portuguese accepted |
| CODE_OF_CONDUCT | Yes, 23 lines | `CODE_OF_CONDUCT.md` | Own text, **not** the Contributor Covenant (it says so); contact `claudio@studiomoob.com`; no response time |
| SECURITY | Yes, 24 lines | `SECURITY.md` | Already mentions GitHub private vulnerability reporting (enabled, according to the text; to be confirmed in the settings) and email; missing scope, response targets, CVE, embargo, credit |
| GOVERNANCE | Yes | `GOVERNANCE.md` | Single maintainer; describes protection of `main`; no plan for a second maintainer |
| Issue templates | Partial | `.github/ISSUE_TEMPLATE/bug_report.yml`, `proposal.yml`, `config.yml` | Missing a test-failure template and a review-request template; bug does not ask for kernel, lower FS or `dmesg` (field contents to be confirmed); no labels defined |
| PR template | Yes | `.github/pull_request_template.md` | No items for KASAN, UAPI/format, VM, evidence for numbers |
| CI | Yes | `.github/workflows/ci.yml` | See below |
| KASAN/lockdep | Yes, on demand and weekly | `.github/workflows/kasan.yml` | Does not run per PR; not a required check |
| Pages | Yes | `.github/workflows/pages.yml`, `site/` (index, guide, plan) | No contribution flow on the page; `plan.html` without a recorded accessibility audit |
| Makefile | Yes | `Makefile` | Targets: `all tools attr-tools test fuzz module layer vm-test`; no `help`, no `lint`, no target for a single test |
| VM harness | Yes | `vm/Dockerfile`, `in-container.sh`, `run.sh`, `init`, `suite.sh` (381 lines), `kasan.config`, `kasan-guest.sh` | No way to run an isolated test nor how to add a lower |
| Tests | Yes | `tests/` (`attr_test.c`, `layer.c`, `query.c`, `ipc.c`, `mounted.c`, `idmap.c`, `test_images.py`, `fuzz_attr.c`, `fuzz-corpus/attr`, `third_party`) | Small fuzz corpus; no measured coverage |
| Testing guide | Yes | `docs/testing.md` (138 lines) | Narrative by phase, not a reference; no expected times; no marker table |
| Security roadmap | Yes | `docs/security-roadmap.md` (S1 to S11) | Everything "to do" except the status table; S1 (ADR) not yet written |
| Journal and ADRs | Yes | `docs/history/` (0001 to 0021 + README), `docs/decisions/` (0001 to 0007) | ADRs 0001-0007 only; threat model without an ADR |
| Distribution strategy | Yes | `docs/distribution-strategy.md`, §8 (three stages) | Proposal, nothing implemented |
| Originality | Yes | `docs/research-originality.md`, §7 (phrases to avoid) | Not referenced by `CONTRIBUTING.md` |
| Value assessment | Yes | `docs/value-assessment.md` (669 lines, commit 01e76f7), §9 | Not referenced by `CONTRIBUTING.md` |
| M4 harness | Specification only | `docs/benchmark-harness.md`; `bench/` **does not exist** | Nothing implemented |

CI facts (`ci.yml`): job `Tools and sanitizers` (`make test`, ASan/UBSan with clang, `make
fuzz FUZZ_TIME=30`, `scripts/check-site.py`, 10 min) and job `Native Linux VM` (`make vm-test`, 35
min, evidence attached for 30 days). `kasan.yml`: kernel 6.12.112 with verified SHA-256,
virtme-ng 1.41, 150 min, Monday and manual.

Protection of `main` (according to `GOVERNANCE.md`; **to be confirmed** in Settings > Branches): PR
required, checks `Tools and sanitizers` and `Native Linux VM`, branch up to date, conversations
resolved, no force-push, no deletion, no minimum number of approvals, administrators with
bypass. History was rewritten only once (journal 0018), with the user's authorization. There are 57
commits across all refs; `main` and `origin/main` exist.

Maturity (facts from `README.md`, `docs/research-originality.md` §0/§7 and
`docs/value-assessment.md` §9): one tested kernel (Debian 6.12.111, KASAN on 6.12.112), three
lowers (ext4, btrfs,
XFS), index in RAM only, no measured numbers, UAPI v1 subject to change, three kernel defects
found by our own tests, one author, no external users.

## 1. Reproducible development environment

**What exists.** `make vm-test` (`vm/run.sh`) builds the Docker image (Debian trixie base pinned by digest, kernel 6.12.111+deb13 pinned), compiles modules and boots QEMU x86-64 TCG with
a 180 s timeout, without a privileged container and without KVM. KASAN/UBSAN/lockdep use the
upstream tarball `linux-6.12.112` with a verified hash and virtme-ng (`vm/kasan.config`,
`vm/kasan-guest.sh`).
The same `vm/suite.sh` runs in both environments.

**What is missing**, in order of value for a new contributor:

1. A one-command bootstrap document (`docs/development.md`) with: host requirements
   (Docker; macOS Apple Silicon uses emulation and is slow; Linux x86-64 with `/dev/kvm` can
   speed up
   KASAN, to be confirmed whether the `vm-test` target takes advantage of KVM, since the current text says TCG), disk space, memory and network for the first download.
2. Expected times measured on at least two machines (ARM Mac, x86 Linux). **To be confirmed:**
   no time is recorded apart from the CI timeout; the plan is to measure and record, not estimate.
3. How to run a single test. Today `vm/suite.sh` is sequential. Proposal: environment variable
   `MOOB_ONLY=<step>` or named functions, via a small reviewed change (see §5, item 5).
4. How to add a test: where to put the binary (`tests/*.c`), how `in-container.sh`
   compiles and copies it, where `suite.sh` calls it, which `*_PASS` marker it prints and how it fails.
5. `make help` listing targets; `make lint` (shellcheck, see §4); `make check-spdx`.
6. Optional, only with a volunteer maintainer: `.devcontainer/` reusing `vm/Dockerfile`, or
   `flake.nix`. Maintainer's decision.
7. Repeat at the top of the guide the `AGENTS.md` guideline: never load the module on the host; always a disposable
   VM.

## 2. CONTRIBUTING, CODE_OF_CONDUCT and SECURITY

All three files exist. The proposal is to **revise**, not create.

### 2.1 CONTRIBUTING.md (revision)

Keep what is there and add, in this order:
- Experimental stage and what not to expect (link to the banner, §9).
- How to get started (links to `docs/development.md`, `docs/testing.md`, the list of
  `good-first-issue` issues).
- Style: C in kernel style (tabs, 80 columns as a guide, `checkpatch.pl` once S5
  exists), explicit limits and endianness, SPDX `GPL-2.0-only` in every new file, no
  check disabled to make the build pass.
- Tests: a change in `layer/`, `include/moobfs_ioctl.h`, `common/` or the format requires a green `make
  vm-test` and a new test in the VM; a change in `layer/` also requires the KASAN workflow (`gh
  workflow
  run kasan.yml` on the branch) with a link to the run in the PR.
- **No performance numbers without an evidence file** in `build/bench/<date>/` or attached to the
  journal, with command, kernel, hardware and repetitions (M4, `docs/benchmark-plan.md`).
- Mandatory record: entry in `docs/history/` and `CHANGELOG.md`; ADR for an architectural
  decision, without editing old ADRs (point to the replacement).
- No force-push on `main`; on PR branches, rebase allowed before review and merge without
  rewriting history once there has been a review.
- Language (see §4, item 12): decision pending.
- **DCO:** today the text says there is no DCO and no CLA. Proposal: adopt DCO (`Signed-off-by`, `git
  commit -s`) from a given date, without a CLA, and check it with a GitHub Action. Rationale:
  Linux kernel practice and the GPL-2.0-only license. **Maintainer's decision**; the change is not retroactive and must respect existing contributions (`GOVERNANCE.md`).
- AI-assisted contributions: keep the current section.

### 2.2 CODE_OF_CONDUCT.md

Today it is an own text of 23 lines, which states that it is not a third-party covenant. Proposal:
adopt **Contributor Covenant 2.1** (state the version and the CC BY 4.0 attribution), because it is
recognized by external contributors, keeping the reporting contact. Points to decide: (a)
enforcement contact, today `claudio@studiomoob.com`, while the maintainer's email in the
environment is a different one; **to be confirmed** which address to publish; (b) an alternative to a single maintainer
(the current text refers the report to the GitHub platform when the target is the maintainer); (c) response
time. Record the change in the journal and `CHANGELOG.md`.

### 2.3 SECURITY.md

Proposed structure: scope; channels (GitHub Security Advisories first, email as an
alternative); what to send; response targets; embargo; CVE; credit; current limits. Details
in §7.
Keep the existing "Current boundaries" section and expand it with the threat model once S1 exists.

## 3. Issue organization

**Labels** (create via `gh label create`, list in `docs/labels.md` or in `CONTRIBUTING.md`):

| Group | Labels |
| --- | --- |
| Area | `kernel-layer`, `index-query`, `tools`, `tests-vm`, `kasan`, `docs`, `site`, `bench-m4`, `security` |
| Difficulty | `good-first-issue`, `intermediate`, `kernel-expert` |
| Priority | `p0` (data loss, crash, leak), `p1`, `p2` |
| Status | `needs-evidence`, `blocked-on-m4`, `needs-adr`, `needs-triage` |

Rules: every performance issue is born with `needs-evidence` and `bench-m4`; `blocked-on-m4` marks
decisions that `docs/distribution-strategy.md` §8 and `docs/value-assessment.md` §9 make conditional on
M4. Today the `bug` label exists (applied by `bug_report.yml`).

**Templates** (in `.github/ISSUE_TEMPLATE/`):
- `bug_report.yml` (revise): commit; kernel version (`uname -r`); host or VM; **lower
  filesystem** and mount options (`lower=`, `nolinks`, `index_budget=`); `dmesg` with
  BUG/WARNING/KASAN; minimal steps; whether it ran in the harness (`make vm-test`) or outside.
- `test_failure.yml` (new): which target, expected and obtained marker, excerpt of
  `build/vm/console.log`, whether it reproduces on a second run, host and Docker.
- `proposal.yml` (exists): add a field "Related ADR" (link or "new ADR needed") and
  "impact on UAPI or format".
- `review_request.yml` (new, for the requests in §8, maintainer use).
- Security: **no public template**. `config.yml` already points to `mailto:`; replace/extend it with the
  Security Advisories link (`/security/advisories/new`) and leave `blank_issues_enabled:
  false`.

## 4. Initial contributions without a kernel

Concrete candidates, all verifiable on the host or in ordinary CI. Each becomes an issue with an
area label, a done-when criterion and the files touched. None requires loading a module.

1. **Gaps in `docs/testing.md`**: table of `*_PASS` markers, times, how to read
   `build/vm/*.log` (§5). `docs`, `good-first-issue`.
2. **`moobfs-attr` and `moobfs-check`**: documented error messages and exit codes,
   consistent `--help`. Files `tools/moobfs-attr.c`, `tools/moobfs-check.c`. `tools`.
3. Optional **JSON output** in `moobfs-check` and `moobfs-index` (today `fsck.moobfs` already emits
   JSON), with a documented schema and a test in `tests/`. `tools`, `intermediate`.
4. **Manual pages** (`man/moobfs-attr.1`, `moobfs-check.1`, `moobfs-index.1`,
   `mkfs.moobfs.8`), checked with `mandoc -Tlint`. `docs`, `good-first-issue`.
5. **Fuzz corpus** for `tests/fuzz_attr.c`: today `tests/fuzz-corpus/attr` has few
   files; generate seeds per attribute type and limits (length 0, maximum, invalid
   UTF-8),
   minimize with `-merge=1`. `tests-vm`/`tools`, `intermediate`.
6. **Pieces of the `bench/` harness** per `docs/benchmark-harness.md`: `bench/gen` (deterministic
   dataset from a seed) and `bench/report` (p50/p95/p99, CSV/JSON) in C or Python;
   both
   run on the host without the module. `bench-m4`, `intermediate`. **No published numbers** until there is a VM run with evidence.
7. **`bench/equiv`** (compare `QUERY`, SQLite and the oracle): against the oracle, which already exists;
   part depends on the VM. `bench-m4`, `intermediate`.
8. **`site/plan.html` and other pages**: accessibility audit (contrast, focus, `lang`,
   landmarks, alt text for the diagram), extended `scripts/check-site.py`. `site`,
   `good-first-issue`.
9. **`moobfs-index` (oracle)**: error messages, handling of large trees, tests of
   boundary predicates. It is the correctness reference for `QUERY`; any change requires comparison
   with
   the kernel in the VM (hence `intermediate`).
10. **Compiler warning matrix in CI**: gcc and clang, different versions, `-Wall -Wextra
    -Wconversion -Wshadow -Werror` already used in the `Makefile` for tools. `tools`,
    `good-first-issue`.
11. **`shellcheck`** on `vm/*.sh`, `vm/init`, `scripts/`: fix or suppress with a
    justification; new step in the `Tools and sanitizers` job. `tests-vm`, `good-first-issue`.
12. **Reproducibility of the kernel hashes**: a script that checks the Debian image digest, the
    6.12.111 package and the SHA-256 of `linux-6.12.112` (today in `kasan.yml`); document how to
    update them together (as `docs/testing.md` already requires). `docs`/`tests-vm`.
13. **SPDX check** (`scripts/check-spdx.py`) on `.c/.h/.py/.sh` sources. `tools`,
    `good-first-issue`.
14. **Translation and language policy.** The README has EN and PT-BR and the site is in English, but
    `docs/` (specs, ADRs, journal, security roadmap) is in Portuguese, and the target audience is
    international. Propose: (a) an "At a glance" section in English in the README, with stage, limits and how to validate; (b) policy: code, commits, issues and PRs in English or Portuguese; new normative
    documents (ADRs, specs) in English or bilingual from a given date; the journal stays in the author's language; (c) translations of `docs/testing.md`, `security-roadmap.md` and `spec-m3.md`
    first, marked "courtesy translation, the original prevails". **Maintainer's decision.**

## 5. Documenting how to run the VM tests

Restructure `docs/testing.md` (preserving the current content, moved) into:

1. **Prerequisites**: Docker, platform, space, network; warning never to use `insmod` on the host.
2. **Makefile targets**: table `make test` / `make fuzz` / `make attr-tools` / `make module` /
   `make layer` / `make vm-test`, what each compiles and runs, where it writes (`build/`), observed
   time (to be measured).
3. **Markers and meaning**: `MOOBFS_VM_PASS`, `SHARED:`, `MOUNTED_READ_TESTS_PASS`,
   `WRITABLE_MOUNT_REJECTED`, `LAYER_LOWER_PASS: <fs>`, `ISOLATION_PASS: <fs>` (`suite.sh`),
   `ACCEPTANCE_CLIENTS_PASS`, `POWER_LOSS_REPLAY_PASS`, `IO_ERROR_INJECTION_PASS`, `MOOBFS_ACCEPTANCE_PASS`, `MOOBFS_LAYER_PASS`, `MOOBFS_KASAN_PASS`; and `MOOBFS_VM_FAIL:`. For each one: what
   it proves and what it does **not** prove.
4. **Reading the logs**: `build/vm/console.log`, `environment.txt`, `kernel.config`,
   `image-report.json`; under KASAN, `build/vng/dmesg.txt` and the pattern that fails the run (`BUG: KASAN`,
   `WARNING:`,
   lockdep, UBSAN). How to distinguish an **expected** rejection (malformed image, write on M1) from a failure.
5. **Running an isolated test**: see §1, item 3.
6. **KASAN locally**: replicate the steps of `kasan.yml` with `vng` (virtme-ng 1.41), command
   by command, with expected space and time, and how to download the workflow artifact instead of
   running locally.
7. **Adding a lower** (for example F2FS): `vm/Dockerfile` (packages), `in-container.sh`
   (modules and image), `suite.sh` (`run_lower <fs> <dev> <reflink>`), `kasan.config`,
   `kasan-guest.sh` (extra disk) and `kasan.yml`; warn that each new lower requires a record and does not widen the scope without an ADR.
8. **What is not covered**: full xfstests, syzkaller, other kernels (S2, S3, §0).

## 6. PR review and acceptance criteria

Checklist to attach to the PR template and to `CONTRIBUTING.md`:

- [ ] The scope corresponds to an accepted ADR or a discussed issue; format or UAPI changes without a prior
  issue are not accepted.
- [ ] Tests added in the VM (`vm/suite.sh` or `tests/`) for behavior changes;
  invalid inputs included when there is a parser or ioctl.
- [ ] Green CI, including `Tools and sanitizers` and `Native Linux VM`.
- [ ] For `layer/`: a run of the KASAN workflow on the branch, with a link, without `BUG`, `WARNING`,
  lockdep or UBSAN.
- [ ] Entry in `docs/history/` (goal, decisions, tests actually run, limitations,
  next step) and a line in `CHANGELOG.md`.
- [ ] No claim without evidence: nothing about performance, security, "tested on Linux" or
  "production ready"; check `docs/research-originality.md` §7.
- [ ] UAPI change (`include/moobfs_ioctl.h`): specification, version increment, invalid-input
  tests and an explicit compatibility decision.
- [ ] Format change (M1 in `docs/format-m1.md` or attributes): compatibility decision (ADR).
- [ ] SPDX; no credentials, no real memories; DCO sign-off (if adopted).
- [ ] Statement of AI assistance and what was reviewed by a human.

Review policy:
- **Two reviews** for `layer/`, `include/` and `common/` while there is a single maintainer? That
  is impossible today (one maintainer and administrator with bypass). Honest proposal: require one
  maintainer approval plus **one recorded external review** (comment on the PR, with a name) for changes in `layer/`; enable the two-approval rule when a second
  maintainer exists. Decision pending.
- **Squash**: squash merge with a final message written by the maintainer, to keep `main`
  readable; intermediate commits remain in the PR. Alternative: merge with history when the
  series
  is bisectable. To be defined.
- **Commit message**: short imperative subject with an area prefix (`layer:`, `tests:`,
  `docs:`, `tools:`, `vm:`, `ci:`), blank line, the why, `Signed-off-by` if DCO,
  reference to
  issue/ADR/journal.
- Reverting is preferable to rewriting; no force-push on `main` (only historical exception: 0018).

## 7. Responsible vulnerability disclosure

Proposed content of `SECURITY.md`:

- **Channel:** GitHub Security Advisories (`security/advisories/new`) as the primary one; email as an
  alternative (address **to be confirmed**); never a public issue.
- **In scope:** module `layer/moobfs_layer.ko` and `kernel/moobfs.ko`, UAPI (`QUERY`,
  `GET_INFO`), `common/` (attribute parser), tools in `tools/`, harness scripts in
  `vm/`
  when the defect affects whoever runs the harness (for example, writing outside `build/`).
- **Out of scope:** defects in the lower filesystems (ext4, btrfs, XFS), in the Debian
  kernel or in QEMU unrelated to the layer; report them to the respective projects. Attacks that
  require
  `CAP_SYS_ADMIN` to mount are not escalation (mounting already requires it; S1).
- **Count as a vulnerability:** crash, hang or corruption triggerable by an unprivileged
  user (denial of service included, per `security-roadmap.md`), leak between
  users via `QUERY`, out-of-bounds write, use after free, silent data loss.
- **Response targets** (targets, not guarantees, given one maintainer): acknowledgment within 7 days,
  triage within 14, fix or plan within 90, coordinated disclosure at the end, or earlier if the reporter
  agrees. **Values to be confirmed** by the maintainer; the current text says it promises no deadline.
- **CVE:** request via GitHub (GitHub's CNA) when there is impact on those who run the module; no
  expectation of a CVE for test-only code. For findings in the kernel itself, contact the kernel
  security list per `Documentation/process/security-bugs`.
- **Embargo:** private until the fix is on `main`; reporter and maintainer agree on the date.
- **Credit:** in the advisory, the journal and `CHANGELOG.md`, with consent.
- **No financial reward (bug bounty).**
- Notice: this is not a supported release; fixes only on `main`.

## 8. Encouraging independent reviews

Open "review request" issues (label `security` or `kernel-expert`), each with a
package for the reviewer and a bounded question. None suggests that the layer has been audited.

| Request | Question | Artifacts |
| --- | --- | --- |
| Threat model (S1) | Does the model cover the right adversary? `FS_USERNS_MOUNT` off, `override_creds`, `nop_mnt_idmap`, authorization in `QUERY` | `docs/security-roadmap.md`, journal 0020, `layer/main.c`, `layer/index.c`, `tests/idmap.c` |
| Concurrency | Index `rwsem`, `QUERY` chunking, `rename` and `paths_stale`, race with chmod/rename/unlink (commit `c933a74`) | `layer/index.c`, `tests/query.c`, KASAN logs from the workflow |
| Integrity | `dm-log-writes` and `dm-flakey` scenarios: what is missing (barriers, rename, directory fsync) | `vm/suite.sh`, journal 0015 |
| UAPI | Struct, flags, reserved fields, `copy_to_user`, `open_by_handle_at` returned by `QUERY` (S11) | `include/moobfs_ioctl.h`, `docs/spec-m3.md` |
| Attribute parser | Limits, UTF-8, name alphabet | `common/attr.c`, `tests/fuzz_attr.c` |

The reviewer receives: the specification section, the command to reproduce, the `make
vm-test` logs, the KASAN workflow artifact for the exact revision (commit) and the list of known
limitations
(TOCTOU, false negative after a directory rename, according to `research-originality.md` §7).
Recording findings: issue (or advisory, if it is a vulnerability) plus a journal entry; if the
finding
changes the design, a new ADR that points to the previous one; credit to the reviewer. Do not record
"reviewed" as approval: a comment does not prove security.

## 9. Experimental stage and limitations

Proposed text for the top of `README.md` (and equivalent in PT-BR and in `site/index.html`):

> **Experimental research project. Not for production use.** MoobFS is a native Linux stacked
> filesystem layer (kernel module over ext4, btrfs or XFS) for storing and querying LLM-agent memory
> as typed extended attributes. Current limitations, all verified in the repository:
> - tested on **one kernel** (Debian 6.12.111; a KASAN/lockdep kernel 6.12.112 on demand) in a VM;
> - the attribute index lives **only in RAM** and is rebuilt at mount;
> - mounting requires **root** in the initial namespace; user namespaces and idmapped mounts are rejected;
> - **no performance numbers have been measured**; no speed or efficiency claim is made;
> - the **UAPI v1 may change**; there is no stable release or compatibility promise;
> - our own tests found **three kernel bugs** during development; there has been **no external audit**;
> - reflink, deduplication and durability come from the lower filesystem, not from MoobFS.

The `site/index.html` page shows the same block above the fold. Check each line against
`README.md`, `docs/research-originality.md` §0 and §7 and `docs/value-assessment.md` §9 before
publishing; "three defects" comes from §9.1 and is **to be confirmed** against journals 0011 to 0015.

## 10. Public roadmap

Restructure `ROADMAP.md` into three states, each item with a link to a journal or specification:

- **Done with evidence:** M0 (0001-0003); M1 read-only (0002, `docs/format-m1.md`);
  M2 phases 2a-2e (0011-0015, `docs/spec-m2.md`); KASAN harness (0016); partial phase 3b (0020:
  authorization in `QUERY`, idmap and userns rejected, race with chmod/rename/unlink). Each line cites what **was** tested and what was not.
- **Planned:** rest of M3 (`docs/spec-m3.md`); threat ADR S1; S5 lint; S6
  (revised `SECURITY.md`); fstests (S2); S4 (hostile lower). No dates.
- **Dependent on validation (M4 or security):** `QUERY_BATCH`, `READ_BATCH` and index
  persistence (conditional on M4); benchmark (M4, `docs/benchmark-plan.md` and
  `benchmark-harness.md`); DKMS, `.deb` and public stage 3 (`distribution-strategy.md` §8); syzkaller (S3); continuous fuzzing (S7); hardening (S8); limits (S9); signing (S10).

Phase map for security items: M2 closes with partial S-items from the status table; M3
depends on S1 and S11; M4 does not depend on S-items, but the public version depends on S1-S6 and S8-S10.
Each item gets a label "proposal", "implemented", "tested (kernel, lower)", as
`AGENTS.md` requires.

## 11. Prioritized sequence of actions

Effort: S (hours), M (1 to 3 days), L (a week or more). "Decision" marks what depends on the maintainer.

| # | Action | Effort | Depends on | Decision |
| --- | --- | --- | --- | --- |
| 1 | Review and commit this plan, with journal and `CHANGELOG.md` | S | maintainer review | no |
| 2 | Confirm contacts (conduct and security) and whether Security Advisories is active | S | 0 | **yes** (email) |
| 3 | Confirm the license: `LICENSE` GPL v2, SPDX `GPL-2.0-only`; evaluating "or later" out of scope | S | 0 | **yes** (confirm) |
| 4 | Experimental-stage banner in the README, PT-BR and site (§9) | S | 1 | no |
| 5 | Create labels and `docs/labels.md` | S | 0 | no |
| 6 | Revise `SECURITY.md` (§7), response targets | S | 2 | **yes** (deadlines) |
| 7 | Decide DCO vs no DCO; update `CONTRIBUTING.md` and the PR template | S | 0 | **yes** |
| 8 | Decide on Contributor Covenant 2.1 and the enforcement contact | S | 2 | **yes** |
| 9 | Language policy and an "At a glance" section in English | M | 0 | **yes** |
| 10 | New issue templates (§3) | S | 5 | no |
| 11 | `docs/development.md` and restructure `docs/testing.md` (§1, §5); measure times | M | 0 | no |
| 12 | `ROADMAP.md` in three states (§10) | S | 1 | no |
| 13 | Open 10 to 15 initial issues (§4) with labels | M | 5, 10 | no |
| 14 | `shellcheck` and `check-spdx` in CI | S | 13 | no |
| 15 | Open review requests (§8) after ADR S1 | M | S1 | no |
| 16 | Threat model ADR (S1) | M | 0020 | **yes** (ADR) |
| 17 | `MOOB_ONLY` or isolated execution in `vm/suite.sh` (changes code; requires VM and KASAN) | M | 11 | no |
| 18 | KASAN as a required check or per `layer/` PR | M | CI cost (150 min) | **yes** |

Actions 1 to 14 do not touch kernel code. Actions 17 and 18 touch the harness and CI and follow the rules of §6.

## 12. Files to create or modify

This document creates only `docs/community-plan.md`. If the plan is approved, the files below
would be changed in separate PRs, each with a journal entry and `CHANGELOG.md`.

| File | Action |
| --- | --- |
| `README.md`, `README.pt-BR.md`, `site/index.html` | experimental banner; "At a glance" section in English |
| `ROADMAP.md` | three states with links |
| `CONTRIBUTING.md`, `.github/pull_request_template.md` | checklist from §6, DCO (if approved), KASAN, evidence |
| `CODE_OF_CONDUCT.md` | Contributor Covenant 2.1 (if approved) |
| `SECURITY.md` | scope, targets, CVE, embargo, credit |
| `.github/ISSUE_TEMPLATE/bug_report.yml`, `proposal.yml`, `config.yml` | kernel/lower/dmesg fields; ADR; advisory link |
| `.github/ISSUE_TEMPLATE/test_failure.yml`, `review_request.yml` | new |
| `.github/CODEOWNERS` | new (when there is more than one maintainer) |
| `.github/workflows/ci.yml` | `shellcheck`, `check-spdx`, warning matrix, DCO (if approved) |
| `docs/development.md` (new), `docs/testing.md` | bootstrap, structure from §5 |
| `docs/labels.md` (new) | labels and usage |
| `docs/decisions/0008-*.md` (new, ID to be confirmed) | threat model (S1) |
| `docs/history/NNNN-*.md`, `docs/history/README.md`, `CHANGELOG.md` | records of each step |
| `vm/suite.sh`, `Makefile` | isolated execution, `help`, `lint` (code; requires VM) |
| `scripts/check-spdx.py` (new), `man/*` (new) | initial tasks |
| `.devcontainer/` or `flake.nix` | optional, to be decided |

Limitations: the contents of the template fields, the actual GitHub configuration (labels, protection
of `main`, advisories, Pages) and execution times were not verified; all are marked
"to be confirmed".

## 13. Maintainer decisions (2026-10-10)

| Topic | Decision |
| --- | --- |
| Security and conduct contact | `claudio@studiomoob.com`, plus the private GitHub Security Advisories channel |
| DCO | Adopted: `Signed-off-by` required on every commit, checked in CI |
| Code of conduct | Contributor Covenant 2.1, with the source cited |
| Language | English in code, commits, issues, PRs and README; documentation in `docs/` accepted in Portuguese or English; ADRs and specifications translated on external demand |
| Deadlines in `SECURITY.md` | Acknowledgment within 7 days, assessment within 30 days, update to the reporter every 30 days; fix with no fixed deadline |
| KASAN | Required on pull requests that change `layer/` or `vm/`; weekly and on demand for the rest |
| License | Confirmed GPL-2.0-only |
| Review of `layer/` | One maintainer plus one recorded external review, until there is a second maintainer |
| Execution | Actions 1 to 14 approved; 15 to 18 after the merge of phase 3b |

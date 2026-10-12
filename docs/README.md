# Documentation index

Every document under `docs/`, with its language and one line on what it covers. **EN** means
English, **PT** means Portuguese (pt-BR), and **PT + EN abstract** means Portuguese with an
English summary at the top. By the language policy in [CONTRIBUTING.md](../CONTRIBUTING.md),
code, commits, issues and the README are in English, while documents here may be in either
language. Translations of the entry documents that exist only in Portuguese are open as
`good first issue` tasks with the `area/docs` label.

Status words matter: a **proposal** describes intended behaviour, a **specification** is
accepted but may be only partly implemented, and only the journals and
[testing.md](testing.md) record what actually ran.

## Start here

| Document | Lang | What it covers |
| --- | --- | --- |
| [architecture-overview.md](architecture-overview.md) | EN | How the stacked layer is built, with a source citation for each claim |
| [when-not-to-use.md](when-not-to-use.md) | EN | Comparison with plain files, SQLite, FUSE and memory frameworks; when not to use MoobFS |
| [faq.md](faq.md) | EN | Short sourced answers to common questions |
| [development.md](development.md) | EN | Environment setup, `make` targets, devcontainer, lint and checkpatch |
| [testing.md](testing.md) | EN | What each test runs, its success markers, and what they do not prove |
| [labels.md](labels.md) | EN | Issue and pull request labels, including `good first issue` and `help wanted` |

## Specifications and formats

| Document | Lang | What it covers |
| --- | --- | --- |
| [spec-m2.md](spec-m2.md) | PT | M2: the memory layer stacked over a mature filesystem (pass-through, typed attributes, index, `QUERY`) |
| [spec-m3.md](spec-m3.md) | PT | M3: identity, isolation and provenance, change cursor, batch reads |
| [spec-query-v2.md](spec-query-v2.md) | PT | Domain-scoped `QUERY2` (UAPI v2): domains, homogeneity rule, ioctl contract, phases Q2a–Q2e |
| [format-m1.md](format-m1.md) | EN | The read-only M1 image format, kept as a regression |
| [kernel-infrastructure.md](kernel-infrastructure.md) | PT | Mature kernel infrastructure the layer reuses, checked against Linux v6.12 |
| [native-mvp.md](native-mvp.md) | PT | The original full MVP proposal; its write interface was superseded by ADR-0007 |
| [spec-m2-full-volume-superseded.md](spec-m2-full-volume-superseded.md) | PT | Superseded M2 proposal for a full writable volume, kept for the record |

## Decisions and history

| Document | Lang | What it covers |
| --- | --- | --- |
| [decisions/](decisions/) | PT (ADR-0004 in EN) | Architecture decision records ADR-0001 to ADR-0009, with alternatives and reasons |
| [history/](history/README.md) | PT | Technical journal: one entry per piece of work, with evidence and limitations |

Key decisions for newcomers: [ADR-0007](decisions/0007-stacked-memory-layer.md) (a stacked layer
over a mature filesystem) and [ADR-0009](decisions/0009-query-role-and-user-space-index.md)
(the in-kernel index partitioned by permission domain).

## Benchmarks (M4)

| Document | Lang | What it covers |
| --- | --- | --- |
| [benchmark-plan.md](benchmark-plan.md) | PT | M4 plan: questions, baselines, workloads, what a result must show |
| [benchmark-harness.md](benchmark-harness.md) | PT | Harness specification: dataset, SQLite baselines, latency records, report, where raw archives go |
| [bench-accelerated-guest.md](bench-accelerated-guest.md) | PT | How to run a campaign in an accelerated guest or a cloud VM |
| [../bench/results/](../bench/results/README.md) | EN | Pilot campaign results and the `bench-evidence` release for raw archives |

## Assessments and research

| Document | Lang | What it covers |
| --- | --- | --- |
| [value-assessment.md](value-assessment.md) | PT | Critical assessment of usefulness and value; the English summary is when-not-to-use.md |
| [research-originality.md](research-originality.md) | PT | Prior-art audit (SFS, BFS, YoloFS and others) and what may be claimed |
| [research-metadata-search.md](research-metadata-search.md) | PT | State of the art in file-metadata search, authorization and change feeds (for ADR-0009) |
| [research-metadata-search-addendum.md](research-metadata-search-addendum.md) | PT | Addendum checking the items left unverified in the previous document |
| [research-academic-survey.md](research-academic-survey.md) | PT + EN abstract | Systematic literature survey behind ADR-0009 |
| [research-client-query-patterns.md](research-client-query-patterns.md) | PT + EN abstract | Structured queries in six LLM-agent memory systems, read from their source |
| [research-ai-memory.md](research-ai-memory.md) | PT | Technical reading of ai-memory and how it relates to MoobFS |
| [ecosystem.md](ecosystem.md) | PT | Free software relevant to the project, checked before building anything |
| [project-review-2026-10-10.md](project-review-2026-10-10.md) | PT | Implementation and roadmap review, with static `QUERY` findings |

## Plans and process

| Document | Lang | What it covers |
| --- | --- | --- |
| [security-roadmap.md](security-roadmap.md) | PT | Security and robustness items S1–S11 (threat model, syzkaller, fuzzing, hardening) |
| [distribution-strategy.md](distribution-strategy.md) | PT | Proposed deployment model, DKMS packaging, kernel matrix and release stages |
| [community-plan.md](community-plan.md) | PT | Plan for external contributions (policies, templates, CI); largely executed |
| [seo-geo-plan.md](seo-geo-plan.md) | PT | Discoverability audit and plan for the README, site and docs |
| [dx-plan.md](dx-plan.md) | PT | Contributor-experience plan for GitHub, in three batches |
| [ai-workflow.md](ai-workflow.md) | PT | How AI-assisted development is governed (`AGENTS.md`, `CLAUDE.md`, Cursor rules) |
| [ai-workflow.en.md](ai-workflow.en.md) | EN | English translation of ai-workflow.md (AI-assisted development governance) |

## Assets

| Path | What it is |
| --- | --- |
| [assets/social-preview.svg](assets/social-preview.svg), [assets/social-preview.png](assets/social-preview.png) | GitHub social preview (1280×640); render with `assets/render-social-preview.sh` |

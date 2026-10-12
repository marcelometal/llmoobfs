# Project memory

This directory holds a versioned technical journal. Git preserves the exact changes to files; ADRs
explain decisions, and the journal connects requests, changes and results.

## Where to find each kind of information

- `docs/decisions/`: architecture decisions, alternatives and summarized reasons.
- `docs/history/`: chronological work records and validation evidence.
- `CHANGELOG.md`: changes that matter to users and contributors.
- `docs/native-mvp.md`: the original proposal for the full MVP.
- `AGENTS.md`: continuity instructions for later sessions.

A journal entry must contain the date, goal, decisions/ADRs, related files or commits, tests actually
run, limitations and next step. Use new entries or dated addenda for later changes. Entries of the
session in progress may be completed before the commit.

The early entries reconstruct the conversation in summarized form; they are not a literal or
exhaustive transcript. The decision history records what was decided and its technical reasons,
without claiming access to private internal reasoning.

Preservation depends on commits and copies: no disk or service guarantees retention forever. Keeping
the remote repository and an independent copy makes it possible to recover the history. Large
outputs, test images and binaries live in `build/`; version the scripts that reproduce them and
verifiable summaries of the results.

Journals 0001 to 0050 were written in Portuguese and translated to English in place on 2026-10-12
(ADR-0010); the Portuguese originals remain in the git history.

## Index

- [0001 — Origin and definition of the project](0001-origin.md)
- [0002 — First native milestone](0002-native-read-milestone.md)
- [0003 — Open source base and GitHub](0003-open-source-github.md)
- [0004 — LLM memory and context as an explicit goal](0004-llm-memory-context.md)
- [0005 — Memory model and the Be File System as reference](0005-bfs-reference-memory-model.md)
- [0006 — Publication of ADR-0005 and survey of the free software ecosystem](0006-ecosystem-survey.md)
- [0007 — Technical reading of ai-memory and relative position](0007-ai-memory-reading.md)
- [0008 — Reusable kernel infrastructure and M2 specification with POSIX writes](0008-m2-spec-kernel-infra.md)
- [0009 — What MoobFS would bring that is different, and the principle of not competing with what is mature](0009-differentiation-and-principle.md)
- [0010 — ADR-0007: stacked layer over a mature filesystem; M2 rewritten](0010-adr-0007-stacked-layer.md)
- [0011 — Phase 2a: minimal stacked layer over ext4, validated in a VM](0011-phase-2a-stacked-passthrough.md)
- [0012 — Phase 2b: full pass-through over ext4, btrfs and XFS, with delegated reflink](0012-phase-2b-full-passthrough.md)
- [0013 — Phase 2c: typed attributes validated in the kernel, tools and fuzzing](0013-phase-2c-typed-attributes.md)
- [0014 — Phase 2d: in-memory index and query by attribute (`QUERY`)](0014-phase-2d-index-query.md)
- [0015 — Phase 2e: acceptance with real clients and investigation of lost rows in SQLite](0015-phase-2e-investigation.md)
- [0016 — Harness with KASAN and lockdep on our own kernel; data flow diagram on the site](0016-kasan-lockdep-harness.md)
- [0017 — Use on macOS and security posture compared to the market](0017-macos-and-security-posture.md)
- [0018 — Rewrite of the Git history: removal of questions and personal data](0018-history-rewrite.md)
- [0019 — Decisions arising from an external review of the architecture](0019-external-review-decisions.md)
- [0020 — Phase 3b: POSIX authorization in `QUERY` (in progress)](0020-phase-3b-query-authorization.md)
- [0021 — Prior-art audit and distribution strategy](0021-originality-and-distribution.md)
- [0022 — Usefulness assessment, value proposition and maturity as an adoption criterion](0022-value-assessment.md)
- [0023 — Review of implementation, plans and next experiment](0023-project-review.md)
- [0024 — Preparation for external contributions: policies, templates, documentation and CI](0024-community-readiness.md)
- [0025 — M4: first slice of the benchmark harness](0025-m4-harness-first-slice.md)
- [0026 — First pre-release tag: v0.1.0-experimental](0026-first-prerelease-tag.md)
- [0027 — `QUERY`: seek from the first candidate and results copied one by one](0027-query-fast-path.md)
- [0028 — `QUERY` profile and dentry revalidation compatible with RCU-walk](0028-rcu-walk-revalidation.md)
- [0029 — `QUERY` scale series (1000 to 60 000 memories)](0029-query-scale-series.md)
- [0030 — Verifications: change stream, Landlock and `QUERY` timing channel](0030-verification-notify-landlock-timing.md)
- [0031 — Remount time with 25 000 to 60 000 memories](0031-remount-time.md)
- [0032 — Developer experience on GitHub: plan and batch 1](0032-dx-batch1.md)
- [0033 — Research for the decision on the role of `QUERY` (ADR-0009)](0033-query-design-research.md)
- [0034 — ADR-0009 accepted: per-domain `QUERY` in the kernel (alternative E)](0034-adr-0009-accepted.md)
- [0035 — Phase Q2a: per-domain `QUERY` (UAPI v2), partitions and homogeneity rule](0035-phase-q2a-domain-query.md)
- [0036 — Phase Q2b: ordering and cursor in `QUERY2`](0036-phase-q2b-order-cursor.md)
- [0037 — Phase Q2c: intersection by leapfrog in `QUERY2`](0037-phase-q2c-leapfrog.md)
- [0038 — Phase Q2d: "missing or ..." terms and batch in `QUERY2`](0038-phase-q2d-missing-batch.md)
- [0039 — Developer experience on GitHub: batch 2 (tooling)](0039-dx-batch2.md)
- [0041 — Developer experience on GitHub: batch 3 (showcase)](0041-dx-batch3.md)
- [0042 — New measurement host: Mac mini Server 2011 with Debian 13](0042-bench-host-macmini.md)
- [0043 — Timing channel: per-domain `QUERY2` against v1 (test 4 of the specification)](0043-timing-channel-query2.md)
- [0044 — `QUERY2` measured against v1 and revision of the Q2c equality plan](0044-q2-measured-equality-plan.md)
- [0045 — Triage of the 30 open CodeQL alerts](0045-codeql-triage.md)
- [0046 — KASAN workflow Python dependencies pinned by hash](0046-kasan-pip-hashes.md)
- [0047 — `QUERY2` against SQLite on the same hardware](0047-sqlite-baseline-macmini.md)
- [0048 — Optimization of "N most recent" in `QUERY2`](0048-recent-optimization.md)
- [0049 — `QUERY2`, v1 and SQLite on btrfs and XFS](0049-q2-btrfs-xfs.md)
- [0050 — Pending code items received from the developer experience batch](0050-dx-code-items.md)
- [0051 — The repository moves to English (ADR-0010)](0051-english-everywhere.md)
- [0053 — Verified references](0053-references.md)

# Roadmap

A sequence of research milestones, not a delivery schedule. Items are grouped by
their state of evidence. "Done, with evidence" means a journal entry records the
tests that actually ran, on which kernel and lower filesystem; it never means
production readiness. Nothing here is a compatibility commitment.

## Done, with evidence

| Item | What was tested | What was not | Evidence |
| --- | --- | --- | --- |
| M0 — Project memory | Versioned journal, ADRs, shared AI instructions, Git history, GitHub replica | — | [0001](docs/history/0001-origin.md), [0002](docs/history/0002-native-read-milestone.md), [0003](docs/history/0003-open-source-github.md) |
| M1 — Native shared reads (read-only image) | Image creation, mount in QEMU, distinct inodes sharing physical bytes (FIEMAP), reads, read-only enforcement, 15 malformed images, remount; kept as a regression in every `make vm-test` | Writes, other kernels, KASAN on the Debian kernel | [0002](docs/history/0002-native-read-milestone.md), [format](docs/format-m1.md), [ADR-0003](docs/decisions/0003-readonly-milestone.md) |
| M1 hardening | Frozen as a completed experiment by [ADR-0007](docs/decisions/0007-stacked-memory-layer.md) | Its original exit criteria (fuzzing of the image parser, lifecycle tests) were not completed before freezing | [0010](docs/history/0010-adr-0007-stacked-layer.md) |
| M2 phase 2a — Minimal stacked pass-through | `moobfs_layer.ko` over ext4 in the VM; pass-through acceptance test; 23,000 `fsx` operations | btrfs, XFS | [0011](docs/history/0011-phase-2a-stacked-passthrough.md), [spec](docs/spec-m2.md) |
| M2 phase 2b — Full pass-through | Rename, links, symlinks, `nolinks`, opaque xattr, delegated reflink/dedupe/`copy_file_range`, FIEMAP over ext4, btrfs and XFS | xfstests | [0012](docs/history/0012-phase-2b-full-passthrough.md) |
| M2 phase 2c — Typed attributes | `user.moob.*` validated in the kernel (shared `common/attr.c`), `moobfs-attr`, `moobfs-check`, host unit tests, libFuzzer target | Fuzzing beyond 30 s per CI run | [0013](docs/history/0013-phase-2c-typed-attributes.md) |
| M2 phase 2d — In-memory index and `QUERY` | Index built at mount and kept current; `QUERY`/`GET_INFO` (UAPI v1); kernel answers identical to the `moobfs-index` oracle over the three lower filesystems, after rename, unlink, removexattr and remount; `noindex`, `index_budget` | Budget exhaustion on large trees; persistence | [0014](docs/history/0014-phase-2d-index-query.md) |
| M2 phase 2e — Acceptance with real clients | git, SQLite WAL with concurrent reader (six variants), own memory client, power-loss replay with dm-log-writes, I/O errors with dm-flakey; two layer bugs found and fixed (stale lower dentry losing WAL frames; `set_nlink` race) | Acceptance over btrfs and XFS; full xfstests | [0015](docs/history/0015-phase-2e-investigation.md) |
| KASAN/UBSAN/lockdep harness | Upstream 6.12.112 built with `vm/kasan.config` under virtme-ng, same `vm/suite.sh`; weekly and on demand; a third defect found (uninitialized `rwsem`/xarray on early mount failure) | KMSAN, KCSAN, KFENCE | [0016](docs/history/0016-kasan-lockdep-harness.md) |
| M3 phase 3b — POSIX authorization in `QUERY` | Per-candidate authorization with the caller's credentials; two users, two namespaces, counts and chunking exact; attribute changes never grant; chmod and POSIX ACLs honoured immediately and across remount; idmapped mounts refused (`EINVAL`), user-namespace mounts refused (`EPERM`); 5-second race of chmod/rename/unlink against `QUERY`; passed in the VM and under KASAN on 6.12.112 | Timing channels; M3 phase 3a identity convention (only the `moobfs-check` duplicate detection exists); phase 3c | [0020](docs/history/0020-phase-3b-query-authorization.md), [spec-m3](docs/spec-m3.md) §3 |
| M3 timing and change-stream verifications | Change stream (fanotify and inotify) and Landlock checked against the layer; the v1 `QUERY` timing channel measured (about 1 µs per hidden entry) | — | [0030](docs/history/0030-verification-notify-landlock-timing.md) |
| ADR-0009 — Per-domain `QUERY2` (phases Q2a to Q2e) | UAPI v2 issued on a domain directory, partitions by domain with the homogeneity rule, ordering and authenticated cursor, equality plan, "missing or" terms, `QUERY2_BATCH`, `EXDEV` for directories renamed across domains; answers equal to the oracle on ext4, btrfs and XFS; a 5-second race per lower filesystem of mode changes, renames, unlink/recreate and cross-domain moves against paged `QUERY2`; passed in the VM and under KASAN | Timing channels other than the one of 0043; persistence | [0034](docs/history/0034-adr-0009-accepted.md) to [0038](docs/history/0038-phase-q2d-missing-batch.md), [0052](docs/history/0052-phase-q2e-concurrency.md), [spec](docs/spec-query-v2.md) |
| M4 — Harness and first campaigns | `bench/` (dataset generator, loader, SQLite client and baselines, equivalence check against the oracle, report); Azure pilots, then a dedicated Mac mini host: `QUERY2` against v1 (24 to 30 times faster on `namespace AND agent`) and against SQLite with composite indexes (faster on the five synthetic shapes measured, on ext4, btrfs and XFS; slower on a real client's long ordered traversal, see the [M4 report](docs/history/0088-m4-report.md)); timing channel closed (10 to 23 µs while another domain grows); cold cache, mount time at 60 000 files, write cost of domains | The M4 report itself (below); POSIX latency per operation and index memory; reflink space savings; other hosts | [0025](docs/history/0025-m4-harness-first-slice.md), [0029](docs/history/0029-query-scale-series.md), [0031](docs/history/0031-remount-time.md), [0042](docs/history/0042-bench-host-macmini.md) to [0049](docs/history/0049-q2-btrfs-xfs.md), [0055](docs/history/0055-measurement-gaps.md) |
| First real-client test, stage 1 (ai-memory, unchanged) | Its own 1 147 tests pass inside the layer as on ext4, after a layer bug they exposed was fixed (rename over a just-deleted name); LongMemEval-S retrieval, 50 questions, identical results per question | Repeated runs and the 500 questions; `QUERY2` with its data (stage 2) | [0054](docs/history/0054-real-client-plan.md), [0057](docs/history/0057-rename-over-deleted-name.md), [plan](docs/client-test-plan.md) |
| Tooling and static analysis | checkpatch clean and failing the CI Lint job on any finding; sparse and `W=1` report nothing on both modules; CodeQL triaged; KASAN job reproducible on a local KVM host | sparse and `W=1` in CI | [0045](docs/history/0045-codeql-triage.md), [0050](docs/history/0050-dx-code-items.md), [0056](docs/history/0056-sparse-w1.md), [0058](docs/history/0058-kasan-local.md) |

## Planned

No dates. Each item starts with an issue or ADR and ends with a journal entry.
Listed in the order they are worked on (agreed on 2026-10-12).

| Item | Scope | Reference |
| --- | --- | --- |
| 1. S1 — Threat and credential model | ADR: the adversary (unprivileged local user with access to the mount point, a process writing to the lower tree from outside, the lower tree's content on disk), what the layer promises and what it does not, `FS_USERNS_MOUNT` off, the unprivileged surfaces, and the open points left by journals 0028 and 0030. Then community action 15 (review requests to independent reviewers); also a condition of distribution stage 2 | [security-roadmap](docs/security-roadmap.md) |
| 2. Real-client test, stage 2 | `QUERY2` with ai-memory's data and query shapes: a sidecar mirrors front matter into `user.moob.*`, its structured recalls are replayed through `QUERY2`, answers and latency compared, write cost of the sidecar measured | [client-test-plan](docs/client-test-plan.md) |
| 3. Remaining M3 (done) | Phase 3a: [client-contract](docs/client-contract.md) (journal 0070). Phase 3c (journal 0072): batch read of content dropped, change cursor stays in the client, attribute values with results proposed. Original scope: phase 3c: revisit `READ_BATCH` and the change cursor with the M4 numbers | [spec-m3](docs/spec-m3.md) §2, §4, §5, §6 |
| 4. S4 and S2 | S4: the lower tree as hostile input (attributes written from outside treated as untrusted at the mount scan and in `getxattr`, a fuzz target of adversarial lower trees, the behaviour on an invalid attribute decided in the S1 ADR); S2: fstests over the layer, group `quick`, per lower filesystem | [security-roadmap](docs/security-roadmap.md) |
| 5. Community actions 17–18 and the rest of S6 | `MOOB_ONLY` to run a single suite step; the supported-kernel and UAPI compatibility policy (S6; `SECURITY.md` with contact and response targets is done); KASAN as a required status check in branch protection (today it runs on every `layer/` and `vm/` PR and the process requires a green run, but only "Tools and sanitizers" and "Native Linux VM" are enforced) | [community-plan](docs/community-plan.md) §8, §11 |
| M4 report (done as a pilot-grade report, journal 0088) | The four questions answered with what was measured, including where the layer loses. Not run: repetitions with confidence intervals, three profiles, 1 to 100 processes, the budget-saturation series, and the comparison with an index in user space (journal 0092, in progress) | [0088](docs/history/0088-m4-report.md), [benchmark-plan](docs/benchmark-plan.md) |

## Depends on validation

These are deliberately **not** scheduled: each is conditioned on M4 results or
on the security roadmap, and a negative result may cancel it.

| Item | Condition | Reference |
| --- | --- | --- |
| Index persistence (surviving remount) | M4 must show that rebuild time or memory at the target scale justifies an on-disk structure, and a format and compatibility ADR must exist | [spec-m3](docs/spec-m3.md) §1, [value-assessment](docs/value-assessment.md) §9 |
| Attribute values with query results | Measured in journal 0072: reading the attributes of results costs 2 to 16 µs per file against 0.9 µs per result for the query. A projection answered from the index needs an ADR (it changes the UAPI) and a check that more than one surveyed system needs it. The batch read of content (`READ_BATCH`) is dropped | [spec-m3](docs/spec-m3.md) §4, journal 0072 |
| Change cursor ("changes since revision") | Decision deferred; needs a client with a measured benefit and answers to the open questions in spec-m3 §5 | [spec-m3](docs/spec-m3.md) §5 |
| Own filesystem as a lower (the pre-ADR-0007 path) | Returns only as an alternative implementation of the same interface, and only with evidence that the mature lower filesystems are the bottleneck | [ADR-0007](docs/decisions/0007-stacked-memory-layer.md) |
| Public release (DKMS, `.deb`, kernel matrix) | Stage 2 (technical preview) needs S1, S4, S5, S6, S2 results and S10; stage 3 (public) needs M4 published and S1–S6, S8–S10 done | [distribution-strategy](docs/distribution-strategy.md) §8 |
| Security roadmap, remaining items | S3 syzkaller, S7 continuous fuzzing, S8 runtime hardening, S9 resource limits and denial of service, S10 module signing and distribution, S11 M3/M4 surfaces | [security-roadmap](docs/security-roadmap.md) |

The [original architecture](docs/native-mvp.md) explores the full transactional
MVP; the [M1 format](docs/format-m1.md) intentionally does not implement it, and
ADR-0007 moved the project to a stacked layer. Any extension starts with an ADR,
a format or UAPI compatibility decision and measurable tests.

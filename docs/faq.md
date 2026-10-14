# Frequently asked questions

Short, factual answers about MoobFS as of 2026-10-10. Each answer names the file it is taken
from; when a file and this page disagree, the file wins and this page needs a correction.
Rendered copy: [studiomoob.github.io/llmoobfs/faq.html](https://studiomoob.github.io/llmoobfs/faq.html).

## 1. What is MoobFS?

MoobFS is an experimental GPL-2.0 Linux kernel module, stacked over ext4, btrfs or XFS, that
passes POSIX operations to the lower filesystem and adds typed `user.moob.*` attributes, an
in-memory attribute index and a `QUERY` ioctl. It is a research project; the only tag is the `v0.1.0-experimental` source pre-release.
[`README.md`; `docs/research-originality.md` §9]

## 2. What problem does it target?

It explores storing LLM-agent memories as ordinary files while letting a program ask which
files have given attribute values, with the operating system's permissions deciding what each
caller sees. The narrow hypothesis is attribute queries with per-result POSIX authorization,
no separate database server and no second authorization policy. That hypothesis is not yet
shown to cost less than SQLite plus application-level checks.
[`docs/value-assessment.md`, "Conclusão fundamentada"; `docs/when-not-to-use.md`]

## 3. How does the stacking work?

The module mounts with `mount -t moobfs_layer -o lower=<dir> none <mount>` and delegates
file I/O to the lower directory through the kernel's backing-file helpers
(`fs/backing-file.c`), in the same family of designs as overlayfs and ecryptfs. Attributes
are real xattrs on the lower filesystem, validated by the layer on write. Layer inodes reuse
the lower inode numbers.
[`docs/decisions/0007-stacked-memory-layer.md`; `layer/main.c`; `docs/architecture-overview.md` §1-2]

## 4. What does `QUERY` do?

`QUERY` is an ioctl on the mount root that returns the inode number and mount-relative path
of files matching up to four attribute predicates joined by AND, with the operators `=`, `<`,
`<=`, `>`, `>=`, string prefix and "attribute exists", and a result limit of 1 to 4096.
Answers were checked against a user-space oracle (`moobfs-index`) on ext4, btrfs and XFS.
[`include/moobfs_ioctl.h`; `docs/history/0014-phase-2d-index-query.md`]

## 5. What does `QUERY` not do?

It has no OR, no sorting or aggregation, no full-text search and no vector or semantic
search. The agent application selects relevant memories and builds the model's context; the
filesystem does not extend a context window.
[`docs/value-assessment.md`; `docs/research-originality.md` §7]

## 6. How is `QUERY` authorized?

For each candidate, the kernel resolves the recorded path with the caller's credentials and
requires `inode_permission(MAY_READ)` on the resolved inode, so mode bits and the POSIX ACLs
of the lower filesystem apply per result. Unauthorized results are omitted silently, and
`count`/`truncated` refer only to visible results. This was tested with two uids, two
namespaces, chmod and ACLs on three lower filesystems, in the Debian VM and under KASAN; no
external audit exists. Two known limits, measured on 2026-10-11: response time grows with the
number of matching entries the caller cannot see (about 1.15 µs each), so a caller can estimate
how many exist; and processes confined by Landlock still see results whose files Landlock would
not let them open (the names and attribute values are ones such a process can already list and
read, because Landlock does not cover xattr reads).
[`docs/spec-m3.md` §3.1; `docs/history/0020-phase-3b-query-authorization.md`]

## 7. What is the TOCTOU contract?

A result is not a grant of access. Authorization is checked when the query runs; permissions
or names may change afterwards, so a client must call `open()` normally and accept `EACCES`
or `ENOENT`. There is no guarantee of the absence of a time-of-check/time-of-use window in
UAPI v1. A known limitation is possible false negatives after a directory rename until the
next mount.
[`docs/spec-m3.md` §3.1]

## 8. What is implemented today?

A stacked pass-through layer validated over ext4, btrfs and XFS with `fsx`, typed attributes,
the in-memory index, the `QUERY` and `GET_INFO` ioctls, delegated reflink and dedupe
(provided by the lower filesystem), and acceptance runs with git, concurrent WAL-mode SQLite,
`dm-log-writes` power-loss replay and `dm-flakey` error injection. Phase 3b (authorization in
`QUERY`) closed with a KASAN run and is merged in `main`. Everything is tested in a VM; there
is only a source pre-release tag (`v0.1.0-experimental`).
[`README.md`; `ROADMAP.md`; `CHANGELOG.md`]

## 9. What is not implemented?

A persistent index (it lives in RAM and each domain's part is built by its first query), user-namespace and idmapped
mounts (both refused), batch reads, a change cursor, revisions, packaging (DKMS, `.deb`),
a stable UAPI, a CLI, and the full xfstests suite. Performance is not measured.
[`docs/spec-m3.md`; `docs/distribution-strategy.md`; `ROADMAP.md`]

## 10. How does it differ from SQLite next to plain files?

SQLite offers full SQL, composite indexes and decades of maturity; MoobFS offers a single
store of files plus xattrs and authorization by the OS per result. Writes to SQLite and to
the files are two operations; MoobFS writes a file and an xattr, but file/metadata atomicity
is the client's convention. Today SQLite or plain files are the recommendation for anything
outside a lab.
[`docs/value-assessment.md` "Comparação honesta"; `docs/when-not-to-use.md` §1]

## 11. How does it differ from plain files and from FUSE-based systems?

Plain files need `find` plus `getfattr`, a linear scan, with no query call. FUSE systems run
filesystem logic in a user-space process; MoobFS is a native kernel module, which gives
different trade-offs (a kernel bug can crash the host). No speed comparison has been
measured.
[`AGENTS.md`; `docs/value-assessment.md`; `docs/benchmark-plan.md`]

## 12. Is it a competitor to mem0, Letta or ai-memory?

No. They are memory frameworks and potential clients; MoobFS is a possible storage layer
under them. The M4 benchmark plan treats real clients (ai-memory, mem0, Letta) as workloads.
Prior art in agent filesystems such as YoloFS (2026) is acknowledged.
[`docs/benchmark-plan.md`; `docs/research-originality.md` §3, §9]

## 13. What is proven and what is still a hypothesis?

Proven in the author's VM runs: answer correctness against the oracle, two-uid isolation on
three lower filesystems, git/SQLite acceptance, and a KASAN/UBSAN/lockdep run on kernel
6.12.112. Hypotheses pending M4: any speed or efficiency advantage, behaviour near the index
budget, scale and long-running operation. There is no independent reproduction yet.
[`docs/research-originality.md` §5; `docs/value-assessment.md` "Medições do M4"; `docs/benchmark-plan.md`]

## 14. Which kernels are supported?

One test target: Debian kernel 6.12.111 in a QEMU VM, plus an upstream 6.12.112 KASAN debug
kernel in the weekly and pull-request workflow. Kernel APIs change between versions;
compatibility with other kernels is not promised.
[`README.md`; `docs/testing.md` §1; `docs/community-plan.md` §9]

## 15. Is it production-ready? Is there a release?

No, and no. It is a research project whose only tag is the `v0.1.0-experimental` source pre-release; a module bug can cause a
kernel panic or data loss, and the project's own tests found three kernel-side defects during
development (two in journal 0015, one in journal 0016). Run it only in a disposable VM with
synthetic data.
[`docs/value-assessment.md` §9; `docs/history/0015-phase-2e-investigation.md`; `docs/history/0016-kasan-lockdep-harness.md`; `SECURITY.md`]

## 16. How can I try it?

With Docker, `make vm-test` builds the tools and module and boots a disposable QEMU VM with
the pinned kernel; no host module loading is needed. Expect passing markers such as
`MOOBFS_LAYER_PASS` and `MOOBFS_ACCEPTANCE_PASS` in `build/vm/console.log`. Do not load the
module on a machine you care about.
[`docs/development.md` §3; `docs/testing.md` §3]

## 17. How is data read without the module ("nothing trapped")?

Memories are ordinary files and xattrs on the lower filesystem. After unmounting, `grep`,
`getfattr`, `git` and `rsync` read them directly. This follows from the design and was
exercised with git and `fsx`; reading the lower directory after a specific unmount sequence
is rated "probable" in the originality audit.
[`docs/decisions/0007-stacked-memory-layer.md`; `docs/research-originality.md` §5]

## 18. What license applies?

GPL-2.0-only for the whole project.
[`LICENSE`; `README.md`]

## 19. How do I contribute or ask a question?

Start with `CONTRIBUTING.md`: reproducible bug reports, kernel or format review and
reproducibility improvements are welcome, with a `Signed-off-by` on each commit (DCO) and a
KASAN run required for changes under `layer/` or `vm/`. Questions go to GitHub Discussions
or an issue; review requests with bounded questions are listed in the community plan.
[`CONTRIBUTING.md`; `docs/community-plan.md` §8 and §13]

## 20. How do I report a security issue?

Do not open a public issue. Use the private GitHub Security Advisory channel or the contact
in `SECURITY.md`; the stated response targets are acknowledgement in 7 days and an assessment
in 30.
[`SECURITY.md`; `docs/community-plan.md` §13]

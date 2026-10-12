# Free software ecosystem relevant to MoobFS

Survey date: 2026-10-10. Goal: before implementing something, check whether a free solution
already exists that solves the problem or serves as a reference. Organized by project problem.
Stars, license and last push date were read from the GitHub API on 2026-10-10; they are
indicators of activity, not of quality. Nothing here has been run or integrated yet.

## 1. Testing the module on a kernel with KASAN and lockdep (M1 hardening)

| Project | License | Activity | Use for MoobFS |
| --- | --- | --- | --- |
| [virtme-ng](https://github.com/arighi/virtme-ng) | GPL-2.0-only | active (2026-10) | Boots a freshly compiled kernel in QEMU using the host system as a copy-on-write root. Replaces part of our harness in `vm/` for fast iterations with our own kernel compiled with `CONFIG_KASAN` and `CONFIG_PROVE_LOCKING`. |
| [syzkaller](https://github.com/google/syzkaller) | Apache-2.0 | active | Syscall fuzzer with a `syz-mount-image` mode for filesystem images. Heavy, but it is the reference for continuous fuzzing. |
| [LTP](https://github.com/linux-test-project/ltp) | GPL-2.0 | active | POSIX syscall tests over a mount; useful once there is write support. |

The current harness (`vm/`, precompiled Debian kernel) remains valid for reproducibility;
virtme-ng is complementary, for the instrumented kernel that Debian does not ship.

## 2. Fuzzing the image parser

| Project | License | Activity | Use |
| --- | --- | --- | --- |
| [Janus](https://github.com/sslab-gatech/janus) | MIT | stalled since 2019 | Joint image + syscall fuzzing over the Linux Kernel Library. Method reference; old code. |
| [Hydra](https://github.com/sslab-gatech/hydra) | MIT | stalled since 2022 | Successor to Janus. Same caveat. |
| libFuzzer/AFL++ over `common/` | — | — | The M1 parser is shared between the tools and the module and compiles on the host. User-space fuzzing of that code is the cheapest and most immediate path. |

Suggested decision: user-space fuzzing of the shared parser first; syzkaller
with `syz-mount-image` later; Janus/Hydra only as reading.

## 3. Consistency after power loss (M2)

| Project | License | Activity | Use |
| --- | --- | --- | --- |
| `dm-log-writes` (kernel ≥ 4.1) | GPL-2.0 | in-tree | Records and replays block writes with FUA marks; allows restoring the volume to any point and checking recovery. It is the kernel's standard mechanism. |
| [xfstests](https://git.kernel.org/pub/scm/fs/xfs/xfstests-dev.git) (`generic/45x`, `generic/482`) | GPL-2.0 | active | Regression suite used by all in-tree filesystems. The crash tests use `dm-log-writes`. Being runnable under xfstests is a reasonable maturity criterion for M2. |
| [CrashMonkey/ACE](https://github.com/utsaslab/crashmonkey) | Apache-2.0 | stalled since 2022 | Systematic workload generation and crash-consistency checking (OSDI 2018). Relevant method; the code needs adapting to current kernels. |
| `dm-flakey` | GPL-2.0 | in-tree | I/O fault injection. |

Suggested decision: `dm-log-writes` + our own xfstests-style tests. Do not write our own
replay mechanism.

## 4. On-disk and in-memory structures in the kernel

Detailed survey, verified against v6.12, in [kernel-infrastructure.md](kernel-infrastructure.md).

| Resource | Use |
| --- | --- |
| `lib/btree.c` | Generic in-memory B+ tree in the kernel. Not suitable for disk, but avoids writing an in-memory index. |
| `lib/maple_tree.c`, `lib/xarray.c` | Ordered in-memory maps; modern alternatives for an index cache. |
| `fs/erofs` | Read-only, in-tree, iomap-based filesystem. Closest reference to what M1 does today; worth comparing the read path and image validation. |
| `fs/befs/btree.c` | Reading of BFS's on-disk B+ tree (GPL-2.0, read-only, no exposed attributes/indexes). Layout reference. |
| `fs/nilfs2` | Log-structured with continuous snapshots. Reference for M3. |
| `fs/btrfs`, `fs/bcachefs` | Copy-on-write snapshots. Design references, too large for direct reuse. |
| `lib/crc32c`, `crypto/sha256` | Already used; keep the kernel APIs instead of our own copies where possible. |

## 5. The Be File System in user space

[Haiku](https://github.com/haiku/haiku) (mostly MIT) ships `bfs_shell`, built
on `fs_shell`: the real BFS code running as a command-line program on Linux, with
`query`, `mkindex`, `ls`, `cp`, `sync`. This allows, without a Haiku kernel:

- Observing the behavior of BFS attributes, indexes and queries as an oracle when specifying
  the `QUERY` of ADR-0005.
- Using BFS as a baseline in M4 to compare the cost of structured queries.
- Studying `fs_shell` as a pattern for running MoobFS's own code in user space,
  reusing `common/` and avoiding a VM reboot on every test.

The book *Practical File System Design with the Be File System* (Giampaolo, 1999) circulates as
a PDF; the link at nobius.org answered 406 to automated access and was not verified.

## 6. Memory layer for agents (M4)

All of the below were active as of 2026-10; their license is Apache-2.0 unless the row says otherwise. They are candidates to be
**clients** of MoobFS, not competitors: they handle semantic selection and context assembly,
which ADR-0005 keeps out of the kernel.

| Project | Approach | Relation to MoobFS |
| --- | --- | --- |
| [mem0](https://github.com/mem0ai/mem0) | Memory SDK added to an existing agent; pluggable backends | An alternative storage backend is the simplest integration point for M4. |
| [Letta](https://github.com/letta-ai/letta) | Runtime where the agent is the memory (MemGPT); core/archival tiers | Archival memory can be mounted as a directory. |
| [Graphiti](https://github.com/getzep/graphiti) | Temporal knowledge graph (Zep team) | Facts with temporal validity map well onto revisions + the `time` attribute. |
| [cognee](https://github.com/topoteretes/cognee) | Ingestion of varied documents into a queryable graph | Same; client. |
| [letta-code](https://github.com/letta-ai/letta-code) (Apache-2.0) | Coding agent memory in files, with git | Client and realistic workload for M4; reference for organizing memory in directories. |
| [Agent VFS](https://github.com/johannesmichalke/agent-vfs) (MIT) | User-space virtual filesystem with database persistence | Interface reference for agents; it is neither a kernel module nor a competitor to the layer. |
| [Microsoft GraphRAG](https://github.com/microsoft/graphrag) (MIT) | Knowledge pipeline: entities, relations, communities | Source of attribute and relation datasets for `QUERY` workloads; it is not storage. |
| [ai-memory](https://github.com/akitaonrails/ai-memory) (MIT, Rust) | Markdown wiki in git as source of truth, derived SQLite, capture via hooks, zero LLM by default | Closest client to our model: source of truth in files. Full reading in [research-ai-memory.md](research-ai-memory.md). |

Comparisons published in 2026 are mostly from vendors; no independent measurement
was found. M4 must define its own metrics.

## 7. Local vector search

Measurement plan with these systems: [benchmark-plan.md](benchmark-plan.md).

| Project | License | Use |
| --- | --- | --- |
| [sqlite-vec](https://github.com/asg017/sqlite-vec) | Apache-2.0 | Embedded vector index, serverless; fits in a file next to the mount. |
| [USearch](https://github.com/unum-cloud/usearch) | Apache-2.0 | HNSW in C++ with bindings; index serializable to a file. |

Both stay in user space, per ADR-0005. The vector index can itself be
a versioned memory on the volume.

## What does not exist ready-made

- An in-tree Linux filesystem with indexed typed attributes and native query. BFS did this
  in 1997; the Linux driver does not expose it. That is the space MoobFS occupies.
- Content dedup + revisions + snapshots in a small, auditable format. Btrfs and bcachefs
  have parts of this at a much larger scale.

## Derived next steps

1. User-space fuzzing of `common/` with libFuzzer (days, not weeks).
2. Our own kernel with KASAN/lockdep via virtme-ng running the current suite.
3. Compile Haiku's `bfs_shell` on Linux and record the behavior of `query`/`mkindex`
   as an oracle for the `QUERY` specification.
4. Specify M2 already assuming `dm-log-writes` + xfstests as the crash harness.

## Sources consulted

- [CrashMonkey/ACE, OSDI 2018](https://www.usenix.net/system/files/osdi18-mohan.pdf)
- [dm-log-writes in xfstests (lore)](https://lore.kernel.org/all/20170904064214.GI17782@dastard/)
- [Janus](https://github.com/sslab-gatech/janus), [OS fuzzing survey](https://arxiv.org/pdf/2502.13163)
- [virtme-ng, Kernel Recipes 2024](https://kernel-recipes.org/en/2024/virtme-ng/)
- [bfs_shell on Linux (Haiku)](https://www.haiku-os.org/documents/dev/copying_files_from_and_to_haiku_from_within_linux_using_bfs_shell/), [filesystem development overview](https://git.haiku-os.org/haiku.git/tree/docs/develop/file_systems/overview.rst)
- [mem0/Letta/Zep/cognee comparison](https://mcp.directory/blog/mem0-vs-letta-vs-zep-vs-cognee-2026)
- [iomap, LWN](https://lwn.net/Articles/935934)

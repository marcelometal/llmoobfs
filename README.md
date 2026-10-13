# MoobFS

**An experimental native Linux filesystem for LLM memory and context.**

MoobFS is a Linux kernel module (GPL-2.0, no FUSE) that stacks over a directory of ext4, btrfs
or XFS, validates typed `user.moob.*` attributes on ordinary files and answers attribute queries
from the kernel, returning only the files the caller is allowed to read. It is a research
project exploring storage for LLM-agent memory.

[Português](README.pt-BR.md) · [Project site](https://studiomoob.github.io/llmoobfs/) · [Docs index](docs/README.md) · [FAQ](docs/faq.md) · [Contributing](CONTRIBUTING.md) · [Roadmap](ROADMAP.md)

[![CI](https://github.com/studiomoob/llmoobfs/actions/workflows/ci.yml/badge.svg)](https://github.com/studiomoob/llmoobfs/actions/workflows/ci.yml)
[![License: GPL-2.0-only](https://img.shields.io/badge/license-GPL--2.0--only-blue)](LICENSE)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/studiomoob/llmoobfs/badge)](https://scorecard.dev/viewer/?uri=github.com/studiomoob/llmoobfs)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/15408/badge)](https://www.bestpractices.dev/projects/15408)

## See it run

Each top-level directory of the mount is a permission domain. Root tags four memory files with
a typed attribute; Alice (uid 1001) then queries. She gets her own memory and, in the shared
`team/` domain, only `style.md`. `salary.md` is missing because a domain indexes only files that
everyone who can enter the domain can read (the homogeneity rule of
[spec-query-v2](docs/spec-query-v2.md) §3); being readable by Bob alone, it is outside `team/`'s
index for every caller, Bob and root included, and private files belong in their own domain.
Bob's domain and the whole-mount `QUERY` (v1, for administrators) are refused to her:

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
```

This is real output from `make vm-test` on 2026-10-11 (Debian 6.12.111 in QEMU, ext4 lower): the
`DEMO| ` lines of `build/vm/console.log` with the prefix removed, and the closing `GET_INFO`
call left out. Lines starting with `#` are commands run as root. `query` is the test client in
[`tests/query.c`](tests/query.c), since there is no CLI yet; `--uid 1001` switches to that user
before the call, and the `q2` form is `MOOB_IOC_QUERY2` on a domain directory. The script is the
demonstration block of [`vm/suite.sh`](vm/suite.sh).

## Quickstart

With Docker running (on Apple Silicon the VM runs under CPU emulation and takes longer):

```sh
git clone https://github.com/studiomoob/llmoobfs.git
cd llmoobfs
make vm-test                          # builds the tools and modules, boots a disposable QEMU VM
grep '^DEMO| ' build/vm/console.log   # the session above, from your own run
```

Never load the modules on your host. Setup details are in [docs/development.md](docs/development.md),
and what each success marker proves is in [docs/testing.md](docs/testing.md).

> **Experimental. Source pre-release tag only (`v0.1.0-experimental`); not for production.**
> Facts as of 2026-10-11:
> - **One kernel series**, in a VM: Debian 6.12.111 in QEMU and upstream 6.12.112 under
>   KASAN/UBSAN/lockdep; lower filesystems ext4, btrfs and XFS only.
> - The attribute index lives **in RAM only**, bounded by `index_budget`, rebuilt at every mount.
> - Mounting needs **`CAP_SYS_ADMIN` in the initial user namespace**; user-namespace and
>   idmapped mounts are refused.
> - **UAPI v1 and v2 may change**, with no compatibility promise. With domains on (the default
>   since phase Q2a), the v1 `QUERY` is for administrators only.
> - **No speed or efficiency claim.** Only pilot-grade measurements on one cloud VM exist
>   ([bench/results/](bench/results/README.md)); M4 is not done.
> - **No external audit.** Our own tests and reviews found and fixed kernel-side defects
>   (journals [0015](docs/history/0015-phase-2e-investigation.md),
>   [0016](docs/history/0016-kasan-lockdep-harness.md),
>   [0035](docs/history/0035-phase-q2a-domain-query.md)).
> - Reflink, deduplication and durability come from the lower filesystem, not from MoobFS.
>
> Contributions are welcome ([CONTRIBUTING.md](CONTRIBUTING.md)). Everything in the repository is
> in English, including the design documents and the technical journal
> ([documentation index](docs/README.md)).

## At a glance

**What it is.** A native Linux kernel module, `moobfs_layer.ko`, mounted as a stacked layer
over a directory of a mature filesystem (ext4, btrfs or XFS). Memories are ordinary files;
their metadata are typed extended attributes (`user.moob.*`) that the module validates on
write and indexes in RAM, partitioned by permission domain (by default, each top-level
directory). A `QUERY2` ioctl on a domain directory answers conjunctive attribute predicates from
the kernel, returning only files that everyone who can enter the domain may read; the older
whole-mount `QUERY` checks each result against POSIX permissions and ACLs and, with domains on,
is for administrators. Everything stays readable with plain Unix tools without the layer.
A read-only M1 image format with shared physical payloads is kept as a regression.

**What it is not.** Not FUSE and not a daemon: there is no user-space server, socket or
background process. No semantic or vector search in the kernel: ranking, embeddings and
context assembly belong to the client. Not a database replacement; for anything beyond a
disposable VM today, SQLite or plain files remain the recommendation
([value assessment](docs/value-assessment.md)). No persistence of the index, no snapshots and no
transactions of its own; durability, reflink and dedupe are the lower filesystem's.

**How to check any claim here.** Run `make vm-test` and read `build/vm/console.log`
([testing guide](docs/testing.md)); set up the environment with
[docs/development.md](docs/development.md).

## Project goal

MoobFS aims to provide a filesystem-level storage foundation for persistent memory
and context consumed by LLM-based agents. The intended use is to retain memories
such as observations, decisions and task records across sessions, share repeated
content and eventually preserve revisions that agents can retrieve as context.

The filesystem stores and exposes memory as files. The agent application is
responsible for selecting relevant material and assembling the LLM's context.
Semantic retrieval and context assembly still need to be designed and integrated;
MoobFS does not currently provide them or extend a model's context window.

## How it works

```text
client (git, sqlite3, a script, an agent)
   │ write tmp, setxattr user.moob.*, fsync, rename        │ QUERY ioctl on the mount root
   ▼                                                        ▼
Linux VFS ──► moobfs_layer.ko: validates typed attributes, keeps an in-RAM index,
              authorizes each QUERY result with the caller's credentials,
              delegates I/O through fs/backing-file.c
   ▼
lower filesystem (ext4, btrfs or XFS): real files, real xattrs, journal, reflink
```

Writing a memory is an ordinary POSIX write plus `setxattr`; the layer validates the encoding
(type byte plus payload) before the xattr reaches the lower filesystem and updates its index.
Finding memories is one `QUERY2` call on a domain directory: up to four attribute predicates
joined by AND, answered from that domain's partition of the index after one permission check on
the directory. A file that not everyone entering the domain may read is left out of the
partition ([spec-query-v2](docs/spec-query-v2.md) §3). The client then opens the file
normally. Unmount the layer and the lower
directory is a plain tree that `grep`, `getfattr`, `git` and `rsync` read without the module.
Details with source citations: [architecture overview](docs/architecture-overview.md); the
same flow as a diagram: [site/plan.html](https://studiomoob.github.io/llmoobfs/plan.html#flow).

## What is implemented, with evidence

"Tested on" names the kernel and lower filesystems that actually ran; nothing here means
production readiness. Since [ADR-0007](docs/decisions/0007-stacked-memory-layer.md) MoobFS is
built as a native stacked layer over a mature filesystem; the M1 experiment is frozen and runs
as a regression in the same VM.

| Item | Journal | Tested on |
| --- | --- | --- |
| M1 read-only image: offline creator, verifier, native module, two inodes sharing one payload (FIEMAP), 15 malformed images rejected | [0002](docs/history/0002-native-read-milestone.md), [format](docs/format-m1.md) | Debian 6.12.111 (QEMU), loop device |
| Phase 2a–2b: stacked pass-through (`mount -t moobfs_layer -o lower=<dir>[,nolinks]`): files, directories, rename, links, xattr, mmap, fsync; delegated reflink/dedupe/`copy_file_range`, FIEMAP; `fsx` | [0011](docs/history/0011-phase-2a-stacked-passthrough.md), [0012](docs/history/0012-phase-2b-full-passthrough.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Phase 2c: typed `user.moob.*` attributes validated in the kernel; `moobfs-attr`, `moobfs-check`; host unit tests; libFuzzer target | [0013](docs/history/0013-phase-2c-typed-attributes.md) | Host tests (Linux/macOS), VM over the three lowers |
| Phase 2d: in-memory index, `QUERY` and `GET_INFO` (UAPI v1), answers identical to the `moobfs-index` oracle, also after rename, unlink, removexattr and remount; `noindex`, `index_budget` | [0014](docs/history/0014-phase-2d-index-query.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Phase 2e: acceptance with git, concurrent WAL-mode SQLite, own memory client; power-loss replay with `dm-log-writes`; I/O errors with `dm-flakey`; two layer bugs found and fixed | [0015](docs/history/0015-phase-2e-investigation.md) | Debian 6.12.111; ext4 only |
| KASAN/UBSAN/lockdep harness running the same suite; a third defect found and fixed | [0016](docs/history/0016-kasan-lockdep-harness.md) | Upstream 6.12.112, virtme-ng |
| M3 phase 3b: per-result POSIX authorization in `QUERY`; two uids, two namespaces, chmod and ACLs, remount, idmapped and user-namespace mounts refused, 5-second race of chmod/rename/unlink against `QUERY` | [0020](docs/history/0020-phase-3b-query-authorization.md), [spec-m3](docs/spec-m3.md) §3 | Debian 6.12.111 and KASAN 6.12.112; ext4, btrfs, XFS |
| Phase Q2a: domain-scoped `QUERY2` (UAPI v2) on a domain directory, index partitioned by domain and a homogeneity rule, v1 restricted to administrators while `domain_depth` is on, `EXDEV` for directories across domains; equal to `moobfs-index --domain` (performance and timing-channel test not yet measured) | [0035](docs/history/0035-phase-q2a-domain-query.md), [spec-query-v2](docs/spec-query-v2.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Phase Q2b: `QUERY2` ordered by an attribute, ascending or descending, and paged by an authenticated cursor; equal to `moobfs-index --domain --order` (not yet measured) | [0036](docs/history/0036-phase-q2b-order-cursor.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Phase Q2c: leapfrog intersection of equality terms in `QUERY2`; equal to the oracle, plan checked by result order (not yet measured) | [0037](docs/history/0037-phase-q2c-leapfrog.md) | Debian 6.12.111; ext4, btrfs, XFS |
| Phase Q2d: "missing or ..." terms and batches of up to 8 `QUERY2` requests per call; equal to the oracle (not yet measured) | [0038](docs/history/0038-phase-q2d-missing-batch.md) | Debian 6.12.111; ext4, btrfs, XFS |

Not implemented: a persistent index, user-namespace or idmapped mounts, batch reads, a change
cursor, revisions, packaging, a stable UAPI, a CLI, the full xfstests suite, and the M4
measurements (only pilot-grade campaigns exist, in [bench/results/](bench/results/README.md)). See [ROADMAP.md](ROADMAP.md) for the planned and validation-dependent items.

## Limitations

- One kernel series tested (Debian 6.12.111, upstream 6.12.112 under KASAN), x86-64, in a VM;
  kernel APIs are version-specific and the pinned test target is not a compatibility promise.
- The index is RAM only, non-pageable, bounded by `index_budget` (default 128 MiB), rebuilt by
  a linear scan at every mount; behaviour near a saturated budget on large trees is untested.
- Mounting needs `CAP_SYS_ADMIN` in the initial user namespace; no rootless containers.
- UAPI v1 and v2 may change; the `v0.1.0-experimental` tag is a source snapshot, with no binaries, packaging or support.
- Only pilot-grade figures exist (one Azure VM, one dataset seed; journals 0025–0031); no
  comparison with SQLite, FUSE or the bare lower filesystem may be stated as a result before M4.
- No external audit, no syzkaller, no full xfstests, no threat-model document yet.
- `QUERY` authorization holds at query time only (TOCTOU contract); possible false negatives
  after a directory rename until remount ([spec-m3](docs/spec-m3.md) §3.1).
- File/metadata atomicity is a client convention; durability, reflink and dedupe are the lower
  filesystem's. Acceptance ran over ext4 only.
- Inode numbers are the lower's. When the lower directory spans several btrfs subvolumes, two
  files can show the same `st_dev` and `st_ino` through the layer, which confuses tools that
  detect hard links that way (`tar`, `rsync`, `du`, `find -samefile`). Keep the lower in one
  subvolume.
- Data is written with the mounter's credentials without `CAP_SYS_RESOURCE`, so quota limits hold.
  On ext4 with the default reserve owner (uid 0), ordinary users writing through the layer can
  still use the blocks reserved for root; mount the lower with another `resuid`, or set the
  reserve to zero, if that matters.
- `noatime`, `relatime` and `strictatime` on the layer's mount have no effect; access times
  follow the mount options of the lower filesystem.

## Compared with plain files, SQLite and memory frameworks

**Plain files.** Same data model (files plus xattrs), no module, no root, total portability;
queries are `find` plus `getfattr`, a linear scan with no query call. If attribute queries are
rare, plain files win.

**SQLite next to the files.** Full SQL, composite indexes, decades of maturity, failures
contained in a process, atomic data-and-metadata transactions inside the database; the
application implements authorization and keeps the files and rows consistent. Today this is
the recommendation for anything outside a lab.

**Memory frameworks (mem0, Letta, ai-memory).** They decide what to remember and how to build
a prompt; MoobFS stores files and answers attribute predicates. They are potential clients on
top of the layer, not competitors; the M4 plan uses them as workloads.

**MoobFS.** One store, per-result authorization by the OS, no daemon, data readable without
the module; in exchange, a kernel module from one author, one tested kernel, a RAM-only index
and no measured advantage. Full comparison table and the conditions under which it may be
worth evaluating: [when-not-to-use.md](docs/when-not-to-use.md).

## Try it in a VM

With Docker running, from the repository root:

```sh
git clone https://github.com/studiomoob/llmoobfs.git
cd llmoobfs
make vm-test
```

This builds the Linux tools and both modules, then boots a dedicated QEMU x86-64 VM using
Debian kernel **6.12.111+deb13-amd64**. No privileged container or host module loading is
required; never load the modules on your host. On Apple Silicon it uses CPU emulation; the
first build downloads kernel packages and takes longer. Environment setup is in
[docs/development.md](docs/development.md); what each marker proves and does not prove is in
[docs/testing.md](docs/testing.md). A successful run ends with:

```text
SHARED: inode=2/3 physical=12288 length=16384
MOUNTED_READ_TESTS_PASS
WRITABLE_MOUNT_REJECTED
MOOBFS_VM_PASS: shared extents, independent inodes, reads, readonly, remount, 15 malformed images
LAYER_LOWER_PASS: ext4 reflink=no
LAYER_LOWER_PASS: btrfs reflink=yes
LAYER_LOWER_PASS: xfs reflink=yes
DEMO_PASS: QUERY2 per domain, v1 refused to a user, heterogeneous file left out
MOOBFS_ACCEPTANCE_PASS: git, sqlite wal, memory client, dm-log-writes replay, dm-flakey
MOOBFS_LAYER_PASS: pass-through over ext4, btrfs and xfs; layer tests; typed attributes and tools; index and QUERY equal to the oracle; fsx 9000 ops each; reflink on btrfs/xfs; nolinks; unload
```

### The M1 image tools (regression)

The read-only M1 milestone validated that separate files can reference the same immutable
bytes: an offline tool creates the image and a native module serves reads through the VFS.
It demonstrates physical sharing, not an agent memory workflow. The tools need a C11 compiler,
Make and Python 3 on Linux or macOS:

```sh
make test
printf 'A memory shared by two files.\n' > build/memory.txt
build/host/mkfs.moobfs build/demo.img alpha.txt=build/memory.txt beta.txt=build/memory.txt
build/host/fsck.moobfs build/demo.img
```

The JSON report shows two inode numbers referencing the same object and physical offset. The
creator refuses to overwrite an existing image; `fsck.moobfs` checks images and does not
repair them. The VM fixture contains 12,562 logical bytes referenced by each file, stored
once; FIEMAP reports the same 16,384-byte allocation and the shared flag for both inodes.
Metadata and alignment overhead still count toward total image size.

| Component | M1 behavior |
| --- | --- |
| Native module | VFS mounting, flat directory, buffered reads via iomap, FIEMAP |
| Image creator | Deterministic offline creation, exact-byte deduplication, no overwrites |
| Verifier | Structural checks, bounds, CRC32C metadata, SHA-256 payload checks |
| Limits | 128 files/objects, 64 KiB per object, printable ASCII names up to 63 bytes |
| Validation | 9 tool tests, sanitizers for tools, actual QEMU mounts and 15 malformed images |

Images are immutable while mounted. Files have independent inodes and link count 1; this is
not hard linking. The module validates all content at mount, which adds a full scan. Disk
sharing does not guarantee page-cache sharing. The M1 module has not run under KASAN (the
KASAN harness runs the layer suite), and no benchmark exists.

## Documentation map

Every document under `docs/`, with its language, is listed in the [docs index](docs/README.md).

| Question | Document | Language |
| --- | --- | --- |
| How is the layer built, with source citations? | [architecture-overview.md](docs/architecture-overview.md) | EN |
| Should I use it, compared with files, SQLite, FUSE and memory frameworks? | [when-not-to-use.md](docs/when-not-to-use.md), [value-assessment.md](docs/value-assessment.md) | EN, PT |
| Short answers | [faq.md](docs/faq.md) | EN |
| How do I build, test and read the logs? | [development.md](docs/development.md), [testing.md](docs/testing.md) | EN |
| What does the M2 layer specify? | [spec-m2.md](docs/spec-m2.md), [kernel-infrastructure.md](docs/kernel-infrastructure.md), [ADR-0007](docs/decisions/0007-stacked-memory-layer.md) | EN |
| What does M3 specify (identity, isolation, provenance, change cursor, batch reads)? | [spec-m3.md](docs/spec-m3.md) | EN |
| How will performance be measured? | [benchmark-plan.md](docs/benchmark-plan.md), [benchmark-harness.md](docs/benchmark-harness.md) | PT (English translation in review) |
| What has been measured so far (pilots, not M4)? | [bench/results/](bench/results/README.md); raw archives in the [`bench-evidence`](https://github.com/studiomoob/llmoobfs/releases/tag/bench-evidence) release | EN |
| What does the domain-scoped `QUERY2` (UAPI v2) specify? | [spec-query-v2.md](docs/spec-query-v2.md) | EN |
| What is the M1 image format? | [format-m1.md](docs/format-m1.md) | EN |
| What was the original full MVP proposal? | [native-mvp.md](docs/native-mvp.md) (write interface superseded by ADR-0007) | EN |
| Why was each decision taken? | [docs/decisions/](docs/decisions/), [technical journal](docs/history/) | EN |
| Which free software is relevant around the project? | [ecosystem.md](docs/ecosystem.md) | EN |
| How is AI-assisted development governed? | [ai-workflow.md](docs/ai-workflow.md) (shared through `AGENTS.md`, `CLAUDE.md` and Cursor rules) | EN |
| Prior art, what may and may not be claimed | [research-originality.md](docs/research-originality.md) | EN |
| Which kernels are supported, and what may change between versions? | [compatibility.md](docs/compatibility.md) | EN |
| Changes, governance, conduct, security | [CHANGELOG.md](CHANGELOG.md), [GOVERNANCE.md](GOVERNANCE.md), [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md), [SECURITY.md](SECURITY.md) | EN |

Git records exact changes; journals record concise rationale, experiments and results. Future
work must distinguish proposals from implemented and tested behavior.

## Prior work and positioning

MoobFS builds on work it acknowledges: the Semantic File System (1991) and the Be File System
(1997) for attribute queries, overlayfs and ecryptfs for the stacked-filesystem model, and
YoloFS (2026), which already demonstrates a stacked kernel module aimed at agents. The
domain-scoped `QUERY2` follows research on access-controlled search: partitioning the index by
reader set (Singh et al. 2007; GUFI, SC22) closes the leaks that filtering after the search leaves
open (Büttcher and Clarke, FAST 2005; Wang et al., S&P 2017). The project claims none of these ideas
as new; what it explores is their combination in a stacked kernel layer, and whether that has
measurable value ([research-originality.md](docs/research-originality.md) §4, §5, §9). Every work,
project and tool used is listed with verified sources in [references.md](docs/references.md).

## Contributing and security

Bug reports, format review, kernel review and reproducibility improvements are welcome. Read
[CONTRIBUTING.md](CONTRIBUTING.md) (DCO sign-off, testing requirements, review rules).
Questions go to GitHub Discussions or an issue. AI-assisted contributions are welcome with
human review and accurate test evidence. Do not put private vulnerability details in public
issues; use the [security contact](SECURITY.md).

## License

The entire project is licensed under **GPL-2.0-only**. See [LICENSE](LICENSE).

# 0009 — What MoobFS would bring that is different, and the principle of not competing with what is mature

Date: 2026-10-10, America/Sao_Paulo.

## Request

Answer what benefit the filesystem would bring compared with others; record and publish. Then
the user stated a principle: mature libraries and filesystems, maintained by
millions of users, should not be competition but a foundation; the possible innovation lies in the
features an LLM memory would use. Example given: "concorrer com o git é loucura" ("competing
with git is madness").

## Recorded analysis

**No direct competitor among Linux filesystems:**

- Query by typed attribute inside the kernel (BFS did it in 1997; no Linux FS repeated it).
- A small format, auditable end to end, with full validation and invalid inputs
  tested one by one, designed for memory records.

**Equal to or worse than what exists, therefore not a differentiator:** dedup and reflink (btrfs, XFS,
ZFS), journaling (ext4, XFS), scale, performance and maturity. Semantic search stays out by
decision.

**Dated correction (2026-10-10):** the initial answer listed "replacing git with native revisions"
as a hypothetical benefit. This contradicts the principle above and is withdrawn. Git remains the
content versioning tool; per-memory revisions in M3, if they exist, must
complement (stable identity, per-revision attributes, temporal query), not replace
git, and this needs to be reevaluated in the M3 ADR.

## Principle adopted

Confirmed by the user and accepted as a project rule: use what is mature (git, ext4/btrfs/XFS,
jbd2, iomap, SQLite, xfstests), and concentrate the innovation on the memory-specific features for
LLMs: queryable typed attributes, full verification, identity and provenance of
memories, temporal query.

## Open implication for ADR-0006

The M2 specification (ADR-0006) describes a full volume: block allocator, directories,
inode table, integration with jbd2. By the principle above, this rebuilds what ext4 and btrfs
already do. The coherent alternative is a **native stacked filesystem** (pattern of overlayfs and
ecryptfs, both in-tree, `=m` in the VM; about 12.5 thousand and 8.2 thousand lines in v6.12) over a mature
FS: blocks, journal, directories, reflink, dedup and checksums come from the lower FS (btrfs or
XFS); the MoobFS layer adds only what is memory-specific. It remains a native mountable
module, without FUSE (ADR-0001 preserved). The decision to replace ADR-0006 with this form is
the user's and is recorded as a pending question; neither of the two was implemented.

## Changes

This journal, the history index and `CHANGELOG.md`. No code.

## Next step

Decide between "full volume" (ADR-0006 as it is) and "stacked layer over a mature FS"
(a new ADR replacing 0006). Only then start phase 2a.

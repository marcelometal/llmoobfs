# ADR-0007 — MoobFS as a layer stacked on a mature filesystem

Date: 2026-10-10. Status: accepted by the user as direction; replaces ADR-0006. Nothing
implemented.

## Context

ADR-0006 specified M2 as a full volume (block allocator, inode table,
directories, journal via jbd2). The user set the principle recorded in
[`docs/history/0009`](../history/0009-differentiation-and-principle.md): mature libraries and
filesystems are not competition but a foundation; innovation belongs in the features an LLM
memory would use; "concorrer com o git é loucura" ("competing with git is madness"). A full
volume rebuilds what ext4, XFS and btrfs solved over twenty years and delays the only question
that matters: whether the memory features have measurable value.

## Decision

1. **MoobFS becomes a native stacked filesystem** (pattern of overlayfs and ecryptfs, both
   in-tree): a kernel module mounted over a directory of a mature filesystem
   (the "lower filesystem"). It remains a native mount, without FUSE; ADR-0001 remains valid.
2. **The lower filesystem provides** blocks, journal, directories, mmap, locks, reflink/dedup,
   data checksums and volume snapshots. **The layer provides** only what is memory-specific:
   validated typed attributes, attribute index, `QUERY`, identity and provenance of
   memories (M3), temporal query (M3) and verification of what is memory-specific.
3. **Data remains intact and readable without the layer.** Content is ordinary files of the
   lower filesystem; attributes are real xattrs of the lower with versioned encoding. Unmounting
   MoobFS locks nothing in: `grep`, `getfattr`, `git` and `rsync` keep working in the
   lower directory. This is a requirement, not a side effect.
4. **Explicit interface with the lower filesystem**, through capabilities announced at mount
   (does it have reflink? data checksums? snapshots?). The layer degrades honestly on ext4 and
   uses the most on btrfs or XFS. Test targets: btrfs, XFS and ext4.
5. **Git remains the content versioning tool.** The layer does not implement
   content history. Per-memory revisions in M3, if adopted, complement git with
   stable identity, per-revision attributes and temporal query; they do not replace it.
6. **The M1 format is frozen** as a concluded physical-sharing experiment. The
   M1 module, tools and tests remain in the repository and in the history. A
   filesystem of our own as the lower only returns as an alternative implementation of the
   interface of item 4, if the M4 measurements show that the lower is a bottleneck or that the
   layout limits something memory-specific.
7. **Infrastructure reuse verified on v6.12:** `fs/backing-file.c`
   (`backing_file_open`, `backing_file_read_iter`, `backing_file_write_iter`,
   `backing_file_mmap`, `backing_file_splice_*`, used by overlayfs and by FUSE passthrough)
   to delegate I/O; `vfs_clone_file_range` and `vfs_dedupe_file_range_one` for reflink;
   `vfs_getxattr`/`vfs_setxattr` for attributes; `override_creds` with the credentials of the
   mounter, like overlayfs. xfstests already knows how to test stacked filesystems (`./check --fs
   overlay` is the model).

## Note (2026-10-10): acceptance criterion without external projects

The acceptance criterion inherited from ADR-0006 cited ai-memory. User decision: MoobFS
does not depend on any external agent-memory project, in either direction. M2
acceptance uses git, SQLite and a memory client of our own; ai-memory and the other systems of the
survey (`docs/ecosystem.md`) enter M4 as benchmark objects, not as a requirement.

## Alternatives considered

- Full volume (ADR-0006): rejected by the principle above; document preserved as a
  reference for the "small ext3" form in case item 6 ever applies.
- Two paths in parallel (layer and own FS): rejected because it splits the effort and delays
  measurement; replaced by item 6, which makes our own FS conditional on evidence.
- User-space library or daemon with xattr and SQLite: meets the features, but is not a
  mountable filesystem, which contradicts ADR-0001 and removes the property that any
  program queries memories through a kernel interface.
- Patch to btrfs or XFS for an xattr index: would depend on upstream acceptance of a niche
  feature; discarded.

## Consequences

- The size of the project becomes the size of the innovation: the scale references are overlayfs
  (about 12.5 thousand lines) and ecryptfs (about 8.2 thousand), not ext4 or btrfs.
- Stacked filesystems have their own pitfalls (inode aliasing, mmap, dentry
  consistency, modification of the lower from outside). The M2 specification adopts the
  overlayfs rules: the lower must not be modified while mounted; changes from outside are
  detected at the next mount by the index rebuild.
- Atomicity between content and attributes is that of the lower (there is no transaction covering both);
  the layer documents the order and clients use `fsync`.
- The M2 specification is rewritten ([`docs/spec-m2.md`](../spec-m2.md)). The roadmap and the site
  change accordingly. ADR-0006 receives a supersession note.

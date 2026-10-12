# ADR-0005 — Memory model for agents with the Be File System as reference

Date: 2026-10-10. Status: proposed; accepted by the user as the design basis for M2/M3. Nothing here is implemented.

## Context

Journal [0004](../history/0004-llm-memory-context.md) fixed the goal: a native filesystem
for persistent memory and context consumed by LLM agents. M1 (ADR-0003) validates only
the physical sharing of bytes between inodes. What was missing was a model linking the goal to the
mechanism: what a memory is as a file, which metadata it carries, how an agent
finds it without semantic retrieval, and which part of this justifies being in the kernel.

The original proposal (`docs/native-mvp.md`, §3) treats revision metadata as an opaque block
of up to 4 KiB, interpreted as JSON by the CLI. The kernel does not understand or index them; all
structured search would stay in user space, which weakens the reason for a native filesystem
instead of an ordinary directory with a database beside it.

The Be File System (BFS, 1997, Dominic Giampaolo and Cyril Meurillon) is the closest precedent
of "filesystem as a queryable memory layer": typed extended attributes per file,
automatic per-attribute indexes in B+ trees, equality and range queries served by the
filesystem itself, metadata journaling and B+ tree directories. The design is documented
in *Practical File System Design with the Be File System* (Giampaolo, 1999).

## Decision

1. **Memory = file; revision = immutable content + typed attributes.** The metadata of a
   revision stops being an opaque block and becomes a limited set of attributes with
   name and type. Planned types: `string` (UTF-8), `int64`, `double`, `bool`, `time` (ns since
   epoch) and `raw` (bytes, not indexable). The total limit per revision remains 4 KiB; the
   maximum number of attributes and the name size will be fixed in the M2 format specification.
2. **Exposure through the standard Linux API.** Attributes are read by `getxattr`/`listxattr` in the
   `user.moob.*` namespace. The CLI can still render them as JSON. In M2, writing
   attributes happens inside the transactional `PUT`, not by standalone `setxattr`.
3. **Per-attribute indexes maintained by the module.** Attributes marked as indexable at volume
   creation get a persistent index for equality and range. The initial structure may
   be simple and sequential; an on-disk B+ tree is the target when the M1 limits are removed.
   The index update is part of the same transaction that publishes the revision.
4. **Query interface through ioctl, not through a virtual directory.** The VFS has no
   attribute-query call; BeOS had its own syscall. A `QUERY` command is adopted on the
   `.control` file (same UAPI proposed in `native-mvp.md` §5), which receives a limited
   predicate (attribute, operator, value, conjunction) and returns `(memory_id, revision)` pairs.
   A BeOS-style virtual query directory remains a future alternative, not adopted.
5. **Semantic retrieval remains in user space.** The kernel does not compute embeddings,
   does not compare similarity and does not interpret natural language. The agent's application uses the
   structural queries to reduce candidates and assembles the context on its own.
6. **Metadata journaling as the M2 durability model.** The BFS transaction log
   design (metadata in the journal, data outside it) is the starting reference for
   the write, failure and recovery specification, to be detailed in its own ADR with
   power-loss tests.
7. **B+ tree directories replace the flat namespace** when the M1 limit of 128 entries
   is removed. This is a consequence, not an immediate goal.

Items 1 to 5 refine section 3 of the original proposal; the proposal is preserved and this ADR
points to it. ADRs 0001 to 0004 remain valid.

## Reuse of BFS code

Evaluated and decided as **do not reuse code**, only the design:

- `fs/befs` in upstream Linux (GPL-2.0-only, maintained by Luis de Bethencourt and Salah Triki)
  is a **read-only** driver and, according to its own Kconfig, does not make attributes
  or indexes available. That is, it does not implement exactly the part of interest. Its license is
  compatible with MoobFS's, and `fs/befs/btree.c` serves as a reference for reading an on-disk B+ tree
  with the buffer heads API, but MoobFS uses iomap and its own format.
- Haiku's implementation (OpenBFS, MIT license, C++) is complete, with writes, attributes,
  indexes and queries, but depends on Haiku's kernel API. Porting it to the Linux kernel
  would be rewriting; it is the best reference for behavior and for test cases.
- Using the on-disk BFS format as the MoobFS format was discarded: it has no content dedup,
  revisions or snapshots, and would inherit limits and decisions from 1997 without the idempotent
  transaction with `request_id` that the proposal requires.

## Alternatives considered

- Keep opaque metadata and index in user space (SQLite beside it). Simple, but makes
  the native filesystem a mere byte repository; rejected for contradicting ADR-0001.
- Expose queries as a virtual directory (`/mnt/moob/queries/<predicate>/`). Elegant for
  POSIX tools, but creates dynamic directory entries with semantics hard to
  maintain under concurrency and snapshots. Postponed.
- Index content (full text) in the kernel. Out of scope: it is semantic processing.

## Consequences

- The M2 format specification needs to define: attribute encoding, list of
  indexable attributes, index structure, `QUERY` UAPI layout and validation rules
  for invalid inputs (names, types, sizes, predicates).
- M2 tests must cover equality and range queries, indexes after power
  loss and rejection of malformed predicates, in addition to what the original proposal already requires.
- The roadmap gains an explicit dependency: M2 includes attributes and an index, not just writes.
- No performance gain or usefulness for LLMs is claimed until M4 measures against baselines.

## References

- Giampaolo, D. *Practical File System Design with the Be File System*. Morgan Kaufmann, 1999.
- [Be File System — Wikipedia](https://en.wikipedia.org/wiki/Be_File_System)
- [BeOS filesystem for Linux — kernel documentation](https://docs.kernel.org/filesystems/befs.html)
- `fs/befs/Kconfig` and `MAINTAINERS` in upstream Linux, consulted on 2026-10-10.
- [Haiku BFS](https://github.com/haiku/haiku/tree/master/src/add-ons/kernel/file_systems/bfs)

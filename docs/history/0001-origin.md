# 0001 — Origin and definition of the project

Journal date: 2026-10-10, America/Sao_Paulo. Brief retrospective journal of the conversation.

## Goal that emerged

The user proposed sharing storage addresses between files. The conversation distinguished hard links, reflinks, deduplication and copy-on-write. The intended use was then clarified: persistent LLM memory, at the filesystem level.

## Evolution and decisions

1. Space savings were distinguished from model context savings: sharing bytes on disk does not automatically reduce the tokens delivered to the LLM.
2. An initial proposal used FUSE with a memory engine and SQLite. The user asked for an alternative without FUSE.
3. A local library/engine was considered. The user clarified that mounting as a filesystem is a requirement; that alternative does not satisfy the project.
4. A native Linux module was chosen, with its own format on a block device and conventional reading through the VFS.
5. The full proposal was saved in `docs/native-mvp.md`, with revisions, snapshots, deduplication, transactions and recovery. It is a proposed architecture, not a completed implementation.
6. The user authorized implementing the first milestone: a MoobFS image and a module that mounts and reads two files sharing content.
7. Before the implementation, the user asked for a history of the decisions, changes and construction of the project. The user said they will create a GitHub repository; the screenshot shows a creation screen, it does not prove creation or publication.

## State when starting implementation

The local directory contained only the README and the MVP proposal, without Git initialized or code. The work environment is macOS arm64, with Docker Desktop Linux arm64 available. Validating the module requires a separate Linux.

## References

- [ADR-0001](../decisions/0001-native-filesystem.md)
- [ADR-0002](../decisions/0002-project-history.md)
- [Original proposal](../native-mvp.md)

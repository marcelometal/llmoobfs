# 0005 — Memory model and the Be File System as reference

Date: 2026-10-10, America/Sao_Paulo.

## Request

Resume the project distinguishing the goal (memory/context for LLMs) from the implemented M1.
On resuming, the gap was pointed out: no document linked the goal to the mechanism. The Be File
System was adopted as the design reference for an ADR, with an explicit evaluation of reusing
BFS code.

## Decision

ADR-0005 records the model: memory as a file, revisions with typed attributes exposed via
xattr, per-attribute indexes maintained by the module, structured query through ioctl and semantic
retrieval in user space. BFS metadata journaling becomes the reference for M2.

On reuse: the Linux `fs/befs` driver is read-only and implements neither attributes nor
indexes (confirmed in the upstream Kconfig); Haiku's BFS is complete but tied to Haiku's API.
It was decided to reuse the design and the test cases, not the code.

## Changes

- `docs/decisions/0005-memory-model-bfs-reference.md` (new).
- `docs/history/README.md`, `CHANGELOG.md` and `ROADMAP.md` updated.

## Evidence

Sources consulted in this session: the Wikipedia article, `Documentation/filesystems/befs.rst`,
`fs/befs/Kconfig` and the `BEFS FILE SYSTEM` entry in upstream Linux `MAINTAINERS`.
No code changed; no test was executed nor is claimed as executed.

## Limitations and next step

The ADR is design, not implementation. Next step: specify the M2 format (attributes,
index, `QUERY` UAPI, journaling) in its own document, with invalid-input cases,
before any write code. M1 hardening proceeds independently.

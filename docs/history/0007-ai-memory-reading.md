# 0007 — Technical reading of ai-memory and relative position

Date: 2026-10-10, America/Sao_Paulo.

## Request

Understand what `akitaonrails/ai-memory` can contribute and whether MoobFS would be a storage
option for it; a complete reading, including the code, covering the relative position
of the two projects and the right moment for contacting the maintainer.

## Method

Shallow clone in a scratch directory, direct reading of README, ARCHITECTURE, design-decisions,
vector-backend-policy, temporal, backup, wiki-migrations and comparison; three code readings
in parallel (store + core, wiki + git + watcher, hooks + consolidate + llm + mcp). Nothing was
compiled or executed. Repository metrics read from the GitHub API on that date.

## Conclusions

- ai-memory is an application: it occupies the "client" role of ADR-0005. The source of truth is a
  `wiki/` directory of markdown in git; SQLite is a derived index. There is no backend abstraction.
- MoobFS fits underneath `wiki/`, but today it cannot host it: ai-memory needs
  POSIX writes, a writable `.git` and SQLite with WAL. This imposes POSIX writes on M2, not only
  ioctl. Recorded as a requirement to carry into the M2 specification.
- Git already deduplicates identical blobs at commit and already versions. MoobFS's gain needs to be
  demonstrated: dedup across paths without a commit, native revisions, indexed attributes.
- Integration path without changing their core: admission webhooks mirroring each
  page via `PUT` into MoobFS.
- Contacting the maintainer makes sense only after a working M2, with numbers. Three doc/code
  divergences found can serve as a verifiable first contact.

## Changes

- `docs/research-ai-memory.md` (new), a line in `docs/ecosystem.md`, history index,
  `CHANGELOG.md`.

## Limitations

Code reading without execution; line counts and test proportions are estimates by
position of `mod tests`. Version read: v2.6.3; the project changes fast.

## Next step

M2 specification incorporating the POSIX write requirement and the use case "host
ai-memory's `wiki/`", before code.

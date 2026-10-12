# ai-memory (akitaonrails) — technical reading and relation to MoobFS

Date: 2026-10-10. Source: shallow clone of `github.com/akitaonrails/ai-memory` at `385b81e`
(release v2.6.3, 2026-10-09). MIT license. Created on 2026-05-21; 9.1 thousand stars, 636 forks,
7 open issues on that date. Rust 2024, workspace with 10 main crates and 5 companions.
About 396 thousand lines of Rust, of which somewhere between 40% and 55% are tests depending on the crate.
Nothing was compiled or run; the reading was of the code and the documentation.

## What the project is

Long-term memory for coding agents (Claude Code, Codex, Cursor, Gemini CLI and
twenty more harnesses), shared across agents, machines and people. A single binary
runs an HTTP + MCP server, captures lifecycle events via hooks, sanitizes, consolidates into
markdown pages and serves search and typed "handoffs" between agents.

Pipeline: `capture → consolidate → recall → handoff`. The default path makes no
LLM call at all; an LLM is optional for richer consolidation, reranking and a "dream" step.

## Storage model (confirmed in the code)

**Two layers, one source of truth.** Decision recorded in `docs/design-decisions.md` §3
("Option B: markdown in git is the source of truth, SQLite is a derived index").

| Layer | What it is | Where in the code |
| --- | --- | --- |
| `<data_dir>/wiki/` | Tree of `.md` files with YAML front matter, git repository (vendored libgit2) | `crates/ai-memory-wiki` (19 thousand lines, ~55% tests) |
| `<data_dir>/db/memory.sqlite` | Derived index: FTS5, entities, links, embeddings, sessions, observations, handoffs, audit | `crates/ai-memory-store` (58 thousand lines, ~37% tests), 76 SQL migrations |
| `<data_dir>/raw/` | Immutable JSONL segments of managed workstreams | `crates/ai-memory-workstream` |

Wiki layout: `<workspace-uuid>/<project-uuid>/{sessions,decisions,gotchas,procedures,
concepts,notes,_rules,_slots}/*.md`, plus `_meta.md`, `index.md`, `log.md`. The page
path defines the OKF type ("Open Knowledge Format" v0.2). The front matter carries `kind`, `tier`,
`pinned`, `expires_at`, `session_id`, `agent`, `entities`, `tags`, `last_modified_by`,
`generated.{by,at}`.

**Page write** (`Wiki::write_page`, `wiki.rs:2642-2790`): sanitizes, goes through
optional admission webhooks, writes the file atomically (tmp + rename), upserts into
SQLite; if the upsert fails, it restores the old bytes. The git commit is explicit
(`commit_all`), called at SessionEnd, consolidation and checkpoints. The commit author is always
`ai-memory <ai-memory@local>`; there are no branches, remotes, gc or history rewriting.

**Versioning** (`pages`): each edit creates a new row; the previous one gets `is_latest=0`
and `valid_to`, the new one points to `supersedes`. The SHA-256 hash of the body serves only to detect
a change at the same path: **there is no content deduplication across paths** (confirmed in
`ops.rs:1700-1770`). Decay (`decay.rs`) is a retention score per episodic page;
eviction is a soft delete with a tombstone; permanent purge after 180 days without access. Semantic
and pinned (`pinned`) pages do not decay. TTL via `expires_at` overrides `pinned`.

**Watcher** (`watcher.rs`): `notify` with a 300 ms debounce and full reconciliation every
30 s. It does not distinguish its own writes from human edits: it reindexes everything and relies on the
hash + front matter short-circuit in the store to avoid creating a version. The function that captures the inode in
`atomic.rs` exists but its return value is discarded; the documented inode-based suppression mechanism
is not in use. File removals are not reconciled by default.

**Concurrency:** a single SQLite writer on a dedicated thread (`WriteCmd` via mpsc), WAL,
reader pool. One server per `data_dir`, protected by `flock` in the CLI.

**Backend abstraction: there is none.** `rusqlite` called directly across 29 thousand lines of
`ops.rs` + `reader.rs`; `Wiki` is a concrete struct over `std::fs` with a root fixed at
`data_dir/wiki`. The only extension points are the `LlmProvider` and `Embedder` traits, the
**admission webhooks** (`docs/admission-webhooks.md`) and the companions policy
(integrate via HTTP/MCP, never patch the core). No mention of FUSE, mount, alternative
filesystem, Postgres, LanceDB, Qdrant or "storage layer" as a plan.

## Retrieval

`memory_query` fuses up to four streams via RRF: FTS5 over title+body, matching of
front matter entities, link neighborhood and cosine over `page_embeddings`. Local
embeddings by default since 2.0 (`all-MiniLM-L6-v2`, 384 dims, candle, ~87 MB). Vectors are BLOBs
in ordinary rows, brute-force cosine; `sqlite-vec` is deferred with numeric criteria
(`docs/vector-backend-policy.md`). Post-fusion, an authority multiplier [0.55, 1.50]
favors rules, decisions and procedures. `as_of` queries historical versions
("bi-temporal-lite", ingestion time). 23 MCP tools.

Published benchmark: LongMemEval-S hit@5 0.823 with local embeddings, 0.668 without an LLM; reproducible
harness in `evals/` and `docs/benchmarks/`. Measured write ceiling: ~700 observations/s.

## Relation to MoobFS

**ai-memory sits one layer above.** It is exactly the role that ADR-0005 calls
"client": capture, semantic selection, context assembly. Its source of truth is a
directory of files. MoobFS's natural place is underneath `wiki/`.

**What is already solved there and we should not duplicate:** secret sanitization, handoff
protocol, consolidation, hybrid search, decay, multi-user, integration with 20+ harnesses.

**What git + SQLite already provide and MoobFS needs to beat to justify itself there:**

- Git objects are content-addressed: there is already dedup of identical blobs and revision
  history, with a commit per session. The `supersedes` chain in SQLite duplicates this in the index.
- FTS, graph and vectors live in the derived, rebuildable SQLite.

**What MoobFS could offer that is different:**

- Content dedup **across paths and without a commit**, which ai-memory does not do in the store and
  git only does at commit time.
- **Native** revisions and snapshots, without the double bookkeeping of git + `supersedes`.
- The front matter (`tier`, `pinned`, `expires_at`, `type`, `session_id`, `agent`, `entities`)
  maps directly onto ADR-0005's typed attributes; `QUERY` by attribute would replace
  part of the entity index and of the tier/TTL filters that are SQL today.

**Current incompatibilities, which change M2 decisions:**

1. ai-memory writes with `std::fs` (tmp + rename + fsync), libgit2 writes `.git/`, SQLite
   requires WAL and POSIX locks. M1 is read-only; the original M2 proposal provides for writing
   **only via the `PUT` ioctl**. To host `wiki/` unchanged, M2 needs ordinary POSIX
   writes (`write`, `rename`, `fsync`, `unlink`), or ai-memory needs an adapter.
2. `confinement.rs` rejects symlinks and reparse points and validates `.git` inside the root; a
   mount needs to behave like an ordinary directory.
3. M1 limits (128 files, 64 KiB, flat namespace, ASCII) are insufficient: pages
   use UTF-8 in UUID-nested paths and bodies of up to 16 KiB per observation, but
   long sessions and consolidated pages exceed that.

**Integration point without touching the core:** the admission webhooks are called before
every write, removal, move and handoff, and can observe or mirror the page to an
external system (`git-mirror` example in the documentation). A `moobfs-mirror` webhook that does a
`PUT` of each page with attributes derived from the front matter would allow testing MoobFS as a
store of real memories **without POSIX writes and without changing ai-memory**. It is the
shortest path for M4 to have real data and workloads.

## Recommended sequence

1. The M2 specification includes an explicit requirement: host `wiki/` + `.git` + SQLite of a
   real client that uses `std::fs`. This implies POSIX writes, not just ioctl.
2. Prototype of an admission webhook mirroring pages into MoobFS (minimal M2) to collect a
   corpus and measure real dedup across pages.
3. M4 uses the `evals/` harness and the ~700/s ceiling as baselines; compare `wiki/` on
   ext4 versus MoobFS with identical workloads.
4. Contact the maintainer only with a concrete result in hand. As a good-faith first
   contact, three doc/code divergences found in this reading could become small,
   verifiable issues: the `atomic.rs` inode is never consulted by the watcher; `generated.by =
   human:<user>` documented in `okf.md` but not implemented (the watcher uses `author_id: None`);
   the store crate's description cites "sqlite-vec" while the policy defers it.

## Files to read first in their repository

`docs/ARCHITECTURE.md`, `docs/design-decisions.md`, `docs/admission-webhooks.md`,
`docs/companion-crates.md`, `docs/okf.md`, `docs/vector-backend-policy.md`,
`crates/ai-memory-wiki/src/{wiki,git,watcher,atomic,confinement}.rs`,
`crates/ai-memory-store/src/ops.rs` (upsert at 1700-1870), `migrations/V01__init.sql`.

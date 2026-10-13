# 0070 — M3 phase 3a: the client contract

Date: 2026-10-13. Status: document written; no code changed. Local series (after pull request 57).

## Goal

Phase 3a of `docs/spec-m3.md`: the identity and provenance convention documented end to end. The
maintainer also set a constraint on 2026-10-12: nothing may be specific to the one client tested;
the project is for any agent-memory system.

## What existed

The uniqueness check of `(namespace, memory_id)` is in `tools/moobfs-check.c` and `vm/suite.sh`
tests it (unique ids, a hard link, a duplicate). The convention itself was spread over spec-m3 §2,
spec-m2 §6, spec-query-v2 §2 to §4, `architecture-overview.md` §7 and `distribution-strategy.md`
§2.4; a reader had to assemble it, or copy what the ai-memory test tools do.

## Decision

One document, `docs/client-contract.md`, that names no client: whether a system fits, scope as a
directory, the permission arrangement that satisfies the homogeneity rule, typed attributes,
identity and provenance, the write sequence and the query surface, and what stays in the client's
own store. Every rule says whether the kernel enforces it, a tool checks it, or the client keeps
it. It decides nothing new: two sections (detection with fallback, a second client) are marked as
proposals from `docs/adoption-risks.md`.

Alternative: extend spec-m3 §2. Rejected: identity is one part of what a client must follow, and
the specifications are written for implementers of the layer, not of clients.

## Found while writing

- spec-query-v2 §6 says `moobfs-check` lists heterogeneous files per domain. It does not: the rule
  is implemented only in the `moobfs-index --domain` oracle. A client whose files are left out of
  answers has no tool that says which and why (risk 11 of the register).
- A client confined to its domain cannot ask `GET_INFO`, which is answered on the mount root only.

## Changes

`docs/client-contract.md`; `docs/architecture-overview.md` §7 and the roadmap row no longer call
phase 3a partial; `docs/README.md` lists the document.

## Limitations

A document; nothing checks a client against it automatically. Content comparison for copies
without provenance (spec-m3 §2) stays unimplemented. Only one client has been run.

## Next step

Before phase 3c: the open ends of stage 2 (a live mirror and the measurements repeated on the
current module), then the heterogeneous-file report in `moobfs-check`.

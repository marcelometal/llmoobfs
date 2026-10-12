# 0021 — Prior-art audit and distribution strategy

Date: 2026-10-10. Branch: `m3/3b-query-authz`. Status: completed (research and proposal
documents; nothing implemented).

## Goal

Two research pieces requested before any public announcement: (1) what MoobFS can and cannot
claim as original, with prior art identified by reading code and papers; (2) how the module
would be made available and operated by real users and AI agents, without changing the scope of
ADR-0007.

## Results

- [`docs/research-originality.md`](../research-originality.md): prior-art matrix (SFS 1991,
  BFS/Haiku, overlayfs/ecryptfs, TagFS/TMSU, LiFS, YoloFS, SMFS, VexFS, Factory VFS, Agent FS,
  RAGFS, two patents), comparison by characteristic, list of what is not new, candidate
  differentiators with a classification (proven / probable / not verified), phrases to avoid and
  a proposed public positioning in Portuguese and English. Six repositories were cloned and
  read; nothing was compiled or run.
- [`docs/distribution-strategy.md`](../distribution-strategy.md): three-tier architecture
  (Core, Workspace, Integration), answers to the nine operational questions, a single `moobfs`
  CLI (proposal), complete flow, POSIX permission model, DKMS packaging with a kernel matrix,
  risks and an incremental plan (lab → technical preview → public release). Each item marked as
  EXISTS, FUTURE or DEPENDS ON M4.

## Decisions

1. The closest prior art in form is YoloFS (stacked kernel module for agents, ioctl at the
   root, 2026); in function, BFS. MoobFS does not claim "first filesystem for LLM memory" nor
   "novel attribute query". The defensible differentiator is the combination: an index of typed
   attributes in the kernel over a mature FS without altering it, `QUERY` filtered by the
   caller's POSIX credentials, and data readable without the layer. Classification:
   "probable" until a broader search (the whole YoloFS paper, Haiku's `vfs.cpp`, LKML, patents).
2. No claim of performance, broad security or readiness before M4 and items S1–S6 and S8–S10
   of the security roadmap. The list of phrases to avoid now applies to the README, the site and
   any public text.
3. The patents cited (HP US7617250B2, Dell US20230401171A1) are described technically; there is
   no legal opinion and a specialized assessment is required before any public release.
4. Distribution: one mount per host per lower filesystem, namespaces as directories; per-agent
   mounts only when index memory isolation or administration justifies it. Containers receive a
   bind mount of the subdirectory with aligned uids; `QUERY` requires the superblock root, so a
   bind-mounted subdirectory cannot query (limitation recorded, four alternatives listed, all
   FUTURE or workaround).
5. Public installation only by DKMS on kernels in the tested matrix; any other kernel is
   "unsupported" until it passes the full suite and KASAN.
6. None of the strategy is implemented now: no HTTP server, no embedded MCP, no vector database
   in the kernel, no index persistence before M4, no ADR change.

## Corrections made in review

Both documents were written before the last suite run and said the ACL and idmap/userns tests
were pending; corrected to reflect journal 0020 (they passed in the VM on 2026-10-10). Real
pending items of phase 3b: KASAN on the branch (run 38076335715 triggered) and
`chmod`/`rename`/`unlink` concurrent with queries.

## Limitations

- The dates overlayfs and ecryptfs entered the kernel and the OpenBFS license were not
  re-verified; the YoloFS and LiFS papers were read partially.
- The clone recipe in 2.4 and the read-only bind of the root in 2.5 of the strategy were not
  executed; `index_budget` against cgroup accounting not verified.

## Next step

KASAN result on the branch; test of `chmod`/`rename` concurrent with `QUERY`; PR of
`m3/3b-query-authz` into `main`; then the M4 harness (`bench/`).

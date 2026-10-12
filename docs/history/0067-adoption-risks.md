# 0067 — Adoption risks written down with an answer for each

Date: 2026-10-12. Status: document proposed, awaiting the maintainer; no code changed. Local series.

## Goal

The stage 2 numbers (journal 0065) put the layer at about SQLite's speed on ai-memory's structured
queries. With speed roughly equal, what decides adoption is what a client gains and what it risks.
The maintainer asked for the risks to be documented, each with a way out.

## Decision

A risk register, `docs/adoption-risks.md`, separate from `docs/when-not-to-use.md`: that document
tells a reader not to adopt the layer today; the register says what would have to change. Fifteen
risks, each with its evidence, what exists, an answer of a stated kind (fix, build, contain or
accept) and what remains after it. Every answer points to the roadmap item that already covers it,
or says that it is proposed here.

Alternative: extend `docs/security-roadmap.md`. Rejected: half of the risks are not security
(platform, kernel matrix, UAPI stability, query language, write atomicity, speed, bus factor).

## What is new in it

Most answers already had a place (S2, S4, S6, S9, S10, community actions 15 and 18). Proposed for
the first time:

- the layer as an optional backend: a client detects it with `GET_INFO` and otherwise keeps its own
  index, a requirement for stage 3 of the client test plan;
- rebuild at mount: progress in `GET_INFO`, then per-domain lazy build, before any on-disk format;
- per-domain shares of the index budget, with "incomplete" as a state of the domain;
- a small user-space library for unions, paging and the atomic write sequence;
- `moobfs-check` naming the files that fail the homogeneity rule, and a per-domain flag that an
  unprivileged owner can read;
- a crash-consistency test of the temporary-file, attributes, rename sequence;
- a second-reviewer rule for `layer/` and a stated response time.

## Addendum, same day: the client is any memory system

The maintainer pointed out that the project targets every system of ai-memory's kind, not that
client. The register now says so in its opening, and gains risk 15: the design fitted to the one
client tested. Its answer is a client contract any system can be checked against, stage 2 for a
second and a third system of the six surveyed in `research-client-query-patterns.md`, and a rule
that a change to the layer needs a reason valid for more than one of them.

## Sources and limits

Evidence is taken from journals 0027, 0028, 0055, 0057, 0061, 0064 and 0065, ADR-0011 and the
review of journal 0066 (`docs/research-stacked-fs-kernel-rules.md`). The three findings quoted from
that review (R-CRED-3, a crash an unprivileged user can cause; R-CRED-2; R-INODE-4) were **not
re-verified** here; the register says so where it cites them.

Nothing in the register is implemented by this step, and its order of work is a proposal: the order
agreed in journal 0059 stands until the maintainer changes it.

## Next step

Maintainer review of the register and of the proposed order; its first item is the fix of R-CRED-3
with a regression test.

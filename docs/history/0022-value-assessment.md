# 0022 — Usefulness assessment, value proposition and maturity as an adoption criterion

Date: 2026-10-10. Branch: `m3/3b-query-authz`. Status: completed (analysis; nothing
implemented).

## Goal

Answer, before any announcement, why someone would use MoobFS instead of conventional files or
SQLite, treating the maturity of the alternatives as a concrete advantage of theirs and the
kernel module as a cost and a risk to overcome.

## Result

[`docs/value-assessment.md`](../value-assessment.md): per-operation performance mapped to W1–W9,
an estimate of memory per inode and attribute derived from the structures of `layer/index.c`
(marked as an estimate), security separating what is inherited from the VFS from what the layer
adds, reliability and module maintenance cost, development agility, three use cases with a
proof metric and a no-use condition, a value proposition matrix, negative M4 results that would
knock down each argument, success criteria at four levels, a maturity section with a decision
matrix for lab, controlled production and critical production, and the final deliverable (five
benefits, five limitations, comparison with files and SQLite, conclusion).

## Decisions

1. No performance or productivity gain is claimed; everything that depends on a number stays
   "potential" or "not demonstrated" until M4.
2. Today the recommendation for controlled production and critical production is SQLite or
   conventional files; MoobFS is acceptable only in a lab and a disposable VM.
3. The capabilities that may justify adoption without beating SQLite in every benchmark are:
   POSIX authorization per query result across many uids without an authorization layer in the
   application; data readable by any Unix tool without the layer; no database process per host.
   Any of them requires a real user requirement, not a preference.
4. Code findings recorded for M3/M4: `QUERY` scans the entries of the first term and filters
   the others by the inode's list (probable disadvantage compared to a composite index; choosing
   the most selective term is a candidate improvement, no decision now); `index_used`
   underestimates the real RAM (it does not count slab rounding, xarray nodes or transient
   buffers); a budget failure when recording a path is silent; the plan's S6 scale may not
   saturate 128 MiB with 3 to 8 attributes, so the saturation measurement must reduce
   `index_budget` or increase the scale; a single rwsem per mount serializes attribute writers;
   timing channel through the number of private candidates not tested.
5. Criteria for leaving experimental: full xfstests, KASAN and syzkaller cycles, at least two
   kernel series, twelve months without a data-loss defect, independent reproduction of M4,
   security roadmap items, external review.

## Limitations

Memory estimates not measured; the comparison with SQLite assumes an adequate configuration
(WAL, composite indexes, `ANALYZE`) without running it; document with 669 lines, above the
target.

## Next step

KASAN result on the branch and PR into `main`; M4 becomes the next milestone, with the
indispensable measurements listed in the document's deliverable.

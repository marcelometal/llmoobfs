# 0095 — Direction after the user-space comparison: keep the module, remove its costs

Date: 2026-10-13. Status: decisions recorded; ADR-0012 proposed; no code. Local series (after
pull request 64).

## Decisions (maintainer)

After the M4 report (0088) and the two journals on the alternative without a kernel module
(0092, 0094), the points where that alternative is better were listed: risk to the machine,
no scan at start, pageable memory, richer queries, ease of installation. The maintainer decided:

1. **The index stays in the kernel module**, and its costs are attacked with kernel techniques.
2. **A no-module mode behind the same interface is left for the end.** The user-space baseline
   stays a measuring stick until then.
3. Order of work: the index built on demand and releasable (ADR-0012, proposed with this
   journal); hardening techniques; richer queries in a user-space library that composes
   `QUERY2` batches, with the kernel interface kept small; packaging (DKMS, signed modules,
   security roadmap S10).

## Techniques, in the order proposed

For the two measured costs: ADR-0012.

For the risk to the machine, which cannot be removed, only reduced:

| Technique | What it finds | State |
| --- | --- | --- |
| Fault injection (`failslab`, `fail_page_alloc`) restricted to the module while the suite runs | error paths that are never taken in a healthy run, like the unchecked store of journal 0083 | not started; proposed first, because it reuses the suite and the KASAN kernel |
| syzkaller on the ioctls and on `setxattr` (S3) | inputs and interleavings nobody wrote a test for | not started |
| Continuous fuzzing of the attribute encoding (S7) | parser defects | 30 s per CI run today |
| Runtime hardening (S8): refuse or mark incomplete instead of continuing; no `BUG_ON`; bounded loops with `cond_resched` | turns a defect into an error instead of a crash or a stall | partly the practice; not audited |
| A test under a Linux security module policy | interactions never exercised | not started |
| Readers from outside | what the author cannot see | requested on this date |

Note of 2026-10-13, later the same day: the maintainer accepted ADR-0012 and the order (fault
injection first, then the on-demand index).

## What is not decided

Whether a small set of query operators enters the kernel (`not equal`, a count); a persistent
index on disk; the no-module mode. Each needs its own decision record.

## Next step

Acceptance or changes to ADR-0012 by the maintainer; meanwhile fault injection in the KASAN
job, which needs no design decision.

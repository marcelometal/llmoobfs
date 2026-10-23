# ADR-0019 — Bound QUERY2 domain-build work and frequency

Date: 2026-10-23. Status: **direction accepted; mechanism proposed, not implemented**.
Estimated effort: L (a week or more) for implementation, negative tests,
performance comparison and KVM/KASAN validation. This is an estimate, not
recorded labor time.

## Context

ADR-0012 intentionally builds a domain synchronously on its first query,
after invalidation and after index-budget release. Its memory budget does not
bound the work to rebuild. ADR-0011 leaves fair sharing and resource limits
outside its promises. Journal 0182 measured a 1,000-file cold-cache build
with 163,840 read bytes and a 7,500-file build with 1,990,656 read bytes in
one KVM guest; directory renames caused seven more builds, and two users
alternately evicted each other's domains under a shared budget. These are
bounded observations, not safe thresholds for every device or workload.

The maintainer chose the resource-limit direction after these measurements:
queries that exceed a limit may fail. This supersedes ADR-0012 only where it
allowed an unbounded synchronous rebuild. Its on-demand and reclaimable
index, authorization model and successful-query semantics remain.

## Proposed mechanism

1. Before starting a build of a stale or absent domain, reserve admission
   against both a mount-wide budget and a caller-UID budget. A built, fresh
   domain consumes no build admission. Waiting on another builder must
   recheck freshness before any admission charge. Count each attempted
   build, including failed and interrupted attempts; otherwise repeated
   failure is a way around the rate limit. Charge the UID sampled from the
   original query credentials, before the mounter's credentials override
   them. A bounded UID table must fail closed when full, without losing
   existing accounting. Administrative mount-wide QUERY needs its own
   explicitly documented policy; it must not silently bypass the mount cap.
2. A single admitted build gets a cumulative work allowance across all
   retries and moved-directory rescans. Count each enumerated name before
   lookup, each directory traversal and each attribute-load attempt. Check
   an elapsed-time deadline between operations and before starting another
   batch or retry. The same allowance follows the whole `domain_ensure()`
   call; a rename or budget-release retry cannot reset it. Keep checks outside
   the index spinlock and preserve the existing signal interruption path.
3. If admission or the work allowance is exhausted, return a distinct
   documented errno, drop the partially built domain through the existing
   failure path, and copy no partial QUERY2/QUERY2_BATCH answer or cursor.
   Define whether a batch pays once per domain build, not once per subquery.
   A later call may retry after the admission window, but a domain too large
   for the work allowance remains unqueryable until an administrator changes
   the policy or splits it. Any policy knobs require explicit mount-option
   validation, remount behavior and a compatibility decision.

The admission window and work counters are enforceable bounds on repeated
scan *attempts* and operations the layer starts. Elapsed time is checked
cooperatively: one lower-filesystem operation can block beyond the deadline.
An enumerated-name or attribute-attempt cap is **not** a hard CPU-time or
physical-I/O-byte cap. `/proc/self/io` and block-device counters from the
guest are measurement evidence, not suitable authorization/accounting
counters for this policy. Do not claim a strict CPU or I/O cap until the
kernel offers a verified mechanism and tests show it includes the lower
work. The chosen direction is to bound their principal causes and rebuild
frequency, then measure the residual cost.

Numerical defaults remain undecided. Derive them from a distribution over
normal domain sizes, cold-cache runs on the supported lower filesystems and
the latency budget of a real client; test that a legitimate 7,500-file
domain remains queryable under the selected policy. Publish the chosen
numbers, error contract and mount/remount compatibility before enabling the
limits by default. The 1,000/7,500-file observations alone cannot justify a
universal CPU-time, I/O-byte or rate threshold.

## Rejected alternatives and tradeoffs

- Documentation-only acceptance leaves an unprivileged caller able to
  force repeated builds and cross-user eviction. It does not address issue
  118's measured cost.
- Updating paths in the index on directory rename could remove one trigger,
  but not cold first builds or budget-eviction rebuilds; it also changes
  concurrency and correctness behavior beyond this issue.
- An I/O-byte check based only on the calling process's `read_bytes` would
  be untrustworthy for a kernel stack and has no demonstrated attribution
  across all supported lower filesystems.

## Verification gate

Implement only with a regression that verifies successful normal builds,
explicit denial, no partial answer/cursor, recovery after the admission
window, cumulative counting across retries, separate UIDs and the mount cap.
Run a negative test with the guard removed, the complete native KVM suite,
KASAN/lockdep and a before/after query-performance comparison. Record both
the measured work reduction and the legitimate queries denied. Until then,
the production code still has the limitation described in journal 0182.

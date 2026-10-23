# ADR-0019 — Bound QUERY2 domain-build work and frequency

Date: 2026-10-23. Status: **experimental implementation in local validation; not published**.
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

## Decision and initial mechanism

1. Before starting a `QUERY2` build of a stale or absent domain, reserve admission
   against both a mount-wide budget and a caller-UID budget. A built, fresh
   domain consumes no build admission. Waiting on another builder must
   recheck freshness before any admission charge. Count each attempted
   build, including failed and interrupted attempts; otherwise repeated
   failure is a way around the rate limit. Charge the UID sampled from the
   original query credentials, before the mounter's credentials override
   them. A bounded UID table must fail closed when full, without losing
   existing accounting. `CAP_SYS_ADMIN` callers bypass frequency admission;
   the work and time limit still applies to each build. Legacy mount-wide
   `QUERY` with `domain_depth=0` also bypasses frequency admission to
   preserve its existing race and client behavior. The mount count therefore
   limits aggregate unprivileged `QUERY2` domain builds, not administrator
   activity or legacy whole-mount `QUERY`. The latter remains a residual
   availability risk on mounts that disable domains.
2. A single admitted build gets a cumulative work allowance across all
   retries and moved-directory rescans. Count each enumerated name before
   lookup, each directory traversal and each attribute-load attempt. Check
   an elapsed-time deadline between operations and before starting another
   batch or retry. The same allowance follows the whole `domain_ensure()`
   call; a rename or budget-release retry cannot reset it. Keep checks outside
   the index spinlock and preserve the existing signal interruption path.
3. If admission or the work allowance is exhausted, return a distinct
   documented errno and drop the partially built domain through the existing
   failure path. An individual QUERY2 must not publish a successful partial
   answer or cursor. QUERY2_BATCH already copies earlier subquery result
   buffers before a later failure; its existing error contract requires the
   caller to discard **all** outputs on error. Preserve and test that contract
   rather than claiming that no bytes were copied to the caller.
   Define whether a batch pays once per domain build, not once per subquery.
   A later call may retry after the admission window, but a domain too large
   for the work allowance remains unqueryable until it is split or a later
   version changes the fixed policy. No mount option is introduced in this
   experimental implementation.

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

The initial experimental constants are 32,768 combined work units per build
(one per enumerated name, directory visit or `index_load` attempt), a
cooperatively checked ten-second deadline, 16 attempts per unprivileged UID
and 64 per mount in fixed 60-second windows, and 64 UID accounting slots per
mount. Exhausting the admission count or UID table returns `EDQUOT`; the
work cap returns `E2BIG`, and the deadline returns `ETIMEDOUT`. A fresh
built domain does not consume admission. The fixed window permits boundary
bursts; it is not a sliding-window guarantee. The UID table can itself be
filled by many distinct UIDs, denying additional callers until an entry
expires. These are intentional availability tradeoffs, not fair index-memory
shares (S9). The scope restriction to `QUERY2` followed the failed full-suite
legacy-query race in journal 0182; it is an explicit compatibility choice.

Three repeated ext4 KVM probes of each measured size (journal 0182) gave
45.932–49.011 ms and 159,744–167,936 read bytes for 1,000 files, and
196.926–210.976 ms and 1,986,560–2,002,944 read bytes for 7,500 files.
The 7,500-file shape needs about 15,000 work units and a patched targeted
run passed. This supports that specific case, not universal defaults for
slower devices, btrfs/XFS or different client shapes. The constants must be
reviewed against those cases and the negative/complete suite before
publication. No on-disk format or ioctl structure changes. The query
compatibility decision is explicit: a sufficiently large or frequently
rebuilt domain that once returned results can now fail with these errnos.

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

Before publication, require a regression that verifies successful normal
builds, explicit denial and the established output-on-error contract,
recovery after the admission window, cumulative counting across retries,
separate UIDs and the mount cap. Run a negative test with the guard removed,
the complete native KVM suite, KASAN/lockdep and a before/after
query-performance comparison. Record both the measured work reduction and
the legitimate queries denied. Journal 0182 separates checks already run
from those still pending. The cooperative boundary remains a limitation
even after the tests pass.

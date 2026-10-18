# ADR-0016 — A failed index update rebuilds its domain instead of leaving the index incomplete

Date: 2026-10-18. Status: **proposed**; implemented on branch `f2-domain-rebuild` (journal 0130),
not yet run. Estimated effort: S.
Amends ADR-0011 (finding F2) and the update rule of ADR-0012; both otherwise stand.

## Context

When an index update fails after the lower write has already succeeded (in practice: the index
budget is full, or memory runs out), the module set one mount-wide flag, `incomplete`, logged one
warning and reported `MOOB_FLAG_INDEX_INCOMPLETE` in `GET_INFO`, until remount. ADR-0011 recorded
this as finding F2: one user with write access fills the budget, and the query service degrades
for every user.

The code shows the effect was worse than ADR-0011 says. The flag reached only `GET_INFO`: `QUERY`
and `QUERY2` replies did not carry it, and the domain kept being served from entries that no
longer matched the lower filesystem. A client that did not poll `GET_INFO` got answers that
silently missed files (or, for a value replaced by a failed update, kept the old one) until
remount.

ADR-0012 made the index a cache of per-domain builds: a domain can be taken down and built again
from the lower filesystem at any time, and a directory rename already uses that path
(`paths_stale`, then `domain_fresh()` is false and the next query builds the domain again).

## Decision

- A failed update marks only its own domain. A built domain gets `lost`. `domain_fresh()` is then
  false, so the next query that needs the domain takes it down and builds it again from the lower
  filesystem, releasing the domains queried least recently if the budget needs room, as any
  build does. A domain being built restarts its build (`restart`, as for a rename that could not
  be recorded). The three failure sites (`moobl_index_set`, `index_adopt`, `rec_set_partition`)
  call `domain_lost()`.
- No answer comes from a domain marked `lost`. Its next query either answers from a fresh build
  or fails with `ENOMEM` when the domain does not fit the budget even alone. In both cases the
  answer is complete or explicitly refused, never silently partial.
- The tracepoint `moobfs_index_incomplete` keeps its name and format and fires on each failed
  update. The warning is rate-limited and says the domain will be built again.
- **Compatibility.** No UAPI layout change. `MOOB_FLAG_INDEX_INCOMPLETE` (0x8) stays defined and
  reserved; this version never sets it. A client that checks it (`docs/client-contract.md`)
  keeps working: the flag is clear, and the answers it gets are complete. Kernels before this
  change still set it, and a client must keep honouring it there.

## Alternatives

- **Per-uid or per-domain budgets** (the candidate in ADR-0011). These spread the memory more
  fairly, but on their own they do not fix the silent partial answers. A per-uid share also
  needs a policy for shared domains. They stay a candidate for security roadmap S9, on top of
  this decision.
- **Carry `MOOB_FLAG_INDEX_INCOMPLETE` in query replies.** That makes the problem visible, but it
  still leaves every domain degraded until remount, when one rebuild repairs the affected domain.
- **Refuse the lower write when the index update would not fit.** That changes the filesystem's
  write semantics on an index cost and turns a full index into a denial of writes. Rejected.

## Consequences

- A user who fills the budget still makes other users' domains be released and built again
  (latency), and makes their own over-budget domain fail its queries with `ENOMEM`. The other
  domains' answers stay correct. Fair shares are S9's work.
- A `lost` domain keeps its memory until its next query takes it down, or until a build releases
  it as least recently queried.
- Test: suite step `release`. With two domains built, writes into one of them overflow the
  budget, which the tracepoint confirms. The next query of that domain must equal the oracle
  with every written file. On `main` before this change the step is expected to fail (the
  negative round); journal 0130 records both runs.

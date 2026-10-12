# 0034 — ADR-0009 accepted: per-domain `QUERY` in the kernel (alternative E)

Date: 2026-10-11. Status: decision recorded; specification as a proposal; nothing implemented.

## Decision

The maintainer accepted alternative E of ADR-0009 with the recommended adjustments: an index in
the kernel partitioned by permission domain (homogeneous namespace), `QUERY` issued on the domain
directory in a new UAPI version, ordering by a time attribute with a limit ("the N most
recent"), a small intersection of equality conditions, a batch query for "or" between
values, and the "missing or comparison" operator. Alternative B (SQLite index in user space)
is rejected as a direction for the filesystem and allowed as an optional layer of a client.

## Summary justification

Measurements from journals 0028 to 0031 and the research of journal 0033: v1 has a per-result cost
(path resolution), a per-candidate cost that grows with the size of the set, and a timing
channel between owners; real clients isolate by scope and order by time with a small limit; the
literature shows that only a search that does not traverse other domains closes the channel. E does this
without a database engine, without an extra process, keeping the index updated in the same call and the ACLs
enforced by the lower.

## Next step

`docs/spec-query-v2.md` has four points marked as decisions for the maintainer to confirm
before the code: domains by depth (`domain_depth`), extended ACLs outside the partition
in this version, v1 restricted to `CAP_SYS_ADMIN`, and `EXDEV` for directories renamed across domains.
Then, phases Q2a to Q2e, each with a pull request, suite, KASAN and measurement on Azure.

Update on the same day: the maintainer accepted the four points in the recommended forms
(`domain_depth`, extended ACLs outside the partition, v1 only with `CAP_SYS_ADMIN`, `EXDEV` for
directories across domains) and authorized the start of phase Q2a.

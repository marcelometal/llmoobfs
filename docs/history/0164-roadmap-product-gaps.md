# 0164 — Product gaps around the filesystem and semantic retrieval

Date: 2026-10-20. Status: assessment complete; recommendations only, not an
approved feature sequence or implementation.
Started and ended: 2026-10-20 (America/Sao_Paulo); exact start time and active
effort were not recorded.

## Scope and evidence

Assess the roadmap against the project's purpose and prospective users, including
an independent on-disk filesystem and vector retrieval. The source baseline is
`02381fba3f0a908623cac52180a39f24e02e98a6`, reconfirmed by fetching main.
The separate institutional plan at local commit `94dc83e` was consulted as
context; it was not imported or presented as published main. Concurrent review
work remains separate and is not certified by this assessment.

Read README, ROADMAP, the history index and latest baseline journal 0154,
ADRs 0007/0013/0014, the client contract, adoption risks, distribution strategy,
M3 specification and security roadmap. Inspect `layer/main.c`,
`layer/index.c`, `include/moobfs_ioctl.h`, `bench/client/langgraph_store.py`
and the tools/SQLite component inventory.

Evidence anchors:

- `layer/main.c` registers the filesystem. ADR-0007 already chose a native
  stacked filesystem and froze the independent M1 format as an experiment.
- `layer/index.c:moobl_root_ioctl` accepts QUERY2 on a domain descriptor but
  restricts GET_INFO to the mount root. `do_get_info` hides aggregate counters
  from non-administrators; it does not forbid all information to them.
- Client contract sections 5–8 distinguish conventional identity, publication
  by temporary file and rename, proposed fallback, and client-owned search.
- Distribution strategy section 2.7 already identifies xattr-aware backup and
  a future attribute export/automated recovery runbook.
- ADR-0014 accepts value projection but it is not implemented at this baseline.
  ADR-0013 has a SQLite virtual-table prototype. The second client's adapter
  lives under `bench/client/`, not a supported integration package.

## Audience and positioning

The strongest hypothesis is developers operating file-backed agent memory on a
managed Linux host, especially several processes with distinct operating-system
identities and a need for structured retrieval. Researchers and infrastructure
maintainers are plausible early experimenters. These are target hypotheses,
not evidence of adoption.

One service account representing many application tenants does not give POSIX
permissions a distinct identity for each tenant. A `namespace` or `agent`
attribute is metadata, not an authentication boundary. Deployment examples must
show how credentials reach the query and file-open operations.

The community purpose favors reusable contracts, reproducible experiments and
integration with existing components. It does not require owning every part of
an agent-memory stack.

## Recommendations and relation to existing plans

| Candidate | Existing coverage | Additional deliverable worth evaluating |
| --- | --- | --- |
| Complete developer integration | Client contract, query frontend, distribution proposal and benchmark adapter | A narrow client package: durable publication, paging, capability detection from an accessible domain, explicit errors and an authorization-preserving fallback; verify one adapter before expanding language coverage |
| Hybrid retrieval | Full-text/vector search deliberately belongs to the client | One optional integration that combines structured filters, lexical retrieval and semantic ranking; start in user space and compare with the same engine without MoobFS |
| Derived-index lifecycle | Client revisions/watchers; kernel change cursor deliberately deferred | Specify identities, content versions/hashes, embedding-model version, deletions, revocation, missed-event recovery and a full reconciliation path; prove need before reopening a kernel change cursor |
| Portable memory and recovery | xattr-aware backup described; export/runbook still future | Restore content, typed attributes and relevant permissions into a fresh tree; compare query answers; detect missing metadata rather than silently accepting an incomplete memory |
| Predictable shared operation | S9 fair shares, budgets, tracepoints and packaging already planned | Measure a noisy neighbor, cold-start rebuilds, saturation and user-visible diagnostics; package a documented supported setup only after its technical gates |
| End-to-end usefulness | M4 and two bounded client experiments exist | Measure time to usable context, retrieval relevance, freshness, total RAM/CPU, recovery and integration complexity for a real workflow |

These are proposed work packages, not newly approved roadmap entries. In
particular, packaging, projection, S9 and recovery must not be advertised as
previously undiscovered gaps. Their main gap is delivery and evidence.

## Vector retrieval: proposed boundary and experiment

Keep the native filesystem and its authorization model. An optional user-space
component would produce embeddings and maintain a derived vector index. File
content and typed attributes remain authoritative; the vector index must be
rebuildable. No vector backend is selected by this assessment.

A first experiment should use a small, fully enumerated eligible set within a
domain and exact vector scoring as an oracle, then evaluate scalable filtered
retrieval if the measured cost warrants it. Taking an arbitrary first page of
QUERY2 candidates loses potential relevant results. Taking a global vector
top-k and then removing unauthorized hits can return too few results. Qdrant's
official documentation explains the latter and the trade-off between exhaustive
filtered scoring and filtering during approximate traversal (references below).
It is prior art, not a claim that MoobFS inherits that engine's behavior.

Protect derived vectors, cached text and scores as sensitive data. Execute file
access under the intended credentials, revalidate access before returning
content, and specify concurrent-change behavior. A final permission check alone
does not prove absence of timing, count or ranking leaks in an external index.
Revocation cannot undo content already delivered to a process.

Test updates, renames, deletion, permission changes, a stopped/restarted watcher,
missed events and model changes. Use stable client identity plus version/hash;
path and inode alone are not a durable cross-rebuild identity. Start with
client-side reconciliation, respecting the existing decision on revisions.
The proposal is not a durable event-stream design or a new kernel transaction.

## Independent filesystem and other alternatives

No independent on-disk filesystem is recommended now. MoobFS already is a native
Linux filesystem; owning block allocation, crash recovery and a new disk format
would expand its responsibility. ADR-0007 requires evidence of a lower-filesystem
bottleneck before reconsidering that path.

Do not introduce vector math/ANN structures into the kernel without a measured
need that the user-space integration cannot address. Do not build a universal
memory framework, model-specific policies or a general graph database as part
of this assessment. The existing lower filesystems and mature search components
are foundations to reuse.

Suggested next decision: after the active correctness work, choose a bounded
integration experiment with two OS identities, private and shared domains,
structured plus semantic retrieval, revoke/delete, and restore. Measure both
correctness and whole-workflow value against files plus a user-space index.
Success is reduced integration burden or a measurable benefit without a
weaker authorization contract; failure is also a useful result.

## Changes and validation

This journal, its history index entry, a changelog note and source references are
the only intended changes. No feature, UAPI, disk format, accepted roadmap order,
security status or architectural decision changes. Other public surfaces remain
unchanged because this is a proposal recorded in history, not a change of state.

Documentation validation: `git diff --check` passed;
`sh scripts/check-retired-claims.sh` returned `RETIRED_CLAIMS_OK`;
`python3 scripts/check-site.py .` passed for 12 HTML pages.
No kernel module,
VM, benchmark, retrieval experiment or restore drill ran in this assessment.
No performance, adoption, portability or confidentiality improvement is claimed.

## Sources

- Existing project sources and decisions linked above; prior findings retain
  their original evidence and limitations.
- Qdrant, [Filtering in Vector Search](https://qdrant.tech/documentation/search-patterns/vector-search-filtering/)
  and [Indexing](https://qdrant.tech/documentation/manage-data/indexing/),
  consulted 2026-10-20 via the official site and Context7 (`/websites/qdrant_tech`).
  Thanks to the authors for documenting the filtered-retrieval trade-offs.

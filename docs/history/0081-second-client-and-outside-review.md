# 0081 — Second client chosen; outside review prepared; three inspection reports checked

Date: 2026-10-13. Status: decisions recorded; nothing run. Local series (after pull request 58).

## Second client: the LangGraph store

Decision (maintainer): the second real client is the LangGraph store, the `BaseStore` interface
of `langchain-ai/langgraph`, on which LangMem is built.

Why this one, from the survey in `docs/research-client-query-patterns.md` §5 (code read on
2026-10-11, not re-read today):

- It has an official extension point: a store is a subclass of `BaseStore`. The adapter needs no
  fork and lives in `bench/client/`, as the client-neutral rule requires.
- Its namespace is a tuple that maps to a directory path, and its own authorization example puts
  the user's identity first in the tuple, which is what a domain directory is.
- Its filters are a conjunction of `$eq`, `$ne`, `$gt`, `$gte`, `$lt`, `$lte` with a limit, which
  is close to one `QUERY2` request.
- `InMemoryStore` is a reference implementation in the same package: it is the oracle for the
  equality test, in the role SQLite had with the first client.

Alternatives considered: Letta Code (already stores memory in files, but hardly queries by
field, so it would test little of `QUERY2`); mem0 (not file-based; its filters use OR, NOT and
`contains`); Graphiti and the OpenAI sessions (a graph and a session table; outside §1 of the
client contract).

Known mismatches, which are the reason for the test and must be reported as results, not
smoothed over in the layer:

1. A namespace prefix can be searched at any depth; the layer isolates at one fixed depth.
2. `$ne` has no `QUERY2` operator.
3. The Postgres store compares range values as text and the in-memory store as floats; the
   layer compares typed values.
4. Pagination is `limit` and `offset`, ordered by update time, where the layer has a cursor.
5. Search with a text query is vector search and stays in the client.
6. Values are JSON documents; which keys become typed attributes is the adapter's choice.

Plan, in the stages of `docs/client-test-plan.md`: pin a commit and re-read the interface; check
it against client contract §1; write the adapter; run the store's own tests on it; run the
equality test against `InMemoryStore`; measure. It comes after the M4 report in the agreed order,
with the other open proposals, before stage 3 of the first client.

## Outside review

Decision (maintainer): ask for outside review now (community action 15). The repository already
has two open review-request issues (16, threat model; 17, concurrency of the index), written on
2026-10-11, before ADR-0011 and before this week's fixes.

To do, in this order: refresh the two issues so they point at ADR-0011, the review table of
journal 0079 and the findings F1 to F5; send the requests to the places the maintainer chose
(the Brazilian kernel study groups first, then the overlayfs list with one narrow question on the
credential model). Messages go out under the maintainer's name and are sent by the maintainer;
drafts are prepared in the session. A reply is recorded as an issue comment and a journal entry,
never as "reviewed".

## Inspection reports checked on this date

The maintainer ran three separate inspection sessions and brought their reports. What each
point came to, after reading the code and the 6.12.112 source:

| Report | Point | Verdict |
| --- | --- | --- |
| 1 | `GFP_ATOMIC` in the query path | Led to a probable defect: allocations made with the index lock held can recurse into reclaim, which evicts inodes and takes the same lock. Being fixed and tested in the next journal |
| 1 | no `__GFP_ACCOUNT` | True; a design question for S9, not changed |
| 1 | one lock for the whole index | True and known; unmeasured under concurrent writers; for the M4 report |
| 1 | no `lockdep_assert_held` | Added with the fix |
| 1 | no `MODULE_AUTHOR` in the M1 module | Cosmetic; added with the fix |
| 2 | `vfs_getattr_nosec`, direct `->fiemap` and `->flush` | Same as overlayfs in 6.12 (journal 0079) |
| 2 | SELinux never tested | True; proposed as a new security roadmap item |
| 3 | 11 allocations not zeroed | Checked the five copies to user space: every buffer is zeroed or filled whole from the caller first. No leak |
| 3 | no `MODULE_VERSION` | True; added with the fix |
| 3 | no `pr_debug`, tracepoints or debugfs | True; proposed as a roadmap item, not a defect |
| 3 | `docs/security-roadmap.md` partly in Portuguese | Known, issue 38 |

## Next step

The index lock fix; then the refreshed review issues and the drafts.

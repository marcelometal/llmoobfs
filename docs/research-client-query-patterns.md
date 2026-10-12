# Structured query patterns in memory systems for agents

**Abstract (English).** We read the source of six LLM-agent memory systems (ai-memory, mem0,
Letta in its archived V1 server and in the current Letta Code, Graphiti, the LangGraph store
with LangMem, and OpenAI Agents SDK sessions) to list every structured metadata filter they send
to their stores, separately from vector similarity. Almost every query carries one to three
equality predicates that express scope (user, agent, run, project, namespace), and the
dominant second need is a time range or an ordering by time with a small limit (5 to 100).
Three needs fall outside a kernel query limited to ANDed eq/lt/le/gt/ge/prefix/exists without
ordering: IN/OR lists (message roles, tag "any", kinds, group lists, categories), "missing or
greater than" tests on validity and expiry fields, and ordering by `created_at`/`updated_at`
with limit and cursor or offset. Counts appear in ai-memory and Letta, but in pagination and
administration, not in the retrieval path. Every system pushes the structured filter into the
same engine that ranks by vector or full text, before ranking. Isolation is almost always a
mandatory scope predicate or a namespace prefix enforced by the application, not by the store;
cross-scope queries exist in five of the six systems. Design E (simple kernel query, index
partitioned per permission domain) covers scope by construction and the conjunctive core;
design B (user-space SQLite index) covers the whole observed surface, including FTS, but
moves authorization out of the kernel.

Date: 2026-10-11. Method: shallow clones (`git clone --depth 1`) in a temporary directory, reading
of code and documentation; no code was executed, no dependency installed. The
citations are paths relative to the root of each repository.

| System | Repository | Commit read | Commit date |
| --- | --- | --- | --- |
| ai-memory | akitaonrails/ai-memory (main) | `88f8804` | 2026-10-10 |
| mem0 (OSS) | mem0ai/mem0 (main) | `b7ad69a` | 2026-10-07 |
| Letta V1 (retired server) | letta-ai/letta, branch `archive` | `56ba9c2` | 2026-08-13 |
| Letta Code (current) | letta-ai/letta-code (main) | `271f119` | 2026-10-10 |
| Graphiti | getzep/graphiti (main) | `964bb11` | 2026-10-10 |
| LangGraph store | langchain-ai/langgraph (main) | `799c2f0` | 2026-10-11 |
| LangMem | langchain-ai/langmem (main) | `48e3c11` | 2026-10-02 |
| OpenAI Agents SDK | openai/openai-agents-python (main) | `125efa0` | 2026-10-08 |

The main branch of `letta-ai/letta` (`5bcdd17`) no longer has the server: the README points to
`letta-code` and to the `archive` branch, which keeps the V1 server with archival and
recall memory. Both were read because they represent different models.

## Reference: what the MoobFS `QUERY` offers today

`include/moobfs_ioctl.h`: up to 4 terms (`MOOB_QUERY_MAX_TERMS`) in conjunction; operators
`EQ, LT, LE, GT, GE, PREFIX` (strings only), `EXISTS`; up to 4096 results
(`MOOB_QUERY_MAX_LIMIT`); output with `count` (visible results copied) and `truncated`; no
OR, NOT, ordering, cursor, total count or aggregation. One value per attribute name per
file, at most 64 typed attributes per file (`docs/spec-m2.md`). Each result is
authorized with the caller's credentials (`docs/spec-m3.md` §3.1).

## 1. ai-memory

Source of truth in markdown with front matter; the SQLite (`crates/ai-memory-store`) is a derived
index. In other words, the project is already an instance of alternative B.

- **Main search** (`reader.rs`, `search_page_candidates_for_project`, line 2329): FTS5
  `MATCH` + `workspace_id = ? AND project_id = ?` + `is_latest = 1` + `not_expired`, which is
  `(expires_at IS NULL OR expires_at > now)` (`not_expired`, line 51). Four structured
  predicates, one of them with an absence OR. `ORDER BY pages_fts.rank LIMIT ?`, with
  over-fetch `limit*4`, minimum 20, maximum `limit+300` (`authority_candidate_limit`, line 474).
- **Time travel** (`search_page_candidates_for_project_at`, line 2426):
  `valid_from <= T AND (valid_to IS NULL OR valid_to > T)` + scope + `not_expired`: five
  predicates, two with an absence OR.
- **Recent** (`recent_pages_for_project`, line 2867): scope + `is_latest` + `not_expired`,
  `ORDER BY updated_at DESC LIMIT ?`. The global version (`recent_pages_global`, line 2936) swaps the
  scope for the user's read filter.
- **Session observations** (`session_observations_scoped`, line 3032): `session_id`,
  `workspace_id`, `project_id`, optional FTS and `kind IN (...)`; a total `COUNT(*)` for
  pagination, a second `COUNT(*)` with `NOT (workspace_id = ? AND project_id = ?)` to report
  how many were omitted, and `ORDER BY created_at {ASC|DESC}, id LIMIT ? OFFSET ?`.
- **Handoffs and messages** (lines 5252, 6113, 6153): `workspace_id, project_id, state='open'`
  or `state='pending'`, `ORDER BY created_at ASC LIMIT`; `pending_message_count` is `COUNT(*)`.
- **Vectors** (`load_embeddings`, line 4217): scope + `is_latest` + `provider`, `model`, `dim`
  (five equalities); loads the project's vectors and computes similarity in the process. The filter
  runs before the vector and could be served by a separate attribute index.
- **Isolation**: mandatory `workspace_id`/`project_id` predicate; for multi-user reads,
  `readable_repository_sql` (line 165) adds an OR of three subqueries (project not
  restricted or created by the user, grant in `project_grants`, global scope). The comment
  in `readable_repository_filter` (line 78) requires filtering before the `LIMIT`, not after.
  Cross-scope queries are an explicit feature: `scopes` (list) and `global=true` in
  `memory_query` (`crates/ai-memory-mcp/src/server.rs`, arguments near line 585).
- **Sizes**: `memory_query` default 10, maximum 100 (`server.rs` lines 1750 and 2683); handoffs and
  messages default 50, maximum 200; observations default 50, maximum 200.

## 2. mem0 (OSS SDK)

- **Scope** (`mem0/memory/main.py`, `_build_filters_and_metadata`, line 314): at least one of
  `user_id`, `agent_id`, `run_id` is mandatory (error `VALIDATION_001`); `actor_id` optional. That is
  1 to 4 equalities. `get_all` (line 1269) and `search` (line 1393) repeat the requirement.
- **Extra filters** (`_process_metadata_filters`, line 1538; `search` docstring): `eq, ne,
  in, nin, gt, gte, lt, lte, contains, icontains`, wildcard `*`, and nestable `AND`/`OR`/`NOT`.
  Sibling keys are an implicit AND. The translation is left to each vector store: Qdrant
  `_create_filter` (`mem0/vector_stores/qdrant.py` line 339); pgvector builds
  `payload->>k = ANY(...)`, `( ... OR ... )` and `NOT (...)` (`_build_filter_conditions`).
- **Hosted platform** (`docs/platform/features/v2-memory-filters.mdx`): `user_id`,
  `agent_id`, `app_id`, `run_id` accept `eq, ne, in, *`; `created_at`, `updated_at`,
  `timestamp`, `expiration_date` accept ranges and `in`; `categories` accepts `in` (any one)
  and `contains`. Documented examples: `{"AND": [{"user_id": ...}, {"categories": {"in":
  ["finance","health"]}}]}` and `created_at >= ...`.
- **Ordering and pagination**: `search` orders by combined score (semantic, BM25, entities)
  with `top_k` default 20 and internal over-fetch `max(limit*4, 60)` (`_search_vector_store`, line
  1642). `get_all` does not order: Qdrant uses `scroll` without a requested order (`qdrant.py` line 573),
  pgvector does `SELECT ... WHERE ... LIMIT` without `ORDER BY` (`pgvector.py` line 513). The
  platform paginates with `page`/`page_size` (`mem0/client/main.py` line 397). No count in the
  OSS SDK. Expiry is filtered in Python after the search (`_payload_is_expired`), hence the
  over-fetch.
- **Other reads**: in `add` (line 920 onwards) it fetches up to 10 existing memories with only the
  scope keys; the conversation history comes from local SQLite via `session_scope = ?
  ORDER BY created_at DESC LIMIT 10` (`mem0/memory/storage.py` line 298); the entity
  store is listed with `top_k=10000` (lines 589 and 669); `delete_all` lists in batches of 1000.
- **Time of the filter**: together with the vector, inside the vector engine (Qdrant pre-filter,
  pgvector `WHERE`). Qdrant creates payload indexes only for `user_id, agent_id, run_id,
  actor_id` (`_create_filter_indexes`, line 166).
- **Isolation**: mandatory equality predicate, in the same collection. Omitting a key
  crosses the other scopes (only `user_id` searches all of the user's agents and runs); the
  platform documents wildcard `*` and `in` on `user_id`. The store authorizes nothing.

## 3. Letta

**V1 (branch `archive`).** Archival memory (`passages`) and recall memory (`messages`) in
Postgres/SQLite, with optional Turbopuffer.

- `build_agent_passage_query` (`letta/services/helpers/agent_manager_helper.py` line 1180):
  `organization_id = ?` always; `agent_id` via the `archives_agents` join or `archive_id`; with
  neither of the two, it searches the whole organization. `created_at >= start` and `<= end`; text via
  `lower(text) LIKE %q%`. Up to five predicates in AND. Cursor pagination `before`/`after`
  with `(created_at, id)` (lexicographic OR) and `ORDER BY created_at ASC|DESC, id`; with a vector,
  `ORDER BY cosine_distance`.
- `query_agent_passages_async` (`letta/services/agent_manager.py` line 2416): `limit` default 50;
  tags filtered **after** the SQL, in Python, in ANY (intersection) or ALL (subset) mode,
  with a TODO to move it to SQL. The agent's tool uses `RETRIEVAL_QUERY_DEFAULT_PAGE_SIZE
  = 5` (`letta/constants.py` line 458).
- Turbopuffer (`letta/helpers/tpuf_client.py`): passages with `tags ContainsAny` (ANY) or an AND
  of `Contains` (ALL) and `created_at Gte/Lte` (lines 955 to 1013), one namespace per archive
  (archive); messages with `agent_id Eq` always, `role In [...]`, `project_id`, `template_id`,
  `conversation_id`, `created_at` range (lines 1111 to 1181), one namespace per organization.
  Without search text, it orders by `created_at desc` (line 840).
- Recall (`conversation_search`, `letta/services/tool_executor/core_tool_executor.py`):
  `roles` (list), `start_date`, `end_date`, default limit 5; the SQL path
  (`message_manager.py`, `search_messages_async` line 1142) uses `list_messages` with `roles`,
  text and `ascending=False`.
- Counts: `agent_passage_size_async` (`COUNT`) and `get_tag_counts_for_archive_async`
  (`GROUP BY tag`) in `letta/services/passage_manager.py` lines 955 and 1074, used by the API,
  not by the agent's retrieval.

**Letta Code (current).** The agent's memory is a tree of markdown files with restricted front matter
(`description`, `read_only`, `limit` in the old format; `name`, `description` in
memfs-v2; `src/memory-frontmatter.ts`), versioned in git, in `<root>/<agentId>/memory`
(`src/utils/local-backend-paths.ts`). Selection is by path prefix (`system/` goes into the
prompt; `src/backend/local/system-prompt-compilation.ts` line 333). Isolation between agents
is done by a filesystem sandbox in the kernel, failing closed
(`src/memory-confinement.ts`). I did not find a structured query over attributes in this code;
search is done by the agent with file tools.

## 4. Graphiti (Zep)

- `SearchFilters` (`graphiti_core/search/search_filters.py`): `node_labels`, `edge_types`
  (`IN`), `edge_uuids`, `edge_source_node_uuids`, `edge_target_node_uuids` (`IN`),
  `valid_at`, `invalid_at`, `created_at`, `expired_at` as a list of lists of `DateFilter`
  (outer OR of inner ANDs, with `=, <>, >, <, >=, <=, IS NULL, IS NOT NULL`) and
  `property_filters`. When building the date parameters, the name is `valid_at_{j}` without the index of the
  OR group; with more than one group the values seem to overwrite each other. I did not test it; I record it as a
  reading observation.
- The MCP server only emits AND: `build_fact_search_filters`
  (`mcp_server/src/utils/type_config.py` lines 139 to 194) generates a single group with `>=` and/or
  `<=` for `valid_at` and `invalid_at`, plus `edge_types`.
- **Scope**: `group_id IN $group_ids` in every search (`search_utils.py` lines 228, 336, 474);
  in fulltext, `group_id:"a" OR group_id:"b"` (line 85). `group_ids=None` does not filter: the search
  covers the whole graph. In FalkorDB each `group_id` is a separate graph and the decorator
  `handle_multiple_group_ids` (`graphiti_core/decorators.py` line 29) runs one search per
  group and merges the results: physical partition by scope with fan-out, the same design as E.
  The MCP uses the configuration's default `group_id` when the caller does not provide one
  (`search_memory_facts`, `graphiti_mcp_server.py` line 616). There is no authorization: the
  caller chooses the groups.
- **Ordering and limits**: hybrid search by RRF or node distance, `DEFAULT_SEARCH_LIMIT = 10`
  (`search/search_config.py`), `max_facts` default 10 in the MCP. `retrieve_episodes`
  (`utils/maintenance/graph_data_operations.py` line 67): `valid_at <= ref`, `group_id IN`,
  `source = ?`, `ORDER BY valid_at DESC, created_at DESC LIMIT last_n`, with default window 3
  (`EPISODE_WINDOW_LEN`). Per-group listing with cursor `uuid < ?`, `ORDER BY uuid DESC LIMIT`
  (`nodes.py` line 675).
- **Time of the filter**: inside the same Cypher query that computes similarity or fulltext.

## 5. LangGraph store and LangMem

- `SearchOp`/`BaseStore.search` (`libs/checkpoint/langgraph/store/base/__init__.py` lines 203
  and 784): `namespace_prefix` (tuple), `filter` dict with `$eq, $ne, $gt, $gte, $lt, $lte`, keys
  in AND, no OR, IN, NOT or existence; `limit=10`, `offset=0`. `list_namespaces` (line 966):
  `prefix`, `suffix` with wildcard `*`, `max_depth`, `limit=100`, `offset`.
- PostgresStore (`libs/checkpoint-postgres/langgraph/store/postgres/base.py`,
  `_prepare_batch_search_queries` line 426): the prefix becomes `(prefix = ? OR prefix LIKE '?.%')`
  (`_namespace_prefix_condition`, line 1295), anchored on the `text_pattern_ops` index;
  filters on `value->k`; `$gt` and the like compare `value->>k` as **text** (line 650), not
  as a number. Without `query`: `ORDER BY updated_at DESC LIMIT ? OFFSET ?`. With `query`: vector
  subquery with the same filters in the `WHERE` and over-fetch `limit * vectors_per_doc * 2 + 1`.
- InMemoryStore (`libs/checkpoint/langgraph/store/memory/__init__.py`, `_filter_items` line
  238): filters by prefix and predicates in Python, then scores; range operators convert
  to `float`.
- **Isolation**: the namespace is the scope. The official authorization example
  (`libs/sdk-py/langgraph_sdk/auth/__init__.py` lines 86 to 92) prefixes the user's identity
  to every store operation. The empty prefix `()` searches the whole store.
- LangMem (`src/langmem/knowledge/tools.py` line 97): namespaces such as
  `("memories", "{langgraph_user_id}")`; the search tool exposes `limit=10`, `offset` and a
  free `filter` to the model (line 393); extraction queries with `query_limit=5`
  (`knowledge/extraction.py` lines 847 and 1020).

## 6. OpenAI Agents SDK (sessions)

`src/agents/memory/sqlite_session.py`, `get_items` (line 286): `WHERE session_id = ?
ORDER BY id DESC LIMIT ?` (or everything in ascending order). One scope equality and insertion
order with a limit. It is not semantic memory; it serves as the floor of what a history client
needs.

## 7. Claude Code (public conventions, not code)

From the public documentation (code.claude.com, consulted via Context7): `CLAUDE.md` in levels
(user, project, `.claude/rules/*.md`, `CLAUDE.local.md`) and automatic per-project memory,
loaded by path; the hosting guide isolates tenants by their own directory and
empty `settingSources`. There is no query by attributes; scope is a directory. I did not read the code.

## Comparison table

| System | Structured filters | Operators | Combination | Ordering/pagination | Scope |
| --- | --- | --- | --- | --- | --- |
| ai-memory | workspace, project, session, is_latest, expires_at, valid_from/to, kind, state, provider/model/dim | =, <=, >, IS NULL, IN, NOT | AND with absence OR; authorization OR | FTS rank; updated_at DESC; created_at ASC/DESC; LIMIT/OFFSET; COUNT | project ids mandatory; `scopes` and `global` cross, with read filter in SQL |
| mem0 OSS | user_id, agent_id, run_id, actor_id + free metadata | eq, ne, in, nin, gt..lte, contains, `*` | implicit AND; optional OR/NOT | score; get_all without order; top_k 20 | mandatory equality (≥1 key), same collection; omission crosses |
| mem0 platform | + app_id, created/updated_at, categories | same + date ranges, `in` on categories | AND/OR/NOT | page/page_size | same, wildcard and `in` on user_id |
| Letta V1 | organization, agent/archive, created_at, tags, role, project, template, conversation, text | =, >=, <=, IN, ContainsAny/All, LIKE | AND; tags ANY/ALL (post-filter in SQL) | created_at ASC/DESC + cursor (created_at,id); distance; COUNT and GROUP BY | org mandatory; agent optional; TPUF namespace per archive/org |
| Letta Code | path (prefix `system/`) | path prefix | — | — | directory per agent + kernel sandbox |
| Graphiti | group_id, edge_types, labels, uuids, valid/invalid/created/expired_at, source | IN, =, <>, ranges, IS [NOT] NULL | DNF in the core; AND in the MCP | RRF/score; valid_at DESC LIMIT; uuid cursor | `group_id IN`; None = everything; FalkorDB: graph per group with fan-out |
| LangGraph store | namespace prefix + value keys | $eq, $ne, ranges | AND | updated_at DESC LIMIT/OFFSET; score | namespace prefix; auth prefixes the identity |
| OpenAI sessions | session_id | = | — | id DESC LIMIT | one table, session key |

## Common patterns

1. **Scope as equality or prefix, almost always present.** All systems with structured
   queries put 1 to 3 scope predicates in every query: mem0 (up to 4 counting
   `actor_id`), ai-memory (2), Letta (1 to 2), Graphiti (1 list), LangGraph (1 prefix),
   OpenAI (1). In those that have users, the scope is enforced by the application; the store does not know
   who is asking (exceptions: ai-memory's read SQL and the authorization hook of the
   LangGraph Platform, both above the store).
2. **Time is the second filter.** Range on `created_at` (Letta, mem0 platform, Graphiti),
   bitemporal validity (`valid_at`/`invalid_at` in Graphiti; `valid_from`/`valid_to` in
   ai-memory) and expiry (`expires_at`, `expiration_date`). The validity and expiry tests
   are "missing or greater than", not a simple range.
3. **Ordering by time with a small limit** appears in every listing without a text query:
   `updated_at DESC` (ai-memory, LangGraph), `created_at` with cursor (Letta), `valid_at DESC`
   (Graphiti), id (OpenAI, mem0 history). Default limits between 3 and 50; documented maximums
   from 100 to 200; internal over-fetches from 60 to a few hundred. The only values above 4096
   are mem0's entity store listing (10 000) and maintenance batches.
4. **IN/ANY lists** appear in four systems: message roles and ANY tags (Letta), observation
   kinds (ai-memory), groups, edge types and uuids (Graphiti), categories and `in`
   (mem0). Arbitrary OR is offered by mem0 and by the Graphiti core, but the default
   paths I read do not emit it.
5. **Filter together with ranking.** In all of them, the structured filter goes into the same engine that
   ranks by vector or FTS (Qdrant pre-filter, `WHERE` of pgvector and of Cypher, Turbopuffer
   filters, PostgresStore subquery). Exceptions: Letta SQL tags and mem0 expiry,
   filtered afterwards, with over-fetch. A separate attribute index would only help if the vector
   engine accepted a set of candidate ids, or with post-filter and over-fetch.
6. **Cross-scope queries exist** in mem0 (omission, wildcard), ai-memory (`scopes`,
   `global`), Letta (whole organization), Graphiti (list or `None`) and LangGraph (short or
   empty prefix). In most of them they are authorized only by the application.
7. **Text** (FTS, BM25, `LIKE`, `contains`) accompanies the structured filter in ai-memory, mem0,
   Letta and Graphiti. No typed attribute covers this.

## What alternative E covers and what it does not

Premise: E keeps the current `QUERY` and partitions the index by permission domain (for example,
a namespace directory). The scope stops being a predicate and becomes the partition.

**Covers directly:**

- The scope of all systems, without spending a term: mem0's `user_id`/`agent_id`/`run_id`,
  ai-memory's `workspace/project`, Letta's `agent_id`, Graphiti's `group_id`, the
  LangGraph prefix and OpenAI's `session_id`. The partition also closes, by construction, the timing channel
  described in `docs/spec-m3.md` §3.1, because the scan does not touch other domains.
- Conjunctions of scope + 1 to 3 equality or range predicates: `is_latest`, `state`, single
  `kind`, `source`, `created_at` between two dates (two terms), `provider/model/dim`. ai-memory's
  main search, without FTS, fits in 4 terms if the scope leaves the terms.
- Tags in ALL mode up to 4 tags, if each tag is its own attribute with `EXISTS`
  (`user.moob.tag.<name>`), within the limit of 64 attributes per file.
- Observed limits: almost all response limits are far below 4096.

**Does not cover, with observed frequency:**

- **IN/OR** (4 of the 6 systems with structured queries): roles, ANY tags, kinds, categories,
  lists of groups and uuids. Client-side solution: N queries and union; with `QUERY_BATCH`
  (`docs/spec-m3.md` §4) it becomes one call. Large uuid lists (Graphiti) get bad.
- **"Missing or greater than"** (ai-memory `expires_at` and `valid_to`; Graphiti `IS NULL`): E has neither
  `NOT EXISTS` nor OR. Solution: the client writes a sentinel (`valid_to = INT64_MAX`), which
  is a data-convention change, not a kernel one.
- **NOT/ne/nin** (mem0, Graphiti `<>`, ai-memory's count of omitted ones): not expressible;
  requires reading and filtering in the client.
- **Ordering by time with a limit** (listings of the six systems): E returns up to 4096
  in arbitrary order. If the matched set in the partition fits in 4096, the client sorts and
  cuts; if it does not fit, "the N most recent" becomes incorrect without narrowing the time range in
  steps (`created_at >= t`), which costs several calls and needs adaptive logic.
  Letta's `(created_at, id)` cursor and ai-memory/LangGraph's `OFFSET` have no equivalent.
- **Counts** (ai-memory pagination and pending messages; Letta size and count per tag):
  E reports `count` up to the limit and `truncated`; an exact count above 4096 and `GROUP BY` do not
  exist. These are administrative uses, not retrieval ones.
- **Text and vector**: outside the scope of E and of any attribute index; they remain in the
  client or in a user-space index.
- **Cross-scope queries**: become fan-out per partition and merge in the client (the same pattern
  Graphiti uses in FalkorDB), with authorization guaranteed by the kernel in each partition.

## What alternative B covers

B (user-space SQLite index) covers the whole observed surface: IN/OR/NOT,
`IS NULL`, ordering by time with `LIMIT/OFFSET` or cursor, `COUNT`/`GROUP BY`, FTS5 and,
with an extension or in-process computation, vectors; it is exactly ai-memory's design. The cost
documented in the project: authorization leaves the kernel and needs to be reimplemented in the query
(ai-memory needed `readable_repository_sql` with three subqueries, and the guarantee holds
only for those who read through the server, not for those who open the database); the index is separate from the files and needs
reconciliation with edits made outside it (ai-memory uses a watcher and a periodic scan;
`docs/research-ai-memory.md`). With one database per permission domain, B inherits the POSIX isolation
of the database file, but cross-scope queries go back to being fan-out.

## Limitations of this reading

- I read query paths, not all adapters: mem0 has 27 vector stores and
  I read Qdrant and pgvector; Graphiti has several drivers and I read the common filter construction and the
  FalkorDB decorator.
- Frequency counts are over the paths read and the documentation, not over real usage; there is
  no telemetry on which filters real clients send.
- mem0's hosted platform was read only through the documentation; its server is not public.
- Letta V1 is retired; Letta Code was read superficially and I did not find search
  by attributes in it, which does not prove it does not exist in the cloud backend.
- The observation about date parameters in Graphiti was not tested.
- LlamaIndex and cognee were not read for lack of time. Claude Code was considered only through the
  public documentation.
- Nothing was executed; effective limits can be changed by configuration.

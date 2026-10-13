# 0089 — Second client: a LangGraph store on the layer, equal to the reference store

Date: 2026-10-13. Status: adapter and equality test written and run on the benchmark host;
no latency measured; the adapter was not run under LangGraph's own test suite. Local series
(after pull request 62).

## Goal

The step agreed in journal 0081: a second, different memory system on the layer, to learn
whether the design is fitted to the first client. The system is LangGraph's store interface
(`BaseStore`, langgraph 1.2.15 with langgraph-checkpoint 4.3.0 from PyPI, read on this date).

## What was built

- `bench/client/langgraph_store.py`: `MoobStore`, a `BaseStore` whose two required methods
  (`batch`, `abatch`) serve get, put, delete, search and list_namespaces from a directory of a
  mounted layer. Namespace labels are directories, a key is a file, the value is the file's
  JSON content, and each top-level scalar of the value is a typed attribute
  (`user.moob.v.<key>`). It calls `MOOB_IOC_QUERY2` through `ctypes`; the structure sizes match
  the C header (1098, 272 and 4656 bytes). Nothing in `layer/` or the UAPI changed.
- `bench/client/langgraph-equality.py`: the same random operations on LangGraph's
  `InMemoryStore` (the oracle) and on `MoobStore`; every get, search and list must agree.

## Result

Module of the current `main` (journals up to 0084) on the benchmark host, lower ext4:

| Domain depth | Seed | Operations | Searches compared (items) | Differences |
| --- | --- | --- | --- | --- |
| 1 | 1 | 3 000 | 546 (33 896) | 0 |
| 1 | 3 | 6 000 | 1 114 (120 564) | 0 |
| 2 | 2 | 3 000 | 490 (34 785) | 0 |
| 2 | 5 | 3 000 | 582 (34 891) | 0 |
| 3 | 4 | 3 000 | 592 (34 590) | 0 |

Twelve filter shapes: equality on strings and booleans, ranges on numbers, `$ne`, several keys
at once, a list value, a nested object, a key that is not a valid attribute name, a missing
field. No kernel diagnostic.

## What was learnt about the layer and its contract

1. **The interface was enough.** A second system with a different data model answered every
   search through `QUERY2` plus a read of each result. No change to the layer was needed or
   made.
2. **The homogeneity rule silently hid every file, and the cause was one library call.** The
   first run returned nothing at depth 1: `os.makedirs(path, mode=0o700)` gives that mode to
   the last directory only, so the domain directory was `0755` and the directories under it
   `0700`. Each file then failed the rule "no directory below the domain is more private than
   the domain" and was left out of answers, with no error and `heterogeneous=0` visible only
   to the administrator. This is adoption risk 11 met in practice by a careful client on its
   first day. The contract states the rule; nothing tells a client it has broken it.
3. **Namespaces shallower than the domain depth cannot be queried.** At depth 3, two-label
   namespaces keep their files above every domain. The adapter reads those directories itself:
   83 562 of 110 899 file reads in that run. A system whose scopes have mixed depths should
   mount with the shallowest depth it uses.
4. **About half of what a query returns is discarded after reading.** At depth 1, 55 163 of
   103 534 files read were filtered out by the adapter. Two causes, not separated here: a
   search on a prefix below the domain gets the whole domain's matches back, and a third of the
   filter conditions (`$ne`, lists, nested objects, invalid names) cannot be expressed as terms
   (371 of 1 150).
5. **Returning attribute values with results would not help this client.** A LangGraph search
   returns whole values, so every result's file is read anyway. The case for a projection
   (journal 0072) rests on the first client alone.
6. `MOOB_OP_EXISTS` must carry type 0; a term with a type is `EINVAL`. The header says "EXISTS
   ignores the value", which a reader takes to mean the type is ignored too.

## What was learnt about this client, not about the layer

- The reference store raises `TypeError` when a range filter meets an item without that field
  (`float(None)`): 249 to 480 searches per run. The adapter answers from the index and never
  sees such items. The two stores disagree by one raising and the other answering; the test
  counts these and does not compare them.
- The reference store keeps listing a namespace after its last item is deleted; the adapter
  lists directories that hold items. Set aside in the comparison (2 to 4 per run).
- Range operators coerce both sides to `float`. The adapter stores every number as a double so
  that `3` and `3.0` are one value, and leaves integers that do not fit a double unindexed.
- The reference store promises no order without a text query; the adapter returns newest
  first, as LangGraph's Postgres store does. Results are compared as sets.
- `offset` is applied after reading everything; the layer's cursor is not used for it.

## Limitations

- Correctness only. No latency, no comparison with the Postgres or SQLite stores of LangGraph.
- Random operations by one process as root; no concurrency, no second user, no TTL, no text
  or vector search (which stays in the client).
- The adapter was not run under LangGraph's own tests or inside a graph.
- One lower filesystem (ext4).

## Next step

Decide, with the maintainer, what to do about finding 2: a way for a client to learn that its
files are being left out (a per-domain answer, or `moobfs-check` listing them, which
spec-query-v2 §6 already promises and the tool does not do).

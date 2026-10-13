#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Equality test of the LangGraph adapter: the same random operations on LangGraph's own
InMemoryStore (the oracle) and on MoobStore over a mounted layer; every get, search and
list_namespaces must return the same items. Order is compared only as a set, because the
in-memory store promises none.

  langgraph-equality.py <layer directory> [--depth N] [--seed S] [--ops N]

Prints one EQUALITY line and the adapter's counters; exit status 1 on any difference.
"""
import argparse
import importlib.util
import os
import random
import sys

from langgraph.store.memory import InMemoryStore

spec = importlib.util.spec_from_file_location(
    "langgraph_store", os.path.join(os.path.dirname(os.path.abspath(__file__)), "langgraph_store.py"))
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

USERS = ["alice", "bob", "carol d"]
SPACES = ["memories", "notes", "prefs"]
KINDS = ["fact", "rule", "note", "decision"]


def value(rng):
    v = {"kind": rng.choice(KINDS), "score": rng.choice([rng.randint(0, 100), round(rng.random() * 100, 2)]),
         "pinned": rng.random() < 0.3, "text": "t" * rng.randint(0, 300)}
    if rng.random() < 0.3:
        v["tags"] = rng.sample(["a", "b", "c", "d"], rng.randint(0, 3))     # not indexable: a list
    if rng.random() < 0.2:
        v["meta"] = {"src": rng.choice(["chat", "doc"]), "n": rng.randint(0, 3)}   # nested
    if rng.random() < 0.15:
        v["Mixed-Case Key"] = rng.choice(KINDS)                              # not a valid attribute name
    if rng.random() < 0.1:
        del v["score"]                                                       # missing field
    return v


def filters(rng):
    k = rng.choice(KINDS)
    n = rng.randint(0, 100)
    return rng.choice([
        None,
        {"kind": k},
        {"kind": k, "pinned": True},
        {"score": {"$gte": n}},
        {"score": {"$gt": n, "$lt": n + 30}},
        {"kind": {"$ne": k}},
        {"kind": k, "score": {"$lte": n}, "pinned": False},
        {"tags": ["a", "b"]},
        {"meta": {"src": "chat"}},
        {"Mixed-Case Key": k},
        {"score": n},
        {"kind": {"$eq": k}, "score": {"$gte": 0}, "pinned": {"$ne": True}, "meta": {"n": 1}},
    ])


def as_set(items):
    return sorted((it.namespace, it.key, repr(sorted(it.value.items(), key=str))) for it in items)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--depth", type=int, default=1)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--ops", type=int, default=3000)
    a = ap.parse_args()
    if not mod.layer_mounted(a.root):
        sys.exit(f"{a.root} is not inside a mounted moobfs_layer")
    rng = random.Random(a.seed)
    oracle, store = InMemoryStore(), mod.MoobStore(a.root, domain_depth=a.depth)
    counts = {"put": 0, "delete": 0, "get": 0, "search": 0, "list": 0, "diff": 0, "search_items": 0,
              "oracle_raised": 0, "oracle_lists_emptied_namespace": 0, "adapter_answered_where_oracle_raised": 0}
    keys = []
    for i in range(a.ops):
        r = rng.random()
        ns = (rng.choice(USERS), rng.choice(SPACES)) + ((rng.choice(["x", "y"]),) if rng.random() < 0.3 else ())
        if r < 0.45 or not keys:
            key = f"k{rng.randint(0, 400)}"
            v = value(rng)
            oracle.put(ns, key, v); store.put(ns, key, v)
            keys.append((ns, key)); counts["put"] += 1
        elif r < 0.55:
            ns, key = rng.choice(keys)
            oracle.delete(ns, key); store.delete(ns, key); counts["delete"] += 1
        elif r < 0.70:
            ns, key = rng.choice(keys)
            x, y = oracle.get(ns, key), store.get(ns, key); counts["get"] += 1
            if (x is None) != (y is None) or (x is not None and x.value != y.value):
                counts["diff"] += 1; print("DIFF get", ns, key, file=sys.stderr)
        elif r < 0.97:
            prefix = ns[:rng.randint(0, len(ns))]
            f = filters(rng)
            try:
                x = oracle.search(prefix, filter=f, limit=100000)
            except TypeError:
                # the reference store raises when a range meets an item without the field
                # (float(None)); the adapter answers from the index and never sees that item
                counts["oracle_raised"] += 1
                try:
                    store.search(prefix, filter=f, limit=100000)
                    counts["adapter_answered_where_oracle_raised"] += 1
                except TypeError:
                    pass
                continue
            y = store.search(prefix, filter=f, limit=100000)
            counts["search"] += 1; counts["search_items"] += len(x)
            if as_set(x) != as_set(y):
                counts["diff"] += 1
                print("DIFF search", prefix, f, len(x), len(y), file=sys.stderr)
        else:
            x = oracle.list_namespaces(prefix=ns[:1]); y = store.list_namespaces(prefix=ns[:1]); counts["list"] += 1
            # the reference store keeps listing a namespace after its last item is deleted;
            # the adapter lists directories that hold items. Equal once those are set aside.
            emptied = [n for n in x if n not in y and not any(i.namespace == n for i in oracle.search(n, limit=100000))]
            counts["oracle_lists_emptied_namespace"] += len(emptied)
            if sorted(n for n in x if n not in emptied) != sorted(y):
                counts["diff"] += 1; print("DIFF list", ns[:1], file=sys.stderr)
    print("EQUALITY: " + " ".join(f"{k}={v}" for k, v in counts.items()) + f" seed={a.seed} depth={a.depth}")
    print("ADAPTER: " + " ".join(f"{k}={v}" for k, v in store.stats.items()))
    sys.exit(1 if counts["diff"] else 0)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Hybrid retrieval of the G5 package (protocol section 13, journal 0197).

Per uid and per path, one SQLite file in the uid's cache, outside the data directory,
kept by a watcher that reuses the G4 inotify loop (g4_derived.Watcher):

  items(id, domain, key, sha256, csha)   one row per memory this uid can read
  emb(csha, vec)                         all-MiniLM-L6-v2 vector of the content
  fts(content)                           FTS5, rowid = items.id
  meta(id, domain, question_id, role)    plain-files path only: the metadata table

A search scores the eligible set exactly: the vector branch by cosine over every eligible
row, the lexical branch by FTS5 bm25() restricted to the eligible rows, and reciprocal rank
fusion (k = 60) of each branch's top 100. Ties go to the smaller key. Every returned item is
opened again as the caller and dropped if its bytes no longer match the row.

The eligible set comes from QUERY2 on the layer (path "moob") or from the meta table (path
"plain"). The oracle reads no maintained index: it walks the files, embeds them afresh and
builds its own FTS5 table in memory.

Embedding always uses batch size 1 and two threads, so the same text gives the same bits
in every process (journal 0197).

  g5_retrieval.py watch  --root R --cache C --model M [--meta]
  g5_retrieval.py search --root R --cache C --model M --path moob|plain --queries Q --out O
  g5_retrieval.py oracle --root R --model M --queries Q --out O
"""
import argparse
import hashlib
import json
import os
import re
import resource
import sqlite3
import sys
import time
from urllib.parse import unquote

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import g4_derived as g4

THREADS = 2
TOP, BRANCH, RRF_K = 10, 100, 60
WORD = re.compile(r"\w+")
SCHEMA = """
CREATE TABLE IF NOT EXISTS items(id INTEGER PRIMARY KEY, domain TEXT, key TEXT, sha TEXT,
                                 csha TEXT, UNIQUE(domain, key));
CREATE TABLE IF NOT EXISTS emb(csha TEXT PRIMARY KEY, vec BLOB);
CREATE VIRTUAL TABLE IF NOT EXISTS fts USING fts5(content);
"""
META = """CREATE TABLE IF NOT EXISTS meta(id INTEGER PRIMARY KEY, domain TEXT, question_id TEXT,
                                     role TEXT);
CREATE INDEX IF NOT EXISTS meta_q ON meta(domain, question_id, role);"""


# ---- model ---------------------------------------------------------------------------------
class Model:
    def __init__(self, path):
        os.environ.setdefault("OMP_NUM_THREADS", str(THREADS))
        os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")
        import numpy
        import torch
        from sentence_transformers import SentenceTransformer
        torch.set_num_threads(THREADS)
        self.np = numpy
        self.m = SentenceTransformer(path, device="cpu")
        self.limit = self.m.max_seq_length
        self.embeds = 0
        self.truncated = 0

    def encode(self, text):
        n = len(self.m.tokenizer(text, add_special_tokens=True)["input_ids"])
        self.truncated += n > self.limit
        self.embeds += 1
        v = self.m.encode([text], batch_size=1, normalize_embeddings=True,
                          convert_to_numpy=True, show_progress_bar=False)[0]
        return v.astype("float32").tobytes()


def csha_of(text):
    return hashlib.sha256(text.encode()).hexdigest()


def match_expr(question):
    words = sorted(set(WORD.findall(question.lower())))
    return " OR ".join(f'"{w}"' for w in words)


# ---- ranking, shared by the searcher and the oracle ---------------------------------------
def vector_rank(np, matrix, keys, q):
    """Keys of the eligible rows by cosine, highest first, ties by key. Float64, summed along
    each row, so the order of rows in the matrix does not change a score."""
    if not keys:
        return []
    scores = (matrix.astype("float64") * np.frombuffer(q, "float32").astype("float64")).sum(1)
    order = sorted(range(len(keys)), key=lambda i: (-scores[i], keys[i]))
    return [keys[i] for i in order[:BRANCH]]


def lexical_rank(db, question, eligible_ids, id_key):
    expr = match_expr(question)
    if not expr or not eligible_ids:
        return []
    db.execute("DROP TABLE IF EXISTS temp.elig")
    db.execute("CREATE TEMP TABLE elig(id INTEGER PRIMARY KEY)")
    db.executemany("INSERT INTO temp.elig VALUES (?)", ((i,) for i in eligible_ids))
    rows = db.execute("SELECT fts.rowid, bm25(fts) FROM fts JOIN temp.elig e ON e.id = fts.rowid "
                      "WHERE fts MATCH ?", (expr,)).fetchall()
    rows.sort(key=lambda r: (r[1], id_key[r[0]]))
    return [id_key[r[0]] for r in rows[:BRANCH]]


def fuse(vec, lex):
    score = {}
    for ranked in (vec, lex):
        for rank, key in enumerate(ranked, start=1):
            score[key] = score.get(key, 0.0) + 1.0 / (RRF_K + rank)
    return [k for k, _ in sorted(score.items(), key=lambda kv: (-kv[1], kv[0]))][:BRANCH]


# ---- index ---------------------------------------------------------------------------------
def db_path(cache):
    return os.path.join(cache, "moobfs-g5", "index.db")


def open_db(path):
    """Autocommit connection; the watcher opens and commits its own transactions."""
    db = sqlite3.connect(path, timeout=60, isolation_level=None)
    db.execute("PRAGMA journal_mode=WAL")
    return db


class Watcher(g4.Watcher):
    """The G4 loop with an SQLite store. A path ends equal to what this uid can read."""

    def __init__(self, root, cache, model_path, meta):
        self.root = os.path.abspath(root)
        self.cache = cache
        self.dir = os.path.join(cache, "moobfs-g5")
        os.makedirs(self.dir, mode=0o700, exist_ok=True)
        os.chmod(self.dir, 0o700)
        self.model = "all-MiniLM-L6-v2"
        self.meta = meta
        self.enc = Model(model_path)
        self.db = open_db(db_path(cache))
        self.db.executescript(SCHEMA + (META if meta else ""))
        self.db.execute("BEGIN")
        self.dirty, self.applied, self.dropped = set(), [], {}
        self.overflows, self.passes, self.embeds = 0, 0, 0
        self.since_commit = 0
        self._open_inotify()

    def _row(self, domain, key):
        return self.db.execute("SELECT id, sha FROM items WHERE domain = ? AND key = ?",
                               (domain, key)).fetchone()

    def _drop_row(self, rid, domain, key):
        self.db.execute("DELETE FROM items WHERE id = ?", (rid,))
        self.db.execute("DELETE FROM fts WHERE rowid = ?", (rid,))
        if self.meta:
            self.db.execute("DELETE FROM meta WHERE id = ?", (rid,))
        self._changed(domain, key, "-")

    def settle(self, domain, name):
        if not g4.is_memory(name):
            return
        key = g4.key_of(name)
        got = g4.read_memory(os.path.join(self.root, domain, name))
        row = self._row(domain, key)
        if got is None:
            if row:
                self._drop_row(row[0], domain, key)
            return
        sha, value = got
        if row and row[1] == sha:
            return
        content = str(value.get("content", ""))
        csha = csha_of(content)
        if not self.db.execute("SELECT 1 FROM emb WHERE csha = ?", (csha,)).fetchone():
            self.db.execute("INSERT INTO emb VALUES (?, ?)", (csha, self.enc.encode(content)))
            self.embeds += 1
        if row:
            rid = row[0]
            self.db.execute("UPDATE items SET sha = ?, csha = ? WHERE id = ?", (sha, csha, rid))
            self.db.execute("DELETE FROM fts WHERE rowid = ?", (rid,))
        else:
            rid = self.db.execute("INSERT INTO items(domain, key, sha, csha) VALUES (?, ?, ?, ?)",
                                  (domain, key, sha, csha)).lastrowid
        self.db.execute("INSERT INTO fts(rowid, content) VALUES (?, ?)", (rid, content))
        if self.meta:
            self.db.execute("INSERT OR REPLACE INTO meta VALUES (?, ?, ?, ?)",
                            (rid, domain, value.get("question_id"), value.get("role")))
        self._changed(domain, key, sha)
        self.since_commit += 1
        if self.since_commit >= 500:
            self.persist()

    def settle_domain(self, domain):
        path = os.path.join(self.root, domain)
        if os.path.isdir(path):
            self.watch(path)
        names = set(g4.domain_files(self.root, domain))
        for rid, key in self.db.execute("SELECT id, key FROM items WHERE domain = ?",
                                        (domain,)).fetchall():
            if g4._enc(key) + ".json" not in names:
                self._drop_row(rid, domain, key)
        for name in sorted(names):
            self.settle(domain, name)

    def full_pass(self):
        self.passes += 1
        self.watch(self.root)
        seen = g4.domains(self.root)
        for (domain,) in self.db.execute("SELECT DISTINCT domain FROM items").fetchall():
            if domain not in seen:
                for rid, key in self.db.execute("SELECT id, key FROM items WHERE domain = ?",
                                                (domain,)).fetchall():
                    self._drop_row(rid, domain, key)
        for domain in seen:
            self.settle_domain(domain)

    def persist(self):
        self.db.execute("COMMIT")
        self.db.execute("BEGIN")
        self.since_commit = 0
        if self.applied:
            now = time.time()
            with open(os.path.join(self.dir, "applied.log"), "a") as f:
                f.write("".join(f"{now:.6f} {d} {g4._enc(k)} {s}\n" for d, k, s in self.applied))
            self.applied.clear()
        self.dirty.clear()

    def state(self, idle_at):
        g4.write_atomic(os.path.join(self.dir, "state.json"), json.dumps({
            "idle_at": idle_at, "overflows": self.overflows, "passes": self.passes,
            "embeds": self.embeds, "truncated": self.enc.truncated, "pid": os.getpid(),
            "model": self.model}))


# ---- searcher ------------------------------------------------------------------------------
class Searcher:
    def __init__(self, root, cache, model_path, path_kind):
        import langgraph_store as mod
        self.mod = mod
        self.root = os.path.abspath(root)
        self.kind = path_kind
        self.enc = Model(model_path)
        np = self.enc.np
        self.db = open_db(db_path(cache))
        rows = self.db.execute("SELECT i.id, i.domain, i.key, i.sha, e.vec FROM items i "
                               "JOIN emb e ON e.csha = i.csha").fetchall()
        self.row_of = {}
        self.id_key, self.sha, self.by_dk = {}, {}, {}
        vecs = []
        for n, (rid, domain, key, sha, vec) in enumerate(rows):
            self.row_of[rid] = n
            self.id_key[rid] = key
            self.sha[key] = (domain, sha)
            self.by_dk[(domain, key)] = rid
            vecs.append(np.frombuffer(vec, "float32"))
        self.matrix = np.vstack(vecs) if vecs else np.zeros((0, 384), "float32")
        self.store = mod.MoobStore(self.root, domain_depth=1)

    def eligible(self, domain, flt):
        """Ids of the eligible rows and the size of the eligible set as the filter answers it."""
        if self.kind == "plain":
            sql, args = "SELECT id FROM meta WHERE domain = ?", [domain]
            for name in ("question_id", "role"):
                if name in flt:
                    sql += f" AND {name} = ?"
                    args.append(flt[name])
            ids = [r[0] for r in self.db.execute(sql, args)]
            return ids, len(ids), 0
        terms = self.store._terms(flt)
        rels = self.store._query2(os.path.join(self.root, domain), terms)
        ids, unindexed = [], 0
        for rel in rels:
            if not rel.endswith(".json") or rel.startswith("."):
                continue
            rid = self.by_dk.get((domain, unquote(rel[:-5])))
            if rid is None:
                unindexed += 1
            else:
                ids.append(rid)
        return ids, len(ids) + unindexed, unindexed

    def reread(self, ranked, stats):
        out = []
        for key in ranked:
            if len(out) == TOP:
                break
            domain, sha = self.sha[key]
            got = g4.read_memory(os.path.join(self.root, domain, g4._enc(key) + ".json"))
            if got is None:
                stats["dropped_unreadable"] += 1
                continue
            if got[0] != sha:
                stats["dropped_stale"] += 1
                continue
            out.append(key)
        return out

    def search(self, question, domain, flt):
        np = self.enc.np
        t0 = time.perf_counter()
        q = self.enc.encode(question)
        ids, n_eligible, unindexed = self.eligible(domain, flt)
        keys = [self.id_key[i] for i in ids]
        rows = np.array([self.row_of[i] for i in ids], dtype="int64")
        vec = vector_rank(np, self.matrix[rows] if len(rows) else self.matrix[:0], keys, q)
        lex = lexical_rank(self.db, question, ids, self.id_key)
        fused = fuse(vec, lex)
        stats = {"dropped_unreadable": 0, "dropped_stale": 0}
        out = {"vector": self.reread(vec, stats), "lexical": self.reread(lex, stats),
               "fusion": self.reread(fused, stats)}
        out["seconds"] = time.perf_counter() - t0
        out.update(stats, eligible=n_eligible, unindexed=unindexed)
        return out


# ---- oracle --------------------------------------------------------------------------------
class Oracle:
    """Every memory this uid can read, from the files, embedded afresh, in memory only."""

    def __init__(self, root, model_path):
        self.enc = Model(model_path)
        np = self.enc.np
        self.root = os.path.abspath(root)
        self.db = sqlite3.connect(":memory:")
        self.db.executescript("CREATE VIRTUAL TABLE fts USING fts5(content);")
        self.items, self.id_key, vecs, memo = [], {}, [], {}
        for domain in g4.domains(self.root):
            for name in g4.domain_files(self.root, domain):
                got = g4.read_memory(os.path.join(self.root, domain, name))
                if got is None:
                    continue
                value = got[1]
                content = str(value.get("content", ""))
                c = csha_of(content)
                if c not in memo:
                    memo[c] = self.enc.encode(content)
                rid = len(self.items) + 1
                self.items.append((rid, domain, g4.key_of(name), value.get("question_id"),
                                   value.get("role")))
                self.id_key[rid] = g4.key_of(name)
                self.db.execute("INSERT INTO fts(rowid, content) VALUES (?, ?)", (rid, content))
                vecs.append(np.frombuffer(memo[c], "float32"))
        self.matrix = np.vstack(vecs)

    def search(self, question, domain, flt):
        np = self.enc.np
        q = self.enc.encode(question)
        pick = [n for n, (rid, d, _, qid, role) in enumerate(self.items) if d == domain
                and flt.get("question_id", qid) == qid and flt.get("role", role) == role]
        ids = [self.items[n][0] for n in pick]
        keys = [self.items[n][2] for n in pick]
        vec = vector_rank(np, self.matrix[np.array(pick, dtype="int64")] if pick
                          else self.matrix[:0], keys, q)
        lex = lexical_rank(self.db, question, ids, self.id_key)
        return {"vector": vec[:TOP], "lexical": lex[:TOP], "fusion": fuse(vec, lex)[:TOP],
                "eligible": len(pick)}


def usage():
    r = resource.getrusage(resource.RUSAGE_SELF)
    return {"cpu_s": r.ru_utime + r.ru_stime, "maxrss_kb": r.ru_maxrss}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=("watch", "search", "oracle"))
    ap.add_argument("--root", required=True)
    ap.add_argument("--model", required=True)
    ap.add_argument("--cache")
    ap.add_argument("--meta", action="store_true")
    ap.add_argument("--path", choices=("moob", "plain"))
    ap.add_argument("--queries")
    ap.add_argument("--out")
    ap.add_argument("--timed", action="store_true", help="a warm pass, then a timed pass")
    args = ap.parse_args()
    os.umask(0o077)
    if args.cmd == "watch":
        Watcher(args.root, args.cache, args.model, args.meta).run()
        return
    queries = json.load(open(args.queries))
    t = time.time()
    engine = (Searcher(args.root, args.cache, args.model, args.path) if args.cmd == "search"
              else Oracle(args.root, args.model))
    ready = time.time() - t
    results = {q["id"]: engine.search(q["question"], q["domain"], q["filter"]) for q in queries}
    timed = None
    if args.timed:
        timed = {q["id"]: engine.search(q["question"], q["domain"], q["filter"])["seconds"]
                 for q in queries}
    out = {"results": results, "timed": timed, "ready_s": ready, "usage": usage(),
           "embeds": engine.enc.embeds, "truncated": engine.enc.truncated}
    with open(args.out, "w") as f:
        json.dump(out, f)


if __name__ == "__main__":
    main()

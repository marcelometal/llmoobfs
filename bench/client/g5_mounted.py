#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Mounted G5 gate (protocol section 13, journal 0197). Run as root on the dedicated Linux
host, in the venv that holds the pinned packages.

Draws the LongMemEval sample, publishes one adapter file per turn on the layer, copies the
lower tree to a plain directory, starts one watcher per uid and path, and one oracle per
uid. Once every index has settled, it runs the searches: the MoobFS path in three fresh
processes per uid (a warm pass, then a timed pass each), the plain-files path once, then
the revocation case. Prints G5_* lines, writes everything to --out-dir, and ends with
G5_MOUNTED_PASS or G5_MOUNTED_FAIL and the missed bounds.
"""
import argparse
import collections
import json
import os
import random
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import langgraph_store as mod
from g1_mounted import ALICE, BOB, as_user, ensure_users, mount_layer, prepare_domains, run, umount
from g4_mounted import load_module

HERE = os.path.dirname(os.path.abspath(__file__))
FLOOR, Q1_P95, Q3_P95 = 0.02, 0.300, 2.0
BOOT, SEED = 10000, 1
SETTLE_TIMEOUT = 6 * 3600


def draw(instances):
    """110 non-abstention instances, stratified by question_type, largest remainder."""
    ok = [x for x in instances if not x["question_id"].endswith("_abs")]
    by = collections.defaultdict(list)
    for x in ok:
        by[x["question_type"]].append(x)
    exact = {t: 110 * len(v) / len(ok) for t, v in by.items()}
    count = {t: int(e) for t, e in exact.items()}
    for t in sorted(exact, key=lambda t: (-(exact[t] - count[t]), t))[:110 - sum(count.values())]:
        count[t] += 1
    rng = random.Random(SEED)
    picked = []
    for t in sorted(by):
        picked += rng.sample(by[t], count[t])
    rng.shuffle(picked)
    return picked, count


def turns(x):
    for si, (sid, date, session) in enumerate(zip(x["haystack_session_ids"], x["haystack_dates"],
                                                 x["haystack_sessions"])):
        for ti, t in enumerate(session):
            key = f"{x['question_id']}-s{si:03d}-t{ti:03d}"
            yield key, sid, {"question_id": x["question_id"], "role": t["role"],
                             "content": t["content"], "session_id": sid, "date": date}, \
                bool(t.get("has_answer"))


def pctl(values, p):
    v = sorted(values)
    return v[min(len(v) - 1, int(p * len(v)))] if v else None


class Gate:
    def __init__(self, args, team_gid, gids):
        self.a = args
        self.team_gid = team_gid
        self.gids = gids
        self.misses = []
        self.rec = {}
        self.procs = {}

    def miss(self, message):
        print(f"G5_MISS {message}", flush=True)
        self.misses.append(message)

    def check(self, cond, message):
        if not cond:
            self.miss(message)
        return cond

    def note(self, tag, **fields):
        self.rec.setdefault(tag, {}).update(fields)
        print(f"G5_{tag} " + " ".join(f"{k}={v}" for k, v in fields.items()), flush=True)

    # ---- processes as one uid ------------------------------------------------------------
    def env(self, cache):
        return {"PATH": "/usr/bin:/bin", "HOME": cache, "HF_HOME": os.path.join(cache, "hf"),
                "HF_HUB_OFFLINE": "1", "TRANSFORMERS_OFFLINE": "1", "OMP_NUM_THREADS": "2",
                "TOKENIZERS_PARALLELISM": "false"}

    def spawn(self, uid, cache, argv, log):
        cmd = [self.a.python, "-I", os.path.join(self.a.bin, "g5_retrieval.py")] + argv + [
            "--model", self.a.model]
        out = open(log, "w")
        os.chown(log, uid, self.gids[uid])
        return subprocess.Popen(cmd, user=uid, group=self.gids[uid], extra_groups=[self.team_gid],
                                cwd="/", env=self.env(cache), stdout=out, stderr=subprocess.STDOUT)

    def cache(self, uid, kind):
        return os.path.join(self.a.cache_base, f"{uid}-{kind}")

    def root(self, kind):
        return self.a.mnt if kind == "moob" else self.a.plain

    def start_watcher(self, uid, kind):
        argv = ["watch", "--root", self.root(kind), "--cache", self.cache(uid, kind)]
        if kind == "plain":
            argv.append("--meta")
        t = time.time()
        self.procs[(uid, kind)] = self.spawn(uid, self.cache(uid, kind), argv,
                                             os.path.join(self.a.work, f"watch-{uid}-{kind}.log"))
        return t

    def state(self, uid, kind):
        try:
            with open(os.path.join(self.cache(uid, kind), "moobfs-g5", "state.json")) as f:
                return json.load(f)
        except (OSError, ValueError):
            return {}

    def settled(self, uid, kind, after, timeout=SETTLE_TIMEOUT):
        proc = self.procs[(uid, kind)]
        deadline = time.time() + timeout
        while time.time() < deadline:
            if proc.poll() is not None:
                raise SystemExit(f"G5_MOUNTED_FAIL watcher {uid} {kind} exited {proc.returncode}")
            st = self.state(uid, kind)
            if st.get("idle_at", 0) > after and st.get("pid") == proc.pid:
                return st
            time.sleep(0.2)
        raise SystemExit(f"G5_MOUNTED_FAIL watcher {uid} {kind} did not settle")

    def job(self, uid, kind, cmd, queries, name, timed=False):
        """Run search or oracle as uid over queries; return its output."""
        qpath = os.path.join(self.a.work, f"{name}-queries.json")
        opath = os.path.join(self.a.work, f"{name}.json")
        with open(qpath, "w") as f:
            json.dump(queries, f)
        os.chmod(qpath, 0o644)
        cache = self.cache(uid, kind)
        argv = [cmd, "--root", self.root(kind), "--queries", qpath, "--out", opath]
        if cmd == "search":
            argv += ["--cache", cache, "--path", kind]
        if timed:
            argv.append("--timed")
        open(opath, "w").close()
        os.chown(opath, uid, self.gids[uid])
        return self.spawn(uid, cache, argv, os.path.join(self.a.work, f"{name}.log")), opath

    @staticmethod
    def wait(job):
        proc, opath = job
        if proc.wait() != 0:
            raise SystemExit(f"G5_MOUNTED_FAIL job {opath} exited {proc.returncode}")
        with open(opath) as f:
            return json.load(f)

    def vmhwm(self, proc):
        try:
            for line in open(f"/proc/{proc.pid}/status"):
                if line.startswith("VmHWM:"):
                    return int(line.split()[1])
        except OSError:
            return None


def plant(gate, mnt, picked, assign):
    t = time.time()
    n = 0
    for domain, uid in (("alice", ALICE), ("bob", BOB), ("team", BOB)):
        group = [x for x in picked if assign[x["question_id"]] == domain]

        def work(group=group, domain=domain):
            store = mod.MoobStore(mnt, domain_depth=1)
            c = 0
            for x in group:
                for key, _, value, _ in turns(x):
                    store.put((domain,), key, value)
                    c += 1
            return c

        n += as_user(uid, gate.gids[uid], [gate.team_gid], work)
        print(f"G5_PLANT {domain} total={n}", flush=True)
    gate.note("PLANT", files=n, seconds=f"{time.time() - t:.1f}")


def queries_for(picked, assign, uid):
    own = "alice" if uid == ALICE else "bob"
    out = []
    for x in picked:
        domain = assign[x["question_id"]]
        if domain not in (own, "team"):
            continue
        qid = x["question_id"]
        out.append({"id": f"{qid}|Q1", "question": x["question"], "domain": domain,
                    "filter": {"question_id": qid}})
        out.append({"id": f"{qid}|Q2", "question": x["question"], "domain": domain,
                    "filter": {"question_id": qid, "role": "user"}})
        if domain == own:
            out.append({"id": f"{qid}|Q3", "question": x["question"], "domain": domain,
                        "filter": {}})
    return out


def metrics(picked, results):
    """Per question, per branch, on Q1: recall_any@10, recall_all@10, nDCG@10."""
    import math
    out = {}
    for x in picked:
        r = results.get(f"{x['question_id']}|Q1")
        if r is None:
            continue
        sess, rel = {}, set()
        for key, sid, _, has in turns(x):
            sess[key] = sid
            if has:
                rel.add(key)
        evidence = set(x["answer_session_ids"])
        row = {}
        for branch in ("vector", "lexical", "fusion"):
            top = r[branch][:10]
            got = {sess[k] for k in top}
            dcg = sum(1 / math.log2(i + 2) for i, k in enumerate(top) if k in rel)
            idcg = sum(1 / math.log2(i + 2) for i in range(min(10, len(rel))))
            row[branch] = {"any": float(bool(got & evidence)), "all": float(evidence <= got),
                           "ndcg": dcg / idcg if idcg else None}
        row["type"] = x["question_type"]
        out[x["question_id"]] = row
    return out


def bootstrap(values, seed=SEED):
    import numpy as np
    v = np.array(values, dtype="float64")
    rng = np.random.default_rng(seed)
    means = v[rng.integers(0, len(v), size=(BOOT, len(v)))].mean(1)
    return float(v.mean()), float(np.percentile(means, 2.5)), float(np.percentile(means, 97.5))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ko", required=True)
    ap.add_argument("--data", default="/opt/g5-0197/data/longmemeval_s_cleaned.json")
    ap.add_argument("--model", default="/opt/g5-0197/model")
    ap.add_argument("--python", default="/opt/g5-0197/venv/bin/python")
    ap.add_argument("--lower", default="/srv/moob/ext4/g5-0197-lower")
    ap.add_argument("--plain", default="/srv/moob/ext4/g5-0197-plain")
    ap.add_argument("--mnt", default="/mnt/g5-0197")
    ap.add_argument("--cache-base", default="/srv/moob/ext4/g5-0197-cache")
    ap.add_argument("--bin", default="/srv/moob/ext4/g5-0197-bin")
    ap.add_argument("--work", default="/srv/moob/ext4/g5-0197-work")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--smoke", action="store_true",
                    help="harness check on 2 instances per domain; not a gate run")
    args = ap.parse_args()
    if sys.platform != "linux" or os.geteuid() != 0:
        raise SystemExit("G5_MOUNTED_FAIL run as root on Linux")
    load_module(args.ko)
    team_gid, alice_gid, bob_gid = ensure_users()
    gate = Gate(args, team_gid, {ALICE: alice_gid, BOB: bob_gid})
    umount(args.mnt)
    for path in (args.lower, args.plain, args.cache_base, args.bin, args.work):
        if os.path.isdir(path):
            run(["rm", "-rf", path])
    os.makedirs(args.out_dir, exist_ok=True)
    for path in (args.cache_base, args.bin, args.work):
        os.makedirs(path, mode=0o755)
    for name in ("g4_derived.py", "g5_retrieval.py", "langgraph_store.py"):
        run(["install", "-m", "0644", os.path.join(HERE, name), args.bin])

    instances = json.load(open(args.data))
    picked, count = draw(instances)
    assign = {}
    for i, x in enumerate(picked):
        assign[x["question_id"]] = "alice" if i < 50 else "bob" if i < 100 else "team"
    with open(os.path.join(args.out_dir, "sample.json"), "w") as f:
        json.dump({"per_type": count, "order": [[x["question_id"], x["question_type"],
                                                 assign[x["question_id"]]] for x in picked]}, f,
                  indent=1)
    gate.note("SAMPLE", instances=len(picked), per_type=json.dumps(count, sort_keys=True))
    if args.smoke:
        picked = picked[:2] + picked[50:52] + picked[100:102]
        gate.note("SMOKE", instances=len(picked))

    mount_layer(args.lower, args.mnt, "")
    os.chmod(args.mnt, 0o755)
    prepare_domains(args.mnt, team_gid)
    plant(gate, args.mnt, picked, assign)
    t = time.time()
    run(["rsync", "-aHAX", "--numeric-ids", args.lower + "/", args.plain + "/"])
    os.chmod(args.plain, 0o755)
    gate.note("PLAIN_COPY", seconds=f"{time.time() - t:.1f}")

    for uid in (ALICE, BOB):
        for kind in ("moob", "plain"):
            os.mkdir(gate.cache(uid, kind), 0o700)
            os.chown(gate.cache(uid, kind), uid, gate.gids[uid])
    qs = {uid: queries_for(picked, assign, uid) for uid in (ALICE, BOB)}
    started = {}
    try:
        for uid in (ALICE, BOB):
            for kind in ("moob", "plain"):
                started[(uid, kind)] = gate.start_watcher(uid, kind)
        oracles = {uid: gate.job(uid, "moob", "oracle", qs[uid], f"oracle-{uid}") for uid in (ALICE, BOB)}
        for (uid, kind), t0 in started.items():
            st = gate.settled(uid, kind, t0)
            gate.note("BUILD", **{f"{uid}_{kind}_s": f"{st['idle_at'] - t0:.0f}",
                                  f"{uid}_{kind}_embeds": st["embeds"],
                                  f"{uid}_{kind}_truncated": st["truncated"]})
        oracle = {uid: gate.wait(oracles[uid]) for uid in (ALICE, BOB)}
        for uid in (ALICE, BOB):
            gate.note("ORACLE", **{f"{uid}_ready_s": f"{oracle[uid]['ready_s']:.0f}",
                                   f"{uid}_embeds": oracle[uid]["embeds"]})

        moob_runs = {uid: [] for uid in (ALICE, BOB)}
        for r in range(1, args.runs + 1):
            jobs = {uid: gate.job(uid, "moob", "search", qs[uid], f"search-{uid}-moob-{r}", timed=True)
                    for uid in (ALICE, BOB)}
            for uid in (ALICE, BOB):
                moob_runs[uid].append(gate.wait(jobs[uid]))
        plain = {uid: gate.wait(gate.job(uid, "plain", "search", qs[uid], f"search-{uid}-plain",
                                         timed=True)) for uid in (ALICE, BOB)}

        allowed = {ALICE: {"alice", "team"}, BOB: {"bob", "team"}}
        owner = {x["question_id"]: assign[x["question_id"]] for x in picked}
        diffs = collections.Counter()
        for uid in (ALICE, BOB):
            for q in qs[uid]:
                o = oracle[uid]["results"][q["id"]]
                p = plain[uid]["results"][q["id"]]
                for run_out in moob_runs[uid]:
                    m = run_out["results"][q["id"]]
                    for branch in ("vector", "lexical", "fusion"):
                        diffs["moob_vs_plain"] += m[branch] != p[branch]
                        diffs["moob_vs_oracle"] += m[branch] != o[branch]
                        diffs["plain_vs_oracle"] += p[branch] != o[branch]
                        for k in m[branch] + p[branch]:
                            diffs["outside"] += owner.get(k.rsplit("-s", 1)[0]) not in allowed[uid]
                    want = min(10, m["eligible"])
                    diffs["short_fusion"] += len(m["fusion"]) != want
                    diffs["short_vector"] += len(m["vector"]) != want
                    diffs["lexical_below_10"] += len(m["lexical"]) < want
                    diffs["unindexed"] += m["unindexed"]
                    diffs["dropped"] += m["dropped_unreadable"] + m["dropped_stale"]
        gate.note("ANSWERS", **dict(sorted(diffs.items())))
        for name in ("moob_vs_plain", "moob_vs_oracle", "plain_vs_oracle", "outside",
                     "short_fusion", "short_vector", "unindexed"):
            gate.check(diffs[name] == 0, f"{name} = {diffs[name]}")

        merged = {}
        for uid in (ALICE, BOB):
            merged.update(moob_runs[uid][0]["results"])
        per_q = metrics(picked, merged)
        with open(os.path.join(args.out_dir, "relevance.json"), "w") as f:
            json.dump(per_q, f, indent=1)
        means = {}
        for branch in ("vector", "lexical", "fusion"):
            for m in ("any", "all", "ndcg"):
                vals = [row[branch][m] for row in per_q.values() if row[branch][m] is not None]
                mean, lo, hi = bootstrap(vals)
                means[(branch, m)] = mean
                gate.note("RELEVANCE", **{f"{branch}_{m}10": f"{mean:.3f}[{lo:.3f},{hi:.3f}]n={len(vals)}"})
        best = max(means[("vector", "all")], means[("lexical", "all")])
        branch_best = "vector" if means[("vector", "all")] >= means[("lexical", "all")] else "lexical"
        paired = [row["fusion"]["all"] - row[branch_best]["all"] for row in per_q.values()]
        d, lo, hi = bootstrap(paired)
        gate.note("FLOOR", fusion_all10=f"{means[('fusion', 'all')]:.3f}", best_branch=branch_best,
                  best_all10=f"{best:.3f}", paired_diff=f"{d:.3f}[{lo:.3f},{hi:.3f}]")
        gate.check(means[("fusion", "all")] >= best - FLOOR,
                   f"relevance floor fusion {means[('fusion', 'all')]:.3f} < {best:.3f} - {FLOOR}")

        for r in range(args.runs):
            q1, q3 = [], []
            for uid in (ALICE, BOB):
                for qid, s in moob_runs[uid][r]["timed"].items():
                    (q1 if qid.endswith("|Q1") else q3 if qid.endswith("|Q3") else []).append(s)
            p1, p3 = pctl(q1, 0.95), pctl(q3, 0.95)
            gate.note("LATENCY", **{f"run{r + 1}_q1_p50": f"{pctl(q1, 0.5):.3f}",
                                    f"run{r + 1}_q1_p95": f"{p1:.3f}",
                                    f"run{r + 1}_q3_p50": f"{pctl(q3, 0.5):.3f}",
                                    f"run{r + 1}_q3_p95": f"{p3:.3f}"})
            gate.check(p1 <= Q1_P95, f"run {r + 1} Q1 p95 {p1:.3f}s > {Q1_P95}s")
            gate.check(p3 <= Q3_P95, f"run {r + 1} Q3 p95 {p3:.3f}s > {Q3_P95}s")
        q1p = [s for uid in (ALICE, BOB) for qid, s in plain[uid]["timed"].items() if qid.endswith("|Q1")]
        q3p = [s for uid in (ALICE, BOB) for qid, s in plain[uid]["timed"].items() if qid.endswith("|Q3")]
        gate.note("PLAIN_LATENCY", q1_p50=f"{pctl(q1p, 0.5):.3f}", q1_p95=f"{pctl(q1p, 0.95):.3f}",
                  q3_p50=f"{pctl(q3p, 0.5):.3f}", q3_p95=f"{pctl(q3p, 0.95):.3f}")

        revocation(gate, picked, assign, qs, moob_runs[ALICE][0]["results"])

        for uid in (ALICE, BOB):
            for kind in ("moob", "plain"):
                db = os.path.join(gate.cache(uid, kind), "moobfs-g5", "index.db")
                gate.note("RESOURCES", **{f"{uid}_{kind}_watcher_vmhwm_kb": gate.vmhwm(gate.procs[(uid, kind)]),
                                          f"{uid}_{kind}_db_bytes": os.path.getsize(db)})
            gate.note("RESOURCES", **{f"{uid}_search_moob_maxrss_kb": moob_runs[uid][0]["usage"]["maxrss_kb"],
                                      f"{uid}_search_moob_cpu_s": f"{moob_runs[uid][0]['usage']['cpu_s']:.0f}",
                                      f"{uid}_search_plain_maxrss_kb": plain[uid]["usage"]["maxrss_kb"],
                                      f"{uid}_oracle_maxrss_kb": oracle[uid]["usage"]["maxrss_kb"]})
        gate.rec["raw"] = {"oracle": {str(u): oracle[u] for u in oracle},
                           "plain": {str(u): plain[u] for u in plain},
                           "moob": {str(u): moob_runs[u] for u in moob_runs}}
    finally:
        for proc in gate.procs.values():
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=60)
        gate.rec["misses"] = gate.misses
        with open(os.path.join(args.out_dir, "gate.json"), "w") as f:
            json.dump(gate.rec, f)
        umount(args.mnt)
    if gate.misses:
        print("G5_MOUNTED_FAIL " + "; ".join(gate.misses))
        raise SystemExit(2)
    print("G5_MOUNTED_PASS")


def revocation(gate, picked, assign, qs, before):
    """Uid 1002 makes the evidence turns of one team instance 0600 in both trees."""
    team = [x for x in picked if assign[x["question_id"]] == "team"]
    x = next(x for x in team if any(has for _, _, _, has in turns(x)))
    keys = [k for k, _, _, has in turns(x) if has]
    targets = [os.path.join(root, "team", mod._enc(k) + ".json")
               for root in (gate.a.mnt, gate.a.plain) for k in keys]

    def chmod_all():
        for p in targets:
            os.chmod(p, 0o600)
        return len(targets)

    as_user(BOB, gate.gids[BOB], [gate.team_gid], chmod_all)
    t = time.time()
    qa = [q for q in qs[ALICE] if q["id"].split("|")[0] == x["question_id"]]
    shown = sum(k in keys for q in qa for b in ("vector", "lexical", "fusion")
                for k in before[q["id"]][b])
    inflight = gate.wait(gate.job(ALICE, "moob", "search", qa, "revoke-inflight"))
    dropped = sum(r["dropped_unreadable"] for r in inflight["results"].values())
    leaked_inflight = sum(k in keys for r in inflight["results"].values()
                          for b in ("vector", "lexical", "fusion") for k in r[b])
    for kind in ("moob", "plain"):
        gate.settled(ALICE, kind, t, timeout=600)
    leaked = 0
    for kind in ("moob", "plain"):
        out = gate.wait(gate.job(ALICE, kind, "search", qa, f"revoke-after-{kind}"))
        leaked += sum(k in keys for r in out["results"].values()
                      for b in ("vector", "lexical", "fusion") for k in r[b])
    gate.note("REVOCATION", question=x["question_id"], revoked=len(keys), returned_before=shown,
              inflight_dropped=dropped,
              inflight_returned=leaked_inflight, after_returned=leaked)
    gate.check(leaked_inflight == 0, f"revocation in-flight returned {leaked_inflight}")
    gate.check(leaked == 0, f"revocation returned {leaked} after reconciliation")


if __name__ == "__main__":
    main()

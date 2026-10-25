#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Mounted G4 gate (protocol section 12, journal 0195). Run as root on the dedicated Linux
host, not on macOS.

Mounts the layer like the G1 gate, plants the same corpus, starts one watcher per uid and
runs scenarios E1 to E11. Every inventory is compared with a fresh scan made as that uid.
Prints one G4_* line per measurement, writes the raw numbers to --out, and ends with
G4_MOUNTED_PASS or G4_MOUNTED_FAIL and the list of missed bounds. One run is one of the
three the protocol requires; the run is not repeated or trimmed here.
"""
import argparse
import json
import os
import signal
import subprocess
import sys
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import g4_derived as g4
import langgraph_store as mod
from g1_mounted import (ALICE, BOB, STAMP, ensure_users, get_info, mount_layer, plant,
                        prepare_domains, run, salary, umount)

HERE = os.path.dirname(os.path.abspath(__file__))
SYSCTL = "/proc/sys/fs/inotify/max_queued_events"
F1_P99, F1_MAX, F2_MAX = 1.0, 5.0, 60.0
SETTLE_TIMEOUT = 300.0
SYSTEM_PYTHON = "/usr/bin/python3"


class Run:
    def __init__(self, args, team_gid, gids):
        self.args = args
        self.team_gid = team_gid
        self.gids = gids
        self.mnt = args.mnt
        self.cache = {uid: os.path.join(args.cache_base, name)
                      for uid, name in ((ALICE, "alice"), (BOB, "bob"))}
        self.procs = {}
        self.misses = []
        self.record = {"run": args.label, "scenarios": {}}

    # ---- bookkeeping ---------------------------------------------------------------------
    def miss(self, message):
        print(f"G4_MISS {message}", flush=True)
        self.misses.append(message)

    def check(self, cond, message):
        if not cond:
            self.miss(message)
        return cond

    def note(self, scenario, **fields):
        self.record["scenarios"].setdefault(scenario, {}).update(fields)
        print(f"G4_{scenario} " + " ".join(f"{k}={v}" for k, v in fields.items()), flush=True)

    # ---- acting as one uid ---------------------------------------------------------------
    def as_uid(self, uid, fn):
        """Run fn in a child that has dropped to uid entirely, and return its JSON result."""
        r, w = os.pipe()
        pid = os.fork()
        if pid == 0:
            os.close(r)
            try:
                os.setgroups([self.team_gid])
                os.setresgid(self.gids[uid], self.gids[uid], self.gids[uid])
                os.setresuid(uid, uid, uid)
                out = {"ok": fn()}
            except BaseException:
                out = {"error": traceback.format_exc()}
            data = json.dumps(out).encode()
            while data:
                data = data[os.write(w, data):]
            os._exit(0)
        os.close(w)
        chunks = []
        while True:
            chunk = os.read(r, 1 << 20)
            if not chunk:
                break
            chunks.append(chunk)
        os.close(r)
        os.waitpid(pid, 0)
        out = json.loads(b"".join(chunks))
        if "error" in out:
            raise RuntimeError(f"as uid {uid}: {out['error']}")
        return out["ok"]

    def store(self):
        return mod.MoobStore(self.mnt, domain_depth=1)

    # ---- watchers ------------------------------------------------------------------------
    def start(self, uid, model=None):
        """The watcher uses only the standard library, so it runs on the system Python from
        a copy the two uids can read; the venv may sit in a home they cannot enter."""
        cmd = [SYSTEM_PYTHON, "-I", os.path.join(self.args.bin, "g4_derived.py"), "watch",
               "--root", self.mnt, "--cache", self.cache[uid]]
        if model:
            cmd += ["--model", model]
        started = time.time()
        self.procs[uid] = subprocess.Popen(cmd, user=uid, group=self.gids[uid],
                                           extra_groups=[self.team_gid], cwd="/")
        return started

    def kill(self, uid, sig=signal.SIGTERM):
        proc = self.procs.pop(uid)
        proc.send_signal(sig)
        proc.wait(timeout=60)

    def state(self, uid):
        return g4.read_state(self.cache[uid]) or {}

    def settled(self, uid, after):
        """Wait until uid's watcher reports an empty queue after time `after`. The change
        that returned before `after` was queued by then, so it has been applied."""
        deadline = time.time() + SETTLE_TIMEOUT
        while time.time() < deadline:
            st = self.state(uid)
            proc = self.procs.get(uid)
            if proc is not None and proc.poll() is not None:
                raise SystemExit(f"G4_MOUNTED_FAIL watcher {uid} exited {proc.returncode}")
            if st.get("idle_at", 0) > after and st.get("pid") == (proc.pid if proc else None):
                return
            time.sleep(0.05)
        raise SystemExit(f"G4_MOUNTED_FAIL watcher {uid} did not settle in {SETTLE_TIMEOUT}s")

    def settle_both(self, after):
        for uid in (ALICE, BOB):
            self.settled(uid, after)

    def vmhwm(self, uid):
        try:
            for line in open(f"/proc/{self.procs[uid].pid}/status"):
                if line.startswith("VmHWM:"):
                    return int(line.split()[1])
        except (KeyError, OSError):
            pass
        return None

    # ---- inventories ---------------------------------------------------------------------
    def oracle(self, uid, model=None):
        model = model or self.model_of(uid)
        return self.as_uid(uid, lambda: g4.fresh_scan(self.mnt, model))

    def model_of(self, uid):
        return self.as_uid(uid, lambda: g4.read_model(self.cache[uid]))

    def cached(self, uid):
        return g4.load_cache(self.cache[uid])

    @staticmethod
    def diff(got, exp):
        counts = {"missing": 0, "extra": 0, "sha": 0, "model": 0, "vector": 0}
        domains = set(got) | set(exp)
        for d in domains:
            g, e = got.get(d, {}), exp.get(d, {})
            counts["missing"] += len(set(e) - set(g))
            counts["extra"] += len(set(g) - set(e))
            for key in set(g) & set(e):
                counts["sha"] += g[key][0] != e[key][0]
                counts["model"] += g[key][1] != e[key][1]
                counts["vector"] += g[key][2] != e[key][2]
        return counts

    def inventory(self, scenario, uids=(ALICE, BOB)):
        for uid in uids:
            counts = self.diff(self.cached(uid), self.oracle(uid))
            self.note(scenario, **{f"inventory_{uid}": ",".join(f"{k}:{v}" for k, v in counts.items())})
            self.check(not any(counts.values()), f"{scenario} inventory uid {uid} {counts}")

    def recover(self, scenario, uid, exp, t0):
        """Seconds from t0 until uid's cache equals exp, polled; F2 bound."""
        searches = 0
        deadline = t0 + F2_MAX + 30
        while time.time() < deadline:
            try:
                got = self.cached(uid)
            except (OSError, ValueError):
                got = None
            if got is not None and not any(self.diff(got, exp).values()):
                took = time.time() - t0
                self.note(scenario, recover_s=f"{took:.3f}", searches_during=searches)
                self.check(took <= F2_MAX, f"{scenario} recovery {took:.1f}s > {F2_MAX}s")
                return took
            if searches < 5:
                self.served(scenario, uid, during=True)
                searches += 1
            time.sleep(0.25)
        self.miss(f"{scenario} recovery did not reach the inventory in {F2_MAX + 30}s")
        return None

    # ---- derived search ------------------------------------------------------------------
    def search(self, uid, query, k=10, prefix=("team",)):
        cache = self.cache[uid]
        return self.as_uid(uid, lambda: g4.derived_search(self.mnt, cache, prefix, query, k))

    def served(self, scenario, uid=ALICE, during=False, query=None):
        """Nothing obsolete served: each returned item is the current file with the sha256
        it was scored with, and none is a deleted key."""
        query = query or {"kind": "decision", "flag": True}
        results, stats = self.search(uid, query, k=20)
        for domain, key, _, sha in results:
            path = os.path.join(self.mnt, domain, mod._enc(key) + ".json")
            got = g4.read_memory(path)
            self.check(got is not None, f"{scenario} served deleted {domain}/{key}")
            self.check(got is None or got[0] == sha, f"{scenario} served obsolete {domain}/{key}")
        self.check(stats["scored_other_model"] == 0, f"{scenario} scored another model")
        tag = "during" if during else "after"
        rec = self.record["scenarios"].setdefault(scenario, {}).setdefault(f"search_{tag}", [])
        rec.append(stats)
        return results, stats

    # ---- writers -------------------------------------------------------------------------
    def bob_ops(self, ops):
        """Run ops as uid 1002 through the adapter. Return [(op, key, time after return)]."""
        mnt = self.mnt

        def work():
            store = mod.MoobStore(mnt, domain_depth=1)
            out = []
            for op, key, value in ops:
                if op == "put":
                    store.put(("team",), key, value)
                elif op == "delete":
                    store.delete(("team",), key)
                elif op == "rename":
                    os.rename(os.path.join(mnt, "team", key + ".json"),
                              os.path.join(mnt, "team", value + ".json"))
                elif op == "chmod":
                    os.chmod(os.path.join(mnt, "team", key + ".json"), value)
                out.append((op, key, time.time()))
            return out

        return self.as_uid(BOB, work)

    @staticmethod
    def value(note):
        return {"kind": "fact", "flag": False, "score": 0.75, "note": note}

    def sha_now(self, key):
        got = g4.read_memory(os.path.join(self.mnt, "team", key + ".json"))
        return got[0] if got else "-"

    def applied(self, uid):
        lines = {}
        path = os.path.join(g4.cache_dir(self.cache[uid]), "applied.log")
        for line in open(path):
            t, domain, key, sha = line.split()
            lines.setdefault((domain, key, sha), float(t))
        return lines

    # ---- scenarios -----------------------------------------------------------------------
    def e1_e3(self):
        ops = [("put", f"m{i:05d}", self.value(f"upd-e1-{i:05d}")) for i in range(100)]
        ops += [("put", f"e2{i:05d}", self.value(f"new-e2-{i:05d}")) for i in range(100)]
        ops += [("delete", f"m{i:05d}", None) for i in range(100, 200)]
        done = self.bob_ops(ops)
        last = done[-1][2]
        self.settle_both(last)
        expect = {key: self.sha_now(key) for _, key, _ in done}
        for uid, name in ((ALICE, "alice"), (BOB, "bob")):
            seen = self.applied(uid)
            lat, never = [], 0
            for op, key, t in done:
                t_seen = seen.get(("team", key, expect[key]))
                if t_seen is None:
                    never += 1
                else:
                    lat.append(t_seen - t)
            lat.sort()
            q = (lambda p: lat[min(len(lat) - 1, int(p * len(lat)))] if lat else None)
            self.note("F1", **{f"{name}_n": len(lat), f"{name}_never": never,
                               f"{name}_p50_s": f"{q(0.50):.4f}", f"{name}_p95_s": f"{q(0.95):.4f}",
                               f"{name}_p99_s": f"{q(0.99):.4f}", f"{name}_max_s": f"{lat[-1]:.4f}"})
            self.record["scenarios"]["F1"][f"{name}_latencies_s"] = lat
            if uid == ALICE:
                self.check(never == 0, f"F1 {never} changes never reflected for uid 1001")
                self.check(q(0.99) <= F1_P99, f"F1 p99 {q(0.99):.3f}s > {F1_P99}s")
                self.check(lat[-1] <= F1_MAX, f"F1 max {lat[-1]:.3f}s > {F1_MAX}s")
        for scenario in ("E1", "E2", "E3"):
            self.note(scenario, changes=100)
        self.inventory("E3")
        self.served("E3")

    def e4(self):
        before = {uid: self.state(uid).get("embeds", 0) for uid in (ALICE, BOB)}
        done = self.bob_ops([("rename", f"m{i:05d}", f"r{i:05d}") for i in range(200, 250)])
        self.settle_both(done[-1][2])
        after = {uid: self.state(uid).get("embeds", 0) for uid in (ALICE, BOB)}
        self.note("E4", renames=50, reembeds_1001=after[ALICE] - before[ALICE],
                  reembeds_1002=after[BOB] - before[BOB])
        self.inventory("E4")
        self.served("E4")

    def e5(self):
        keys = [f"m{i:05d}" for i in range(10)]
        values = {k: g4.read_memory(os.path.join(self.mnt, "team", k + ".json"))[1] for k in keys}
        inflight = {"dropped_unreadable": 0, "returned_revoked": 0}
        last = 0.0
        for key in keys:
            last = self.bob_ops([("chmod", key, 0o600)])[-1][2]
            results, stats = self.search(ALICE, values[key], k=5)
            inflight["dropped_unreadable"] += stats["dropped_unreadable"]
            inflight["returned_revoked"] += sum(1 for _, k, _, _ in results if k == key)
        self.check(inflight["returned_revoked"] == 0, f"E5 in-flight returned {inflight}")
        self.settle_both(last)
        held = self.cached(ALICE).get("team", {})
        self.check(not any(k in held for k in keys), "E5 revoked entries left for uid 1001")
        self.check(all(k in self.cached(BOB).get("team", {}) for k in keys),
                   "E5 owner lost its entries")
        leaked = 0
        for key in keys:
            results, _ = self.search(ALICE, values[key], k=10)
            leaked += sum(1 for _, k, _, _ in results if k in keys)
        self.check(leaked == 0, f"E5 {leaked} revoked items returned after reconciliation")
        self.note("E5", revoked=10, inflight_dropped=inflight["dropped_unreadable"],
                  inflight_returned=inflight["returned_revoked"], after_returned=leaked)
        self.inventory("E5")

    def e6(self):
        team = os.path.join(self.mnt, "team")
        os.chmod(team, 0o2700)
        t = time.time()
        self.settle_both(t)
        for uid in (ALICE, BOB):
            left = len(self.cached(uid).get("team", {}))
            self.check(left == 0, f"E6 uid {uid} kept {left} team entries")
        results, _ = self.search(ALICE, {"kind": "decision"}, k=10)
        self.check(results == [], "E6 search returned team items while revoked")
        self.inventory("E6")
        os.chmod(team, 0o2770)
        t = time.time()
        self.settle_both(t)
        back = len(self.cached(ALICE).get("team", {}))
        self.note("E6", entries_while_revoked=0, entries_back_1001=back)
        self.inventory("E6")
        self.served("E6")

    def e7(self):
        self.kill(ALICE, signal.SIGKILL)
        ops = [("put", f"m{i:05d}", self.value(f"upd-e7-{i:05d}")) for i in range(300, 367)]
        ops += [("put", f"e7{i:05d}", self.value(f"new-e7-{i:05d}")) for i in range(67)]
        ops += [("delete", f"m{i:05d}", None) for i in range(400, 466)]
        done = self.bob_ops(ops)
        self.settled(BOB, done[-1][2])
        exp = self.oracle(ALICE)
        t0 = self.start(ALICE)
        self.recover("E7", ALICE, exp, t0)
        self.settled(ALICE, t0)
        self.inventory("E7")
        self.served("E7")

    def e8(self):
        original = open(SYSCTL).read().strip()
        try:
            with open(SYSCTL, "w") as f:
                f.write("64\n")
            self.kill(ALICE)
            t = self.start(ALICE)
            self.settled(ALICE, t)
            os.kill(self.procs[ALICE].pid, signal.SIGSTOP)
            ops = [("put", f"m{i:05d}", self.value(f"upd-e8-{i:05d}")) for i in range(1000, 2000)]
            done = self.bob_ops(ops)
            self.settled(BOB, done[-1][2])
            exp = self.oracle(ALICE)
            t0 = time.time()
            os.kill(self.procs[ALICE].pid, signal.SIGCONT)
            self.recover("E8", ALICE, exp, t0)
            self.settled(ALICE, t0)
            overflows = self.state(ALICE).get("overflows", 0)
            self.note("E8", changes=1000, queue=64, overflows=overflows)
            self.check(overflows >= 1, "E8 recorded no IN_Q_OVERFLOW")
            self.inventory("E8")
            self.served("E8")
        finally:
            with open(SYSCTL, "w") as f:
                f.write(original + "\n")
            proc = self.procs.get(ALICE)
            if proc is not None:
                os.kill(proc.pid, signal.SIGCONT)
        self.kill(ALICE)
        t = self.start(ALICE)
        self.settled(ALICE, t)
        self.note("E8", sysctl_restored=open(SYSCTL).read().strip())

    def e9(self):
        cache = self.cache[ALICE]
        exp = self.oracle(ALICE, model="hash64-v2")
        self.as_uid(ALICE, lambda: g4.write_atomic(
            os.path.join(g4.cache_dir(cache), "model"), "hash64-v2\n"))
        t0 = time.time()
        self.procs[ALICE].send_signal(signal.SIGHUP)
        absent = []
        for _ in range(3):
            _, stats = self.served("E9", ALICE, during=True)
            absent.append(stats["not_current_model"])
        self.recover("E9", ALICE, exp, t0)
        self.settled(ALICE, t0)
        self.note("E9", model="hash64-v2", absent_during=",".join(map(str, absent)))
        self.inventory("E9")
        _, stats = self.served("E9", ALICE)
        self.check(stats["not_current_model"] == 0, "E9 v1 entries left after reconciliation")

    def e10(self):
        lower_team = os.path.join(self.args.lower, "team")
        for i in range(20):
            path = os.path.join(lower_team, f"l{i:05d}.json")
            with open(path, "w") as f:
                json.dump({"value": self.value(f"lower-e10-{i:05d}"),
                           "created_at": STAMP, "updated_at": STAMP}, f)
            os.chown(path, 0, self.team_gid)
            os.chmod(path, 0o644)
        t = time.time()
        time.sleep(2.0)
        self.settled(ALICE, t)
        seen = sum(1 for i in range(20) if f"l{i:05d}" in self.cached(ALICE).get("team", {}))
        visible = sum(1 for i in range(20)
                      if os.path.exists(os.path.join(self.mnt, "team", f"l{i:05d}.json")))
        self.note("E10", lower_writes=20, running_watcher_saw=seen, visible_through_mount=visible)
        exp = self.oracle(ALICE)
        self.kill(ALICE)
        t0 = self.start(ALICE)
        self.recover("E10", ALICE, exp, t0)
        self.settled(ALICE, t0)
        self.kill(BOB)
        t = self.start(BOB)
        self.settled(BOB, t)
        self.note("E10", bob_restarted=1)
        self.inventory("E10")
        self.served("E10")

    def e11(self):
        mnt, ready = self.mnt, "/tmp/g4-0195-go"
        try:
            os.unlink(ready)
        except FileNotFoundError:
            pass
        pids = []
        for uid, who in ((ALICE, "alice"), (BOB, "bob")):
            pid = os.fork()
            if pid == 0:
                try:
                    os.setgroups([self.team_gid])
                    os.setresgid(self.gids[uid], self.gids[uid], self.gids[uid])
                    os.setresuid(uid, uid, uid)
                    while not os.path.exists(ready):
                        time.sleep(0.005)
                    store = mod.MoobStore(mnt, domain_depth=1)
                    for i in range(30):
                        store.put(("team",), "dup", self.value(f"dup-{who}-{i:02d}"))
                    os._exit(0)
                except BaseException:
                    traceback.print_exc()
                    os._exit(9)
            pids.append(pid)
        open(ready, "w").close()
        for pid in pids:
            _, status = os.waitpid(pid, 0)
            self.check(os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0,
                       f"E11 publisher {status}")
        t = time.time()
        self.settle_both(t)
        final = g4.read_memory(os.path.join(mnt, "team", "dup.json"))
        self.note("E11", rounds=30, final_note=final[1]["note"] if final else "-")
        self.inventory("E11")
        for uid in (ALICE, BOB):
            entry = self.cached(uid).get("team", {}).get("dup")
            self.check(entry is not None and final is not None and entry[0] == final[0],
                       f"E11 uid {uid} entry is not the final name")
        self.served("E11")

    def protected_cache(self):
        for owner, other in ((ALICE, BOB), (BOB, ALICE)):
            d = g4.cache_dir(self.cache[owner])

            def probe(d=d):
                out = []
                for path in (d, os.path.join(d, "team.json"), os.path.join(d, "state.json")):
                    try:
                        if os.path.isdir(path):
                            os.listdir(path)
                        else:
                            open(path).close()
                        out.append(path)
                    except PermissionError:
                        pass
                return out

            opened = self.as_uid(other, probe)
            self.check(opened == [], f"cache of {owner} opened by {other}: {opened}")
        notes, values = set(), set()
        for domain in ("alice", "bob", "team"):
            base = os.path.join(self.mnt, domain)
            for name in os.listdir(base):
                if g4.is_memory(name):
                    got = g4.read_memory(os.path.join(base, name))
                    if got:
                        values.add(g4.canonical(got[1]))
                        if isinstance(got[1].get("note"), str):
                            notes.add(got[1]["note"])
        found, size = 0, {}
        for uid in (ALICE, BOB):
            d = g4.cache_dir(self.cache[uid])
            total = 0
            for name in os.listdir(d):
                text = open(os.path.join(d, name), "rb").read()
                total += len(text)
                text = text.decode("ascii", "replace")
                found += sum(1 for v in values if v in text)
                found += sum(1 for n in notes if n in text)
            size[uid] = total
        self.check(found == 0, f"cache holds {found} value or note strings")
        self.note("CACHE", protected=1, value_strings_found=found,
                  bytes_1001=size[ALICE], bytes_1002=size[BOB],
                  notes_checked=len(notes), values_checked=len(values))


def load_module(ko):
    loaded = "moobfs_layer" in open("/proc/modules").read()
    if loaded:
        have = open("/sys/module/moobfs_layer/srcversion").read().strip()
        want = subprocess.check_output(["modinfo", "-F", "srcversion", ko], text=True).strip()
        mounts = [ln for ln in open("/proc/mounts") if "moobfs_layer" in ln.split()[2:3]]
        if have != want:
            if mounts:
                raise SystemExit("G4_MOUNTED_FAIL another moobfs_layer is mounted; not replacing it")
            run(["rmmod", "moobfs_layer"])
            loaded = False
    if not loaded:
        run(["insmod", ko])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ko", required=True)
    ap.add_argument("--lower", default="/srv/moob/ext4/g4-0195-lower")
    ap.add_argument("--mnt", default="/mnt/g4-0195")
    ap.add_argument("--cache-base", default="/srv/moob/ext4/g4-0195-cache")
    ap.add_argument("--bin", default="/srv/moob/ext4/g4-0195-bin")
    ap.add_argument("--label", default="1")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    if sys.platform != "linux" or os.geteuid() != 0:
        raise SystemExit("G4_MOUNTED_FAIL run as root on Linux")
    load_module(args.ko)
    team_gid, alice_gid, bob_gid = ensure_users()
    gids = {ALICE: alice_gid, BOB: bob_gid}
    umount(args.mnt)
    for path in (args.lower, args.cache_base, args.bin):
        if os.path.isdir(path):
            run(["rm", "-rf", path])
    mount_layer(args.lower, args.mnt, "")
    os.chmod(args.mnt, 0o755)
    prepare_domains(args.mnt, team_gid)
    os.makedirs(args.cache_base, mode=0o755)
    os.makedirs(args.bin, mode=0o755)
    run(["install", "-m", "0644", os.path.join(HERE, "g4_derived.py"), args.bin])
    r = Run(args, team_gid, gids)
    for uid, path in r.cache.items():
        os.mkdir(path, 0o700)
        os.chown(path, uid, gids[uid])
    groups = [team_gid]
    t = time.time()
    plant(args.mnt, ALICE, alice_gid, groups, "alice", 1000)
    plant(args.mnt, BOB, bob_gid, groups, "bob", 1000)
    plant(args.mnt, ALICE, alice_gid, groups, "team", 5000)
    salary(os.path.join(args.mnt, "team"), BOB, bob_gid, groups)
    r.note("PLANT", files=7001, seconds=f"{time.time() - t:.1f}")
    info = get_info(args.mnt)
    r.note("GET_INFO", budget=info.index_budget, used=info.index_used)
    try:
        for uid in (ALICE, BOB):
            t0 = r.start(uid, model="hash64-v1")
            r.settled(uid, t0)
            r.note("START", **{f"pass_s_{uid}": f"{r.state(uid)['idle_at'] - t0:.3f}"})
        r.inventory("START")
        salary_in = "salary" in r.cached(ALICE).get("team", {})
        r.check(not salary_in, "uid 1001 indexed salary.json")
        r.check("salary" in r.cached(BOB).get("team", {}), "uid 1002 lost salary.json")
        for step in (r.e1_e3, r.e4, r.e5, r.e6, r.e7, r.e8, r.e9, r.e10, r.e11):
            step()
        r.protected_cache()
        r.note("WATCHER", vmhwm_kb_1001=r.vmhwm(ALICE), vmhwm_kb_1002=r.vmhwm(BOB),
               embeds_1001=r.state(ALICE).get("embeds"), embeds_1002=r.state(BOB).get("embeds"))
    finally:
        for uid in list(r.procs):
            r.kill(uid)
        r.record["misses"] = r.misses
        with open(args.out, "w") as f:
            json.dump(r.record, f, indent=1)
        umount(args.mnt)
    if r.misses:
        print("G4_MOUNTED_FAIL " + "; ".join(r.misses))
        raise SystemExit(2)
    print("G4_MOUNTED_PASS")


if __name__ == "__main__":
    main()

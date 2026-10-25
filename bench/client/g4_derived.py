#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Derived index of the G4 package (protocol section 12, journal 0195).

One vector per memory, per OS identity, kept in that uid's cache directory and never in
the data directory. An entry is (namespace, key, sha256, model, vector) and holds no value
bytes. The watcher follows the data directory with inotify in the state-based way of
journal 0092: every event names a path to look at again, and that path ends equal to what
this uid can read at that moment, including "gone". A start, a SIGHUP (model change) and
IN_Q_OVERFLOW run a full pass, which reuses a vector whose sha256 and model still match.

Cache layout, under <cache>/moobfs-g4/ (directory 0700, files 0600):
  <domain>.json  {"entries": {key: [sha256, model, vector hex]}}
  model          current model name
  state.json     {"idle_at", "overflows", "passes", "embeds", "pid", "model"}
  applied.log    "<time> <domain> <key> <sha256 or ->" per change made durable
  requeue        paths a derived search found stale, read by the watcher when idle

Everything in the cache is text: names, hex digests and hex vectors. Nothing here is in
the layer or its interface.

  g4_derived.py watch --root <data dir> --cache <dir> [--model hash64-v1]
"""
import argparse
import ctypes
import ctypes.util
import fcntl
import hashlib
import json
import os
import re
import select
import signal
import struct
import sys
import termios
import time
from urllib.parse import quote, unquote

DIMS = 64
MODELS = {"hash64-v1": 1, "hash64-v2": 2}
DEFAULT_MODEL = "hash64-v1"
TOKEN = re.compile(r"[a-z0-9_.]+")

IN_ATTRIB, IN_CLOSE_WRITE, IN_MOVED_FROM, IN_MOVED_TO = 0x4, 0x8, 0x40, 0x80
IN_CREATE, IN_DELETE, IN_DELETE_SELF, IN_MOVE_SELF = 0x100, 0x200, 0x400, 0x800
IN_Q_OVERFLOW, IN_IGNORED, IN_ISDIR = 0x4000, 0x8000, 0x40000000
IN_NONBLOCK, IN_CLOEXEC = 0o4000, 0o2000000
MASK = (IN_ATTRIB | IN_CLOSE_WRITE | IN_MOVED_FROM | IN_MOVED_TO | IN_CREATE | IN_DELETE
        | IN_DELETE_SELF | IN_MOVE_SELF)
EVENT = struct.Struct("iIII")
IDLE_TICK = 0.2


# ---- model ---------------------------------------------------------------------------------
def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def embed(value, model):
    """hash64: feature hashing of the canonical JSON's tokens into DIMS signed components,
    then L2 normalization (Weinberger et al. 2009). A stand-in for an embedding model so a
    model change can be tested. It makes no relevance claim."""
    seed = MODELS[model]
    vec = [0.0] * DIMS
    for tok in TOKEN.findall(canonical(value).lower()):
        h = hashlib.sha256(f"{seed}:{tok}".encode()).digest()
        vec[int.from_bytes(h[:4], "little") % DIMS] += 1.0 if h[4] & 1 else -1.0
    norm = sum(x * x for x in vec) ** 0.5 or 1.0
    return struct.pack(f"<{DIMS}f", *(x / norm for x in vec)).hex()


def vector(hexed):
    return struct.unpack(f"<{DIMS}f", bytes.fromhex(hexed))


# ---- reading what this uid can read --------------------------------------------------------
def _enc(label):
    return quote(label, safe="")


def is_memory(name):
    return name.endswith(".json") and not name.startswith(".")


def key_of(name):
    return unquote(name[:-5])


def read_memory(path):
    """(sha256, value) of a memory this uid can open now, or None."""
    try:
        with open(path, "rb") as f:
            data = f.read()
        value = json.loads(data)["value"]
    except (OSError, ValueError, KeyError, TypeError):
        return None
    return hashlib.sha256(data).hexdigest(), value


def domains(root):
    try:
        names = sorted(os.listdir(root))
    except OSError:
        return []
    return [n for n in names if not n.startswith(".") and os.path.isdir(os.path.join(root, n))]


def domain_files(root, domain):
    try:
        names = os.listdir(os.path.join(root, domain))
    except OSError:
        return []
    return sorted(n for n in names if is_memory(n))


def fresh_scan(root, model):
    """The oracle: every memory this uid can read, embedded from nothing. Reads no cache."""
    out = {}
    for domain in domains(root):
        entries = {}
        for name in domain_files(root, domain):
            got = read_memory(os.path.join(root, domain, name))
            if got:
                entries[key_of(name)] = [got[0], model, embed(got[1], model)]
        out[domain] = entries
    return out


# ---- cache ---------------------------------------------------------------------------------
def cache_dir(cache):
    return os.path.join(cache, "moobfs-g4")


def write_atomic(path, text):
    tmp = f"{path}.tmp.{os.getpid()}"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    try:
        os.write(fd, text.encode())
        os.fsync(fd)
    finally:
        os.close(fd)
    os.rename(tmp, path)


def read_model(cache):
    try:
        with open(os.path.join(cache_dir(cache), "model")) as f:
            return f.read().strip()
    except FileNotFoundError:
        return DEFAULT_MODEL


def load_cache(cache):
    """{domain: {key: [sha256, model, vector hex]}} as stored."""
    out = {}
    d = cache_dir(cache)
    for name in sorted(os.listdir(d)):
        if name.endswith(".json") and name != "state.json":
            with open(os.path.join(d, name)) as f:
                out[name[:-5]] = json.load(f)["entries"]
    return out


def read_state(cache):
    try:
        with open(os.path.join(cache_dir(cache), "state.json")) as f:
            return json.load(f)
    except (FileNotFoundError, ValueError):
        return None


# ---- derived search ------------------------------------------------------------------------
def derived_search(root, cache, prefix, query, k=10):
    """Exact search over this uid's entries under prefix, current model only.

    Ties are broken by (namespace, key). Each candidate is opened and read again as the
    caller; one whose bytes no longer hash to the entry, or that cannot be opened, is
    dropped and its path queued for the watcher. That read is not a permission system and
    does not hide counts, scores or timing."""
    model = read_model(cache)
    q = vector(embed(query, model))
    stored = load_cache(cache)
    scored, other_model = [], 0
    for domain, entries in stored.items():
        if prefix and domain != prefix[0]:
            continue
        for key, (sha, emodel, vec) in entries.items():
            if emodel != model:
                other_model += 1
                continue
            scored.append((-sum(a * b for a, b in zip(q, vector(vec))), domain, key, sha))
    scored.sort()
    stats = {"scored": len(scored), "not_current_model": other_model,
             "dropped_stale": 0, "dropped_unreadable": 0, "scored_other_model": 0}
    results, requeue = [], []
    for _, domain, key, sha in scored:
        if len(results) == k:
            break
        path = os.path.join(root, domain, _enc(key) + ".json")
        got = read_memory(path)
        if got is None:
            stats["dropped_unreadable"] += 1
            requeue.append(path)
            continue
        if got[0] != sha:
            stats["dropped_stale"] += 1
            requeue.append(path)
            continue
        results.append((domain, key, got[1], sha))
    if requeue:
        with open(os.path.join(cache_dir(cache), "requeue"), "a") as f:
            f.write("".join(p + "\n" for p in requeue))
    return results, stats


# ---- watcher -------------------------------------------------------------------------------
class Watcher:
    def __init__(self, root, cache, model):
        self.root = os.path.abspath(root)
        self.cache = cache
        self.dir = cache_dir(cache)
        os.makedirs(self.dir, mode=0o700, exist_ok=True)
        os.chmod(self.dir, 0o700)
        if model:
            write_atomic(os.path.join(self.dir, "model"), model + "\n")
        self.model = read_model(cache)
        self.entries = {}
        try:
            self.entries = load_cache(cache)
        except (OSError, ValueError):
            self.entries = {}
        self.dirty, self.applied = set(), []
        self.overflows, self.passes, self.embeds = 0, 0, 0
        self.dropped = {}
        self._open_inotify()

    def _open_inotify(self):
        libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
        self._add = libc.inotify_add_watch
        self._add.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_uint32]
        self._rm = libc.inotify_rm_watch
        self.fd = libc.inotify_init1(IN_NONBLOCK | IN_CLOEXEC)
        if self.fd < 0:
            raise OSError(ctypes.get_errno(), "inotify_init1")
        self.wds = {}
        self.reload = False
        self.stop = False
        signal.signal(signal.SIGHUP, self._on_hup)
        signal.signal(signal.SIGTERM, self._on_term)

    def _on_hup(self, *_):
        self.reload = True

    def _on_term(self, *_):
        self.stop = True

    def watch(self, path):
        """Add a watch before the directory is read. A directory this uid cannot read gets
        no watch; the root's IN_ATTRIB for it brings it back."""
        if path in self.wds.values():
            return
        wd = self._add(self.fd, path.encode(), MASK)
        if wd >= 0:
            self.wds[wd] = path

    # one path ends equal to what this uid can read now
    def settle(self, domain, name):
        if not is_memory(name):
            return
        key = key_of(name)
        entries = self.entries.setdefault(domain, {})
        got = read_memory(os.path.join(self.root, domain, name))
        old = entries.get(key)
        if got is None:
            if old is not None:
                self._drop(domain, key)
            return
        sha, value = got
        if old and old[0] == sha and old[1] == self.model:
            return
        entries[key] = [sha, self.model, self._vector(sha, value)]
        self._changed(domain, key, sha)

    def _vector(self, sha, value):
        """A vector dropped in this batch with the same sha256 and model is reused: a
        rename is a delete and a new key for LangGraph, not new content."""
        vec = self.dropped.get((sha, self.model))
        if vec is None:
            self.embeds += 1
            vec = embed(value, self.model)
        return vec

    def _drop(self, domain, key):
        sha, model, vec = self.entries[domain].pop(key)
        self.dropped[(sha, model)] = vec
        self._changed(domain, key, "-")

    def settle_domain(self, domain):
        path = os.path.join(self.root, domain)
        if os.path.isdir(path):
            self.watch(path)
        names = set(domain_files(self.root, domain))
        entries = self.entries.setdefault(domain, {})
        for key in list(entries):
            if _enc(key) + ".json" not in names:
                self._drop(domain, key)
        for name in sorted(names):
            self.settle(domain, name)

    def full_pass(self):
        self.passes += 1
        self.watch(self.root)
        seen = domains(self.root)
        for domain in list(self.entries):
            if domain not in seen:
                for key in list(self.entries[domain]):
                    self._drop(domain, key)
        for domain in seen:
            self.settle_domain(domain)

    def _changed(self, domain, key, sha):
        self.dirty.add(domain)
        self.applied.append((domain, key, sha))

    def persist(self):
        for domain in sorted(self.dirty):
            write_atomic(os.path.join(self.dir, f"{_enc(domain)}.json"),
                         json.dumps({"entries": self.entries.get(domain, {})}))
        self.dirty.clear()
        self.dropped.clear()
        if self.applied:
            now = time.time()
            with open(os.path.join(self.dir, "applied.log"), "a") as f:
                f.write("".join(f"{now:.6f} {d} {_enc(k)} {s}\n" for d, k, s in self.applied))
                f.flush()
                os.fsync(f.fileno())
            self.applied.clear()

    def state(self, idle_at):
        write_atomic(os.path.join(self.dir, "state.json"), json.dumps({
            "idle_at": idle_at, "overflows": self.overflows, "passes": self.passes,
            "embeds": self.embeds, "pid": os.getpid(), "model": self.model}))

    def pending(self):
        n = ctypes.c_int(0)
        fcntl.ioctl(self.fd, termios.FIONREAD, n)
        return n.value

    def read_events(self):
        """Paths to settle, as (domain, name); or a full pass request."""
        work, domains_again, full = set(), set(), False
        while True:
            try:
                buf = os.read(self.fd, 1 << 16)
            except BlockingIOError:
                break
            off = 0
            while off < len(buf):
                wd, mask, _, length = EVENT.unpack_from(buf, off)
                name = buf[off + EVENT.size: off + EVENT.size + length].rstrip(b"\0").decode()
                off += EVENT.size + length
                if mask & IN_Q_OVERFLOW:
                    self.overflows += 1
                    full = True
                    continue
                if mask & IN_IGNORED:
                    self.wds.pop(wd, None)
                    continue
                path = self.wds.get(wd)
                if path is None:
                    continue
                if path == self.root:
                    if name and (mask & IN_ISDIR or mask & IN_ATTRIB):
                        domains_again.add(name)
                    continue
                domain = os.path.relpath(path, self.root)
                if not name or mask & IN_ISDIR:
                    domains_again.add(domain)
                else:
                    work.add((domain, name))
        return work, domains_again, full

    def requeued(self):
        path = os.path.join(self.dir, "requeue")
        try:
            os.rename(path, path + ".work")
        except FileNotFoundError:
            return set()
        with open(path + ".work") as f:
            lines = f.read().split()
        os.unlink(path + ".work")
        work = set()
        for p in lines:
            rel = os.path.relpath(p, self.root).split(os.sep)
            if len(rel) == 2:
                work.add((rel[0], rel[1]))
        return work

    def run(self):
        os.umask(0o077)
        self.full_pass()
        self.persist()
        self.state(time.time())
        while not self.stop:
            if self.reload:
                self.reload = False
                self.model = read_model(self.cache)
                self.full_pass()
                self.persist()
            try:
                ready, _, _ = select.select([self.fd], [], [], IDLE_TICK)
            except InterruptedError:
                continue
            if not ready:
                work = self.requeued()
                for domain, name in sorted(work):
                    self.settle(domain, name)
                self.persist()
                if not work and self.pending() == 0 and not self.reload:
                    self.state(time.time())
                continue
            work, domains_again, full = self.read_events()
            if full:
                self.full_pass()
            else:
                for domain in sorted(domains_again):
                    self.settle_domain(domain)
                for domain, name in sorted(work):
                    if domain not in domains_again:
                        self.settle(domain, name)
            self.persist()
        self.persist()


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    w = sub.add_parser("watch")
    w.add_argument("--root", required=True)
    w.add_argument("--cache", required=True)
    w.add_argument("--model", choices=sorted(MODELS))
    args = ap.parse_args()
    if args.cmd == "watch":
        Watcher(args.root, args.cache, args.model).run()


if __name__ == "__main__":
    sys.exit(main())

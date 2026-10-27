#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Issue 118 (journal 0210): the first QUERY2 of a domain built over a slow lower device.

Run as root on the dedicated test host, with the layer module loaded and dm-delay available:

    DELAYS="0 5 20 50 100 200 400" python3 bench/client/issue118_slow_lower.py

A 1 GiB file on /srv/moob/ext4 becomes a loop device behind dm-delay (delay set per run, reads
and writes), with ext4 and 5000 files in one domain (2500 match). For each delay the layer is
mounted afresh and the page cache dropped, so the first QUERY2 builds the domain from the slow
device; uid 2001 asks twice. A final query with no delay checks recovery. Everything it created
is removed at the end. Prints one JSON line per delay and SLOW118_RESULTS.
"""
import json, os, subprocess, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "python", "src"))
import moobfs
from moobfs import Term

IMG, LOWER, MNT, NAME = "/srv/moob/ext4/slow118.img", "/mnt/slow118-lower", "/mnt/slow118", "moobslow118"
UID, FILES = 2001, 5000
DELAYS = [int(x) for x in os.environ.get("DELAYS", "0 5 20 50").split()]

def run(*a, **k):
    return subprocess.run(a, check=True, capture_output=True, text=True, **k).stdout.strip()

def table(delay_ms):
    sectors = run("blockdev", "--getsz", LOOP)
    return f"0 {sectors} delay {LOOP} 0 {delay_ms}"

def reload(delay_ms):
    run("dmsetup", "suspend", NAME)
    run("dmsetup", "reload", NAME, "--table", table(delay_ms))
    run("dmsetup", "resume", NAME)

def query_as_user():
    r, w = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(r)
        os.setgroups([]); os.setgid(UID); os.setuid(UID)
        t = time.monotonic()
        try:
            n = len(moobfs.Domain(os.path.join(MNT, "dom"), backend="kernel").query(Term("kind", "==", "note")))
            out = {"n": n}
        except OSError as e:
            out = {"errno": e.errno}
        out["s"] = round(time.monotonic() - t, 2)
        os.write(w, json.dumps(out).encode()); os._exit(0)
    os.close(w); data = os.read(r, 4096); os.waitpid(pid, 0)
    return json.loads(data)

LOOP = None
try:
    if not os.path.exists(IMG):
        run("truncate", "-s", "1G", IMG)
    LOOP = run("losetup", "--find", "--show", IMG)
    run("dmsetup", "create", NAME, "--table", table(0))
    dev = f"/dev/mapper/{NAME}"
    run("mkfs.ext4", "-q", "-F", dev)
    os.makedirs(LOWER, exist_ok=True); os.makedirs(MNT, exist_ok=True)
    run("mount", dev, LOWER)
    os.chmod(LOWER, 0o755)
    dom = os.path.join(LOWER, "dom"); os.mkdir(dom, 0o700); os.chown(dom, UID, UID)
    for i in range(FILES):
        moobfs.publish(dom, f"m{i:05d}.json", b"{}", {"kind": "note" if i % 2 else "fact", "n": i})
    run("sync")
    results = []
    for delay in DELAYS:
        reload(delay)
        run("mount", "-t", "moobfs_layer", "-o", f"lower={LOWER},domain_depth=1", "none", MNT)
        run("sync"); open("/proc/sys/vm/drop_caches", "w").write("3\n")
        first = query_as_user()
        second = query_as_user()
        run("umount", MNT)
        results.append({"delay_ms": delay, "first": first, "second": second})
        print(json.dumps(results[-1]), flush=True)
    reload(0)
    run("mount", "-t", "moobfs_layer", "-o", f"lower={LOWER},domain_depth=1", "none", MNT)
    after = query_as_user()
    run("umount", MNT)
    print(json.dumps({"delay_ms": 0, "recovery": after}), flush=True)
    print("SLOW118_RESULTS " + json.dumps(results))
finally:
    subprocess.run(["umount", MNT], capture_output=True)
    subprocess.run(["umount", LOWER], capture_output=True)
    subprocess.run(["dmsetup", "remove", NAME], capture_output=True)
    if LOOP:
        subprocess.run(["losetup", "-d", LOOP], capture_output=True)
    if os.path.exists(IMG):
        os.unlink(IMG)

#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# bench/run: orchestrates one benchmark campaign (docs/benchmark-harness.md §2, §6, §8).
# Prepares each system, loads the dataset, checks equivalence, runs the workloads in an
# order that alternates between repetitions, collects system metrics and keeps every raw
# artifact under --out. Numbers are only meaningful on a Linux host or VM with KVM; under
# emulation this script validates the procedure, not the results.
#
#   run.sh --out DIR --scale N --profile 6|8|16 --reps R [--seed S] [--procs N]
#          [--plain NAME=DIR ...] [--layer NAME=MOUNT:LOWER ...]
#
# --plain  a directory on a plain filesystem (baselines A and B): W1, W2, W4, W8, SQLite
#          baseline load and plan gate, SQLite-vs-oracle equivalence, and W3 through the
#          SQLite library (bench/sqlclient) with the SQL of the same shapes.
# --layer  a mounted moobfs_layer root and its lower directory (system C): everything above
#          plus W3 (QUERY), W5, W6, W7 and three-way equivalence. Run as root for W6/W7.
# Environment: BIN (built tools, default build/host), SQLITE (default sqlite3).
set -eu
BIN=${BIN:-build/host}
SQLITE=${SQLITE:-sqlite3}
here=$(cd "$(dirname "$0")" && pwd)
# sibling scripts: bench/ layout on a host, bench-* prefixed when staged into the VM
sib() { if [ -e "$here/$1" ]; then echo "$here/$1"; else echo "$here/bench-$1"; fi; }
SQLBASE=$(sib sqlbase.sh); EQUIV=$(sib equiv.sh); QUERIES=$(sib queries-w3.txt); REPORT=$(sib report.py)
out=""; scale=1000; profile=8; reps=3; seed=1; procs=4
W3_REPEAT=${W3_REPEAT:-50}       # executions of every W3 shape per repetition
W3_UID=${W3_UID:-65534}          # regular user for the authorization measurement (nobody)
QUERY_ONLY=${QUERY_ONLY:-0}      # 1: only W1 (to build the dataset), W3 (root and regular user) and W7
LAYER_MOUNT_OPTS=${LAYER_MOUNT_OPTS:-domain_depth=0}  # options after lower= when W6/W7 remount (v1 QUERY needs depth 0)
plain=""; layer=""
while [ $# -gt 0 ]; do
    case $1 in
        --out) out=$2; shift 2 ;;
        --scale) scale=$2; shift 2 ;;
        --profile) profile=$2; shift 2 ;;
        --reps) reps=$2; shift 2 ;;
        --seed) seed=$2; shift 2 ;;
        --procs) procs=$2; shift 2 ;;
        --plain) plain="$plain $2"; shift 2 ;;
        --layer) layer="$layer $2"; shift 2 ;;
        *) echo "run.sh: unknown option $1" >&2; exit 64 ;;
    esac
done
[ -n "$out" ] && [ -n "$plain$layer" ] || { echo "usage: see the header of bench/run.sh" >&2; exit 64; }
mkdir -p "$out"
log() { printf '%s %s\n' "$(date -u +%H:%M:%S)" "$*" | tee -a "$out/run.log"; }

# environment.txt: what §6 of the harness document asks for
{
    echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "kernel: $(uname -srm)"
    echo "cpu: $(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ //')  cores: $(nproc 2>/dev/null || echo ?)"
    if [ -f "$here/cpu-freq.sh" ]; then sh "$here/cpu-freq.sh" show; else echo "cpufreq: not recorded (bench/cpu-freq.sh not next to this script)"; fi
    echo "memory: $(grep MemTotal /proc/meminfo 2>/dev/null | awk '{print $2 " kB"}')"
    echo "kvm: $([ -e /dev/kvm ] && echo present || echo absent)  (host virtualization: $(systemd-detect-virt 2>/dev/null || echo unknown))"
    echo "sqlite: $($SQLITE --version 2>/dev/null | cut -d' ' -f1-2 || echo absent)"
    echo "scale: $scale profile: $profile reps: $reps seed: $seed procs: $procs w3_repeat: $W3_REPEAT w3_uid: $W3_UID query_only: $QUERY_ONLY"
    echo "systems: plain=[$plain ] layer=[$layer ]"
    echo "commit: $(git -C "$here/.." rev-parse --short HEAD 2>/dev/null || echo unknown)"
} > "$out/environment.txt"
log "environment recorded in $out/environment.txt"

"$BIN/bench-gen" --seed "$seed" --inodes "$scale" --profile "$profile" --out "$out/manifest.txt"
grep -v '^#' "$QUERIES" > "$out/queries.txt"
# Q8 uses tag1, which exists only in profile 16; the line is kept so shape numbers stay Q1..Q9
# (on other profiles Q8 matches nothing in every system, which the equivalence check confirms)
while read -r q; do sh "$SQLBASE" sql $q; done < "$out/queries.txt" > "$out/queries.sql"   # SQL of the same shapes
log "manifest: $scale memories, profile $profile; $(wc -l < "$out/queries.txt" | tr -d ' ') query shapes"

# QUERY answers over the whole mount, while the oracle and SQLite only see <mount>/bench, so
# anything else under a layer root would make the kernel return extra results and fail the
# equivalence check; refuse to start instead of measuring a contaminated tree
for spec in $layer; do
    rest=${spec#*=}; mnt=${rest%%:*}
    for entry in "$mnt"/* "$mnt"/.[!.]*; do
        [ -e "$entry" ] || continue
        case ${entry##*/} in bench|lost+found) continue ;; esac   # lost+found: fsck's directory, never typed
        echo "run.sh: $mnt must contain nothing but bench/ (found ${entry##*/})" >&2; exit 1
    done
done

diskstat() { cat /proc/diskstats > "$1" 2>/dev/null || true; }

run_system() {   # $1 rep dir, $2 name, $3 target dir, $4 mount root ("" for plain), $5 lower root ("" for plain)
    rep=$1; name=$2; target=$3; mount=$4; lowerroot=$5; lower=${5:+$5/bench}
    d="$rep/$name"; mkdir -p "$d"
    rm -rf "$target"; mkdir -p "$target"
    diskstat "$d/diskstats.before"
    "$BIN/bench-load" --manifest "$out/manifest.txt" --target "$target" --workload W1 --procs "$procs" --out "$d/w1"
    sync; diskstat "$d/diskstats.after-w1"
    [ -n "$mount" ] && "$BIN/query" "$mount" info > "$d/get_info.txt" 2>&1 || true
    "$BIN/moobfs-check" "$target" > "$d/check.json" || { echo "run.sh: moobfs-check failed for $name"; return 1; }
    rm -f "$d/base.db"*
    SQLITE=$SQLITE sh "$SQLBASE" init "$d/base.db"
    SQLITE=$SQLITE sh "$SQLBASE" load "$d/base.db" "$out/manifest.txt" "$target"
    if [ -n "$mount" ]; then
        env SQLITE="$SQLITE" SQLBASE="$SQLBASE" BIN="$BIN" sh "$EQUIV" "$out/queries.txt" "$d/base.db" "$lower" "$mount" > "$d/equiv.txt" || { cat "$d/equiv.txt"; return 1; }
    else
        env SQLITE="$SQLITE" SQLBASE="$SQLBASE" BIN="$BIN" sh "$EQUIV" "$out/queries.txt" "$d/base.db" "$target" > "$d/equiv.txt" || { cat "$d/equiv.txt"; return 1; }
    fi
    tail -1 "$d/equiv.txt" | tee -a "$out/run.log"
    [ "$QUERY_ONLY" = 1 ] || "$BIN/bench-load" --manifest "$out/manifest.txt" --target "$target" --workload W2 --procs "$procs" --out "$d/w2"
    if [ -n "$mount" ]; then
        "$BIN/bench-load" --queries "$out/queries.txt" --target "$target" --root "$mount" --workload W3 --repeat "$W3_REPEAT" --out "$d/w3"
        # the same shapes as a regular user: every result goes through a real permission check;
        # recorded as a separate system "<name>+u" so the report keeps both
        mkdir -p "$rep/$name+u"
        "$BIN/bench-load" --queries "$out/queries.txt" --target "$target" --root "$mount" --workload W3 --repeat "$W3_REPEAT" --uid "$W3_UID" --out "$rep/$name+u/w3"
    else
        # W3 on a baseline: the same shapes through the SQLite library (bench/sqlclient)
        "$BIN/bench-sqlclient" --db "$d/base.db" --sql "$out/queries.sql" --repeat "$W3_REPEAT" --out "$d/w3"
    fi
    if [ "$QUERY_ONLY" = 1 ]; then
        if [ -n "$mount" ] && [ "$(id -u)" = 0 ]; then
            "$BIN/bench-load" --workload W7 --target "$mount" --lower "$lowerroot" --mount-opts "$LAYER_MOUNT_OPTS" --queries "$out/queries.txt" --out "$d/w7" --repeat 1
            "$BIN/query" "$mount" info > "$d/get_info.after-w7.txt" 2>&1 || true
        fi
        sync; diskstat "$d/diskstats.after"
        log "$name: done (query only)"
        return 0
    fi
    "$BIN/bench-load" --manifest "$out/manifest.txt" --target "$target" --workload W4 --procs "$procs" --out "$d/w4"
    if [ -n "$mount" ]; then
        "$BIN/bench-load" --manifest "$out/manifest.txt" --target "$target" --root "$mount" --queries "$out/queries.txt" --workload W8 --procs "$procs" --out "$d/w8"
        "$BIN/bench-load" --manifest "$out/manifest.txt" --target "$target" --root "$mount" --queries "$out/queries.txt" --workload W5 --procs "$procs" --out "$d/w5"
        if [ "$(id -u)" = 0 ]; then
            "$BIN/bench-load" --workload W6 --target "$mount" --lower "$lowerroot" --mount-opts "$LAYER_MOUNT_OPTS" --queries "$out/queries.txt" --out "$d/w6" --repeat 1
            "$BIN/bench-load" --workload W7 --target "$mount" --lower "$lowerroot" --mount-opts "$LAYER_MOUNT_OPTS" --queries "$out/queries.txt" --out "$d/w7" --repeat 1
        else
            log "$name: W6/W7 skipped (not root)"
        fi
    else
        "$BIN/bench-load" --manifest "$out/manifest.txt" --target "$target" --workload W8 --procs "$procs" --out "$d/w8"
    fi
    sync; diskstat "$d/diskstats.after"
    log "$name: done"
}

r=1
while [ "$r" -le "$reps" ]; do
    rep="$out/rep$r"; mkdir -p "$rep"
    # alternate the order between repetitions: odd reps plain first, even reps layer first
    if [ $((r % 2)) -eq 1 ]; then order="plain layer"; else order="layer plain"; fi
    for kind in $order; do
        if [ "$kind" = plain ]; then
            for spec in $plain; do run_system "$rep" "${spec%%=*}" "${spec#*=}" "" "" || exit 1; done
        else
            for spec in $layer; do
                name=${spec%%=*}; rest=${spec#*=}; mnt=${rest%%:*}; low=${rest#*:}
                run_system "$rep" "$name" "$mnt/bench" "$mnt" "$low" || exit 1
            done
        fi
    done
    r=$((r + 1))
done
if command -v python3 > /dev/null 2>&1; then
    python3 "$REPORT" --out "$out/report" "$out"/rep* | tee "$out/report.txt"
else
    log "python3 absent: run bench/report.py --out $out/report $out/rep* on a host to produce results.csv"
fi
# the W1 tree of a layer system is removed so the mount is left as it was found
for spec in $layer; do rest=${spec#*=}; rm -rf "${rest%%:*}/bench"; done
if true; then :
fi
log "BENCH_RUN_DONE: $reps repetitions, artifacts in $out"

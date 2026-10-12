#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# q2-gaps.sh: the measurements journals 0044-0049 left open, on the Mac mini (journal 0055).
#   1. mount time with a cold and a warm page cache (60 000 memories already on the volume)
#   2. QUERY2 and v1 right after dropping the page cache (one call per repetition)
#   3. write cost with domains on (domain_depth=1) and off (domain_depth=0): W1 creation and
#      W4 attribute updates of 10 000 memories
# Run as root: q2-gaps.sh OUT
set -eu
OUT=$1; L=/srv/moob/layer-ext4; LOW=/srv/moob/ext4/root; B=/opt/moob-test
HERE=/home/debian/moobfs/bench
[ ! -e "$OUT" ] || { echo "q2-gaps: $OUT exists"; exit 1; }
mkdir -p "$OUT"
[ -d "$LOW/ns3" ] || { echo "q2-gaps: no 60k dataset under $LOW"; exit 1; }
drop() { sync; echo 3 > /proc/sys/vm/drop_caches; }
ms() { echo $(( ($(date +%s%N) - $1) / 1000000 )); }
remount() { umount "$L"; mount -t moobfs_layer -o "lower=$LOW${1:+,$1}" none "$L"; }
log() { echo "q2-gaps: $(date -u +%H:%M:%S) $*"; }
{ uname -r; sha256sum /lib/modules/*/updates/moobfs_layer.ko 2>/dev/null || true; lsmod | grep moobfs; } > "$OUT/host.txt"

log "mount times"
for r in 1 2 3; do
    umount "$L"; drop; s=$(date +%s%N); mount -t moobfs_layer -o "lower=$LOW" none "$L"; c=$(ms $s)
    umount "$L"; s=$(date +%s%N); mount -t moobfs_layer -o "lower=$LOW" none "$L"; w=$(ms $s)
    echo "rep $r cold_ms $c warm_ms $w" | tee -a "$OUT/mount.txt"
done
"$B/query" "$L" info > "$OUT/info.txt"

log "cold-cache queries"
printf '%s\n' "namespace eq string ns3 agent eq string a07" > "$OUT/q-ns3.txt"
grep -v '^#' "$HERE/queries-w3.txt" | sed -n 3p > "$OUT/q-v1.txt"
for r in $(seq 1 20); do
    mkdir -p "$OUT/cold/rep$r/v2-cold" "$OUT/cold/rep$r/v2-warm"
    drop; "$B/bench-load" --workload W3 --target "$L/ns3" --root "$L/ns3" --query2 1 --uid 1001 --queries "$OUT/q-ns3.txt" --repeat 1 --out "$OUT/cold/rep$r/v2-cold/w3"
    "$B/bench-load" --workload W3 --target "$L/ns3" --root "$L/ns3" --query2 1 --uid 1001 --queries "$OUT/q-ns3.txt" --repeat 1 --out "$OUT/cold/rep$r/v2-warm/w3"
done
remount domain_depth=0
for r in $(seq 1 20); do
    mkdir -p "$OUT/cold/rep$r/v1-cold" "$OUT/cold/rep$r/v1-warm"
    drop; "$B/bench-load" --workload W3 --target "$L" --root "$L" --uid 1001 --queries "$OUT/q-v1.txt" --repeat 1 --out "$OUT/cold/rep$r/v1-cold/w3"
    "$B/bench-load" --workload W3 --target "$L" --root "$L" --uid 1001 --queries "$OUT/q-v1.txt" --repeat 1 --out "$OUT/cold/rep$r/v1-warm/w3"
done
reps=""; for r in $(seq 1 20); do reps="$reps $OUT/cold/rep$r"; done
# shellcheck disable=SC2086
python3 "$HERE/report.py" --out "$OUT/cold/report" $reps > /dev/null

log "write cost, domains on and off"
"$B/bench-gen" --seed 7 --inodes 10000 --profile 8 --out "$OUT/manifest-10k.txt"
for r in 1 2 3; do
    for mode in on off; do
        if [ $mode = on ]; then remount ""; else remount domain_depth=0; fi
        d=$OUT/write/rep$r/$mode; mkdir -p "$d"
        rm -rf "$L/wbench"; mkdir "$L/wbench"; chmod 0755 "$L/wbench"; sync
        "$B/bench-load" --manifest "$OUT/manifest-10k.txt" --target "$L/wbench" --workload W1 --procs 4 --out "$d/w1"
        "$B/bench-load" --manifest "$OUT/manifest-10k.txt" --target "$L/wbench" --workload W4 --procs 4 --out "$d/w4"
        "$B/query" "$L" info > "$d/info.txt"
    done
done
rm -rf "$L/wbench"; remount ""
reps=""; for r in 1 2 3; do reps="$reps $OUT/write/rep$r"; done
# shellcheck disable=SC2086
python3 "$HERE/report.py" --out "$OUT/write/report" $reps > /dev/null
echo "Q2_GAPS_DONE"

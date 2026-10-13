#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# The user-space baseline (bench/userspace/) against the layer on one host: the same data set
# written to a plain directory followed by udx-watch and to a layer mount, then the same query
# shapes through SQLite on the baseline's per-domain database and through QUERY2 (journal 0094).
#
#   sudo env BIN=<built tools> sh bench/userspace/compare.sh <plain dir> <layer mount> <its lower> <out dir> [scale]
#
# The plain directory and the lower must be empty and on the same filesystem. Run as root.
set -eu
BIN=${BIN:-build/host}
PLAIN=$1; LAYER=$2; LOWER=$3; OUT=$4; SCALE=${5:-60000}
here=$(cd "$(dirname "$0")/.." && pwd)
[ "$(id -u)" = 0 ] || { echo "compare.sh: run as root" >&2; exit 1; }
[ -z "$(ls -A "$PLAIN")" ] && [ -z "$(ls -A "$LOWER")" ] || { echo "compare.sh: $PLAIN and $LOWER must be empty" >&2; exit 1; }
mkdir -p "$OUT"
log() { echo "compare: $(date -u +%H:%M:%S) $*" | tee -a "$OUT/run.log"; }
{ uname -r; cat /sys/module/moobfs_layer/version; sqlite3 --version | cut -d' ' -f1; } > "$OUT/environment.txt"
"$BIN/bench-gen" --seed 1 --inodes "$SCALE" --profile 8 --out "$OUT/manifest.txt"

log "baseline: W1 and W4 on the plain directory with udx-watch running"
"$BIN/udx-watch" "$PLAIN" 1 > "$OUT/watch.out" 2> "$OUT/watch.err" &
wpid=$!
i=0; until grep -q UDX_WATCH_READY "$OUT/watch.out" "$OUT/watch.err" 2>/dev/null; do sleep 1; i=$((i + 1)); [ $i -lt 60 ] || break; done
mkdir -p "$OUT/write/udx" "$OUT/write/layer"
"$BIN/bench-load" --manifest "$OUT/manifest.txt" --target "$PLAIN" --workload W1 --procs 4 --out "$OUT/write/udx/w1"
"$BIN/bench-load" --manifest "$OUT/manifest.txt" --target "$PLAIN" --workload W4 --procs 4 --out "$OUT/write/udx/w4"
t0=$(date +%s%N)
# settled when two status reads a second apart agree and say synced
prev=""; while :; do cur=$("$BIN/udx-query" --status "$PLAIN/ns3" 2>&1 || true); [ "$cur" = "$prev" ] && echo "$cur" | grep -q "state=synced" && break; prev=$cur; sleep 1; done
echo "settle_after_writes_ms=$(( ($(date +%s%N) - t0) / 1000000 )) status=[$cur]" > "$OUT/baseline.txt"
kill -USR1 $wpid; sleep 1; tail -1 "$OUT/watch.err" >> "$OUT/baseline.txt"
grep -E 'VmRSS|VmHWM' /proc/$wpid/status | tr '\n' ' ' >> "$OUT/baseline.txt"; echo >> "$OUT/baseline.txt"
echo "index_bytes_on_disk=$(du -cb "$PLAIN"/*/.udx.db* | tail -1 | cut -f1)" >> "$OUT/baseline.txt"

log "layer: W1 and W4 through the mount"
"$BIN/bench-load" --manifest "$OUT/manifest.txt" --target "$LAYER" --workload W1 --procs 4 --out "$OUT/write/layer/w1"
"$BIN/bench-load" --manifest "$OUT/manifest.txt" --target "$LAYER" --workload W4 --procs 4 --out "$OUT/write/layer/w4"
chmod 0755 "$LOWER" "$PLAIN"
opts=$(awk -v m="$LAYER" '$2 == m { print $4 }' /proc/mounts | sed 's/^rw,relatime,//')
umount "$LAYER"; mount -t moobfs_layer -o "$opts" none "$LAYER"
"$BIN/query" "$LAYER" info > "$OUT/layer-info.txt"

printf '%s\n' "namespace eq string ns3 agent eq string a07" "agent eq string a07" "kind eq string note" > "$OUT/q-ns3.txt"
printf '%s\n' "kind eq string note" "agent eq string a07" > "$OUT/q-recent.txt"
log "equal answers on this data set"
n=0; bad=0
while read -r q; do
    n=$((n + 1))
    "$BIN/moobfs-index" --domain "$PLAIN/ns3" $q | grep -v '\.udx\.db' > "$OUT/eq.oracle" || true
    "$BIN/udx-query" "$PLAIN/ns3" $q > "$OUT/eq.udx"
    "$BIN/query" "$LAYER/ns3" q2 $q | cut -f2 | sort > "$OUT/eq.layer.paths"
    cut -f2 "$OUT/eq.oracle" | sort > "$OUT/eq.oracle.paths"
    cmp -s "$OUT/eq.oracle" "$OUT/eq.udx" && cmp -s "$OUT/eq.oracle.paths" "$OUT/eq.layer.paths" || bad=$((bad + 1))
    echo "shape $n: oracle=$(wc -l < "$OUT/eq.oracle") udx=$(wc -l < "$OUT/eq.udx") layer=$(wc -l < "$OUT/eq.layer.paths")" >> "$OUT/equal.txt"
done < "$OUT/q-ns3.txt"
echo "EQUAL: shapes=$n different=$bad" | tee -a "$OUT/equal.txt"

while read -r q; do "$BIN/udx-query" --sql "$PLAIN/ns3" $q; done < "$OUT/q-ns3.txt" > "$OUT/q-ns3.sql"
while read -r q; do "$BIN/udx-query" --sql --order created time desc --limit 20 "$PLAIN/ns3" $q; done < "$OUT/q-recent.txt" > "$OUT/q-recent.sql"
for r in 1 2 3; do
    d=$OUT/rep$r; mkdir -p "$d/udx-ns3" "$d/udx-recent" "$d/v2-ns3" "$d/v2-recent"
    log "rep $r: queries"
    "$BIN/bench-sqlclient" --db "$PLAIN/ns3/.udx.db" --sql "$OUT/q-ns3.sql" --out "$d/udx-ns3/w3" --repeat 50
    "$BIN/bench-sqlclient" --db "$PLAIN/ns3/.udx.db" --sql "$OUT/q-recent.sql" --out "$d/udx-recent/w3" --repeat 50
    "$BIN/bench-load" --workload W3 --target "$LAYER/ns3" --root "$LAYER/ns3" --query2 1 --uid 1001 --queries "$OUT/q-ns3.txt" --repeat 50 --out "$d/v2-ns3/w3"
    "$BIN/bench-load" --workload W3 --target "$LAYER/ns3" --root "$LAYER/ns3" --query2 1 --uid 1001 --order created:time:desc --limit 20 --queries "$OUT/q-recent.txt" --repeat 50 --out "$d/v2-recent/w3"
done
kill $wpid 2>/dev/null || true
python3 "$here/report.py" --out "$OUT/report" "$OUT/write" "$OUT"/rep? > "$OUT/report.log" 2>&1 || true
log "COMPARE_DONE"

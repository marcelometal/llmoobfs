#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# q2-campaign.sh: QUERY2 against the v1 QUERY on one dataset (journal 0044). Run as root on a
# layer mounted with an empty lower root. Builds a bench/gen dataset with W1 directly under the
# layer root, so the namespaces ns0..ns7 are the domains, then measures with bench-load W3:
#   v1-root   the nine v1 shapes of bench/queries-w3.txt as root (domain_depth=1)
#   v2-ns3    QUERY2 on ns3 as --uid: namespace AND agent (same predicate and answer as v1 Q3),
#             agent alone, kind alone
#   v2-ns1    QUERY2 on ns1 as --uid: namespace AND kind AND created (v1 Q4)
#   v2-recent QUERY2 on ns3 as --uid, ordered by created descending, limit 20
#   v1-user   the nine v1 shapes as --uid, remounted with domain_depth=0 (per-result checks)
# and checks that v1 and QUERY2 return the same inodes for Q3 and Q4.
#   q2-campaign.sh --layer MOUNT --lower DIR --out DIR [--scale 60000] [--profile 8] [--reps 3]
#                  [--repeat 50] [--uid 1001] [--seed 1] [--procs 4]
set -eu
BIN=${BIN:-/opt/moob-test}
here=$(cd "$(dirname "$0")" && pwd)
layer=""; lower=""; out=""; scale=60000; profile=8; reps=3; repeat=50; uid=1001; seed=1; procs=4
while [ $# -gt 0 ]; do
    case $1 in
        --layer) layer=$2; shift 2 ;;
        --lower) lower=$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        --scale) scale=$2; shift 2 ;;
        --profile) profile=$2; shift 2 ;;
        --reps) reps=$2; shift 2 ;;
        --repeat) repeat=$2; shift 2 ;;
        --uid) uid=$2; shift 2 ;;
        --seed) seed=$2; shift 2 ;;
        --procs) procs=$2; shift 2 ;;
        *) echo "q2-campaign.sh: unknown option $1" >&2; exit 64 ;;
    esac
done
[ -n "$layer" ] && [ -n "$lower" ] && [ -n "$out" ] || { echo "usage: see the header of q2-campaign.sh" >&2; exit 64; }
[ "$(id -u)" = 0 ] || { echo "q2-campaign.sh: run as root" >&2; exit 1; }
[ ! -e "$out" ] || { echo "q2-campaign.sh: $out exists" >&2; exit 1; }
[ -z "$(ls -A "$lower" | grep -v '^lost+found$' || true)" ] || { echo "q2-campaign.sh: $lower is not empty" >&2; exit 1; }
log() { echo "q2-campaign: $(date -u +%H:%M:%S) $*"; }
remount() { umount "$layer"; mount -t moobfs_layer -o "lower=$lower${1:+,$1}" none "$layer"; }
mkdir -p "$out"
{ uname -r; "$BIN/query" "$layer" info; } > "$out/host.txt"

"$BIN/bench-gen" --seed "$seed" --inodes "$scale" --profile "$profile" --out "$out/manifest.txt"
log "W1: $scale memories, profile $profile"
"$BIN/bench-load" --manifest "$out/manifest.txt" --target "$layer" --workload W1 --procs "$procs" --out "$out/w1"
chmod 0755 "$lower"
remount ""
"$BIN/query" "$layer" info > "$out/info.after-w1.txt"
t=$(grep '^namespace eq string ns1 kind eq string fact created ge time' "$here/queries-w3.txt" | awk '{print $NF}')
grep -v '^#' "$here/queries-w3.txt" > "$out/q-v1.txt"
printf '%s\n' "namespace eq string ns3 agent eq string a07" "agent eq string a07" "kind eq string note" > "$out/q-ns3.txt"
printf '%s\n' "namespace eq string ns1 kind eq string fact created ge time $t" > "$out/q-ns1.txt"
printf '%s\n' "kind eq string note" "agent eq string a07" > "$out/q-recent.txt"

for r in $(seq 1 "$reps"); do
    d=$out/rep$r; mkdir -p "$d/v1-root" "$d/v2-ns3" "$d/v2-ns1" "$d/v2-recent"
    log "rep $r: v1 as root"
    "$BIN/bench-load" --workload W3 --target "$layer" --root "$layer" --queries "$out/q-v1.txt" --repeat "$repeat" --out "$d/v1-root/w3"
    log "rep $r: QUERY2 as uid $uid"
    "$BIN/bench-load" --workload W3 --target "$layer/ns3" --root "$layer/ns3" --query2 1 --uid "$uid" --queries "$out/q-ns3.txt" --repeat "$repeat" --out "$d/v2-ns3/w3"
    "$BIN/bench-load" --workload W3 --target "$layer/ns1" --root "$layer/ns1" --query2 1 --uid "$uid" --queries "$out/q-ns1.txt" --repeat "$repeat" --out "$d/v2-ns1/w3"
    "$BIN/bench-load" --workload W3 --target "$layer/ns3" --root "$layer/ns3" --query2 1 --uid "$uid" --order created:time:desc --limit 20 --queries "$out/q-recent.txt" --repeat "$repeat" --out "$d/v2-recent/w3"
done
log "equivalence v1 against QUERY2 (inodes)"
"$BIN/query" "$layer" namespace eq string ns3 agent eq string a07 | cut -f1 | sort -n > "$out/eq-q3-v1.txt"
"$BIN/query" --uid "$uid" "$layer/ns3" q2 namespace eq string ns3 agent eq string a07 | cut -f1 | sort -n > "$out/eq-q3-v2.txt"
"$BIN/query" "$layer" namespace eq string ns1 kind eq string fact created ge time "$t" | cut -f1 | sort -n > "$out/eq-q4-v1.txt"
"$BIN/query" --uid "$uid" "$layer/ns1" q2 namespace eq string ns1 kind eq string fact created ge time "$t" | cut -f1 | sort -n > "$out/eq-q4-v2.txt"
for q in q3 q4; do
    if cmp -s "$out/eq-$q-v1.txt" "$out/eq-$q-v2.txt"; then echo "EQUIV_V1_V2 $q: $(wc -l < "$out/eq-$q-v1.txt") inodes"; else echo "EQUIV_V1_V2_MISMATCH $q"; fi
done | tee "$out/equivalence.txt"
log "v1 as uid $uid (domain_depth=0)"
remount domain_depth=0
for r in $(seq 1 "$reps"); do
    mkdir -p "$out/rep$r/v1-user"
    "$BIN/bench-load" --workload W3 --target "$layer" --root "$layer" --uid "$uid" --queries "$out/q-v1.txt" --repeat "$repeat" --out "$out/rep$r/v1-user/w3"
done
remount ""
reps_dirs=""; for r in $(seq 1 "$reps"); do reps_dirs="$reps_dirs $out/rep$r"; done
# shellcheck disable=SC2086
python3 "$here/report.py" --out "$out/report" $reps_dirs
grep -q MISMATCH "$out/equivalence.txt" && { echo "Q2_CAMPAIGN_FAIL: equivalence"; exit 1; }
echo "Q2_CAMPAIGN_PASS: report in $out/report"

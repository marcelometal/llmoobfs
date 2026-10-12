#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# q2c-compare.sh: QUERY2 plans of two module builds on the same dataset (journal 0044). Run as
# root after bench/q2-campaign.sh, on the same layer and lower (the dataset stays). For each
# module, in the order given, unmounts the layer, reloads the module, remounts (domain_depth=1)
# and runs bench-load W3 --query2 as --uid on domain ns3 (and ns1) for these shapes:
#   q-ns3:    namespace AND agent; agent AND namespace (the other term leads); agent alone;
#             agent AND kind (two selective equality terms)
#   q-ns1:    namespace AND kind AND created (v1 Q4)
#   q-recent: kind, agent; ordered by created descending, limit 20
#   q2c-compare.sh --layer MOUNT --lower DIR --out DIR --module NAME=KO [--module NAME=KO ...]
#                  [--reps 3] [--repeat 50] [--uid 1001]
set -eu
BIN=${BIN:-/opt/moob-test}
here=$(cd "$(dirname "$0")" && pwd)
layer=""; lower=""; out=""; modules=""; reps=3; repeat=50; uid=1001
while [ $# -gt 0 ]; do
    case $1 in
        --layer) layer=$2; shift 2 ;;
        --lower) lower=$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        --module) modules="$modules $2"; shift 2 ;;
        --reps) reps=$2; shift 2 ;;
        --repeat) repeat=$2; shift 2 ;;
        --uid) uid=$2; shift 2 ;;
        *) echo "q2c-compare.sh: unknown option $1" >&2; exit 64 ;;
    esac
done
[ -n "$layer" ] && [ -n "$lower" ] && [ -n "$out" ] && [ -n "$modules" ] || { echo "usage: see the header of q2c-compare.sh" >&2; exit 64; }
[ "$(id -u)" = 0 ] || { echo "q2c-compare.sh: run as root" >&2; exit 1; }
[ ! -e "$out" ] || { echo "q2c-compare.sh: $out exists" >&2; exit 1; }
[ -d "$lower/ns3" ] && [ -d "$lower/ns1" ] || { echo "q2c-compare.sh: no dataset in $lower (run q2-campaign.sh first)" >&2; exit 1; }
log() { echo "q2c-compare: $(date -u +%H:%M:%S) $*"; }
mkdir -p "$out"
t=$(grep '^namespace eq string ns1 kind eq string fact created ge time' "$here/queries-w3.txt" | awk '{print $NF}')
printf '%s\n' "namespace eq string ns3 agent eq string a07" "agent eq string a07 namespace eq string ns3" \
    "agent eq string a07" "agent eq string a07 kind eq string note" > "$out/q-ns3.txt"
printf '%s\n' "namespace eq string ns1 kind eq string fact created ge time $t" > "$out/q-ns1.txt"
printf '%s\n' "kind eq string note" "agent eq string a07" > "$out/q-recent.txt"
for m in $modules; do
    name=${m%%=*}; ko=${m#*=}
    log "module $name ($(sha256sum "$ko" | cut -c1-16))"
    umount "$layer"
    rmmod moobfs_layer
    insmod "$ko"
    s=$(date +%s%N)
    mount -t moobfs_layer -o "lower=$lower" none "$layer"
    "$BIN/query" "$layer" info > "$out/info-$name.txt"
    echo "mount_ms $(( ($(date +%s%N) - s) / 1000000 ))" >> "$out/info-$name.txt"
    for r in $(seq 1 "$reps"); do
        d=$out/rep$r; mkdir -p "$d/$name-ns3" "$d/$name-ns1" "$d/$name-recent"
        "$BIN/bench-load" --workload W3 --target "$layer/ns3" --root "$layer/ns3" --query2 1 --uid "$uid" --queries "$out/q-ns3.txt" --repeat "$repeat" --out "$d/$name-ns3/w3"
        "$BIN/bench-load" --workload W3 --target "$layer/ns1" --root "$layer/ns1" --query2 1 --uid "$uid" --queries "$out/q-ns1.txt" --repeat "$repeat" --out "$d/$name-ns1/w3"
        "$BIN/bench-load" --workload W3 --target "$layer/ns3" --root "$layer/ns3" --query2 1 --uid "$uid" --order created:time:desc --limit 20 --queries "$out/q-recent.txt" --repeat "$repeat" --out "$d/$name-recent/w3"
    done
    for q in "namespace eq string ns3 agent eq string a07" "agent eq string a07 kind eq string note"; do
        # shellcheck disable=SC2086
        "$BIN/query" --uid "$uid" "$layer/ns3" q2 $q | cut -f1 | sort -n > "$out/answer-$name-$(echo "$q" | tr ' ' '_').txt"
    done
done
for f in "$out"/answer-*; do
    q=${f#*answer-*-}
    for g in "$out"/answer-*"$q"; do cmp -s "$f" "$g" || { echo "Q2C_COMPARE_MISMATCH: $f $g"; exit 1; }; done
done
reps_dirs=""; for r in $(seq 1 "$reps"); do reps_dirs="$reps_dirs $out/rep$r"; done
# shellcheck disable=SC2086
python3 "$here/report.py" --out "$out/report" $reps_dirs
echo "Q2C_COMPARE_PASS: same answers under every module; report in $out/report"

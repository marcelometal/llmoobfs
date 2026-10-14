#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# The second half of ADR-0012 measured (journal 0103): what a released domain costs when it is
# queried again. Two parts, on one mount with the module that is loaded:
#   1. shrinker: build one domain, release it through drop_caches (index_idle=0), time the next
#      query; with the page cache kept (drop_caches=2) and dropped (drop_caches=3);
#   2. budget: an index_budget that holds about BUDGET_DOMAINS domains, every domain queried in
#      turn for two rounds, against the same rounds with the mount's own budget.
#
#   sudo env BIN=<built tools> sh bench/release.sh <layer mount> <domain bytes> <out dir>
#
# <domain bytes> is the index size of one domain (GET_INFO "used" after its first query). The
# mount is unmounted and mounted again with its own options plus the ones under test, and left
# mounted with its own options; nothing is written to it.
set -eu
BIN=${BIN:-build/host}
REPS=${REPS:-5}
BUDGET_DOMAINS=${BUDGET_DOMAINS:-2.5}
LAYER=$1; DBYTES=$2; OUT=$3
[ "$(id -u)" = 0 ] || { echo "release.sh: run as root" >&2; exit 1; }
opts=$(awk -v m="$LAYER" '$2 == m { print $4 }' /proc/mounts | sed 's/^rw,relatime,//')
[ -n "$opts" ] || { echo "release.sh: $LAYER is not a mount" >&2; exit 1; }
mkdir -p "$OUT"
now() { date +%s%N; }
ms() { echo $(( ($(now) - $1) / 1000000 )); }
counters() { "$BIN/query" "$LAYER" info | sed -n 's/^domains_built=\([0-9]*\) domain_builds=\([0-9]*\) domain_releases=\([0-9]*\) .*/built=\1 builds=\2 releases=\3/p'; }
used() { "$BIN/query" "$LAYER" info | sed -n 's/.* used=\([0-9]*\) entries=.*/\1/p'; }
remount() { umount "$LAYER"; mount -t moobfs_layer -o "$opts${1:+,$1}" none "$LAYER"; }
q() { "$BIN/query" "$LAYER/$1" q2 kind eq string note > /dev/null; }
doms=$(cd "$LAYER" && for d in */; do printf '%s ' "${d%/}"; done)
first=${doms%% *}
{ uname -r; cat /sys/module/moobfs_layer/version; cat /sys/module/moobfs_layer/srcversion
  echo "mount options: $opts"; echo "domains: $doms"; echo "domain bytes: $DBYTES"; } > "$OUT/environment.txt"
: > "$OUT/times.txt"

# 1. released by the shrinker, then queried again
remount index_idle=0
for drop in 2 3; do
    for r in $(seq "$REPS"); do
        sync; echo 3 > /proc/sys/vm/drop_caches
        t=$(now); q "$first"; b=$(ms $t)
        t=$(now); q "$first"; h=$(ms $t)
        sync; echo $drop > /proc/sys/vm/drop_caches
        c=$(counters)
        t=$(now); q "$first"; a=$(ms $t)
        echo "shrinker drop_caches=$drop rep=$r first_build_ms=$b built_query_ms=$h after_drop[$c] rebuild_ms=$a after[$(counters)]" | tee -a "$OUT/times.txt"
    done
done

# 2. every domain in turn, under a budget that holds a few, and under the mount's own
budget=$(awk -v d="$DBYTES" -v n="$BUDGET_DOMAINS" 'BEGIN { printf "%d", d * n }')
for case in "budget index_budget=$budget" "own"; do
    name=${case%% *}; extra=${case#"$name"}; extra=${extra# }
    remount "$extra"
    sync; echo 3 > /proc/sys/vm/drop_caches
    for round in 1 2; do
        for d in $doms; do
            t=$(now); q "$d"; a=$(ms $t)
            echo "rotation $name${extra:+ ($extra)} round=$round domain=$d query_ms=$a used=$(used) [$(counters)]" | tee -a "$OUT/times.txt"
        done
    done
done
remount ""
echo "RELEASE_BENCH_DONE"

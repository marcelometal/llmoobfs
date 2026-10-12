#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# sqlite-baseline.sh: the SQLite side of an existing dataset (journal 0046). Builds the
# baseline database of bench/sqlbase.sh from the files a W1 run left in --target, then times
# with bench-sqlclient the SQL of every line of --queries plus every statement of --extra-sql
# (one per line), --reps repetitions of --repeat executions, and writes the report.
#   sqlite-baseline.sh --manifest FILE --target DIR --db FILE --queries FILE --out DIR
#                      [--extra-sql FILE] [--reps 3] [--repeat 50]
set -eu
BIN=${BIN:-/opt/moob-test}
SQLITE=${SQLITE:-sqlite3}
here=$(cd "$(dirname "$0")" && pwd)
manifest=""; target=""; db=""; queries=""; out=""; extra=""; reps=3; repeat=50
while [ $# -gt 0 ]; do
    case $1 in
        --manifest) manifest=$2; shift 2 ;;
        --target) target=$2; shift 2 ;;
        --db) db=$2; shift 2 ;;
        --queries) queries=$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        --extra-sql) extra=$2; shift 2 ;;
        --reps) reps=$2; shift 2 ;;
        --repeat) repeat=$2; shift 2 ;;
        *) echo "sqlite-baseline.sh: unknown option $1" >&2; exit 64 ;;
    esac
done
[ -n "$manifest" ] && [ -n "$target" ] && [ -n "$db" ] && [ -n "$queries" ] && [ -n "$out" ] || { echo "usage: see the header of sqlite-baseline.sh" >&2; exit 64; }
[ ! -e "$out" ] || { echo "sqlite-baseline.sh: $out exists" >&2; exit 1; }
[ ! -e "$db" ] || { echo "sqlite-baseline.sh: $db exists" >&2; exit 1; }
log() { echo "sqlite-baseline: $(date -u +%H:%M:%S) $*"; }
mkdir -p "$out"
"$SQLITE" --version > "$out/sqlite-version.txt"
log "load"
s=$(date +%s)
SQLITE=$SQLITE sh "$here/sqlbase.sh" init "$db"
SQLITE=$SQLITE sh "$here/sqlbase.sh" load "$db" "$manifest" "$target"
echo "load_s $(( $(date +%s) - s )) rows $("$SQLITE" "$db" 'SELECT count(*) FROM memories;')" > "$out/load.txt"
grep -v '^#' "$queries" | while read -r q; do SQLITE=$SQLITE sh "$here/sqlbase.sh" sql $q; done > "$out/queries.sql"
[ -z "$extra" ] || grep -v '^--' "$extra" >> "$out/queries.sql"
while read -r stmt; do echo "== $stmt"; "$SQLITE" "$db" "EXPLAIN QUERY PLAN $stmt"; done < "$out/queries.sql" > "$out/plans.txt"
for r in $(seq 1 "$reps"); do
    mkdir -p "$out/rep$r/sqlite"
    log "rep $r"
    "$BIN/bench-sqlclient" --db "$db" --sql "$out/queries.sql" --repeat "$repeat" --out "$out/rep$r/sqlite/w3"
done
reps_dirs=""; for r in $(seq 1 "$reps"); do reps_dirs="$reps_dirs $out/rep$r"; done
# shellcheck disable=SC2086
python3 "$here/report.py" --out "$out/report" $reps_dirs
echo "SQLITE_BASELINE_DONE: report in $out/report"

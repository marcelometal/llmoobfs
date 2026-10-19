#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Correctness smoke test of the benchmark harness itself (docs/benchmark-harness.md §9).
# Linux only; produces no performance numbers. Runs W1, W2 and W4 on a plain directory,
# checks determinism of gen, validates the created tree with moobfs-check and runs report.
# Usage: sh bench/smoke.sh [BUILD_DIR] [TARGET_DIR]
set -eu
BUILD=${1:-build/host}
TARGET=${2:-$BUILD/bench-smoke/target}
OUT=$BUILD/bench-smoke
rm -rf "$OUT" && mkdir -p "$OUT/rep1/plain" "$OUT/rep2/plain" "$OUT/rep1/w8" "$OUT/rep2/w8" "$TARGET"
fail() { echo "BENCH_SMOKE_FAIL: $*"; exit 1; }

"$BUILD/bench-gen" --seed 7 --inodes 300 --profile 16 --out "$OUT/m1.txt"
"$BUILD/bench-gen" --seed 7 --inodes 300 --profile 16 --out "$OUT/m2.txt"
cmp -s "$OUT/m1.txt" "$OUT/m2.txt" || fail "gen is not deterministic"
"$BUILD/bench-gen" --seed 8 --inodes 300 --profile 16 --out "$OUT/m3.txt"
cmp -s "$OUT/m1.txt" "$OUT/m3.txt" && fail "gen ignores the seed"
[ "$(grep -vc '^#' "$OUT/m1.txt")" -eq 300 ] || fail "manifest line count"
# every memory_id unique; 16 attributes per line in profile 16
[ "$(grep -v '^#' "$OUT/m1.txt" | grep -o 'memory_id=s:[^ ]*' | sort -u | wc -l)" -eq 300 ] || fail "memory_id not unique"
grep -v '^#' "$OUT/m1.txt" | awk -F'\t' '{ n = split($5, a, " "); if (n != 16 && n != 15) { print "line " NR " has " n " attrs"; exit 1 } }' || fail "attribute count"
for p in 6 8; do
    "$BUILD/bench-gen" --seed 1 --inodes 50 --profile $p --out "$OUT/p$p.txt"
    grep -v '^#' "$OUT/p$p.txt" | awk -F'\t' -v want=$p '{ n = split($5, a, " "); if (n != want && n != want - (want > 6)) exit 1 }' || fail "profile $p attribute count"
done

for rep in rep1 rep2; do
    rm -rf "$TARGET" && mkdir -p "$TARGET"
    "$BUILD/bench-load" --manifest "$OUT/m1.txt" --target "$TARGET" --workload W1 --procs 3 --out "$OUT/$rep/plain/w1"
    [ "$(find "$TARGET" -type f -name '*.md' | wc -l)" -eq 300 ] || fail "W1 created the wrong number of files"
    find "$TARGET" -name '*.tmp' | grep -q . && fail "W1 left temporary files"
    "$BUILD/moobfs-check" "$TARGET" > "$OUT/$rep/check.json" || { cat "$OUT/$rep/check.json"; fail "moobfs-check rejected the W1 tree"; }
    attrs=$(grep -v '^#' "$OUT/m1.txt" | awk -F'\t' '{ n += split($5, a, " ") } END { print n }')
    grep -q "\"typed_attributes\": $attrs" "$OUT/$rep/check.json" || fail "moobfs-check attribute count"
    # SQLite baseline right after W1 (W4 changes score later, so loading from the manifest afterwards would not match the files)
    rm -f "$OUT/$rep/base.db"*
    sh bench/sqlbase.sh init "$OUT/$rep/base.db"
    sh bench/sqlbase.sh load "$OUT/$rep/base.db" "$OUT/m1.txt" "$TARGET"
    [ "$(sqlite3 "$OUT/$rep/base.db" 'select count(*) from memories')" = 300 ] || fail "sqlbase row count"
    BIN=$BUILD sh bench/equiv.sh bench/queries-w3.txt "$OUT/$rep/base.db" "$TARGET" | tee "$OUT/$rep/equiv.txt" | tail -1 | grep -q '^EQUIV_PASS: 9 shapes' || fail "equivalence SQLite vs oracle"
    # the SQLite W3 client: one record per statement, row counts equal to the oracle's line counts
    grep -v '^#' bench/queries-w3.txt | while read -r q; do sh bench/sqlbase.sh sql $q; done > "$OUT/$rep/queries.sql"
    mkdir -p "$OUT/$rep/sqlite"
    "$BUILD/bench-sqlclient" --db "$OUT/$rep/base.db" --sql "$OUT/$rep/queries.sql" --out "$OUT/$rep/sqlite/w3" --repeat 2
    [ "$(wc -c < "$OUT/$rep/sqlite/w3.0.rec")" -eq $((18 * 32)) ] || fail "sqlclient record count"
    "$BUILD/bench-load" --manifest "$OUT/m1.txt" --target "$TARGET" --workload W2 --procs 2 --out "$OUT/$rep/plain/w2"
    "$BUILD/bench-load" --manifest "$OUT/m1.txt" --target "$TARGET" --workload W4 --procs 1 --out "$OUT/$rep/plain/w4"
    # 300 creates, 300 reads, 300 + 300 attribute ops: 32 bytes each
    total=$(cat "$OUT/$rep"/plain/*.rec | wc -c)
    [ "$total" -eq $((1200 * 32)) ] || fail "record count ($total bytes)"
    # W8 without a mount root: reads and attribute updates only, 10 processes, at least one record per memory
    "$BUILD/bench-load" --manifest "$OUT/m1.txt" --target "$TARGET" --workload W8 --procs 10 --out "$OUT/$rep/w8/mixed"
    [ "$(cat "$OUT/$rep"/w8/*.rec | wc -c)" -ge $((300 * 32)) ] || fail "W8 record count"
done
# an error must be recorded, not hidden: W2 against a missing tree
mkdir -p "$OUT/err/plain"
"$BUILD/bench-load" --manifest "$OUT/m1.txt" --target "$OUT/missing" --workload W2 --out "$OUT/err/plain/w2" || true
python3 bench/report.py --out "$OUT/report" "$OUT/rep1" "$OUT/rep2" > "$OUT/report.txt"
grep -q '^plain W1 create: n=300 err=0' "$OUT/report.txt" || { cat "$OUT/report.txt"; fail "report W1 row"; }
grep -q '^plain W4 attr_rm: n=300 err=0' "$OUT/report.txt" || fail "report W4 row"
[ "$(grep -c '^sqlite W3 query:Q[1-9]: n=2 err=0' "$OUT/report.txt")" -eq 9 ] || { cat "$OUT/report.txt"; fail "report sqlite W3 rows per shape"; }
python3 bench/report.py --out "$OUT/report-err" "$OUT/err" | grep -q 'W2 read: n=0 err=300' || fail "errors not reported"
[ -s "$OUT/report/results.csv" ] && [ -s "$OUT/report/results.json" ] || fail "report files"
python3 bench/compare.py --out "$OUT/compare" --a plain "$OUT/rep1" "$OUT/rep2" --b plain "$OUT/rep1" "$OUT/rep2" > "$OUT/compare.txt"
grep -q '^W1 create p50_us: n=2/2: too few repetitions' "$OUT/compare.txt" || { cat "$OUT/compare.txt"; fail "compare with 2 repetitions"; }
[ -s "$OUT/compare/compare.csv" ] && [ -s "$OUT/compare/ecdf.csv" ] || fail "compare files"
echo "BENCH_SMOKE_PASS: gen deterministic; W1/W2/W4/W8 on a plain directory; moobfs-check clean; SQLite baseline equal to the oracle on 9 shapes; SQLite W3 client recorded; report with 2 repetitions; compare refuses a verdict on 2"

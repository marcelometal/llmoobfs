#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# bench/equiv: for every W3 query line, the kernel QUERY, the SQLite baseline and the
# user-space oracle must return the same set of inodes, and no answer may be truncated
# (docs/benchmark-harness.md §4). Any difference aborts a measurement.
#
#   equiv.sh <queries-file> <db> <lower-dir> [<mount-root>]
# Environment: BIN (directory with query and moobfs-index; default: build/host),
# SQLBASE (default: bench/sqlbase.sh). Without <mount-root> only oracle and SQLite are compared.
# Exit 0 when all shapes agree (SCAN plans and kernel answers truncated at the 4096-result UAPI
# limit are reported, not fatal), 1 on any mismatch.
set -u
queries=$1; db=$2; lower=$3; mount=${4:-}
BIN=${BIN:-build/host}
SQLBASE=${SQLBASE:-bench/sqlbase.sh}
tmp=${TMPDIR:-/tmp}/equiv.$$
mkdir -p "$tmp"
rc=0; n=0
grep -v '^#' "$queries" | while read -r line; do
    [ -n "$line" ] || continue
    n=$((n + 1))
    # shellcheck disable=SC2086
    sh "$SQLBASE" plan "$db" $line > "$tmp/plan" 2>&1; prc=$?
    case $prc in
        0) planword=SEARCH ;;
        3) planword=SCAN; echo "equiv: query $n ($line): SQLite plan is a SCAN; a measurement must justify it or add an index (§4.1)"; echo "EQUIV_SCAN $n" ;;
        *) echo "equiv: query $n: unexpected SQLite plan:"; cat "$tmp/plan"; echo "EQUIV_FAIL $n"; continue ;;
    esac
    # shellcheck disable=SC2086
    sh "$SQLBASE" query "$db" $line | cut -d'|' -f1 | sort -n > "$tmp/sql" || { echo "EQUIV_FAIL $n sqlite"; continue; }
    # the oracle's own status: in a pipeline only the last command's counts, and a killed oracle
    # would leave an empty answer that looks valid (found with the sanitizer smoke test)
    # shellcheck disable=SC2086
    "$BIN/moobfs-index" "$lower" $line > "$tmp/oracle.raw" || { echo "EQUIV_FAIL $n oracle"; continue; }
    cut -f1 "$tmp/oracle.raw" | sort -n > "$tmp/oracle"
    if ! cmp -s "$tmp/sql" "$tmp/oracle"; then
        echo "equiv: query $n ($line): SQLite and oracle differ"; diff "$tmp/sql" "$tmp/oracle" | head -5; echo "EQUIV_FAIL $n"; continue
    fi
    if [ -n "$mount" ]; then
        # shellcheck disable=SC2086
        "$BIN/query" "$mount" $line > "$tmp/kraw" 2> "$tmp/kerr" || { echo "EQUIV_FAIL $n kernel"; continue; }
        # QUERY returns at most 4096 results (UAPI v1, no paging): a wider answer is truncated,
        # so it cannot be compared; SQLite and the oracle were already compared above
        if grep -q 'truncated' "$tmp/kerr"; then
            echo "equiv: query $n ($line): kernel answer truncated at the UAPI limit; $(wc -l < "$tmp/oracle" | tr -d ' ') matches"; echo "EQUIV_TRUNCATED $n"; continue
        fi
        cut -f1 "$tmp/kraw" | sort -n > "$tmp/kernel"
        if ! cmp -s "$tmp/kernel" "$tmp/oracle"; then
            echo "equiv: query $n ($line): kernel and oracle differ"; diff "$tmp/kernel" "$tmp/oracle" | head -5; echo "EQUIV_FAIL $n"; continue
        fi
    fi
    echo "equiv: query $n: $(wc -l < "$tmp/oracle" | tr -d ' ') inodes, sets agree, SQLite plan $planword"
done > "$tmp/log"
cat "$tmp/log"
grep -q 'EQUIV_FAIL' "$tmp/log" && rc=1
total=$(grep -c '^equiv: query [0-9]*: [0-9]* inodes' "$tmp/log")
scans=$(grep -c '^EQUIV_SCAN' "$tmp/log")
truncs=$(grep -c '^EQUIV_TRUNCATED' "$tmp/log")
rm -rf "$tmp"
if [ "$rc" -eq 0 ]; then
    if [ -n "$mount" ]; then who="kernel, SQLite, oracle"; else who="SQLite, oracle"; fi
    echo "EQUIV_PASS: $total shapes ($who); SQLite SCAN plans to justify: $scans; kernel answers truncated at the UAPI limit: $truncs"
fi
exit $rc

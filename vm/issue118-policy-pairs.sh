#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Sequential matched-image VM pairs, alternating module order. Opt-in only.
# Usage: vm/issue118-policy-pairs.sh PREFIX PAIRS FILES READ_DELAY_MS SHAPE SEED_RUN
set -eu
[ "$#" -eq 6 ] || {
    echo 'usage: issue118-policy-pairs.sh PREFIX PAIRS FILES READ_DELAY_MS SHAPE SEED_RUN' >&2
    exit 64
}
prefix=$1 pairs=$2 files=$3 delay_ms=$4 shape=$5 seed_run=$6
case "$prefix" in ''|*[!a-zA-Z0-9_-]*) echo 'invalid prefix' >&2; exit 64 ;; esac
case "$pairs" in ''|*[!0-9]*) echo 'invalid pair count' >&2; exit 64 ;; esac
[ "$pairs" -ge 1 ] && [ "$pairs" -le 30 ] || exit 64
repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
i=1
while [ "$i" -le "$pairs" ]; do
    if [ $((i % 2)) -eq 1 ]; then
        first=current second=prelimit
    else
        first=prelimit second=current
    fi
    for module in "$first" "$second"; do
        run_id=$(printf '%s-p%02d-%s' "$prefix" "$i" "$module")
        sh "$repo/vm/issue118-policy-host.sh" "$run_id" "$files" \
            "$delay_ms" "$shape" "$module" "$seed_run"
    done
    printf 'MOOBFS_POLICY_PAIR_PASS: pair=%s first=%s second=%s\n' \
        "$i" "$first" "$second"
    i=$((i + 1))
done

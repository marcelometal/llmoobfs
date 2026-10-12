#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# stage1b-retrieval.sh: stage 1b of docs/client-test-plan.md. Runs ai-memory's LongMemEval retrieval
# harness (zero-LLM, offline, deterministic: a real `ai-memory serve` on a fresh data directory, the
# haystacks replayed through its hooks, questions through MCP memory_query) with the server's data
# directory on plain ext4 and inside the MoobFS layer on the same SSD. The harness creates the data
# directory with tempfile, so TMPDIR chooses where it lives. Compares accuracy (must be equal) and the
# memory_query latency. Run as root; the harness runs as the user owning the ai-memory checkout.
#   stage1b-retrieval.sh OUT [SAMPLE]      (SAMPLE: number of questions, default 50; "all" for 500)
set -eu
OUT=$1; SAMPLE=${2:-50}
AIM=${AIM:-/home/debian/clients/ai-memory}
USER_NAME=${USER_NAME:-debian}
PLAIN=${PLAIN:-/srv/moob/ext4/aimem-plain}
LAYERD=${LAYERD:-/srv/moob/layer-ext4/aimem}
[ ! -e "$OUT" ] || { echo "stage1b: $OUT exists" >&2; exit 1; }
mkdir -p "$OUT"
[ -f "$AIM/evals/datasets/longmemeval_s.json" ] || { echo "stage1b: dataset missing" >&2; exit 1; }
if [ "$SAMPLE" = all ]; then ARGS=""; else ARGS="--sample $SAMPLE"; fi
{ su "$USER_NAME" -c "git -C $AIM log -1 --format='ai-memory %h %ad %s' --date=short"; uname -r; echo "sample: $SAMPLE"; } > "$OUT/env.txt"
su "$USER_NAME" -c "cd $AIM && PATH=\$HOME/.cargo/bin:\$PATH cargo build --release --locked -p ai-memory-cli -p ai-memory-eval" > "$OUT/build.log" 2>&1
for where in plain layer; do
    if [ $where = plain ]; then dir=$PLAIN; else dir=$LAYERD; fi
    rm -rf "$dir"; mkdir -p "$dir/tmp"; chown -R "$USER_NAME:$USER_NAME" "$dir"
    s=$(date +%s); rc=0
    su "$USER_NAME" -c "cd $AIM && PATH=\$HOME/.cargo/bin:\$PATH TMPDIR=$dir/tmp cargo run --release --locked -p ai-memory-eval -- retrieval $ARGS" > "$OUT/$where.log" 2>&1 || rc=$?
    echo "$where exit=$rc seconds=$(( $(date +%s) - s ))" | tee -a "$OUT/summary.txt"
    latest=$(ls -td "$AIM"/evals/runs/*-retrieval 2>/dev/null | head -1)
    [ -n "$latest" ] && cp -r "$latest" "$OUT/$where-report"
    rm -rf "$dir"
done
echo "STAGE1B_DONE: $(tr '\n' ' ' < "$OUT/summary.txt")"

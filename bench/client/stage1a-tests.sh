#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# stage1a-tests.sh: stage 1a of docs/client-test-plan.md. Runs ai-memory's own test suites for the
# crates that touch the file system most (wiki in Markdown under git, SQLite store, JSONL
# workstreams) twice, with TMPDIR on plain ext4 and inside the MoobFS layer on the same SSD, and
# records the result lines and the wall time of each run. Run as root; the tests run as the
# unprivileged user that owns the ai-memory checkout.
#   stage1a-tests.sh OUT
set -eu
OUT=$1
AIM=${AIM:-/home/debian/clients/ai-memory}
USER_NAME=${USER_NAME:-debian}
PLAIN=${PLAIN:-/srv/moob/ext4/aimem-plain}
LAYERD=${LAYERD:-/srv/moob/layer-ext4/aimem}
CRATES="-p ai-memory-wiki -p ai-memory-store -p ai-memory-workstream"
[ ! -e "$OUT" ] || { echo "stage1a: $OUT exists" >&2; exit 1; }
mkdir -p "$OUT"
grep -q " $(dirname "$LAYERD") moobfs_layer " /proc/mounts || { echo "stage1a: no layer mounted above $LAYERD" >&2; exit 1; }
{ su "$USER_NAME" -c "git -C $AIM log -1 --format='ai-memory %h %ad %s' --date=short"; uname -r; } > "$OUT/env.txt"
# build the test binaries once, outside the measured runs
su "$USER_NAME" -c "cd $AIM && PATH=\$HOME/.cargo/bin:\$PATH cargo test --release --locked $CRATES --no-run" > "$OUT/build.log" 2>&1
for where in plain layer; do
    if [ $where = plain ]; then dir=$PLAIN; else dir=$LAYERD; fi
    rm -rf "$dir"; mkdir -p "$dir/tmp"; chown -R "$USER_NAME:$USER_NAME" "$dir"
    s=$(date +%s)
    rc=0
    su "$USER_NAME" -c "cd $AIM && PATH=\$HOME/.cargo/bin:\$PATH TMPDIR=$dir/tmp cargo test --release --locked $CRATES" > "$OUT/$where.log" 2>&1 || rc=$?
    echo "$where exit=$rc seconds=$(( $(date +%s) - s ))" | tee -a "$OUT/summary.txt"
    grep -E '^test result:' "$OUT/$where.log" | tee "$OUT/$where.results.txt" > /dev/null
    rm -rf "$dir"
done
echo "STAGE1A_DONE: $(cat "$OUT/summary.txt" | tr '\n' ' ')"

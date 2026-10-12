#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# stage2.sh: stage 2 of docs/client-test-plan.md on the benchmark host. Mounts the layer with
# domain_depth=2 as ai-memory's wiki directory (each workspace/project directory is a domain), runs
# a real `ai-memory serve` (zero-LLM) under umask 077 (private memory: directories 0700, files 0600,
# which satisfies the homogeneity rule), writes the W-AIM workload through its MCP tool, stops the
# server, mirrors the structured fields into user.moob.* and compares six query shapes between its
# SQLite and QUERY2: answers (aimem-compare.py) and, when the bench-aimem-latency binary is there,
# latency warm and cold (REPEAT and COLD samples per project and shape; MAX_PROJECTS limits the
# projects measured, 0 for all). Run as root.
#   stage2.sh OUT PROJECTS PAGES_PER_PROJECT
set -eu
OUT=$1; PROJECTS=$2; PAGES=$3
HERE=$(cd "$(dirname "$0")" && pwd)
AIM_BIN=${AIM_BIN:-/home/debian/clients/ai-memory/target/release/ai-memory}
QUERY=${QUERY:-/opt/moob-test/query}
LATENCY=${LATENCY:-/opt/moob-test/bench-aimem-latency}
REPEAT=${REPEAT:-200}; COLD=${COLD:-10}; MAX_PROJECTS=${MAX_PROJECTS:-0}
USER_NAME=${USER_NAME:-debian}
BASE=${BASE:-/srv/moob/ext4/aim2}
PORT=${PORT:-47321}
TOKEN=stage2-local-token
[ ! -e "$OUT" ] || { echo "stage2: $OUT exists" >&2; exit 1; }
grep -q '^moobfs_layer ' /proc/modules || { echo "stage2: moobfs_layer not loaded" >&2; exit 1; }
mkdir -p "$OUT"
cleanup() {
    [ -n "${SRV:-}" ] && kill "$SRV" 2>/dev/null || true
    mountpoint -q "$BASE/data/wiki" && umount "$BASE/data/wiki" || true
}
trap cleanup EXIT
rm -rf "$BASE"; mkdir -p "$BASE/lower" "$BASE/data/wiki"
chown "$USER_NAME:$USER_NAME" "$BASE/lower" "$BASE/data" "$BASE/data/wiki"; chmod 700 "$BASE/lower" "$BASE/data"
mount -t moobfs_layer -o "lower=$BASE/lower,domain_depth=2" none "$BASE/data/wiki"
chown "$USER_NAME:$USER_NAME" "$BASE/data/wiki"; chmod 700 "$BASE/data/wiki"
{ "$AIM_BIN" --version 2>/dev/null || true; uname -r; echo "projects=$PROJECTS pages=$PAGES"; } > "$OUT/env.txt"
su "$USER_NAME" -s /bin/sh -c "umask 077; AI_MEMORY_AUTH_TOKEN=$TOKEN AI_MEMORY_EMBEDDING_PROVIDER=none \
    exec $AIM_BIN serve --transport http --bind 127.0.0.1:$PORT --data-dir $BASE/data" > "$OUT/server.log" 2>&1 &
SRV=$!
i=0; until curl -s -o /dev/null "http://127.0.0.1:$PORT/" 2>/dev/null; do i=$((i+1)); [ $i -lt 60 ] || { echo "stage2: server did not start" >&2; exit 1; }; sleep 1; done
python3 -I "$HERE/aimem-workload.py" --url "http://127.0.0.1:$PORT" --token "$TOKEN" --projects "$PROJECTS" \
    --pages "$PAGES" --out "$OUT/workload.csv" | tee "$OUT/workload.txt"
sleep 2; kill "$SRV"; wait "$SRV" 2>/dev/null || true; SRV=
find "$BASE/data/wiki" -name '*.md' -path '*/*/*/*' | head -3 | xargs -r stat -c '%a %U %n' > "$OUT/modes.txt"
python3 -I "$HERE/aimem-mirror.py" --db "$BASE/data/db/memory.sqlite" --wiki "$BASE/data/wiki" --out "$OUT/mirror.csv" | tee "$OUT/mirror.txt"
"$QUERY" "$BASE/data/wiki" info > "$OUT/info.txt" 2>&1 || true
python3 -I "$HERE/aimem-compare.py" --db "$BASE/data/db/memory.sqlite" --wiki "$BASE/data/wiki" --query "$QUERY" | tee "$OUT/compare.txt"
[ ! -x "$LATENCY" ] || "$LATENCY" --db "$BASE/data/db/memory.sqlite" --wiki "$BASE/data/wiki" --out "$OUT/latency.csv" \
    --repeat "$REPEAT" --cold "$COLD" --max-projects "$MAX_PROJECTS" | tee "$OUT/latency.txt"

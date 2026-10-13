#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
docker build --platform linux/amd64 -t moobfs-m1-test -f vm/Dockerfile .
# MOOB_ONLY="btrfs powerloss" make vm-test runs only those suite steps (vm/suite.sh)
docker run --rm --platform linux/amd64 -e MOOB_ONLY="${MOOB_ONLY:-}" -v "$PWD:/work" moobfs-m1-test sh vm/in-container.sh

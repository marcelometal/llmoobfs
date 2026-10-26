#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
cd "$(dirname "$0")/.."
MOOB_DEBIAN_KERNEL=${MOOB_DEBIAN_KERNEL:-6.12.111}
docker build --platform linux/amd64 --build-arg "MOOB_DEBIAN_KERNEL=$MOOB_DEBIAN_KERNEL" -t moobfs-m1-test -f vm/Dockerfile .
# MOOB_ONLY="btrfs powerloss" make vm-test runs only those suite steps (vm/suite.sh)
docker run --rm --platform linux/amd64 -e MOOB_DEBIAN_KERNEL="$MOOB_DEBIAN_KERNEL" -e MOOB_ONLY="${MOOB_ONLY:-}" -v "$PWD:/work" moobfs-m1-test sh vm/in-container.sh

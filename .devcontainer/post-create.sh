#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Host-side tools for `make test`, `make lint`, `make fuzz`, `make bench-smoke` and
# `make checkpatch` (libclang-rt-18-dev carries libFuzzer and the sanitizer runtimes of
# Ubuntu 24.04's clang 18). Docker for `make vm-test` comes from the docker-in-docker feature.
set -eu
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
	build-essential clang libclang-rt-18-dev llvm python3 shellcheck libsqlite3-dev sqlite3 curl perl
echo 'Ready: make help lists the targets; make vm-test runs the VM suite (several GiB of disk).'

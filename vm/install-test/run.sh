#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Roadmap item C2: follow docs/operations.md in a disposable Debian 13 VM (the official
# generic cloud image, which boots the distribution's amd64 kernel). Needs only Docker.
# The tracked and new (not ignored) files of the working tree are copied into the guest;
# nothing on the host is installed or loaded. Log: build/install-test/log.txt; final marker MOOBFS_INSTALL_PASS.
set -eu
cd "$(dirname "$0")/../.."
docker build --platform linux/amd64 -t moobfs-install-test -f vm/install-test/Dockerfile vm/install-test
mkdir -p build/install-test
# --no-xattrs: macOS tar would otherwise store its provenance attribute in every entry
git ls-files -z --cached --others --exclude-standard |
	COPYFILE_DISABLE=1 tar --no-xattrs --null -T - -cf build/install-test/src.tar
docker run --rm --platform linux/amd64 -v "$PWD:/work" moobfs-install-test sh vm/install-test/in-container.sh

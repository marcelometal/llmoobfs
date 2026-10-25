#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Copy a MoobFS lower tree with rsync. Refuse a destination that has entries.

The mechanism is `rsync -aHAX --numeric-ids`, the command in
docs/distribution-strategy.md section 2.7. Numeric ids are preserved. This
script does not rewrite owners and does not invent an archive format.

A destination that already contains anything is left untouched: the process
exits before rsync runs. An absent destination is created empty and then
filled. An existing empty directory is filled.
"""
import os
import subprocess
import sys


def fail(message):
    print(f"G3_RESTORE_REFUSED {message}", file=sys.stderr)
    raise SystemExit(2)


def main():
    if len(sys.argv) != 3:
        fail("usage: g3_restore.py SRC DEST")
    src, dest = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    if not os.path.isdir(src):
        fail("source is not a directory")
    if os.path.ismount(src):
        fail("source is a mount; copy the lower directory")
    if os.path.exists(dest):
        if not os.path.isdir(dest):
            fail("destination exists and is not a directory")
        if any(os.scandir(dest)):
            fail("destination is not empty")
    else:
        os.makedirs(dest)
    subprocess.check_call([
        "rsync", "-aHAX", "--numeric-ids",
        src.rstrip("/") + "/", dest.rstrip("/") + "/",
    ])
    print("G3_RESTORE_OK")


if __name__ == "__main__":
    main()

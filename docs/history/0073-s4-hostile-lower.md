# 0073 — S4: the lower tree as hostile input

Date: 2026-10-13. Status: implemented and tested; `make vm-test` and the local KASAN job pass.
Local series (after pull request 57).

## Goal

Security roadmap S4 and promise 5 of ADR-0011: no crash, hang or memory corruption whatever the
lower contains or however it changes. The review of journal 0066 read the mount scan and found it
compliant; nothing tested it with input the layer did not write.

## What was built

- `tests/hostile.c`, deterministic per seed. `plant` writes a tree directly on the lower: names
  the layer must skip (upper case, other punctuation, leading, trailing and double dots, 65 bytes
  or more), every valid type, and for each rule of `moob_attr_validate` a value that breaks it (no
  bytes, unknown type byte, short and long numbers, a bool of 2, a NaN, five kinds of bad UTF-8,
  strings and raw values beyond their limits, values up to the 64 KiB an extended attribute can
  hold), files with 60 to 90 attributes, hard links, and attributes on directories. `mutate`
  changes, adds and removes attributes and unlinks files, to run behind a mounted layer. `probe`
  reads everything back through a mount. `marker` sets the format marker to any bytes.
- `vm/suite.sh`, once per lower filesystem (`HOSTILE_LOWER_PASS`): for each tree, mount; the
  inodes returned by eleven queries must equal the user-space oracle's; `moobfs-check` must report
  problems; mutate behind the mount, read everything and query again (stale by contract, must
  return); remount and compare with the oracle again; mount with domains on and compare `QUERY2`
  per domain with the oracle of that domain. Then the format marker: ten invalid markers must
  refuse the mount and `1` must mount. Three trees by default, `HOSTILE_SEEDS` for more.
- `tests/hostile-user.sh` and `make hostile-test`: the same trees through `moobfs-check` and
  `moobfs-index`, meant to be built under sanitizers; a step of the "Tools and sanitizers" CI job
  runs 50 trees. This is the user-space first line the roadmap asks for.

## Found

- **An empty or over-long format marker was accepted.** `moobl_fill_super` treated `-ERANGE` (a
  marker longer than the 16-byte buffer) and a zero-length marker as "no marker" and mounted,
  while a marker of the right size with invalid content refused. Now both refuse. Without the
  change the suite fails at `hostile-marker-size-accepted-0-ext4`.
- **`moobfs-check` passed a null pointer to `qsort`** when no file carried an identity
  (`tools/moobfs-check.c`), undefined behaviour reported by the sanitizer on the first tree.
  Harmless in practice; fixed with an early return.
- Nothing else: on every tree the kernel index equalled the oracle, before and after the changes
  behind the mount, with domains off and on.

## Behaviour, now stated in ADR-0011

An invalid attribute on the lower is skipped silently (not indexed, not logged, the mount does not
fail) and reported by `moobfs-check`; `getxattr` through the layer returns it unchanged. The
64-attribute limit applies to writes through the layer: a lower file with more is indexed whole
and gets `ENOSPC` for a new attribute. Only a decimal format marker this module knows mounts.

Alternative for invalid attributes: a rate-limited line in `dmesg`, as the roadmap suggested.
Rejected: a process that can write to the lower could keep the log busy, and `moobfs-check` already
names the file, the attribute and the reason.

## Evidence

- `make checkpatch`: clean.
- `make vm-test`: `HOSTILE_LOWER_PASS` on ext4, btrfs and XFS with 3 trees each, and every earlier
  marker through `MOOBFS_LAYER_PASS`.
- The same tree without the marker fix: `MOOBFS_VM_FAIL: hostile-marker-size-accepted-0-ext4`.
- `make hostile-test` with `-fsanitize=address,undefined` (gcc, Debian 13), 50 trees:
  `HOSTILE_USER_PASS` after the `qsort` fix; the first run stopped on that report.
- Local KASAN (`HOSTILE_SEEDS=40 vm/kasan-local.sh` on the benchmark host, upstream 6.12.112 with
  KASAN, UBSAN and lockdep): `MOOBFS_KASAN_PASS` after 39 minutes of guest time, no kernel
  diagnostic. The suite stops at the first failure, so all three filesystems ran their 40 trees;
  the serial console kept only the XFS line (`HOSTILE_LOWER_PASS: xfs, 40 trees`), the other two
  were lost to interleaved output, as some earlier markers of that console are.

## Limitations

- The trees are generated from fixed rules, not guided by coverage: 120 trees under KASAN is a
  soak, not a fuzzing campaign (S3 and S7).
- The lower filesystem's own code is trusted: a crafted disk image of ext4, btrfs or XFS is out
  of scope, as the kernel maintainers put it for mounting untrusted images.
- Not covered: exhausting the index budget from the lower (S9), a lower that changes while the
  mount scan is running, special files, and the timing of the scan on very large hostile trees.
- The answer comparison uses inode sets, not paths, because a hard-linked file has more than one.

## Next step

S2, fstests over the layer: the port and a first run of the `quick` group on ext4 are under way.

# 0013 — Phase 2c: typed attributes validated in the kernel, tools and fuzzing

Date: 2026-10-10, America/Sao_Paulo. Status: implemented and validated in the test VM.

## Request

Continue to phase 2c of the M2 specification: typed encoding of `user.moob.*` with
validation in `setxattr`, the `moobfs-check` and `moobfs-attr` tools, fuzzing of the encoding.

## Implementation

- `include/moobfs_attr.h` and `common/attr.c`: shared encoding (one type byte plus
  payload; types `string` UTF-8 ≤ 1 KiB, `int64`, `double` without NaN, `bool`, `time`, `raw`
  ≤ 4 KiB), strict UTF-8 validator (rejects NUL, overlongs, surrogates, > U+10FFFF),
  name rules (`[a-z0-9_.]`, 1–64 bytes, no dots at the edges or doubled), reserved
  name `format`, and the version marker parser. Pure functions, without allocation; the module
  includes them through `layer/attr.c` as M1 does with `common/format.c`.
- `layer/main.c`: `setxattr` on `user.moob.*` goes through `moobl_typed_check` before touching the
  lower: `EPERM` for the reserved name, `EINVAL` for an invalid name or encoding,
  `ENOSPC` when creating the 65th typed attribute (count by `vfs_listxattr` only when the attribute is
  new). Opaque namespaces keep passing without validation. At mount, the layer reads
  `user.moob.format` at the lower root and refuses unknown versions or versions greater than 1; absence
  is accepted; a lower without xattr generates a warning.
- `tools/moobfs-attr.c`: typed `get`, `set`, `rm`, `list` over the xattr API, plus `rawset`
  for tests to plant invalid bytes. `tools/moobfs-check.c`: walks a tree
  (`nftw`, without following symlinks, without crossing mounts), validates each typed attribute and the format
  marker, emits JSON and returns 2 when there are problems; it does not repair. Both Linux-only, target
  `make attr-tools`.
- `tests/attr_test.c`: 60 checks of the encoding, run in `make test` on the host.
  `tests/fuzz_attr.c` + `tests/fuzz-corpus/attr/`: libFuzzer target for the validator, names and
  marker; `make fuzz` (LLVM clang with ASan/UBSan); a CI step with a 30 s budget.
- `tests/layer.c`: typed attributes section (valid and invalid encodings, reserved
  name, limit of 64 with a check that rejected writes do not touch the lower,
  opaque namespace untouched). `vm/init`: exercises `moobfs-attr` and `moobfs-check` on each
  lower, including planting an invalid `bool` directly in the lower and requiring the
  checker to report it with the reason.

## Evidence executed

- Host (macOS arm64): `make test` → `attr encoding tests: 60 checks passed` and the 9 M1 tests.
- VM (`make vm-test`, Debian kernel 6.12.111): `MOOBFS_VM_PASS`, `LAYER_LOWER_PASS` for
  ext4, btrfs and XFS, `MOOBFS_LAYER_PASS: … typed attributes and tools …`; no `BUG`, `Oops`
  or `WARNING`. SHA-256 of `moobfs_layer.ko`: see `build/vm/environment.txt` of the run.
- Fuzzing: not executed on the host (Apple clang does not ship libFuzzer). In CI (run 38066427480,
  commit `8ab6ecd`, ubuntu-24.04 with clang): 21 747 211 executions in 31 s under ASan and UBSan,
  coverage 113 edges, final corpus of 138 entries, no failure.

## Attempts and fixes

1. The phase 2b pass-through test used `user.moob.kind` with untyped bytes; it now uses
   `user.other.kind`. The refusal by the module was the correct behavior.
2. The 64 MiB ext4 image received 1 KiB blocks from `mke2fs`, whose xattr block holds
   about 36 entries; the 37th attribute failed with `ENOSPC` coming from ext4, not from the layer.
   The image now uses `-b 4096`, and the specification records that the effective limit is the smaller
   of the layer's 64 and the lower's capacity.
3. A stray brace in the test made the build fail and the VM log be that of the previous round; the
   error was detected by the old marker in the console. The initramfs had no `/tmp`.

## Limitations

Same as phase 2b (no KASAN/lockdep, no full xfstests, no dm-log-writes, no real
client in the VM). `moobfs-check` reads values of up to 4 KiB + 1; larger attributes are reported
as a problem without being read. The per-file limit is checked only at creation, at the cost of one
xattr listing per creation.

## Next step

Phase 2d: in-RAM index built at mount, updated on `setxattr`/`removexattr`/`unlink`,
`QUERY` and `GET_INFO` through ioctl, `moobfs-index` in user space as the oracle.

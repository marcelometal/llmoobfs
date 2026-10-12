# 0008 — Reusable kernel infrastructure and M2 specification with POSIX writes

Date: 2026-10-10, America/Sao_Paulo.

## Request

Research mature kernel modules and infrastructure that avoid reinventing the wheel and, with that,
specify M2 with POSIX writes (a consequence of the ai-memory reading, journal 0007).

## Method and evidence

Direct verification in the Linux v6.12 source code (exported symbols in `fs/jbd2`,
`fs/iomap/buffered-io.c`, `fs/remap_range.c`, `fs/xattr.c`, `fs/mbcache.c`, `fs/verity`) and in
the `build/vm/kernel.config` of the VM's Debian kernel (`JBD2=m`, `FS_IOMAP=y`, `FS_VERITY=y`,
`DM_LOG_WRITES=m`, `DM_FLAKEY=m`, `DM_THIN_PROVISIONING=m`, `DM_SNAPSHOT=m`). No code
was compiled or executed in this session.

## Decisions (proposals, ADR-0006)

Full POSIX writes (without hard links and symlinks); journaling through jbd2; data through iomap with
an initial prototype to validate the combination; sharing through `remap_file_range`
(`FICLONE`/`FIDEDUPERANGE`) with copy-on-write; typed attributes as `user.moob.*` xattr with
`mbcache`; in-RAM index and `QUERY` through ioctl; new format `MOOBFSW2` with M1 import;
acceptance with git + SQLite + ai-memory. The ioctl write interface of the original proposal
is superseded; the document is preserved.

## Changes

- `docs/kernel-infrastructure.md`, `docs/decisions/0006-m2-posix-writes.md`, `docs/spec-m2.md`
  (new); `ROADMAP.md`, `README.md`, `docs/ecosystem.md`, history index, `CHANGELOG.md`.

## Limitations

The iomap + jbd2 combination has no in-tree precedent; phase 2a exists to decide this before
the final format. Numeric limits (16 GiB, 65 536 inodes, 4 KiB of xattr) are experiment
ceilings, not measured. The in-RAM index is not persistent by decision.

## Next step

Phase 2a: minimal iomap + jbd2 prototype in a VM, with `fsx` and `dm-log-writes`, before any
v2 format code. In parallel, fuzzing of `common/`.

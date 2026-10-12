# 0010 — ADR-0007: stacked layer over a mature filesystem; M2 rewritten

Date: 2026-10-10, America/Sao_Paulo.

## Request

After the principle recorded in 0009, it was evaluated whether stacking over a mature FS loses
novelty and whether it would be worth keeping two paths (layer and own FS). Recorded evaluation: the
novelty (typed attributes, index, `QUERY`, provenance) lies entirely in the layer; control
of the on-disk layout is lost and there is a performance ceiling from the double layer; two paths in
parallel split the effort and delay measurement. User decision: a single path, with our own
FS conditional on M4 evidence; ADR-0007 and a rewrite of the specification.

## Changes

- `docs/decisions/0007-stacked-memory-layer.md` (new; replaces ADR-0006, which receives a dated
  note and is preserved).
- `docs/spec-m2.md` rewritten as a stacked layer; the previous version preserved in
  `docs/spec-m2-full-volume-superseded.md`.
- `docs/kernel-infrastructure.md`: addendum with `fs/backing-file.c`, overlayfs, ecryptfs and
  delegated reflink, verified on v6.12 and in the VM configuration (`OVERLAY_FS=m`,
  `ECRYPT_FS=m`).
- `ROADMAP.md`, `README.md`, history index, `CHANGELOG.md`.

## Evidence

Exported symbols of `fs/backing-file.c` and their use by overlayfs read from the v6.12 code; line
count of overlayfs and ecryptfs by `wc -l` on the main files; xfstests `README.overlay`
confirms support for stacked FSs. No code compiled or executed.

## Limitations

The specification is a proposal. M1 is frozen as a concluded experiment and regression; it is no
longer the product's base. Pitfalls specific to stacked FSs (inode aliasing, mmap, modification of the
lower from outside) are listed, not resolved.

## Next step

Phase 2a: minimal wrapfs-style pass-through with `backing_file_*` over ext4, `fsx` and KASAN, in a VM.

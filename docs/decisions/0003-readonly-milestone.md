# ADR-0003 — Experimental read format for M1

Date: 2026-10-10. Status: adopted for the first milestone.

## Context

The full proposal includes a transactional log, checkpoints, snapshots and a revision namespace. The first authorized milestone is smaller: create an image, mount it and read two independent files that share content.

## Decision

Create an experimental read-only image format, with a superblock, an object table, a file table and aligned payloads. The magic identifies this M1 format separately. It will not be presented as an implementation of the full transactional format.

Each file has its own inode; distinct entries may point to the same physical object. The offline tool groups only identical bytes. The module verifies structure, checksums and content during mount, and serves reads through the page cache/iomap.

Start with a flat namespace, up to 128 files/objects, names in printable ASCII of up to 63 bytes and objects of up to 64 KiB. The content bytes have no encoding restriction. A published image is immutable and can only be mounted read-only. Do not implement writes, publication ioctls, checkpoints or snapshots in this milestone. FIEMAP serves to verify the shared extents.

## Consequences

The milestone makes it possible to validate the real VFS → module → blocks path and the sharing, with reproducible tests. The future format will require offline conversion or a new image, without promising binary compatibility. The original proposal remains in `docs/native-mvp.md`.

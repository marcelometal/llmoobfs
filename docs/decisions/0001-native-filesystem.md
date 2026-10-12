# ADR-0001 — Native mountable Linux filesystem

Date: 2026-10-10. Status: accepted by the user.

## Context

The goal is to experiment with shared persistent storage for LLM memory with access through a mounted filesystem.

## Decision

Implement `moobfs.ko` as a Linux module, registered in the VFS, with its own format on a block device. Search and semantic processing may exist in user space, without taking part in normal reads.

## Alternatives considered

- FUSE: reduces the initial development cost, but was explicitly discarded by the user.
- Library/service over an existing filesystem: useful as a memory engine, but does not meet the requirement of a native mountable filesystem.

## Consequences

It is necessary to compile against a defined kernel and test mount, I/O and invalid inputs in a VM. Removing FUSE does not, by itself, demonstrate a performance gain. The first milestone is read-only; the full MVP includes publication and durability.

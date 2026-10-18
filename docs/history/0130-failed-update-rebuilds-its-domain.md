# 0130 — A failed index update rebuilds its domain (finding F2, ADR-0016)

Date: 2026-10-18. Status: change and negative round run under KASAN on GitHub's KVM runners;
pull request opened. Estimated effort: S.

## Why

While choosing what is left before the technical-preview stage, the code behind finding F2 was
read again. When the index budget is full, an attribute write into a domain already built makes
the update fail after the lower write has succeeded. The module then set one mount-wide
`incomplete` flag, and only `GET_INFO` reported it: `QUERY` and `QUERY2` kept answering from the
domain and silently missed the file, or kept a replaced value, until remount. ADR-0011 described
F2 as a visible degradation; it was also a silent wrong answer.

## Prior art

The index has been a cache of per-domain builds since ADR-0012. The usual answer when a cache
cannot be updated is to invalidate the entry instead of serving it stale; the dentry cache does
this with `d_invalidate()`, and the layer already did it for a directory rename (`paths_stale`).

## Change

- `layer/index.c`, `layer/moobl.h`: `domain_lost()` replaces `index_mark_incomplete()`.
  - On a failed update, a built domain is marked `lost`, so `domain_fresh()` is false and the next
    query takes the domain down and builds it again, releasing the least recently queried domains
    if needed.
  - A domain being built restarts its build.
  - The three failure sites call it. The tracepoint `moobfs_index_incomplete` is unchanged; the
    warning is rate-limited.
- UAPI: no layout change. `MOOB_FLAG_INDEX_INCOMPLETE` is reserved and no longer set (a comment
  in `include/moobfs_ioctl.h`). The client contract keeps the check for older modules.
- Test, suite step `release`, in each lower filesystem's run:
  - With d0 and d1 built, `0.9 × one domain` bytes of string attributes are written into new
    files of d0.
  - The tracepoint must show a failed update (otherwise the test proved nothing), and the next
    query of d0 must equal the oracle with every file written.
  - Expected on `main` before the change: the answer misses files.
- Documents: ADR-0016 (accepted by the maintainer on 2026-10-18); notes in ADR-0011 (F2). Updated: architecture overview, client contract,
  distribution strategy, spec-query-v2 §9. The old warning text goes to
  `docs/retired-claims.txt`.

## Evidence

- The module builds with `W=1` against the Debian 6.12 headers of the `make vm-test` image, with
  no warning.
- `scripts/checkpatch.sh`: no findings.
- `scripts/check-retired-claims.sh`: OK.

- KASAN workflow on the branch (run 39165103937): `MOOBFS_KASAN_PASS`, with
  `RELEASE_FILL: ext4, 491 writes overflowed the budget; d0 built again with all of them`, and
  the same on btrfs and XFS.
- Negative round, the test alone on `main` (branch `neg/f2-test-only`, run 39165106169): failed
  with `fill: kernel 244 oracle 491 written 491`, `MOOBFS_VM_FAIL: rl-fill-answer-differs-ext4`.
  Before the change, half of the files written after the budget filled were missing from
  the answers.

## Limitations

- F2's fairness part is not addressed: one user can still make other users' domains be released
  and rebuilt (latency). Per-user shares remain under security roadmap S9.

## Next step

Merge after the maintainer's review. SECURITY, the FAQ (and its site page) and the security
roadmap already say, in this change, that only F2's fairness part remains open.

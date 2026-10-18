# 0116 — The homogeneity rule judged every class; per-result checks no longer hold query_sem

Date: 2026-10-15 to 2026-10-17. Started 2026-10-15 06:20, ended 2026-10-17 17:20 (calendar time,
local; the work stopped for about two days on a usage limit). Status: fixed and tested in the VM;
`scripts/checkpatch.sh` and the full suite still to run (see Limitations).
Estimated effort: M.

Source: an independent source review of 5582e06 (journal 0114, published after these fixes)
and its adversarial verification against Linux 6.12.112. Two of its findings were rated high
and confirmed from the source; they are findings F9 and F10 of ADR-0011. Both were reported as
private advisories in the repository before any fix was published.

## F9: a file only "others" could read was answered to its group

Condition 1 of the homogeneity rule (`docs/spec-query-v2.md` §3, `reach_ok()` in
`layer/index.c`) admitted a file into its domain's partition when its bit for others was set and
it had no extended ACL. The kernel does not fall back to the bits of others for the owner or for
a member of the group: `acl_permission_check()` (fs/namei.c) uses the owner bits for the owner
and the group bits for a member of the group whenever they differ from those of others. A file
`0604` of group `staff`, in a domain the members of `staff` can traverse, was therefore answered
by `QUERY2` without a per-result check to members of `staff` who cannot read it: its name, its
inode number and, through equality or range probes, its values. The same held for an
intermediate directory `0701` and the search bit. It broke promise 3 of ADR-0011.

The user-space oracle (`tools/moobfs-index.c`) and `moobfs-check` carried the same rule, so the
suite, which compares the kernel with the oracle, agreed with the kernel; the rule itself was
never compared with the kernel's own decision.

- **Fix:** condition 1 asks the bit of all three classes (`a+r` for files, `a+x` for directories);
  otherwise condition 2 decides, as before. Same change in the kernel and both tools. Spec §3
  keeps the old text in a dated note, with the compatibility decision: no format change; a file
  the old condition admitted and the new one does not becomes heterogeneous unless condition 2
  admits it (`0644` files and `0640` files in a domain of their group stay in the partition).
- **Test:** `tests/permmatrix.c` and the suite step `permmatrix`. A domain is planted directly
  on the lower (random modes from 12 file modes and 9 directory modes, three owners, two groups,
  files directly in the domain or under one of four intermediate directories), the layer is
  mounted afterwards so the partition answers (the step requires `fallback_domains=0`), and
  nine identities (owners, group members with primary or supplementary group, a stranger) run
  `QUERY2`. The oracle is the lower kernel: a file may be returned only if the identity can
  `getxattr` it on the lower and open the domain there; a readable file the rule calls
  homogeneous must be returned. The program also counts the answers the rule before this
  journal gave wrongly, and the step fails if there were none, so a pass proves the matrix
  reached the defect.

## F10: a query waiting on a FUSE mount held up every query of the mount

In a domain answered with per-result checks (after a permission change inside it; every domain
under an LSM that labels the layer's inodes, journal 0104), `caller_may_read()` resolved each
candidate's path with `vfs_path_lookup()` while `query_sem` was held for reading
(`query2_one()`), in the caller's mount namespace and crossing mounts. A FUSE mount over a
directory of the domain whose daemon never answers makes the walk wait: once the daemon has read
the request, `request_wait_answer()` (fs/fuse/dev.c) waits without interruption. A writer of
`query_sem` (the rebuild of a domain after a directory rename, a release over the budget, both
reachable by any user) then queues, and every new reader queues behind it: with the semaphore
owned by readers, `rwsem_down_read_slowpath()` (kernel/locking/rwsem.c) does not let a reader
steal it past a waiting writer. Every query of every user of the mount stopped until the daemon
went away. It broke promise 1 of ADR-0011; F7 (journal 0105) was the same class for the copy of
results.

- **Fix:**
  - `query_run()` releases `query_sem` while the candidates of a chunk are checked (they are
    copies: path, value, key) and takes it again before the next chunk with `query_repin()`.
    `QUERY2`: if its domain was taken down meanwhile it is built and pinned again
    (`query2_pin()`) and the scan resumes from its key, as after any concurrent change; v1, which
    reads every partition, fails with `EAGAIN` if any was taken down, as when its first pin did
    not hold. The walk can also wait without leaving the mount, on the lock of one of the
    layer's own directories (suspected path: a reader of that directory stalled on a fault in
    its buffer, a writer queued behind it); so the release is the fix and the next item a
    tightening.
  - The walk passes `LOOKUP_NO_XDEV | LOOKUP_NO_SYMLINKS`: a result lives on this mount under
    its recorded name, and a walk that left the mount was refused afterwards anyway. A recorded
    name reached through a symbolic link planted since is now omitted instead of followed.
  - `caller_may_read()` asserts with lockdep that `query_sem` is not held.
- **Test:** `tests/fusestall.c`, a FUSE daemon without libfuse that answers `FUSE_INIT` and
  nothing else, and the suite step `stall`: with the daemon mounted over `D/a`, (1) a query of
  `D` by its owner, (2) after a directory rename in `D`, a query that rebuilds `D` and takes
  `query_sem` for writing, (3) a query of `E` by another user. (3) must answer within seconds
  (it waited behind (2), which waited behind (1)); (1) and (2) must answer `top` and omit `a/f`.

## Results

On the benchmark host, upstream 6.12.112 with KASAN, UBSAN and lockdep (`vm/kasan-local.sh`,
`KCACHE=~/.cache/moobfs-kasan`), on 2026-10-15:

- `MOOB_ONLY="permmatrix stall" sh vm/kasan-local.sh`: `PERMMATRIX_STEP_PASS: 12 seeds, no answer
  the lower would refuse; 307 answers the rule before journal 0116 gave wrongly` (per seed: 9
  identities, 48 files, 6 to 90 results, `mismatches=0`). The `stall` step first failed at
  `st-no-fallback`: the test expected exactly one domain in fallback, but its own `chown -R`
  through the layer had put both in fallback; the check now asks for at least one.
- `MOOB_ONLY=stall`: `STALL_PASS` and `MOOBFS_KASAN_ONLY_PASS`, no kernel diagnostic, no lockdep
  report (the new assertion in `caller_may_read()` included).
- Negative rounds, one per protection, each on a copy of the tree with only that protection
  undone:
  - A, `index.c` of `origin/main` (no release, no lookup flags): fails at
    `st-other-user-held-up-by-a-query-waiting-on-fuse`. The report of finding F10, reproduced.
  - B, the release kept and the lookup flags removed: fails at
    `st-domain-query-1-waits-on-the-fuse-mount`, after the query of the other user answered.
    Each protection is seen by its own check.
  - C, the kernel's condition 1 back to "others" (tools fixed): fails at `pm-mismatch-1`. The
    leak of finding F9, reproduced against the lower's own decision.
- A first run with `MOOB_ONLY="permmatrix stall ext4"` failed earlier, at `remount-ro-ext4`:
  `mount -o remount,ro` of util-linux 2.41 on the host is refused by the layer. The step and the
  remount code are unchanged here; the GitHub runner (util-linux 2.39) and the busybox harness
  pass it. Recorded as a separate investigation (journal 0118).

## Limitations

- Not yet run: `scripts/checkpatch.sh` (the benchmark host stopped answering before it ran) and
  the full suite; both belong to the batch's KASAN run.

- The matrix plants no ACLs: files with extended ACLs stay out of the partition by the rule, and
  the matrix does not exercise condition 2's ACL clause.
- The stall test uses a mount made by root in the initial namespace; an unprivileged user does
  the same in a user namespace or with `fusermount3`, not exercised here.
- Lookups of the layer's own directories still run under the caller's credentials and can wait
  on the directory's lock; with `query_sem` released that wait holds only the caller.

## Next step

The batch of journals 0115 to 0118 under the KASAN workflow, then publication of the fixes
before the review journal 0114 and the advisories.

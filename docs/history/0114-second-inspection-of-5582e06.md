# 0114 — Second independent inspection of commit 5582e06 (source reading, nothing executed)

Date: 2026-10-15. Started 09:30, ended 11:40 (calendar time, local). Status: findings recorded,
none reproduced. Estimated effort to reproduce and fix the two high findings: M.

This journal records a review of `5582e063f469980c65353a0c7a9074a668ed7955` made by an assistant
(Claude) from the same brief as the outside review of journal 0111, independently of it. The
sources were read from `git archive 5582e06`; kernel rules were checked against the Linux
6.12.112 tree under `build/review-kernel-6.12.112/` (fs/namei.c, fs/dcache.c, fs/open.c,
fs/super.c, fs/xattr.c, fs/attr.c, fs/backing-file.c). No module was built or loaded; every
finding below is a claim from source tracing until a VM run confirms or discards it.

## Scope

ADR-0011 as the contract, excluding the known items listed in the brief (F2, F4, hard links
not re-partitioned, the non-promises, the items of journal 0105 left for later). Read in
full: ADR-0011, `layer/main.c`, `layer/index.c`, `layer/moobl.h`, `include/moobfs_ioctl.h`,
`include/moobfs_attr.h`, `common/attr.c`, `sqlite/moobfs_vtab.c`, `docs/spec-query-v2.md`
section 3.

## Findings (line numbers at 5582e06)

### 1. The homogeneity shortcut admits files that owner-class and group-class traversers cannot read

- **Location:** `layer/index.c:522-542` (`reach_ok`, condition at 529-530); spec text
  `docs/spec-query-v2.md:36` (condition 1).
- **Severity:** high (information leak through the query interface).
- **Confidence:** confirmed (source).
- **Trigger:** Domain directory `D` (mode 0755, owner bob). Alice creates `D/f` with mode
  0604, group `staff`, and sets `user.moob.secret = "x"`. Carol is a member of `staff` and
  can traverse `D`. Carol runs `QUERY2` on `D` with the term `secret = "x"` (or
  `secret EXISTS`, or any prefix/range probe). `f` is returned with its name and inode
  number, and each probe tells Carol whether the value matches. On the lower, Carol cannot
  `getxattr` `f` (needs `MAY_READ`): `acl_permission_check()` (fs/namei.c:350-387) judges a
  group member by the group bits only, never by the "other" bits. Same for a file 0004
  owned by Carol herself, and for an intermediate directory 0701 with `want_read=false`.
- **Why it is wrong:** `reach_ok` returns true as soon as `in->i_mode & o` is set and there
  is no extended ACL, regardless of the caller's class. That is not a sufficient condition
  for "every traverser can read it": POSIX DAC does not fall through to the other bits for
  the owner or for group members. The partition therefore holds files some traversers
  cannot read, which breaks promise 3 of ADR-0011 and the rule of spec §3. The spec itself
  states the wrong condition, so this is a design defect, not an implementation slip.
- **Suggested fix:** condition 1 must also require the owner bit and the group bit (`in`
  readable by all three classes: `(mode & 0444) == 0444`), or fall back to condition 2.
  Add a test with a group-owned 0604 file and a group member as caller.

### 2. Per-result checks run with `query_sem` held and walk mounts the caller controls

- **Location:** `layer/index.c:2274-2289` (`caller_may_read`), `2748-2750` (called per
  chunk inside `query_run`), `3160-3164` (`query2_one` holds `query_sem` from
  `query2_pin` until after `query2_exec`); `fs/namei.c:2814` (`vfs_path_lookup`),
  `fs/namei.c:1010-1018` (`LOOKUP_NO_XDEV` handling in mount traversal).
- **Severity:** high (hang of the query service for every user of the mount).
- **Confidence:** confirmed for the lock scope; the blocking lookup needs a user-controlled
  mount on the layer, which I did not run.
- **Trigger:** Mallory owns domain `D` with an indexed file `D/a/f`. (1) `chmod` on `D/a`
  puts `D` into fallback (`moobl_index_perm_changed`, 2032-2036), so every result of `D`
  is checked (`sc.check`, 3097). With SELinux or Smack active, `lsm_labels()` makes every
  domain checked, no chmod needed. (2) In a private mount namespace Mallory mounts a FUSE
  filesystem on `D/a` (FUSE has `FS_USERNS_MOUNT`; or `fusermount3`) whose daemon never
  answers. (3) Mallory issues `QUERY2` on `D`: `query2_pin` returns with `query_sem` read
  held, `query_run` gathers `f`, releases `ix->lock`, and `caller_may_read` resolves
  `a/f` from the domain dentry in Mallory's namespace; the walk crosses into the FUSE mount
  and blocks in the FUSE lookup for as long as the daemon likes (once the request is
  handed to the daemon the wait is not killable; `request_wait_answer()`, fs/fuse/dev.c,
  not in the local tree). (4) Mallory, or anyone, renames a directory inside a built domain
  and queries it: `domain_ensure` takes `down_write(&ix->query_sem)` (1477) and waits
  behind the reader. From then on every `QUERY2` of every user blocks in
  `down_read(&ix->query_sem)` (3024), since a waiting writer blocks new readers
  (`rwsem_down_read_slowpath`, kernel/locking/rwsem.c, cited from memory). Recovery needs
  root to kill the daemon.
  Even without a mount, a fallback domain with N candidates performs N VFS path walks with
  disk I/O under `query_sem`; the user chooses N.
- **Why it is wrong:** this is the class finding F7 closed for the result copy: a wait of
  unbounded, user-controlled length while holding a semaphore shared by the whole mount.
  Breaks promise 1 (denial of service for other users).
- **Suggested fix:** release `query_sem` before the per-result checks (the checks are
  against the VFS, not the index; the resume key already tolerates a rebuild), and pass
  `LOOKUP_NO_XDEV | LOOKUP_NO_SYMLINKS | LOOKUP_BENEATH` to `vfs_path_lookup`, since a result
  must be on this mount anyway (the `p.mnt == file->f_path.mnt` check at 2283 then becomes
  a lookup failure instead of a wait).

### 3. Ref-walk revalidation of a negative dentry reads a lower dentry that `moobl_instantiate` may free

- **Location:** `layer/main.c:269-287` (`moobl_d_revalidate`, non-RCU path, 276-278),
  `layer/main.c:413-422` (`moobl_instantiate` swaps `info->lower.dentry` and `dput`s the
  old one); `fs/namei.c:1703-1706` (`lookup_fast` ref-walk calls `d_revalidate` with no
  parent lock), `fs/dcache.c:374-389` (`dentry_free` defers to `call_rcu`).
- **Severity:** critical class (use after free in the dentry cache), but a narrow window.
- **Confidence:** suspected: the interleaving is concrete, the window is two loads and
  needs a full RCU grace period in between, so it requires `CONFIG_PREEMPT` (or
  `preempt=full`) and a preemption at exactly that point; practically rare, impossible to
  hit on a non-preemptible kernel.
- **Trigger:** T1 `stat("D/x")` where `x` is a negative upper dentry that still references
  the unhashed lower dentry of an unlinked file. In ref-walk, `lookup_fast` calls
  `moobl_d_revalidate` without any lock; it loads `info->lower.dentry` (276). T2
  `creat("D/x")` holds the parent lock, `vfs_create` on the lower makes a new lower dentry,
  `moobl_instantiate` publishes it (420) and `dput(stale)` (421) kills the old one, whose
  memory is freed after a grace period. T1, preempted between 276 and 277 for longer than
  the grace period, then reads `lower->d_flags` from freed memory; if the stale bit
  `DCACHE_OP_REVALIDATE` reads as set, it calls through `lower->d_op->d_revalidate`.
- **Why it is wrong:** the comment at 416-419 covers only RCU-walk readers. The ref-walk
  path holds neither `rcu_read_lock` nor a reference on the lower dentry while
  dereferencing it, and `d_fsdata` of a negative dentry is mutable.
- **Suggested fix:** in the non-RCU path, read the lower dentry under `rcu_read_lock()` and
  take a reference with `lockref_get_not_dead` before touching it (or skip the lower
  revalidate entirely for negative upper dentries: ext4, btrfs and XFS have no
  `d_revalidate`). While there: the ref-walk path never checks whether the lower became
  positive, so the "retries in ref-walk, which runs the full check below" promise of the
  comment at 240-241 is not kept for negative dentries (non-promise 2 territory, no crash).

### 4. Name operations ignore a read-only lower mount

- **Location:** `layer/main.c:439, 456, 479, 505, 584, 626, 651` (vfs_create, mkdir,
  unlink, rmdir, rename, link, symlink on the lower), `844, 854, 857` (setxattr,
  removexattr), `934` (notify_change), `704` (fileattr), `724-726` (ACLs). No
  `mnt_want_write(sbi->lower.mnt)` anywhere in the module.
- **Severity:** medium (promise 2: the lower, through that mount, would refuse).
- **Confidence:** confirmed (source).
- **Trigger:** `mount --bind -o ro /srv/data /srv/data-ro`; `mount -t moobfs_layer
  -o lower=/srv/data-ro none /mnt`. As any user with directory write permission:
  `touch /mnt/u/x`, `setfattr`, `chmod`, `truncate`, `rm` all succeed on the lower.
  Data writes do fail: `backing_file_open` goes through `do_dentry_open`, which calls
  `mnt_get_write_access(f->f_path.mnt)` on the lower path (fs/open.c:884), so the mount is
  half read-only.
- **Why it is wrong:** `vfs_create` and friends never check the vfsmount; the callers in
  fs/namei.c do (`mnt_want_write` at 3954, 4086, 4450). Overlayfs takes `mnt_want_write`
  on the upper mount for every change (`ovl_want_write`). ecryptfs does not, which is the
  precedent the layer follows, but ADR-0011 promises the lower's own answer.
- **Suggested fix:** `mnt_want_write(sbi->lower.mnt)` around each lower mutation (or refuse
  to mount on a read-only lower mount and say so).

### 5. A domain build buffers a whole directory listing in unaccounted kernel memory

- **Location:** `layer/index.c:1149-1166` (`scan_actor` appends every name to a list),
  `1339-1350` (the list is consumed only after `iterate_dir` returns).
- **Severity:** medium (resource exhaustion by an unprivileged user, outside the index
  budget).
- **Confidence:** suspected (not measured).
- **Trigger:** a user fills one directory of their domain with entries of 255-byte names
  up to their disk quota (ext4 spends about 264 bytes per entry), then issues `QUERY2`.
  `scan_actor` allocates `sizeof(struct scan_name) + len` per entry, `GFP_KERNEL` without
  `__GFP_ACCOUNT`, so the build holds roughly the directory's on-disk size in kernel memory
  at once, not charged to the budget nor to the caller's memory cgroup. A 4 GiB directory
  costs about 4 GiB of kernel memory per build, and the user can repeat the build at will
  (rename a subdirectory, query again).
- **Why it is wrong:** promise 1 includes denial of service; `index_budget` was meant to be
  the bound on what a user can make the kernel hold.
- **Suggested fix:** process names in bounded batches (stop the actor after N names,
  remember `ctx->pos`, resume), or at least account the list with `__GFP_ACCOUNT`.

### 6. `domain_drop` and `moobl_index_destroy` walk the whole index under the write lock without `cond_resched`

- **Location:** `layer/index.c:823-845`, `1706-1719`; callers `domain_takedown` (852),
  reachable from the shrinker (1660-1676) and from any user's rebuild (1479).
- **Severity:** medium (latency for every user; soft-lockup warnings possible, which
  promise 1 counts as a failure).
- **Confidence:** suspected (not measured).
- **Trigger:** `index_budget=4G`, a domain with tens of millions of entries, then a rename
  inside it and a `QUERY2`: `domain_takedown` frees every entry of the domain while holding
  `ix->lock` for writing and `query_sem` for writing, with `rb_first` per entry and no
  scheduling point. On a non-preemptible kernel a loop over 10^7 to 10^8 entries lasts
  seconds to tens of seconds; the watchdog fires at 20 s.
- **Why it is wrong:** long loops under a lock need `cond_resched()` or batching; here the
  lock also serializes every index update and every query of the mount.
- **Suggested fix:** free in batches, releasing the lock between batches (the domain is in
  state NONE from the first batch, so nothing reads it), or keep a per-domain list of
  records so the drop is linear in the domain, not in the whole name tree.

### 7. Low: a typed attribute cannot be set on a write-only file

- **Location:** `layer/main.c:815-824` (`moobl_typed_check` calls `vfs_getxattr` on the
  lower with the caller's credentials and returns its error).
- **Severity:** low (wrong refusal, no security effect).
- **Confidence:** confirmed (source).
- **Trigger:** `chmod 0200 f; setfattr -n user.moob.k -v $'\x01x' f` as the owner. The
  VFS authorizes the write, `vfs_getxattr` on the lower returns `-EACCES`
  (`xattr_permission` needs `MAY_READ` for `user.*`), and the set fails. Directly on the
  lower the same `setfattr` succeeds.
- **Suggested fix:** probe existence with `__vfs_getxattr` on the lower inode (no
  permission check; the VFS already authorized the write), or treat `-EACCES` like
  `-ENODATA`.

### 8. Low: directory reads run with the caller's credentials, not the mounter's as ADR-0011 says

- **Location:** `layer/main.c:1250-1260` (`moobl_iterate` calls `iterate_dir` with no
  `override_creds`; the real file was opened with the mounter's credentials at 1240).
- **Severity:** low (documentation of the credential model is wrong; the actual behaviour
  is stricter, not weaker).
- **Confidence:** confirmed (source).
- **Trigger:** under SELinux, a process allowed `read` on the layer's directory label but
  not on the lower's gets `EACCES` from `security_file_permission()` inside `iterate_dir`,
  while a plain `read(2)` of a file succeeds (delegated with the mounter's credentials).
  Same for `moobl_flush` (1211) and `moobl_llseek` (1113).
- **Suggested fix:** either `override_creds` in `moobl_iterate` (overlayfs's choice) or
  correct the ADR's "Data operations" bullet.

### 9. Low (user space): a pushed equality ignores the constraint's collation

- **Location:** `sqlite/moobfs_vtab.c:508-517` (`omit = 1` whenever the right-hand side
  encodes), `mv_best_index` never calls `sqlite3_vtab_collation()`.
- **Severity:** low (wrong answers in SQL, not the kernel).
- **Confidence:** suspected (not run).
- **Trigger:** `SELECT * FROM mem WHERE kind = 'Note' COLLATE NOCASE`. The kernel compares
  bytes, SQLite is told the constraint is handled and does not re-check, so rows with
  `kind = 'note'` are missing.
- **Suggested fix:** set `omit` only when `sqlite3_vtab_collation()` returns `BINARY`.

## Things checked and found sound

- NOFS scope around every `ix->lock` section and the `__GFP_FS` check in the shrinker:
  reclaim cannot re-enter the index lock from an allocation made under it, and
  `super_cache_scan` (fs/super.c:193) stops without `__GFP_FS`.
- Lock order `dom->build` → `query_sem` (write) → `ix->lock`, with queries taking only
  `query_sem` (read) and `ix->lock` (read) and never `build`; the over-budget release only
  tries another domain's `build`. No cycle found.
- No `dput`/`iput` under `ix->lock`: the dentry from `d_find_alias` and the inode from
  `ilookup5` are dropped after the unlock; `moved_dir` lists are freed after the unlock.
- `ix->lock` is taken inside parent `i_rwsem` (lookup, rename, link) and never the other
  way round.
- The online build: a write during a reading marks the loader dirty and the reading is
  repeated; a file is always absent or whole; renamed directories are rescanned from the
  side list; a file renamed during a build is adopted by the rename path.
- Per-file placement races: `setxattr`, `chmod`, `chown`, `set_acl`, `link` and `rename`
  of the same file are serialized by the upper inode's `i_rwsem` (`vfs_setxattr`
  fs/xattr.c:320, `vfs_link` fs/namei.c:4797, `vfs_rename` fs/namei.c:5051); directory
  permission changes put the domain into fallback. I could not construct the "placement
  reused after a concurrent permission change" case journal 0111 mentions.
- `caller_may_read` is rooted at the domain directory (`ND_ROOT_PRESET`, fs/namei.c:627,
  2475): symlinks cannot escape it, and the result must carry the indexed inode number.
- Ioctl input: every reserved field, pad byte and count is checked; term and cursor
  lengths are bounded (`len ≤ 1024`, matching the 1024-byte buffers); results and `info`
  are zeroed before copy; cursors are MACed with a per-mount key, so their fields cannot be
  forged. The one implicit tail-padding byte of `struct moob_query_term` is a known item.
- F5 remains fixed: `file_remove_privs` runs with the caller's credentials before every
  delegated write, clone and copy; `S_NOSEC` is cleared by `notify_change` (fs/attr.c:412)
  and by `security.*` sets (fs/xattr.c:232), so the `SB_NOSEC` shortcut cannot keep a
  set-user-ID bit alive; mmap writes clear it in the lower's own `page_mkwrite` with the
  faulting task's credentials.
- `backing_file_open` takes `const struct file *user_file` in 6.12.112
  (fs/backing-file.c:33), as called.
- Devices are refused by `SB_I_NODEV`; `SB_I_NOEXEC` refuses exec and `PROT_EXEC` mappings.
- `common/attr.c`: bounds of the UTF-8 validator and of `moob_attr_validate` are correct;
  `name[i - 1]` is guarded by the leading-dot check.

## Files read in full

`docs/decisions/0011-threat-and-credential-model.md`, `layer/main.c`, `layer/index.c`,
`layer/moobl.h`, `include/moobfs_ioctl.h`, `include/moobfs_attr.h`, `common/attr.c`,
`sqlite/moobfs_vtab.c`, `docs/spec-query-v2.md` §3 only.

Skimmed or not read: `layer/moobl_trace.h`, `layer/attr.c` (3 lines), the rest of
`docs/spec-query-v2.md`, tests, tools, `vm/`.

## Areas not assessed

- Behaviour under a real LSM policy beyond what `lsm_labels()` implies.
- The rwsem writer-preference and FUSE wait semantics were cited from memory
  (kernel/locking/rwsem.c, fs/fuse/dev.c are not in the local kernel subset).
- Timing channels of QUERY2 (fallback vs. homogeneous answers differ in cost).
- The `tests/` suite and whether any existing test already covers finding 1.
- Anything on btrfs subvolume boundaries beyond `moobl_other_device`.

## Three places to stress first

1. **Finding 2 with syzkaller or a hand-written stressor:** one thread in a fallback
   domain querying through a FUSE mount that never answers, a second renaming a directory
   and querying, a third user querying another domain; expect the third to hang.
2. **Build versus writes:** `tests/buildrace.c` style, but with `rename`, `link`,
   `unlink` and `chmod` of files and of intermediate directories interleaved with
   `scan_delay_us`, under KASAN, plus `umount` attempts; and the negative-dentry
   `stat`/`creat` race of finding 3 under KASAN on a `CONFIG_PREEMPT` kernel.
3. **Permission matrix for the homogeneity rule (finding 1):** every mode in
   {0004, 0040, 0400, 0604, 0640, 0444} × {owner, group member, other} × {file,
   intermediate directory}, asserting that `QUERY2` returns a file if and only if
   `getxattr` on the lower succeeds for that caller.

## Verification, 2026-10-15 (outcome of each finding)

Each finding was re-derived from the source of 5582e06 and checked against Linux 6.12.112
(`fs/namei.c`, `fs/dcache.c`, `fs/open.c`, `fs/backing-file.c`, `fs/namespace.c`,
`kernel/locking/rwsem.c`, `fs/fuse/dev.c`, `fs/ext4/ext4_jbd2.c`, `fs/xfs/xfs_trans.c`).

| # | Verdict | Corrected severity | Fixed in |
| --- | --- | --- | --- |
| 1 | Confirmed (`acl_permission_check` judges owner and group by their own bits) | high | journal 0116, finding F9 |
| 2 | Confirmed; the two kernel claims cited from memory hold: `request_wait_answer()` ends in an uninterruptible wait once the request is read, and `rwsem_down_read_slowpath()` queues a new reader behind a waiting writer when the semaphore is owned by readers. Recovery does not need root: the daemon's owner can abort the connection or let the daemon die. `LOOKUP_NO_XDEV` alone does not close the class (a walk can still wait on a directory lock of the layer itself, suspected path); releasing `query_sem` per chunk does, provided each chunk pins the domain again | high | journal 0116, finding F10 |
| 3 | Confirmed as a real window, downgraded: it needs a preemptible kernel and a full grace period between two loads. The suggested alternative "skip the lower revalidate" is wrong: ext4 has `d_revalidate` for casefolded and encrypted names | medium, plausible | journal 0120 |
| 4 | Confirmed, and incomplete: the worse consequence is a frozen lower (any user's create or chmod hits `WARN_ON` in `ext4_journal_check_start()`), recorded as a new finding | medium | journal 0117, finding F11 |
| 5 | Confirmed from the source; impact not measured | medium | journal 0120 |
| 6 | Confirmed from the source, downgraded: with the default 128 MiB budget the walk lasts tens of milliseconds; seconds only with a budget of several GiB | low | journal 0120 |
| 7 | Confirmed | low | journal 0120 |
| 8 | Confirmed; fixed in the code rather than the ADR | low | journal 0120 |
| 9 | Confirmed at 5582e06; the suggested fix (`omit` only under BINARY) was not enough, and journal 0112 had not fixed it either: the condition must not reach the kernel at all under another collation | low (user space) | journal 0120 |

New findings of the verification:

- **Frozen or read-only lower mount** (extension of item 4): finding F11, journal 0117.
- **Idmapped lower mount ignored** (`nop_mnt_idmap` everywhere): finding F12, journal 0117; it had
  been noted as R5 of `docs/project-review-2026-10-10.md` and never fixed.
- **Suspected**: a per-result check waiting on the lock of one of the layer's own directories,
  held by a reader stalled on a fault in its buffer; closed by the same change as item 2.

Items 1, 2 and the frozen-lower finding were reported as private security advisories before the
fixes were published (GHSA-822g-5gf6-fj9q, GHSA-r83m-5gx7-mfxx, GHSA-p942-3xhv-m45h).

*Published 2026-10-18, after the fixes were merged (pull request 80 for F9 to F12; journal 0120 for
items 3, 5, 6, 7, 8 and 9).*

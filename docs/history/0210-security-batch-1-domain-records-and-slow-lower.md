# 0210 — Security batch 1: domain records dropped on rmdir (#113), QUERY2 over a slow lower (#118)

Date: 2026-10-27. Status: done on a local branch; closes issues #113 and #118 once merged.
Work started: 2026-10-27, afternoon (America/Sao_Paulo), when the maintainer set the order
security, then consumable C6, then evolution G6. Ended: see the checks below. Wall-clock time,
not an engineering-effort measurement. #113 estimated M; #118's evidence S. First batch under
the check cycle of journal 0209.

## Goal and decisions

**#113 (R2-11 of the round-2 review).** `domain_get()` never removed a domain record and
`moobl_index_perm_changed()` added fallback marks; both were freed only at unmount. So
`mkdir d; QUERY2 d; rmdir d` in a loop allocated one record each time, outside the index budget
and the shrinker, on a lower that does not reuse inode numbers (btrfs). The maintainer chose the
real fix, dropping the record when its directory is removed, over two alternatives: counting the
records in the index budget, which would bound memory but let dead records fill the budget until
the next mount, and documenting the gap.

The records could not simply be erased. `domain_lru()` walked `ix->domains` with no lock ("domains
are never removed ... so the walk needs no lock"), `query2_pin()` read a record before taking
`query_sem`, and `domain_ensure()` held a record across its build mutex and the whole scan. So:

- **References.** A record carries a `refcount_t`: one for `ix->domains`, one for each holder that
  uses it without the index lock or `query_sem`. `domain_get()` takes one (`domain_ensure()` drops it
  through `domain_unpin()`); `domain_lru()` walks under `rcu_read_lock()` and takes one on the record
  it returns, which `domain_release()` drops. The last reference frees the record with `kfree_rcu()`.
  `query2_pin()` now reads the record only under `query_sem`.
- **Removal.** `rmdir` calls `moobl_index_dir_removed()`, which erases the domain's fallback mark,
  marks the record gone, and makes a built one answer nothing until it is built again. It then tries
  `domain_reap()`: the record's build mutex, `query_sem` and the index lock, each only *tried*,
  because rmdir holds the VFS directory locks a build of that domain may be waiting for. Under all
  three, a record that is still gone and has no holder is taken down if built and erased. A busy
  record is dropped by its last holder (`domain_unpin()`) or by a later attempt before a new record
  is allocated (`domain_reap_some()`, up to eight).
- **Inode reuse.** ext4 and XFS reuse inode numbers. A new domain directory that finds a record
  marked gone takes it back, and a built one is built again.
- **Removed directories through a descriptor.** A `QUERY2` on a directory already removed returns
  `ENOENT` and creates no record. A build that finishes after its directory was removed drops its
  record and returns `ENOENT`. A chmod on a removed directory adds no fallback mark. Without these,
  `fd = open(d); rmdir(d); QUERY2(fd)` would leave a record that no rmdir ever removes.
- A read-only module parameter, `domain_records`, counts the records of every mount, for tests
  only, like `scan_delay_us`. No UAPI, format or mount-option change.

**#118 (M3).** The resource guards of PRs 135 and 138 were already merged. The issue stayed open
for a really slow lower device and a real client project of about 17,000 files in one domain. The
second exists since G5 (journal 0201): domains of about 25,000 real memories built at
`index_build_work=65536`. For the first, a lower behind `dm-delay` on the dedicated host (below).
The maintainer accepted closing the issue with that evidence. The availability limit stays stated
in `SECURITY.md`: an unprivileged query can start a build with the mounter's credentials, bounded
by work, deadline and rate, not by a strict CPU or I/O cap. Fair sharing (S9) is a separate item.

## Changes

- `layer/moobl.h`: `refcount_t ref`, `rcu_head`, `key` and `gone` in `struct moobl_domain`;
  `domains_gone` in the index; `moobl_index_dir_removed()`.
- `layer/index.c`: `domain_put()`, `domain_reap()`, `domain_unpin()`, `domain_reap_some()`; the
  reference in `domain_get()`; `domain_ensure()` split so the record is held for the build;
  `domain_lru()` and the shrinker's count under RCU; `domain_release()` drops its reference;
  `query2_pin()` reads under `query_sem` and refuses removed directories;
  `moobl_index_dir_removed()`; `moobl_index_perm_changed()` ignores removed directories; the
  `domain_records` test counter.
- `layer/main.c`: `moobl_rmdir()` calls `moobl_index_dir_removed()`.
- `vm/suite.sh`: step `domainrecs`, on btrfs and on ext4.
- `bench/client/issue118_slow_lower.py` and `bench/results/2026-10-27-issue118-slow-lower/`.
- Both READMEs, `docs/status.md` and `docs/status.pt-BR.md`, `SECURITY.md`, ADR-0020 (note),
  `docs/testing.md`, the changelog and the history index.

## Evidence

All on the dedicated Debian host. KASAN runs used `vm/kasan-local.sh` with the cached upstream
6.12.112 KASAN/UBSAN/lockdep kernel of `vm/kasan.config`.

- `make layer W=1` against 6.12.111: no warning. `make checkpatch`: nothing reported.
- `MOOB_ONLY=domainrecs sh vm/kasan-local.sh`: `DOMAINRECS_PASS` and `MOOBFS_KASAN_ONLY_PASS`, no
  kernel diagnostic. btrfs and ext4 each: records 0 before and 0 after 200 domains, each new
  directory answered with its own file, fallback marks unchanged after 50 chmod-then-rmdir and after
  the removed directory.
- Negative round 1, the `rmdir` call removed: `MOOBFS_VM_FAIL: dr-records-btrfs: 0 before, 200
  after 200 domains`, the growth of the issue.
- Negative round 2, the `ENOENT` checks of `query2_pin()` removed: `MOOBFS_VM_FAIL:
  dr-dead-query-btrfs (want ENOENT)`. The sources were restored and checked by hash afterwards.
- #118, `bench/client/issue118_slow_lower.py` on 6.12.111 with this branch's module: 5,000 files,
  2,500 matching. First `QUERY2` after a fresh mount and a dropped page cache: 0.30 s with no delay,
  0.81 s at 5 ms per I/O, 1.37 s at 20 ms, 3.05 s at 50 ms, 5.25 s at 100 ms, 9.76 s at 200 ms, all
  2,500 results. At 400 ms, `ETIMEDOUT` after 11.07 s with no result list; the second query answered
  all 2,500 in 8.46 s, and with the delay back at 0 in 0.04 s. `dmesg` showed only the expected
  `building the index of a domain failed (-110)`.
- The full batch suite: recorded below once run.

## Errors found on the way

- Preparing the #118 run, a command made the operator's home on the dedicated host readable by
  every user (`chmod -R a+rX ~`, meant for the library's directory only). It was found in the same
  session and the home was set back to `0700`, `~/.ssh` to `0700` and `authorized_keys` to `0600`.
  The window lasted a few minutes. The other accounts on the host are test users only, and `~/.ssh`
  holds no private key. No secret is known to have been read.

## Limitations

- The `domain_records` counter is module-wide. The test runs one mount at a time.
- A record whose removal finds it busy waits for its last holder or for the next record
  allocation. Between those, a gone record stays allocated, bounded by the domains in use at that
  moment.
- A directory removed on the lower behind the layer (not through it) does not pass through the
  layer's rmdir, so its record stays until unmount, as before. Changing the lower behind a mounted
  layer is outside the contract (ADR-0007).
- `dm-delay` slows a loop device; no physically slow disk was used.
- The full `make vm-test` (Docker, Debian 6.12.111 and 6.12.113) runs in the pull request's CI: the
  dedicated host has no Docker.

## Next step

Security batch 2: S9, fair sharing of the index budget between users, then the G2 latency
ceiling.

## Checks of this commit

Recorded below once run.

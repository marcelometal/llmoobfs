# 0115 — KCSAN over the layer: first runs, no data race in the module (security roadmap S8)

Date: 2026-10-15 to 2026-10-17. Started 2026-10-15 05:29, ended 2026-10-17 17:40 (calendar time,
local; the first run alone took about 50 minutes, and the work stopped for about two days on a
usage limit). Status: triage done, filter written and checked offline; the run with the filter
has not been executed. Estimated effort: S.

## Goal

Run the stress steps of the suite (`buildrace`, `release`) under the Kernel Concurrency
Sanitizer, the data-race detector of security roadmap S8, and fix what it finds in the layer.
KCSAN cannot be built together with KASAN, so it has its own kernel fragment
(`vm/kcsan.config`, added with commit 9032fa8: the KASAN fragment with KASAN and UBSAN replaced
by KCSAN, lockdep kept; `KCONFIG=` of `vm/kasan-local.sh`) and its own kernel cache.

## First run: the steps passed, the harness failed on the kernel's own races

```
KCONFIG=vm/kcsan.config KCACHE=~/.cache/moobfs-kcsan MOOB_ONLY="buildrace release" sh vm/kasan-local.sh
```

on the benchmark host (upstream 6.12.112, KCSAN with `CONFIG_KCSAN_REPORT_VALUE_CHANGE_ONLY`
and the interrupt watcher, lockdep). Every step passed (`BUILDRACE_DONE` on the three
filesystems, `RELEASE_PASS` on ext4, btrfs and XFS) and the run ended in
`MOOBFS_VM_FAIL: kernel-diagnostic-layer`: the suite's last check treats any `BUG:` line as a
failure, and a KCSAN report starts with `BUG: KCSAN:`.

35 reports. Each was read and, where the layer was on the stack, its offsets were resolved
with `scripts/faddr2line` against the run's `vmlinux`:

| Report (count) | Racing accesses | Layer on the stack | Verdict |
| --- | --- | --- | --- |
| `tick_nohz_handler / tick_nohz_idle_got_tick` (19) | timer code | no | kernel, not ours |
| `virtqueue_kick_prepare`, `virtqueue_get_buf_ctx`, `vring_interrupt` (10) | virtio ring, guest devices | no | kernel, not ours |
| `dentry_unlink_inode / step_into` (1) | `d_flags` during an `execve` path walk on the 9p root | no | kernel, not ours |
| `d_splice_alias / d_splice_alias` (2) | `start_dir_add()` reads `dir->i_dir_seq` plainly, `end_dir_add()` stores it with `smp_store_release()` (fs/dcache.c:2442, 2452) | yes, as the caller: `moobl_lookup` and the build's `scan_run` look up names of the same lower directory in parallel | kernel: the plain read is only a guess that the `cmpxchg()` beside it validates |
| `mb_cache_entry_create` / `mb_cache_entry_delete_or_get` (2) | `cache->c_entry_count`, read without the list lock to decide whether to schedule reclaim (fs/mbcache.c:82), updated under it (119, 273) | yes, as the caller: `moobl_xattr_set` reaches ext4's xattr cache through `vfs_setxattr` and `vfs_listxattr` | kernel: a heuristic read, an off-by-one only delays a shrink |

None of the 35 has an access whose top frame is in `moobfs_layer`: the module itself raced on
nothing these steps exercise. No change was made to `layer/`.

## The harness: KCSAN reports judged apart

`vm/kdiag.sh` (new, sourced by `vm/kasan-guest.sh`, and used by the suite's last check when it
is defined) replaces the plain `grep` of the kernel log. A KCSAN report fails the run only when
the top frame of one of its access stacks is a function of `moobfs_layer`; the others are
counted on one `KCSAN_OUTSIDE:` line, with how many had the layer lower on the stack. Every
other diagnostic (KASAN, UBSAN, lockdep, `WARNING`, `BUG`) fails as before, and without KCSAN in
the kernel the function is the plain `grep`; the initramfs harness, which does not have it, keeps
its `grep`.

The rule is the top frame, not "the layer anywhere in the stack": the four reports above have
the layer as a caller of the kernel's own racy code, and failing on them would make the run
depend on races that are not the module's to fix. The filter of KCSAN's debugfs file, which
takes function names, was not used: it is set before the module is loaded and the module is
loaded and unloaded several times per run.

Checked offline on the first run's console log: `KCSAN_OUTSIDE: 36 report(s) with no racing
access in moobfs_layer (4 reached through it)` and exit 0 (36: one title printed again by the
failing check at the end of the log); the same log with one top frame renamed to a module
function fails and prints that report. `shellcheck --shell=sh --severity=warning` (0.11.0, as
`make lint`): clean.

## Runs with the filter

Not executed. The run with the filter was being prepared when the work stopped, and the
benchmark host was not reachable when it resumed. Until it runs, the filter is checked only
against the console log of the first run (above), not on a live kernel.

## Limitations

- KCSAN is a sampling detector: a clean run says the races were not observed in these steps,
  not that there are none. Only `buildrace` and `release` ran under it; the rest of the suite,
  the fault-injection block and syzkaller have not.
- The top-frame rule would miss a race whose racing access is in an inline helper of the kernel
  called on the module's own data (the top frame is then the helper). None of the 35 reports is
  of that kind; it is to be kept in mind when reading `KCSAN_OUTSIDE` lines with reports reached
  through the layer.
- KCSAN does not run in CI; the run is local, on the benchmark host.

## Next step

Run `KCONFIG=vm/kcsan.config KCACHE=~/.cache/moobfs-kcsan MOOB_ONLY="buildrace release" sh
vm/kasan-local.sh` with the filter: it must end in the KCSAN marker with a `KCSAN_OUTSIDE:` line.
Then one negative round (an unmarked racy access added to the layer, reverted afterwards) must
fail it, and KCSAN should cover the rest of the suite and the steps of journal 0116.

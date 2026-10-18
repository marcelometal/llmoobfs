# 0131 — Negative rounds: each test fails without its fix

Date: 2026-10-18. Status: done for the four rounds pending since journals 0120 and 0127. The
rounds of findings F2 and F4 are recorded in their own journals (0130, 0132). Estimated effort: S.

## Why

A test that passes with a fix proves little until it is shown to fail without it. Journal 0120
left three negative rounds open (items 9, 7 and 5 of the second source review), and journal 0127
left the negative round of the KCSAN filter. They waited for the benchmark host. Since the KASAN
and KCSAN workflows run on GitHub with KVM, they ran there instead, in parallel.

## Method

Each round is a throwaway branch `neg/<name>` from `main`, with only the fix undone (or, for
the filter, a defect added), and the workflow dispatched on it. These branches are never merged.

| Round | Change on the branch | Workflow, run | Result |
| --- | --- | --- | --- |
| Item 9, collation | the BINARY test in `sqlite/moobfs_vtab.c` disabled | KASAN, 39163834997 | failed: `MOOBFS_VM_FAIL: sx-differs` (step `sqlext`) |
| Item 7, write-only file | `vfs_getxattr()` back in `moobl_typed_check()` | KASAN, 39163837101 | failed: `MOOBFS_VM_FAIL: typed-attr-write-only-refused-ext4` |
| Item 5, batch boundary | the name that overflows a batch dropped instead of kept for the next one | KASAN, 39163839288 | failed: `MOOBFS_VM_FAIL: bd-differs-from-oracle` (step `bigdir`) |
| KCSAN filter | an unmarked counter written from every CPU in `moobl_d_revalidate()` | KCSAN, 39163841111 | failed: `BUG: KCSAN: data-race in moobl_d_revalidate [moobfs_layer] / moobl_d_revalidate [moobfs_layer]`, judged `MOOBFS_VM_FAIL: kernel-diagnostic-layer` |

## Notes

- Item 5's test checks answers, not memory. Undoing the batching would leave answers equal, so
  the round breaks the batch boundary instead. It shows that `bigdir` crosses batch boundaries
  and would catch a name lost there; that the build holds one batch at most is still read from
  the code, not measured.
- In the KCSAN round, the kernel's own races (virtio, tick) were reported as well, and only the
  race inside `moobfs_layer` failed the run, as `vm/kdiag.sh` intends.
- While preparing item 9, a gap was found and fixed in journal 0129: the KASAN workflow's
  pull-request filter did not cover `sqlite/`, `tools/` or `bench/`.

## Next step

None for these rounds. The `neg/*` branches stay as evidence until the maintainer decides to
delete them.

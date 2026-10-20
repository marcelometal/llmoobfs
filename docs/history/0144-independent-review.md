# 0144 — Independent review: file-status flags and attribute read errors

Date: 2026-10-20. Status: review round complete; two correctness findings remain
unfixed, one reproduced in a dedicated VM and one established by source inspection.
This is an AI-assisted review, not an outside human audit or a safety certification.

## Goal and scope

Review `d110d4e63e18eb009ef385ff0dc7856c0393ef14` (`v0.4.1-experimental`)
independently, alongside three isolated investigations: xattr-get coverage (0145),
public documentation (0146) and query latency (0147). Pull request 105 does not
change the reviewed module source. Work uses `codex/independent-review-0144`,
leaving the shared checkout alone.

Read the handoff and memory rules, repository instructions, specifications,
ADRs 0002/0007/0010/0011/0016 and earlier review/correction records. Inspected
file operations, credentials, index construction/updates, query validation,
result checks, cursor/buffer ownership and typed-attribute validation.
Compared the file-operation path with the exact Linux 6.12.112 sources used by
the test kernel, including backing-file helpers and overlayfs. This is a bounded
review, not an exhaustive proof or a new concurrency stress campaign.

Two other agents independently checked and confirmed the source paths below.
Known limitations such as direct lower writes, query/open TOCTOU, domain-count
leakage and the outstanding fair-sharing part of F2 are not new findings.
No privilege bypass or memory-corruption defect was established in this round.

## R1 — Clearing O_APPEND does not clear the lower file's append flag

Severity: medium, file-I/O correctness. Confidence: reproduced.

Locations at the reviewed commit: `layer/main.c:96` (`moobl_real`), `1250–1265`
(`moobl_open`), `1291–1310` (`moobl_write_iter`).

The lower file receives the upper flags at open. Later `F_SETFL` changes are not
synchronized to `mf->real`. On a synchronous write, `backing_file_write_iter()`
calls `vfs_iter_write()` on the lower file; its new `kiocb` inherits the lower
file's old `f_iocb_flags`. Clearing the upper append flag leaves `IOCB_APPEND`
in the lower write. `generic_write_checks_count()` moves the offset to EOF.

Reproduction: create a new file with `O_RDWR | O_CREAT | O_EXCL | O_APPEND`,
write `AAAA`, clear `O_APPEND` with `F_SETFL`, then `pwrite(fd, "Z", 1, 0)`.
Expected: `ZAAA`, length 4. Observed through MoobFS: `AAAAZ`, length 5,
although `F_GETFL` reports append disabled.

The standalone [reproducer](../../tests/fcntl-flags.c) returns 0 for correct
contents, 1 for mismatched contents and 2 for setup/I/O errors. It refuses an
existing path. It is not wired into the passing suite before the fix.

Executed in a fresh dedicated virtme-ng VM with Linux `6.12.112-virtme` from
the existing KASAN kernel build, two vCPUs, 3 GiB and a new 64 MiB ext4 image:

```text
FCNTL_RESULT path=/tmp/review-lower/baseline bytes=4 content=ZAAA append_flag=0
FCNTL_RESULT path=/tmp/review-layer/through-layer bytes=5 content=AAAAZ append_flag=0
REVIEW_FCNTL_DONE baseline=0 layer=1 kernel=6.12.112-virtme
```

Built the module from `git archive d110d4e`; compiled the test using
`cc -O2 -Wall -Wextra -static`. The guest mounted ext4 and the layer, ran the
test directly and through the layer, then unmounted both and unloaded the module.
Only this guest loaded the module. The repository's `kdiag` matcher found no
KASAN/UBSAN/lockdep, BUG, Oops or task/RCU diagnostic in captured dmesg.
This reproduces a correctness failure; it is not a passing full sanitizer suite.

Evidence remains in the local review folder (`fcntl-0144-console.log`,
`fcntl-0144-dmesg.txt`, `fcntl-0144-build.log`) and the fresh remote workspace
`~/review-fcntl-0144.caIkrk/`. No existing image or host module changed; vng exited.
The committed reproducer adds only identification/license comments to the executed
source. Reproduce inside a dedicated VM after mounting the two paths above:

```sh
cc -O2 -Wall -Wextra -static tests/fcntl-flags.c -o /tmp/fcntl-flags
/tmp/fcntl-flags /tmp/review-lower/baseline
/tmp/fcntl-flags /tmp/review-layer/through-layer
```

Prior art: overlayfs compares flags in `ovl_real_fdget()` and updates both
`f_flags` and `f_iocb_flags` in `ovl_change_flags()`. A fix should follow the
exact kernel's validation and synchronization rules, testing both directions of
mutable flag changes. The related `O_DIRECT` concern was not tested here.

## R2 — Operational getxattr errors become a successful incomplete index

Severity: medium, query correctness. Confidence: high in the source path;
not dynamically reproduced with a lower-filesystem read failure.

Location: `layer/index.c:1049–1051`, in `attrs_read()`.

Every `vfs_getxattr()` return `n <= 0` is treated as an invalid or absent value,
including operational errors such as `-EIO` and `-ENOMEM`. `attrs_read()` can
return 0 after skipping that attribute; `index_load()` applies the reduced set;
`domain_ensure()` can mark the domain `MOOBL_DOM_BUILT` and clear `lost`.
Subsequent queries accept it as fresh. `index_adopt()` can likewise miss
invalidation because it receives success.

Concrete scenario: the first query builds a domain with a readable, homogeneous
file containing valid `user.moob.kind=note`. Listing succeeds, but fetching the
value returns a transient `EIO`. Without a concurrent write, the code completes
the domain and can answer `kind EQ note` successfully while omitting the file.
The omission persists until a rebuild. This is the consequence of the inspected
control flow, not an observed injected-I/O test.

Expected by `docs/spec-query-v2.md:291–297`: propagate lower scan errors, discard
the failed build and retry later; never answer from a partial domain.
Ignoring malformed encodings, oversized values or a concurrently removed
attribute does not justify ignoring operational failures. The dirty-loader
retry handles concurrent updates, not the discarded error.

Next validation: a dedicated-VM test must distinguish absent/malformed attributes
from listed valid attributes whose value read fails, covering first build,
adoption/update and recovery. No fault-injection result or fix is claimed here.

## Parallel results and integration

- **0145:** saved coverage is 0/4 PCs for `moobl_xattr_get`; the object has an
  instrumented body. Cause unresolved. A security classifier interrupted seed
  execution; it was not retried or bypassed. The aggregate 88.7% does not alone
  prove S3's separate per-function criterion.
- **0146:** public claims reconciled with code and recorded evidence, in English
  and Portuguese. Documentation checks and the recorded negative check passed
  and failed, respectively, as detailed there. No external surface published.
- **0147:** reanalysis of 20,000 existing calls reproduces the stored medians.
  Removing the first cycle leaves p50 differences of +1.16%, +2.62%, +3.03%;
  Q1's p99 difference falls from about +54% to +3.33%. Cause unassigned;
  no new benchmark.

Pull request 105's KASAN/lockdep check completed successfully at 2026-10-20
09:22:04 UTC, alongside all other checks. Under the maintainer's prior
authorization, it was merged at 09:25:53 UTC as `3627172`.
This records that PR's CI result, not validation of fixes for R1/R2.

Integrated the parallel commits locally on that merge. Added history index,
changelog, reproducer and dated qualification of journal 0143. No module,
on-disk format or UAPI change; no new functionality. Review work is not pushed.

## Validation, limitations and next step

After integration, `sh scripts/check-retired-claims.sh` reported
`RETIRED_CLAIMS_OK`, `python3 scripts/site-faq.py --check` matched its source,
and `python3 scripts/check-site.py .` validated all 12 pages.
`git diff --check` and `cc -Wall -Wextra -fsyntax-only tests/fcntl-flags.c`
also completed successfully. The latter is only a local syntax check, separate
from the actual guest execution above.

No complete `make vm-test`, correction negative round or batch KASAN run was
performed for R1/R2 because there is no correction yet. The targeted test does
not establish behavior of other flags, filesystems or concurrent accesses.
Coverage and performance causes remain open.

Next: a local R1/R2 correction batch with regression tests, negative rounds,
per-correction checks and final batch KASAN (immediate lockdep validation for
lock/lifetime changes). Resolve the blocked coverage execution through the
normal approval path. Feature decisions and outside human review remain separate.

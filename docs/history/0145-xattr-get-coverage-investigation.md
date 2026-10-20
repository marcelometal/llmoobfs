# 0145 — Investigating the missing xattr-get coverage

Date: 2026-10-20. Status: static inspection complete; runtime investigation interrupted
before the seed was executed. No module or harness fix.

## Goal and scope

Explain why the fifth syzkaller run reports `moobl_xattr_get` at 0 of 4 PCs despite the
`xattr-flags` seed containing descriptor and pathname reads. The investigation uses its own
worktree, based on `syz-fifth-run` at `dfe114c`, and does not change the main checkout.
Related records: journals 0138 and 0143, ADR-0011, and security roadmap S3.

The intended experiment was to replay the existing seed in a dedicated snapshot VM and
collect per-call coverage, using the existing fuzzing kernel and module. It was not executed.
No change of the physical host's module, CPU frequency, mounts or source tree was made.

## Evidence actually inspected

On the benchmark host, the existing `syz-main/build/syzkaller/funccover.csv` contains:

```text
moobfs_layer,home/debian/syz-main/layer/main.c,moobl_xattr_set.cold,1,6
moobfs_layer,home/debian/syz-main/layer/main.c,moobl_xattr_set,38,50
moobfs_layer,home/debian/syz-main/layer/main.c,moobl_xattr_get,0,4
```

`nm -S syz-main/layer/moobfs_layer.ko` shows a concrete local text symbol
`moobl_xattr_get`, offset `0x0a40`, size `0x01ff`. Disassembly of that function includes
calls with `R_X86_64_PLT32 __sanitizer_cov_trace_pc-0x4` relocations, including the entry
site at offset `0x0a60`. Thus a claim that this handler has no separately instrumented
body in this object is contradicted by the object itself. This does **not** establish
that the guest executed the same object or explain how the manager attributed coverage.

The source registers this function as the xattr handler's `.get` callback. The existing
seed has two `fgetxattr$moob` and two `getxattr$moob` calls, covering a typed name,
`security.selinux`, a pathname read and a short buffer. The harness enables both call
variants. Their presence in source does not prove successful execution in the fifth run.

The host's syzkaller checkout reports commit
`8fb0ae89890497b42240b9325a0af718bb41645d`, matching the version already cited in
[references](../references.md#syzkaller-security-roadmap-s3-journal-0109).
The exact configured fuzzing kernel tree is `~/.cache/moobfs-syz/linux-6.12.112`.
The physical host reports `6.12.111+deb13-amd64`; it was not used to load or test a module.

Read-only commands actually executed include:

```sh
grep moobl_xattr ~/syz-main/build/syzkaller/funccover.csv
nm -S ~/syz-main/layer/moobfs_layer.ko | grep moobl_xattr
objdump -dr --disassemble=moobl_xattr_get ~/syz-main/layer/moobfs_layer.ko | head -100
git -C ~/syz/syzkaller rev-parse HEAD
```

The seed, harness, kernel config fragment, image setup script and `syz-execprog` source
were also read. A source search using `rg` on the remote host failed because `rg` is
not installed there; no conclusion relies on that failed search.

## VM lifecycle and interruption

A new QEMU/KVM guest was booted with two CPUs, 2 GiB, the existing 6.12.112 kernel and
`snapshot=on` over the existing syzkaller image. Its console reached the SSH service and
login prompt. An early SSH readiness query timed out; it produced no guest kernel,
module or symbol output. No test binary or seed was copied to the guest and no
syzkaller/KCOV test was started.

The coordinating agent received the notification:

> Agent errored: This content was flagged for possible cybersecurity risk

The coordinator instructed this investigation to stop without repeating or bypassing the
blocked action. That notification is an execution limitation, not a result from MoobFS.
There is no new syscall result, coverage trace, crash report or pass result to attribute
to the module.

Only cleanup followed. The newly created QEMU process was identified by its own PID file.
The guest was asked to `poweroff`. Although the command printed a system-bus error, the
subsequent existing console log records orderly device detachment, filesystem
synchronization and `reboot: Power down`. Its PID file disappeared and its dedicated SSH
port refused a connection. A later attempted `sync; /sbin/poweroff -f` did not reach the
guest because it had already shut down.

The isolated directory and console log remain on the benchmark host under
`~/xattr-kcov-0145.CVHcRr/`; the backing image and fifth-run artifacts were preserved.
No VM from this investigation remains active.

## Interpretation and remaining work

The 0/4 report is confirmed in the saved fifth-run artifact; the explanation remains
unknown. Complete elimination of this function by inlining is not a supported explanation
for the inspected object. Other possibilities, including calls failing before the callback,
corpus selection and module-to-coverage attribution, have not been tested.

**Dated qualification of journal 0143 (2026-10-20):** its 88.7% aggregate exceeds the
numeric S3 threshold. However, the written S3 criterion also requires every reachable
function to be exercised, with reasons for exclusions. The saved artifact still reports
zero for this reachable callback, and this investigation provides no runtime evidence
resolving that gap. Therefore the aggregate alone does not establish the full S3 criterion.
The coordinator and documentation review were informed so public claims can be qualified
without rewriting the earlier record.

No `make vm-test`, KASAN run, negative round, module build, seed replay or new coverage
measurement was completed here. No runtime test is claimed as passed. The remaining
experimental step requires resolving the execution restriction through the normal
approval path; it was not resumed or replaced with a workaround.

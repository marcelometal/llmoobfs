# 0128 — Shell scripts pass shellcheck at every severity (issue #5)

Date: 2026-10-18. Status: in progress. The change is committed locally; `make vm-test` and the
KASAN workflow on the pull request are still to be run. Estimated effort: S.

## Why

Issue #5: the `Lint` job ran shellcheck at `--severity=warning`, and the info-level findings
(1211 with shellcheck 0.11, almost all SC2086 in `vm/suite.sh`) stayed open as a starter task.
Following journal 0125, issues are closed by the project itself.

## Changes

- SC2086 (1107 findings): the expansions in `vm/suite.sh` and `vm/kasan-guest.sh` are quoted.
  They were applied with shellcheck's own fix output (`-f diff`), and adjacent quoted segments
  were merged (`"$LAYER/$d"`, not `"$LAYER"/"$d"`). Check: with every `"` removed, each line is
  identical to the original, so the only change is quoting.
- One deliberate exception: in the `buildrace` comparison `$q` holds one condition as four words
  and is split on purpose; it keeps a `# shellcheck disable=SC2086` line with that reason.
- SC2015 (91): every `A && B || C` in the scripts has as C `fail`, `return`, `exit` or an error
  branch, so it is the intended "fail if either fails". It is disabled once, in `.shellcheckrc`,
  with that reason.
- Individual fixes:
  - `MODS/order` is read with `while read` (the file has one name per line, written by
    `vm/in-container.sh`).
  - The exit status of `moobfs-check` is tested directly.
  - The `for` glob is quoted.
  - `sq()` was defined outside `sqlite_run`, which redefines it on every call, so the copy
    outside is removed.
  - The SELinux labels are read with `stat -c %C` instead of `ls -Z | cut`.
  - The syzkaller crash count uses `find`.
  - Justified disables remain for MODS (comes from the environment), PATH in `demo()`
    (local to a subshell on purpose) and the untracked fstests `env.sh`.
- `make lint` (and the CI through it) now runs shellcheck at every severity.
  `docs/development.md` and CONTRIBUTING say so.

## Evidence

- `shellcheck --shell=sh` on every tracked `vm/*.sh` and `scripts/*.sh`: no findings with
  versions 0.9.0 (Ubuntu 24.04, as the CI installs it), 0.10.0 and 0.11.0.
- `sh -n vm/suite.sh`: syntax accepted.

## Limitations

- Not yet run in the VM. `vm/selinux-guest.sh`, `vm/fstests-guest.sh` and
  `vm/syzkaller-local.sh` are not exercised by the CI, so their small edits run only with
  `make selinux-test`, the fstests run and the syzkaller run on the benchmark host.
- `bench/` and other scripts outside `vm/` and `scripts/` are not linted. There, most of the
  remaining SC2086 are deliberate splits of a query (`$q`), and those scripts run only on the
  benchmark host.

## Next step

Run `make vm-test` (`MOOBFS_LAYER_PASS`, `MOOBFS_ACCEPTANCE_PASS`), open the pull request (the
KASAN workflow runs because `vm/` changed) and close issue #5. Then issue #72 (one list of test
programs).

*Note, 2026-10-18 (VM run).* The full suite of this change ran under KASAN, UBSAN and lockdep
on the benchmark host (`vm/kasan-local.sh`, native KVM): `MOOBFS_LAYER_PASS`,
`MOOBFS_ACCEPTANCE_PASS` and `MOOBFS_KASAN_PASS`, no kernel diagnostics. The KASAN workflow of
pull request 85 passed too, and the pull request was merged (c3c73e4). The edits in
`vm/selinux-guest.sh`, `vm/fstests-guest.sh` and `vm/syzkaller-local.sh` remain unexercised
until their next runs.

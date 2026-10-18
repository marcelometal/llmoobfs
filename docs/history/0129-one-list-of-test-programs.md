# 0129 — One list of test programs for every VM harness (issue #72)

Date: 2026-10-18. Status: in progress. The change is committed locally; the KASAN workflow on
the pull request and `make vm-test` are still to be run. Estimated effort: S.

## Why

The test programs that run inside the VMs were compiled from five hand-written lists:
`vm/in-container.sh`, `vm/kasan-local.sh` and the KASAN, KCSAN and arm64 workflows. Journal
0106 recorded a KASAN failure caused by a program that had been added to one list only. While
this change was being prepared, the same drift was found in the arm64 workflow, which did not
build `permmatrix` and `fusestall`, so its runs of those steps could not have worked.

## Change

- `vm/build-tests.sh <output dir>`: compiles every test program with the flags each one needs,
  one line per program. The five places call it and their lists are removed. Each harness now
  builds the union of the old lists: the Debian VM also gets `failnth` (run only by the KASAN
  guest), and the KASAN harnesses also get `mounted` (used only by `vm/init`). An unused
  program costs a few seconds of compilation and changes no test.
- `docs/development.md`: a new test program is added in `vm/build-tests.sh` and invoked in
  `vm/suite.sh`, two places instead of three.
- The KASAN workflow's pull-request filter also covers `sqlite/`, `tools/` and `bench/`. The
  suite runs the SQLite extension (step `sqlext`), the tools (`moobfs-index` is the oracle) and
  the benchmark tools, but a change only under those directories skipped the run. Found while
  preparing the negative round of journal 0120's item 9, which touches `sqlite/` only.
- Not changed: `vm/selinux-local.sh` builds only `query` and `moobfs-attr`, for one guest with
  its own needs.

## Evidence

- In the `make vm-test` Docker image (linux/amd64): `sh vm/build-tests.sh /tmp/o` built all 14
  programs; without an argument it prints the usage and exits with status 64.
- shellcheck (warning severity) on the script and the two changed harness scripts: no
  findings.

## Limitations

The harnesses themselves have not run with the change yet. The KASAN workflow runs on the pull
request because `vm/` changed; the KCSAN and arm64 workflows run only on dispatch.

## Next step

Open the pull request, check the KASAN run and close issue #72.

# 0202 — Tool JSON output and schemas (issue #7)

Date: 2026-10-26. Status: implemented and locally host-tested on `claudiofp/ISSUE-7-JSON`.
Work started: 2026-10-26, approximately 10:20 UTC. Work ended: approximately 10:26 UTC. Calendar time is
recorded here; labor time was not measured. Estimated effort: S (hours).

## Goal

Add optional JSON output to the user-space `moobfs-index` oracle, and describe the JSON
documents emitted by the oracle and `moobfs-check`. Keep the index oracle's existing text
output byte for byte because VM and benchmark scripts parse it.

## Decisions

- Use `dir`, `count`, `results`, `ino`, and `path`, matching `moobfs query --json` where the
  values have the same meaning. Do not add kernel-specific path flags to the oracle.
- Emit JSON only after a completed walk and after ordering and `--limit`, so `count` describes
  precisely the emitted results. Keep the existing text printing loop in the non-JSON branch.
- Document the existing checker fields without changing its output. Treat both tool JSON
  shapes as experimental, consistent with `docs/compatibility.md`.
- Wire the new Linux host test into `hostile-test`, which CI's Tools and sanitizers job already
  runs. A separate `json-test` target allows a focused local run. The test uses a small typed
  xattr tree; if the host refuses user xattrs, it reuses the hostile test fixture.

## Changes

`tools/moobfs-index.c`, `tests/tools-json.sh`, `Makefile`, `docs/tools.md`, both tool man pages,
`CHANGELOG.md`, and the history index. The kernel, UAPI, attribute encoder and VM harness are
untouched.

## Evidence

- In a local Ubuntu 24.04 container, `make attr-tools BUILD=build/json-host` compiled all four
  Linux tools with gcc and the project's `-Werror` warning flags. `make json-test
  BUILD=build/json-host` printed `TOOLS_JSON_PASS` using a typed-xattr fixture. `make
  hostile-test BUILD=build/json-host HOSTILE_SEEDS=2` printed `HOSTILE_USER_PASS` and
  `TOOLS_JSON_PASS`. The JSON test compares the oracle's text rows to its JSON entries and
  validates both documents' required fields and types with Python's `json` module.
- In a second local Ubuntu 24.04 container, `make BUILD=build/json-all` built the image tools,
  `make test BUILD=build/json-all` passed 60 attribute checks and nine image tests, and
  `make attr-tools CC=clang BUILD=build/json-clang` compiled all four Linux tools warning-free.
  `shellcheck tests/tools-json.sh` passed.
- `make man-lint` initially found four new-sentence line warnings in the edited manual pages;
  splitting those sentences onto new lines fixed them, and the rerun passed.
- `sh scripts/check-retired-claims.sh` (`RETIRED_CLAIMS_OK`), `sh scripts/check-spdx.sh`,
  `python3 scripts/check-site.py` (12 pages valid), and `git diff --check` passed.

## Limitations and next step

The JSON schema is experimental. Names are emitted as their stored bytes, escaped only for
`"`, `\` and control characters, as `moobfs query --json` does; a name that is not UTF-8
yields a document strict parsers reject (documented in `docs/tools.md`, not tested). This user-space task did not run a mounted-layer comparison
or a VM; the existing VM suite still parses text output, whose printing loop was preserved.
The hostile suite used two seeds locally; CI runs fifty. No project status claim on the
public surfaces inventory changed, so those surfaces require no update. The coordinator can
propose publication to the maintainer; there is no push, PR or merge in this worktree.

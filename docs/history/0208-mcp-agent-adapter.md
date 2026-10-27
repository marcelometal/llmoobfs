# 0208 — MCP adapter over stdio, as the agent's own uid (C7)

Date: 2026-10-27. Status: done on a local branch; roadmap item C7 meets its three acceptance
criteria on the dedicated host. Work started: 2026-10-27 about 13:30 America/Sao_Paulo, when
the maintainer chose C7 over C6 on this session's recommendation (C7 smaller and built on C5;
C6 later). Ended: 2026-10-27 11:18. Wall-clock time, not an engineering-effort measurement.
Estimated M.

## Goal and decision

ROADMAP C7: one adapter that exposes the library to agents through a standard protocol, whose
answers equal the library's, which runs under the agent's own uid and cannot act with another
identity (distribution strategy §2.5), and through which the two-user isolation test passes.
[ADR-0024](../decisions/0024-mcp-agent-adapter.md) records the design:

- an MCP server in the library's package, over stdio only. The agent's host starts it as its own
  subprocess, so it has the agent's credentials and no others;
- it refuses root and setuid or setgid, and cannot switch users. Every permission decision is the
  kernel's, for the agent;
- every path is a domain and a path relative to it under `--root`, with no absolute path, no
  `..` and no symbolic link.

The alternatives (a shared HTTP server, a root server that drops to a given uid, raw `QUERY2`
structures in the tools, a separate package) are in the ADR with the reasons they were not chosen.

## Changes

- `python/src/moobfs/mcp.py`: `Memory` (path confinement and the five operations over the
  library), `build_server` (an `MCPServer` with `info`, `query`, `read`, `write`, `delete`; the
  last two absent with `--read-only`), `check_identity`, and `python -m moobfs.mcp`. JSON values
  map to typed attributes with an optional explicit `type`. A time is an ISO 8601 string or
  integer nanoseconds. Refusals reach the agent as tool errors with their reason. Created
  subdirectories take the domain's mode, setgid included.
- `python/pyproject.toml`: extra `mcp` (`mcp>=2.3`).
- `python/tests/test_mcp.py`: the tools through an in-process client on a plain directory: write,
  read, query and delete; nine path escapes refused for `read` and `write`, among them `..`,
  absolute, a symbolic link inside the domain, a symbolic link as the domain and a NUL; bad terms
  as tool errors; the read-only tool list; `info`; the identity check. Skipped without `mcp`, so
  not in CI.
- `python/tests/mcp_mounted.py`: the acceptance on a mounted layer (below).
- ADR-0024; ROADMAP C7 row; `python/README.md`; `docs/testing.md`; `docs/references.md` §14;
  the changelog; the history index.

## What ran

Dedicated Debian host, kernel `6.12.111+deb13-amd64`, ext4 lower, `domain_depth=1`, module built
with `make layer` from this branch (the layer is unchanged since `c793f8d`). A venv under
`/opt/c7` with `mcp==2.3.0` (28 packages) and the library installed from a copy with
`pip install --no-build-isolation`, setuptools by hash. The venv sits outside the operator's
home because the test users cannot enter it (the G4 attempt of journal 0195 had the same cause).

- `test_mcp.py`: 6 tests, OK.
- `mcp_mounted.py`, as root, which forks into each user and starts `python -m moobfs.mcp` as that
  user's stdio subprocess:

```text
MCP_ANSWERS alice: 143 requests (93 with answers) equal to the library (7 skipped: infinite double), backend kernel
MCP_ANSWERS team: 143 requests (92 with answers) equal to the library (7 skipped: infinite double), backend kernel
MOOBFS_MCP_PASS: answers equal to the library on alice and team through stdio as uid 2001; alice
and bob each refused 4 of 4 calls on the other's domain; same 197 team answers; bob's team write
reached alice; the server refused root
```

The corpus is `kernel_equal.py`'s, with a private `bob` domain (uid 2002) added. The four
cross-domain calls per user are a query, a read, a write and a `../` path. `info` listed only
the domains each user can enter. Neither cross write left a file.

Negative round: with the server's `missing_ok` dropped (on the host copy, restored and
reinstalled afterwards), the test printed `MOOBFS_MCP_FAIL` at the first ordered request with a
`missing_ok` term.

## Errors found on the way

- The first mounted run failed with `ModuleNotFoundError` in the child that became uid 2001: the
  venv was under the operator's home (`0700`). It was rebuilt under `/opt/c7`.

## Limitations

- JSON has no infinity, so a double term of `inf` or `-inf` cannot be sent; such stored values
  remain readable. 7 of 150 random requests were skipped for that.
- One host, ext4, the pinned SDK; neither MCP test runs in CI, which has no `mcp`.
- The path confinement is a scope guard, not a boundary against the agent itself: its uid can
  open directly whatever the kernel allows it.
- Not tested with a real agent host (an IDE or desktop client); the tests use the SDK's own
  client over the same stdio transport.

## Next step

Issue #145: tick C7. The remaining consumable item is C6, in the maintainer's order.

## Checks of this commit

Completed 2026-10-27 at 11:18 America/Sao_Paulo. `git diff --check`,
`sh scripts/check-spdx.sh`, `sh scripts/check-retired-claims.sh` (`RETIRED_CLAIMS_OK`),
`python3 scripts/check-site.py .`, `python3 scripts/site-faq.py --check` and `py_compile` of the
package and tests passed. A local check resolved 326 relative Markdown destinations, anchors
included, in the eight changed documents. The unit tests passed on macOS (Linux, LangGraph and
MCP ones skipped). No kernel code changed.

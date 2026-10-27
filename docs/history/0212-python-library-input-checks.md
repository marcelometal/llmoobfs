# 0212 — Python library: checked terms, int64 range, no helper processes, scan on the directory

Date: 2026-10-27. Status: done on `marcelometal/pylib-hardening`. Work started: 2026-10-27
about 15:35 America/Sao_Paulo; ended about 15:39. Calendar time, not an effort measurement.
Estimated effort: S.

## Goal and decision

A source review of the Python library by a contributor, made with an AI assistant, found four
defects in code the MCP adapter and the LangGraph store share. None is reachable through either
adapter as they build values today; each is fixed in the library.

- `Term` checked only string and double values. A `Value` built by hand with a payload of the
  wrong size reached `_fill()`, whose `ctypes.memmove()` copies the payload into the 1024-byte
  `value` field of the request: `Term("a", "==", Value(T_INT64, b"x" * 4000))` wrote past the
  field, into the next terms and past the structure. `Term` now runs `validate()`, the check
  `set_attr()` already used, on every value.
- `Value.of()` and `Value.time_ns()` let `struct.error` escape for an integer outside 64 bits,
  and so did `Value.of()` for a `datetime` after the year 2262. Callers that catch `ValueError`,
  as the MCP tools do, missed it. Both now raise `ValueError`.
- `publish()` without `replace` and `layer_mounted()` looked the C library up with
  `ctypes.util.find_library("c")` on every call, which starts `/sbin/ldconfig -p` and, where
  that is missing, `gcc`, `cc` or `ld` found through `PATH`. The library now takes the C library
  already loaded in the process (`ctypes.CDLL(None)`, as `bench/client` does), once.
- The scan judged the domain directory by `lstat()` of its path: for a path that is a symbolic
  link to the domain, the homogeneity rule saw the link's `0777` mode and owner instead of the
  directory `QUERY2` would be issued on. It now uses `fstat()` and the ACL of the opened
  directory. Answers could only shrink, never widen.

## Changes

`python/src/moobfs/_libc.py` (new), `attrs.py`, `domain.py`, `publish.py`, `query.py`,
`scan.py`; `python/tests/test_unit.py` (terms with hand-built values, the int64 range) and
`python/tests/test_linux.py` (a link to the domain; no process started by `publish()` or
`layer_mounted()`); the changelog and the history index.

## What ran

Python 3.14.10 on Debian forky, `TMPDIR` on a filesystem with user extended attributes.

- `make pylib-test`: 31 tests, OK, 10 skipped (MCP and LangGraph absent, one needs root).
- The same suite in a venv with the versions of journals 0207 and 0208 (`mcp==2.3.0`,
  `langgraph==1.2.15`, `langgraph-checkpoint==4.3.0`, `langchain-core==1.7.0`): 31 tests, OK,
  2 skipped (root).
- Negative round, `python/src` of `origin/main` with the new tests: `test_int64_range` error,
  `test_term_checks_a_value_built_by_hand` failure, `test_no_helper_process` failure
  (`started ['/sbin/ldconfig', '-p']`), `test_scan_through_a_link_to_the_domain` failure.

## Limitations

Not run as root, so `test_group_domain_and_other_caller` was skipped; not run against a mounted
layer. `ctypes.CDLL(None)` needs a Python linked dynamically against its C library, as every
Linux distribution builds it.

## Merge note (2026-10-27, maintainer)

`_fill()` now also refuses a payload longer than the request's 1024-byte field right before
the copy. `Term` checks its value when it is built, but that check holds only while
`STRING_MAX` equals `VALUE_MAX` and while nobody replaces `term.value.payload` afterwards; the
bound at the copy keeps the field safe in both cases. New unit test
`test_fill_bounds_a_payload_replaced_after_the_term`: 32 tests OK (17 skipped, macOS, Python
3.14); with the `query.py` of this branch before the change it fails with `ValueError not
raised`. The index line and the title now use the repository's `0212 —` form.

## Next step

Review and merge.

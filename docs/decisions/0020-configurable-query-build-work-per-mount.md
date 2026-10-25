# ADR-0020 — Configure the bounded query-build work per mount

Date: 2026-10-25. Status: accepted and merged by PR 138; issue 118 open.
Supersedes the fixed-work-number part of [ADR-0019](0019-bound-query-build-work-and-frequency.md),
while retaining its admission, cumulative accounting, error, deadline and
output-on-error decisions.

## Context

The experimental guard in ADR-0019 fixes a 32,768-unit allowance for every
domain build. Journal 0185 measured a valid 17,000-file, six-attribute
domain denied with `E2BIG` after nearly all of its measured reads. Ten
matched pairs with 20 ms synthetic lower-read delay found a median 17.945 ms
wall saving at that boundary, about 0.44% of the pre-limit median, while
the guarded query used a median 13.262 ms more process CPU. Journal 0186
verified 2,000 real ai-memory MCP writes over ten small projects and the
earlier 60,000-page bulk import used ten domains of roughly 6,000 pages.
Neither client result covers a single project near 17,000 files or a real
slow lower device. A universal new limit is therefore not established.

## Decision

Keep the default at 32,768 work units. Add the mount option
`index_build_work=<units>`, accepted from 1,024 through 65,536 inclusive.
Only the mounter can choose it. The option is immutable for the life of
the mount; a new mount-API reconfiguration may repeat the current value
but must reject a different one. Show a non-default value in mount options
for auditability. Apply the chosen allowance to the same cumulative
counter across names, directory visits, attribute-load attempts, retry
rounds and moved-directory rescans; no counter resets or bypasses are
introduced. The 10-second cooperative deadline, 16 attempts per UID and
64 per mount per 60-second window remain unchanged. A build denied by the
work allowance still returns `E2BIG` without publishing a partial answer.

The upper bound is twice the default and covers the measured 17,000-file
fixture while keeping an explicit finite ceiling. It is an experimental
operator choice, not a claim that every 65,536-unit build is tolerable on
every lower device. The lower bound allows an operator to reduce work on
a slower mount. The option has no effect when indexing is disabled.

## Compatibility and alternatives

Mounts without the new option retain the exact numerical policy and ioctl
behavior already published. An operator who raises the value may now get
a full answer for a domain previously denied with `E2BIG`, at the cost of
more synchronous work. A smaller value may deny domains that previously
succeeded. The time deadline can still deny any size, and neither number
is a strict CPU or physical-I/O byte bound. There is no on-disk format or
ioctl structure change.

Raising the default for everyone was rejected because one host and a
synthetic delay do not establish a safe universal value. Making the
allowance unlimited was rejected because it restores the original
unprivileged rebuild exposure. Splitting client projects into smaller
domains remains a useful client-side option but cannot be assumed for
every existing layout. A dynamic rule based on measured I/O or CPU was
not chosen because the current counters do not attribute all lower work
to one query and the deadline is cooperative.

## Validation gate

Test parsing and rejection of zero, values below 1,024, values above
65,536, negative and malformed values. Check display of a non-default
option, same-value remount acceptance and changed-value refusal through
the new mount API. In a dedicated VM, prove the default still denies a
17,000-file attributed domain with untouched query output and the
configured ceiling completes it. Check a reduced allowance on a small
domain, the existing cumulative work/retry and admission tests, and the
cooperative deadline. Run the complete native VM suite and KASAN/lockdep
before considering publication. Keep issue 118 open for real lower-device
and larger real-client policy evidence.

## Local validation, 2026-10-25

The opt-in dedicated native-KVM test passed invalid-value, boundary,
default-denial, full-answer and remount checks. The complete native VM
suite passed on Debian 6.12.111. The complete KASAN/UBSAN/lockdep suite
passed on upstream 6.12.112 with no kernel diagnostics (journal 0187).
These runs validate the local implementation, not the operator-policy
choice or behavior on a real slow lower device. The opt-in 17,000-file
path ran on the native kernel; the complete KASAN suite exercised the
unchanged default policy.

The maintainer approved this range, default and PR publication on
2026-10-25. Issue 118 remains open for the broader evidence described
above; PR CI and review have not yet completed.

## Publication status, 2026-10-25

PR 138 passed all twelve reported checks on head `6621b21` and merged as
`b7ae1b8`, publishing this mount option in `main`. The preceding paragraph
records the pre-merge review state. The compatibility choice remains the
32,768 default with a bounded operator opt-in; issue 118 remains open for
real slow lower-device and large single-project evidence (journal 0188).

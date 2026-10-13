# 0080 — S6: compatibility policy proposed; one CodeQL rule turned off

Date: 2026-10-13. Status: the policy is a **proposal** awaiting the maintainer; the CodeQL
configuration is in place and takes effect when this series is published. Local series (after pull
request 58).

## Goal

The part of security roadmap S6 that was still missing (the policy of supported kernels and of
UAPI compatibility), and an answer to why the same code-scanning alert keeps having to be
dismissed.

## Compatibility policy

`docs/compatibility.md` states what is true today for the three interfaces that have a version
(kernels, the attribute encoding on disk, the ioctl interface) and proposes the rule for changing
each, plus mount options, tools and releases. It gathers what spec-m2 §10, spec-query-v2, the
README and distribution-strategy §6 already said. Nothing in it is implemented as code: the
build-time refusal of an unlisted kernel (rule 1.2) is named as a code change still to make.

It is a proposal because these are promises the project makes to users, which are the
maintainer's to accept. Decision already taken by the maintainer on this date: no patch release
of `v0.2.0-experimental` for now, although it carries the defects fixed in journals 0074 and
0078.

## CodeQL: `cpp/path-injection` turned off

Finding: of the 22 code-scanning alerts dismissed so far, 21 are `cpp/path-injection`, and none
pointed at a defect. The rule reports a file opened under a name that comes from the command
line. Every program CodeQL analyzes here is a tool or a benchmark that an operator runs with
their own privileges on the files they name, so every new tool raises it again (alerts 44 and 45
for `bench/readcost.c` were the latest; dismissed on this date with the maintainer's
authorization).

The other rules are worth keeping: alerts fixed in the past include three `cpp/unbounded-write`
and six `cpp/world-writable-file-creation`.

Decision: `.github/codeql/codeql-config.yml` excludes that one rule, and the file says when to
turn it back on: before a program exists that opens paths chosen by someone less trusted than the
user running it (a set-user-ID tool, a daemon, a network service, a tool that follows paths read
from a file another party wrote).

Alternatives: keep dismissing by hand (rejected: a required check goes red on every new tool, and
a habit of dismissing hides the alert that matters); ignore the `bench/` and `tools/` paths
(rejected: it would turn off every rule there, including the ones that found defects).

The three alerts that stay open are from Scorecard and are not about code: project younger than
90 days, no approved reviews, static analysis not run on every commit.

## Evidence

- The configuration is syntax only until it runs: the CodeQL workflow triggers on changes under
  `.github/codeql/`, so the pull request that carries this commit is its test. **Not run yet.**

## Next step

`MOOB_ONLY` in `vm/suite.sh` and KASAN as a required check (community actions 17 and 18), which
close S6 together with the maintainer's answer to the proposal.

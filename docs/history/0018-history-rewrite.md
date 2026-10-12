# 0018 — Rewrite of the Git history: removal of questions and personal data

Date: 2026-10-10, America/Sao_Paulo. Status: executed locally; publication by force-push done
by the user (see commands below).

## Decision

Recording questions and answers in the history was a wrong decision for a project open to
contributors; the rule became "decisions, errors and discarded hypotheses". Since the old
versions of the journals contained those questions and also a personal e-mail address and the
name of a personal GitHub account, the user explicitly authorized rewriting the history, the
single exception to the "do not rewrite shared history" rule of `AGENTS.md`, recorded there
with this date.

## What was rewritten

- Content: in every commit where they appeared, the question passages of journals 0005, 0007,
  0010, 0011, 0015 and 0017 were replaced by the decision form; the commit attribution section
  of journal 0012 lost the personal e-mail address; the old `AGENTS.md` rule was replaced by
  the current one. Nothing else changed in any commit.
- Authorship: the 16 commits with the personal e-mail address and the merge commit with an
  accented name now use `Claudio <claudio@studiomoob.com>`, the identity of the `claudiofp`
  account.
- Tool: `git filter-repo` with `--mailmap` and a `--blob-callback`; run in an isolated clone;
  verification by `git grep` over all commits that no occurrence of the e-mail address, the
  account or the question passages remained.

## Hash map

The hashes cited in the documents were updated to the new values in this same commit.
Mapping of the most cited ones (old → new): 653c5b9 → 9afc275, aa018cc → 607c23e,
e7f31c7 → 8ab6ecd, 2500d59 → 1a83bf8, 42e3901 → 0fd38a5, c48cc9f → 9a37b88,
ae16320 → b2fb037, 7b6abbe → b71ae05, e949355 → 1699791, 02e5e53 → c5db4b0. CI runs from
before the rewrite remain on GitHub linked to the old hashes; the evidence in the journals does
not depend on them, since each journal carries commands and output summaries.

## Repository scan

Done before the rewrite, over files and over all blobs in the history: no secrets, tokens or
keys; no local machine paths; no versioned build artifacts or drafts; no blobs above 400 KiB;
the only personal data was the e-mail address and the account cited above.

## Publication

Sequence executed by the user (the assistant prepared it in `build/publish-rewrite.sh`):
temporarily remove the protection of `main`, force-push the rewritten clone, recreate the
protection with the configuration saved in `build/protection-before.json`, and realign the
local checkout.

## Update (2026-10-12)

The branch `docs/llm-memory-context` still carried the seven commits from before the rewrite, with
the personal address in their author and committer fields; it was the only remote branch with no
common ancestor with `main`. With the maintainer's authorization it was deleted, after a local
`git bundle` copy was made outside the repository. The read-only ref `refs/pull/1/head` of the merged
pull request 1 still points to those commits and cannot be removed by the owner, so the maintainer
asked GitHub Support to remove the commits and their cached views. No other pull request ref
carries them (all `refs/pull/*/head` checked).

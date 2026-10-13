# 0090 — `moobfs-check` lists the files left out of `QUERY2` answers

Date: 2026-10-13. Status: implemented and tested in the VM suite on the three lower
filesystems. Local series (after pull request 62). No change to the module.

## Goal

Finding 2 of journal 0089: a file that fails the homogeneity rule is absent from answers with
no error anywhere, and a careful client produced that state on its first run. spec-query-v2 §6
said `moobfs-check` "lists heterogeneous files per domain"; the tool did not.

## Change

`moobfs-check --domain-depth N <directory>` adds a `left_out` array and a `left_out_count` to
its JSON. An entry is a file or directory that carries typed attributes and that `QUERY2` would
not return, with one reason:

- `above every domain directory`;
- `has more than one hard link`;
- `directory <path>: more private than the domain directory`, or the same without the directory
  for the file itself;
- `owner or group differs from the domain directory`;
- `has an access ACL`, `the domain directory has an access ACL`.

The rule is the one `tools/moobfs-index.c` evaluates as the oracle, copied with the reasons
added. The depth is an argument because the tool reads the lower tree and cannot know the mount
option. Entries are not problems: the exit status and `problem_count` do not change, since a
file shared on purpose is a legitimate state. Without the option the output is as before.

Decision left open: a way for a client confined to its domain to learn the same thing. That is
an interface change (a per-domain count, or a flag on a query reply) and needs an ADR; the tool
covers the administrator, which is who can fix directory modes anyway.

## Evidence

- `make vm-test`: a tree with one queryable file, one under a `0700` directory in a `0755`
  domain, one with two names, and one above the domain gives `left_out_count` 4 with the three
  expected reasons, does not list the queryable file, and exits 0; the same tree without the
  option prints no `left_out`. On ext4, btrfs and XFS. Every marker through `MOOBFS_LAYER_PASS`.
- By hand on the benchmark host, same result.
- Not checked against the kernel's own decision file by file: the oracle and this tool share the
  rule by copy, and the oracle is what the suite compares the kernel with.

## Limitations

- The two copies of the rule (oracle and check) can drift; a shared source file would prevent
  it and was not done.
- A domain that fell back to per-result checks returns such files to callers who may read them;
  the tool reports what the homogeneous path would do.

## Next step

The M4 measurement of question 2 when the host is free; the rewritten message to the YoloFS
author after checking the three points in a VM.

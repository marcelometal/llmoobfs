# Specification: per-domain `QUERY` (UAPI v2)

Status: **accepted specification**, 2026-10-11, following from ADR-0009 (alternative E, accepted). The four
points marked **[accepted decision]** were accepted by the maintainer on the same day, in the recommended forms:
domains by `domain_depth`, extended ACLs outside the partition in this version, v1 restricted to
`CAP_SYS_ADMIN`, and `EXDEV` for directories renamed across domains. Implementation in phases (§8).

## 1. Goal

Answer attribute queries within a permission domain without traversing entries of other
domains, without checking the path of each result and without a database engine. Closes the
timing channel of journal 0030, removes the per-result cost of journal 0028 and bounds the cost
per candidate by the size of the domain (journal 0029).

## 2. Domains

- **Definition.** A domain is a directory at a fixed depth below the root of the mount,
  given by the mount option `domain_depth=N` (default 1: each first-level directory is a
  domain; 0 turns domains off and keeps only the v1 behavior). **[accepted decision]** Alternative
  considered: marking domains with a reserved attribute; discarded because any directory owner
  could create or undo domains without the administrator.
- **Partition.** The in-RAM index now has one partition per domain (tree of attribute names
  and, per name, tree of entries ordered by type, value and inode), plus one partition for what
  lies outside any domain (above depth N). The inode-to-record map
  stays global and stores the domain of each inode.
- **Budget.** The `index_budget` still applies to the whole mount; `GET_INFO` now
  reports the number of domains. (Revision of 2026-10-12, journal 0061: the counters of `GET_INFO`, `heterogeneous` and
  `fallback_domains` included, are filled only for `CAP_SYS_ADMIN`; ADR-0011 finding F1.)

## 3. Homogeneity rule

A file only enters the partition of its domain if every process able to traverse the domain
directory can also read it. Since the kernel does not enumerate processes, the rule is checked by
sufficient conditions, evaluated over the inode of the file `f` and that of the domain directory `D`:

1. `f` grants read to "others" (`o+r`) and has no access ACL; **or**
2. `f` and `D` have the same owner and the same group, neither has an access ACL beyond the minimal one,
   and, for each class (owner, group, others), if `D` grants execute to that class, `f` grants
   read to it.

Files with an extended access ACL stay outside the partition in this version, except through condition 1.
**[accepted decision]** Handling ACLs would require comparing named entries; it is left for a revision with its own
test.

A file that does not satisfy the rule is **heterogeneous**: it does not appear in v2 queries, is counted in
`GET_INFO` (`heterogeneous`) and listed by `moobfs-check`. The rule is re-evaluated in the same call
that changes what it uses: `setattr` (mode, owner, group), `set_acl` and creation, under the write lock
of the index. A change of mode, owner, group or ACL of `D` itself marks the partition as "to
re-evaluate"; the next query to that partition re-evaluates all entries before answering
(cost linear in the size of the domain, paid once per change).

**Q2a implementation (2026-10-11, journal 0035), differences from the text above.** The
proposal remains recorded as above; the implemented behavior is this:

- A change of mode, owner, group or ACL of a **directory** at the domain depth or below
  it (`D` itself or an intermediate one) does not re-evaluate the partition: the domain starts answering
  with per-result permission checking (like v1) until the next mount, and `GET_INFO`
  counts these domains in `fallback_domains`. Reason: re-evaluating would require traversing the subtree
  under the index write lock; the fallback mode is correct, simpler and rare in normal
  use (agent directories do not change permission on every write).
- Intermediate directories between `D` and the file enter the rule through the execute bit: each one
  must let through every process that traverses `D` (condition 1 with `o+x`, or condition 2
  with execute in place of read).
- A regular file with more than one name (`nlink > 1`) is heterogeneous, because another name may be
  in another domain. When it goes back to one name, it may stay heterogeneous until the next change of
  mode, owner, group, ACL or rename, or until the next mount (fail-safe: it stays out,
  never appears in excess).
- With `domain_depth=0`, v1 keeps the previous semantics and remains open to everyone; the restriction
  to `CAP_SYS_ADMIN` (§4.5) applies when domains are on.
- `MOOB_IOC_QUERY2` with the index off (`noindex`) returns `ENOSYS`, like v1. (Revision of 2026-10-12, journal 0050: both versions now return `EOPNOTSUPP`.) The fields
  `order_by`, `order`, `cursor`, `flags`, padding, reserved fields and the `MISSING_OK` flag must
  be zero in this phase (`EINVAL`); they come in phases Q2b and Q2d.

## 4. Interface (UAPI v2)

### 4.1 `MOOB_IOC_QUERY2`

Issued on an open descriptor of the **domain directory** (not the root). Any other descriptor
returns `ENOTTY`; domains off (`domain_depth=0`), `EOPNOTSUPP`.

New fields relative to v1:

- `version = 2`.
- Up to 4 terms with the v1 operators, plus the per-term flag `MOOB_TERM_MISSING_OK`: the term
  also matches entries that do not have the attribute (validity and expiry of the "missing or greater
  than" kind). At least one term must be without that flag, except when there is `order_by`.
- Optional `order_by`: name of an attribute of type `int64`, `time`, `double` or `string`, and
  `order` (`ASC` or `DESC`). Without `order_by`, the order is that of the scan (unspecified).
- `limit` from 1 to 4096 and an opaque 128-byte `cursor`: the response returns in `cursor` the next
  position when `truncated = 1`; resending the same query with that `cursor` continues from there. A
  cursor from another query, from another mount or after re-evaluation of the partition returns `ESTALE`.
- Results: `ino`, `flags` and path **relative to the domain directory**.

**Q2b implementation (2026-10-11, journal 0036), additions to the text above.**

- The ordering type is explicit: field `order_type` (`string`, `int64`, `double` or `time`),
  taken from the structure's padding, without changing its size. An attribute may have entries of
  different types; only those of the requested type enter the ordering. This makes "wrong type" a
  verifiable input error (`EINVAL`), while `order_by` with a valid name that no file in the
  domain has returns an empty response, not an error: the absence of an attribute is not a
  malformed request.
- Ties in value are broken by inode number, in the same direction as the order.
- The cursor is the key (type, value, inode) of the last delivered result, plus a digest of the
  query (domain, fallback state, ordering and terms; the `limit` may change between
  pages) and an authentication code, both with a random per-mount key. Corrupted,
  from another query, from another mount or issued before the domain went into fallback:
  `ESTALE`. Values of up to 96 bytes go in the cursor; larger ones are recovered from the current
  entry of the inode, which must still have the same type, size and prefix, otherwise `ESTALE`.
- Pagination is keyset-based, as in databases: entries created, removed or with a changed value
  between pages may appear twice or not appear; those that did not change appear
  exactly once.
- Without `order_by`, the cursor also works, over the scan order of the first term.

### 4.2 Authorization

On receiving the call, the kernel checks `MAY_EXEC | MAY_READ` on the domain directory with the
caller's credentials (the ancestors were checked when the descriptor was opened). If
it passes, the results come from the partition without per-result checking: the homogeneity rule
guarantees that whoever traverses the domain can read each file indexed in it. Timing contract
(TOCTOU): the same as v1; a concurrent permission change takes effect from the next call.

### 4.3 `MOOB_IOC_QUERY2_BATCH`

Up to 8 v2 queries over the same domain in one call, with concatenated results and one count
per query. Union and duplicate removal are left to the client. Covers "or" across values
(tags, types) with one call.

**Q2d implementation (2026-10-11, journal 0038), differences and additions.**

- The batch carries a vector of complete `moob_query2` requests, each with its own results
  vector, `limit`, `count`, `truncated` and `cursor`, instead of a single vector of concatenated
  results. A client that wants contiguous results points the requests at slices of
  one same vector. Reason: each request stays pageable by its own cursor, without a rule for
  splitting the limit among requests.
- All requests are validated before any execution; an invalid request refuses the whole
  batch with `EINVAL` and nothing is written back. The requests run in order, each under its
  own acquisition of the index lock: the batch is not a single snapshot.
- `MOOB_TERM_MISSING_OK` lives in the term's flags byte (formerly `reserved[0]`, renamed
  `flags`). v1 refuses any flag (`EINVAL`). `EXISTS` with the flag is refused ("exists or
  missing" would match everything). An attribute present with another type, invalid or raw (`raw`) counts
  as present or missing according to the index: a different type is present and does not match; invalid and
  raw are not indexed and count as missing.
- In the simple scan, the kernel drives by the first term without the flag (on a copy of the terms;
  the cursor digest uses the request order). The intersection uses only `EQ` terms without the flag.

### 4.4 Execution plan in the kernel, without a planner

- With `order_by`: walks the tree of the ordering attribute in the partition, in the requested order, and
  tests the terms on each entry until the `limit` is filled.
- Without `order_by` and with two or more equality terms without `MISSING_OK`: intersection by
  leapfrog over the runs of those terms, which are already ordered by inode within
  each value; the remaining terms are tested on each inode of the intersection.
- In the other cases: scan of the first term without `MISSING_OK`, as in the corrected v1.

**Q2c implementation (2026-10-11, journal 0037).** The intersection applies to `QUERY2` without
`order_by` with two or more `EQ` terms; v1 keeps the scan of the first term, to
serve as a comparison baseline. The first `EQ` term, in request order, drives: the results
come out in inode order and the cursor stores the value of that term with the last inode. Each step is
a logarithmic search in the tree of each term, until all reach the same inode; the remaining
terms (including the `EQ` ones, for simplicity) are tested only on the inodes of the intersection.

**Revision of the equality plan (2026-10-12, journal 0044).** Measured on the host of journal 0042,
the leapfrog only won when the run of the first term was long and was 2.7 to 3.6 times
slower than the simple scan when it was short. The plan became: measure the
runs of the `EQ` terms in parallel until the first one ends and scan only that one, testing all
the terms. Resuming this plan uses only the inode. The original proposal of §4.4 remains recorded
above.

### 4.5 v1

**[accepted decision]** Proposal: `MOOB_IOC_QUERY` (v1) on the root continues to exist, with the current
semantics, but only for those with `CAP_SYS_ADMIN` in the initial user namespace (administration
tools and comparison with the oracle). For everyone else it returns `EPERM`, because it traverses all
domains and keeps the timing channel. Alternative: keep v1 open and document the channel.

## 5. Rename across domains

A file renamed into another domain: its entries change partition in the same call, and the homogeneity
rule is re-evaluated at the destination. A **directory** renamed into another domain: returns
`EXDEV`, as across filesystems; `mv` then copies and deletes. **[accepted decision]** Avoids moving
whole subtrees between partitions inside a `rename`.

## 6. Tools

- `moobfs-index --domain DIR`: oracle that evaluates the same homogeneity rule and answers only
  about the domain, for comparison.
- `moobfs-check --domain-depth N`: lists the files left out of `QUERY2` answers, with the reason
  (implemented in journal 0090; until then the tool did not do it).
- `bench`: W3 shape per domain and the timing-channel experiment of journal 0030 as a test.

## 7. Required tests

1. Invalid inputs: version, terms, `MISSING_OK` flag alone without `order_by`, `order_by` on a
   nonexistent attribute or of the wrong type, `cursor` corrupted, from another query and after
   re-evaluation (`ESTALE`), descriptor that is not a domain (`ENOTTY`), `domain_depth=0`
   (`EOPNOTSUPP`), batch with more than 8 queries.
2. Equivalence with the per-domain oracle, in all W3 shapes, on the three lower filesystems.
3. Homogeneity: a `chmod` that makes a file more restricted removes it from the results on the next
   call; reverting brings it back; an extended ACL excludes it unless readable by everyone; a change in the
   mode of the domain directory triggers re-evaluation.
4. Timing channel: the latency of a query in a domain with 10 files does not vary (within noise)
   when another domain goes from 0 to 20 000 files that match the predicate.
5. Isolation: a user without access to the domain directory cannot open it or query.
6. Rename: a file across domains changes partition; a directory across domains returns `EXDEV`.
7. Ordering and cursor: "N most recent" equal to the ordered oracle; full pagination by cursor
   returns the same set as the query without a limit.
8. Concurrency: the race test of journal 0020 applied to v2, including `chmod` on the domain
   directory during queries.
9. KASAN and lockdep without diagnostics; v1 restricted as in 4.5.

## 8. Phases

| Phase | Content | Done-when criterion |
| --- | --- | --- |
| Q2a | Per-domain partitions, homogeneity rule, `MOOB_IOC_QUERY2` without `order_by`, v1 restriction | Tests 1, 2, 3, 4, 5, 9 |
| Q2b | `order_by`, `cursor` | Test 7 |
| Q2c | Leapfrog intersection | Equivalence and measurement against Q2a in the `namespace AND agent` shape |
| Q2d | Batch and `MISSING_OK` | Tests 1 and 2 for the new fields |
| Q2e | Rename across domains | Test 6 and 8 |

Each phase comes in through its own pull request, with a journal in `docs/history/`, the VM suite and
KASAN, and a measurement on the Azure host compared with journal 0029.

**Status note (2026-10-12, journal 0052).** Test 6 (renames across domains: `EXDEV` for directories,
repartition for files) landed with Q2a. Q2e therefore adds test 8: the `QUERY2_RACE_PASS` block of the
suite races mode changes, renames, unlink and recreate, files moving to another domain and back,
and `chmod` of the domain directory against paged `QUERY2` over the plain, equality and ordered
plans. Q2c and Q2d went in one combined pull request by the maintainer's decision, and the
measurements moved from the Azure host to the Mac mini host (journal 0042).

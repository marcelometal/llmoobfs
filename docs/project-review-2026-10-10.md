# Project review — 2026-10-10

Base examined: `36eb64b023bf2a3fdeaf99e0cf070843d036e4e1`, branch
`m3/3b-query-authz`. Review of architecture, implementation, tests, plans and value
proposition. The recommendations below are proposals, not new architectural decisions.
The unversioned file `docs/community-plan.md` was preserved.

## Assessment

The project has already gone beyond the shared-read proof. There is a mountable native
layer, delegated POSIX writes, typed attributes, an index and structured query,
with a substantial testing infrastructure. ADR-0007 is the most important decision:
reusing a mature filesystem concentrates the effort on the memory interface.

It is still a research prototype. The base does not demonstrate a performance advantage, nor
an improvement in the quality of an agent's memory. There are query consistency defects
identified by reading and identity/permission configurations that need specific
reproduction before broadening use.

Recommendation: close correctness and the QUERY contract, run a small M4 and produce
a demonstration of memory across sessions. Do not start a new disk format,
vector search in the kernel or several integrations simultaneously.

## What exists and what is still a plan

| Area | Current evidence | Limit |
| --- | --- | --- |
| Open source base | GPL-2.0-only license, bilingual README, site, workflows, guides, ADRs, history and instructions for agents | The status documents already diverge from each other |
| M1 | Offline image, fsck, native read-only module, two inodes with shared physical content | Frozen experiment; it is not the current backend of the layer |
| M2 | `layer/main.c`: filesystem stacked over a lower directory; POSIX operations and delegation of I/O/reflink | Does not make ext4 reflink-capable nor offer universal automatic deduplication |
| Attributes | Validator shared between kernel/tools, `user.moob.*`, types and limits | An update of content and several attributes does not form a single transaction |
| Index | RAM, trees per attribute name and records per inode; rebuild at mount | Mount cost, budget and consistency under failures still require work |
| QUERY | Conjunctive predicates, UAPI v1, comparison with oracle | Structured search, without embeddings or semantic ranking |
| M3 | Identity/provenance conventions, checker; POSIX authorization implemented in this branch | Do not confuse branch with release; batch and change cursor remain postponed proposals |
| Linux validation | Journals of ext4/Btrfs/XFS, fsx, git, SQLite WAL, own client, dm-log-writes/flakey and concurrency | Broad coverage for a prototype, without amounting to POSIX certification, a security audit or all of fstests |
| M4 | Plan and harness design | No benchmark results presented in this review |
| LLM memory | Files and metadata usable by a client | Selection, ranking, knowledge update and context assembly belong to the client |

Current flow: client → VFS/QUERY → MoobFS layer → lower filesystem → disk.
Content and xattrs persist in the lower filesystem; the index is a derived structure.
This separation is good for recovery and inspection, but requires making explicit any
incomplete response when the index fails.

## Priority code findings

The first three findings follow directly from the code flow. They were not
reproduced in a VM in this review. P1 means fix/investigate before treating
the feature as reliable; P2 means fix in the next cycle.

### R1 — P1: changing an indexed attribute to RAW keeps the previous value

In `layer/index.c:238`, `index_put()` returns for `MOOB_T_RAW` before locating
and removing the previous entry. `moobl_xattr_set()` writes the new xattr and calls
`moobl_index_set()` without prior removal (`layer/main.c:655`).

Expected consequence: after turning `kind=string:note` into RAW, a query
for `kind=note` keeps finding the file until the index is rebuilt. Disk and
index start to disagree without necessarily marking the index as incomplete.

Suggested fix: remove the previous entry also on the transition to RAW,
preserving the rule of not indexing the new payload. Test string→RAW, RAW→string,
EXISTS, removal and equality with the oracle before/after remount.

### R2 — P1: path truncation before authorization causes false negatives

`fill_result()` (`layer/index.c:735`) limits the path to 255 bytes and flags
truncation. `do_query()` copies that path to the candidate (`:891`) and then
`caller_may_read()` tries to resolve it. The truncation indicator is not preserved.

A valid relative path longer than 255 bytes normally stops resolving,
and the query silently omits a readable file. The limit of the UAPI
representation is altering the access decision.

Suggested fix: authorize using the full path and define separately
how the UAPI represents long results. While there is no adequate
representation, returning an explicit error/documented incompleteness state is preferable
to a seemingly complete response. Test paths near and above the limit.

### R3 — P2: operational errors can turn into absence of results

`caller_may_read()` (`layer/index.c:798`) returns a boolean and converts any error
from `vfs_path_lookup()` into `false`; errors from `inode_permission()` are also
indistinct. This includes failures other than access denied or object removed.
It contradicts the contract that promises to propagate operational failures.

Suggested fix: a return with error/allowed/omitted and explicit classification of
errno. Inject failures and check that an operational failure does not produce success with
zero results. EACCES/ENOENT during concurrency require their own handling.

### R4 — P1, hypothesis to reproduce: inode identity across Btrfs subvolumes

The index uses only `ino` as the xarray key (`layer/index.c:172`); the upper inode
copies `lower->i_ino` (`layer/main.c:98`). The scan walks directories and contains no
explicit subvolume barrier. Candidate validation compares the
inode number again (`layer/index.c:808`).

Btrfs has an inode number space per subvolume. Therefore, distinct inodes
in different subvolumes can collide in this representation. The official documentation
recommends the subvolume/inode pair for identity in the filesystem. This supports the risk;
it does not prove, on its own, the exact manifestation or a leak in MoobFS.

Priority test: two nested subvolumes, numerically equal inodes, different attributes
and permissions; compare result, opened content and oracle. Until there is
adequate internal identity, consider refusing to cross subvolumes. The solution must
also handle the identity exposed by the layer, not just add a field to the
tree. [Btrfs source](https://btrfs.readthedocs.io/en/latest/Subvolumes.html).

### R5 — P1, configuration to reproduce: lower filesystem with remapped IDs

`moobl_fill_super()` resolves the lower filesystem (`layer/main.c:1107`) without explicitly refusing
an idmapped mount; delegated operations and permissions use `nop_mnt_idmap`.
Refusing idmap on the upper mount does not automatically cover selecting an
already remapped lower filesystem.

Test: mount an idmapped lower filesystem and use it in `lower=`, checking UID/GID, creation,
ACL and QUERY. For the first experimental release, refusing the configuration may be
simpler than supporting it. No exploitation was demonstrated in this review.

### R6 — P2: the batch limits candidate results, not the scan work

In `layer/index.c:816`, the initial search starts at `rb_first()`. The loop from
`:881` walks entries until it gathers candidates that satisfy all terms.
A very selective query can visit the whole tree under the read lock.
Having a balanced tree does not make that query automatically logarithmic.

Measure the number of entries examined, lock duration and writer latency.
Then evaluate positioning at the start of the searched range, stopping at the end,
choice of the first term and batches bounded by entries examined. This limitation
of work is relevant even before fine performance optimizations.

### R7 — P2: checker may convey undue confidence

`tools/moobfs-check.c:48` ignores `FTW_NS` and `FTW_DNR`; an uninspected directory
may not show up as a problem. The attribute name is interpolated without escaping into the
JSON (`:36`), although invalid names coming from the lower filesystem are precisely
one of the cases the tool must report.

Suggestion: distinguish a clean tree from an incomplete inspection, signal an error in the output and
escape all strings. Test an inaccessible directory and an xattr with quotes in the name.

## Other robustness points

- The index may become incomplete after a successful write to the lower filesystem. The client
  needs to observe that state; responses must not inadvertently look complete.
- `index_used` is not the real RAM: allocator, xarray and transient buffers need
  measurement. The temporary list of names of a directory deserves a separate limit test.
- The `--uid` tests use setgid/setuid without clearing supplementary groups. Preparing the
  complete identity prevents the tests from inheriting unintended access through a group.
- The QUERY/open race admits ENOENT/EACCES by contract. That test is useful for
  stability, but it is not enough to prove absence of improper results: add
  controlled revocation phases, with barriers and known expected results.
- Specifically review readonly on the lower filesystem and filesystem freezing in the
  delegated metadata operations. It is a pending review area, not a bug
  confirmed here.
- Reflink shares data; do not assume that the FICLONE ioctl transfers xattrs. The
  client contract must say how to copy attributes and when to generate a new identity.
- Git versions the project files, but does not automatically preserve the xattrs of the
  memories. Propose export/import of content + metadata and a round-trip test.
- `(namespace, memory_id)` is logical identity; `namespace` as an attribute is not a
  security barrier. Isolation depends on POSIX credentials and permissions.

## Plans and communication

The README announces only phase 2a in its title and describes 2a–2e in the body. ROADMAP still
lists KASAN as pending generically, although the history records a pass on
main; the current branch has another run. The opening of the M3 specification says nothing
was implemented, although partial code already exists. The roadmap still cites a mandatory
external client and batch/cursor as if the scope had not been refined.

Do not erase the history: add dated notes and keep a short table of
current status with commit, evidence and pending item. ADRs describe the decision at the moment
it was taken; they need a link to the current status, not retrospective rewriting.

The most specific potential benefit is: files and attributes queryable by
several processes, with POSIX authorization and persistence in an ordinary filesystem. The
absence of a database daemon does not distinguish the project from SQLite, which also runs
in the application's process. [SQLite documentation](https://www.sqlite.org/serverless.html).

Being in the kernel does not prove lower end-to-end latency. VFS traversal,
checking each path, rebuilding and maintaining the index may dominate the load.
The cost of privileged installation and compatibility with kernels also enters the
adoption evaluation. Keeping Linux 6.12 as the explicit initial target and expanding the matrix
after stabilizing the contract is more defensible than promising broad Linux support.

Two concrete adjustments to the benchmark design: `docs/benchmark-harness.md:59`
declares `memory_id TEXT UNIQUE`, while the proposed identity is the pair
`(namespace, memory_id)`; align the contracts before generating the dataset. The use of
SQLite with `synchronous=NORMAL` (`:67`) also requires making explicit which recovery
after power loss is expected and making the synchronization policy of the compared
systems equivalent. Do not interpret different durability configurations
as an intrinsic performance advantage.

## Recommended path

1. **QUERY correctness:** regressions R1–R3; reproductions R4/R5; incomplete index
   state; test identity. Complete the branch's KASAN and update the status.
2. **Minimal M4:** compare plain lower filesystem, layer with `noindex`, indexed layer and
   files + SQLite with adequate indexes. Start with reading a known path,
   selective query, concurrent update and mount/rebuild.
3. **Same semantics:** same content and attributes, equivalent authorization,
   equivalent durability and results checked before timing. Compare
   logical identity, not physical inode numbers across backends. Measure p50/p95/p99,
   CPU, real RAM, mount time and entries examined. Use native Linux/KVM;
   do not use TCG emulation as performance evidence for Linux users.
4. **Demonstration of memory across sessions:** agent A records a decision;
   authorized agent B retrieves it after restart; a new version replaces a piece of information;
   another UID does not receive it; export/import keeps identity and provenance.
   The small client may have MoobFS/SQLite adapters for the experiment without changing
   the decision that the product is a native filesystem.
5. **Only then broaden:** agent integration, packaging and kernel matrix.
   Postpone batch, revision cursor, persistent index and ranking until measurements
   identify the concrete need. A change cursor needs to define
   persistence and restart; a volatile counter does not solve the problem on its own.

Define criteria before the benchmark: mandatory correctness; acceptable limits of
overhead, memory and mount time; a relevant load with measurable benefit.
It is not necessary to beat SQLite at everything, but it is necessary to justify the cost of the layer.

## Papers and implications for the project

The applications below are architecture inferences, not experimental results
about MoobFS. The pages and abstracts of the papers were consulted; the experiments
were not reproduced and no exhaustive literature review was made.

- [MemGPT](https://arxiv.org/abs/2310.08560): treats context management through memory
  tiers. Reinforces the separation between persistent storage and the policy that
  decides what enters the context window. That policy remains in the client.
- [LongMemEval](https://arxiv.org/abs/2410.10813): evaluates extraction, cross-session
  reasoning, time, knowledge update and abstention. It can evaluate the memory flow
  after the storage benchmark. Keep model, prompt and retrieval strategy
  the same when comparing backends, so as not to attribute agent gains to the FS.
- [A-MEM](https://arxiv.org/abs/2502.12110): explores organization and relations between
  memories. Suggests value in provenance and explicit relations; does not imply implementing
  a semantic graph or LLM execution in the kernel.
- [OverlayFS permission model](https://www.kernel.org/doc/html/latest/filesystems/overlayfs.html):
  reference for distinguishing the credentials of the caller and of access to the lower filesystem.
  Current documentation does not replace checking the APIs in the kernel 6.12 used by the project.

## Evidence of this review

- Ran `make test BUILD=build/review-20261010`: 60 encoding checks and
  9 Python image tests passed. This does not run the module or QUERY on Linux.
- Consulted code, specifications, ADRs and validation journals; the Linux tests
  cited above are evidence recorded by the project, not re-runs in this session.
- History 0016 records `MOOBFS_KASAN_PASS` on main in run
  [38074631812](https://github.com/studiomoob/llmoobfs/actions/runs/38074631812).
- The branch's KASAN run
  [38079499011](https://github.com/studiomoob/llmoobfs/actions/runs/38079499011)
  was `in_progress`, without conclusion, at the check made in this review.
- No module was loaded on the macOS host; no new VM run was made.
  The proposed reproductions still need to be implemented/run.
- No code fix, site change or remote publication is part of this
  deliverable. The findings do not amount to a complete security audit of the kernel.

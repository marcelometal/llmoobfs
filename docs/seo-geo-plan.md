# MoobFS SEO/GEO audit and plan (proposal)

Date: 2026-10-10. Status: **proposal; nothing has been implemented.** This document does not change
any other file. Basis: reading of `README.md`, `README.pt-BR.md`, `ROADMAP.md`,
ADRs 0005-0007, `docs/spec-m2.md`, `docs/spec-m3.md`, `docs/benchmark-plan.md`,
`docs/research-originality.md` (§4, 5, 7, 9), `docs/value-assessment.md` ("Deliverable" and §9),
`docs/distribution-strategy.md` §8, `docs/community-plan.md` (§8, 9, 10, 13),
`docs/history/README.md`, `site/*.html`, `CHANGELOG.md` and `LICENSE`, on branch
`community/readiness`. README, ROADMAP, CONTRIBUTING and `.github/` were being edited by another
agent; they were read only as they stood and may change. GitHub metadata comes from the task
statement (query of 2026-10-10) and was not re-verified.

Principle: SEO here means that whoever searches for the subject finds a **correct description,
without exaggeration**. GEO (AI assistant answers) is a consequence of clear, factual and
citable text; no file or metadata guarantees citation. All public sentences obey the list
of claims to avoid in `docs/research-originality.md` §7.

---

## 1. Diagnosis of the current state

Priority: A = before any outreach; M = right after; B = when there is capacity.

| Item | Status | Problem | Priority |
| --- | --- | --- | --- |
| Repository name vs. project | Repo `llmoobfs`; project, README and site say "MoobFS" | Two spellings split searches and citations; `git clone` and badges use `llmoobfs`. Renaming breaks old clones, but GitHub keeps a redirect (behavior with Pages to be confirmed) | M (maintainer decision) |
| Repository description | "Experimental native Linux filesystem for persistent LLM memory. Read-only shared-content milestone; no FUSE." | Outdated: omits the stacked layer, typed attributes, `QUERY`; "read-only milestone" contradicts M2/M3 | A |
| Topics | 5: deduplication, filesystem, kernel-module, linux, llm-memory | Too few; `deduplication` misrepresents the current focus (dedup belongs to btrfs/XFS, see `research-originality.md` §7); missing xattr, vfs, ext4/btrfs/xfs, ai-agents | A |
| README structure | Opens with read-only M1; M2 appears in a dense paragraph; "What is implemented" is still the M1 table; no "what it is not" section, comparison or FAQ | A newcomer reads about a project that is no longer the current one; the approved experimental banner (community-plan §9) is not yet at the top (to be confirmed after the ongoing edit) | A |
| Internal links | README points to Portuguese `docs/*` without a language notice (some are flagged: "in Portuguese") | An English-speaking reader lands on PT pages without knowing; links from `site/` to GitHub use the `llmoobfs` path | M |
| License visibility | `LICENSE` GPL-2.0-only and badge in the README; GitHub detects the license from the file | Adequate; mention the license in About only if GitHub displays it (to be confirmed in the UI) | B |
| Releases and tags | No release, no tag (`git tag` empty) | No citable version; tools and assistants have no stable point to reference. But "no release" is honest and consistent with the stage | M (maintainer decision) |
| Changelog | `CHANGELOG.md` with a single, detailed "Unreleased — 2026-10-10" section | Useful and honest, but without dates per phase or links to journals; becomes noise without a first milestone | B |
| Contribution documentation | `CONTRIBUTING.md`, `GOVERNANCE.md`, `CODE_OF_CONDUCT.md`, `SECURITY.md` exist; `CONTRIBUTING.md` and `.github/` are being rewritten | Out of scope; this plan only depends on the README linking to them | — |
| Discussions | Disabled; wiki disabled | No public question channel; questions become issues. Disabled wiki is correct (it would duplicate `docs/`) | M (maintainer decision) |
| Maintenance/experimental status | "Experimental" text present in the title and in paragraphs; no limitations banner at the top; one author, one kernel | Missing the visible, uniform "not for production" statement and the limitations (community-plan §9) | A |
| Site metadata | `<title>` "MoobFS — A filesystem for LLM memory and context."; meta description still describes the read-only milestone; no `og:`, `canonical` or JSON-LD tags on the three pages; no `robots.txt` or `sitemap.xml` in `site/` | Title promises "a filesystem" without "experimental"; outdated description; shares without a preview | A (description and title), M (rest) |
| Site content | `index.html` still shows M1 as "Implemented today: read-only M1"; `guide.html` reproduces only M1; `plan.html` says M2 "is not implemented" | Contradicts `ROADMAP.md` and the README; it is the most indexable page and the most outdated | A |
| GitHub Pages | Active at `studiomoob.github.io/llmoobfs/`, serving `site/` | Exists and works; it is the asset to fix, not to create. Renaming the repo would change the URL | M |
| Language | README in EN, README.pt-BR and technical docs mostly PT | Almost all the technical content that would answer English searches is in Portuguese | M |
| Public evidence | `MOOBFS_*_PASS` markers in the README; CI; `docs/history/` journals | Good credibility asset; missing a single English page summarizing what was proven and what was not | M |

Strengths to preserve: explicit limitations language, separation of proposal/implemented/tested,
acknowledgment of prior art (SFS, BFS, YoloFS) and a clear license.

---

## 2. Optimized GitHub description

Field limit: 350 characters. Counts below are approximate (check when pasting).

**Recommended (~290 characters):**

> Experimental Linux kernel module (GPL-2.0, no FUSE): a stacked filesystem layer over ext4, btrfs or XFS that adds typed xattr attributes, an in-memory index and a QUERY ioctl authorized by the caller's POSIX credentials. Intended as storage for LLM-agent memory. Research project, not for production.

**Alternative A (~245 characters, shorter, focus on the mechanism):**

> Experimental stacked Linux filesystem layer (kernel module, no FUSE) over ext4/btrfs/XFS: typed extended attributes plus an in-memory index and a QUERY ioctl that honors POSIX permissions. Built to explore storage for LLM-agent memory. Not production-ready.

**Alternative B (~265 characters, focus on use by agents):**

> Research project: kernel-level metadata queries for AI-agent memory stored as ordinary files. A stacked Linux filesystem module over ext4, btrfs or XFS; data stays readable without the module. Experimental, tested on one kernel in a VM, no performance numbers yet.

Criteria: says "experimental"; names the real mechanism (`user.moob.*`, index, `QUERY`);
does not use "first", "fastest", "secure", "production"; "no FUSE" stays because it is a design
constraint (AGENTS.md). Alternative B reproduces the core limitations and is the most conservative.

Repository homepage: keep `https://studiomoob.github.io/llmoobfs/`.

Site title and description (proposal, `site/index.html`):

- `<title>`: `MoobFS — Experimental stacked Linux filesystem layer for LLM-agent memory`
  (~75 characters; above the ~60 ideal for SERP, acceptable; short alternative:
  `MoobFS — experimental Linux filesystem layer for agent memory`).
- `<meta name="description">`: `Experimental Linux kernel module stacked over ext4, btrfs or XFS: typed xattr attributes, an in-memory index and a permission-aware QUERY ioctl for LLM-agent memory. Research project, not for production.`

---

## 3. Recommended topics

GitHub maximum: 20. The "Search volume" column has no numbers (none was measured);
"speculative" means I do not know whether the term is used in relevant volume on GitHub.
Before applying, check each topic's repository count on the page
`github.com/topics/<name>` (to be confirmed), and prefer those that already have an active community.

| # | Topic | Justification (one line) | Volume/use |
| --- | --- | --- | --- |
| 1 | `linux-kernel` | Kernel module; the topic is conventional for kernel code | common use (count to be confirmed) |
| 2 | `kernel-module` | Already present; describes the `moobfs_layer.ko` artifact | common use |
| 3 | `filesystem` | Already present; main category | common use |
| 4 | `vfs` | The project implements Linux VFS operations | niche, but precise |
| 5 | `stacked-filesystem` | Exact technical term for the design (ADR-0007), like overlayfs/ecryptfs | speculative (probably few repositories) |
| 6 | `xattr` | Typed attributes live in xattrs | niche, precise |
| 7 | `extended-attributes` | Spelled-out form of the previous one, for those who do not know the abbreviation | speculative |
| 8 | `posix` | `QUERY` authorization follows POSIX permissions | common use |
| 9 | `ext4` | Validated lower | common use |
| 10 | `btrfs` | Validated lower | common use |
| 11 | `xfs` | Validated lower | common use |
| 12 | `ai-agents` | Declared target audience (agent memory) | common use |
| 13 | `llm` | Usage context | very broad use, low specificity |
| 14 | `agent-memory` | Problem-specific term | speculative (growing; no number) |
| 15 | `llm-memory` | Already present; keeps continuity | speculative |
| 16 | `metadata` | The central function is metadata querying | broad use, low specificity |
| 17 | `indexing` | In-memory attribute index | broad use (alternative to `index`) |
| 18 | `c` | Language of the module and the tools | very common use |
| 19 | `qemu` | VM tests are a central part of the evidence and attract kernel reviewers | niche |
| 20 | `gpl-2-0` | Signals a kernel-compatible license | speculative (GitHub already shows the license) |

Removed relative to the current set: `deduplication` (dedup belongs to btrfs/XFS; keeping it would
suggest the claim to avoid "MoobFS reflink/dedup"). Do not use: `memory` (ambiguous with RAM),
`rag`, `semantic-search`, `vector-database`, `ai-memory` (the project does no semantic search or
vectors; they would be misleading topics). Reserve 1-2 slots (for example, `gpl-2-0` or `indexing`)
to swap after looking at the topic pages.

---

## 4. Proposed README reorganization

Headings in English (language policy, community-plan §13). Order and role of each section:

| # | Heading | Purpose (one line) |
| --- | --- | --- |
| 1 | `# MoobFS` + subtitle + links + badges | Name, one-line sentence (the §2 description) and navigation (PT, site, contributing, roadmap) |
| 2 | `> Experimental research project. Not for production use.` (banner) | **Exact text approved in community-plan §9**, with the seven limitations; check each line against the repository before publishing ("three defects" to be confirmed against journals 0011-0015) |
| 3 | `## At a glance` | Five lines: stacked layer, typed attributes, in-RAM index, `QUERY` with POSIX authorization, data readable without the module; one status block (tested lowers and kernel) |
| 4 | `## What MoobFS is, and what it is not` | Two short lists; "is not": semantic search, vectors, OR/free text, replacement for SQLite/git/ai-memory, FUSE, production, owner of dedup/reflink |
| 5 | `## How it works` | Write and query flow in one diagram (reuse `site/plan.html`; create a text/Mermaid version in the repository) with a link to `docs/architecture-overview.md` |
| 6 | `## What is implemented, with evidence` | Table capability / status (implemented, tested on which kernel and lower) / link to journal or test; replaces the M1 table; M1 becomes a regression footnote |
| 7 | `## Limitations` | Objective list: one kernel, index only in RAM, initial root, no userns/idmap, UAPI v1 may change, no measured performance, no external audit, TOCTOU and false negative after directory rename |
| 8 | `## Compared with plain files, SQLite and agent-memory frameworks` | Short table derived from `docs/value-assessment.md` ("Honest comparison") and the rule "today, SQLite or files are the recommendation"; frameworks treated as clients |
| 9 | `## Quick start (disposable VM)` | `make vm-test` and expected markers; warning not to load on a host; link to `docs/testing.md` |
| 10 | `## Documentation map` | Table: reader's question -> document -> language (EN/PT) |
| 11 | `## Prior work and positioning` | Acknowledgment of SFS, BFS, overlayfs, ecryptfs, YoloFS (text from `research-originality.md` §9, English version) |
| 12 | `## Contributing and security` | Link to CONTRIBUTING, review requests (community-plan §8), SECURITY (contact and Advisories) |
| 13 | `## License` | GPL-2.0-only, link to `LICENSE` |

Rules: the README stays in English; `README.pt-BR.md` mirrors the structure (this proposal does not
edit it). Remove the extensive M1 content from the README and move it to `docs/` or `site/guide.html`
(already exists). Do not use "first", "fastest", "most secure". Limit the README to ~200 lines.

---

## 5. SEO/GEO documentation structure

### 5.1 Target queries and what already answers them

| Target query | Existing document that answers it | Language | Gap |
| --- | --- | --- | --- |
| "Linux filesystem for AI agents" | `README.md` (partial, outdated), `docs/spec-m2.md`, `ADR-0007` | EN partial / PT | No English page explaining the current architecture |
| "persistent memory for AI agents" | `README.md` goal; `docs/research-ai-memory.md` | EN / PT | README section uses M1 language; missing positioning against mem0/Letta/ai-memory in English |
| "kernel-level metadata indexing" | `docs/spec-m2.md`, `ADR-0005`, `layer/index.c` | PT | No English text |
| "POSIX filesystem for agent memory" | `docs/spec-m3.md` §3 and §3.1 | PT | No English text; TOCTOU contract only in PT |
| "stacked filesystem for LLM memory" | `ADR-0007` | PT | Same |
| "AI agent memory infrastructure" | `docs/value-assessment.md`, `docs/distribution-strategy.md` | PT | No English summary; comparisons only in PT |
| "structured metadata queries on Linux" | `docs/spec-m2.md` (QUERY), `docs/spec-m3.md` | PT | No short `QUERY` usage example in English |

Conclusion: the content exists and is good, but it is in Portuguese and scattered. The gap is one of
**English translation/synthesis**, not of page volume. No per-keyword pages.

### 5.2 New pages (at most three, in English)

| Page | Where | Source | Note |
| --- | --- | --- | --- |
| `docs/architecture-overview.md` | `docs/` (versioned with the code; the site can link to it) | ADR-0005/0007, spec-m2, spec-m3 §3 | Layers, write and query flow, `user.moob.*`, index, `QUERY`, authorization and TOCTOU contract, "nothing trapped". Answers queries 1, 3, 4, 5, 7 in a single page, with a title that does not repeat keywords |
| `docs/when-not-to-use.md` (title: "Comparison and when not to use MoobFS") | `docs/` | `value-assessment.md` "Deliverable" and §9 | Comparison table, cases where it may be worth it and cases where it is not, the sentence "today use SQLite or plain files"; answers queries 2 and 6 |
| `docs/faq.md` | `docs/`, with a rendered version at `site/faq.html` | Section 6 below | Short, citable questions; each answer cites the source file |

Rules: each claim cites the file; numbers and statuses are copied, not rewritten; the
"proposal / implemented / tested" label follows AGENTS.md; no page duplicates the README.
Translating the ADRs/specs in full is outside the plan (decision of community-plan §13: on
external demand).

### 5.3 GitHub Pages vs. a page on the StudioMoob domain

| Criterion | GitHub Pages (current) | Page on a StudioMoob domain |
| --- | --- | --- |
| Status | Exists, serves `site/`, free | Does not exist for the project (to be confirmed) |
| Cost | Zero; no hosting | Domain/hosting already paid by the company (to be confirmed); effort of maintaining two sources |
| Proximity to the code | Same repository; changes in the same PR | Duplication or an additional deploy |
| Domain authority | Shared `github.io` subdomain; low own authority | The company domain may have more authority (**to be confirmed**, not measured) and gives a stable URL independent of the repository name |
| Risk | Renaming the repo changes the URL | Dependency on another system; risk of diverging from the repository |
| Technical credibility | Natural for an open-source project | May reinforce the company brand, but looks like marketing if not strictly technical |

Recommendation (no immediate implementation): **keep and fix GitHub Pages**. Reassess an
own domain (for example a subdomain pointing to Pages, with `CNAME`) only after M4,
if the decision on the repository name creates a need for a stable URL. A `CNAME` avoids
duplicating content and preserves links. Do not create a second page with the same text.

---

## 6. Technical FAQ (English)

Answers in English for `docs/faq.md`; in brackets, the source. None contains "first",
"fastest" or "most secure".

**1. What is MoobFS?**
MoobFS is an experimental GPL-2.0 Linux kernel module, stacked over ext4, btrfs or XFS, that
passes POSIX operations to the lower filesystem and adds typed `user.moob.*` attributes, an
in-memory attribute index and a `QUERY` ioctl. It is a research project with no release.
[`README.md`; `docs/research-originality.md` §9]

**2. What problem does it target?**
It explores storing LLM-agent memories as ordinary files while letting a program ask which
files have given attribute values, with the operating system's permissions deciding what each
caller sees. The narrow hypothesis is attribute queries with per-result POSIX authorization,
no separate database server and no second authorization policy. That hypothesis is not yet
shown to cost less than SQLite plus application-level checks. [`docs/value-assessment.md`, "Grounded conclusion"]

**3. How does the stacking work?**
The module mounts with `mount -t moobfs_layer -o lower=<dir>` and delegates file I/O to the
lower directory through the kernel's backing-file helpers, in the same family of designs as
overlayfs and ecryptfs. Attributes are real xattrs on the lower filesystem, validated by the
layer on write. [`docs/decisions/0007-stacked-memory-layer.md`; `README.md`]

**4. What does `QUERY` do?**
`QUERY` is an ioctl on the mount root that returns files matching up to four attribute
predicates joined by AND (operators `=`, `<`, `<=`, `>`, `>=` and prefix) with a result limit.
Answers were checked against a user-space oracle on ext4, btrfs and XFS. [`docs/spec-m2.md`; `CHANGELOG.md` phase 2d]

**5. What does `QUERY` not do?**
It has no OR, no sorting or aggregation, no full-text search and no vector or semantic
search. The agent application selects relevant memories and builds the model's context; the
filesystem does not extend a context window. [`docs/value-assessment.md`; `docs/research-originality.md` §7]

**6. How is `QUERY` authorized?**
For each candidate, the kernel resolves the recorded path with the caller's credentials and
requires `inode_permission(MAY_READ)` on the resolved inode, so mode bits and ACLs delegated to
the lower filesystem apply per result. Unauthorized results are omitted silently, and
`count`/`truncated` refer only to visible results. This was tested with two uids on three
lower filesystems; no external audit exists. [`docs/spec-m3.md` §3.1; `docs/history/0020`]

**7. What is the TOCTOU contract?**
A result is not a grant of access. Authorization is checked when the query runs; permissions or
names may change afterwards, so a client must call `open()` normally and accept `EACCES` or
`ENOENT`. There is no guarantee of the absence of a time-of-check/time-of-use window in UAPI
v1. A known limitation is possible false negatives after a directory rename until the next
mount. [`docs/spec-m3.md` §3.1]

**8. What is implemented today?**
A stacked pass-through layer validated over ext4, btrfs and XFS with `fsx`, typed attributes,
the in-memory index, the `QUERY` and `GET_INFO` ioctls, delegated reflink and dedupe, and
acceptance runs with git, concurrent WAL-mode SQLite, `dm-log-writes` power-loss replay and
`dm-flakey` error injection. Phase 3b (authorization in `QUERY`) closed with a KASAN run.
[`README.md`; `ROADMAP.md`; `CHANGELOG.md`]

**9. What is not implemented?**
Persistent index (it lives in RAM and is rebuilt at mount), user-namespace and idmapped
mounts (rejected), batch reads, a change cursor, revisions, packaging, a stable UAPI, a CLI,
and the full xfstests suite. Performance is not measured. [`docs/spec-m3.md`; `docs/distribution-strategy.md` §2.9; `docs/research-originality.md` §9]

**10. How does it differ from SQLite next to plain files?**
SQLite offers full SQL, composite indexes and decades of maturity; MoobFS offers a single
store of files plus xattrs and authorization by the OS per result. Writes to SQLite and to the
files are two operations; MoobFS writes a file and an xattr, but file/metadata atomicity is
the client's convention. Today SQLite or plain files are the recommendation for anything
outside a lab. [`docs/value-assessment.md` "Honest comparison"]

**11. How does it differ from plain files and from FUSE-based systems?**
Plain files need `find` plus `getfattr`, a linear scan, with no query call. FUSE systems run
filesystem logic in a user-space process; MoobFS is a native kernel module, which gives
different trade-offs (a kernel bug can crash the host). No speed comparison has been
measured. [`AGENTS.md`; `docs/value-assessment.md`; `docs/benchmark-plan.md`]

**12. Is it a competitor to mem0, Letta or ai-memory?**
No. They are memory frameworks and clients; MoobFS is a possible storage layer under them.
The M4 benchmark plan treats real clients (ai-memory, mem0, Letta) as workloads. Prior art in
agent filesystems such as YoloFS (2026) is acknowledged. [`docs/benchmark-plan.md`; `docs/research-originality.md` §3, §9]

**13. What is proven and what is still a hypothesis?**
Proven in the author's VM runs: answer correctness against the oracle, two-uid isolation on
three lower filesystems, git/SQLite acceptance, and a KASAN/lockdep run on kernel 6.12.112.
Hypotheses pending M4: any speed or efficiency advantage, behavior near the index budget,
scale and long-running operation. There is no independent reproduction yet.
[`docs/research-originality.md` §5; `docs/value-assessment.md` "Indispensable M4 measurements"; `docs/benchmark-plan.md`]

**14. Which kernels are supported?**
One test target: Debian kernel 6.12.111 in a QEMU VM, plus a 6.12.112 KASAN debug kernel on
demand. Kernel APIs change between versions; compatibility with other kernels is not
promised. [`README.md`; `docs/community-plan.md` §9]

**15. Is it production-ready?**
No. It is a research project with no release; a module bug can cause a kernel panic or data
loss, and the project's own tests found three kernel bugs during development (count to be
confirmed against records 0011 to 0015). [`docs/value-assessment.md`; `docs/community-plan.md` §9]

**16. How can I try it?**
With Docker, `make vm-test` builds the tools and module and boots a disposable QEMU VM with the
pinned kernel; no host module loading is needed. Expect passing markers such as
`MOOBFS_LAYER_PASS`. Do not load the module on a machine you care about. [`README.md`; `docs/testing.md`]

**17. How is data read without the module ("nothing trapped")?**
Memories are ordinary files and xattrs on the lower filesystem. After unmounting, `grep`,
`getfattr`, `git` and `rsync` read them directly. This follows from the design and was
exercised with git and `fsx`; reading the lower directory after a specific unmount sequence is
rated "probable" in the originality audit. [`docs/decisions/0007`; `docs/research-originality.md` §5]

**18. What license applies?**
GPL-2.0-only for the whole project. [`LICENSE`; `README.md`]

**19. How do I contribute?**
Start with `CONTRIBUTING.md`: reproducible bug reports, kernel or format review and
reproducibility improvements are welcome, with a `Signed-off-by` on each commit (DCO) and KASAN
required for changes under `layer/` or `vm/`. Review requests with bounded questions are listed
in the community plan. [`CONTRIBUTING.md`, still being rewritten; `docs/community-plan.md` §8 and §13]

**20. How do I report a security issue?**
Do not open a public issue. Use the private GitHub Security Advisory channel or the contact in
`SECURITY.md`; the planned response times are acknowledgement in 7 days and assessment in 30.
[`SECURITY.md`; `docs/community-plan.md` §13]

---

## 7. Legitimate outreach strategy

General rule: publicize **reproducible results**, not promises. Each publication links the
exact commit, the command and the logs. Before M4, no performance comparison.

| Channel | When | Honest framing | Notes |
| --- | --- | --- | --- |
| Review requests (`kernel-expert`/`security` issues) | Now (after the phase 3b merge, community-plan actions 15-18) | Bounded question, package for the reviewer (community-plan §8) | Without claiming an audit; credit to the reviewer |
| `r/linux_kernel` | Only with something technically debatable (subreddit rules and whether they accept own projects to be confirmed) | "Experimental stacked fs, here are the limits, looking for review" | Avoid posting if the rules forbid self-promotion |
| kernelnewbies (list/IRC) | Learning questions about VFS APIs, when there is a real doubt | Specific question, not an announcement | The list's current policy to be confirmed |
| LKML / linux-fsdevel | **Only** with a concrete technical question for the maintainers (for example, use of `vfs_path_lookup` in the ioctl); out-of-tree code and an experimental module do not justify an RFC | Short question, reproducible patches/links, aware that the module is out of tree | Follow the list's rules; never for promotion |
| LWN | Do not request coverage; only a factual comment where the subject is already being discussed | — | Do not write a "pitch" |
| Hacker News "Show HN" | After M4 is published or a clear reproducible demonstration; framing "experimental, one kernel, no benchmarks yet (or: here are the raw numbers)" | The author answers in real time, admits limits | Title without superlatives; no asking for votes |
| Technical articles | After M4, with raw data, scripts and the commit; before that, a text on the testing approach (`dm-log-writes`, KASAN) is possible | Describe method, failures found and limitations | Publish the numbers even if unfavorable (foreseen in ADR-0007) |
| Participation in related projects | When technically relevant: YoloFS (approach comparison), ai-memory, Letta, mem0 (integration as a client; an issue only if there is a reproducible interoperability problem) | Contribution or concrete question, no announcement | Respect each project's rules; do not open promotional issues |
| External reviewers | Direct invitations to people chosen for technical affinity, with the package from community-plan §8 | No pressure; "reviewed" never means "approved" | Record findings in an issue/advisory and the journal |
| Contribution documentation and "good first" issues | Continuous | Issues without kernel work (community-plan §4) | Keeps contributors qualified |

Explicitly excluded: fake or bought stars or forks; paid links; spam or indiscriminate
automated posting; multiple accounts; mass requests for stars; `llms.txt` or
artificial metadata presented as a guarantee of AI citation (if one is ever added,
it will be only a document index, with no promise); text written to mislead assistants;
claims from the list in `research-originality.md` §7. For AI answers, the only legitimate
method is to make the documentation correct, stable and citable.

---

## 8. Prioritized implementation plan

Effort: S (up to 2 h), M (half a day to 2 days), L (more than 2 days). Items marked
"decision" depend on the maintainer.

| # | Action | Effort | Depends on | Maintainer decision |
| --- | --- | --- | --- | --- |
| 1 | Update the repository description (§2) and topics (§3) on GitHub | S | — | Choice between recommended/A/B; remove `deduplication` |
| 2 | Update `<title>`, meta description and the status block of `site/index.html` (and `guide.html`/`plan.html`) to M2/M3; add experimental banner | M | Approved text of community-plan §9; rewritten README | — |
| 3 | Reorganize the README per §4 (includes the banner) | M | End of the other agent's ongoing edit | — |
| 4 | Mirror the structure in `README.pt-BR.md` | S | 3 | — |
| 5 | Create `docs/architecture-overview.md` (EN) | M | Facts from ADR-0005/0007, spec-m2, spec-m3 | — |
| 6 | Create `docs/when-not-to-use.md` (EN) from `value-assessment` | M | — | — |
| 7 | Create `docs/faq.md` (EN) with §6 reviewed against the current code | M | 5, 6 (for links) | — |
| 8 | Add to the site: `faq.html`, `og:`/`canonical`, `robots.txt`, `sitemap.xml`, link to the architecture | M | 2, 7; final site URL (depends on 12) | — |
| 9 | Enable Discussions (categories: Q&A, Ideas, Show and tell) or keep only issues | S | Capacity to moderate | **Decision: enable or not**; suggestion: enable after the phase 3b merge if there is time to answer, otherwise keep disabled and acknowledge the channel in CONTRIBUTING |
| 10 | Create the first tag/release `v0.1.0-experimental` (GitHub pre-release) with notes linked to `CHANGELOG.md` | M | Phase 3b merge; CHANGELOG with dated section | **Decision: create or not.** Pro: citable point. Con: suggests stability and UAPI v1 may change; distribution-strategy places a public version after M4. If created, mark as "pre-release" and use the name with "experimental" |
| 11 | Adjust `CHANGELOG.md`: "Unreleased" section split by phase with links to journals | S | 10 | — |
| 12 | Rename the repository `llmoobfs` to `moobfs` | M | Check the name's availability in the organization; verify redirect of clone, Pages, badges and links | **Decision: rename or keep.** Renaming aligns name and project, and GitHub redirects the repository and clones, but the Pages URL changes (redirect to be confirmed). Do it before outreach, only once; update all links and badges |
| 13 | Measure baseline traffic (Insights/Traffic, Pages with Search Console) before any outreach | S | 2, 8 | — |
| 14 | Selective outreach (§7), starting with review requests | M | 1-8; phase 3b closed | — |
| 15 | Publish the M4 benchmark and the technical article | L | M4 completed | Whether to publish unfavorable numbers (already foreseen) |
| 16 | Reassess own domain/`CNAME` | S | 12, 15 | Yes/no |

Suggested order: 1, 12 (if it is to be done), 3, 4, 2, 5-7, 8, 9, 10, 11, 13, 14, 15, 16. Items 1 and 2
fix outdated information and do not depend on a strategic decision.

---

## 9. Success criteria at 30, 60 and 90 days

The goal is **credibility and qualified contributors**, not visits. No target below
is a guarantee; the ranges are order-of-magnitude hypotheses for a new, niche
(kernel) project, without paid promotion; adjust after measuring the baseline from action 13.

### Metrics available on GitHub

| Metric | Where | Use |
| --- | --- | --- |
| Unique visitors and clones, 14 days | Insights > Traffic | Detect spikes after a publication; short-retention data, export manually every 14 days |
| Referrers and popular content | Insights > Traffic | See which pages (README, docs, site) attract and from where |
| Stars and forks | Repository page | Weak signal; interpret only with the profile of who interacts |
| Issues and PRs from new accounts | Issues/PRs | Strong signal: answered review requests, documentation or test PRs |
| CI results and KASAN runs | Actions | Continuous quality |

### Metrics that require external tools

| Metric | Tool | Note |
| --- | --- | --- |
| Impressions, queries and position of the Pages pages | Google Search Console (site verification, feasibility for `github.io` to be confirmed) and Bing Webmaster | Data only appears weeks after indexing |
| Presence in AI answers | Monthly manual sampling of ~10 questions (the §5.1 queries) in a few assistants, recording the date, the model and whether there is a correct citation | No coverage promise; results vary between runs |
| Citations in technical texts | Manual search for mentions of the repository (links) and alerts | Record the context and whether the description is correct |

### Ranges per period

| Period | Quality (priority) | Reach (informative) | Rationale |
| --- | --- | --- | --- |
| 30 days | Metadata, README, site and FAQ fixed and consistent with each other; 0 claims from the §7 list of `research-originality.md`; 1 or 2 review requests published; response to every issue within ~7 days | Stars: from a handful to ~20 (speculative range, most coming from direct contacts); weekly unique visits in the tens | Without broad outreach, traffic tends to come from the author's contacts; a low count does not indicate failure |
| 60 days | At least 1 substantive external review or 1 external PR (documentation, test or reproduction); Pages indexed (appears in Search Console); 1 independent reproduction of `make vm-test` recorded ("to be confirmed" if it occurs) | Stars ~20 to ~80 if there is a well-received technical post (hypothesis); 0 to 3 external contributors | The main limiting factor is kernel reviewers' time, not discovery |
| 90 days | M4 benchmark plan executed or with a publication date; release decision made and recorded; monthly AI sample shows correct descriptions (or no citation, which is acceptable) | Stars ~50 to ~250 only if M4 results are published (very uncertain); 1 to 5 external contributors | Interest depends on measurable results; an unfavorable M4 is also a valid result (ADR-0007) |

Warning signs that something is wrong: star spikes without issues or clones, suspicious
referrers, new accounts with no history asking for promotion or offering services. Do not act on
artificial stars; if detected, ignore them and do not amplify them.

---

## 10. Files and settings that would need to change

Consolidated list; **none was changed by this document**. Files marked (*) are
being edited by another agent: coordinate first.

| File | Change |
| --- | --- |
| `README.md` (*) | §4 reorganization; approved banner; links to `docs/architecture-overview.md`, `docs/when-not-to-use.md`, `docs/faq.md`; no claims from the §7 list |
| `README.pt-BR.md` (*) | Mirror the structure; keep the same limits |
| `ROADMAP.md` (*) | Statuses "completed with evidence / planned / dependent on validation" (community-plan §10); no dates; links to journals |
| `CONTRIBUTING.md` (*) and `.github/` (*) | Link to Discussions (if enabled), review requests and the language policy; no change from this plan |
| `site/index.html` | New `<title>` and meta description (§2); "M2/M3" status in place of M1; experimental banner; `og:title`, `og:description`, `canonical` |
| `site/guide.html`, `site/plan.html` | Updated descriptions and notices; reproduction guide for the layer (today covers only M1); `plan.html` stops saying M2 is not implemented |
| `site/faq.html` (new) | Rendering of the §6 FAQ |
| `site/robots.txt`, `site/sitemap.xml` (new) | Page discovery by Pages; final URL depends on the rename decision |
| `docs/architecture-overview.md` (new, EN) | §5.2 |
| `docs/when-not-to-use.md` (new, EN) | §5.2 |
| `docs/faq.md` (new, EN) | §6, reviewed against the code before publishing |
| `docs/history/` and `CHANGELOG.md` | Journal for this round (when implemented) and, if any, the note on the first tag; decision history on renaming and Discussions |
| `docs/decisions/` | A short ADR only if the repository name changes, or if the release policy is defined (sequential ID; do not change existing ones) |

GitHub settings (outside the repository, made by the maintainer):

| Setting | Proposed change |
| --- | --- |
| Description | §2 text (one of the three options) |
| Topics | §3 list, up to 20 |
| Website | Keep `https://studiomoob.github.io/llmoobfs/` (or the new URL after renaming) |
| Discussions | **Maintainer decision** (action 9); if enabled, categories Q&A, Ideas, Show and tell |
| Wiki | Keep disabled |
| Releases | **Maintainer decision** on `v0.1.0-experimental` (action 10), as a pre-release |
| Repository name | **Maintainer decision** (action 12) |
| Social preview | 1280x640 image without claims (to be confirmed whether one already exists) |
| Security | Keep private Security Advisories enabled (contact in community-plan §13) |

Pending "to be confirmed" items: exact behavior of the Pages redirect after renaming;
feasibility of Search Console for `github.io`; current rules of the subreddits and lists;
current repository count per topic; existence of a social preview image;
number of kernel defects (journals 0011 to 0015); current README text after the ongoing
edit.

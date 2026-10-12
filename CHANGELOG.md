# Changelog

## Unreleased

- Phase Q2d (journal 0038): term flag `MOOB_TERM_MISSING_OK` ("missing or ...", the byte
  formerly `reserved[0]` is now `flags`) and `MOOB_IOC_QUERY2_BATCH` (up to 8 QUERY2 requests on
  one domain, one permission check, all validated before any runs). Operators ending in `?` in
  `query`, `moobfs-index` and the bench query files; `query q2batch`.
- Phase Q2c (journals 0037 and 0044): `QUERY2` without `order_by` and with two or more
  equality terms finds the shortest of their inode-ordered runs by walking them in lockstep
  and scans only that one; a first version with leapfrog seeks was measured slower whenever
  the first term's run was short and was replaced. Test client `--raw` prints the kernel's
  order. Measurements on the Mac mini host (`bench/q2-campaign.sh`, `bench/q2c-compare.sh`,
  `bench/results/2026-10-12-macmini-q2/`): QUERY2 answers v1 Q3 24 to 30 times faster and Q4
  about 7 times faster, with the same inodes.
- Phase Q2b (journal 0036): `QUERY2` orders by an attribute (`order_by`, `order`,
  `order_type`, ties by inode) and pages with an authenticated 128-byte cursor (`ESTALE` for
  a cursor from another query or mount, corrupt, or issued before a fallback). Oracle
  `--order`/`--limit`, test client paging, `bench-load --order/--limit`.
- KASAN workflow: `virtme-ng` 1.41 and its dependencies are installed from
  `vm/kasan-requirements.txt` with `pip --require-hashes` instead of a version-only pin
  (journal 0046; OpenSSF Scorecard Pinned-Dependencies).
- CodeQL triage (journal 0045): every one of the 30 open alerts has a decision. Fixed in
  `bench/`: manifest fields are copied with `memcpy` of the length already checked against the
  destination (the three `cpp/unbounded-write` alerts were false positives), and record files
  and the `bench-gen` manifest are created with mode 0644 through `bench_create_out()` instead
  of `fopen`'s 0666 (unchanged under umask 022); W6 opens `drop_caches` without `O_CREAT`.
  The 20 `cpp/path-injection` alerts and the `cpp/sql-injection` alert describe the intended
  use of command-line tools and of the benchmark harness; dismissal commands for the
  maintainer are in the journal.
- Contributor experience, batch 3 (plan `docs/dx-plan.md`, journal 0041): `README.md` and
  `README.pt-BR.md` open with one sentence, a real demonstration (domain-scoped `QUERY2` as a
  regular user, results filtered by permission, v1 refused) captured from `make vm-test`, and a
  four-command quickstart, followed by the experimental notice, now compact and dated
  2026-10-11 (UAPI v2, pilot-grade measurements only); the demonstration is a new `vm/suite.sh`
  block with assertions and the `DEMO_PASS` marker. `docs/README.md` indexes every document
  with its language. `docs/assets/social-preview.svg`/`.png` for the GitHub social preview.
  Benchmark raw archives move to one release, `bench-evidence`: `bench/evidence-release.md`
  (release text and manifest), `scripts/bench-evidence-release.sh` (dry run by default;
  `--apply` uploads, verifies size and SHA-256 through the API and the public URL, then deletes
  the per-campaign pre-releases and keeps their tags; `add` for new campaigns),
  `bench/results/README.md`, links in `bench/results/*/README.md` and
  `docs/benchmark-harness.md` §7.1. The release move itself is pending for the maintainer.
- Contributor tooling, batch 2 (plan `docs/dx-plan.md`, journal 0039): `.devcontainer/`
  (Ubuntu 24.04, docker-in-docker, host tools; not yet advertised, the Codespaces measurement
  of `make vm-test` is pending); OpenSSF Scorecard workflow and README badge; CodeQL for the
  user-space C (`tools/`, `common/`, `bench/`); `make checkpatch` (`checkpatch.pl --no-tree`
  on `layer/` and `kernel/`, script pinned to `v6.12.112` and verified by SHA-256), advisory
  in the CI `Lint` job; `area/*` labels on pull requests by path (`.github/labeler.yml`) and a
  greeting on a person's first issue and first pull request; Dependabot for the devcontainer.
- Phase Q2a of the domain-scoped QUERY (`docs/spec-query-v2.md`, journal 0035): mount option
  `domain_depth` (default 1, 0 keeps the previous behaviour); the RAM index is partitioned by
  domain and by the homogeneity rule; `MOOB_IOC_QUERY2` on a domain directory answers from its
  partition with one permission check per call; `GET_INFO` reports `domain_depth`,
  `heterogeneous` and `fallback_domains` (flag `MOOB_FLAG_DOMAINS`) in former reserved space;
  the v1 `QUERY` needs `CAP_SYS_ADMIN` while domains are on; a directory renamed across
  domains or depths gets `EXDEV`. `moobfs-index --domain`, `query q2|q2errno|q2invalid|rename`,
  `bench-load --query2 1` and `--mount-opts`, and `bench/timing-v2.sh`. **Default change:** a
  mount without `domain_depth=0` restricts the v1 QUERY to administrators.
- Contributor experience (plan `docs/dx-plan.md`, journal 0032): `make help` and `make lint`
  (the CI `Lint` job now runs `make lint`, which also covers `scripts/*.sh`); SPDX check moved
  to `scripts/check-spdx.sh`; `.github/CODEOWNERS`; `CITATION.cff`; `good first issue` and
  `help wanted` documented alongside the project's own labels.
- ADR-0009 accepted (alternative E): the attribute index stays in the kernel, partitioned by
  permission domain; `docs/spec-query-v2.md` proposes the domain-scoped QUERY (UAPI v2). Research
  behind the decision: `docs/research-metadata-search.md` and addendum,
  `docs/research-academic-survey.md`, `docs/research-client-query-patterns.md`.
- `QUERY`: results are copied to the caller one at a time instead of through a limit-sized
  kernel array, and the scan of the first term starts at the first candidate and stops after
  the last one (journal 0027). UAPI unchanged. New suite block with one attribute holding
  several types.
- Dentry revalidation now answers in RCU-walk for the plain case instead of always forcing
  ref-walk, so path walks through the layer stay lockless (journal 0028).

## v0.1.0-experimental — 2026-10-10

First pre-release tag: a source snapshot of M1, M2 (phases 2a–2e), M3 phase 3b and the M4
harness, with the community and SEO files. No binaries, no packaging, no support, no
performance numbers. Everything below was previously listed as unreleased.

- `bench/`: first slice of the M4 harness: deterministic manifest generator, loader for W1-W4
  with per-call latency records, report with percentiles and dispersion, `make bench-smoke` in
  CI. Correctness only; no performance numbers. `bench/sqlbase.sh` (SQLite baseline schema,
  composite indexes, plan gate) and `bench/equiv.sh` (kernel, SQLite and oracle must agree)
  run in the VM suite over the layer. W5, W6/W7, W8, `bench/run.sh` (campaign orchestrator)
  and `bench/sqlclient.c` (W3 through libsqlite3 for the baselines) complete the harness.
- SEO/GEO files (plan `docs/seo-geo-plan.md`, journal 0024): `README.md` reorganized into the
  planned section order (banner and "At a glance" unchanged; new "How it works", "What is
  implemented, with evidence", "Limitations", comparison, "Documentation map", "Prior work and
  positioning"), `README.pt-BR.md` mirrored; new English pages `docs/architecture-overview.md`
  (layer internals with source citations), `docs/when-not-to-use.md` (comparison with plain
  files, SQLite, FUSE systems and memory frameworks; what M4 must show) and `docs/faq.md`
  (twenty sourced answers); site updated to the current state (M1, M2 phases 2a–2e and M3
  phase 3b done with evidence, M4 pending, nothing released) with the new title and
  description, the experimental banner, `rel=canonical` and Open Graph tags on every page,
  `site/faq.html`, `site/robots.txt` and `site/sitemap.xml`; stale statements fixed (`plan.html`
  said M2 was not implemented and that `QUERY` returned file handles; `guide.html` said the QEMU
  timeout was 180 s). Documentation only; no code, test or CI changes; no release or tag.
- Community readiness (plan `docs/community-plan.md`, actions 1–14, journal 0024): Contributor
  Covenant 2.1 as `CODE_OF_CONDUCT.md`; `SECURITY.md` with scope, what to include, response
  targets (acknowledge in 7 days, assessment in 30, updates every 30), coordinated disclosure,
  CVE via GitHub, credit, no bounty; `CONTRIBUTING.md` rewritten with the experimental stage,
  language policy, DCO (`Signed-off-by` required), commit style, history and scope rules, testing
  requirements, review rule (one maintainer plus one recorded external review for `layer/`),
  no performance claims without evidence; PR template checklist; issue templates revised
  (`bug_report`, `proposal`, `config`) and added (`test_failure`, `review_request`);
  `docs/labels.md`; `docs/development.md` (new) and `docs/testing.md` restructured with a marker
  table; experimental-status banner and "At a glance" in both READMEs; `ROADMAP.md` in three
  evidence states; CI `Lint` job (shellcheck, SPDX headers, DCO check on pull requests); KASAN
  workflow now also runs on pull requests touching `layer/` or `vm/`. Documentation and
  configuration only; no code or test changes.
- `docs/project-review-2026-10-10.md`: implementation and roadmap review, static QUERY
  correctness findings, configurations requiring Linux reproduction, and a proposed
  focused benchmark and agent-memory demonstration. Analysis only; no code fixes.
- `docs/research-originality.md`: prior-art and originality audit (SFS, BFS, YoloFS, SMFS and
  others; patents described, no legal opinion); claims classified as proven, probable or
  unverified, with a list of statements to avoid in public texts.
- `docs/distribution-strategy.md`: proposed deployment model (Core, Workspace, Integration),
  DKMS packaging and kernel matrix, container bind-mount limits, incremental release plan;
  nothing implemented.
- `docs/value-assessment.md`: critical assessment of real usefulness, value proposition and
  maturity as an adoption criterion; today's recommendation for production stays SQLite or
  plain files; code findings on index term selection and budget accounting recorded.
- `docs/spec-m3.md`: M3 specification proposal (identity, isolation, provenance, change cursor,
  batch reads).
- `docs/benchmark-plan.md`: M4 benchmark methodology, baselines and the real clients used as
  workloads; M3 scope gains agent/namespace isolation, batch reads and a change cursor.
- `docs/security-roadmap.md`: proposed list of security and robustness techniques for the layer
  (threat model, fstests, syzkaller, hostile lower, static analysis, disclosure policy,
  hardening, resource limits, module signing), with current state and sources; linked from
  the roadmap. Nothing implemented yet.
- Phase 2e: acceptance with git, SQLite (WAL, concurrent processes) and an own memory client;
  power-loss replay with dm-log-writes and I/O error injection with dm-flakey. Two layer bugs
  found and fixed: stale lower dentry after unlink+recreate (lost SQLite WAL frames) and a
  concurrent `set_nlink` race on unlocked attribute copies (kernel warning on CI).
- Phase 2d: in-memory attribute index built at mount and kept current on writes; `QUERY` and
  `GET_INFO` ioctls on the mount root (UAPI v1); `moobfs-index` user-space oracle; kernel answers
  verified identical to the oracle over ext4, btrfs and XFS.
- Phase 2c: typed `user.moob.*` attributes validated in the kernel (shared encoding in
  `common/attr.c`), `moobfs-attr` and `moobfs-check` tools, host unit tests and a libFuzzer target.
- Phase 2b: rename, links, symlinks (`nolinks` option), opaque xattr pass-through, delegated
  reflink/dedupe/copy_file_range and FIEMAP; validated over ext4, btrfs and XFS in the VM.
- Phase 2a of the memory layer: `layer/moobfs_layer.ko`, a native stacked pass-through
  filesystem over a lower directory, validated in the QEMU VM over ext4 with the pass-through
  acceptance test and 23,000 `fsx` operations (vendored from xfstests).
- ADR-0007 supersedes ADR-0006: MoobFS becomes a native stacked memory layer over a mature
  filesystem; `docs/spec-m2.md` rewritten accordingly, previous version preserved. Proposal only.
- Journal 0009: differentiation analysis and the "build on mature software, innovate only in
  LLM-memory features" principle; open question whether M2 becomes a stacked filesystem.
- ADR-0006 and `docs/spec-m2.md`: M2 specified as a writable POSIX volume on jbd2 + iomap with
  shared extents via `remap_file_range`; `docs/kernel-infrastructure.md` lists the reused kernel
  infrastructure verified against v6.12. Proposal only; no code.
- `docs/research-ai-memory.md`: code-level reading of akitaonrails/ai-memory and its
  relation to MoobFS; M2 now needs POSIX writes to host a real client.
- Site: interactive plan page (`site/plan.html`) showing inputs, processing and outputs per milestone.
- `docs/ecosystem.md`: survey of free software relevant to testing, crash consistency,
  kernel data structures, BFS tooling and agent memory layers.
- ADR-0005: agent memory model (typed attributes, kernel-side attribute indexes,
  ioctl queries) with the Be File System as design reference; no code changes.
- Clarified LLM memory/context as the project goal in both READMEs and the site,
  separating future agent workflows from the implemented read-only M1.
- Native Linux read-only filesystem module, offline image creator and verifier.
- Independent file inodes can share one immutable payload; FIEMAP exposes sharing.
- Versioned M1 disk format with bounded parsing, CRC32C metadata and SHA-256 content.
- Tool tests and isolated QEMU mount tests, including malformed images and read-only enforcement.
- Project history, architecture decisions and shared Codex, Claude and Cursor rules.
- GPL-2.0-only license for the entire project.
- English/Portuguese README, contribution, governance, security and conduct policies.
- Issue/PR templates, pinned CI actions and a static GitHub Pages site with a reproduction guide.

This is an experimental milestone, not a stable release or the complete transactional MVP.

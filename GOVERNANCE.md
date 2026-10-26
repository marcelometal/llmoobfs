# Governance

MoobFS is maintainer-led by Studio Moob (`@studiomoob`). The current project contact
is Claudio at claudio@studiomoob.com. Contributors propose changes through issues
and pull requests; maintainers make and record final technical and release decisions.

Architecture decisions are recorded in `docs/decisions/`, implementation evidence
in `docs/history/`, and user-facing changes in `CHANGELOG.md`. Proposals do not
become compatibility commitments merely because they appear in a design document.

There are no stable releases yet. A future release must identify a Git commit,
supported kernel, disk-format compatibility and reproducible validation. Changes
to project governance or license policy require explicit maintainer decisions and
must respect existing contributor rights.

For behavior concerns use the conduct policy; for vulnerabilities use the security
policy. Ordinary questions can use issues. Public discussions should remain public
unless privacy or security calls for the documented private contact.

## Repository controls

The main branch requires pull requests, the tool/sanitizer and native-VM checks,
an up-to-date branch and resolved review conversations for contributors subject
to protection. Force pushes and deletion are disabled. No numeric approval quota
is enforced while the project has a single maintainer; maintainers still review
contributions. Administrators currently retain bypass for repository maintenance.
GitHub Pages deploys the versioned static site from main through Actions.
Who has access to what, and the incident procedure, are in
`docs/security-operations.md`.

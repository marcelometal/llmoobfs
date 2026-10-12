# 0003 — Open source base and GitHub

Date: 2026-10-10. Status: published; remote CI and Pages validated.

## Request and choices

The user asked to prepare GitHub, README and Pages before advancing the project. They confirmed
GPL-2.0-only across the whole repository and claudio@studiomoob.com as the public contact.
For the site, they asked to combine a visual presentation and technical documentation. ADR-0004
records the policy and the official sources researched. PRODUCT.md and DESIGN.md record
the site's direction; the Impeccable skill guided the presentation.

## Changes

Created README in English and Portuguese, contributing, conduct, security, governance,
roadmap, reproduction guide and M1 format, issue/PR templates and Dependabot for
Actions. CI runs tools, sanitizers and the VM. External Actions pinned by
commit. Pages publishes only `site/`, with deployment permissions in its own job.
The static site has a visual explanation of the sharing and a reproducible guide.

Configured the `origin` remote to `https://github.com/studiomoob/llmoobfs.git`,
verified as an existing empty and private repository, with administrative access.
Description, homepage and topics were configured; issues enabled and wiki disabled.

## Local validation

Links and anchors of the two pages passed in `python3 scripts/check-site.py`.
Preview inspected in the browser; navigation to the guide verified. A column
overflow on a narrow screen was fixed with `min-width: 0`; the later measurement did not
show horizontal overflow. The diagram got its own composition for mobile.
Port 8080 was occupied; the preview was started on 127.0.0.1:8879.
Native tests and evidence are in journal 0002. Kernel tests are not repeated
for documentation-only changes. Remote CI and Pages will be recorded in an
addendum with the real results, without presuming success from the presence of the workflows.

## Addendum — authorized and verified publication

The first request to enable Pages returned HTTP 422: the organization's plan
does not allow Pages on this private repository. The user explicitly authorized
“Sim, tornar público e publicar o Pages” (“Yes, make it public and publish Pages”). The repository was made public, Pages
configured for GitHub Actions with HTTPS and private vulnerability reporting
enabled (the API confirmed `enabled: true`). No paid plan was purchased.

The first attempt of the Pages workflow failed while the site was not yet
enabled. After the configuration, the rerun passed. Results at commit `ed4b997`:

- [CI 38058154595](https://github.com/studiomoob/llmoobfs/actions/runs/38058154595):
  success in tools/sanitizers and native Linux VM.
- [Pages 38058154618](https://github.com/studiomoob/llmoobfs/actions/runs/38058154618):
  success on the rerun, with the site at https://studiomoob.github.io/llmoobfs/.
- [Public repository](https://github.com/studiomoob/llmoobfs): README, license,
  policies, history, code, workflows and site pushed to the `main` branch.

The community profile API reported 100% of the criteria it considers;
this is not a certification of security or technical quality. The permanent summaries
stay in Git; CI artifacts have 30-day retention. The security contact was
updated to include the GitHub private channel now enabled.

## Addendum — branch protection and public verification

The API confirmed `main` protected with the checks `Tools and sanitizers` and
`Native Linux VM`, branch up to date before merge, PR required for whoever cannot
bypass the rule, and conversation resolution. Force-push and deletion disabled.
Zero required approvals avoids blocking a project with a single maintainer;
the review policy remains in CONTRIBUTING.md. Administrators keep a bypass
(`enforce_admins: false`), an explicit exception, not absolute protection.

The published homepage, guide and CSS answered HTTP 200. The public page was
opened and verified in the browser. The Pages configuration requires HTTPS. No
change to the native code happened after the successful CI mentioned above.

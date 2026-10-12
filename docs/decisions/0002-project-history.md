# ADR-0002 — Versioned technical history

Date: 2026-10-10. Status: accepted; requested by the user.

## Decision

Use Git for the exact history of code/documents, ADRs for decisions and chronological journals for requests, changes, experiments and results. Record alternatives and justifications in verifiable technical summaries.

## Consequences

Future sessions read `AGENTS.md` and the history before working. Superseded decisions remain available with a reference to their successor. Do not version secrets or large reproducible binaries. The original proposal is preserved.

The user is creating the GitHub repository. Initialization and local commits may proceed; remote publication will wait for the user's address and instruction. A remote repository does not replace an independent backup copy.

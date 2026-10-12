Translation of `docs/ai-workflow.md` at commit `fccf38a`. The Portuguese original prevails.

# Working with Codex, Claude Code and Cursor

The common policy is in `AGENTS.md`, versioned alongside the code.

| Tool | Entry point in the project |
| --- | --- |
| Codex | `AGENTS.md` at the repository root. |
| Claude Code | `CLAUDE.md` imports `@AGENTS.md`. |
| Cursor | `.cursor/rules/project-history.mdc`, with `alwaysApply: true`, instructs reading the common policy and includes the essential rules. |

The instructions travel with the repository clone. Open each tool at the repository root and confirm that its rules are loaded. This project makes no change to global preferences or local hooks. Instructions steer the agents; tests, diff review and commits provide verifiable evidence.

## Work cycle

1. Gather the relevant context from the README, journal and ADRs.
2. Record the session goal and any new decisions when necessary.
3. Implement the change and run the appropriate validation.
4. Record the actual results, relevant failures, corrections and limitations.
5. Review the diff, update the changelog and commit a completed unit of work.
6. Leave the next step clear; remote publishing requires the user's authorization.

## Official sources verified on 2026-10-10

- [Codex and AGENTS.md](https://developers.openai.com/cookbook/examples/gpt-5/codex_prompting_guide)
- [Memory and imports in Claude Code](https://code.claude.com/docs/en/memory)
- [Cursor project rules](https://cursor.com/docs/rules)

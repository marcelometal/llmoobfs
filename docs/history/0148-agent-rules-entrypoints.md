# 0148 — Make the shared project rules visible at the right entry points

Date: 2026-10-20. Status: local instructions updated; no publication.

## Goal and decision

The maintainer consolidated the MoobFS continuity rules in a private local file and
asked that Codex find it without applying MoobFS-specific rules to other projects.
Keep the full memory and outreach procedures outside Git. Put the process rules
that apply to every tool in the repository's `AGENTS.md`; keep the private file
path in the user's Codex instructions with a MoobFS-only condition.

The existing public-surfaces rule was already in `AGENTS.md`. Added local batch
order, correction checks and KASAN cadence, issue ownership, client neutrality,
and prior-art/reference practice there. Removed its instruction about the timing
of outreach; outreach procedures remain in the private local rules. The project
instructions contain no address of the local private file or personal contacts.

Outside Git, the user-level Codex instructions now identify MoobFS by checkout
or repository identity and direct the assistant to read the handoff and the
private rules in full. The handoff's introduction names that file as the current
source and the older memory files as historical copies. A dated note in the
private rules clarifies which general process rules may be summarized publicly.

Alternative considered: copy the entire private file into the repository.
Rejected because it mixes working rules with personal and outreach history.
No architectural decision, module behavior, format or UAPI changed.

## Verification and limitations

Inspected both `AGENTS.md` entry points and the handoff after editing. Ran
`git diff --check` and checked the private filename and contact details were
absent from the repository diff. No module build, VM suite, KASAN, negative
round or outreach action was needed or performed for this instruction-only unit.

The private and global Codex files are local to this machine, not delivered by
Git. A new machine or assistant without them must receive the handoff path in
the initial prompt. Next: use these entry points in later sessions and keep
dated rule changes in the local file.

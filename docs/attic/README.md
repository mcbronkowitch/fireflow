# Attic

This directory holds discontinued work from FireFlow development—specs, plans, audits, and tuning notes that remain in the repository for their reasoning and design rationale, rather than their code implementation. The documents here have been superseded or removed from the active roadmap and should be read for context only, not as current direction.

## How to recover SWARM

**Tag:** `attic/swarm-2026-08-18`. The withdrawal is written up in
[`2026-08-18-swarm-withdrawn.md`](2026-08-18-swarm-withdrawn.md), which carries
the recovery commands, what the two rounds measured, and what was never
established. Both specs, all three plans and the round-1 CPU decision are kept
here as documents; the code, the tests and the `itcm-relief` bench work exist
only in the tag.

## How to recover the faceplate

**Last commit that still contains the faceplate:** `a4aca1a` — the parent of the
Task 4 deletion commit, `feat(repo): delete the flow layer and Glow`.

The parent is recorded rather than the deletion commit itself: this note is
written in the deletion commit, so it cannot name that commit's own hash.

To recover the deleted `hardware/glow-faceplate/` KiCad faceplate tree, use:
```bash
git show a4aca1a:hardware/glow-faceplate/            # lists the tree
git show a4aca1a:hardware/glow-faceplate/README.md   # one file out of it
```
Repeat the second command with each path the listing names to pull every file.

This avoids hunting through the history with `--diff-filter=D` when the faceplate KiCad files need to be reviewed or recovered.

# Attic

This directory holds discontinued work from FireFlow development—specs, plans, audits, and tuning notes that remain in the repository for their reasoning and design rationale, rather than their code implementation. The documents here have been superseded or removed from the active roadmap and should be read for context only, not as current direction.

## Recovered files

**Last commit that still contains the faceplate:** `a4aca1a` — the parent of the
Task 4 deletion commit, `feat(repo): delete the flow layer and Glow`.

The parent is recorded rather than the deletion commit itself: this note is
written in the deletion commit, so it cannot name that commit's own hash.

To recover the deleted `hardware/glow-faceplate/` KiCad faceplate tree, use:
```bash
git show a4aca1a:hardware/glow-faceplate/
git show a4aca1a:hardware/glow-faceplate/README.md
```

This avoids hunting through the history with `--diff-filter=D` when the faceplate KiCad files need to be reviewed or recovered.

# Attic

This directory holds discontinued work from FireFlow development—specs, plans, audits, and tuning notes that remain in the repository for their reasoning and design rationale, rather than their code implementation. The documents here have been superseded or removed from the active roadmap and should be read for context only, not as current direction.

## Recovered files

**Task 4 hardware deletion commit hash:** `[TASK_4_DELETION_COMMIT_HASH]`

To recover the deleted `hardware/glow-faceplate/` KiCad faceplate tree, use:
```bash
git show [TASK_4_DELETION_COMMIT_HASH]^:hardware/glow-faceplate/
```

This avoids hunting through the history with `--diff-filter=D` when the faceplate KiCad files need to be reviewed or recovered.

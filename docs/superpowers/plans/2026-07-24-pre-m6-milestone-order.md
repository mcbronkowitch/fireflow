# Pre-M6 Milestone Order Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Document WAVE, STRING, ZAP, and PULL as milestones M5i–M5l before the hardware-dependent M6 milestone.

**Architecture:** This is a documentation-only change. `docs/roadmap.md` remains the detailed source of truth, while `README.md` mirrors its summary table and preserves the existing M6 identifier.

**Tech Stack:** Markdown, PowerShell, Git

## Global Constraints

- Required order: `M5i WAVE → M5j STRING → M5k ZAP → M5l PULL → M6`.
- PULL is the final milestone before M6.
- Existing milestone identifiers must not change.
- All four new milestones remain planned and unimplemented.
- M6 remains hardware-dependent, planned, and unimplemented.
- No DSP, host, test, build, or release files may change.

---

### Task 1: Synchronize the roadmap and README

**Files:**
- Modify: `docs/roadmap.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: the approved ordering in `docs/superpowers/specs/2026-07-24-pre-m6-milestone-order-design.md`
- Produces: matching M5i–M6 milestone sequences in the detailed roadmap and README summary

- [ ] **Step 1: Record the current mismatch**

Run:

```powershell
rg -n "\*\*M5[i-l]\*\*|M5[i-l] —|M6.*next|M6.*planned" README.md docs/roadmap.md
```

Expected: no M5i–M5l entries; M6 is described as the next planned milestone.

- [ ] **Step 2: Update the detailed roadmap**

In `docs/roadmap.md`:

- Set `Last updated` to `2026-07-24` and state that M5i–M5l now precede M6.
- Insert planned status-table rows for WAVE, STRING, ZAP, and PULL immediately before M6.
- Extend the milestone-order explanation to identify the four engine-level milestones as the available-hardware work before M6.
- Under `## Planned`, add one section per milestone in M5i–M5l order. Each section must summarize its existing source spec, say that it is not implemented, and link the exact spec path.
- Rename the M6 section suffix from `(next; spec ready)` to `(after M5l; spec ready)`.

Use these exact milestone labels and spec paths:

```text
M5i — WAVE
docs/superpowers/specs/2026-07-18-wave-engine-design.md

M5j — STRING
docs/superpowers/specs/2026-07-18-string-engine-design.md

M5k — ZAP
docs/superpowers/specs/2026-07-18-zap-percussion-engine-design.md

M5l — PULL
docs/superpowers/specs/2026-07-19-pull-chord-gravity-design.md
```

- [ ] **Step 3: Mirror the summary in README**

In the `README.md` roadmap table, insert four rows immediately before M6:

```markdown
| **M5i** | WAVE: four-voice PPG-style wavetable part engine | planned (spec ready; not implemented) |
| **M5j** | STRING: four-voice Karplus-Strong part engine | planned (spec ready; not implemented) |
| **M5k** | ZAP: monophonic percussion part engine | planned (spec ready; not implemented) |
| **M5l** | PULL: chord gravity between the two decks | planned (spec ready; not implemented) |
```

Keep the existing M6 row after them and change its status to:

```text
planned after M5l (spec ready; implementation not started)
```

- [ ] **Step 4: Verify ordering, wording, and scope**

Run:

```powershell
rg -n "\*\*M5[i-l]\*\*|\*\*M6\*\*|### M5[i-l]|### M6" README.md docs/roadmap.md
git diff --check
git diff --name-only
```

Expected:

- Both documents list M5i, M5j, M5k, M5l, then M6.
- `git diff --check` produces no output.
- `git diff --name-only` lists only `README.md` and `docs/roadmap.md`.

- [ ] **Step 5: Commit**

```powershell
git add -- README.md docs/roadmap.md
git commit -m "docs: schedule engine milestones before M6"
```

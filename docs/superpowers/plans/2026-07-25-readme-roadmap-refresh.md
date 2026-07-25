# README and Roadmap Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Update the public README and detailed roadmap to the verifiable
Spotymod 2.13.1 state while preserving their existing structure and honest
hardware boundary.

**Architecture:** Treat repository state as the source of truth and keep the two
documents at different levels of detail. `README.md` remains the concise public
entry point; `docs/roadmap.md` records the exact completed FORM/SONG behavior,
release evidence, and remaining milestones.

**Tech Stack:** Markdown, Git history and tags, VCV `plugin.json`, PowerShell
search guards, Python VCV panel guard.

## Global Constraints

- The approved design is
  `docs/superpowers/specs/2026-07-25-readme-roadmap-refresh-design.md`.
- Modify only `README.md` and `docs/roadmap.md`.
- Preserve both documents' existing structure and voice.
- Every changed factual claim must be supported by current `main`, the VCV
  manifest/tags, executable tests, committed benchmark evidence, or an approved
  repository spec.
- Current VCV version is exactly `2.13.1`.
- Current production engines are exactly Synth, Sampler, and WAVE.
- FORM values are exactly `TWO MOTIFS`, `ONE + VAR`, `HIERARCHICAL`,
  `CALL / RESPONSE`, and `OSTINATO`.
- SONG values are exactly `AAAB`, `ABAB`, `ABBB`, `BUILD`, `ROTATE`, `MIRROR`,
  and `OFF`.
- The current PLAY row is exactly `STEP · FORM · SONG · NEW`; TRIG is not a
  current panel control.
- The current executable Doctest case count is 680.
- M5h was released in 2.11.0, WAVE and SOURCE/Detune in 2.13.0, and the
  FORM/SONG split in 2.13.1.
- Do not mark STRING, ZAP, PULL, or M6 as started.
- Do not claim that the modulation-first firmware runs on Spotykach hardware.
- Do not turn either file into a full VCV control manual.

---

### Task 1: Refresh the Public README

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: `host/vcv/plugin.json`, the approved documentation-refresh spec,
  and the already-implemented FORM/SONG engine and panel.
- Produces: a concise public description that Task 2 uses as the summary-level
  consistency target.

- [ ] **Step 1: Record the stale README checks**

Run:

```powershell
rg -n "currently \\*\\*2\\.12\\.2\\*\\*|pending the next release|tests/ runs 200|both engines|NEW/TRIG|\\bTRIG\\b" README.md
rg -n "FORM|SONG|AAAB|ABAB|ABBB|BUILD|ROTATE|MIRROR|OFF|STEP · FORM · SONG · NEW" README.md
```

Expected: the first command finds the stale 2.12.2, pending-release,
200-test-case, and two-engine language; the second does not yet document the
complete FORM/SONG surface.

- [ ] **Step 2: Add the current melodic STEP model**

In `## What makes this fork different`, after the sampler/SliceMap paragraph
and before the general SuperModulator paragraph, add a compact paragraph with
these exact facts:

```markdown
Melodic **STEP** lanes keep two persistent full-pattern snapshots, A and B.
**FORM** chooses how those phrases are composed — TWO MOTIFS, ONE + VAR,
HIERARCHICAL, CALL / RESPONSE, or OSTINATO — while **SONG** independently
arranges them as AAAB, ABAB, ABBB, BUILD, ROTATE, MIRROR, or OFF. Structural
changes land on phrase boundaries; OFF keeps A evolving while B remains stored.
On the Rack panel the PLAY row is **STEP · FORM · SONG · NEW**. NEW rebuilds
the A/B pair and, on a Sampler, also spawns a grain immediately.
```

Keep the wording at summary level. Do not add the exact 16-phrase BUILD or
ROTATE tables here; those belong in the roadmap.

- [ ] **Step 3: Correct architecture and VCV release facts**

Update the architecture image alt text from `tests/ runs 200 deterministic
doctest checks` to `tests/ runs 680 deterministic Doctest cases`.

In `## Play it now — VCV Rack (beta)`, change the release sentence so it says:

```markdown
— `.vcvplugin` builds for Windows, Apple Silicon and Linux, currently **2.13.1**
(Synth, Sampler, and WAVE, including the independent FORM/SONG phrase arranger).
```

Retain the existing release link, installation sentence, beta qualification,
and M6 boundary.

- [ ] **Step 4: Refresh the README roadmap table**

Make these exact status corrections:

- M5 remains done and its cumulative sampler follow-ups are described as
  released through 2.11.0.
- M5h says `released in 2.11.0`, not `pending the next release`.
- M5i says `released in 2.13.0` in addition to its existing implementation and
  hardware-gate evidence.
- Insert this completed cross-cutting row after M5i:

```markdown
| **+ FORM/SONG** | Persistent A/B phrase snapshots: five FORM phrase engines plus AAAB, ABAB, ABBB, BUILD, ROTATE, MIRROR, and OFF SONG arrangements; boundary-safe changes and legacy patch migration | **done** (engine + renderer + VCV; released in 2.13.1) |
```

Do not change the planned status or order of M5j, M5k, M5l, or M6.

- [ ] **Step 5: Verify the README contract**

Run:

```powershell
$readme = Get-Content -Raw README.md
$required = @(
  '2.13.1', 'Synth, Sampler, and WAVE',
  'TWO MOTIFS', 'ONE + VAR', 'HIERARCHICAL', 'CALL / RESPONSE', 'OSTINATO',
  'AAAB', 'ABAB', 'ABBB', 'BUILD', 'ROTATE', 'MIRROR', 'OFF',
  'STEP · FORM · SONG · NEW', '680 deterministic Doctest cases',
  'released in 2.11.0', 'released in 2.13.0', 'released in 2.13.1'
)
foreach ($text in $required) {
  if (-not $readme.Contains($text)) { throw "README missing: $text" }
}
$forbidden = @('currently **2.12.2**', 'pending the next release',
               'tests/ runs 200 deterministic doctest checks', 'both engines',
               'NEW/TRIG')
foreach ($text in $forbidden) {
  if ($readme.Contains($text)) { throw "README retains stale text: $text" }
}
git diff --check -- README.md
```

Expected: no exception and no diff-check output.

- [ ] **Step 6: Commit the README refresh**

Run:

```powershell
git add README.md
git commit -m "docs: refresh README for 2.13.1"
```

Expected: one commit containing only `README.md`.

---

### Task 2: Refresh the Detailed Roadmap and Cross-Check Both Documents

**Files:**
- Modify: `docs/roadmap.md`
- Verify: `README.md`

**Interfaces:**
- Consumes: Task 1's public summary and the exact sequences/API behavior from
  `docs/superpowers/specs/2026-07-25-spotykach-form-song-split-design.md`.
- Produces: the authoritative detailed status record aligned with the README.

- [ ] **Step 1: Record the stale roadmap checks**

Run:

```powershell
rg -n "Last updated|verified only against|M5i|M5j|FORM|SONG|AAAB|MIRROR|lastBasis|principle" docs/roadmap.md
```

Expected: M5i and M5j exist, but the header still describes WAVE as the latest
completed work and no completed FORM/SONG entry explains the 2.13.1 behavior.

- [ ] **Step 2: Refresh the roadmap header and source list**

Add the FORM/SONG split spec to `Design intent`:

```markdown
  (`2026-07-25-spotykach-form-song-split-design.md`).
```

Rewrite `Last updated` as:

```markdown
- **Last updated:** 2026-07-25 (VCV 2.13.1; the FORM/SONG A/B arranger is
  complete; STRING remains the next planned engine milestone before M6).
```

Replace the inaccurate “verified only against the desktop offline renderer”
reminder with:

```markdown
> **Reminder:** the portable engine is exercised by the desktop offline
> renderer and the live VCV Rack host. Selected CPU workloads have real Daisy
> hardware measurements in `docs/bench/`, but the modulation-first firmware
> shell that runs the instrument on Spotykach hardware remains milestone M6.
```

- [ ] **Step 3: Add the completed status-table entry**

After the M5i row, insert:

```markdown
| **+ FORM/SONG** | Persistent A/B phrase snapshots with independent phrase-engine FORM and seven-mode SONG arrangement | ✅ **done** (engine, renderer, and VCV; released in 2.13.1; stable VCV parameter IDs and legacy patch migration) |
```

Update the milestone-order paragraph to say that FORM/SONG is a completed
cross-cutting melodic STEP capability and does not change the M5j → M5k → M5l
→ M6 order.

- [ ] **Step 4: Add the detailed FORM/SONG completion section**

Insert a new `### FORM/SONG phrase arranger ✅` section after the M5i WAVE
section and before `## Planned`. It must contain all of these facts:

```markdown
Melodic STEP lanes own two persistent full-pattern snapshots. FORM generates A
with one of TWO MOTIFS, ONE + VAR, HIERARCHICAL, CALL / RESPONSE, or OSTINATO;
B is derived as a related turnaround. SONG selects only which stored snapshot
plays and therefore consumes no random draw.
```

Document the exact arrangement table:

```markdown
| SONG | Sequence |
|------|----------|
| AAAB | `AAAB · AAAB · …` |
| ABAB | `ABAB · ABAB · …` |
| ABBB | `ABBB · ABBB · …` |
| BUILD | `AAAB · AABB · ABBB · AABB` |
| ROTATE | `AAAB · AABA · ABAA · BAAA` |
| MIRROR | deterministic Thue–Morse A/B selection |
| OFF | A plays and evolves continuously; B remains stored |
```

Then document:

- FORM, SONG, NEW, and effective STEPS changes apply only at melodic STEP phrase
  boundaries.
- SONG-only changes preserve both snapshots; FORM, NEW, and effective STEPS
  rebuild the pair and restart SONG.
- NEW also punches an active Sampler immediately.
- FLOW pauses arrangement position and snapshot evolution.
- The VCV PLAY row is `STEP · FORM · SONG · NEW`; TRIG was removed.
- The stable PRINCIPLE numeric slots became FORM, stable TRIGGER numeric slots
  became SONG, and NEW kept its IDs while moving outward.
- Old `principle` and beta `lastBasis` JSON states migrate to FORM; SONG starts
  at AAAB. Generated notes and live phrase position are not serialized.
- Verification includes
  `host/render/scenarios/demo_song_aaab.json`,
  `host/render/scenarios/demo_song_modes.json`, deterministic helper/lane/host
  tests, the VCV panel guard, and the 2.13.1 release.

Link the approved split spec at the end of the section.

- [ ] **Step 5: Refresh adjacent release evidence**

In the M5 sampler detail, state that:

- M5h was released in 2.11.0;
- the contextual SOURCE control plus independent context-menu Detune was
  released in 2.13.0.

In the M5i detail, state that WAVE was released in 2.13.0. Do not change the
recorded QSPI size, digests, or cycle measurements.

- [ ] **Step 6: Run cross-document fact guards**

Run:

```powershell
$manifest = Get-Content -Raw host/vcv/plugin.json | ConvertFrom-Json
if ($manifest.version -ne '2.13.1') { throw 'manifest version drift' }
$readme = Get-Content -Raw README.md
$roadmap = Get-Content -Raw docs/roadmap.md
foreach ($doc in @($readme, $roadmap)) {
  foreach ($text in @('2.13.1', 'TWO MOTIFS', 'ONE + VAR', 'HIERARCHICAL',
                      'CALL / RESPONSE', 'OSTINATO', 'AAAB', 'ABAB', 'ABBB',
                      'BUILD', 'ROTATE', 'MIRROR', 'OFF',
                      'STEP · FORM · SONG · NEW')) {
    if (-not $doc.Contains($text)) { throw "document missing: $text" }
  }
}
foreach ($text in @('M5j', 'M5k', 'M5l', 'M6')) {
  if (-not $roadmap.Contains("| **$text**")) { throw "roadmap missing: $text" }
}
if ($roadmap -match '\\| \\*\\*(M5j|M5k|M5l|M6)\\*\\*.*✅') {
  throw 'planned milestone incorrectly marked complete'
}
rg -n "currently \\*\\*2\\.12\\.2\\*\\*|pending the next release|verified only against|NEW/TRIG" README.md docs/roadmap.md
if ($LASTEXITCODE -eq 0) { throw 'stale documentation text remains' }
python host/vcv/res/test_panel.py
git diff --check -- README.md docs/roadmap.md
```

Expected: manifest is 2.13.1, both documents contain the complete FORM/SONG
vocabulary, planned milestones remain planned, stale searches return no match,
the panel guard reports `PASS -- panel guards ok`, and diff check emits nothing.

- [ ] **Step 7: Review the final documentation diff**

Run:

```powershell
git diff HEAD~1 -- README.md docs/roadmap.md
git status --short
```

Expected: the diff changes facts and concise explanations only; it does not
restructure either document. Status contains only `docs/roadmap.md`.

- [ ] **Step 8: Commit the roadmap refresh**

Run:

```powershell
git add docs/roadmap.md
git commit -m "docs: bring roadmap through 2.13.1"
```

Expected: one commit containing only `docs/roadmap.md`.

- [ ] **Step 9: Run the final clean-tree verification**

Run:

```powershell
python host/vcv/res/test_panel.py
git diff --check HEAD~2..HEAD
git status --short
git log -3 --oneline
```

Expected: panel guard passes, diff check is silent, status is clean, and the
latest two commits are the focused README and roadmap updates.

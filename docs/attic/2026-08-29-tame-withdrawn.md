# TAME, withdrawn

**Withdrawn 2026-08-29, on a listening decision.** Not a technical failure: the
second version demonstrably worked, its tests were green, and the render-hash
gates never moved. Bastian listened to it at every setting it could reach and
said "höre ich ganz leicht aber alles nicht brauchbar" — I can just barely hear
it, and none of it is usable. That is a sufficient reason and the only one that
mattered.

Two attempts are behind this, both built and both merged on the same day.
`main` was wound back to `76bf752` (release 2.21.10), which is exactly the last
commit before the first TAME spec. Nothing TAME-related had ever been pushed,
so no public history was rewritten. Because the whole feature was one day's
work, `main` now carries **no trace of it at all** — not a roadmap row, not a
deleted file, nothing a later `git log` will stumble over. This note and the
tag are the only record.

## What it was

A master-bus resonance leveler, sitting in the master chain ahead of the
limiter, driven by one normalized `P_TAME` — the last entry in the parameter
enum, which is why removing it renumbered nothing and moved no render hash.
Engine default 0, a bit-exact bypass. It never had a panel home; the VCV
context menu was its only surface.

**v1, the parked-tone notch.** Two notches that waited for a resonance to stand
still — to *park* — before engaging on it. The idea was that a tone which holds
its pitch is the one that fatigues the ear, and a moving one is musical.

**v2, the spectral leveler.** A complete replacement. A 19-band third-octave
detector, decimated to `sr/4`, measuring how far each band stands above the
mean of its non-adjacent neighbours; the three largest excesses each get a
peaking cut of up to 9 dB; and the broadband level the cuts removed is handed
back as a makeup capped at 6 dB, so the block cannot degenerate into a volume
knob. Always regulating, never gated.

## What was measured

Kept here because the measurements outlive the block.

- **v1 never engaged on real playing.** Measured during the v2 spec work on
  `tame_moving_resonance.json`'s pre-TAME 0–90 s (8192-pt FFT, 2048-sample hop,
  continuity-tracked spectral peak): the dominant peak drifts at **1.98 oct/min
  median** (p90 32.46), and the longest run that ever stays under v1's
  0.5 oct/min engage threshold is **0.26 s** — against the 3 s v1's own attack
  needed before it opened. v1 sat in bit-exact bypass for the entire scene. It
  was not tuned wrong; the premise that a troublesome resonance parks was
  false for this instrument.

- **v2 works, and that is the problem.** Measured 2026-08-29 on the same scene,
  three 40 s renders differing only in TAME, analysed over the last 20 s:

  | | broadband RMS | difference from the off-render |
  |---|---|---|
  | off | −35.53 dB | — |
  | shipped law, AMOUNT 1 | −35.50 dB | diff-RMS −54.5 dB → **19 dB below the program** |
  | extremes (0 / 3.0 / 24 / 18) | −35.51 dB | diff-RMS −40.8 dB → **5 dB below the program** |

  The shipped law moves single third-octave bands by about **±1.5 dB**, and the
  broadband level is unchanged *by construction* — the makeup cancels it. There
  is almost nothing there to hear. Driven to the extremes single bands move
  7 dB and it becomes clearly audible, but by then it is audible as damage.
  **This is the concept, not the calibration:** a level-preserving narrow-band
  leveler on a dense ambient master is inaudible while it behaves and ugly once
  it does enough to notice.

- **The makeup cap was a leash on the depth, not on the gain.** The saturation
  rule subtracts the makeup overshoot off *every* depth, so the makeup cap
  limited how deep the block was willing to cut at all. Measured (534.15 Hz at
  0.5 in 0.05 white noise, 10 s, seed 22222, AMOUNT 1): cap 0 → **0.15 dB** of
  cut and no makeup; cap 6 (shipped) → 6.15 dB of cut with the makeup pinned at
  6.00; cap 12 → 8.31 dB, makeup 8.17; above that the band's own 19.9 dB excess
  is the limit. So **a makeup cap of 0 was TAME switched off, not a cuts-only
  mode** — the opposite of what the name suggests, and worth knowing before
  anyone builds a compensated cutter again. On a loud resonance the shipped law
  therefore never reached its own 9 dB depth cap; only raising the ratio made
  the depth cap bind again (threshold 0, ratio 3: cap 9 → 9.00 dB,
  cap 24 → 21.99).

- **The detector had an HF blind spot that was never fixed.** Decimated Nyquist
  is 6 kHz against a two-pole anti-alias at 4.5 kHz, so 6–12 kHz content folds
  into the 50 Hz–4 kHz view at only 12–15 dB of attenuation and is read as band
  energy at the mirror frequency. Measured: a 10 kHz sine at 0.4 opens a ghost
  cut at 1965 Hz at the 9 dB cap. Bounded in practice by the 2 % relevance
  floor, and the shipped scenes never triggered it (6–12 kHz share: −76 dB on
  `tame_moving_resonance`, −34 dB on `sampler_slice_drums`, the brightest scene
  in the tree) — but the sampler takes arbitrary user material. The real fix
  was a steeper anti-alias or `kDecim = 2`, both of which change the CPU price.

- **RAM was priced, CPU never was.** `sizeof(spky::Tame)` is 1956 bytes against
  176 for the v1 block it replaced — **+1.78 KB inside `Instrument`**, which
  lives in DTCM on the Daisy. The `tame_idle` / `tame_engaged` bench rows were
  written and link cleanly on the ARM toolchain, but no board was ever
  attached, so the CPU cost of a 19-band decimated detector on the H7 is
  **unknown**. Anyone reviving this starts by running that bench.

## What was never established

- Whether **K = 3 cuts** is enough. Probe 2 measured 2.88 bands simultaneously
  more than 9 dB over their neighbourhood and 4.63 more than 6 dB; three was
  chosen at the low end of that on purpose, and the fourth and fifth offender
  were left untreated. No listening pass ever tested the difference.
- Whether **master placement** is right, or whether one deck dragging the other
  down needs per-deck treatment instead.
- Whether the makeup's lift is audible on sparse material. It was flagged as
  the honest cost — the reverb tail and the noise floor come up by as much as
  the cap — and never separately judged.

## What is in the attic, and what is only in the tag

Only this note is kept here. Both specs and both plans (2 872 and 12 847 words
for v2 alone) are complete in the tag and are best read from it directly rather
than duplicated.

Everything else lives only in the tag: `engine/fx/tame.{h,cpp}` (576 lines),
`tests/test_tame.cpp` (395 lines), the `fast_log2`/`fast_exp2` pair added to
`engine/fx/fx_util.h` for the detector and its 39 lines of tests, the two
`tame_*` render scenarios, the two bench workload rows, and the VCV context
menu.

**Two pieces are worth salvaging separately, neither TAME-specific.** The
`fast_log2`/`fast_exp2` minimax pair in `fx_util.h` is a general cheap-dB
utility with its own tests. And `host/render/scenarios/tame_moving_resonance.json`
is a deliberately built scene whose resonant emphasis *moves* — a useful test
signal for anything spectral, independent of what was measuring it.

## How to recover

**Tag:** `attic/tame-2026-08-29`, at `89768bb` — the merge of
`feat/tame-spectral-leveler`, which contains v1 as an ancestor.

```bash
git show attic/tame-2026-08-29:docs/superpowers/specs/2026-08-29-tame-spectral-leveler-design.md
git show attic/tame-2026-08-29:engine/fx/tame.cpp
git log --oneline 76bf752..attic/tame-2026-08-29        # everything withdrawn
git diff 76bf752 attic/tame-2026-08-29 -- engine/fx/fx_util.h
```

To bring the whole thing back: `git checkout -b tame-revival
attic/tame-2026-08-29`.

# FireFlow — Roadmap & Status

Living status document for the modulation-first instrument. The README carries
the summary table; this file tracks the detail: what each milestone contains, what
is actually built today, and what is still design-only.

- **Project status, 2026-08-04:** the project continues as a **standalone
  project** and is now called **FireFlow** — it was `spotymod` until this date,
  and releases up to and including 2.18.x carry that name. It is no longer part
  of the Synthux residency, and the plan to run
  the modulation-first engine on **Spotykach hardware is cancelled** — that port
  is not a milestone any more. The hardware milestone is now an instrument of
  its own, a **Daisy Patch Submodule prototype** (M6). The original Spotykach
  firmware tree stays in the repository, buildable, documented in
  `docs/upstream-firmware.md`; it is history, not a target.
  **2026-08-08:** the reducibility rule is retired — the hardware target is
  defined (60 HP, the full control set; envelope spec
  `docs/superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md`).
  The live rule is one-in-one-out: a panel change that adds a control names
  the control it removes.
- **Design intent:** the master design spec
  (`docs/superpowers/specs/2026-07-10-spotykach-modulation-first-synth-design.md`),
  the scale spec (`docs/superpowers/specs/2026-07-11-spotykach-scales-design.md`),
  the FX spec (`docs/superpowers/specs/2026-07-11-spotykach-fx-design.md`),
  the center-section spec
  (`docs/superpowers/specs/2026-07-12-spotykach-center-section-design.md`) and the
  ambient-reverb v2 spec
  (`docs/superpowers/specs/2026-07-12-spotykach-ambient-reverb-v2-design.md`), and
  the FORM/SONG split spec
  (`docs/superpowers/specs/2026-07-25-spotykach-form-song-split-design.md`).
  (These specs keep their original filenames, written while the project was
  still a Spotykach fork.)
- **Last updated:** 2026-08-04 (the project left the residency and the Spotykach
  hardware target, per "Project status" above; before that, 2026-08-03: VCV
  2.17.1; the BBD deck gained its own PITCH/
  tape-TIME surface and click-free dynamic stage changes, both released, and
  every state-dependent panel caption now comes from one generator table — see
  "BBD PITCH / tape TIME surface", "BBD dynamic-stage declick" and "VCV
  engine-aware captions" below).
- **Every CPU number below was measured on a Daisy Seed, and Seed numbers do
  not travel to the M6 target board.** Measured 2026-08-07 on a Daisy Patch
  Submodule, same profile/layout/optimization/transport: every one of the 24
  workloads is more expensive there, the decision workload
  `instrument_worst_bbd_dtcm` by **+0.53 to +0.80 percentage points** — more
  than either board's repeat band, so this is reproducible and not noise. Both
  boards still fit; the reserve falls from 2.97 to **2.17 points**, and the AXI
  neighbours `instrument_worst_bbd` (99.25 %) and `inst_bbd_engine_worst`
  (99.67 %) sit less than half a point under the block budget. **Do not quote a
  Seed figure for a submodule claim** — not with an asterisk, not as an
  approximation. See
  [Seed vs Patch Submodule](bench/2026-08-07-seed-vs-patch-sm.md); the cause is
  not measured and is deliberately not named there.
- **`engine/` runs on the target board — and it does not sound clean yet.**
  Measured 2026-08-08. `shell/` is the first firmware that compiles `engine/`
  ([its README](../shell/README.md) draws the line against `bench/` and the
  root firmware). On a Patch Submodule with audio on 3.5 mm jacks it makes
  sound, and `SHELL_CPU_PROBE=1` puts the operating point at **62.78 % avg /
  65.30 % max** (`sr=48000`, `block=96`, self-reported) — 35 points of room,
  consistent with the bench row `instrument_init` (66.58 / 77.96 %). But the
  output carries a constant-amplitude artifact **on the audio block rate**,
  28 dB above what the desktop render of the identical operating point has
  there, plus audible tearing. Four explanations are each ruled out by their
  own measurement — hardware, the audio input, `-O2` vs `-O3`, and the
  engine's 96-sample control raster (the artifact *moves* with block size, so
  it belongs to the block boundary). **A fifth, CPU overrun, is ruled out by
  the number above**, and a sixth — the engine's block arithmetic — by
  `tests/test_block_size_invariance.cpp`, which is green.
  **What it is instead: not the samples.** Forcing the callback to write
  nothing but zeros while the engine still runs leaves the artifact at
  *exactly* the same level (−59.5 dBFS both ways); filling the idle gap in
  each block with a `nop` loop drops it 7.5 dB. So it reaches the output
  without using the signal path, and it tracks how compute activity is
  distributed inside the block. **This is a finding about the carrier board,
  not about `engine/`** — nothing here argues for changing the engine, and
  Phase 1's own PCB should design decoupling and analog/digital supply
  separation deliberately rather than hope. Supply ripple, ground coupling
  and radiation are *not* separated; the clean next measurement is the same
  operating point on a Daisy Seed with its own audio output.
- **The engine's worst case does not fit on the target board.** Same capture:
  `instrument_worst` reads **102.27 % avg / 108.62 % max** on the submodule at
  O3, against the 960 000-cycle block. Whether that matters depends on whether
  it is a reachable operating point — the question Phase 0 Task 6 Schritt 8
  was written to answer, and it is still open.
- **CPU status: the selected O3 ITCM-hot benchmark passes; the production-shell
  timing boundary remains pending.** Both O3 DTCM+BBD benchmark repeats are
  below 100 % offline and in the real callback. This is the benchmark stop
  gate, not a claim that the production firmware shell is measured: M6 still
  needs an ITCM loader (or equivalent copy path) before the same placement and
  timing can be established there. The optimization history began with the
  2026-07-29 hardware run, which measured `instrument_worst` at **120.9 %** of
  the 960 000-cycle block
  inside a real audio callback, and `instrument_worst_bbd` at 133.2 %. The
  increase is entirely in the FX chain; voices, modulation plane and reverb are
  unchanged. A follow-up cost-curves round (`cd6dafd`, same day) priced the FX
  chain control-by-control and found `instrument_worst_bbd` at **132.79 %** —
  **32.8 points** over the gate — with only **≈17.2** authorised points
  reachable (wrapper-to-control-rate, the 24 kHz clock ceiling, STAGES) and
  **18.3** more sitting in the BBD model itself. The owner has now decided the
  next round, in order: **(1) move the FLUX wrapper's per-sample work to
  control rate, (2) re-measure, (3) collapse FLUX to mono per deck** — this
  supersedes the design spec's stereo refusal; `kFiltOrder` stays refused.
  Projected, that programme reaches only **≈106 %**, not under 100 %.
  **At that point, an optimization round was ordered before ZAP (M5k), PULL
  (M5l) and M6** — see
  "FLUX → BBD" and "FX cost curves" for the row-by-row evidence.
  **Update, 2026-07-29 (mono collapse measured):** step (1) landed at
  **125.24 %** and step (3), the FLUX-to-mono collapse, is now measured too —
  `instrument_worst_bbd` at **112.88 %** (branch `perf/flux-mono`; its content is
  on `main` — `bd346eb` is an ancestor — but it was fast-forwarded, so there is
  no merge commit to cite; both prior estimates undershot what steps 1 and 3
  actually returned).
  **The instrument is still over budget: 12.88 points remain to 100 %.** See
  "FLUX mono collapse" below for the full reading, including why this round's
  saving does not confirm the earlier instruction-cache hypothesis.
  **Update, 2026-07-29 (instrument ablated; the gap is not where it was
  assumed to be):** the unattributed fifth of the budget has now been measured
  directly rather than inferred by subtraction. It is **not contention between
  the decks, and barely glue** — 64 % of it sits *inside* the two decks, which
  the block rows were supposed to have priced. Part of that is rows set to
  operating points the gate does not use (FLUX, ~4.6 points); the rest is
  `Part`-internal work no row prices at all, and contention *within* a deck is
  still an open candidate for some of it — the round measured contention only
  between decks. There is **no instrument-level cut to be found**; the
  overshoot must come from inside the blocks. The same round also proved that
  code layout alone moves the gate by ~2 points at an unchanged checksum,
  which restates the target: **the overshoot is 10.77 points, not 12.88.**
  Branch `perf/instrument-ablation`, merged as `1e9ec05`. See
  "Instrument-level ablation" below.
  **Update, 2026-07-29 (deck interior split):** the gate reads **110.10** in
  this build, so **the overshoot is 10.10 points**. Of the 7.645 unpriced points
  per deck, **6.85 survives both operating-point corrections this round
  measured** — but it is *not* yet attributable to `Part` structure: ~2.29 of it
  is FLUX's own operating-point error, which the predecessor sized and this
  round did not re-price, and in-deck contention is still unseparated. Re-pricing
  FLUX comes before any further split. The round also reprices the "cut a voice"
  arithmetic: the **marginal** voice costs 3.97 points, not the 4.48 average, so
  one voice per deck is worth ≈7.9 points — 78 % of the gap, not all of it.
  Branch `perf/deck-interior-ablation`, merged as `57201ff`. See "Deck-interior
  ablation" below.
  **Update, 2026-07-30 (the remainder split; it is `Part`-level code):** the
  gate reads **110.51** in this build, so **the overshoot is 10.51 points**, not
  10.10. The predecessor's 6.85-point residue is now split, and the large
  majority of it is **code this project wrote, carrying no sonic cost**:
  `Part`-level code is **2.65–4.00 points per deck** (`pct_avg`–`pct_max`),
  **5.3–8.0 across the instrument** — 76 % of the `pct_max` gap, 82 % of the
  `pct_avg` one. **Contention was not the answer:** contention plus anything
  still unnamed is **0.84 per deck as measured, 1.1–1.3 once a known bias in
  the FX ladder is allowed for**, and that is an *upper bound*. FLUX's real
  operating point costs **+1.30** per deck, not the +2.29 carried forward from
  round 1. Next is a **fix round inside `Part::_control_tick`** — the first
  non-diagnostic round in this sequence — with the voice cut (≈7.9 points) now
  clearly the second option rather than the first. Branch
  `perf/remainder-split`, merged as `a93327e`. See "Remainder split" below.
  **Update, 2026-07-30 (the per-sample call boundary; the first round that changed
  `engine/`):** the gate reads **104.91** `pct_avg` / **108.69** `pct_max`, so
  **the overrun is 4.91 / 8.69**. The round-3 line above reads the same-source
  baseline as 110.51 `pct_max`; this round's own rebuild of that source reads
  **110.76**, a 0.25 `pct_max` shift at an unchanged `.text` and unchanged
  checksums (spec §9.12), so the "down from 6.50 / 10.76" this round quotes is
  measured against its own baseline and not against the line above. Making
  `Part`'s per-sample body inlinable at every call site recovered **about 1.6
  points, ± 0.5** — roughly a quarter to a third of the `Part`-level bucket — and
  it is **bit-exact**: all 23 bench checksums are identical across all four builds
  of the round and to round 3's, and the desktop render gates confirm it
  independently — `main` and the branch render byte-identical WAVs through
  `host/render` on clang/x86-64 without `-ffast-math`. This was the first round in
  the sequence that could have changed the sound, and it did not.
  **The mechanism does not account for the size.** §3.2's ISA argument predicted
  0.40–0.70 for removing the two per-sample prologue/epilogue pairs; the gate
  moved 1.59 and the four `Instrument` rows 1.38–2.71 — **2–4× what the ISA
  allows**, so the round's quantitative prediction is falsified in the favourable
  direction and **the majority of the saving has no identified cause** (spec
  §9.11). A candidate exists — registers held across the block — and it is
  unverified.
  Three findings outlast the points. **Exactly one row failed to move:**
  `deck_shell`, the row built to isolate one `Part`, at +0.20 — inside drift, so
  not a regression, but not a saving either — while `instr_part_1` and
  `instr_part_2`, which drive a `Part` from a bench loop just as `deck_shell`
  does, moved −1.16 and −1.25. **No mechanism separates them**, and an earlier
  reading that only `Instrument::process` rows improved is refuted by those two
  rows. What survives is narrower and still useful: **a row that isolates a
  component need not reproduce *how that component is called*.** Second, the same
  error one level up — **both hosts call `Instrument::process` with `n = 1`**
  (`host/render/main.cpp:102`, `host/vcv/src/Fireflow.cpp:637`), so the
  per-block amortisation the bench measured at `n = 96` does not exist on them;
  the removal of the two `Part` prologue pairs still does, the register-holding
  candidate does not, and **no figure is claimed for what the hosts see**. The
  firmware is not affected: `app.cpp:117` already sets `block_size = 96` and
  `ProcessAudio` passes it through, so M6 gets the full saving with nothing
  buffered — on block size the bench models the firmware faithfully, and it is
  the two development hosts the finding bounds. (That evidence is the *upstream*
  `app.cpp`, which is no longer the M6 target; the conclusion carries over only
  as long as the prototype shell also runs a 96-sample block, which is now this
  project's own choice rather than an inherited fact.) Third,
  **this bench cannot demonstrate a change smaller than about 0.5 points on the
  gate**, because every comparison it can make is cross-build and cross-build
  layout drift moves rows containing no `Part` by up to 0.49. **No further
  bit-exact `Part` round is recommended until that floor is lowered** — a
  recommendation that rests on the floor, not on the model, since the model's
  own prediction did not survive this round; the two
  remaining candidates — the block entry point and the voice cut (≈7.9 points) —
  are both listening decisions rather than checksum ones. Branch
  `perf/part-per-sample`, merged as `bc0ff78` after a two-reviewer pass — unlike
  rounds 1–3 this one changes `engine/`, so it landed after review rather than as
  evidence. See "Per-sample call boundary" below.
  **Update, 2026-07-30 (same-binary DTCM A/B):** placing only the 49,792-byte
  `Instrument` state in CPU-local DTCM, while both variants share the same AXI
  audio buffers and the exact same process callback, saves **0.64 CPU points
  average and 0.72–0.74 maximum**. Both runs return the identical checksum
  `483e8e82`; DTCM passes the pre-registered keep rule. The gate is still
  **104.64 % average / 108.75–108.77 % maximum** offline and
  **104.80 % / 108.74–108.81 %** in the real callback. Four voices per deck
  remain non-negotiable, so the next ordered round is ITCM. Branch
  `codex/perf-tcm-ladder`; evidence
  `docs/bench/2026-07-30-8702bc8-system.{md,csv}`.
  **Update, 2026-07-30 (ITCM audio hotset):** moving 41,984 bytes of the
  measured per-sample audio path into ITCM saves **at least 6.01 CPU points
  average and 6.09 maximum** across every AXI/ITCM pairing. All four gate
  captures return checksum `483e8e82`; four voices per deck and all DSP
  settings are unchanged. The retained gate is **98.65 % average /
  102.64–102.71 % maximum** offline and **98.81–98.82 % /
  102.78–102.79 %** in the real callback. A first probe at ITCM address zero
  produced silence and was rejected; reserving the first 256 bytes restored
  bit-exact output. ITCM passes its pre-registered keep rule, but the maximum
  is still over budget, so the next ordered round is `-O3`/LTO. Evidence
  `docs/bench/2026-07-30-d570e47-system-{axi,itcm-hot}.{md,csv}`.
  **Update, 2026-07-30 (O3/LTO compiler ladder):** select O3 and ship root
  `OPT = -O3`; do not add LTO or activate the dormant `C_USR_FLAGS`. Against
  O2's two 98.65 % / 102.66--102.71 % offline and 98.81 % /
  102.74--102.75 % callback runs, O3 measured 95.44--95.45 % /
  99.46--99.51 % offline and 95.63 % / 99.52--99.54 % in the callback. Its
  strict savings are **3.20 average and 3.15 maximum CPU points**. O2 retains
  gate checksum `483e8e82`; O3 is deterministic at `3ad2d267` and has four
  documented cross-mode checksum changes. The owner explicitly accepts
  changed sound, so deterministic cross-mode differences are no longer an
  automatic rejection; hashes do not prove inaudibility or perceptual
  equivalence. O2 and O3 retain `0xa400` and `0xd8e0` byte ITCM sections,
  respectively, with ITCM `LOAD` segments, all ten representative symbols in
  ITCM, and the `0xc280`-byte instrument store at `0x200005c8` in DTCM.
  O3+LTO is rejected before hardware: its ITCM section is empty, it has no ITCM
  `LOAD`, seven representative symbols are missing, and three are in AXI SRAM
  (its DTCM store remains at `0x20000528`). The O3 production ELF links without
  overflow, but it does not yet load the benchmark's ITCM section; production
  shell timing remains pending at that loader boundary. O3 passes the
  benchmark's offline and callback stop gate, so this benchmark ladder stops
  before half-rate reverb. Evidence:
  [O2 Markdown](bench/2026-07-30-1aa74ee-system-itcm-hot-o2.md),
  [O2 CSV](bench/2026-07-30-1aa74ee-system-itcm-hot-o2.csv),
  [O2 QSPI receipt](bench/2026-07-30-1aa74ee-system-itcm-hot-o2-qspi-verified.json),
  [O3 Markdown](bench/2026-07-30-1aa74ee-system-itcm-hot-o3.md),
  [O3 CSV](bench/2026-07-30-1aa74ee-system-itcm-hot-o3.csv),
  [O3 QSPI receipt](bench/2026-07-30-1aa74ee-system-itcm-hot-o3-qspi-verified.json),
  and the
  [O3+LTO static rejection](bench/2026-07-30-1aa74ee-system-itcm-hot-o3-lto-static-rejection.md).
  **Update, 2026-08-04 (signal-path regression; the gate holds, the ITCM
  placement does not):** the seventeen `engine/` commits since `19f7560` are
  now measured. The gate `instrument_worst_bbd_dtcm` reads **96.43 % `pct_max`
  offline / 96.69 % in the real callback** — it **fits**, with **3.57 points of
  margin offline** — at profile `regress`, execution layout `axi`,
  optimization `-O3`, tree `bd01608` (an ancestor of `main`), against a
  constructed baseline `6134b4f` (`bench/baseline-19f7560`: today's `bench/`
  with `engine/` rolled back to `19f7560`), so the two cycles differ in
  `engine/` and in nothing else. **The head of this block — that the selected
  O3 ITCM-hot benchmark passes — no longer holds for the image M6 needs:** on
  the `regress` profile (`system` + `bbd`, the first `itcm-hot` image ever to
  compile the `bbd` family) the `.itcm_audio_hot` section **overflows the
  64 KiB region by 832 bytes** at `-O3` on `main` and does not link (`-O3`
  alone had already left the baseline **32 bytes** free; the seventeen commits
  add 864 B), while the baseline links at `-O3` and fails the placement
  inspector instead, `spky::BbdLine::Process` having been inlined away
  entirely. At `-O2` both trees link and both still fail placement, because
  that symbol resolves to the bench-harness TU `build/workloads_bbd.o`, which
  `itcm_hot.lds` does not list. **The `system`-only `--itcm-hot` image the head
  claim was measured on (2026-07-30, `0xd8e0` = 55,520-byte hot section) was
  not rebuilt at today's `main`, so whether *it* still links is unmeasured.**
  The `itcm-hot` half of the planned matrix therefore did not run; both
  measured cycles are `axi`, which is also what an M6 firmware would get today.
  **The intended ITCM placement does not currently fit the optimization level
  that ships**, and that is an M6 problem this round does not solve. Three rows
  moved *down* — `inst_worst_deck_bus` (−2.38 against a 0.20 repeat band),
  `fx_grit` (−0.20 against 0.02, in a subsystem the round did not touch) and,
  more weakly, `instrument_worst` (−0.82 against a 0.58 band, the widest in the
  profile) — and **no mechanism was measured for any of them**; they are
  stated, not explained. The gate holds,
  so the attribution round is not forced; it still has no spec. Evidence:
  [signal-path regression](bench/2026-08-04-2101349-signal-path-regression.md),
  from captures
  [baseline](bench/2026-08-04-6134b4f-regress-axi-o3.md) and
  [main](bench/2026-08-04-bd01608-regress-axi-o3.md).

> **Reminder:** the portable engine is exercised by the desktop offline
> renderer and the live VCV Rack host. Selected CPU workloads have real Daisy
> hardware measurements in `docs/bench/`, but the firmware shell that turns the
> engine into a playable device remains milestone M6.
>
> **All hardware measurements in `docs/bench/` were taken on a Daisy Seed.** The
> M6 target, a Daisy Patch Submodule, carries the same STM32H750 at 480 MHz with
> the same SDRAM and QSPI, so the cycle counts and the ITCM/DTCM placement work
> carry over — but nothing has been measured on Submodule hardware, and no
> figure below should be read as if it had.

## Status at a glance

| Milestone | Scope | Status |
|-----------|-------|--------|
| **M1** | Portable engine foundation: SuperModulator, five lanes, `Instrument` API, desktop render host + tests | ✅ **done** |
| **+ Scales** | Pitch quantization (13 scales, SCALE/CHROM/FREE, root) layered onto the PITCH lane | ✅ **done** (engine + host; UI wiring deferred to M6) |
| **M1.6** | FX: per-part FLUX (tape echo) + GRIT (drive/reduce), shared ambient reverb, FX params as modulation targets | ✅ **done** (engine + host; UI wiring deferred to M6) |
| **M2** | Polyphonic synth voice (replaces the M1 test tone) | ✅ **done** (engine + host; UI wiring deferred to M6) |
| **M3** | Capture sequencer (freeze the PITCH lane into a loop) | ✅ **done** (engine + host; UI wiring deferred to M6) |
| **+ Entropy** | Looping S&H melody buffer; bipolar ENTROPY (erode / loop / grow) replaces EVOLVE | ✅ **done** (engine + host; switch mapping in M6) |
| **M4** | Center section — MORPH / COUPLE / DRIFT / SPOT / SETTLE | ✅ **done** (engine + host; UI wiring deferred to M6) |
| **M4.5** | Ambient reverb v2 — Oliverb port: Doppler SIZE, DECAY > 100 % bloom, TONE, DEPTH; shimmer + DaisySP-LGPL removed | ✅ **done** (engine + host; UI wiring deferred to M6) |
| **M4.6** | Dynamics — one-knob comp per part (glue → dense → pump, auto-makeup) + stereo-linked master limiter with MASTER DRIVE (delivers M6 engine delta 3 early) | ✅ **done** (engine + host; UI wiring deferred to M6) |
| **M4.8** | Reverb dry/wet — equal-power MIX at the master join + clear-on-sleep CPU bypass | ✅ **done** (engine + host; UI wiring deferred to M6) |
| **M4.9** | Reverb DIFFUSION knob (replaces DEPTH) — room density 0–0.9, weak line-mod coupling, full-wash first pass | ✅ **done** (engine + host; UI wiring deferred to M6) |
| **SYNC/COUPLE redesign** | One global SYNC switch (replaces per-part sync toggles), transport phase + rate ladder, zoned COUPLE (texture-only in grid world, grid-gravity zone in free world), VCV panel layout A, CLK/RST wired | ✅ **done** (engine + VCV host; spec `docs/superpowers/specs/2026-07-16-sync-couple-redesign-design.md`) |
| **M4.10** | Chord layer — COLOR knob, diatonic stacks, voice-leading, live FLOW surface | ✅ done (engine + hosts; hardware placement deferred to the reduction round — now the regrouping round, envelope spec 2026-08-08) |
| **+ COLOR-MOTION** | MOTION becomes COLOR's third destination — bipolar additive with a zero-gate, density varies per note | ✅ **done** (engine only; no new surface) |
| **Bench** | Bench firmware — DWT cycle measurement of the engine, nine DaisySP candidates and SRAM-vs-SDRAM buffer access on real hardware | ✅ **done** (`bench/`, results in `docs/bench/`) |
| **M5a** | Sampler — the texture deck: engine + render host (granular cloud, live resampling) | ✅ **done** (engine + desktop host; VCV wiring is M5b) |
| **M5a — generous ranges** | SIZE, PITCH, resonance, MOTION scatter and record-feedback ranges opened from M5a's conservative first pass, each ceiling chosen from measurement rather than habit; listening renders produced for the ranges to be judged by ear | ✅ **done** (engine + render host; spec `docs/superpowers/specs/2026-07-21-sampler-generous-ranges-design.md`; merged) |
| **M5b** | Sampler on the panel — ENG remap, REC pad, WAV load/save, patch persistence, factory sample | ✅ **done** (VCV host; merged) |
| **M5c** | Morphagene-style control surface — DENS (runtime grain overlap), SCAN (running playhead with a real dead zone), NEW/punch, LEN, and contextual SOURCE (`TIMB` Synth, `FRAME` Wave, `ORG` Sampler, `MATL` Body, `DRIVE` BBD); per-part Detune A/B is a constant context-menu spread; SIZE made live downward so turning LEN back shortens what is already sounding | ✅ **done** (engine + VCV host; spec `docs/superpowers/specs/2026-07-21-sampler-morphagene-controls.md`; as of "VCV engine-aware captions" below, MELODY drives Sampler SCAN only — `set_variation` no longer also fires on a Sampler deck) |
| **M5d** | Slice-groove -- recorded/loaded material becomes a live slice map; STEP plays clocked slices, MOTION moves from ordered playback to free traversal, and SIZE sets the slice length | ✅ **done** (engine + render host; released in 2.9.0) |
| **M5e** | Sampler FEEL -- COLOR becomes material-derived accents in STEP, while preserving the synth COLOR/chord path | ✅ **done** (engine + VCV panel; released in 2.9.0) |
| **M5f** | Sampler cloud dispersion -- COLOR in FLOW spreads grain pitch through detune and octave layers, with no new control or RNG draw | ✅ **done** (engine + VCV panel; released in 2.10.0) |
| **M5g** | Sampler playability pass -- FLOW-to-STEP snaps the entering deck to the transport, SCAN is linear below its knee (up to 4x), and MOD reaches the read position quadratically | ✅ **done** (engine; released in 2.10.1) |
| **M5h** | Per-deck ROOM mix -- each deck has its own equal-power dry/send mix into one shared reverb; the central REV_MIX is removed | ✅ **done** (engine + VCV panel; released in 2.11.0) |
| **Sampler bench + grain cap** | The texture deck priced on the Daisy (7 rows + 6 ablations), and the grain-count spike it exposed capped via `kSpawnHeadroom` | ✅ **done** (`bench/workloads_sampler.cpp`, `docs/bench/2026-07-22-*`) |
| **CPU hunt round 3** | Three measured removals: libm `sinf` on the reverb send per sample, a filter computing five outputs to use one (`engine/util/svf_lp.h`), and control-rate libm re-run on unchanged inputs | ✅ **done** (engine; released in 2.8.0) |
| **M5i** | WAVE — four-voice PPG-style wavetable part engine | ✅ **done** (engine/core, renderer, and VCV; 65,024-byte mapped-QSPI bank; `wave_2x4` 308497 / 312180 cycles in hardware run 1, below SYNTH and budget) |
| **+ FORM/SONG** | Persistent A/B phrase snapshots with independent phrase-engine FORM and seven-mode SONG arrangement | ✅ **done** (engine, renderer, and VCV; released in 2.13.1; stable VCV parameter IDs and legacy patch migration) |
| **Mod grid lock** | In STEP the four texture lanes stop owning a clock and follow the deck's integer step count; the lane ratios become cycle lengths (4/6/8/12/16 at STEPS = 8), TIDE stretches slot counts, and DRIFT, EVOLVE, SPOT and float drift can no longer push a lane off the grid | ✅ **done** (engine + VCV; released in 2.13.2; spec `docs/superpowers/specs/2026-07-25-mod-lane-step-grid-lock-design.md`) |
| **M5j** | BODY — one-voice-per-deck resonator part engine, morphing string → metal → bell, with a sympathetic excitation bus | ✅ **done** (engine, renderer and VCV; `body_2x4` 295078 / 295724 cycles, 30.7 % of the block, inside the spec's 29–32 % prediction and below SYNTH; released in 2.14.0) |
| **FLUX → BBD** | FLUX's interpolating tape echo replaced by a bucket-brigade delay model — the clock rate *is* the delay time, so **RATE bends stored pitch only transiently, while it moves, and only with feedback up — held steady, a bucket-brigade line's pitch is unity at any clock rate**, and `FXT_FLUX_TIME` is a genuine chorus/vibrato modulation lane (STAGES has since moved off FLUX entirely — it is the BBD deck's own PITCH-lane base, captioned `BEND` on the panel; see "BBD PITCH / tape TIME surface" below) | ✅ **done** (engine, renderer and VCV; RATE/STAGES/`FXT_FLUX_TIME` confirmed by ear, DRIVE diagnosed and fixed but awaiting re-listening; **over CPU budget** — measured 2026-07-29, `instrument_worst` 120.9 % anchored and `instrument_worst_bbd` 133.2 %, against a 960 000-cycle block; the FX chain is where the increase sits, see below) |
| **BODY playability** | Three fixes from the first extended ear pass: the bow follows the note instead of droning at a fixed 200 Hz, a continuously driven resonator no longer runs decades above its own struck level, and FILTER's left half fades evenly instead of falling off a cliff | ✅ **done** (engine; released in 2.15.0; two measured items left open — see below) |
| **BBD part engine** | `ENGINE_BBD` (ENG state 4) is a fifth, voiceless part engine — no synth voices, just a stereo `BbdEcho` pair fed by audio-in and/or the neighbour (via a new audio-rate cross-deck bus), driven through the five modulation lanes (SOURCE→DRIVE, PITCH→clock, SIZE→division, MOTION→FEEDBACK, LEVEL→dry/wet MIX). The same work reverts FLUX — the echo effect on every engine, including this one — from its BBD model back to a plain tape echo, so the "FLUX → BBD" row above no longer describes FLUX's current mechanism; the bucket-brigade device that survives is this engine, on the BBD deck only | ✅ **done** (engine + VCV; spec `docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md`; released in 2.17.0; measured on real hardware at the engine's own worst case, `inst_bbd_engine_worst` 94.16 avg / 98.39–98.56 max % of the 960 000-cycle block at `-O2` — fits, but no `-O3` measurement of this row exists; see below — **superseded 2026-08-04: the `-O3` measurement now exists.** On the `regress` profile, layout `axi`, `-O3`, this row reads **92.91 % `pct_max`** on the constructed `19f7560` baseline and **96.91 %** on `main` (+4.00 against a 0.19 repeat band). Evidence: [signal-path regression](bench/2026-08-04-2101349-signal-path-regression.md)) |
| **BBD PITCH / tape TIME surface** | `STAGES_A/B` leaves the FX box and takes over the shared VOICE `ATK` slot as the BBD deck's own pitch control, visible only while that deck is BBD (`ATTACK` is hidden there and stays reachable as a `Freeze Attack` context-menu slider); the vacated FX slot gets a new knob driving `FXT_FLUX_TIME` geometrically from ×0.25 through ×1 to ×4, distinct from RATE's tempo-synced division | ✅ **done** (VCV; spec `docs/superpowers/specs/2026-08-02-vcv-bbd-pitch-flux-time-surface-design.md`; released in 2.17.1; the two new captions, `PITCH` and `TIME`, were renamed `BEND` and `MULT` by "VCV engine-aware captions" below) |
| **BBD dynamic-stage declick** | `BbdLine` keeps one continuous full-ring write history behind every stage-count change and crossfades the old and new read taps over a fixed 16-tick smoothstep, removing the index-reset impulse the previous immediate resize produced when COLOR/MOTION modulates a line's stage count | ✅ **done** (engine; spec `docs/superpowers/specs/2026-08-03-bbd-dynamic-stages-declick-design.md`; released in 2.17.1; fixed-stage output stays bit-identical, COLOR = 0 stays bit-identical left/right) |
| **VCV engine-aware captions** | Every state-dependent panel caption now comes from one `DYNAMIC_CAPTIONS` generator table instead of hand-written special cases: BODY gets honest VOICE words (`HIT`/`DAMP`/`CHAR`/`EXCIT`/`BRITE`) and BBD gets its own (`TAIL`/`TILT`/`FEED`/`LOSS`); the FX-box word collisions are resolved by renaming FLUX `RATE`→`DIV`, FLUX `TIME`→`MULT`, per-deck `ROOM`→`SEND`, `MASTER_DRIVE`→`PUSH` and BBD `PITCH`→`BEND` (collision with the orbit's `PITCH` eyebrow); the GRIT mode pad now shows its own state, `SAT`/`CRSH`, instead of the word `GRIT` it collided with; MELODY drives Sampler `SCAN` only, no longer also `set_variation`; the permanently-printed `SCAN`/`LEN` second words are deleted from the static plate | ✅ **done** (VCV host + generator; spec `docs/superpowers/specs/2026-08-03-vcv-engine-aware-captions-design.md`; branch `vcv-engine-aware-captions`, not yet merged to main or released) |
| **M5k** | ZAP — monophonic percussion part engine | ⬜ **planned** (spec ready; not implemented) |
| **M5l** | PULL — chord gravity between the two decks | ⬜ **planned** (spec ready; not implemented) |
| **M6** | Hardware prototype — Daisy Patch Submodule bring-up: panel, controls, LEDs, CV/gate I/O, preset persistence | ⬜ planned (**panel undefined; the existing shell spec is superseded** — see below) |

Milestone order follows the design spec's build order (audible first, hardware
last). The scale layer was inserted after M1 because it only touches the PITCH
lane's output stage and needed no new engine. M1.6 sits before M2 so that
M2–M5 build on the final signal chain (part FX + reverb sends) from the start
instead of rewiring it later; the M1 test tone is enough to hear and verify
the effects in the renderer. FORM/SONG is a completed cross-cutting melodic STEP
capability and does not change the M5j → M5k → M5l → M6 order. M5k and M5l are
the remaining engine-level milestones that can be completed without the target
hardware; M6 follows them as the hardware bring-up. What changed with the move
to a standalone prototype is M6's content, not its position: it is still last,
and it is now the milestone that has to define its own device rather than fit an
existing one.

## Done

### M1 — Portable engine foundation ✅

The complete modulation core, host-independent (`engine/` has no libDaisy
include), audible via the desktop renderer.

- **SuperModulator per part** (`engine/mod/super_modulator.*`) — one macro
  surface (RATE, SHAPE, PROBABILITY, SMOOTH, RANGE, DEPTH) over **five
  independent lanes**, each at a fixed musical ratio of the master rate.
- **Modulation lanes** (`engine/mod/lane.*`) — two run modes, **FLOW**
  (smooth LFO) and **STEP** (clock-quantized sequences), plus the bipolar
  **ENTROPY** control (erode / loop / grow — see the entropy sequencer
  entry below). Own phase, own RNG stream, own probability dice per
  lane. Continuous waveform morph (sine → triangle → ramp → pulse → S&H) in
  `engine/mod/waveforms.h`; RANGE mapping (off → unipolar → bipolar) in
  `engine/mod/range.h`; deterministic RNG in `engine/mod/rng.h`.
- **Part + engine interface** (`engine/parts/*`) — `Part` routes lane outputs to
  targets and exposes `pitch_cv()` / gate. `engine_iface.h` is the sound-engine
  boundary; `test_tone_engine.h` is the M1 placeholder engine (110–880 Hz over
  the 0..1 pitch contract).
- **Public API** (`engine/instrument.h`) — `init(sample_rate)`, normalized
  `0..1` setters, `process(in, out, size)`. Single boundary for both hosts.
- **Desktop render host** (`host/render/`) — scenario JSON → 16-bit stereo WAV +
  `mods.csv` (every lane, pitch CV, gate). Vendored `nlohmann/json`.
- **Tests** (`tests/`, doctest) — lane STEP quantization, ENTROPY loop/grow/erode
  determinism, per-step dice, RANGE mapping, RNG determinism, SuperModulator,
  Part routing, WAV writer, scenario parsing.

### Scale quantization ✅ (extends M1)

Melodies sit in a musical scale by default, with chromatic and free modes for
drift experiments. Engine + host complete; the hardware gestures are specified
but not yet wired (that is UI work, i.e. M6).

- **Quantizer module** (`engine/pitch/quantizer.h`) — near-stateless
  `SCALE / CHROM / FREE`, 12-bit scale mask, root in semitones, ±15-cent
  hysteresis at raster boundaries, ~30–50 ms change slew (FREE is a bit-exact
  passthrough).
- **13 scales in three groups,** dark → bright inside each: modes (Aeolian,
  **Dorian (default)**, Mixolydian, Lydian), pentatonics (Hirajoshi, Pygmy,
  minor pentatonic, Kumoi, major pentatonic), exotic (Phrygian, Hijaz,
  harmonic minor, whole tone). Boot default: Dorian, both parts SCALE.
- **Placement** — last stage of `Part::target_value(LANE_PITCH)`, so SMOOTH
  glides step through scale notes and ENTROPY grows or erodes the melody. `pitch_cv()` is
  the single quantized source of truth for engine, CV out, and the future
  capture sequencer.
- **Host** — `set_scale`, `set_quant_mode`, `set_root` scenario actions; demo
  scenarios `dorian_melody.json`, `pentatonic_melody.json`, `dorian_vs_drift.json`.
- **Tests** (`tests/test_quantizer.cpp` + Part integration) — scale mapping,
  hysteresis, change-slew settle time, FREE passthrough, root shift, and that
  `pitch_cv()` only lands on allowed scale degrees in SCALE mode.

### M1.6 — FX ✅

Per-part FLUX (tape echo *— redesigned as a bucket-brigade delay, see
"FLUX → BBD" below*) + GRIT (drive/reduce) ported from the original
firmware, a shared ambient reverb *(core replaced in M4.5 — Oliverb port,
shimmer removed)*, and 5 curated FX parameters per part as first-class modulation
targets — a second tap on the same five lanes (fixed 1:1 lane → target
mapping, `engine/fx/part_fx.h`).

- **`engine/fx/`** — `fx_util.h` (XFade/SoftSwitch ports), `grit.*`, `flux.*`,
  `reverb.*`, `part_fx.*`. DaisySP is now an `engine/` dependency (portable
  C++; still no libDaisy). Memory is injected (`FxMem`): echo buffers +
  reverb object — static on desktop, SDRAM on Daisy (M6).
- **Signal flow** — per part: engine → GRIT → FLUX → FX MIX (equal-gain
  dry/wet); post-FX send × REVERB SEND (equal-power) into the shared room,
  which joins the master after the part mix. Bypass is bit-exact.
- **Host** — 10 new scenario actions, 5 FX columns per part in `mods.csv`,
  demo scenarios `dub_delay.json` / `ambient_wash.json`.
- **UI (M6)** — FLUX/GRIT pads, hold-layers, ALT gestures per the FX spec.

### M2 — Polyphonic synth voice ✅

4-voice trigger-driven synth engine (`engine/synth/`) is the boot-default
part engine; `TestToneEngine` stays selectable (`set_engine` — tests, A/B
reference).

- **Voice** — 2× polyblep `MorphOsc` (single phasor, continuous
  sine→tri→saw→pulse, detune in cents) + sub sine → DaisySP `Svf` lowpass →
  exponential AD/ADS envelope (retrigger-from-level) → equal-power pan with
  slow deterministic per-voice drift. Audio-path sine is the shared
  polynomial `fast_sin` (`engine/util/fast_sin.h`) — no libm `sinf` in the
  voice path; drift + envelope coefficients update at control rate
  (96-sample blocks). CPU-budget constraints from the spec.
- **Engine** — round-robin allocation, oldest-steal with retrigger-from-
  level; STEP = plain AD notes; FLOW = sustaining-last-voice drone (sustain
  0.7, pitch continuously follows the quantized PITCH target; entering FLOW
  with no sustaining voice auto-triggers — the drone promise). Targets:
  TIMBRE (morph), FILTER (60 Hz–14 kHz exp), PITCH
  (latched at trigger, 110·8^p), MOTION (pan fan ±1/±0.5 × width + drift),
  LEVEL (smoothed master gain).
- **Tempo-coupled envelopes** — attack/decay are ratios of the master
  modulation cycle (defaults 2 % / 1.5×, attack floor 2 ms, decay clamp
  50 ms–20 s), edited via `set_voice_attack/decay/resonance/sub/detune`
  (VOICE layer; hardware gestures in M6).
- **Part / Instrument** — `set_engine(EngineId)` with a click-free
  SoftSwitch crossfade; `set_cycle`/`set_flow` forwarding (default no-ops on
  `IPartEngine`); `trigger_manual` (PLAY tap); `active_voices` / `voice_env`
  introspection.
- **Host** — 7 new scenario actions; `voices` + `v0..v3` CSV columns; demo
  scenarios `overlapping_voices.json` (the master spec's M2 acceptance demo)
  and `flow_drone.json`. Existing scenarios pinned to `ENGINE_TEST_TONE`.
- **UI (M6)** — VOICE edit layer gestures (PLAY-pad hold), PLAY-tap manual
  trigger wiring, engine-switch gesture.

### M3 — Capture sequencer ✅

Per-part freeze of the PITCH lane's last cycle into a replayable loop
(`capture_now` / `set_replay` in scenarios; `ALT + SEQ` on hardware, M6).
Capture swaps the lane's *source*, not the system.

- **CaptureLoop** (`engine/mod/capture.h`) — header-only double buffer
  (2 × 192 slots): the lane rolls its pre-smooth target + trigger pattern
  into the ring every generative sample; `capture_now` freezes the last
  full cycle. A dumb buffer — `ModLane` owns all slot timing, so record
  and replay share one phase→slot mapping.
- **ModLane replay** — recorded fired slots become the boundaries; live
  PROBABILITY dice thin the frozen loop (fail = hold), SMOOTH / RANGE /
  TUNE / quantizer stay live, ENTROPY is ignored on the replaying lane.
  Recording never touches the RNG — bit-determinism preserved.
- **SuperModulator / Instrument** — one loop per part, wired to the PITCH
  lane; `capture_now` / `set_replay` / `replaying` / `loop_valid`.
- **Host** — `capture_now`/`set_replay` scenario actions, `a_cap`/`b_cap`
  CSV columns, demos `capture_loop.json` + `capture_pentatonic.json` /
  `capture_duet.json`.
- **UI (M6)** — ALT+SEQ gesture, ring step-pattern display with playhead.

### Entropy sequencer ✅ (reworks the lane core, post-M3)

Listening to M3 renders showed STEP + S&H melodies were unusable note
salad (one random value per cycle, or pure noise per step). Now every
lane owns a looping 32-slot step buffer (seeded at init — a melody exists
from cycle one), and the LOOP/EVOLVE toggle became one bipolar **ENTROPY**
control:

- **0 — LOOP**: the melody repeats exactly (the LOOP contract, finally
  honored in the S&H zone).
- **> 0 — GROW**: fired steps mutate via a root-gravity random walk (small
  intervals common, leaps rare); the phase/shape/rate walk runs scaled by
  entropy.
- **< 0 — ERODE**: fired steps pull toward the root, note by note, down to
  a single tone; the walk settles back toward neutral.
- Mutation only on fired steps (suppressed steps hold note and slot);
  `shape_value()` returns the S&H operand exactly at SHAPE = 1; scenario
  action renamed `set_evolve` → `set_entropy`; demos
  `demo_step_melody.json` (entropy showcase) + `entropy_duet.json`.
- **UI (M6)** — panel switch 2 becomes ERODE / LOOP / GROW.

### M4 — Center section ✅

One `Center` class owns MORPH / COUPLE / DRIFT / SPOT, computed at control
rate (one 96-sample block) and wired through narrow `ModLane` /
`SuperModulator` / `Part` hooks — no engine-level branching.

- **MORPH** — fader, equal-power A↔B blend of both the dry mix and the
  reverb send (supersedes the M1.6 pre-morph-send rule: a fully
  morphed-away part injects no new reverb, only its already-committed tail
  rings out); boot default 0.5, smoothed.
- **COUPLE** — ALT + fader, a Kuramoto phase-locked loop between the two
  parts' master rates: mutual phase pull (no phase jumps) with a ±5-octave
  ([1/32..32]) rate clamp, locking in 1–2 cycles at couple = 1 and staying
  quiet at low couple. Superseded by the SYNC/COUPLE redesign below: the
  per-part `sync_mode` anchor is gone, replaced by a single global SYNC
  switch and zoned grid gravity / hard lock against the transport.
- **DRIFT** — SPOT-hold + fader, one shared Ornstein-Uhlenbeck "weather"
  walk (τ ≈ 45 s, bounded) feeding six hardcoded taps (rate ± ½ octave,
  shape ± 0.15, detune ± 25 cents, per lane); `set_drift` is smoothed.
- **SPOT** — per-lane random kick: a permanent ±½-cycle phase jump plus a
  ±0.35 shape jolt that decays back to 0 (τ ≈ 1.5 s); replay-immune (a
  captured loop ignores kicks).
- **SETTLE** — panic glide: DRIFT and the weather walk ease to 0, EVOLVE
  walk states re-center, and any open SPOT kick decays early; COUPLE and
  MORPH are untouched.
- Zero-effect invariant preserved: couple = 0 and drift = 0 reproduce every
  pre-existing lane/CSV column bit-for-bit, modulo the MORPH mix now being
  equal-power instead of the old unity-sum placeholder (a level-only
  change at the boot default).
- **Host** — five scenario actions (`set_morph`, `set_couple`, `set_drift`,
  `spot`, `settle`), five `mods.csv` global columns (`morph`, `couple`,
  `drift`, `weather`, `phase_err`), demos `couple_lock.json` (COUPLE
  convergence/anchor) + `weather_spot.json` (DRIFT weather + SPOT + SETTLE).
- **UI (M6)** — MORPH fader, ALT + fader for COUPLE, SPOT-hold + fader for
  DRIFT, SPOT tap gesture.

### M4.5 — Ambient reverb v2 (Oliverb port) ✅

The shared room becomes a playable instrument (spec:
`docs/superpowers/specs/2026-07-12-spotykach-ambient-reverb-v2-design.md`): vendored
MIT Oliverb core (Clouds Parasite) under `third_party/oliverb/` — float32,
48 kHz, deterministic. SIZE rescales the delay reads live (Doppler tail
warp), DECAY crosses 100 % at ~0.9 of its travel into a soft-limited bloom
(cap 1.05), TONE is the in-loop damping, DEPTH chorus-modulates the lines.
`set_shimmer` is gone (API + scenario action). Removing `ReverbSc` +
`PitchShifter` drops the DaisySP-LGPL dependency — the build is MIT-clean.
Facade, injection point (`FxMem`), and wet-only routing unchanged; the M6
shell places the ~130 KB object in SDRAM as before.

### M4.6 — Dynamics ✅

One-knob compressor per part (`engine/fx/comp.*`, end of the PartFx chain
BEFORE the reverb send tap — dry and send are compressed and auto-gained
together, so full-wet patches profit fully) plus a stereo-linked master
limiter (`engine/fx/limiter.h`, stmlib gain-riding recipe, exact
bit-transparency below the −1 dBFS knee) at the Instrument mix stage with
MASTER DRIVE (pre-gain 1–4×). The comp knob is a loudness knob first:
threshold/ratio/release/auto-makeup ride one macro (glue ~2:1 at a third,
dense ~5:1 at two thirds, 10:1 + 350 ms pumping at the top). API:
`set_comp(part, n)` / `set_master_drive(n)`, boot defaults 0/0. Delivers
the M6 shell spec's "Engine delta 3" (master soft-clip) early.

The by-ear pass reshaped the gain computer (amendment in the same
spec): a **post-comp envelope ceiling** (−8 dBFS) stops the
auto-makeup from grinding program peaks into the master limiter,
downward gain moves act in ~0.5 ms, and the attack tightens with the
knob (5 ms → 2 ms) — quiet material still gets the full makeup, so the
loudness intent survives. Showcases: `comp_pump.json` (verification arc)
and `m7_bloom.json` (dev-diary render — one strummed Am7 into a long
room, the comp knob resurrects the dying tail). Spec:
`docs/superpowers/specs/2026-07-13-spotykach-dynamics-design.md`, plan:
`docs/superpowers/plans/2026-07-13-spotykach-dynamics.md`. The knob-map
suggestions recorded here (GRIT layer SMOOTH → COMP per side, FLUX-layer TUNE
→ MASTER DRIVE) were written for Spotykach's panel and do not carry over to
the M6 prototype; they stand only as a record of the intent.

### M4.8 — Reverb dry/wet mix ✅

- `set_reverb_mix` (0..1): equal-power dry/wet crossfade at the master join —
  dry = cos(m·π/2), wet = sin(m·π/2) with exact endpoints, 10 ms one-pole
  glide. Default 0.25, chosen by ear: keeps the dry level with a leaner room
  than the old fixed mix (wet −8.3 dB; the old balance sits at MIX 0.5,
  −3 dB overall — MIX multiplies on top of the internal wet trim). The wet
  path keeps its internal −8 dB bloom-headroom trim; the send input is
  untouched by MIX, so the tail character never changes while turning.
- MIX 0 is a true bypass: the wet gain fades out, the room is cleared once
  (`AmbientReverb::clear()` — buffer + loop filter state, params survive) and
  `process()` is skipped. Oliverb CPU drops to zero; a self-oscillating bloom
  is genuinely killed. Any MIX > 0 wakes into a clean, empty room
  (`reverb_asleep()` exposes the gate for the M6 UI).
- Hosts: VCV `REV_MIX` knob (shared center strip, default 0.25), render
  action `set_reverb_mix`.

### M4.9 — Reverb DIFFUSION knob ✅

- `set_reverb_diffusion` (0..1) replaces `set_reverb_depth`: AP coefficient
  `0.90·n` (0 = discrete slap echoes, boot 0.7 → 0.63 ≈ the old stock 0.625
  room, 1.0 = dense wash that melts attacks), line modulation weakly coupled
  (`(0.05 + 0.20·n)·450` samples — motion rides the knob, never dominant).
  DEPTH is gone ersatzlos, like shimmer in M4.5.
- Motivation: at full MIX/DECAY/SIZE the Oliverb feeds the freshly diffused
  input straight to the output taps, so attacks punched through the wash;
  more diffusion smears the first pass (A/B verified by ear, 2026-07-14).
- Hosts: VCV `REV_DIFF` "DIFF" knob (same panel slot/param id as DEPTH),
  render action `set_reverb_diffusion`; `ambient_wash` migrated.

### SYNC/COUPLE redesign ✅

Landed 2026-07-16 (spec: `docs/superpowers/specs/2026-07-16-sync-couple-redesign-design.md`,
plan: `docs/superpowers/plans/2026-07-16-sync-couple-redesign.md`). One
global **SYNC** switch replaces the two near-invisible per-part `Free / Sync /
Triplet` toggles and gives COUPLE a clear split between "grid world" and
"organic world":

- **Transport phase** — new beat/bar phase accumulator in the engine core,
  advanced from tempo BPM at control rate; `Instrument::clock_pulse()` lets a
  host report external CLK edges (re-measures BPM and aligns downbeat phase,
  previously rate-only), and RST is now actually read and reset-aligns the
  phase.
- **Rate ladder** — 17 speed-sorted musical divisions (`engine/mod/divisions.h`)
  replace the old 9-division per-part Sync table; SYNC-on RATE snaps to the
  ladder, SYNC-off RATE stays continuous 0.02–30 Hz.
- **Grid world (SYNC on)** — both parts' PITCH lanes servo-lock to the
  transport (rate + downbeat phase); COUPLE governs only the four non-pitch
  mod lanes (1.0 lockstep, lower = independent Kuramoto breathing); DRIFT's
  rate tap likewise skips the pitch lane, its detune tap stays global.
- **Organic world (SYNC off)** — unchanged pairwise Kuramoto below COUPLE 0.5;
  0.5–1.0 fades in "grid gravity" (the coupled target additionally pulled
  toward the nearest musical division of tempo, rate and phase); COUPLE = 1.0
  converges to the same hard lock as grid-world SYNC, so flipping SYNC at full
  COUPLE is seamless.
- **VCV host** — panel layout A: TIME group (SYNC / TEMPO / COUPL) under
  MORPH, per-part sync toggles removed, CLK phase-align + live RST wired,
  division-aware RATE tooltip. Param relayout breaks saved 2.0.x patches
  (expected — version bump to **2.1.0**).
- Full engine suite green (216 cases, 0 skipped) and both `ambient_wash` /
  `demo_step_melody` renders verified clean post-landing; VCV Rack play test
  and audio listening pass deferred to a human (see dev log).

### M4.10 — Chord layer (COLOR knob) ✅

Spec: `docs/superpowers/specs/2026-07-17-chord-layer-color-design.md`, plan:
`docs/superpowers/plans/2026-07-17-chord-layer-color.md`. One new per-part
knob, **COLOR** (0..1), turns the single-note engine into a chord instrument
without a mode: 0 is today's one note, higher settings add tones (fifth-below,
then root/third, then seventh, then a ninth color tone at full), zones voiced
additively and crossfaded with hysteresis so the knob never flutters on an
edge.

- **Engine** — chord tones are built from the active scale's quantizer mask
  (diatonic stacking: root + every second scale note), so chord quality is
  emergent from the scale, never selected, and always harmonizes with the
  other part's melody. Voice-leading picks the chord lay that minimizes
  total semitone movement from the previous chord (common tones stay put).
  Per-note gain scales ~1/sqrt(n) so density changes color, not level.
- **Live surface** — in FLOW, COLOR acts continuously on the sounding voices
  (bloom in / collapse out, click-free) rather than latching at the next
  trigger; in STEP the chord is built at trigger time. `Instrument::set_color(int,
  float)` is the host entry point.
- **Hosts** — VCV `COLOR_A`/`COLOR_B` big knobs (panel: free corner between
  the macro orbit and the center strip), render action `set_color`
  (`chord_bloom.json` demo scenario). Default 0 keeps the init patch's
  single-note sound bit-identical.
- Full engine suite green, zero pre-existing failures; the three Task 1
  baseline scenario hashes match post-landing (COLOR-0 bit-identity proven);
  `chord_bloom.json` renders deterministically. VCV Rack play test and
  audio listening pass deferred to a human. Hardware panel placement is
  explicitly deferred to the upcoming reduction/macro round (per the
  standing hardware-reducibility constraint; now the regrouping round —
  envelope spec 2026-08-08).

### COLOR as a MOTION target ✅ (extends M4.10)

Spec: `docs/superpowers/specs/2026-07-18-color-motion-target-design.md`, plan:
`docs/superpowers/plans/2026-07-18-color-motion-target.md`. COLOR was the one
pitch-layer macro nothing could modulate — a stab in a phrase always carried
the same chord density. MOTION becomes COLOR's third destination, alongside
the pan fan and drift amount it already drives, so density now varies per
note instead of tracking wherever the knob was last left.

- **Engine** — bipolar additive with a zero-gate, not multiplicative: `Part`
  now owns the COLOR knob and adds MOTION's ±1 output, scaled by MOD and a
  `kColorMod` constant (0.2), gated in over the first 1% of knob travel
  (`kColorGate`). In STEP each trigger samples whatever density is current at
  that instant; in FLOW the existing zone-hysteresis path reads a moving
  color as a bloom/collapse. `COLOR = 0` forces the gate to 0 and `MOD = 0`
  zeroes the swing, so both invariants hold structurally rather than by
  tuning: the chord layer's bit-identity guarantee and today's-behaviour
  default survive untouched.
- **No new surface** — no panel control, no scenario action, no parameter id;
  disabling works through MOTION's existing target-active flag.
- The three COLOR-0 chord-layer baselines (`ambient_wash`, `demo_step_melody`,
  `demo_density_sweep`) render byte-identical pre- and post-landing;
  `chord_bloom.json` sweeps COLOR to 0.95 at the boot MOD of 1.0 with MOTION
  active, so its reference render was re-cut to the now-breathing chords.

### Bench ✅

Plan: `docs/superpowers/plans/2026-07-18-bench-firmware.md`. `bench/` is a
standalone Daisy app, never shipped and never linked into `spotykach.bin` —
it boots the engine alone on a Daisy Seed and reads DWT cycle counts around
fixed workloads, then prints a Markdown/CSV pair over semihosting. The
shipping firmware (`main.cpp`, `app.cpp`, `src/`, `engine/`, the root
Makefile) is untouched by its presence; Step 1 of the bench plan re-proves
that on every run.

The headline numbers are no longer estimates — they come from a real Daisy
Seed at 480 MHz, 48 kHz, block 96 (`docs/bench/2026-07-19-6e38090.md`):

- The full instrument at its worst case (8 voices, COLOR 4-note on both
  parts, all FX on, high diffusion, echo at max, GRIT in Drive mode on both
  parts — GRIT Reduce runs ~2.2 points higher and is not the case this number
  covers) costs 92 % (avg) / 98 % (max) of the block budget offline and 93 %
  (avg) / 98 % (max) anchored inside a real audio callback. **The max is under
  budget for the first time, and the max is the gate** — so the bench now
  emits *"the 2×4 architecture fits"* on its own. Four optimization passes
  have taken ~58 points off the anchored max, from 156 %. The margin is
  **2.3 points**, far thinner than the saving the last cut returned from its
  larger call site: one unbudgeted feature can spend all of it — GRIT Reduce
  alone would eat almost the whole of it. The newest pass is the **fast-tanh
  cut** (spec `docs/superpowers/specs/2026-07-19-fast-tanh-design.md`), worth
  **8.1 points** on the anchored max as first measured at `87f3538`
  (103.89 % → 95.77 %; the committed baseline report's reading — the spec's
  own Context section quotes 104.06 % from the same run's second capture
  repeat, reconciled in the spec's Outcome) against a predicted ~11 — **it
  underdelivered, and cleared the gate only because just ~4 points stood in
  the way.** A follow-up correctness fix then gave **1.9 points back**: the
  `|fast_tanh| ≤ 1` bound the design rests on turned out not to hold (the
  clamp constant sat 4.1e-7 above the true root, and the guard test's grid was
  8.6× wider than the violation band), so the bound is now enforced on the
  return value instead of the threshold. That is why the shipped figure is
  97.69 % and not 95.77 %. **The listening pass cleared it and the cut is on
  `main`, shipped as Spotymod 2.6.0** (tag `v2.6.0`). Both named risks were
  checked by ear and neither was audible: the echo bloom at maximum feedback,
  where the new hard clamp caps the limit cycle marginally harder than `tanh`'s
  asymptote did, and master DRIVE at high settings, where the same curve error
  is scaled onto the summed master bus and grows with drive. One human listening
  session, not a measurement — the error arithmetic in the spec is what stands
  as the durable record of its size.
  Before it, the **mod-plane control-rate cut**
  (`docs/superpowers/plans/2026-07-19-mod-plane-control-rate.md`), worth
  **~19 points** against a predicted 17–19: the plane fell 253 254 → 56 667
  cycles (−77.6 %), landing at the top of that predicted range, not past it —
  only the avg (20.8) exceeded it, and the avg is not the gate. Before it, on the
  same branch, the **Part-glue control-rate cut**
  (`docs/superpowers/plans/2026-07-19-part-glue-control-rate.md`) was worth
  ~19.6 points — the glue fell 112 820 → 18 664 cycles per part, 83.5 %,
  against a predicted 70–85 % — alongside four **FX hygiene cuts** whose
  largest more than halved the reverb (`oliverb_solo_sram` 186 673 → 91 420
  cycles). What is left on the ranked list below is no longer needed to clear
  the gate; it should be held as margin rather than spent.
- **The drifted attribution is re-baselined and the flag is cleared — but the
  family predicts rank order better than magnitude.** `part_glue_flow` had
  halved a second time at `94468af` (19.86 % → 9.97 %) even though the
  Part-glue cut had already landed at `c7f6a73`, which looked like instability.
  At `87f3538` it reads 9.98 % — a 0.07 % move across an independent build,
  same checksum. Two consecutive runs agree, so that second halving was a
  one-time re-attribution when the 96-sample raster tick landed, not drift.
  Every row the fast-tanh cut should not have touched held (`grit_drive_solo`
  identical to the cycle, `synth_4_voices` within 1 cycle, the `micro_*`
  controls flat), and the checksum column confirms only FLUX and driven-limiter
  rows changed hash. The family can be trusted again — with one lesson from
  spending it: its two fast-tanh predictions were **both high** (8 → 5.8, 3 →
  1.5), because a ceiling books a whole call site's cost to the one call it
  contains. Treat the ranking as reliable and the absolute figures as upper
  bounds. Two further cautions: an `inst_worst_no*` difference carries a ~10 %
  composition-and-layout error band (in-context reverb moved 11 289 cycles with
  no reverb code change), and a solo-row saving is an upper bound on what the
  composed instrument returns (FLUX gave back 3.56 points in context against
  the 5.80 its solo rows predicted).
- Two caveats on the recent runs. `echo_short_sram` / `echo_short_sdram` are
  **not comparable** to `9be5df9` — the delay-time one-pole moved out of
  `EchoDelay` into `Flux`, so those rows no longer carry the per-sample
  slew (they are comparable `94468af` → `87f3538`, which is where the
  fast-tanh halving above is read). And the earlier figures below (the
  ablation closure, the mod-plane history) are stated against `9be5df9` and
  were not re-derived since.
- **The unaccounted gap is now attributed, and the go/no-go conclusion did
  not move.** Component rows summed to ~120 % of budget while
  `instrument_worst` measured ~159 % (avg) — a ~375k-cycle (39-point) gap
  with no named owner. Fourteen `abl` bench rows
  (`docs/superpowers/plans/2026-07-18-bench-ablation-family.md`) close it on
  paper: **Part glue** — `Part::process`'s per-sample lane-target/quantizer/
  ChordBuilder machinery, isolated for the first time — is the single
  largest owner at ≈112820 cycles/part (≈12 % of budget each, ≈23 % for
  both parts); the **driven master limiter** costs 27698 cycles (≈3 %)
  whenever `MASTER DRIVE` defeats its bit-exact bypass; running the
  **reverb** in-context costs 42076 cycles (≈4 %) more than its isolated
  cost — a real composition/cache-coupling tax that FLUX's own coupling
  term does *not* show (that one came back negative); and **CHOKE**, once
  actually measured instead of assumed, *reduces* worst-case cost by
  ≈94293 cycles (≈10 %) — it is not a worst-case axis. Inside FLUX,
  `std::tanh` in `EchoDelay` is now confirmed the dominant per-sample cost
  (≈60 % of FLUX's isolated per-part delta over the FX-none shell (fx_flux_sdram − fx_none)), ahead of its
  SDRAM memory tax (≈5 %) and its remaining bpf/interpolation/SetDelay
  machinery (≈35 %). Summing every named term (mod plane ×2, Part glue ×2,
  engine ×2, full PartFx ×2, in-context reverb, driven-limiter tax) against
  `instrument_worst` closes to within 7.8 % of budget (5.2 % of
  `instrument_worst`) — under the 10-point threshold, so the residual is not
  treated as a missing owner; it's attributed to the additive-stacking
  approximation used for "full PartFx" (no row yet measures GRIT+FLUX+COMP
  running together in one part) plus compounded row-to-row jitter. The 2×4
  verdict did not move *at the time* — at `9be5df9` `instrument_worst` still
  sat within jitter of every prior measurement — the closure's contribution
  was naming where the cost lives instead of leaving 39 points dark; it is
  the four cuts it enabled that eventually flipped the verdict, four commits
  later. Ranked cut list for the next spec
  (predicted savings, largest first, all as % of the 960k-cycle budget):
  Part glue to control rate — **SPENT 2026-07-19** (`c7f6a73`, spec
  `2026-07-19-part-glue-control-rate-design.md`): measured **19.6 %**, not the
  23 % ceiling, because 18 664 cycles/part are a mandatory per-sample
  remainder. The `SMOOTH=0` risk case largely dissolved — the engine had
  never seen those intermediate values, since it reads at its own 96-sample
  control tick; what remained was fire timing, answered with an event refresh
  rather than a finer raster; reverb composition-coupling investigation (≈4 % already paid,
  mechanism unexplained) plus a speculative, unmeasured half-rate-reverb
  hypothesis (order ≈10 %, needs its own ablation before it's trusted); fast
  tanh in `EchoDelay` — **SPENT 2026-07-19** (`87f3538`, spec
  `2026-07-19-fast-tanh-design.md`): measured **5.8 %** against a ceiling of
  ≈8 %, the echo kernel itself more than halving (`echo_short_sram` 21 154 →
  8 752 cycles) but returning less once composed into the instrument; fast tanh
  in the master limiter's `shape()` — **SPENT 2026-07-19**, same commit:
  measured **1.5 %** against ≈3 %, half the prediction, because the ceiling had
  booked the whole driven-limiter tax to `tanh` when most of what remains is
  gain-riding arithmetic. Together **8.1 points** on the anchored max, which
  cleared the 100 % gate; and two hygiene
  one-liners already known from source — `PartFx` rev-send `std::sin` →
  `fast_sin` (measured ≈1 %, ceiling ≈2 %) and the double pitch
  quantization in `Part::process` (ceiling ≈2 %, likely much smaller, no
  dedicated bench row yet) — both change engine output and belong with a
  listening pass, not a silent merge. Full arithmetic and the complete
  ranked list:
  `docs/superpowers/plans/2026-07-18-bench-ablation-family.md`, `## Outcome
  (2026-07-19)`.
- The modulation plane was originally measured at about 33 % of the block
  budget against the design spec's 4–6 % estimate — wrong by roughly six
  times, and the single most actionable finding in the table. The cause,
  found by decomposing the plane into per-lane bench rows (plan+spec
  `docs/superpowers/specs/2026-07-18-mod-plane-optimization-design.md`):
  `waveforms.h`'s `wave_sine` called libm `std::sin` once per sample per
  lane, even though the audio path itself had used the cheap `fast_sin`
  polynomial since M2 — the modulation path had simply never been moved
  over. Switching it to `fast_sin` brought the plane down to about **26 %**
  of the block budget; still measured from AXI SRAM rather than the firmware's
  zero-wait DTCMRAM, so the figure stays conservative. The spec's own prediction (a fall to about 17 %)
  **fell short** — the sine really was the single biggest line item, but
  its estimated per-lane cost was too high, so the realized saving came in
  at about 40 % of what the spec predicted. A smaller residual cost
  (`fast_sin` itself running closer to ~50 cycles than the ~10–15 its own
  header claims on this call site) and the ten lanes' own per-sample
  machinery — independent of any waveform call — now account for most of
  what is left; see the spec's Outcome section for the full breakdown —
  DONE 2026-07-19 (texture lanes on the 96er raster, spec
  2026-07-19-mod-plane-control-rate-design.md) — **SPENT, measured at
  `94468af`: the plane fell to 5.90 % (avg) / 6.11 % (max) of the block
  budget, 253 254 → 56 667 cycles, a 77.6 % cut worth ~19 points on the
  anchored worst case (predicted 17–19).** Where the earlier `wave_sine`
  cut undershot its spec, this one overshot: moving the lanes off the
  per-sample path removed not just their waveform evaluation but the
  per-sample lane machinery that the previous Outcome had identified as the
  irreducible remainder. `super_mod_5lanes` fell 13.23 % → 2.86 % on the
  same run.
  The mod plane's output changed deliberately as part of this cut (`fast_sin`
  is not bit-identical to libm `sin`), so `renders/` byte-identity is no
  longer treated as a regression gate for it — re-cut references are the
  accepted price, per the spec's own decision.
- The grain-read proxy — the access pattern M5's granular engine will lean
  on — costs about 5.3× in SDRAM against the same reads in SRAM. That is
  the sampler's exposure, measured before the sampler exists. **Caveat**
  (carried from the texture-deck spec): this is a directional floor-risk
  number measured over a 64 KB window, not a constant to carry forward —
  and it is a lower bound twice over, because the RNG draw and division per
  grain are common to both the SRAM and SDRAM rows and dilute the ratio,
  and a 64 KB SDRAM window is partly cached.

Numbers, method and the full nine-candidate DaisySP table live in
`docs/bench/`; how to run the bench yourself is in `bench/README.md`.

### M5 — Sampler: the texture deck ✅
A granular cloud as a third engine behind `engine_iface` — not a second
melodic instrument, but the room the synth part plays in. Grain scheduler
(16 slots/part, chord-locked, MOTION as an order→chaos scatter macro) over a
ported `Buffer` record core; live IN L/R recording is the primary path, WAV
loading the second, and the cloud plays while recording. The voice row
(ATK DEC FILT RES SUB SOURCE) provides the original common surface. Panel cost:
the existing ENG pad plus one REC button per part.

Shipped in three passes — **M5a** engine + render host, **M5b** the VCV panel
(ENG/REC, WAV load/save, patch persistence, factory sample), **M5c** the
Morphagene-style surface (DENS, SCAN, NEW, LEN). Released in **2.8.0**. In
**2.13.0**, the visible SOURCE control became contextual — `TIMB` on Synth,
`FRAME` on WAVE, and `ORG` on Sampler — and independent per-part Detune A/B
arrived in the context menu.

The deck then received five completed follow-up milestones before hardware work:

- **M5d -- Slice-groove:** live `SliceMap` analysis while recording or loading; STEP triggers slices on the phrase clock, MOTION traverses the pool, and SIZE shapes the slice duration. Released in **2.9.0**.
- **M5e -- FEEL accents:** on sampler decks, COLOR becomes FEEL in STEP and derives accents from the recorded material; synth COLOR remains unchanged. Released in **2.9.0**.
- **M5f -- Cloud dispersion:** on sampler decks in FLOW, COLOR controls the existing per-grain detune/octave spread without adding a random draw or a new surface. Released in **2.10.0**.
- **M5g -- Playability pass:** entering STEP locks only the entering deck to the running transport; SCAN gets a usable linear lower range (maximum 4x), and MOD's source-position influence is quadratic. Released in **2.10.1**.
- **M5h -- Per-deck ROOM:** the former shared master reverb mix is replaced by one ROOM control per deck. Both decks still feed one shared Oliverb room. Released in **2.11.0**.

Spec: `docs/superpowers/specs/2026-07-18-sampler-texture-deck-design.md`
(supersedes the older Deck/Vox adapter spec, whose slice-player trigger model
predates the melody rework, the groove engine, CHOKE and the chord layer),
extended by `2026-07-21-sampler-morphagene-controls.md`.

Two things a later reader should not have to re-derive:

- **Grain count is capped** (`kSpawnHeadroom`, `engine/sampler/sampler_config.h`).
  MOTION jitters the spawn *interval* by ±75 % while grain *length* stays
  fixed, so short intervals stacked grains — the live count wandered 5..11
  where DENS asked for 8, and per-block cost is linear in it. The cap also
  bounds how far tape mode may stretch a grain; those are the same resource,
  and the value is an ear decision with its table at the constant.
- **The deck's worst case still exceeds the block budget** (107 % max against
  the synth's 94 %). The overrun is steady FX-chain load, not the cloud:
  dropping either FLUX or the reverb from that patch clears it. See
  `docs/bench/2026-07-22-8668367.md`.

### M5i — WAVE ✅

WAVE is the completed four-voice PPG-style wavetable part engine behind the
existing part-engine interface. Its `WaveEngine` core shares the SYNTH voice
semantics while its deterministic 16-frame, 7-mip int16 bank supplies the
digital-glassy scan. The renderer accepts `"wave"`; VCV ENG exposes
Synth → Sampler → Wave while retaining saved values 0 and 1.

The committed generated bank is 65,024 bytes (32,512 int16 samples), linked in
`.qspiflash_data` at `0x90040000`. Hardware run 1 measured `synth_2x4`
340347 / 346106 and `wave_2x4` 308497 / 312180 average/maximum cycles; run 2
measured 340342 / 346105 and 308503 / 311962. WAVE is no slower than SYNTH in
either run and its maximum is below the 960,000-cycle block budget. Both
accepted captures contain identical unique 68-row sets and checksums and
report the byte-verified QSPI payload digest `ac234ac7f7540ed5cd0e8b8496b84fca8084a3b2c05cc513aa4dad8ed811fc27`.

Scenario: `host/render/scenarios/wave_formant_sweep.json`
(`wave_formant_sweep.sha256`). Spec:
`docs/superpowers/specs/2026-07-18-wave-engine-design.md`. Hardware evidence:
`docs/bench/2026-07-25-8c5f2e1.md` and `docs/bench/2026-07-25-8c5f2e1.csv`.
Released in **2.13.0**.

### FORM/SONG phrase arranger ✅

Melodic STEP lanes own two persistent full-pattern snapshots. FORM generates A
with one of TWO MOTIFS, ONE + VAR, HIERARCHICAL, CALL / RESPONSE, or OSTINATO;
B is derived as a related turnaround. SONG selects only which stored snapshot
plays and therefore consumes no random draw.

| SONG | Sequence |
|------|----------|
| AAAB | `AAAB · AAAB · …` |
| ABAB | `ABAB · ABAB · …` |
| ABBB | `ABBB · ABBB · …` |
| BUILD | `AAAB · AABB · ABBB · AABB` |
| ROTATE | `AAAB · AABA · ABAA · BAAA` |
| MIRROR | deterministic Thue–Morse A/B selection |
| OFF | A plays and evolves continuously; B remains stored |

FORM, SONG, NEW, and effective STEPS changes apply only at melodic STEP phrase
boundaries. SONG-only changes preserve both snapshots; FORM, NEW, and effective
STEPS rebuild the pair and restart SONG. NEW also punches an active Sampler
immediately. FLOW pauses arrangement position and snapshot evolution.

The VCV PLAY row is `STEP · FORM · SONG · NEW`; TRIG was removed. The stable
PRINCIPLE numeric slots became FORM, stable TRIGGER numeric slots became SONG,
and NEW kept its IDs while moving outward. Old `principle` and beta `lastBasis`
JSON states migrate to FORM; SONG starts at AAAB. Generated notes and live phrase
position are not serialized.

Verification includes `host/render/scenarios/demo_song_aaab.json`,
`host/render/scenarios/demo_song_modes.json`, deterministic helper/lane/host
tests, the VCV panel guard, and the 2.13.1 release.

Approved split spec:
`docs/superpowers/specs/2026-07-25-spotykach-form-song-split-design.md`.

### M5j — BODY ✅

BODY is the completed resonator part engine behind the existing part-engine
interface. One continuous morph along a physical axis — harmonic partials
(Karplus string) → stretched (dispersion) → freely inharmonic (24-mode bank) —
with both structures live, so the mod lanes drive the material. A playable
exciter on RESO, and a sympathetic excitation bus fed by the part's own FLUX
echo, the other deck and the audio input, selected per deck in the VCV context
menu. `SynthEngineT` was lifted from the oscillator to the voice type; the
SYNTH and WAVE reference renders held byte-identical throughout.

**It ships at one voice per deck, not four.** The hardware gate (below) priced
a BODY voice at 1 395 cycles/sample against a four-voice SYNTH deck's 1 764, so
one BODY voice per deck is both what fits and cheaper than the part it
replaces. The spec's chord layer survives in altered form: `trigger_chord`
keeps the root and drops the rest, while COLOR still reads the chord's quality
and bends the mode bank's inharmonicity with it (`chord_character`), so the
chord controls move the material even though one note sounds.

**Measured on the Seed** (`docs/bench/2026-07-27-c3c0cdb-body.md`, profile
`body`, two runs agreeing):

| row | avg cyc | max cyc | % of block |
|---|---:|---:|---:|
| `body_2x4` | 295 078 | 295 724 | 30.7 % |
| `body_2x4_string` (MATL 0) | 295 220 | 296 402 | 30.8 % |
| `inst_body_worst` | 900 266 | 980 090 | 93.8 % / 102.1 % |
| *for comparison* `synth_2x4` | 338 694 | 343 751 | 35.3 % |

The spec predicted 280 000 – 311 000 cycles for `body_2x4`. The estimate held.
`SRAM_EXEC` finished at 187 168 B of 262 880 B (71.2 %); the string delay lines
are in SRAM, not SDRAM, as the cache analysis required.

`body_2x4_string` measures the same as `body_2x4` because both structures run
every sample regardless of MATL — it is not the ablation the plan expected, and
the component rows below are where the string/bank split actually lives.

**One regression the branch introduces, deferred by decision.** The Task 9 tape
tap runs per sample on every deck with FLUX or GRIT engaged, including decks
carrying no BODY. Measured by mutation on hardware it costs 51 607 cycles —
**5.4 % of the block** — and is what moves `instrument_worst` from 96.9 % to
101.9 % on its maximum, flipping that capture's generated verdict from "fits"
to "does not fit".

**Update: FLUX's BBD redesign is now done** (see "FLUX → BBD" below), and the
tap stays, on narrower grounds than "it will be benched fresh." The tap
itself — `PartFx::tape_tap()` / `Part::_fx.tape_tap()`
(`engine/fx/part_fx.cpp`), a single-sample read of `Flux`'s own echo output
into the sympathetic excitation bus — was never the mechanism the BBD
redesign removed; that was the cross-deck tap *bank* (`derive_offsets`,
`spky::TapBank`), deleted because a BBD has no read pointer to offset into.
This tap reads whatever `Flux::process` already leaves behind each sample,
which the BBD rewrite still produces, so it is architecturally unaffected by
the redesign. What is genuinely still open is the number: no hardware
measurement of `instrument_worst_bbd` exists yet (its row was added to the
`system` profile by `bench/workloads_system.cpp` in Task 11, but running it
needs a Daisy Seed and an ST-Link that were not available during this
work), so whether the tap's 51 607-cycle/5.4 % cost
against the *old* tape echo still holds against the BBD engine is unmeasured
either way. Gating the tap at control rate remains open, now pending that
fresh number rather than pending the redesign itself. Full measurement table
in the bench capture.

Supersedes STRING (`2026-07-18-string-engine-design.md`), which ruled out
modal/bell territory on a cost figure that measured `daisysp::Resonator`'s
per-sample coefficient math rather than modal synthesis itself.

Scenarios: `host/render/scenarios/body_strum.json`, `body_bow.json`,
`body_sympathetic.json` (no hash gates yet — the tuning pass is open). Spec:
`docs/superpowers/specs/2026-07-26-body-resonator-engine-design.md`.
Hardware evidence: `docs/bench/2026-07-27-c3c0cdb-body.md` and `.csv`.

<details>
<summary>How the hardware gate got here — the voice-count decision and two superseded runs</summary>

**Hardware gate: passed at `kVoices = 1`**
(`docs/bench/2026-07-26-1ec4429-body.md`, profile `body`, two runs agreeing).

| row | avg cyc / block | per sample |
|---|---:|---:|
| `body/mode_bank_24` (24 modes, coefficients moving) | 86 762 | 904 |
| `body/ks_string_pair` (two `daisysp::String`) | 187 251 | 1 950 |
| `body/ks_string_pair_port` (two `spky::KsString`) | 44 741 | **466** |

Per voice per sample, `(mode_bank_24 + ks_string_pair_port)/96 + 25`:
**1 395 cycles**. Against the ladder that is `kVoices = 1` — and one voice per
deck costs 2 790 cycles/sample, **27.9 % of the block**, inside the ~32 %
envelope every rung of that ladder was drawn around.

The comparison that settles it: a SYNTH deck at four voices (`synth_4_voices`)
costs 1 764 cycles/sample. A BODY deck at one voice costs 1 395. **BODY is
cheaper than the part it replaces**, so the instrument gets no more expensive
by carrying it.

At two voices per deck it does not fit: 5 580 against `synth_2x4`'s 3 535 would
push the worst case past 100 %. One voice per deck is the answer, which is the
trade the design already chose — spec §7 keeps 24 modes and spends polyphony
first.

With the string fixed, **the mode bank is now the expensive half** — 904 of the
1 395, 65 % of a voice. That is where any future voice count would have to come
from, and the mode count is a user decision, not a cost decision.

Open, and a design question rather than a measurement: the spec's control
mapping assumes polyphony it will not have. The chord layer and stab
humanisation need a ruling at one voice per deck before Phase 3 starts.


**First run: blocked** (`docs/bench/2026-07-26-0010b45-body.md`).

| row | avg cyc / block | per sample |
|---|---:|---:|
| `body/mode_bank_24` (24 modes, coefficients moving) | 86 761 | 904 |
| `body/mode_bank_24_static` (same bank, coefficients held) | 81 688 | 851 |
| `body/ks_string_pair` (two `daisysp::String`) | 196 556 | 2 047 |

Per voice per sample, by the plan's formula
`(mode_bank_24 + ks_string_pair)/96 + 25`: **2 976 cycles**. The ladder's
bottom rung is 810, so this is 3.7× past even `kVoices = 1`. One BODY voice
alone costs 30 % of the whole block budget; the two the design needs (one per
deck) cost 60 %, on top of an instrument already anchored at 97 %.

**Where the cost is, and it is not where the design assumed.** The modal bank
is fine: 24 modes cost 904 cycles/sample, and the control-rate coefficient
caching works exactly as intended — holding the parameters still saves only
53 of those, so `_recompute()` is ~6 % of the bank, not the per-sample tax
that made STRING rule modal territory out. The Karplus pair is the problem at
2 047 cycles/sample, **1 024 per string** — one `daisysp::String` costs 1.8×
an entire SYNTH voice (`synth_1_voice`, 558 cycles/sample: two MorphOsc, sub,
SVF and envelope). STRING's spec called Karplus-Strong "structurally cheaper"
than SYNTH. Measured, it is the opposite, and the modal half it rejected is
the affordable one.

**The nonlinearity was measured and acquitted**
(`docs/bench/2026-07-26-f58644f-body.md`). `body/ks_string_pair_nolin` runs the
same pair with `SetNonLinearity(0)` — no allpass stretch line, no dispersion
noise, no curved bridge — and costs 173 956 cycles against 196 794. The entire
nonlinearity is **119 cycles per string per sample, 11.6 %**. Per voice that is
2 741 instead of 2 976: still 3.4× past the ladder.

What remains is 906 cycles/sample for a Karplus-Strong stripped to a delay-line
read, a DC blocker and a one-pole — arithmetic worth tens of cycles. The rest is
`String::ProcessInternal` recomputing its parameter block **every sample, in
both nonlinearity branches**: two `powf`, one `atanf`, one filter
`SetFrequency`. Our own `abl/micro_powf` row prices `powf` at 198 cycles, so the
two `powf` alone account for ~400 of the 906.

This is the same defect as `daisysp::Resonator` — the one whose cost made the
STRING spec rule modal territory out. Both halves of BODY were priced on
libraries that recompute coefficients per sample; the modal half was already
fixed here (`engine/body/mode_bank.h`, control-rate `_recompute()`), and the
Karplus half was not — until `engine/body/ks_string.{h,cpp}`, which is what the
third run measures.

</details>

### FLUX → BBD ✅

FLUX's interpolating tape echo (M1.6) is replaced by a bucket-brigade delay
(BBD) model: the combined BBD-and-filters model of Holters & Parker
(DAFx-18), as ported from `jpcima/bbd-delay-experimental` (BSL-1.0; full
attribution and license text in `THIRD_PARTY.md`, credits in `CREDITS.md`).
The class, its name and its public interface are unchanged — `Flux`,
`SoftSwitch`, `engaged()`, the bit-exact off path, `set_rate`/`set_mix`/
`set_feedback`/`set_bpm`. What changed is what sits behind them. The one
idea the whole redesign turns on: **the clock rate *is* the delay time**,
not an index into a buffer via a read pointer.

Four consequences follow directly, each now checked by ear as well as by
computation:

- **Delay time is a clock rate, not a buffer offset.** There is no read
  pointer any more — see "what was deleted", below.
- **Changing the clock while repeats are ringing bends the pitch already
  stored in the line**, the same way an EHX Deluxe Memory Man's rate knob
  does to a repeat already in flight. **Confirmed by ear** (2026-07-27,
  played live in VCV Rack): pushing RATE mid-repeat audibly bends it.
- **Bandwidth follows the clock.** A fixed charge-transfer loss pole tracks
  the clock as a constant ratio (`f_-3dB ≈ f_clk/4`), so STAGES — which
  moves the clock at a fixed delay time — doubles as a five-octave
  brightness axis. **Confirmed by ear**: STAGES reads as a brightness
  control end to end.
- **Chorus is clock modulation.** `FXT_FLUX_TIME` no longer nudges a
  fractional read index; it multiplies the clock directly, so as a live
  modulation-lane target it produces the vibrato/chorus a real BBD gives
  when its clock wobbles. **Confirmed by ear** as a working live modulation
  lane.

**Two numbers were corrected on the way**, both recorded as errata in the
design spec (`docs/superpowers/specs/2026-07-27-flux-bbd-delay-design.md`):
the clock relation is `f_clk = N / (2 · t_d)`, not `2N / t_d` — the spec's
own recorded correction, catching a classic datasheet-inversion error that
would otherwise run the clock 4× too high; and the cell count a two-phase
BBD line needs is `stages / 2`, not `stages` — this plan's correction, since
even clock ticks write a cell and odd ticks read one, and that alternation
*is* the two-phase clock, so only half the stage count is ever a distinct
stored sample at once.

**Memory** dropped accordingly: `FxMem::echo` (`Flux::kMaxSamples`, the
floats-per-channel the host must inject) went from 262144 floats per channel
— **4.19 MB across all four lines** (2 decks × 2 channels × 262144 floats ×
4 B) — to 8192 floats per channel (`kMaxStages/2` with `kMaxStages` = 16384)
— **128 KB across all four lines** (2 decks × 2 channels × 8192 floats ×
4 B), the same basis on both sides of the comparison. `host/vcv/README.md`'s
memory itemisation reflects the new figure.

**CPU: half measured, and the half that decides is the missing one.** The
bench gate for this redesign is split across two files:
`bench/workloads_bbd.cpp` builds the isolated `bbd` family, while
`bench/workloads_system.cpp` adds the `instrument_worst_bbd` row to the
`system` profile (Task 11).

The isolated family **has** been measured on a Daisy Seed, 2026-07-28, two
runs at `3a6820c` and `6338f63` (`docs/bench/2026-07-28-*-bbd.{md,csv}`):

| workload | max cyc | max % of block |
|---|---:|---:|
| `bbd_ceiling` | 55 259 | 5.75 |
| `bbd_line_only` | 35 944 | 3.74 |
| `bbd_line_tap` | 34 566 | 3.60 |
| `bbd_line_tap_half` | 25 774 | 2.68 |
| `bbd_walk_sdram` | 3 306 | 0.34 |

The system-level row was measured on 2026-07-29 at `1f7671d`
(`docs/bench/2026-07-29-1f7671d-system.{md,csv}`), and **it does not fit.**
Both runs agree, and the anchored figure — measured inside a real audio
callback, which is the one that decides — agrees with the offline one:

| workload | avg % | max % | anchored max % |
|---|---:|---:|---:|
| `instrument_worst` | 117.0 | 120.6 | **120.9** |
| `instrument_worst_bbd` | 128.6 | 133.2 | — |

The bench writes its own verdict: *"the 2x4 architecture does not fit… the
design has to shed voices or FX."*

**Where the cost went, row by row against `518f639` (2026-07-26).** Every
row whose checksum is unchanged is the same computation on both dates, so
these are like-for-like:

| row | 07-26 max % | 07-29 max % | checksum |
|---|---:|---:|---|
| `mod_plane_2x_center` | 7.45 | 7.44 | same |
| `synth_2x4` | 35.86 | 35.77 | same |
| `wave_2x4` | 32.64 | 32.28 | same |
| `fx_none` | 2.55 | 2.56 | same |
| `fx_comp` | 3.26 | 3.29 | same |
| `oliverb_solo_sram` | 9.46 | 9.48 | same |
| `fx_grit` | 4.78 | **7.70** | same |
| `fx_flux_sdram` | 7.08 | **19.76** | changed |
| `instrument_worst` | 97.50 | **120.55** | changed |

The voices, the modulation plane and the reverb did not move. **The whole
increase is in FX.** `fx_flux_sdram` nearly tripled, which is the BBD
replacing the tape echo — a different algorithm, so the changed checksum is
expected, but 12.7 points is what it costs. `instrument_worst`'s own
checksum changed for the same reason: FLUX inside it is now a BBD, so the
97.5 % figure is the same *scenario* measured on a different *instrument*,
not a like-for-like regression.

**`fx_grit` is unexplained and should be treated as a lead.** It rose 61 %
— 45 913 → 73 200 avg cycles — with an **identical checksum**, and
`engine/fx/grit.{cpp,h}` has no commits in that range. FLUX is provably not
leaking into it: `setup_fx` disables it with `immediate = true`, and
`Flux::process` returns at `flux.cpp:280` when the soft switch is idle.
`fx_none`, the same shell with everything off, did not move. So this is
~2.9 points of the block that nothing in the source accounts for.

**Consequence for the build order.** The instrument is over budget *before*
ZAP (M5k) and PULL (M5l) add anything, and before M6's firmware shell adds
its own overhead. The optimization round therefore comes first, and its
target is the FX chain, not the voices.

To reproduce: the bench's `full` profile does not link, for a documented and
pre-existing reason unrelated to this work — an SRAM/SRAM_EXEC region
overflow (`bench/README.md:34`) — so run the narrow profile from `bench/`:

```
python run.py --profile system
```

If the QSPI guard rejects the receipt, the engine has changed since the bank
was last programmed and only the ELF binding is stale; re-run step 3 of the
`bench/README.md` sequence to rebind it.

**What was deleted, and why.** The tap bank (`engine/fx/taps.{h,cpp}`,
`spky::TapBank`, `spky::derive_offsets`) — the mechanism that fed each
deck's FX with two offsets read out of the OTHER deck's rhythm. It cost real
budget (~4.7 points of `instrument_worst`), but that is not why it went:
**a BBD has no read pointer**, so an offset-into-the-line mechanism
contradicts the model it would now sit inside, rather than merely being an
expense the model can no longer afford.

**Ear pass status** (2026-07-27, played live in VCV Rack — the owner's own
listening, not derivable from the render host or from any measurement):
three of the four design claims above are now heard, not merely computed —
RATE bending stored pitch, STAGES as a brightness axis, and
`FXT_FLUX_TIME` as a live modulation lane. DRIVE was reported inaudible at
any setting; that report was investigated and traced end to end,
`Fireflow.cpp` → `Instrument::set_drive` → `PartFx::set_drive` →
`Flux::set_drive`, and the leading evidence was a measured inverted-U in
DRIVE's effect on output delta (peaking near DRIVE ≈ 0.5, collapsing by
DRIVE = 1.0; see the design spec's errata item 5), together with the
makeup gain around the saturator holding small-signal loop gain at unity by
construction at every DRIVE setting. **That makeup-gain law was the bug**:
it shrank the saturator's ceiling by 30 dB across the knob (1.796 → 0.057),
measured as a 14 dB drop in the actual echo return between DRIVE 0 and
DRIVE 1. Fixed in two commits (`ce07532`, `3dea01a`; see the design spec's
errata item 7): the ceiling is now fixed at `kSatCeil`, and DRIVE's range
moved to 0..+12 dB so self-oscillation stays reachable at DRIVE 0. **DRIVE
is diagnosed, fixed and re-heard** — the owner accepted the corrected DRIVE on
2026-07-30. Nothing is owed to it: the 0..+12 dB range stands as chosen, and
`kDriveHiDb` is not to be raised as a "finish" to this work. It remains the
cheap lever if DRIVE is ever judged too tame, but that would be a new voicing
decision, not the completion of this one.

Spec: `docs/superpowers/specs/2026-07-27-flux-bbd-delay-design.md`. Plan:
`docs/superpowers/plans/2026-07-27-flux-bbd-delay.md`.

### FX cost curves ✅

The optimization round the FLUX → BBD entry above called for. Rather than
guessing at fixes, it priced four suspect controls across their travel (FLUX
clock, STAGES, voice count, reverb) and closed two open questions (the FLUX
wrapper's own cost; the unexplained `fx_grit` rise) on real hardware —
`docs/bench/2026-07-29-cd6dafd-sweep.md`, full reading in
`docs/superpowers/specs/2026-07-29-fx-cost-curves-design.md` §9.

**Dispositions.** FLUX's clock ladder: **reshape the range** — drop
`kClockMaxHz` 32 → 24 kHz, worth 1.76 points across both decks, and note it
also turns three indistinguishable top rungs on the rate control into four.
STAGES, voice count and the reverb controls: **leave it** — each costs in
proportion to what it gives (STAGES is flat at a fixed clock, refuting a
cache hypothesis; voices scale linearly, ~1.45 fixed + ~4.2/voice, no knee;
the room controls span 0.64 points across their whole travel).

**The wrapper, measured rather than inferred.** `Flux`'s own per-sample
bookkeeping — slews, snaps, a clamp, a division, and a `std::pow` in
`bbd_drive_gain` (reached every sample via an unconditional, un-dirty-checked
`set_feedback` call whenever GRIT or FLUX is engaged), all driven by controls
that only move at the 96-sample tick — costs **7.71 points per deck, 15.4
both**, 46 % of what FLUX costs above the bare FX shell. The `std::pow` is
the largest single identified component of that figure. It changes the sound
at no setting.

**`fx_grit`'s 2.9-point rise, answered.** Not GRIT itself (measured cheap in
isolation) and not an unresolved shell mystery: the same `std::pow` in
`bbd_drive_gain`, gated only on buffer validity with no dirty-check on
DRIVE — found by reading, and now corroborated by measurement (≈1.60/deck)
alongside 1.17 points of pure code-layout drift. This saving is already
counted inside the wrapper's 15.4 points above; it is not an addition to it.
Fix, for the next round: cache the gain in `set_drive`, multiply in
`apply_feedback`. `engine/` was off limits to this round, so nothing was
changed yet.

**The gate, and what it takes.** `instrument_worst_bbd` measured **132.79 %**,
32.8 points over the 100 % gate. Everything this round was authorised to
spend — the wrapper, the clock ceiling, STAGES, room — sums to **17.16**:
short by 15.64. The remaining 18.3 points is the BBD model itself (the two
`BbdEcho` lines per deck), reachable only through the two levers the design
spec's §3 had refused: stereo FLUX and `kFiltOrder`.

**Owner decision, 2026-07-29 — "Erst den Mantel, messen, dann auf mono."**
Seeing these numbers, the owner took back half of that refusal. Order, fixed:
(1) move the wrapper to control rate; (2) **re-measure**; (3) collapse FLUX
to a single mono `BbdEcho` per deck. `kFiltOrder` stays refused. This
supersedes §3 of the cost-curves spec for the stereo lever only — a later
reader must not treat that refusal as still standing in full.

Stacking the accepted plan (wrapper 15.4 + mono ≈9.2, the latter a half-share
of the measured two-line cost and not yet re-measured + clock ceiling 1.76)
reaches **≈26.4** against the 32.8-point gate — **≈106 %**, not under 100 %,
even if every step lands as estimated. Stated plainly, not softened: the
instrument does not fit yet, and this round did not claim otherwise. The
re-measurement step exists precisely because two estimates were already
compounded once, into the 34-point figure this round was sized against, and
that figure turned out wrong.

Spec: `docs/superpowers/specs/2026-07-29-fx-cost-curves-design.md` §9. Evidence:
`docs/bench/2026-07-29-cd6dafd-sweep.md`. Merged to `main` from
`perf/fx-cost-curves` on 2026-07-29; the branch changes no production code, so
it lands as evidence and documentation only.

### FLUX control-rate wrapper ✅ (step 1 of the accepted plan above)

The first of the three steps the owner fixed above: move `Flux`'s per-sample
wrapper bookkeeping — the `std::pow` in `bbd_drive_gain`, reached every sample
via an unguarded `set_feedback`/`set_time_mod` push — to control rate, via a
cached feedback-coefficient scale and two unchanged-value guards. No control-
tick restructuring, no musical change. Spec:
`docs/superpowers/specs/2026-07-29-flux-control-rate-design.md`. Branch:
`perf/flux-control-rate`. **The work is on `main`** — `9f587bf`, the branch's
last commit, is an ancestor of `main`, verified rather than assumed — but it was
fast-forwarded rather than merged, so there is no merge commit to cite in the
style of rounds 1–3 (`1e9ec05`, `57201ff`, `a93327e`), and the branch no longer
exists locally.

**Measured, not banked in advance.** Against §2's prediction of ~2.5
points/deck, ~5 total: the isolated one-deck row (`fx_flux_sdram`) saved 1.49
— over-predicted. The gate row (`instrument_worst_bbd`) saved **7.55
points**, 132.79% → **125.24%** — under-predicted by 51%. GRIT's own
companion rows (`sweep_grit_bare`, `sweep_grit_no_bbd_mem`) held byte-identical
while `fx_grit` — also byte-identical — dropped 1.37 points: GRIT alone was
paying for gated `Flux` work it never uses, confirming §7's *mechanism*.
§7's *number* was off by about a third, though: the gated-work residual
(`fx_grit − sweep_grit_no_bbd_mem`) was 1.60 before this round and is 0.55
after, not 0.00, so "confirmed exactly" overstates it. Full reading, including
the load-dependence pattern behind the isolated/gate-row gap — **measured**
across three rows, with only its icache/pipeline mechanism still a
hypothesis — in
`docs/superpowers/specs/2026-07-29-flux-control-rate-design.md` §13.

**What remains.** 25.24 points over the gate, down from 32.79. Next in the
owner's order: collapse FLUX to one mono `BbdEcho` per deck, estimated ~9.2
points in isolation — which on isolated arithmetic alone would still leave
the row over 100%, though §13.3 gives a specific reason (this round's own
isolated estimate undershot the gate-row saving by more than 2×) not to treat
that as settled until it is measured directly. `engine/fx/bbd.h`'s `1/sr_`
division in `BbdLine::SetClock`, worth an estimated ~0.6 points, stays
deliberately untouched — model territory, left for the mono round.

### FLUX mono collapse ✅ (step 3 of the accepted plan above)

Step three of the owner's fixed order: collapse FLUX's stereo `BbdEcho` pair
to one line per deck (mono input, 0.5·(l+r), fanned back to both channels).
This round changes the sound on purpose — the dry signal keeps its per-voice
pan, but the echo it feeds is now centred — and where DRIVE bites, since one
saturator/compander/feedback loop now sees the summed signal instead of two
seeing their own channel. No panning, widening, or DRIVE/compander re-tuning
was added; that was deliberately left to the owner — **and the owner decided on
2026-07-30 to keep it mono and to add none of them.** Branch: `perf/flux-mono`.
**The work is on `main`** — `bd346eb` (the collapse) and `1ba3f18` (the bench
comment fixes) are both ancestors of `main`, verified — but it was
fast-forwarded rather than merged, so there is no merge commit to cite and the
branch no longer exists locally.
Spec: `docs/superpowers/specs/2026-07-29-flux-mono-design.md`
§10. Evidence: `docs/bench/2026-07-29-4d1e929-sweep.md` (before) against
`docs/bench/2026-07-29-1ba3f18-sweep.md` (after).

**Measured saving.** The gate, `instrument_worst_bbd`, fell **125.24 % →
112.88 %**, a **12.36-point** cut — more than the naive 9.16-point expectation
(half of `sweep_flux_lines_2ch`'s 9.17, times two decks). Across both rounds
of this programme: **132.79 → 125.24 → 112.88**. **12.88 points remain
to reach 100 %.**

**The load-factor question, answered, and the answer is deflationary — and
where the operating point is held fixed, it is worse than "doesn't
generalise."** The wrapper round (above) found the same kind of change worth
1.49 points/deck in isolation, 2.16 on `instrument_worst`, 3.78 on
`instrument_worst_bbd` — a clean, monotonic climb it attributed to an
instruction-cache hypothesis. This round's equivalent triplet is 4.66 / 3.90 /
6.18 — **not monotonic**, the middle figure dips *below* the isolated row.
That dip is not noise: `fx_flux_sdram` and `instrument_worst` run FLUX at the
identical operating point (bpm 120, rate index 3, STAGES 8192, `FXT_FLUX_TIME`
neutral, DRIVE 0 — an 8192 Hz clock either way), and the per-line saving falls
16 % between them — the opposite sign of the wrapper round's load scaling. The
climb to 6.18 at the gate is explained instead by FLUX's own operating point,
not by load: this round's per-line saving climbs with RATE (0→11: 4.56 →
4.87 → 5.43 → 6.05 → 6.53 points) and STAGES (512→16384: 4.29 → 4.38 →
4.86 → 5.38), independent of anything else running, and `instrument_worst_bbd`
runs FLUX at its hottest reachable point (STAGES 16384, clock at the **32 kHz**
ceiling, `bbd_tuning::kClockMaxHz`) — the same clock as `sweep_flux_rate_11`,
the hottest isolated row at 6.53. An operating-point-only model therefore
predicts the gate should save **at least 6.53** per deck; it returned
**6.18** instead, a small deficit in the same direction as the fixed-point dip
above. **This weakens the wrapper round's icache hypothesis as a general
law**; it stays confined to that round's specific `std::pow` call site rather
than becoming a rule for the next removal. Full reading, including the
checksum evidence that confirms the comparison rows still measure what they
say (`sweep_flux_lines_2ch` returned a byte-identical checksum across both
captures, as Task 4's review predicted in advance), in the spec's §10.

**Two rows got measurably more expensive**, with no FLUX line running at all:
`fx_grit` +0.32 (5.16 → 5.48) and `sweep_grit_no_bbd_mem` +0.30 (4.61 → 4.91)
— about 6 % on a 5-point row. Two clean controls that never reach `Flux`'s
per-sample bookkeeping (`sweep_grit_bare`, `fx_none`) did not move; the two
that did are exactly the two whose `flux.o` code this branch recompiled,
while the code they actually execute (`Flux::process`'s prologue,
`set_feedback`, `set_time_mod`, `engaged()`) is textually unchanged — a
control set that strengthens the candidate explanation without proving it
(full table in the spec's §10.4). `sweep_grit_no_bbd_mem`'s checksum held
byte-identical while its cost moved, which is the signature of layout drift
rather than new work, consistent with this project's own prior note that a
cross-build layout shift once moved a 29K-cycle row by about 7 %. That is
offered as the candidate explanation, not a proven one.

**Listening: the reverb helps, but not enough to make the loss disappear.**
Side/mid ratio, before → after: dry 0.7394 → 0.1675 (22.7 % of before), verb
0.7671 → 0.3063 (39.9 % of before). The reverb roughly doubles what survives,
confirming §6's assumption that it recovers some width from the now-centred
echo — but even in the realistic (reverb-engaged) case, more than half the
width is gone. What remains is carried by the dry path's existing per-voice
pan, not by the echo, which is centred in both renders.

**What remains.** The owner's panning decision is open — whether the lost
width needs recovering, and in what form, decided by ear against the four
render files this round produced, not by this measurement. `engine/fx/bbd.h`'s
`1/sr_` division in `BbdLine::SetClock` is still unaddressed, worth an
estimated ~0.6 points, left deliberately as model territory. And the gate
itself: **12.88 points still separate `instrument_worst_bbd` from the 100 %
line.** `perf/flux-mono`'s content is on `main` (`bd346eb`, `1ba3f18`,
fast-forwarded, no merge commit). The owner accepted the mono collapse on
2026-07-30, and at the time that acceptance was written down as permanent:
"FLUX stays mono… a later round must not treat the collapse as provisional,
and must not 'restore' stereo FLUX as a fix." No panning or widening layer was
to be added on top, and no DRIVE or compander re-tuning was owed to the change.

**Superseded, 2026-07-30 — the collapse was a budget measure, not a
preference.** The owner's own framing on revisiting it: mono came out of
necessity. It was step (3) of a CPU programme (see the decision above it), and
the listening pass that accepted it was accepting a *fait accompli* under
budget pressure, not choosing a centred echo over a wide one. **The
"do not restore" instruction is therefore withdrawn.** What stands from the
paragraph above is the measurement (12.36 points for two lines, ≈6.18 each) and
the observation that the dry path's per-voice pan, not the echo, carries the
image on an FX-layer deck.

Two consequences, both from
`docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md` (drafted as
`2026-07-30-…` before it moved here): a BBD **part engine** is stereo, because it *is* the signal path and has
no dry path to carry an image; and the tape echo that replaces FLUX returns
**stereo, as it was** (`EchoDelay _echo_l; _echo_r;` at `e004a3d^`), with mono
still a legitimate outcome — decided by ear on the finished tape echo rather
than inherited from a decision taken about a bucket-brigade.

### Instrument-level ablation ✅ (where the unmeasured budget actually goes)

Every optimisation round so far searched inside the blocks that have bench
rows, because adding those rows up reached only ~87 % of the gate and the
missing fifth had never been measured — only inferred by subtraction. This
round built the ladder that measures it: three new rows in a new `instr`
family (`bench/workloads_instr.cpp`) plus the existing gate, so the budget
partitions exhaustively **inside a single run**. No `engine/` file was
touched **by this round's own change** — the wider compared interval has one
comment-only edit in `bbd.h` (below), which cannot reach codegen. Branch:
`perf/instrument-ablation`, **merged as `1e9ec05`**. Spec:
`docs/superpowers/specs/2026-07-29-instrument-ablation-design.md` §8.
Evidence: `docs/bench/2026-07-29-930ec17-ablate.md` / `.csv`.

**The three differences** (`pct_max`, run 2; `avg_cyc` agrees in direction on
all three):

| difference | isolates | points |
|---|---|---:|
| `instr_part_2` − 2 × `instr_part_1` | contention *between* decks (± deck A/B asymmetry — see below) | **−0.54** |
| `instr_noverb` − `instr_part_2` | instrument glue | **+4.04** |
| `instrument_worst_bbd` − `instr_noverb` | reverb *in situ* | **+14.79** |

`2 × 46.24 − 0.54 + 4.04 + 14.79 = 110.77`, the gate exactly.

**The answer, and it is a null result the spec committed in advance to writing
plainly.** The gap is mostly neither of the two things the round offered.
**Contention between the decks is at most a few tenths of a point, in either
direction, sign unsupported.** Two decks together cost 0.54 points *less*
than two decks priced separately, but `instr_part_1` measures deck A alone
and there is no deck-B-alone row: the difference also carries `(cost B − cost
A)` between the two differently-seeded decks, which is enough on its own to
flip the sign. Even so, the cache/SDRAM-pressure hypothesis is **refuted for
the deck pair** at that bound. (The ladder does not test contention *inside*
a deck; see below.) Glue is only **4.04 points** in total, covering
`Center::update`, the CHOKE framing, MORPH, the dry taps, the cross-deck
rhythm exchange and the limiter together.
Of the 24.14 unaccounted points in this build, **15.47 (64 %) sit inside the
two decks** — 7.73 per deck; one bare `Part` configured as the gate configures
a deck costs 46.24 points against 38.51 for its own block rows — **5.17 (21 %)
is the reverb costing more in situ than `oliverb_solo_sram` prices it**, and
4.04 (17 %) is the glue. **There is no instrument-level cut to be found.**

**More than one mechanism owns that 15.47, and the next round should not
assume all of it is deletable.** About 30 % of it (~4.6 points) is FLUX being
*priced wrong* — STAGES 8192 / rate 3 in the block row while the gate runs
16384 with the clock on its ceiling — the same class as per-deck modulation
being priced at rate 0.5 and 0.6 (density 0.7 for both) against the gate's
0.8 / 1.0. The rest is `Part`-internal work no row prices *at all*
(`process_in`, the engine fade, voice-to-FX routing are the suspects, none of
them sized this round) — plus, unseparated by anything measured here,
**contention between the blocks inside one deck**: every block row was
measured in isolation, and running those
blocks together in one `Part` shares a D-cache and an SDRAM bus. That part of
the original hypothesis is still open.

**The finer round is not worth running.** Its whole subject — reverb
smoothers, MORPH, `derive_intervals` — divides 4.04 points three ways. Those
sub-point quantities *are* resolvable within a single run (the run-to-run
spread of this round's within-run differences was 0.10–0.32 points); they are
simply too small to repay a hardware round, since deleting the entire glue
bucket would still leave 6.73 of the 10.77 points outstanding, and
`process_in` is not in the glue at all.
The two bare decks are 92.48 points, **83.5 % of the gate**; only FLUX
(~13/deck) and the eight voices (35.80) are large enough to close a 10-point
gap. The one cheap follow-up worth having is a single row, not a ladder: the
reverb alone at the instrument's **SIZE 1.0 and TONE 0.5** (the isolated row
differs from the instrument on both), to split the 5.17 into operating point
versus smoothers-and-mixing.

**A finding beyond this round: code layout moves the gate by ~2 points.**
`instrument_worst_bbd` returned **110.78 / 110.77** here against **112.79 /
112.88** in `docs/bench/2026-07-29-1ba3f18-sweep.csv`, at the **identical
checksum `483e8e82`** — same computation, ~2 `pct_max` points cheaper, with no
change to any instruction-generating engine code (the sole `engine/` edit in
the interval is one comment line in `bbd.h`). The perturbation was a **profile
family swap**, not a one-file addition: the baseline image carried
`bench/workloads_sweep.cpp` (~900 lines, fifteen rows) and this one carries
`bench/workloads_instr.cpp` (three rows) instead. This is the effect the mono
round saw on `fx_grit` and could not prove; the unchanged checksum proves it.
Other rows moved too, at unchanged checksums: `instrument_init` +4.27 %,
`fx_grit` +2.24 %, `oliverb_solo_sram` +1.73 %, several others −1 to −1.8 %
(those are `avg_cyc` changes, not `pct_max` points). **Any cross-build claim
in this project smaller than ~2 points on the gate or ~2 % on a small row, at
a comparable build change, has measured its own layout** — a bound calibrated
across a family swap, so not a proven floor for the smallest possible edit,
but proof that the perturbation is real and unbounded by anything measured.
Differences taken *within* one run are not subject to it. It also restates the
target: the overshoot is **10.77 points, not the 12.88 the mono round
recorded**.

**What remains.** The **10.77-point gap** to 100 %, which must now be found
inside FLUX and the voices. The owner's **panning decision** from the mono
collapse is still open, decided by ear against that round's four renders. And
`engine/fx/bbd.h`'s `1/sr_` division in `BbdLine::SetClock` is still
unaddressed, worth an estimated ~0.6 points, still deliberately left as model
territory.

### Deck-interior ablation ✅ (the 7.73 per deck, split)

Branch `perf/deck-interior-ablation`. Spec
`docs/superpowers/specs/2026-07-29-deck-interior-design.md` §8, evidence
`docs/bench/2026-07-29-c4ae8db-ablate.{md,csv}`. Two rows added to the existing
`instr` family and `ablate` profile; no engine change.

The predecessor left **7.73 unpriced points per deck**. Two rows re-priced the
two block rows whose operating point provably differed from the gate's:
`deck_engine_hot` (one `SynthEngine` driven as `Part::process` drives it —
through a virtual `IPartEngine*`, in FLOW, cycle derived from a real
modulator's `master_hz()`, `process_in()` called) and `deck_mod_hot` (one
`SuperModulator` at RATE 0.8, no `Center`).

**Result: the two operating points this round re-priced were not the problem.**
All figures `pct_max`, run 2, computed entirely within one run. (The headline was
7.73 when the round was planned; 7.645 here is the layout drift below, not a
correction — `instr_part_1` 46.24 → 46.00, block sum 38.505 → 38.355.)

| | points |
|---|---:|
| voices: `deck_engine_hot` − `synth_2x4` / 2 | **+0.70** |
| modulation: `deck_mod_hot` − `mod_plane_2x_center` / 2 | **+0.10** |
| **remainder** | **+6.85** |
| per-deck excess | 7.645 |

89.6 % survives both corrections — but **it is not yet `Part` structure**, and
this round cannot call it that. Roughly **2.29 of those 6.85 points per deck are
FLUX's known operating-point error**, which the predecessor measured and this
round did not re-price: `fx_flux_sdram` runs STAGES 8192 / rate index 3 /
feedback 0.7 while a deck runs 16384 / top rate / 0.9, so the FX increments
subtracted above are too cheap and the remainder is correspondingly too large.
In-deck block contention, which the predecessor also left open, is still in there
too. About **4.5 points** are genuinely unattributed. The `Part`-level work that
*is* inside: the chord builder, the quantizer, `_control_tick`'s pushes, the
`_engine_fade` multiply, and `Part::process`'s own loop — **not**
`_adjust_surface`, which is a `SynthEngineT` member that `deck_engine_hot`
already pays, so the subtraction removes it.

The four differences established by reading (two virtual dispatches per sample,
FLOW vs STEP, a 14×-shorter derived cycle, `process_in` where the old row calls
none) are all inside the +0.70 as a **joint upper bound**; their own per-sample
share is **+0.29**, which is what `pct_avg` moves by. The extra ~0.41 shows up
only in `pct_max` because it is a per-block event this row pays and `synth_2x4`
does not — the ~14 `trigger_chord` fires, landing in whichever block sets
`max_cyc`. Read either way, none of the four was expensive. The remainder is
unaffected: `instr_part_1` fires once per cycle too, so the fire cost cancels.

**`synth_2x4`'s points are not underpriced — but the "cut a voice" arithmetic
needed a different correction than expected.** `synth_2x4` prices eight voices
at 4.479 each (35.83 in this build, 35.80 in the predecessor's) and
`deck_engine_hot` four at 4.65, so the rows were right. Neither average is the
right number for the decision: differencing
`synth_1_voice`/`_2_voices`/`_4_voices` (5.65 / 9.91 / 17.85) shows a
**non-linear** ladder — the second voice costs 4.26, the third and fourth 3.97
each. So the **marginal** voice is about **3.97**, and **1.7–2.0 points** per
engine are fixed overhead a cut does not reclaim. Cutting one voice per deck
saves ≈ **7.9 points** — not the 9.3 `deck_engine_hot`'s average implies, nor
the 8.96 `synth_2x4`'s does — i.e. **78 % of the gap**, cheap and structural,
but not sufficient alone.

**Two candidates closed by reading, not measuring.** The four engines a `Part`
holds cost memory and no CPU (`_engine` is one pointer; the fade multiply is
exactly 1.0 at hold). The two virtual dispatches per sample are structural —
the interface *is* the four-engine design — and, being per-sample costs, sit
inside the **+0.29** `pct_avg` share, net of the other three per-sample
differences. Separately: **DENSITY is inert in FLOW**, in both bench rows and in
the gate itself (`_density` → `_groove_k`, reached only from `_effective_gate()`,
which `_on_boundary()` calls only in STEP — none of the three sets it), and
`set_tempo_bpm` is inert in FREE mode. A knob the gate sets to 1.0 has no cost
implication at the gate's own operating point.

**The layout drift reproduced a third time.** All 18 rows shared with
`930ec17-ablate.csv` returned identical checksums, and all 18 of the
predecessor's rows are present here (this run has 20), so the shared set was not
chosen after the fact. Costs moved anyway: the gate 110.77 → **110.10**,
`fx_grit` 5.61 → 5.43, `oliverb_solo_sram` 9.62 → 9.66. `fx_grit` is the row the
mono round first suspected and could not prove; it has now moved in three
consecutive runs at an unchanged checksum. **The ±2-point bound is loosened, not
confirmed:** its gate half holds (0.67 of ±2), but its small-row half does not —
`fx_grit` moved −**2.84 %** of `avg_cyc` (53474 → 51957), past the ±2 % the
predecessor calibrated across a *family swap*, and across a much smaller build
change. Every other row moved ≤1.29 %. The target is now **10.10 points**.

**Unlike the predecessor, a finer round now has a subject.** The remainder is
6.85 points on deck A and 6.95 on the incremental deck B, ≈**13.8 across the
two** — larger than the gap. Last round's argument against splitting rested on
the glue being 4.04 points, too small to repay a round; that does not apply here.
Three cheap rows, in order: (1) **FLUX at the deck's real operating point** —
first, because the remainder cannot be named until those ~2.29 points per deck
are re-attributed; it is a bench-row change, not an engine change, and
re-attributing them does not by itself save them (whether STAGES 16384 and the
top flux rate are settings the instrument needs is the owner's call). (2) **The
marginal voice at the gate's operating point**, so the 7.9-point estimate stops
being a difference of rows measured at the old one. (3) **A `Part` with its chord
surface held at one note** — the chord/quantizer path; nothing in this round
ranks the remainder's constituents, so it is not "the largest", just one of three
exclusions documented as a consequence rather than measured. The second changes a
decision — if the marginal voice is nearer 4.65 than 3.97, cutting one closes
**92 %** of the gap rather than 78 %, and "four voices or three" becomes the
cheapest route to 100 % this project has.

### Remainder split ✅ (the 6.85 per deck, named)

Branch `perf/remainder-split`. Spec
`docs/superpowers/specs/2026-07-30-remainder-split-design.md` §9, evidence
`docs/bench/2026-07-30-ccd5f12-ablate.{md,csv}`. Three rows added to the existing
`instr` family and `ablate` profile; no engine change.

The predecessor left a **6.85-point residue per deck** it could not name, with
FLUX's ~2.29-point operating-point error and in-deck contention both still
inside it. Three rows separate them: `fx_flux_hot` (FLUX at the deck's own
operating point on all **four** axes, DRIVE included), `tone_solo` (the shell's
engine, reached through a virtual `IPartEngine*` exactly as `Part::process`
reaches it) and `deck_shell` (a whole `Part` at the gate's operating point
running that engine, with every FX block off at `fx_none`'s exact operating
point).

**Result: the residue is `Part`-level code, not contention.** All figures
`pct_max`, run 2, computed entirely within one run. The gate reads **110.51**
here, so the target is **10.51 points**, not the predecessor's 10.10.

| per deck | `pct_max` | `pct_avg` |
|---|---:|---:|
| voices — `deck_engine_hot` | 18.57 | 17.93 |
| modulation — `deck_mod_hot` | 3.76 | 3.50 |
| FX, FLUX now faithful | 18.48 | 18.35 |
| **`Part`-level code** | **4.00** | **2.65** |
| `instr_part_1` | 45.65 | 43.54 |
| **contention + anything still unnamed** | **0.84** | **1.11** |

`Part`-level code is **2.65–4.00 per deck** — 2.65 (`pct_avg`) is the
steady-state figure and 4.00 what a `Part` costs in its worst block, because
`deck_shell` spreads 15.4 % against `tone_solo`'s 3.0 % and a 15 % spread is a
**rare-event** signature rather than a once-per-block cost (`trigger_manual()`
once per 250 blocks, the gate's lane fire once per ~72 blocks). Doubled that is
**5.3–8.0 across the instrument** — and the doubling **assumes deck B's shell
matches deck A's, which this round did not measure** (`instr_part_2 −
instr_part_1` is 46.18 against 45.65, so the second deck is about half a point
dearer). **Compared like with like, the compromise-free bucket is about three
quarters of the overrun:** 8.00 against the `pct_max` gap of 10.51 (**76 %**),
or 5.30 against the `pct_avg` gap of 6.50 (**82 %**, the gate reading 106.50 in
both runs). Quoting the `pct_avg` bucket against the `pct_max` gap mixes metrics
and understates the finding.

**Contention is bounded, small, and now the *smallest* named term.** 0.84 as
measured — **1.1–1.3** once §6.11's known negative bias is allowed for, since
the FX line charges `PartFx`'s shared outer-branch work twice where a real deck
pays it once — and it is an **upper bound**, because `deck_shell` runs the
`Part` almost uncontended (§6.1) so every contention effect lands in this term.
Three rounds have now failed to find contention anywhere: nil between decks, nil
at deck granularity, about a point inside a deck. It is small enough to stop
spending rounds on, but **it is not noise** — the run-to-run spread of the same
quantity is **0.10**, an eighth of it.

**FLUX's operating point costs +1.30 per deck.** `fx_flux_hot` −
`fx_flux_sdram` = **+1.30** (`pct_max`; +1.34 `pct_avg`), four axes moved
together on one build, against round 1's **+2.29** from two axes swept
separately on a different build. So the residue was smaller than round 2
reported — but **which of the two figures is "wrong" is not separable here.**
The added DRIVE axis moves the BBD saturator's threshold in the direction that
takes `fast_tanh`'s cheap early-return path *more* often, so a four-axis figure
landing below a two-axis estimate is what that mechanism predicts with no
round-1 error at all, and no row anywhere prices DRIVE alone. What is settled is
the within-run number to use: **+1.30**. Round 2's own formula re-evaluated in
this build gives **6.14**, and `1.30 + 4.00 + 0.84 = 6.14` exactly — the
predecessor's residue is fully split, and the 6.85 → 6.14 balance is layout
drift, not a correction.

**§4.1's arithmetic was wrong, and the registered sharpest test did not catch
it.** `Part::process` runs `_mod.process()` every sample
(`engine/parts/part.cpp:378`), so `deck_shell` contains the modulation plane;
the registered formula subtracted `deck_mod_hot` only from the residue and so
charged it **twice**. That one defect inflated `Part`-level code to 7.76 and
drove the residue to −2.92, falsifying two of six pre-registered predictions on
its own — while every row *as measured* returned exactly what it was built to
return. §5 nominated the *sign* of `Part`-level code as the test that would
catch a broken ladder; the ladder was broken and the sign test passed
comfortably. What caught it was the residue crossing its −1.0 sanity bound.
**A future round should register the residue's sign, not a component's.**

**The layout drift reproduced a fourth time, and did not shrink.** All **20**
rows shared with `c4ae8db-ablate.csv` returned identical checksums, in both runs
of this build and against the previous one; the two runs differ in cycles on 15
rows and in checksum on none. Costs moved anyway: the gate 110.10 → **110.51**,
`instr_part_1` 46.00 → 45.65, and the largest `pct_max` movement of any shared
row is **`instr_noverb` at +0.86**, twice the gate's. Two rows again exceed ±2 %
of `avg_cyc` at unchanged checksums — `fx_flux_sdram` **+2.65 %**,
`mod_plane_2x_center` −2.56 % — so round 2's loosened ±2 %-on-a-small-row bound
is not tightened here either. Which is why every figure above comes from one run
and none from a subtraction across builds.

**Next is a fix round on `Part`-level code — the first non-diagnostic round in
this sequence.** 5.3–8.0 points across the instrument, no sonic cost, and now
named rather than inferred. It starts inside **`Part::_control_tick`**
(`engine/parts/part.cpp:180-374`): the chord build, the quantizer, the five
`target_raw` evaluations, the target pushes, the FX target-cache fill and the
excitation bus are the once-per-tick `Part`-level work `deck_shell` actually
measured, and the rare-event shape of the spread points additionally at the
chord build, which runs on every note. **`Flux::set_rhythm` is *not* in this
bucket:** it is **instrument-level, not `Part`-level**, called only from
`Instrument::process`'s control tick (`engine/instrument.cpp:96-97`) and never
from `Part::_control_tick`, and `deck_shell` instantiates a bare `Part` with no
`Instrument` — so it contributes exactly **zero** to the 2.65–4.00 measured
here. It remains a real candidate (it runs `update_thin_pattern()` and
`derive_intervals()` unguarded twice per tick, including at LINK 0 where nothing
changed), but any saving there comes out of round 1's separately measured
**4.04-point glue** bucket, not out of this round's `Part` bucket. One further
row would repair the FX ladder — GRIT *and* FLUX both engaged, so `FXtotal` can
be formed with one copy of the shared branch instead of two. **The voice cut
remains the fallback at ≈7.9 points**, and it is now clearly the *second*
option, not the first, because `Part`-level code is of comparable size and costs
nothing to hear.

Not a guaranteed close, and none of these points is free to take: a `Part`'s
per-sample loop and control tick can be tightened, not deleted. But for the
first time in three rounds the largest named quantity is code rather than a
residue.

### Per-sample call boundary ✅ (the first round that changed `engine/`)

Branch `perf/part-per-sample`. Spec
`docs/superpowers/specs/2026-07-30-part-per-sample-design.md` §9, evidence
`docs/bench/2026-07-30-7272b27-ablate.{md,csv}` (a same-session baseline, round
3's source rebuilt) against `-dc17cdc-`, `-86cf817-` and `-cd639ec-`, two runs
each. **The first round in this sequence that touched `engine/`**, and therefore
the first that could have changed the sound.

**The gate fell 106.50 → 104.91 `pct_avg` (110.76 → 108.69 `pct_max`), so the
overrun is 4.91 / 8.69.** That baseline is this round's own same-session rebuild
of round 3's source; round 3's line above reads the same source at 110.51
`pct_max`, and the 0.25 gap between them is itself the drift this round went on
to measure (spec §9.12). Round 3 named `Part`-level code as 2.65–4.00 points per
deck; this round took **about a quarter to a third of it** by making `Part`'s
per-sample body inlinable at every call site, so the nine-register `stmdb`/`ldmia`
pair and the `vpush {d8-d9}` that `Part::process` paid 96 times per block are now
paid once per block. Three smaller items followed in one build. **Item 2** was
predicted below this bench's resolution and duly produced no measurement.
**Item 3** was reverted because its stated premise — "two wasted `vmul`" — turned
out on the disassembly to be a trade rather than a removal, leaving it with no
demonstrable justification either way. **Item 1** — reordering `Part`'s members —
is different in kind: the spec's §5.2 registered **no prediction** for it, saying
it "could be worth nothing or could be worth more than item 4", and this round
measured it in one build together with items 2 and 3, so it has **no measured
value here** either way. It is not a "below resolution" item; it is an unweighed
one.

**Quote the saving as 1.6 ± 0.5, not 1.59.** The three builds that all contain
the change read the gate at 104.40, 105.36 and 104.91.

**But the mechanism does not explain the size, and the round says so.** §3.2's
ISA argument predicted **0.40–0.70** across the instrument for removing the two
per-sample prologue/epilogue pairs. The gate moved **1.59** and the four
`Instrument` rows **1.38–2.71** — **two to four times what the ISA allows**. The
static half of the model held: `Part::process` has no symbol in the final ELF and
the save/restore is verifiably gone. The quantitative half was **falsified**, in
the favourable direction, and **the majority of the saving therefore has no
identified cause** (spec §9.11). One candidate exists — that inlining the body
into the caller's loop lets the compiler keep values in registers across the 96
samples — and it is **unverified**; the attempt to read it out of the disassembly
was abandoned without a clean loop boundary. Nothing here should be read as "the
nine-register pair was worth 1.6 points".

**Bit-exact, and measured as such — twice, on two independent instruments.** All
23 bench rows returned identical checksums in every run of all four builds, and
identical to round 3's `ccd5f12`. Independently, the repo's two byte-identity
render gates were run on `main` (`a93327e`) and on the branch (`0eed246`) for the
first time: **both commits render byte-identical WAVs**, at the default build
type and again at `-O3`, through `host/render` on clang/x86-64 **without
`-ffast-math`** — the one mechanism a reviewer identified as a plausible way the
change could have altered output. `wave_formant_sweep` passes on the branch.
(`ctrl_identity`'s stored constant is optimisation-level dependent: it matches
the documented `cmake -S . -B build` configuration and fails at `-O3` **on `main`
too**. That is a pre-existing gate defect worth its own fix, not a finding about
this branch.)

> **Correction, 2026-08-04 — the sense of that parenthetical is now inverted.**
> On today's `main` the stored constants match a **Release** build and fail under
> Debug, which is what a bare `cmake -S . -B build` gives you. Measured on one
> unchanged tree in two independently configured build directories: Debug →
> `spky_tests` and `ctrl_identity` both report `SYNTH reference moved`; Release →
> all four tests pass. The references were evidently regenerated from an optimised
> build at some point after the round above was written. The build commands in
> `README.md`, this document and `host/vcv/README.md` now pass
> `-DCMAKE_BUILD_TYPE=Release` explicitly. The underlying gate defect is unchanged
> and still unfixed: these are byte-identity checks on floating-point renders, so
> they remain tied to one optimisation level.

**Exactly one row failed to move, and no mechanism explains which one.** The four
`Instrument`-based rows moved −1.38 to −2.71 `pct_avg`; the two bare-`Part` rows
`instr_part_1` and `instr_part_2` moved −1.16 and −1.25; and **`deck_shell`, the
row round 3 built to isolate one `Part`, moved +0.20** — inside the control
group's own drift, so neither a saving nor a regression, but a row that did not
move. An earlier reading of this round grouped the rows by whether they run their
`Part` inside `Instrument::process` and concluded that only those improved; **the
final build refutes that** — `instr_part_1` and `instr_part_2` are direct-call
rows and both cleared a point — and the grouping cannot in any case distinguish
`instr_part_1` (one `Part`, bench loop, −1.16) from `deck_shell` (one `Part`,
bench loop, +0.20). What survives is the narrower lesson, and it is the useful
one: **a row that isolates a component does not necessarily reproduce how that
component is called**, and for a change *at* the call boundary that is the
property that matters. The round's sharpest pre-registered row-level prediction
was placed on the row that could not see the change.

**The same error is one level up, and it bounds what this saving is worth
today.** `Instrument::process` owns the 96-sample loop
(`engine/instrument.cpp:76`), and **both hosts that exist call it with `n = 1`** —
`host/render/main.cpp:102` and `host/vcv/src/Fireflow.cpp:637`. At `n = 1` the
loop runs once per call, so `Instrument::process`'s *own* nine-register prologue
and `vpush {d8-d13}` are paid per sample; only the bench passes 96. What still
holds there is the removal of the two `Part::process` prologue/epilogue pairs per
sample — §3.2's 0.40–0.70. What does not hold is any part of the saving that came
from state staying in registers across the block, which is precisely the
unverified candidate for the unexplained majority above. **So the bench measured
a saving the two current callers would only partly see. No figure is claimed for
how much** — it is not measured.

**The product, however, is already the right shape, and this was checked after
the section above was written.** `app.cpp:117` sets `block_size = 96` — the
bench's block exactly — and `AppImpl::ProcessAudio` (`app.cpp:183-199`) passes
`size` straight to `_core.process`. The Daisy audio callback is block-based at 96
by construction, so when M6 wires it to `Instrument::process` the full saving
applies with nothing buffered and no latency cost. **On block size the bench is a
faithful model of the firmware, not an optimistic one.** The `n = 1` finding
bounds the two *development* hosts: VCV cannot do better in principle, since Rack
calls a module once per sample and a block would cost up to 96 samples of latency
on audio and CV alike; `host/render` could batch but applies scenario events at
exact sample indices, so batching would move event timing and change the render
that `ctrl_identity` hashes. Neither is worth changing for CPU.

**This bench cannot demonstrate a change smaller than about 0.5 points on the
gate.** Run-to-run spread inside one build is ≤ 0.04 on `pct_avg` and the gate
reads identically in both runs of all four builds — but every comparison the round
can make is cross-build, and cross-build layout drift moved `fx_grit`, a row
containing no `Part`, by 0.47–0.49, while a **−368-byte** `.text` change moved the
four `Instrument` rows over a 2.11-point spread. That bounds what any future
bit-exact round can *show*, whatever it actually saves, and it is this round's
most useful output for whoever plans the next one.

**Recommendation: no further bit-exact `Part` round until that floor is lowered.**
The residue is real — roughly 1.9–3.0 points per deck, still 68–75 % of what is
left of the overrun, and still free of sonic cost. The structural lever is spent:
`Part::process` no longer exists as an out-of-line symbol, so it cannot be pulled
again. **But the recommendation rests on the floor, not on the model.** It would
be tempting to say "the one lever whose size the ISA could predict has now been
pulled, so nothing predictable remains" — and this round is not entitled to that,
because the ISA's prediction was falsified by 2–4× and most of what was actually
recovered is unexplained. An unidentified cause of that size is not evidence that
no further lever exists; it is evidence that reading the code did not find the one
that mattered. What is solid is the instrument limit: a round 5 of micro-items
inside `Part` would spend hardware sessions on changes it could not weigh,
whatever they saved. The two candidates large enough for this
bench as it stands are both listening decisions rather than checksum ones: the
spec's Stage 2 block entry point, which is *not* bit-exact because
`Instrument::process` interleaves the decks per sample and CHOKE reads the
priority deck each sample, and the voice cut at ≈7.9 points. Lowering the bench's
floor — a same-build A/B, or any way to hold layout fixed across a change — may be
worth more than either. The `n = 1` finding says what to check first, not what to
change: the bench's call shape and the shipped call shape have to be compared
before the numbers mean anything. Done here, that comparison came out in the
bench's favour — 96 on both sides — but it was checked after the fact rather than
before, which is the habit worth keeping.

**Merge status: merged as `bc0ff78`** (`--no-ff`, matching rounds 1–3). Rounds
1–3 landed as evidence and documentation only; this is the first round in the
sequence that changes `engine/`, so it went through a two-reviewer pass — one on
the `engine/` diff with bit-exactness as its first question, one auditing every
figure against the CSVs — and a fix round before merging. Neither reviewer found
a behavioural defect. The audit found seven prose defects, which is the same
distribution rounds 2 and 3 produced.

### DTCM instrument placement ✅ (same-binary A/B)

Branch `codex/perf-tcm-ladder`. Spec
`docs/superpowers/specs/2026-07-30-dtcm-instrument-ab-design.md`, evidence
`docs/bench/2026-07-30-8702bc8-system.{md,csv}`.

This round removes cross-build layout drift from the comparison. The AXI row
and DTCM row live in one firmware image, call the same `proc_inst`, share the
same AXI-resident counter and output arrays, and differ only in the address of
the live `Instrument`. A fail-closed host check rejects unequal A/B checksums.
The linked DTCM object is 49,792 bytes at `0x200005b0`; total DTCM use is 51,248
of 131,072 bytes (39.10 %).

| run | AXI avg | DTCM avg | saving | AXI max | DTCM max | saving |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 105.28 | 104.64 | **0.64** | 109.49 | 108.77 | **0.72** |
| 2 | 105.28 | 104.64 | **0.64** | 109.49 | 108.75 | **0.74** |

Both runs return checksum `483e8e82` for both rows. The saving clears the
pre-registered 0.50-point threshold in both metrics and both runs; direction
and spread checks pass. **Keep the DTCM placement for the firmware shell.**
It does not close the budget, so the next round is the ordered ITCM A/B. No
voice count, DSP setting, or audio buffer residency changed.

### ITCM audio hotset ✅ (paired AXI/ITCM builds)

Branch `codex/perf-tcm-ladder`. Spec
`docs/superpowers/specs/2026-07-30-itcm-audio-hotset-design.md`, evidence
`docs/bench/2026-07-30-d570e47-system-{axi,itcm-hot}.{md,csv}`.

The ten-object audio hotset links 41,984 bytes of executable code at
`0x00000100`; the reserved null prefix makes its total ITCM address footprint
42,240 of 65,536 bytes (64.45 %). The `Instrument` state remains in DTCM.
Layout identity is carried on the wire and in evidence, so an AXI image cannot
be accepted as an ITCM run.

| run | AXI avg | ITCM avg | paired saving | AXI max | ITCM max | paired saving |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 104.66 | 98.65 | **6.01** | 108.80 | 102.64 | **6.16** |
| 2 | 104.67 | 98.65 | **6.02** | 108.85 | 102.71 | **6.14** |

All gate captures return checksum `483e8e82`. Across the stricter comparison
of every ITCM run against both AXI runs, the minimum saving is **6.01 average /
6.09 maximum**, far above the pre-registered 1.00-point threshold. **Retain the
ITCM hotset for later firmware-shell integration.** The current production
shell has no ITCM loader, so it cannot claim this placement yet. The real
benchmark callback agrees: 98.81–98.82 % average and 102.78–102.79 % maximum.

The first hardware probe had placed the first weak function at
`0x00000000`. It kept executing but collapsed the instrument audio sum to
zero; the BBD checksum became `50306fb5`, the fold of the constant eight
active voices. That apparent eight-point saving was rejected. Reserving
`0x00000000..0x000000ff` restored every AXI checksum in two diagnostic runs
and in both accepted ITCM runs. The retained, sound-preserving result is the
six-point figure above. Since the maximum remains above 100 %, the next
ordered round at that point was `-O3`/LTO; half-rate reverb remained behind it.

### BODY playability ✅ (extends M5j)

BODY shipped in 2.14.0 measured and in budget, but the first extended session
with it (2026-07-28, the owner playing FLOW in VCV Rack) found it hard to get a
usable sound out of. Three separate causes, all found from that one report, all
fixed and released in 2.15.0.

**The bow droned at 200 Hz.** In FLOW the click zone re-fires its impulse
continuously, and the re-arm ran on a fixed 5 ms timer — which is exactly
200 Hz at 48 kHz, whatever note was played. An FFT of the owner's recording
showed an exact harmonic series on 200.000 Hz with the played note absent from
it. The re-arm now runs at the fundamental (`engine/body/exciter.h`), which is
also the physically right answer: every impulse then arrives in phase with the
wave already circulating in the string, which is what a bow does. One coupling
is documented at the fix: the filter is not reset between re-arms, so a shorter
period raises per-impulse amplitude as well as rate. Measured flat at 0.230 for
periods down to ~60 samples; the 110–880 Hz pitch contract is 436–55 samples, so
the whole range sits in the flat part and tracking f0 does not tilt excitation
level across the keyboard. Anyone widening that contract upward leaves the flat
region and should re-measure.

**A driven resonator has no natural level.** A struck resonator decays; a
continuously driven one accumulates until dissipation balances input, and that
ratio is the structure's own gain — which for BODY spans three orders of
magnitude across MATL and DECAY. Measured: one FLOW voice peaked at 629, 56 dB
over full scale, against SYNTH which stays inside ±3 dB. This was on `main`,
predated the bow fix, and affected every RESO zone. `VoiceT` does not have the
problem because FLOW there sustains an envelope over an oscillator.

The first attempt was a feed-forward compensation from a closed-form sustain
gain, and it failed: the implementer measured 20–34 dB of error and correctly
refused to widen the tolerance. Root cause — `KsString::process` already
contains `fclamp(s, ±20)`, so the real loop is bounded long before the damping
filter approaches lossless, and no closed form that ignores that clamp can
predict it. The spec records this and three failed measurement designs in its
§8 errata; the plan carries a SUPERSEDED banner.

What shipped instead follows M5a's own rule — *where opening a path lets a value
diverge, add the bounding nonlinearity the instrument already has rather than
re-imposing a ceiling*: a `tanh` ceiling on the FLOW sum in
`BodyVoice::process`, `kFlowSatCeil = 0.4`. Worst-case peak 629 → 0.283. STEP is
untouched — the ceiling sits behind the `_sustaining` branch, so a struck note
takes the same path it always did. A side effect worth knowing: the `cross-deck excitation` test, red since the bow fix, went
green again.

**FILTER died before its fade began.** Below FILT ≈ −0.4 BODY went almost
silent. The cause is that BODY's loudness-versus-brightness curve is a straight
line spanning only 10.8 dB, against SYNTH's 27.9 — so the *shared* left-hand
fade, sized for an engine already ~28 dB down by the time it engages, had to
erase everything in 0.2 of knob travel. Fixed with a 17 dB cubic loudness tilt
in `set_cutoff_hz`, applied after the energy follower so a dark voice is not
stolen early. Left half went from −1.5 / −2.9 / −4.4 / −5.7 dB to
−3.5 / −8.0 / −13.6 / −20.6 dB. Confirmed by ear.

**Two things measured and deliberately left open.**

- **FLOW-to-STEP spread is 34 dB**, not the 8–12 dB the owner asked for. The
  ceiling cannot close it — 0.4 and 0.2 give the same spread — because the
  residual sits at the *low* end, and that end is not yet understood. Worth
  understanding before anything is designed for it.
- **A struck note reaches 1.088**, i.e. over full scale, at some settings. This
  is pre-existing, not introduced here, and it affects STEP. `test_body_engine.cpp`
  records the finding in a comment next to the FLOW peak test.

Spec: `docs/superpowers/specs/2026-07-28-body-sustain-gain-design.md` (§1–§4
abandoned, see its §8). Plan:
`docs/superpowers/plans/2026-07-28-body-sustain-gain.md` (superseded, do not
execute).

2.15.1 is a separate, non-engine release: the owner's `drone.vcvm` panel became
the init patch (`host/vcv/src/init_patch.hpp`).

### BBD part engine ✅

`ENGINE_BBD` (`EngineId` = 5; VCV's `ENG` switch state 4) is a fifth
selectable part engine. Unlike every other engine it carries no synth
voices at all: `BbdEngine::trigger()` is a no-op, and its
`consumes_input()`/`process_in()` route the deck's incoming audio straight
into two independent `BbdEcho` lines, one per channel
(`engine/parts/bbd_engine.{h,cpp}`).

Three coupled movements, one branch each, landed together for **2.17.0**:

1. **An audio-rate cross-deck bus** — `_deck_tap[PART_COUNT][2]` and
   `Instrument::deck_tap(p)`, a fixed one-sample latency in both directions,
   independent of CHOKE. It gives a deck genuine audio-rate access to its
   neighbour's post-FX output, where the existing `_dry_tap` BODY uses is a
   96-sample-decimated control-rate bus. As a side effect the sampler now
   records the neighbouring deck without an external patch cable.
2. **`ENGINE_BBD = 5`** — the engine itself. Its five modulation lanes
   become SOURCE → DRIVE, PITCH → the BBD clock, SIZE → the delay division,
   MOTION → FEEDBACK, LEVEL → dry/wet MIX; the VOICE-row reassignment
   (RESONANCE → TILT, SUB → FEED, FILT → LOSS, DECAY → TAIL, and
   `STAGES_A/B` as the PITCH-lane base captioned `BEND`) is covered by
   "BBD PITCH / tape TIME surface" below and by `host/vcv/README.md`'s BBD
   section, not repeated here. Switching a deck to BBD defaults FLUX off and
   "Route: other deck" on, so an unpatched BBD deck still has the sibling's
   output to feed its line.
3. **FLUX reverts to a tape echo.** `Flux` no longer wraps `BbdEcho`; it now
   holds `TapeEcho<kTapeSamples>` (`engine/fx/tape_echo.h`), an interpolating
   delay line behind an 800 Hz band-pass and feedback saturation — the
   pre-M1.6 mechanism, restored. **The "FLUX → BBD" entry above therefore
   describes an interlude, not FLUX's current mechanism**: the echo effect
   on every engine (including this one) is a tape echo again; the
   bucket-brigade device that survives is this part engine, reachable only
   on the BBD deck. `host/vcv/README.md`'s BBD section opens with exactly
   this warning.

**Two physical facts the design settled by review (rev. 2, after rev. 1 got
the central mechanism wrong — two reviewers caught it independently), both
counter-intuitive and load-bearing:**

- **A bucket-brigade line writes and reads on the same clock, so its
  steady-state pitch is unity at every stage count.** Transposition exists
  only *while the clock is moving*, and only survives with feedback up —
  `LANE_PITCH` is multiplicatively gated by `LANE_MOTION`; at FEEDBACK 0,
  moving PITCH is inaudible.
- **Each repeat is mostly silence** — measured ~75–100 ms of audible content
  per 250 ms repeat, shrinking with every upward pitch step. The gappy,
  duty-cycled character is the engine's most distinctive trait, not an
  artefact.

**CPU: measured directly at the engine's own worst case, not inferred from
a mismatched operating point — and it fits, but only proven at `-O2`.** The
design spec's §2 declined to trust a naive per-line estimate (4.55 points,
corrected in the same section to 5.9–6.3) because it was taken at
`Flux::init`'s boot-default operating point, not the roughly 3.9×-faster
tick rate a deck's clock-ceiling actually runs at. Two real-hardware
sessions since measure `inst_bbd_engine_worst` — both decks on
`ENGINE_BBD`, shortest division, clock-ceiling PITCH, maximum
COLOR/FEEDBACK/MIX, both excitation sources live — directly instead:

| session | freeze | `inst_bbd_engine_worst` avg / max % |
|---|---|---:|
| `docs/bench/2026-07-31-b9afe47-bbd-engine.md` | off (a named harness limitation, not the literal worst case) | 82.88 / 86.19 |
| `docs/bench/2026-08-01-19f7560-flux-tape.md` | on, held across the measured window | 94.16 / 98.39–98.56 |

Both sessions link `-O2`. **No `-O3` measurement of this row exists**, so
whether it clears the gate with the margin O3 showed elsewhere (see the
CPU-status note at the top of this document) is not established here.

**Update, 2026-08-04 (signal-path regression): the sentence above is
superseded — the `-O3` measurement now exists.** Closing this exact gap was a
named scope item of that round. `inst_bbd_engine_worst` was measured at
`-O3`, layout `axi`, profile `regress` (`system` + `bbd` in one image), on the
Daisy Seed, in both trees of a same-session A/B:

| tree | `inst_bbd_engine_worst` avg / **max** % |
|---|---:|
| constructed baseline `6134b4f` (`engine/` at `19f7560`) | 88.89 / **92.91** |
| `bd01608` (ancestor of `main`) | 92.65 / **96.91** |

So the row **fits at `-O3` on `main`** with about 3 points of margin, and the
measured difference between the two trees is **+4.00 points** against that
row's own repeat band of **0.19** — twenty-one times it. The decision gate
`instrument_worst_bbd_dtcm` in the same captures reads **96.43 %** offline /
96.69 % in the real callback. Note the identity difference against the two
`-O2` rows in the table above: different profile, different optimization,
different image — those figures are **not** subtracted from these, which is
why the round constructed a baseline instead of reusing a capture. Evidence:
[signal-path regression](bench/2026-08-04-2101349-signal-path-regression.md),
from captures [baseline](bench/2026-08-04-6134b4f-regress-axi-o3.md) and
[main](bench/2026-08-04-bd01608-regress-axi-o3.md).

Movement 1's cross-deck bus is folded into both rows above (both excitation
sources are on by default) — its own isolated cost, measured separately at
~5.75–6.03 points/block in a two-sampler mutual-routing worst case
(`docs/bench/2026-07-31-20eafed-deck-bus.md`), is itself conditioned on a
200-block settle and not confirmed as the steady-state figure.

Spec: `docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md` (all
three movements). Plans: `docs/superpowers/plans/2026-07-31-bbd-part-engine.md`
(movements 1–2), `docs/superpowers/plans/2026-08-01-flux-tape-echo.md`
(movement 3).

## Planned

### M5k — ZAP ⬜

Monophonic two-oscillator FM/AM percussion part engine whose PITCH lane selects
percussion archetypes. The design is complete, but implementation has not
started.

Spec: `docs/superpowers/specs/2026-07-18-zap-percussion-engine-design.md`

### M5l — PULL ⬜

Chord gravity between the two decks, using the existing scale, root, and chord
layers. PULL is deliberately the last engine milestone before M6. The design is
complete, but implementation has not started.

Spec: `docs/superpowers/specs/2026-07-19-pull-chord-gravity-design.md`

### M6 — Hardware prototype ⬜ (after M5l; **needs a new spec**)

An instrument of its own, built on a **Daisy Patch Submodule**: a thin shell
hosting `engine/`, plus the physical surface around it — controls, LEDs, CV +
gate + V/Oct + clock I/O, and preset persistence. **First milestone that runs on
hardware.** Nothing of it is implemented.

**2026-08-07 — the submodule is measurable without a debug probe.** The bench
has a second transport (`--transport usb`): `dfu-util` loads it through the
Daisy bootloader and it reports over USB-CDC, so a board with no SWD pins
soldered on can be measured with nothing but the cable that powers it. Proven
on the Seed against its own known number; the submodule run itself still waits
on a board switch in the bench. Two defects surfaced on the way, and both were
in the **shipping** firmware rather than only in the measuring tools: the WAVE
bank sat on the bootloader's app address, and DTCM-resident code had no load
address in SRAM. Neither was visible while a probe was always attached.
USB-CDC costs 0.66 % of the block budget — measured, explained, and it cancels
when like is compared with like. See `bench/README.md` ("Two transports") and
`docs/bench/2026-08-07-transport-semihost-vs-usb.md`.

**The existing shell specification is superseded**
(`docs/superpowers/specs/2026-07-12-spotykach-firmware-shell-design.md`). It was
written for Spotykach's fixed panel and assumes that device's surface throughout
— its pads, its three 3-position switches, its LED ring, its CYCLE control. With
the Spotykach port cancelled, none of that is given any more, and the panel is
now a design question rather than a constraint to fit.

What survives from it is the interaction logic, and it is worth reading for
that: release-based tap/hold gestures, the edit-layer model, preset persistence,
and the deferred scale gestures (ALT-hold inspect, ALT+TUNE scale select,
ALT+PITCH-pad mode cycle) are all independent of which knobs exist. What does
not survive is every mapping onto a specific control.

So M6 now decomposes into two pieces, in order:

1. **Panel design** — decide the prototype's control surface. The reduction
   round is **cancelled**: the envelope spec
   (`docs/superpowers/specs/2026-08-08-fireflow-hardware-envelope-design.md`)
   fixes the hardware at **60 HP with the full control set** (82 runtime
   params on 80 physical positions, BEND sharing ATTACK's knob). What remains
   is a **regrouping** pass — bounded, tested in Rack via the hardware-mode
   panel generator (`host/vcv/res/gen_hw_panel.py`), max. 3 rounds, static
   labels enforced from round 1. The parked hardware-placement questions from
   earlier milestones (M4.10's COLOR placement, the BBD deck's contextual
   VOICE row, the per-deck SEND) come due in this round — as placement, not
   as cuts.
2. **Bring-up** — the shell itself, against the panel decided in step 1.

Until step 1 has a spec, M6 has no implementable definition, and the "spec ready"
status this milestone carried since 2026-07-12 no longer holds.

**Flow layer — Plan A and Plan B both landed 2026-08-05.** The compact-macro-
module spec (`docs/superpowers/specs/2026-08-05-flow-machine-design.md`)
splits its own build into two plans. Plan A — `engine/flow/`: the seeded
terrain generator, the six-macro story layer, weather, the NEW gesture
family, render-host scenario wiring and the audio sanity gates — is built
and green. Plan B — the VCV module **FireFlow Glow**, a second module in the
`Fireflow` plugin: 12 HP, six macro knobs and one NEW button over a
generated terrain, driving `engine/flow/` instead of `Fireflow.cpp`'s
one-control-per-parameter surface — is also built. Its panel is drawn at
true hardware dimensions specifically so it can double as step 1's 1:1 M6
panel draft above; see `host/vcv/README.md`'s "FireFlow Glow" section for the
macro table, the NEW gesture family and its LED signatures, the CV/clock
jacks, and terrain codes.

**Verified in Rack 2026-08-06.** The branch merged with its three hand-check
lists still unrun — no agent has a Rack — and Bastian ran them on the built
plugin before the merge: the module wakes on the house seed and sounds, the
six macros and five CVs move it, the clock overrides tempo; the whole NEW
gesture family reads correctly, including the refusal flash the plan's own
code could not light; and a terrain survives copy, paste, save/reload and
Ctrl+I. All three confirmed. The lists themselves are in
`.superpowers/sdd/2026-08-05-flow-glow-vcv-module/task-{3,4,5}-report.md`.

With Glow built, the open items in
`docs/superpowers/specs/2026-08-05-flow-listening-notes.md` are reachable
for the first time: that file's own listening rounds were called off because
a rendered file can only judge level, never whether a knob feels alive under
the hand or a terrain is worth staying in, and answering that needed an
instrument to play rather than a file to play back. Glow is that instrument.
The house seed in `engine/flow/taste.h` remains the one open item Task 6
called out explicitly: a measured placeholder, not the by-ear choice the
design spec asks for, now answerable on the module that exists to answer it.

**Taste tables (`glow-taste-tables`, 2026-08-06) — two open findings, both
left for the owner's ear.** The branch encoded Bastian's by-ear rules into
`engine/flow/taste.h` (vetoes, redrawn curves, an archetype window, musical
weights, the COMP band move, a per-domain adventure draw) and its final
review found no Critical issues, but two gates carry findings that a code fix
cannot close — both currently live only in `taste.h`'s own header comments,
which is not where this project looks for open questions, so they are
recorded here too:

- **`kBlendSpikeDb` (the NEW-blend level gate, §7.8) is RED and stays red.**
  Two of the four asserted seeds breach it (0xD0D at +6.49 dB, 0xC0C0 at
  +6.36 dB, both in the gate's window 3). Measured on the population the gate
  actually asserts over (masters 1..2000, the 85 non-Sampler/no-engine-switch
  terrains), the worst in-gate spike exceeds the 6 dB bound on 24/85 (28.2 %)
  at HEAD and 31/85 (36.5 %) at the branch point — the branch made the rate
  BETTER, not worse; the gate reads red only because the taste tables moved
  two of the ten fixed candidate seeds into the breaching third that was
  always there. **The honest resolution is a spec tolerance in §7.8 of
  `docs/superpowers/specs/2026-08-06-glow-taste-structure-design.md`** — either
  the blend genuinely has to hold level (and the generator changes until it
  does) or §7.8 states the fraction it tolerates and the gate becomes a
  distribution check — **not a code fix**, and not raising `kBlendSpikeDb` to
  cover the two seeds.
- **The calm-corner floor (`kCalmCornerRmsMin`, §7.8) — a mute finding, green
  only by seed-set luck.** Over the same 1 566-terrain calm-corner scan, 103
  terrains (6.6 %) now render functionally mute at their calm corner (rms at
  or below the silence floor), down from 193 (12.3 %) at the branch point —
  still roughly one drawn terrain in fifteen. Awaiting the owner's ruling
  (same shape of question as the spike gate: does the generator guarantee an
  audible calm corner, or does §7.8 state a tolerated fraction).
- **The coupling between them, so a partial fix does not reopen the other
  half:** both findings trace to the SAME one span. The drone SHAPE cap
  (`P_SHAPE_A/B` drone span `{0,1}` → `{0,.25}`) is what retired the earlier
  `kCalmCornerRmsMax` ceiling breach at master 0x707 (reverting it alone puts
  0x707 back over the ceiling; reverting either of the other two candidate
  edits at that commit does not) — and reverting that same cap is also what
  un-mutes master 0x404 (1.35e-03 reverted vs. 7.00e-08/6.93e-08 for the other
  two). So if the ruling on the mute finding is "the calm corner must stay
  audible," the drone SHAPE cap is the first place to look — and the ceiling
  breach it retired comes back with it. See `kCalmCornerRmsMax`'s and
  `kCalmCornerRmsMin`'s comments in `engine/flow/taste.h` for the full
  per-commit isolation.

## Build & verify

```bash
source env.sh            # optional: toolchain on PATH, CC/CXX
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # Debug fails the render-hash gates
cmake --build build
ctest --test-dir build --output-on-failure
./build/render.exe host/render/scenarios/dorian_vs_drift.json out.wav mods.csv
```

See the README for the full desktop build instructions, and
`docs/upstream-firmware.md` for the original Spotykach firmware still in the
tree.

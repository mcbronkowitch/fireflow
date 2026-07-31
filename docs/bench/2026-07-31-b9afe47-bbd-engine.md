# Bench evidence 2026-07-31 — the BBD part engine on hardware (`b9afe47`)

Measured on the real Daisy Seed (STM32H750) on this desk, `bench/` profile
`system`, execution layout `itcm-hot`, optimization `o2`, `--repeat 2`,
`--program-qspi` beforehand. Raw capture:
`2026-07-31-b9afe47-system-itcm-hot-o2.md` / `.csv` in this directory. Every
figure below is from **that single build and that single session**, so the
comparison rows are genuine comparisons.

Spec: `docs/superpowers/specs/2026-07-31-bbd-part-engine-design.md`, §8.3 row 1.

## Row real, not stale

`grep -c "inst_bbd_engine_worst" bench/build/bench.map` returned **2**
(`.text._ZN5bench12_GLOBAL__N_127setup_inst_bbd_engine_worstEv` and its
`.rodata` string pool — the assert messages, see below). Read from
`bench.map`, not from the memory table, because the bench build can relink a
stale object and still print a plausible figure for code that was never
linked.

## The row asserts what it is measuring

Movement 1's Task 5 shipped a row that stayed on `ENGINE_SYNTH` and therefore
never reached `process_in` at all — it measured a SYNTH deck under a BBD row's
name, and nothing noticed, because a wrong-but-stable configuration returns a
wrong-but-stable checksum and `run.py` only compares run against run.

`configure_inst_bbd_engine_worst` therefore ends with, per deck:

```
assert(inst.engine_id(p) == ENGINE_BBD);
assert(inst.bbd_div(p) == 0);                        // 1/32, the shortest
assert(inst.bbd_clock_hz(p) >= bbd_tuning::kClockMaxHz);
assert(!inst.bbd_frozen(p));
```

These are live: the bench builds `-O2` with `NDEBUG` undefined, and the
assert message strings are present in the linked image
(`.rodata...setup_inst_bbd_engine_worst`, and
`inst.bbd_clock_hz(p) >= bbd_tuning::kClockMaxHz` is readable in
`arm-none-eabi-strings build/bench.elf`).

## What the row does

Both decks on `ENGINE_BBD` — the engine's **stereo pair per deck**, i.e. four
BBD lines in the instrument. On top of `configure_inst_worst`'s full FX
context (GRIT, COMP, the reverb at 0.5 mix / 0.9 diffusion, the master
limiter, the modulation plane at density 1 on both decks), plus:

- `LANE_PITCH` pinned at 1.0 with the quantizer in `QuantMode::Free`, and
  `LANE_SIZE` pinned at 0.0 (`kDivs[0] == 1/32`) — together these put the
  clock on `kClockMaxHz`, which is what the per-sample cost is proportional
  to: `BbdLine::Process` runs `f_clk / f_s` cell ticks per sample.
- `LANE_MOTION` (FEEDBACK) and `LANE_LEVEL` (MIX) pinned at 1.0.
- `LANE_SOURCE` (DRIVE) left **active** on the plane, so
  `BbdEcho::SetDrive`'s unchanged-value guard is defeated every control
  block — a `std::pow` per line per block, which is the worst case and what a
  plane-driven patch actually does.
- COLOR at maximum (inherited from `configure_inst_worst`'s `set_color(p, 1)`),
  so the two lines of each deck run at spread clocks.
- RESONANCE at 1 so the feedback-path tilt one-pole is live
  (`BbdEcho::fb_path` is identity at `tilt_ == 0`), SUB at 1 so the input
  arrives at full level, DETUNE at 0 for the fastest clock slew.
- Both excitation sources on (`other_deck` and `audio_in`), because a
  voiceless engine with nothing routed to it measures an idling delay.
- **FLUX off on both decks.** §3's first transitional hazard is that between
  movements 2 and 3 a BBD deck runs *three* BBD lines — the engine's stereo
  pair plus FLUX's own mono line behind it — which is neither what §5.11
  describes nor what §2 prices. Leaving FLUX on would make this row silently
  keep measuring that transitional shape after movement 3 lands, which §3
  explicitly asks it not to.

### One deliberate departure from §8.3

§8.3 asks for the freeze **engaged**; this row leaves it **off**. Two reasons,
stated rather than hidden:

1. The freeze is reached only through `Part::_gate_edge` → `set_gate`, i.e.
   through the composed gate, which is a countdown (`_gate_ctr`) that goes
   high for `_gate_len` samples after a fire. There is no way to hold it high
   from the `Instrument` API for a whole measured window, so a "freeze
   engaged" row would in practice be a row whose freeze ramps in and out
   under the measurement.
2. Freeze-engaged *is* the marginally heavier per-sample path — `_push_freeze`
   turns `SetFeedbackDcBlock` on, adding a DC blocker to each line's feedback
   path — but only the DC blocker, because RESONANCE at 1 already makes the
   tilt one-pole live in this row. So the understatement is bounded and named:
   **one first-order DC blocker per line per sample** (`BbdEcho::fb_path`'s
   `dc_on_` branch — one multiply, two adds, two stores, one flush compare),
   across four lines, and nothing else.

This row is therefore a *near*-worst case with a named residual, not the
literal §8.3 configuration. Movement 3 should revisit it if the freeze becomes
reachable from a bench setup.

## The figures

`pct_max` is the gate. Both runs, same binary, same session:

| row | run 1 avg / max | run 2 avg / max |
|---|---|---|
| **`inst_bbd_engine_worst`** | **82.88 / 86.19** | **82.89 / 86.27** |
| `instrument_worst` (comparison) | 96.44 / 100.19 | 96.44 / 100.21 |
| `instrument_worst_bbd` | 100.41 / 104.41 | 100.42 / 104.43 |
| `inst_worst_deck_bus` | 71.17 / 74.75 | 71.17 / 74.77 |
| `synth_4_voices` | 17.66 / 17.89 | 17.66 / 17.89 |
| `fx_flux_sdram` | 13.31 / 13.40 | 13.31 / 13.40 |

Checksum `7636f0a7`, identical across both runs.

## This is a USE cost, not a code cost

Stated explicitly, because movement 1's note records what conflating the two
did: it produced a 140× discrepancy that took a separate experiment to
explain.

`inst_bbd_engine_worst` is a **use** cost — what the whole instrument costs
when both of its decks are being played through the BBD part engine, including
the modulation plane, GRIT, COMP, the reverb, the limiter and the cross-deck
bus. It is not the engine's isolated code cost and cannot be turned into one
by subtraction: §8.2's first structural caution is that **component rows do
not sum**, with the repo's own figure — component rows summing to ~120 % of
budget while `instrument_worst` measured ~159 %, a 39-point gap with no named
owner.

## Against §2's estimate — reported, not explained

§2 estimates **≈11.8–12.6 `pct_avg` for a stereo BBD deck**, against a SYNTH
deck's 17.60–18.21. This run's `synth_4_voices` is 17.66 avg, so the SYNTH half
of that comparison reproduces exactly.

The BBD half **cannot be read off this row**, and the honest position is to say
so rather than to derive a number and defend it. What the row supports is a
whole-instrument bound, and one bracketing observation:

- `instrument_worst` (96.44 avg) and `inst_bbd_engine_worst` (82.89 avg) differ
  by **13.55 points** for the whole instrument. Per deck the swap removes four
  SYNTH voices *and* a FLUX line and adds a stereo BBD pair. If the removed
  parts cost anything like their own rows (`synth_4_voices` 17.66,
  `fx_flux_sdram` 13.31), the added stereo pair would have to be *well above*
  §2's 11.8–12.6 to close the arithmetic — but §8.2 says that arithmetic is
  exactly the kind this repo has already been burned by, so it is recorded as
  an open question, not as a measurement.

**What would settle it** is a same-build A/B of the kind movement 1 used for
the bus: this row measured once as-is and once with both decks left on
`ENGINE_SYNTH` and FLUX off, so the only difference is the engine. That row
does not exist and is not created here.

## Two things this run moved, both named

1. **The DTCM+BBD decision gate moved by about 1.2 points at an unchanged
   checksum.** `instrument_worst_bbd_dtcm` reads 103.78–103.83 offline here
   against the 102.64–102.71 recorded at `docs/roadmap.md:145`, with the
   checksum still `483e8e82` — bit-identical DSP. This is §8.2's second
   structural caution happening: composition and layout move the gate by
   points at an unchanged checksum. Two things in this build changed layout —
   `bbd_engine.o` entering the ITCM hotset (which shifts every object after it
   in the section) and `workloads_system.o` gaining a row. The gate's verdict
   is unchanged: it did not fit before and does not fit now.
2. **The ITCM hotset grew.** 41,984 → **48,800 bytes** resident, ending at
   0xbfa0, with **16,480 bytes free** under the hard 64 KiB `ASSERT`. Per
   object, from `bench.map` (48,388 bytes attributed, the rest alignment):

   | object | bytes |
   |---|---|
   | `synth_engine.o` | 10,468 |
   | `voice.o` | 8,668 |
   | **`bbd_engine.o`** | **5,992** |
   | `part.o` | 5,716 |
   | `instrument.o` | 5,592 |
   | `reverb.o` | 3,908 |
   | `flux.o` | 3,484 |
   | `grit.o` | 1,516 |
   | `comp.o` | 1,048 |
   | `bbd.o` | 1,024 |
   | `part_fx.o` | 972 |

   Note `bbd.o` at 1,024 bytes: `BbdLine::Process` (0x3dc) is **not** in it any
   more, because the linker took `bbd_engine.o`'s weak copy. That is the same
   fact the guard failure below reports, seen from the other side. The rest of
   the section's growth over the 41,984 recorded at `docs/roadmap.md:145` is
   not attributable to this task alone — that figure is from a 2026-07-30 build,
   many commits back.

## The ITCM guard was already failing on this branch

Not a hypothetical. `bench/itcm_placement.py`'s `HOT_SYMBOL_FRAGMENTS`
requires `spky::BbdLine::Process(` to link *inside* ITCM.
`BbdLine::Process` is a **weak** symbol, and from Task 4 onward `bbd_engine.o`
carries a copy of it. Measured on this tree with `bbd_engine.o` removed from
`itcm_hot.lds` — i.e. the branch's state before this commit:

```
ERROR: representative ITCM symbol is outside ITCM:
       spky::BbdLine::Process(float) at 0x2400bf1c
```

With `bbd_engine.o` in the hotset all three resolve inside ITCM
(0x100–0x10000):

| symbol | address | size |
|---|---|---|
| `spky::Flux::process(float&, float&)` | `0x00003a34` | 0x45e |
| `spky::BbdLine::Process(float)` | `0x000042ec` | 0x3dc |
| `spky::BbdEngine::process(float&, float&)` | `0x00004adc` | 0x308 |

`spky::BbdEngine::process(` was added to `HOT_SYMBOL_FRAGMENTS` in the same
commit: `BbdLine::Process` proves `fx/bbd.o` landed, and the engine's own
per-sample kernel is a separate translation unit and a separate hotset line.

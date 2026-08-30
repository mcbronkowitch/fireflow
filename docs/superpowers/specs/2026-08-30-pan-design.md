# PAN — design

**Date:** 2026-08-30
**Status:** designed, not implemented
**Scope:** engine (`Center` + the mix stage), both VCV modules, `param_table.h`.
The `shell/` firmware and the CV surface are explicitly out (§9).

## 1. Concept

One bipolar knob per deck, `PAN_A` / `PAN_B`, placing that deck in the stereo
field of the master mix. Range −1 … +1, init exactly 0.

The slot has been held open on the hardware plate since 2026-08-30: `LEVEL_SLOTS[0]`
is empty on purpose, and `test_level_band_holds_an_empty_slot`
(`host/vcv/res/test_hw_panel.py:830`) exists to stop a tidy-up from closing it.
The big panel has a matching hole — `FX_BOT[1]`, freed when MULT was retired
(`host/vcv/res/gen_panel.py:318`) and never regrouped. PAN fills both. **No
control moves and no row is re-pitched.**

## 2. What was measured before anything was decided

Whether a deck's own output is already stereo decides whether a balance law
loses content at the stops. It was measured, not assumed.

**Setup:** `Instrument::init(48000.f)` — engine only, no FX chain — both decks
set to the same engine, FLOW (no `set_step`), boot defaults otherwise. 5 s, one
sample per `process()` call so `deck_tap()` is read at full rate; the first 0.5 s
dropped. Mid = ½(L+R), side = ½(L−R), RMS of each over deck A's tap.

| Engine | midRMS | sideRMS | S/M | L/R correlation |
|---|---|---|---|---|
| TEST_TONE | 0.157957 | 0.000000 | 0.0000 | 1.0000 |
| SYNTH | 0.062148 | 0.017828 | 0.2869 | 0.8483 |
| WAVE | 0.000000 | 0.000000 | — | — |
| BODY | 0.050245 | 0.016287 | 0.3241 | 0.8112 |
| BBD | 0.026990 | 0.002287 | 0.0847 | 0.9875 |
| SAMPLER | 0.000000 | 0.000000 | — | — |
| FEED | 0.057119 | 0.000000 | 0.0000 | 1.0000 |

**WAVE and SAMPLER were silent in this setup and are therefore UNMEASURED, not
mono.** SAMPLER has no buffer without an `FxMem`; WAVE's silence was not chased.
Do not quote their rows as evidence of anything.

FEED and TEST_TONE are genuinely mono (side energy exactly 0, correlation 1).
BBD is very nearly so. Only SYNTH and BODY carry real side energy, and even
there it is about a third of the mid, not half — that is `kPanFan`'s four-voice
spread (`synth_engine.cpp:344`) seen from the mix.

**The FX chain is not in this measurement.** FLUX is stereo and would add width.
That cuts one way only: more side energy strengthens the case for a rotation law,
so the balance law below was chosen against the *weaker* version of its own
evidence, not the stronger one.

## 3. The law

Balance, unity at centre:

```
gL = min(1, 1 − p)
gR = min(1, 1 + p)
```

| p | gL | gR |
|---|---|---|
| −1 | 1 | 0 |
| 0 | 1 | 1 |
| +1 | 0 | 1 |

Only the fading side moves; the other stands at unity. What this costs is
stated plainly: at a hard stop the opposite channel's content is gone, so a
SYNTH or BODY deck loses its voice fan (§2: S/M ≈ 0.29 / 0.32). A FEED or BBD
deck loses nothing, because it had nothing to lose. Rotation (mid/side) was
considered and rejected: it buys back a third of the side energy on two of the
five sounding engines, at the price of a per-sample M/S matrix.

**Unity at centre is load-bearing, not a preference.** `p == 0` gives
`gL == gR == 1.0f` exactly, so the multiply is exact and a centred PAN cannot
move a render. The existing render-hash gates (`ctrl_identity`, `spky_tests`)
are expected to stay green with PAN present. That is an expectation the plan
**verifies with a `ctest` run**, not a claim this document makes.

## 4. Where the state lives

In `Center`, beside LVL — but **not folded into `gain_a()` / `gain_b()`**.

That distinction is the whole of the dry-only decision, and it contradicts the
reason LVL is in `Center` at all. `center.h:26` says LVL belongs there *because*
it multiplies the equal-power MORPH gain. PAN does not: the same `ga` / `gb`
feed the reverb send at `instrument.cpp:524`, and PAN must not reach it. So PAN
is a second, parallel per-deck pair — `pan_l(part)` / `pan_r(part)` — living in
`Center` for its control-tick machinery (`_cr`, `OnePole`) and for keeping
per-deck mix state in one place, and carrying a comment that says why it is
*not* where LVL is.

**Smoothing:** one `OnePole` per deck on the **position**, not on the two gains
— `init(_cr, 0.03f)`, `reset(0.f)`, mirroring `_lvl_smooth`. The gains are
derived from the smoothed position each control tick. One smoother instead of
two, and `reset(0.f)` → `process(0.f)` returns exactly `0.f` (`OnePole::process`
short-circuits inside its 0.0005 dead band), which is what §3's exactness rests
on.

30 ms is copied from LVL. It is a **first-try value with no listening pass**
(§9).

## 5. Where the gain lands

Four sites in `instrument.cpp` multiply by a deck gain. Which of them PAN joins
*is* the design:

| Site | What it is | PAN? |
|---|---|---|
| `instrument.cpp:464` | dry sum, null-reverb path | **yes** |
| `instrument.cpp:514` | dry sum, reverb path | **yes** |
| `instrument.cpp:524` | reverb send | **no** — the cloud stays centred |
| `instrument.cpp:370` | CHOKE sidechain (`pri_gain`) | **no** |

The send exclusion is the feature: the dry deck moves to the side while its
reverb tail stays in the middle.

The CHOKE exclusion needs its own argument, because the sidechain's stated rule
is "a sidechain follows what you HEAR" (`instrument.cpp:362-369`), and a panned
deck is still heard. The detector takes `max(|L|, |R|)`; under §3's law the loud
channel is always at unity, so folding PAN in would change the detector's answer
neither at centre nor at a stop. **This is an argument from the law, not a
measurement.** If it is ever doubted, measure it — do not reason about it twice.

PAN joins **after** `_deck_tap` and `_dry_tap` are written
(`instrument.cpp:455`), the same rule MORPH and the CHOKE duck already obey
(`instrument.cpp:460`): BODY's excitation bus and the deck bus must not pan.

## 6. The mod ring

One row in `MOD_DECK_TARGETS` (`host/vcv/res/gen_panel.py:676`):

```python
("PAN", "HOST", 1, 0.0),   # LANE_SIZE, x1/2
```

`MODK_HOST` means the term is computed host-side in knob space by
`spkymod::modded` (`host/vcv/src/mod_layer.hpp`) — **no new engine target slot,
no change to `FXT_COUNT`, no change to any lane fan-in table.** Exactly the path
COMP already takes.

`LANE_SIZE` is the slowest of the five (×1/2), so the motion is a drift through
the field rather than a ping-pong. It is shared with SMOOTH, RANGE, RES, RATE
and the REV targets, which means PAN breathes with the room rather than
independently — accepted deliberately.

Two properties fall out for free and should not be re-derived later:

- **Depth 0 is bit-exact.** `modded()` returns the knob by early return when
  depth is 0, and every host-computed depth boots at 0. The ring is a no-op on
  a fresh patch.
- **The swing is symmetric about the knob.** PAN is bipolar and boots at 0, so
  a lane term of ±1 at depth 1 sweeps the full width around centre. No offset
  correction is needed.

## 7. Surface

`PAN_A` / `PAN_B` are appended to the **end** of `APPENDED_PANEL_PARAMS`
(`host/vcv/res/gen_panel.py:658`), never added to `part_controls()` — that would
grow `PART_STRIDE` and shift every part-B and SHARED id. Same rule FILT, COLOR,
LINK, REV_MIX and PULL follow. The 49 `MOD_LAYER_PARAMS` ids behind them shift
by two; accepted, this is a dev alpha and saved patches may break.

| Panel | Position | Row reads |
|---|---|---|
| `Fireflow` (big) | `FX_BOT[1]`, `ROW_V2`, `SMKNOB`, label `PAN` | `LINK PAN GRIT LVL` |
| `FireflowHW` | `DECK_POS["PAN"] = (LEVEL_SLOTS[0], Y_B2L)`, size class `S` | `FB PAN GRIT SEND TONE` |

Rack param: bipolar, min −1, max +1, default 0.

### `param_table.h`

```
X(P_PAN_A, -1.f, 1.f, 0)   X(P_PAN_B, -1.f, 1.f, 0)
```

**Appended after `P_PULL`, never inserted.** `tests/param_impact_points.h` binds
its frozen vectors to the enum positionally; an insertion silently repairs every
later row onto the wrong parameter and the resulting red test reports the wrong
diagnosis ("these parameters died"). An append zero-fills the new tail slot —
and on a −1…+1 axis `0.0` is exactly centre, so the frozen points land on the
neutral value by construction. Two `apply_param()` branches follow.

`tests/test_param_impact.cpp` will pick both up automatically and compare a
lo/hi render; PAN hard left against hard right is audible, so it is expected to
pass without an exclusion.

## 8. Tests

**One guard is meant to go red.** `test_level_band_holds_an_empty_slot`
(`test_hw_panel.py:830`) currently asserts that `HW_SIZE` has no `PAN` entry and
that slot 0 is unoccupied. It is not deleted — it is inverted: slot 0 holds
`PAN_A`, and the band's order is PAN / GRIT / SEND.

New engine gates (`tests/test_pan.cpp`):

1. **Centre is unity.** After settling, `pan_l == pan_r == 1.0f` exactly, and
   the summed output of an instrument that had `set_pan(p, 0.f)` pushed is
   bit-identical to one that never had `set_pan` called at all.
2. **A stop is a stop.** PAN at −1 with deck B silent: the right channel's dry
   contribution is exactly 0.
3. **The send does not move.** PAN hard left with SEND up: the reverb return
   stays symmetric. This is the gate that defends §5, and it defends it against
   the *obvious* simplification — folding PAN into `ga`/`gb`, which would look
   like a tidy-up and would silently pan the cloud. Of the three, this is the
   one most worth having.
4. **No zipper.** A step from −1 to +1 produces no per-sample discontinuity
   above a threshold. The threshold is **not written here**: it is a runtime
   number and must come from a probe before it enters the plan.

Each gate is proved RED before it is made to pass (`docs/gotchas.md`,
memory `fireflow-tests-must-be-able-to-fail`).

### The rebake cascade

Four init values — `PAN_A`, `PAN_B`, `MODD_PAN_A`, `MODD_PAN_B`, all `0.0` —
and `test_panel.py:3250` requires every param in `PARAMS` to have an
`INIT_DEFAULTS` entry. Touched, per memory `fireflow-init-rebake-checklist`:

- `host/vcv/res/gen_panel.py` — `INIT_DEFAULTS`
- `host/vcv/res/test_panel.py` — the `approved` dict, a second independent
  transcription: **retyped, not copied**
- `tests/test_mod_layer.cpp` — "every host-computed depth boots at 0" gains two
  faces
- `bench/audition/init_patch.cpp` — hand-written push arms
  (`init_patch.cpp:117`); needs a `set_pan` arm

Regenerated, not edited: `res/Fireflow.svg`, `res/FireflowHW.svg`,
`src/generated_panel.hpp`, `src/generated_hw_panel.hpp`, `src/init_patch.hpp`.
Both generators run from `host/vcv/`. The task ends with
`host/vcv/build-local.sh install` and a restart of Rack (memory
`fireflow-vcv-finish-with-install`).

## 9. Out of scope

- **`shell/controls.cpp`** — no mux channel for PAN. There is no hardware and
  M6 bring-up has no spec; a channel nobody can measure is a claim, not a
  control.
- **A CV input for PAN.** The mod ring covers movement; the io budget is not
  spent on this.
- **Master PAN and stereo width.**
- **Rotation (mid/side).** Rejected in §3, with the measurement that decided it.

## 10. By-ear status

**Nothing here has had a listening pass.** Both first-try values go into
`docs/by-ear-decisions.md` as such, the way PULL's `kPullDead` is recorded:

- the balance law itself, against equal-power and against rotation
- the 30 ms smoothing time, copied from LVL rather than chosen for PAN
- `LANE_SIZE` as the ring's lane, chosen for its ×1/2 rate on paper

The measurement in §2 is real. Everything downstream of it that concerns taste
is not yet.

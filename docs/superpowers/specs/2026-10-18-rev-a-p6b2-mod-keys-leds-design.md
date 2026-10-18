# Rev A P6b-2 — knob ownership, the four keys, the LED law

**Date:** 2026-10-18
**Phase:** Rev A P6, second half (master plan
`2026-09-28-rev-a-master-plan-design.md`, row P6). P6b-1
(`2026-10-09-rev-a-p6b1-shared-control-law-design.md`) split P6b in two; this
is the second.

**Inputs:** the brainstorming of 2026-10-18 with Bastian (his decisions are
marked *Bastian*), the 9 mm panel spec `2026-10-07-panel-nine-mm-raster-design.md`
§5 (the lamp table — the meanings are decided there, not here), the P2 pin map
`2026-09-29-rev-a-p2-pin-map-design.md` §4 (keys, LEDs, the LED crosstalk rule),
the code at `fbdd7350`.

## 1. Intent

The Rev A boards are on their way. When one is on the table, Bastian wants to
switch it on, **hear the init patch, and start playing with the knobs**
(*Bastian*). P6b-1 made every pot reach its target through the shared control
law. Three things still stand between that and a playable instrument:

1. The firmware cannot hold a value a knob does not show. At boot the first
   valid sweep overwrites the whole init patch with wherever the 73 knobs
   happen to stand (read at `shell/scan_value.cpp:42`: a pot's first value is
   always emitted). There is no way to edit a mod depth either, because a pot
   cannot be two things.
2. The four keys are read and debounced but do nothing.
3. The 15 lamps are dark.

This round fixes all three, and it lays the ground for saving and loading
patches later: a patch is a set of stored values the knobs do not show, and
the rule in §2 is exactly what a patch load needs (*Bastian*).

## 2. Knob ownership — the rule

*Bastian, 2026-10-18.* Every parameter has a stored value, and the stored value
is what sounds. A pot does not own its parameter until it is **moved**; the
moment it is moved, the parameter **jumps** to the pot's position and follows
the pot from then on. A pot nobody touches leaves its parameter alone. No
pickup (catching the stored value by sweeping through it): you cannot see
where the pickup point is (*Bastian*).

The same rule applies everywhere a pot and its parameter come apart. A pot is
**released** (stops owning, its position becomes the new reference) when:

| Moment | Which pots are released |
|---|---|
| Boot | all — the init patch plays (`control_boot()` already loads it) |
| MOD latched or unlatched | every pot whose target changes with the latch, i.e. every pot whose sound id has a `kModLayer` row. A pot with no depth twin keeps owning its sound parameter through the latch |
| A pot's target changes for any other reason | that pot. Today the only case is ATTACK ↔ STAGES when the deck's ENGINE knob enters or leaves the BBD slot (`knob_target()`) |
| Someone other than the pot writes the stored value | that pot. Today: the law's `bbd_edge` event sets FLUX to 0 (§5). Later: a patch load releases all |

"Moved" means more than pot noise. The threshold is **measured, not chosen**:
the plan's first coupon task records the raw spread of an untouched pot over
a minute with the LEDs switching, and the threshold sits clearly above it and
above `kPotHysteresis` (16). A pot that crosses the threshold from its
reference becomes owned; the value it writes on that crossing is its current
position, not the reference.

This replaces Bastian's first answer of the session ("the knobs are the truth
from the first moment"); he revised it once MOD made the conflict visible.

**VCV does not change.** A Rack knob is drawn where its value is, so there is
never a gap to own across. The ownership layer is firmware-only and sits in
`shell/`, between `pot_filter()` and the knob vector. It is plain C++ with no
hardware type, so it is host-tested in ctest like `scan_value.cpp`.

## 3. The four keys

The 165 carries `REC_A`, `REC_B`, `MODBTN`, `SHIFTBTN` on D0–D3
(`kRevaKeys`, P2 §4). A key acts on its debounced **press** (released →
pressed), never on release or on hold.

- **MOD** toggles `g_knobs[MODBTN]` between 0 and 1, the same latch VCV has,
  and releases the pots of §2. While latched, a pot with a `kModLayer` row
  writes that row's `depthId`; every other pot keeps writing its sound id. A
  pot whose current target has no depth twin while latched — ATTACK on a BBD
  deck, whose target is then STAGES — writes nothing while latched; VCV hides
  both halves of that knob under the latch, which is the same thing.
- **REC A / REC B** toggle `g_knobs[REC_A]` / `g_knobs[REC_B]`, the latch the
  law already reads (`control_law.h:633`): it records only while the deck's
  engine is the Sampler, exactly as in VCV. The audio input already reaches
  `inst.process()`. The law's `rec_started` event has nothing to clear on the
  hardware (no sample path, no factory sample) and is ignored.
- **SHFT** stays reserved, no function (*Bastian*). It will get one with patch
  save/load. Its key is read and its press count reported like the others.

The init patch plays FEED on deck A and WAVE on deck B (`INIT_DEFAULTS`
`ENGINE_A` 5, `ENGINE_B` 2), so the first switch-on sounds without a sample.

## 4. The LED law

### 4.1 Where it lives

`host/vcv/src/led_law.hpp` moves to `control/led_law.hpp` — the step P6b-1
§3.3 deferred — and both hosts call it. It stays Rack-free and keeps its
tests. The firmware calls it once per audio block after `control_tick()`, and
writes the result to the 595 chain's LED bits through a generated
LightId → bit table (`shell/gen_panel_map.py`, which already knows the LED
nets at chain bits 13–27). `FLOW_A_L` / `FLOW_B_L` have no bit; the law keeps
writing them as 0.

The law's inputs grow by what VCV knows and the firmware does not: a CLOCK
pulse and a RESET pulse arrived this tick, and whether SHIFT is latched
(always false this round). The firmware passes false for the pulses.

### 4.2 What each lamp shows

The meanings are decided in spec 2026-10-07 §5 (*Bastian, 2026-10-07*); this
round builds all of them, in both hosts (*Bastian, 2026-10-18*: "großer Umfang
mit LEDs").

| Lamp | Shows | Today in VCV | This round |
|---|---|---|---|
| `GATE_A_L` / `_B_L` | a note sounds | done | unchanged |
| `SONG_A_L` / `_B_L` | flash on a phrase change | done | unchanged |
| `TEMPO_L` | the transport beat | done | unchanged |
| `REC_A_L` / `_B_L` | pulsing while recording, fill level when holding content | done | unchanged |
| `MODBTN_L` | MOD's double pulse while latched, otherwise the master limiter | done | unchanged |
| `LVL_A_L` / `_B_L` | **this deck delivers signal at all**: lit while the deck's own output into the mix is above a floor. It follows the level directly, with no hold: on a STEP deck with short notes and rests it blinks with the notes, as the level does (*Bastian, 2026-10-18*). Only enough smoothing that one audio cycle does not flicker it | still the `LANE_LEVEL` excursion | **new meaning** |
| `FTIME_A_L` / `_B_L` | one flash per FLUX time period, only while that deck's FLUX MIX > 0 | dark | **new** |
| `SHIFTBTN_L` | steady while SHIFT is latched (never, this round); otherwise the input level, the peak of IN L and IN R; on top, one pulse when a new engine (either deck) or a new scale has **taken effect** — at the switch, not while the knob turns | dark | **new** |
| `CLK_L` / `RST_L` | a flash per pulse at the CLOCK / RESET jack | dark | **live in VCV**, dark on the hardware until those jacks are wired (my call, §8) |

The new lamps need engine observers that do not exist yet: a per-deck output
level into the mix, an input peak, the FLUX period's phase (or a "period
elapsed" flag) and whether FLUX MIX is above zero, and an "engine or scale
took effect" signal. They are `const` reads on `spky::Instrument`; they must
not change what the engine computes. Two already have a natural source:
"FLUX MIX > 0" is the gate the law itself uses to switch the FLUX block on
(`pp(FLUX_A, p) > 1e-4f`, `control_law.h:533`), and "an engine took effect" is
the end of `Part`'s engine-switch fade (`_switching` / `_pending_engine`). The
LightId → chain-bit table already exists in `panel-map.json`'s `leds` (all 15
ids); the generator only has to emit it. Where the tap sits for "the deck's own
output" (spec 2026-10-07 §5 left "tap and floor" to this round) is a probe:
the plan finds the tap in `engine/center/`, prints its level across
LVL 0 / cutoff closed / empty sampler / a playing deck, and only then sets the
floor.

New constants — the LVL floor, the FTIME flash length, the SHIFT lamp's
switch pulse — are **by-eye candidates**, marked as such in the code and
listed in `docs/by-ear-decisions.md`'s unconfirmed section. The switch pulse
must read differently from the input-level display under it and from MOD's
double pulse on the other lamp; one single long flash is the starting
candidate (my call).

### 4.3 How many brightness steps the hardware can show

VCV quantises to 16 steps because "the mux scan gives the hardware 16
brightness steps for free" (`Fireflow.cpp:370-376`). That no longer holds.
The P2 crosstalk rule says LED bits change **only** in the latch that carries
the mux address, once per audio block — 96 samples, so 2 ms at 48 kHz
(`src/hw/board.h:69`; arithmetic, not measured). A lamp dimmed by switching
it over N blocks then repeats at 500/N Hz: 31 Hz at 16 steps, which will
flicker.

The rule stays (the P2 spec requires it "including for any LED dimming"). The
number of steps is decided **by eye on the coupon's LED** before the LED task:
the plan builds an image that holds LED_1 at each duty of 4, 6 and 8 steps
and Bastian picks the largest that does not flicker. VCV's `kLedSteps`
follows the result, for the reason that comment gives: Rack should not
breathe more finely than the panel ever can. The lamps that need levels are
`MODBTN_L` (limiter), `SHIFTBTN_L` (input) and `REC_*` (fill); every other
lamp is on/off. The duty pattern spreads the on-blocks rather than bunching
them (my call).

`duty()` calls `std::pow` per lamp per call. On the Daisy that is the libm
cost the P6b-1 round had to remove from the law; with a handful of steps it
becomes a table.

## 5. Events on the hardware

P6b-1 ignored the law's events because "a physical FLUX pot cannot be turned
back" (P6b-1 §4.2). Ownership changes that: on `bbd_edge[p]` the firmware sets
`g_knobs[FLUX_p]` to 0 and releases the FLUX pot (§2). FLUX then stays at 0
until the pot is moved, as VCV's knob drops to 0. `exciteOtherDeck` follows
VCV's handling: the shell keeps a persistent `control::Options` and sets
`deck[p].excite_other_deck` on the event. This closes P6b-1's known divergence "after a
BBD edge the hardware's FLUX stays at its pot value".

## 6. CPU

P6b-1 left the law at 11 800 cycles per tick (peak 13 900) out of a reserve of
about 2.9 points (~28 000 cycles) of the 960 000-cycle block, with Plan B (a
two-block split) and its trigger written down (P6b-1 §6). This round adds the
ownership layer (73 compares per block, negligible by construction) and the
LED law. The LED law's cost is **measured on the coupon** with the existing
cycle counter, alone and together with the law, and recorded in the roadmap.
If law plus LED law exceed the reserve, the LED law runs every second block
first (lamps at 250 Hz update are still far faster than any lamp meaning
needs); Plan B stays as P6b-1 wrote it.

## 7. Done

Bastian accepts done on three paths now and tests the full panel later on the
Rev A board (*Bastian*: "reicht erstmal so, ich teste das später am großen
Board nochmal ausführlich").

1. **Desktop.** `ctest` green in Release. New tests, each proven red once:
   the ownership layer (boot plays stored values; a jitter below the threshold
   does not take ownership; a move above it jumps and then follows; MOD
   releases exactly the `kModLayer` pots; an ATTACK↔STAGES switch releases
   ATTACK; a `bbd_edge` releases FLUX and holds 0); the key mapping (a press
   toggles, a release does nothing, SHFT does nothing); the LED law's new
   meanings against the observers; the observers do not move render output.
2. **Rack.** `FireflowHW` shows the new lamp meanings; MOD and REC behave as
   before on both widgets. The big `Fireflow` panel draws only GATE and REC
   lamps (`kLightCtls`, `generated_panel.hpp:380`) and gets no new ones —
   no layout change on that panel (my call). `host/vcv/build-local.sh install`, then Bastian's
   check in Rack.
3. **Coupon.** All three shell images build. One session: the image boots and
   plays the init patch wherever RV2/RV4/RV6 stand; moving a pot jumps its
   parameter; the key toggles MOD (the coupon has one key: it is MOD, my
   call), a pot then edits its depth audibly and leaves its sound value alone,
   and unlatching hands the pots back without a jump; LED_1 shows `MODBTN_L`
   at the chosen step count without visible flicker; the ownership threshold
   and the LED law's cycle cost are measured.
4. **Deferred to P7 (bring-up):** all 73 pots, all four keys and all 15 lamps
   on the Rev A board.

Docs: the roadmap's M6 entry (what changed, the measured threshold, step
count and cycle cost), `shell/README.md` (the divergences list loses its
FLUX line and gains whatever this round leaves open), `docs/engine-map.md` for
any measured fact about the new observers.

## 8. Calls I made (not decided by Bastian)

- MOD and REC act on press, as toggles — the latches VCV already has.
- The coupon's single key is MOD.
- The big `Fireflow` panel gets no new lamps; only `FireflowHW` shows them.
- `CLK_L` / `RST_L` go live in VCV now, where the jacks exist; on the
  hardware they stay dark until CLOCK and RESET are wired. In the
  conversation I said both stay dark until the jacks exist; VCV has them.
- A pot whose target changes for any reason is released, ATTACK↔STAGES
  included.
- The firmware applies `bbd_edge` (§5), closing a P6b-1 divergence.
- The SHIFT switch pulse starts as one long flash; the duty pattern spreads
  on-blocks.
- If the LED law does not fit the CPU reserve, it runs every second block
  before anything else is split.

## 9. Out of this round

SHFT functions and patch save/load (the ownership rule is ready for them);
the reserved pots `ROOT_A`, `ROOT_B`, `REV_MOD` (roadmap "Reserved knobs
become parameters", its own spec); CV, CLOCK and RESET on the hardware; a
factory sample on the hardware; retuning `kDepthDead` (0.04) for a real 9 mm
pot — a by-hand check at P7, it moves the init pre-images.

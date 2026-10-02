# Rev A P6a — panel scan over the real pin map

**Date:** 2026-10-02
**Phase:** Rev A P6 (master plan `2026-09-28-rev-a-master-plan-design.md`, row P6:
"Scan over the real pin map, every control on its parameter; prepared on the
coupon where possible"). P6 is split in two specs; this is the first.
**Inputs:** P2 pin map (`2026-09-29-rev-a-p2-pin-map-design.md` §3, §4, §7),
`hardware/reva/panel-map.json` (P3), `hardware/reva/blocks.py` (P3), panel
scan part 1 (`2026-09-28-coupon-panel-scan-design.md`, `shell/README.md`).

## 1. Goal and scope

The shell firmware scans the Rev A panel as P2 and P3 define it — ten 4051s on
four sense pins, a 40-bit 595 chain, the 165 with four keys, two calibration
channels — and every pot whose meaning is already unambiguous drives its
engine parameter. The table that does this is generated from the hardware
sources, so a panel change in P3 reaches the firmware or turns a guard red.

Done means: host tests prove the Rev A chain and table against the sources,
both play images (coupon and Rev A) link, and one board session on the coupon
shows the rebuilt driver scanning clean with the Rev A step pattern, keys read
through the 165, LEDs switching under the scan, and D8/D9 answering as ADC_12
and ADC_11.

**P6b (second spec, not this one):** a portable control layer extracted from
`Fireflow::pushParams()` that VCV and the firmware share, the MOD layer, the
35 pots this spec leaves without a target, key functions, the LED law. Jacks
(CV, gates, pitch out) and the SD card are later still.

## 2. Which pots get a target

### 2.1 The finding

`apply_param()` (`engine/param_table.h:151`) is not what a knob does in VCV.
VCV's control law lives in `Fireflow::pushParams()` (`host/vcv/src/Fireflow.cpp:765`)
and calls the instrument directly. Read from the two sources:

- DEPTH: `apply_param(P_DEPTH_x)` calls `set_depth`; in VCV the **MOD** knob
  calls `set_depth` (`Fireflow.cpp:790`) and DEPTH writes the LANE_MOTION base
  (`:1130`).
- COMP: VCV splits the travel into level and compressor with a curve
  (`:872-883`); `apply_param` sets the compressor raw.
- FLUX and GRIT: VCV switches the FX block on by knob position (`:840-846`);
  GRIT is bipolar with a dead zone in VCV (`:1143-1148`) and 0..1 in the table.
- MELODY, SUB, DENSITY, DETUNE change meaning per engine (`:993-1100`); DETUNE
  is squared; ENGINE is remapped (`:906-914`).
- RES: VCV's knob is 0..1 (`:479`), the table's range is 0..0.75.
- COUPLE, DRIFT, TEMPO carry zones or a different scale (`:1211-1229`, `:1256`).

### 2.2 The rule

A pot is **safe** when all three hold:

1. VCV calls exactly the setter `apply_param` calls for its `ParamId`;
2. VCV passes the knob value unchanged — no curve, zone, engine condition or
   additional call (`mv()`/`mvp()` count as unchanged: without a MOD layer
   they return the knob);
3. the VCV knob range equals the table range.

### 2.3 The result: 35 of 70

| Kind | Pots | Target |
|---|---|---|
| per deck (× 2) | RATE, SHAPE, SMOOTH, RANGE, TUNE, DECAY, FILT, COLOR, LINK, PAN | same-name `ParamId` |
| per deck (× 2) | REV_MIX | `P_REVMIX_x` (`Fireflow.cpp:1241-1242`) |
| per deck (× 2) | MOD | `P_DEPTH_x` (`:790` calls `set_depth`, as `apply_param(P_DEPTH_x)` does) |
| global | MORPH, TIDE, CHOKE, PULL, SHUFFLE, REV_SIZE, REV_DECAY, REV_TONE, REV_DIFF, PACE | same-name `ParamId` |
| global | SCALE | `P_SCALE` (13 steps both sides: `SCALE_LIST_COUNT` = 13) |

**Without a target in P6a (35):** DENSITY (also calls `sampler_overlap`),
ATTACK (shares its pot with STAGES), SUB (re-pointed on the sampler), RES
(range), DEPTH, COMP, FLUX, GRIT, MELODY, DETUNE, ENGINE, STEPS, SONG, SOURCE,
FLUXRATE, FLUXFB (each × 2), and COUPLE, DRIFT, TEMPO. They are scanned and
reported, never sent.

**Accepted divergence until P6b:** without a MOD layer every mod depth on the
hardware is zero. Where VCV's init patch carries non-zero depths, the
hardware sounds different. That is P6b's work, not a P6a defect.

## 3. The scan driver

### 3.1 Step model

Part 1's model enables one group per step. Rev A enables **one mux per sense
pin per step** (P2 §3). `ChainProfile` gains a flag `parallel_sense`:

- **sequential** (today): step = one group's channel; unchanged.
- **parallel**: each sense pin owns an ordered list of muxes; at step *k* every
  sense pin enables the mux holding its *k*-th channel, and a sense pin whose
  channels are exhausted has every mux disabled. Steps = the largest channel
  count of any sense pin.

Rev A: muxes 3/3/2/2 on SENSE_0..3 → 24 steps, 48 ms per sweep. SENSE_2 and
SENSE_3 are fully disabled in steps 16–23; their node floats, `sense_live()`
says false and nothing is stored.

Types widen: `kMaxGroups` 10, enable mask `uint16_t`, chain word `uint64_t`
(40 bits). `read_chain()` keeps only the first 8 bits of the return stream
(the 165; with `DS` on GND everything after is zero), so its return stays
narrow and no shift exceeds its type.

### 3.2 Profiles

- `kCouponChain` — unchanged, sequential. Every coupon probe (settle, xtalk,
  tone, wait, scan check, bring-up) keeps exactly its measured pattern; a host
  test pins that.
- `kCouponPlayChain` — same wiring as `kCouponChain`, parallel: the 4067 on
  ADC_9 and the 4051 on ADC_10 enabled together, 16 steps. The coupon's play
  image (`SHELL_PANEL_SCAN=1 SHELL_COUPON_PROBE=1`) uses it, so the coupon
  rehearses Rev A's pattern: two muxes live at once on separate sense pins.
  `coupon_span()` reads its input in `kCouponChain` step order, so the play
  image stores each raw word at `step_of(kCouponChain, group, ch)`.
- `kRevaChain` — generated (§4). Replaces the stale pre-P2 `kPanelChain`.

### 3.3 Chain, latch and LEDs

Bit layout from `blocks.py` `SR_OUTPUTS`, with bit *k* of the word landing on
output *k* of the flattened table (MSB first through U_SR1 → U_SR5, as
`write_chain()` clocks it): address 0–2, enables 3–12, LEDs 13–31, spare 32–39.

**The LED rule (P2 §4):** LED bits change only in the latch that carries the
mux address. One block = one `select()` = one word carrying address, enables
and LEDs together. `set_leds()` only stores the next LED field; it reaches the
595s with the next step's latch, never on its own. (`read_chain()` raises the
latch once before shifting, for the 165's parallel load; that edge re-latches
the word already latched, so no output moves.)

**Power-up (P2 §3):** `init()` latches all enables off before the first step.

### 3.4 Keys

Every step reads the 165 in the same pass as the write (`read_chain()`, no
second latch). Keys REC_A, REC_B, MODBTN, SHIFTBTN sit on D0..D3; the return
stream's first bit is D7, so key *i* is bit 7 − *i* (the coupon's single key on
D0 is bit 7, as `kCouponChain.button_bit` already says). Keys are active low.
Debounce: a key changes state after three equal reads in a row (6 ms). P6a
exposes a pressed-mask and one press counter per key; keys have no function.

### 3.5 Span

Rev A reads its own span from CAL_GND and CAL_3V3 (`panel-map.json`
`calibration`) once per sweep: valid when the rail reads at least `kRailFloor`
and the zero at most `kRailMargin` (`coupon_expect.h`). An invalid sweep keeps
the previous span; before the first valid span nothing reaches the engine. The
placeholder `kPanelSpan` goes.

### 3.6 Table lookup

Key = (global mux 0–9, channel); the sense pin rides along as a check field.
A row without a target updates only its reported value.

### 3.7 Coupon-only extras in the play image

- While the key is held, LED0 is lit — key path, LED field and the latch rule
  in one gesture, and LEDs switch while the scan runs.
- `SHELL_PLAY` also prints the raw words of ADC_11 and ADC_12 for the D8/D9
  check (P2 §7).

### 3.8 Report

`SHELL_PLAY` at 2 Hz as today. Coupon: the three pots, span, sweeps, key mask,
press count, ADC_11/ADC_12 raw. Rev A: the 70 values by table row, span,
sweeps, key mask and press counts. No names in the firmware; the row → name
list is in the generated header's comment.

## 4. Generator and guard

**`shell/gen_panel_map.py`** — plain Python, no KiCad. Reads
`hardware/reva/panel-map.json`, `hardware/reva/blocks.py` (`SR_OUTPUTS`,
`KEYS`, `MODULE_PINS`; sense pin → ADC channel: A2 = ADC_9, A3 = ADC_10,
D9 = ADC_11, D8 = ADC_12, P2 §2) and `engine/param_table.h` (`ParamId` names).
It owns two lists: `SAFE` (pot → `ParamId`, with its `Fireflow.cpp` line as
evidence) and `UNMAPPED` (pot → reason). Every pot must be in exactly one, or
the generator fails.

**Output `shell/generated_panel_map.h`** (committed): `kRevaChain`;
`kRevaControls[70]` = `{mux, ch, sense, param or -1}`; the two calibration
channels; the key bits; the row → name list as a comment.

**Guard `shell/test_gen_panel_map.py`**, ctest `shell_panel_map_guard`:
regenerated output equals the committed header; the SAFE/UNMAPPED partition is
complete and disjoint; every SAFE `ParamId` exists; every pot id exists in
`host/vcv/src/generated_hw_panel.hpp`. Each check has a sabotage case that
proves it goes red (pot missing, wrong `ParamId`, stale header, pot in both
lists).

**Host doctest** (`tests/test_mux_plan.cpp`, `tests/test_controls_map.cpp`),
against `kRevaChain` and `kCouponPlayChain`: every step enables exactly one mux
per sense pin that still has channels; every (mux, channel) is visited exactly
once per sweep; the chain word carries each field at its `blocks.py` bit; the
key bits; LED bits change only between steps' words; (mux, channel) is unique
in the table and no calibration channel is a pot row; `kCouponChain`'s
patterns are unchanged.

## 5. Failure handling

Out-of-range step, group or channel answers −1 / all-off, as today. No value
reaches the engine before the first valid span; an invalid sweep keeps the old
span. An unknown or unclassified pot stops the generator.

## 6. Code space

The coupon play image used 98.8 % of `SRAM_EXEC` (about 3 KB left,
`docs/roadmap.md` "Carried into part 2"). `apply_param` already links every
setter, so the table should cost mainly its own rows — reasoned, not measured.
Gate: the coupon play image and the Rev A play image both build and link; the
`SRAM_EXEC` use of each is read from its `.map` before and after and recorded.
If it does not fit: first drop the parameter-name strings from `kParams` in
the firmware build (748 B, never read there); if that is not enough, STOP and
report the numbers.

## 7. Coupon session (Bastian, about 15 minutes)

Flash the coupon play image; read `SHELL_PLAY` by hand.

1. All three pots reach both stops; at rest each value stays within H = 16.
2. Press the key five times: the press count reads 5; LED0 is lit while held.
3. Hold the key, pots untouched: no value moves by more than H (LEDs switching
   under the scan).
4. Feed a known voltage into the D8/D9 test points (P2 §7): D9 to GND and D8
   to the module's 3V3 → ADC_11 ≈ 0, ADC_12 ≈ rail. Swap: the readings swap.
   (Where the coupon offers a 3V3 point to jumper from is the plan's to look
   up in `hardware/coupon/`, not assumed here.)

## 8. Documentation

`shell/README.md`: a part 2 section. `docs/roadmap.md`: a P6a entry with the
measured code-space numbers and the session result. Correct "three keycaps on
the 165" to four (`shell/README.md:285`, `docs/roadmap.md:4100`).

## 9. Out of scope

The shared control layer from `pushParams`, the MOD layer and the 35 pots
without a target (P6b); key functions and the LED law (P6b); jacks — CV,
gates, pitch out — and the SD card; spare-channel noise and SD-on-A10 noise
(P7); a `SHELL_PLAY` reader.

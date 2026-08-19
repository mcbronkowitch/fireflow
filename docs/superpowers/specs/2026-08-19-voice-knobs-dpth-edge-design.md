# DPTH and EDGE across all six engines

**Date:** 2026-08-19
**Status:** design, approved in conversation, not yet planned
**Branch:** to be cut from `main` at `eacc7e4`

## 1. What this is

The VOICE row grew from three knobs to four on 2026-08-19 (commit `d18e50f`).
The two new controls, `DPTH` and `EDGE`, ship pointed at FEED only: on the
other five engines they are the first controls on this panel that do nothing.
That was deliberate and temporary — Bastian wanted to feel whether two more
VOICE knobs are worth having before deciding what they should do. They are.

This design says what they do on the other five.

It is not a sound-design document. Every rail, curve and shipped value it
names is a *first* value; which of them survive is a listening question and is
listed in §9, not decided here.

## 2. Decisions this design rests on

Four questions were asked and answered before anything below was written.

1. **One knob carries a family axis, one is free.** The house has both kinds
   already: `ATTACK`/`DECAY`/`FILT` mean one thing on every engine and print
   six different words for it, while `SOURCE` means something different on
   each. `EDGE` becomes the first kind, `DPTH` the second.
2. **EDGE's axis is the engine's second filter** (§4).
3. **DPTH is the base of the MOTION lane** on all six engines (§3).
4. **The sampler scales that base** rather than dropping it (§3.3).

A review pass over this document (2026-08-19, after §1–§9 were first written)
found two defects and added two more decisions:

5. **EDGE is a trim, not an absolute corner** (§4.2). One knob has one boot
   value; six engines have six neutral points. FILT already solves exactly
   this, and EDGE copies it.
6. **BODY's zone 2 stays unfiltered** and EDGE is inert there, written down
   rather than repaired (§4.6).

## 3. DPTH — the MOTION lane's base

### 3.1 The mechanism already exists

`LANE_MOTION` is read by every engine, and each reads it as something else:

| Engine | reads `_targets[LANE_MOTION]` as | where |
|---|---|---|
| SYNTH | `width` → the four-voice pan fan **and** the per-voice drift | `synth_engine.cpp`, `update_control` |
| WAVE | identical; `kVoices == 4` keeps `kPanFan[v]` exact, so SYNTH and WAVE stay bit-identical | same file |
| BODY | `width` → drift only; the fan is pinned to centre because `BodyVoice::kEngineVoices == 1` | same file, the `kVoices > 1` guard |
| SAMPLER | scatter: grain position jitter and spawn-interval jitter | `sampler_engine.cpp`, `_spawn_one` |
| BBD | `_fb_lane` — the feedback amount | `bbd_engine.cpp:213` |
| FEED | `_depth_n` — the FM index | `feed_engine.cpp:61` |

The host has never written that lane's base. `Fireflow.cpp` states the
consequence itself: *"An engine that reads LANE_MOTION therefore had a control
whose ends the player could not reach."* Until 2026-08-18 the only thing that
moved it in Rack was `MOD`. FEED's `DPTH` is the repair, applied to one engine
through a ternary:

```cpp
inst.set_target_base(p, spky::LANE_MOTION,
                     feedPart ? pp(DEPTH_A, p) : 0.5f);
```

**The change is that the ternary goes away.** Five of the six cells cost no
engine code at all.

### 3.2 The shipped default does not move the sound

`DPTH`'s init default stays `0.5`. That value is simultaneously `Part`'s
compiled-in base and `feed_cfg::kDepthBase`, so on SYNTH, WAVE, BODY, BBD and
FEED an untouched patch writes exactly what the line above already wrote. The
sampler is the one exception and gets its own section.

### 3.3 The sampler scales the base

On a sampler deck `Part::_control_tick` currently **discards** the base:

```cpp
if (_engine_id == ENGINE_SAMPLER) {
    const float mmod = _active[LANE_MOTION] ? ... : 0.f;
    _tg[LANE_MOTION] = clampf(mmod, 0.f, 1.f);
}
```

The reason is measured, not aesthetic, and the measurement is recorded in that
comment: at base `0.5` the position jitter is uniform over a window of exactly
one content length, so `(ORGANIZE*span + SCAN + jitter) mod content` is uniform
**independently of both summands**. ORGANIZE and SCAN had provably zero effect
on spawn position — means of 12036 / 11896 / 11951 at SOURCE 0 / 0.25 / 0.9
over a content of 24000.

Note where the pathology sits: at base `≥ 0.5`, which is exactly where DPTH's
shipped default is.

**The flattening is replaced by a scale.** The sampler reads the base through
`kMotionBaseScale = 0.5f`, so:

- knob `0.5` → base `0.25` → jitter window half a content length; ORGANIZE and
  SCAN stay audible.
- knob `1.0` → base `0.5` → the degenerate state, now reachable deliberately
  ("full fog") instead of by accident.
- knob `0.0` → base `0` → **exactly today's behaviour**. This is the return
  ticket and it belongs in the by-ear note.

The sampler is therefore the only engine whose factory sound moves. Whether
`0.25` is the right shipped point is a listening question (§9).

### 3.4 One thing to watch on BBD

`_fb_lane = clampf(t[LANE_MOTION], 0.f, 1.f) * 1.2f / bbd_drive_gain(_drive)`.
At `DPTH = 1.0` that is above unity before the loss pole eats into it. This is
not new territory — `MOD` can already drive the lane to 1.0 today — but it
becomes reachable without modulation, which is a different thing to have under
a finger. A listening item, not an assumption.

## 4. EDGE — the second filter

### 4.1 The axis, stated so it does not lie

> `FILT` takes the top end of what leaves the engine. `EDGE` takes what `FILT`
> does not: on engines with a loop or a resonator it works **inside** them; on
> the straight engines it takes the **bottom** end.

The second half of that sentence is not a compromise, it is the honest reading
of the code. SYNTH's and WAVE's voice is a straight chain — oscillator → sub →
`SvfLp` → out — with no loop and no nonlinearity; `voice.cpp:23` says so
outright: *"no SetDrive: SvfLp has no drive term"*. A second low-pass in front
of the first would be musically indistinguishable from turning `FILT` down,
and a control that imitates another control is worse than a dead one, because
it promises something. A high-pass is not an imitation: thin/full and
bright/dark are different axes.

**Build the high-pass on the engine's summed output, not per voice** — two
one-poles per deck instead of eight. Note what this is and is not: filtering
the sum is not identical to filtering each voice, because each voice carries
its own envelope and pan drift before the sum, and a filter only commutes with
a *constant* gain. It is the cheaper of two legitimate designs, not a
rearrangement of one. `SynthEngineT`'s sum point is linear
(`synth_engine.cpp`), so nothing else stands in the way.

**Direction, identical on all six: up = more edge, less weight.** A low-pass
corner rising means brighter; a high-pass corner rising means thinner. Both
lose at the bottom and gain at the top.

### 4.2 The contract: a trim, not an absolute corner

**One knob has one boot value. Six engines have six neutral points.** The
first draft of this design had EDGE be an absolute cutoff with per-engine
rails and a Hz readout, inherited from FEED, where the knob boots at
`INIT_DEFAULTS["DAMP_A"] = 0.632718364` because that position *means* 3200 Hz
on FEED's 200–16000 Hz rails. The same position on a new high-pass lands
mid-travel: five engines would boot with a filter switched on, moving their
factory sound and taking `ctrl_identity` and the render hashes with them.
There is no set of rails that fixes this honestly — for a high-pass, placing
neutral at 0.63 means the lower two thirds of the knob all sit below hearing.

**FILT already solves this exact problem and EDGE copies it.** `FILT` is not
an absolute cutoff either; it is a **bipolar trim around a neutral each engine
defines for itself** — `SamplerEngine` has `kFiltNeutral`, `SynthEngineT`
trims `_targets[LANE_SIZE]`, `BbdEngine::set_filt` trims `kLossCoef` by an
asymmetric octave span, and all of them agree that `t == 0` is the untouched
value.

EDGE takes the same shape: **centre = the engine's own neutral, travel = a
span in octaves either side.**

| Engine | EDGE's neutral | So centre means |
|---|---|---|
| SYNTH, WAVE, SAMPLER | the high-pass corner at its bottom rail | the filter is off |
| BODY | the exciter corner `_recompute_filter` derives from RESO | today's strike |
| BBD | pre-emphasis flat | today's input |
| FEED | `feed_cfg::kDampFixedHz` (3200 Hz) | today's loop damping |

Three things follow, all of them improvements:

- **The factory sound is preserved by construction, on all six engines** — not
  by six rail sets coincidentally agreeing on one knob position.
- **The gate gets stronger, not weaker.** Today's gate recomputes
  `INIT_DEFAULTS` from `kDampFixedHz` and the host's rails. Its successor
  asserts something better: *the neutral IS the engine constant* — one
  assertion per engine, each read from that engine's own header.
- **FEED's knob changes shape**, from an absolute Hz readout to a trim around
  3200 Hz. It is one day old and has never been through a listening pass, so
  there is nothing to preserve. It can still print Hz: neutral × 2^(t·span).

### 4.3 The six cells

| Engine | Where EDGE acts | What it does | Work |
|---|---|---|---|
| SYNTH | one-pole high-pass on the engine's stereo sum | takes the weight out | 2 one-poles/deck, new |
| SAMPLER | same, on the summed grain bus | same | 2 one-poles/deck, new |
| WAVE | same | same | 2 one-poles/deck, new |
| BODY | the exciter's low-pass corner, ahead of the resonator | how bright the strike that hits the string is | a trim inside `Exciter::_recompute_filter`, filter exists |
| BBD | **pre-emphasis** one-pole on the input, ahead of the line | how bright what enters the line is | 1 one-pole/channel, new |
| FEED | the one-pole **inside** the feedback path | unchanged in what it does | the filter stays; the knob becomes a trim (§4.2) and moves onto the broadcast (§4.4) |

Each cell gets its own octave span either side of neutral, exactly as `ATTACK`
and `DECAY` already carry per-engine curves and `BbdEngine::set_filt` already
carries an asymmetric one.

### 4.4 How EDGE reaches six engines

FEED's EDGE is delivered by `Part::set_feed_damp_hz`, which is deliberately
**not** in the broadcast line — a one-engine setter, in Hz, with the knob's
curve living host-side in `Fireflow.cpp`. That shape does not survive contact
with five more cells, and the plan must change it rather than add five more
one-engine setters:

- `Part` gains `set_voice_edge(float t)` next to `set_voice_filt(float t)`,
  broadcasting to all six engines. `set_feed_damp_hz` and its host-side curve
  go away; FEED's neutral moves into `feed_config.h` where the other engines'
  neutrals also live.
- `Instrument` gains the matching per-part forward.
- Each engine gains `set_edge(float t)`, bipolar, `0 == neutral` — the same
  signature `set_filt` already has.
- **All three hosts have to deliver it, and only one of them will complain if
  it does not.** `host/vcv/` pushes it in `pushParams`; `host/render/` needs a
  scenario action in `scenario.cpp` or EDGE is unreachable from every render
  test; `shell/` maps one control today and needs nothing, which is a fact
  worth stating so nobody goes looking.

The same review that found this also found the reason it matters: an engine
missing from a broadcast line is the silent-dead-knob failure this project has
already had three times, and `part.h`'s own comment on those lines says so.

### 4.5 Why BBD is pre-emphasis and not `kFilterHz`

An earlier draft of this design named `bbd_tuning::kFilterHz` (3600 Hz, three
poles inside the delay loop) as BBD's existing cell. That is wrong twice over,
and `bbd_engine.cpp:363` documents both:

- **It is not reachable.** The constant is baked into `butterworth_poles()`,
  its coefficients live in two file-scope singletons that every `BbdLine` holds
  raw pointers into, one deck's knob would retune the whole instrument, a
  rebuild is 396 transcendentals, and in-place rebuild is a shared-mutable
  hazard safe only with the audio callback stopped.
- **The axis is already occupied twice.** `FILT` moves the loss pole, which is
  inside the loop and is the pole that actually carries the darkness; `RES` is
  the feedback-path tilt. BBD is the engine with the *most* inner tone, not the
  freest.

What BBD does not have is a control over what **arrives**. All three existing
tone controls shape how the signal decays. In a companded bucket brigade the
compander responds to what comes in, so pre-emphasis is an audibly different
grip and is not redundant with either existing control.

### 4.6 BODY's dead zone, stated rather than hidden

`Exciter::_recompute_filter` derives the click/noise cutoff from `_char`
(RESO) in three zones: click 2–8 kHz, noise 1–10 kHz, and — in zone 2, the
sputter/ping character at RESO ≥ 0.67 — a flat 10 kHz the comment marks
`unused`.

It says `unused` because **zone 2 does not run through the filter at all.**
`Exciter::process` computes `sputter * (1 - t) + ping * t` and never calls
`_lp.process()`; the one-pole is in the zone 0 and zone 1 branches only. An
earlier draft of this document had this wrong and called the repair "give the
corner back, a small change" — it is not: it is inserting a filter into a
signal path that has none.

**Decision: zone 2 stays as it is, and EDGE is inert there.** Not because the
repair is hard, but because it would change a character somebody chose, and it
would couple EDGE to level rather than tone — the ping is a `fast_sin` at the
fundamental, so a low-pass sweeping down attenuates it outright instead of
colouring it.

So: **EDGE does nothing in the top third of RESO on a BODY deck.** That is a
documented blind spot, and the plan writes it into the source comment, the
engine map and the by-ear ledger so the next reader does not file it as a bug.
A knob that changes meaning inside another knob's travel — the alternative —
is the failure mode `DYNAMIC_CAPTIONS` exists to prevent.

## 5. The words

Order is the engine enum order, which is what `DYNAMIC_CAPTIONS` uses:
SYNTH, SAMPLER, WAVE, BODY, BBD, FEED.

| | SYNTH | SAMPLER | WAVE | BODY | BBD | FEED |
|---|---|---|---|---|---|---|
| **DPTH** | DPTH | SCAT | DPTH | SWAY | RPTS | DPTH |
| **EDGE** | EDGE | EDGE | EDGE | SNAP | PRE | EDGE |

This is `FILT`'s shape, deliberately: `("FILT", "FILT", "FILT", "BRITE",
"LOSS", "FILT")` prints the axis name on the engines whose reading is the
plain one and a specific word only where the meaning genuinely departs. §6
says why that shape, and not a specific word per cell, is the one to copy.

`SCAT`, `SWAY`, `RPTS`, `SNAP` and `PRE` were checked against the generator's
printed vocabulary; none collides. `RPTS` rather than `FDBK` because `FB` is
already printed for FLUX's tape-echo feedback (`FLUXFB_A/B`, tooltip `FFB`),
and two feedbacks would be one word too many on one instrument. Repeats
*within* one knob's tuple are house practice — `ATTACK` prints `ATK` four
times, `FILT` prints `FILT` four times.

## 6. Why the SYNTH cell is the axis name

An aluminium panel cannot repaint, so it prints one word per knob, and
`test_hw_panel.py::test_static_captions_only` enforces which one: for any
control that has a dynamic-caption tuple, the plate word must equal
`words[0]`, the SYNTH cell. That is why the plate says `TIMB` for SOURCE and
`FILT` for FILT.

The guard bites harder than it looks. `gen_hw_panel.place()` applies
`HW_CAPTION` *before* the plate list is built (`n.label = _caption_for(...)`),
so the override and the asserted label are the same string: an `HW_CAPTION`
entry that departs from `words[0]` does not sneak past the guard, it fails it.
Keeping `DPTH`/`EDGE` on metal while printing engine-specific words in Rack
would require relaxing a guard that exists for a good reason.

**So the SYNTH cell carries the axis name instead**, and no override or
exception is needed. The cost is one line of information: Rack no longer has
room to print `WIDE` on a SYNTH deck, where DPTH is stereo width and drift.
That is the same cost `FILT` already pays on four of its six cells, and it
buys a plate that does not advertise the weakest of an axis's six meanings —
stereo width, on a knob that elsewhere runs a delay's repeats and an FM index.

`WIDE` and `WGHT` stay on the shelf. If the plate practice is ever revisited,
they are the words this design would have used.

## 7. What has to be proven

**One gate per cell, measured at the effect.** The trap is known and is worth
naming: a test that calls `set_target_base` and then reads `target_base()`
proves nothing — it is shape 1 from the vacuous-gate list. Each cell is
measured where it lands:

| Cell | Measured at |
|---|---|
| DPTH / SYNTH, WAVE | L against R, and the drift excursion |
| DPTH / BODY | pitch wander over time |
| DPTH / BBD | decay length of the repeats |
| DPTH / SAMPLER | the spawn-position histogram — the same measurement that produced the 2026-07-22 finding, so the rig exists |
| DPTH / FEED | unchanged; the existing FEED gates cover it |
| EDGE / SYNTH, WAVE, SAMPLER | spectrum below the corner |
| EDGE / BODY | the exciter's own output, before the resonator |
| EDGE / BBD | the first pass against the repeats — they must move differently |
| EDGE / FEED | unchanged |

Four gates above the cell level:

- **Factory sound.** At `DPTH = 0.5` nothing moves on five engines. RED-provable:
  keep the ternary out, move the default, the render breaks.
- **Sampler return ticket.** `DPTH = 0` reproduces today's flattened behaviour
  exactly.
- **Host wiring.** `feed_host_wiring_issues` currently asserts that DPTH writes
  the base **only** on FEED. That inverts: keeping the ternary becomes the
  regression.
- **Shipped defaults.** The gate that recomputes `INIT_DEFAULTS` from
  `feed_config.h` changes its subject: instead of deriving one knob position
  from one engine constant, it asserts **the neutral is the engine constant**,
  once per engine, each read from that engine's own header. EDGE at centre
  must leave all six engines bit-unchanged; that is one render per engine and
  it is RED-provable by nudging any single neutral.
- **Delivery.** A gate that fails if any engine is missing from
  `set_voice_edge`'s broadcast line, and a render-host scenario that actually
  moves EDGE — without the `scenario.cpp` action the whole control is
  unreachable from every render test, which is the one way this feature could
  ship with green tests and no coverage at all.

**No CPU figure before the bench.** Four new one-poles per deck is a statement
about the design; what they cost is the board's to say. A bench row runs before
any number enters a document.

## 8. Not in this design

- The octave spans and curves themselves — listening work, §9. The *neutrals*
  are not optional and are named in §4.2; only how far the trim reaches is
  open.
- BODY's level parity, which has its own `⬜ Planned` roadmap entry.
- **"EDGE = how sharp are the edges"** — the rejected alternative axis: polyblep
  amount on SYNTH, mip bias on WAVE, i.e. a deliberate lo-fi/aliasing control.
  It is a good knob and a different promise from this one. It goes into the
  roadmap as its own idea so it is not lost.

## 9. By-ear items this creates

1. Every EDGE rail pair, six engines.
2. The sampler's `kMotionBaseScale` and whether base `0.25` is the right
   factory point.
3. BBD at `DPTH = 1.0` — above unity before the loss pole.
4. BODY zone 2: whether restoring the exciter's corner there is an improvement
   or a change to a character that was chosen.
5. Whether the high-pass on SYNTH, WAVE and SAMPLER wants three different
   octave spans or is honestly one control with one span.
6. **DPTH on BODY may be a whisper.** `body_voice.cpp` caps the drift at
   `kDriftDetuneCt = 3.f` (±3 cents) and `kDriftPanAmt = 0.25f`, and BODY's
   pan fan is pinned to centre at one voice — so a full knob sweep buys ±3
   cents of wander. If the listening pass finds `SWAY` too small to be worth a
   quarter of the VOICE row, the ceiling is the number to move, not the knob.

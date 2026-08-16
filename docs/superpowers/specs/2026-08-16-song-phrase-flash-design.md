# SONG phrase flash — blink on A/B switch, drop the STEPS lamp

**Status:** design, not built. Brainstormed 2026-08-16 with the owner, after
the LED-feedback round shipped a SONG law that could not be read.
**Parent:** [`2026-08-16-led-feedback-design.md`](2026-08-16-led-feedback-design.md)
§3.2 / §3.5 / §4.4. This document replaces those three. Placement of the
remaining knob lamps is unchanged (§5 of the parent).
**Scope:** `FireflowHW` draw list and `host/vcv/src/led_law.hpp`. No engine
change. The shared `LightId` inventory stays at 21.

## 1. What was wrong

The STEPS lamp (`FLOW_A_L` / `FLOW_B_L`) is written to duty 0 every block. It
cannot light, whatever the knob does.

The SONG lamp answers "which snapshot is sounding" with a wall-clock
double-pulse on snapshot B. FLOW melody advances SONG, so the lamp flips A/B
*and* strobes at 1 Hz while B plays. That is noise, not a readout. The
interesting event is the switch itself.

## 2. SONG — flash on the edge

One lamp per deck, still at `SONG`, still reading `Instrument::active_pattern(p)`.

**Dark, except for a short flash when the snapshot changes.** A→B and B→A are
the same flash. OFF never switches (`song_symbol_at` returns 0), so the lamp
stays dark. First `fill()` arms the previous-pattern latch from the current
value and does not flash.

- Duration: `kSongFlash = 0.15 s` wall-clock, the middle of the parent's
  100–200 ms event-flash window. Hard on/off, no envelope, not a fraction of
  the beat (this is an event, not a metronome).
- If a second switch arrives while the flash is still on, the timer restarts.
  At phrase lengths shorter than 150 ms the lamp stays on — that is merged
  switches, not a new state display.
- `Panel` holds the previous pattern (and remaining flash time) per deck.
  `phrase_on(snapshot_b, blink)` goes away.

## 3. STEPS — off the plate

`FLOW_A_L` / `FLOW_B_L` leave `KNOB_LAMPS`. The STPS caption recentres on the
knob. `FireflowHW` does not draw the two lamps (`HW_LIGHTS` skips them).

The `LightId`s stay in the shared enum so `NUM_LIGHTS` and the large module
are untouched. `fill()` still writes them, always duty 0.

## 4. Not in this round

TEMPO, GATE, the eight excursion lamps, CEIL, REC, SYNC, the two pad lamps.

## 5. Gates

Each must go red once.

- **S1 — a snapshot edge produces a flash, then dark.** Drive a STEP deck
  through an AAAB cycle; `SONG_*_L` is off on A, goes to full duty on the
  A→B edge, and is off again after 150 ms. The B→A edge flashes the same way.
- **S2 — no flash on first fill.** Construct a panel, `fill()` once with
  snapshot B already active: duty 0.
- **S3 — OFF stays dark.** `SongMode::Off`, many wraps: duty 0 throughout.
- **S4 — STEPS is not on the HW plate.** `FLOW_A_L` / `FLOW_B_L` are absent
  from `HW_LIGHTS`; STPS captions sit on the knob x.
- **S5 — `phrase_on` is gone.** A grep of `led_law.hpp` finds no
  double-pulse helper.

S1–S3 replace the parent's G11 (steady vs double-pulse). S4 is a panel
guard. S5 is a grep, not a behaviour test.

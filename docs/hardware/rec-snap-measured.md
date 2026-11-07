# REC on the bar: board measurements

Rev A #1, branch `feat/rec-snap`, build `68510c2`. Spec:
[`2026-11-07-sampler-records-other-deck-on-the-bar-design.md`](../superpowers/specs/2026-11-07-sampler-records-other-deck-on-the-bar-design.md),
plan [`2026-11-07-sampler-records-other-deck-on-the-bar.md`](../superpowers/plans/2026-11-07-sampler-records-other-deck-on-the-bar.md)
Task 10. The board printed every number below; the captures are not in the
repo. Session of 2026-11-07.

## 1. Setup

- **Images.** Clean builds of the committed tree, flashed QSPI first, then
  SRAM, through `remote_run.py boot` / `flash --expect-build`. Both are play
  images (`SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1`, persistence on): Bastian's
  persisted patch loaded in both, and neither wrote QSPI without a panel
  change.
  - `reva_play_no_snap`: `SHELL_REC_SNAP=0`, no rings, `.sdram_bss` 0x22E3000
    (36.6 MB).
  - `reva_play`: `SHELL_REC_SNAP=1` (the default), `.sdram_bss` 0x23FCC00
    (37.7 MB) of 64 MB.
- **The patch.** `SHELL_PERSIST boot eng=1,6`: deck A SYNTH, deck B FEED. No
  deck was a SAMPLER in the hands-off runs, so they price the instrument-level
  part of the snap (the bar clock, the snap checks, the LED law), not the
  ring. The ring was priced in the hands-on run (§3).
- **Capture.** `shell/read_play.py COM4 <file> <seconds>`; `cb_max` is the
  worst audio callback since the first second, in DWT cycles of a 960 000-cycle
  block.

## 2. The callback, hands off (plan Task 10 step 1)

| image | capture | frames | `cb_max` | median `cb_last` | p99 `cb_last` | `led_max` |
|---|---|---|---|---|---|---|
| `reva_play_no_snap` | 10 min | 880 | 763 987 | 709 244 | 745 684 | 1 789 |
| `reva_play` | 10 min | 879 | 770 062 | 709 616 | 750 200 | 2 283 |

The snap costs **6 075 cycles** of `cb_max` (0.63 % of a block) against the
gate of 19 200 (2 %), and 372 cycles of the median. One capture per image; the
spread between runs of one image is not measured. `led_max` (the LED law's
worst tick, outside the callback) rises by 494 cycles: the TEMPO lamp's beat-1
branch.

## 3. The callback, hands on

Bastian played the snap image through the by-hand list (§4), deck A on
SAMPLER. The capture ran beside him (`presses=5,0,4,1`: REC A five times,
MOD four, SHIFT once).

| image | capture | frames | `cb_max` | `cyc_max` | `led_max` |
|---|---|---|---|---|---|
| `reva_play`, hands on | first 10 min | 1 009 | 797 576 | 18 292 | 2 408 |

797 576 is 83 % of a block, under the read_play criterion of 900 000. It
includes engine swaps and every pot move of the session, so it is not the
snap's price alone; it is the worst the play image did while the snap worked.

## 4. By hand (plan Task 10 step 2), Bastian's verdicts

| # | check | verdict |
|---|---|---|
| 1 | Deck A SAMPLER, deck B a bass: REC A twice, the take holds the bass | ok |
| 2 | Start late (just after beat 1), stop early (just before beat 1): the take starts and ends on the bar and sits in time with B | ok |
| 3 | A held start blinks REC fast; TEMPO shows beat 1 brighter | ok |
| 4 | An overdub snaps the same way | ok |
| 5 | TEMPO below 40 BPM, PACE down: REC acts at once, TEMPO pulses evenly | ok |
| 6 | An external source at the input is recorded and heard while recording; B is not doubled | **not tested yet** |
| L1 | B recorded on A stays at its own level; the take shows up when REC stops | ok |
| L2 | Both decks SAMPLER recording each other; SAMPLER recording a BBD deck | ok |
| L3 | The held-request blink (8 Hz) | ok |
| L4 | TEMPO beat 1 at full, the other three at 0.35 | ok |
| L5 | An audio-in take through the new `tanh` path | **not tested yet** (needs the input, item 6) |
| L6 | The take becomes playable one beat after the stop press | ok |

## 5. What is left

- Item 6 and L5: an external source at the audio input, recorded on a SAMPLER
  deck, heard while recording, and compared by ear with a take from before the
  `tanh` path (`reva_play_no_snap` does not help there: the route is on in
  both images).
- The board stays on `reva_play` (build `68510c2`), the normal play image.

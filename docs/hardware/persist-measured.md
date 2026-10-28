# Panel-state persistence on Rev A: board probes

Measured on **2026-10-28**, one session, Rev A #1, branch `feat/panel-persist`.
Spec: [`2026-10-28-panel-state-persistence-design.md`](../superpowers/specs/2026-10-28-panel-state-persistence-design.md),
plan Task 6. Every number below was printed by the board; captures in
[`captures/`](captures/) as `persist-2026-10-28-*.txt`.

## Results

| Probe | Image | Verdict |
|---|---|---|
| 1 + 2, QSPI write safety and duration | `SHELL_PERSIST_STRESS=1` (forced erase + write every 2 s) | **pass** |
| Power cut mid-save | same | **pass, 3 cuts** (plan asked for ~10) |
| Reflash keeps the record | normal | **pass** |
| 3, change rate | normal | **failed at H = 16, passes with the per-pin band** |
| 4, boot window | normal | **~30 ms of record values; fixed by the boot mute** |
| 5, BBD boot | normal | **pass, both cases** |

### 1 + 2. Write safety and duration (`…-stress.txt`)

136 forced erase + program cycles, all `ok=1`, no fault, no reset, clean by
ear. Erase + program 64 034-79 968 us per save (one line read 4 277 147 363:
the old `GetUs()` wrap, since replaced by DWT cycles). Worst callback gap
960 668 cycles against the 960 000-cycle block: the QSPI never held the audio
interrupt off. `win` 491 622-676 094 cycles during saves.

A normal save writes a slot without erasing: **14-18 ms** (`us=13950`, 13992,
15653, 17963, DWT-timed, `…-bbd-boot.txt`).

### Power cut (`…-powercut-1.txt`, `…-powercut-2.txt`)

Three cuts during the stress image's saves; each booted `src=flash` with the
seq of the last `save … ok=1` printed before the cut (547, 559, 563). About
3.5 % of a stress cycle is a write, so three cuts are unlikely to have landed
mid-write; the torn-slot path is host tested, not board tested.

### Reflash (`…-reflash.txt`)

After a DFU flash of `shell-qspi.bin` the first line was `src=flash seq=572`,
and every later reflash of the day kept the newest seq: the persist sectors
(0x907FE000, 0x907FF000) lie above the DFU-writable region, which ends at
0x907C0000.

### 3. Change rate (`…-rate-h16.txt`, `…-rate-perpin.txt`)

With the coupon's band H = 16, owned pots re-emitted almost every pass hands
off: `chg` 76 → 1166 in about 13 minutes, 15 rows toggling by one printed step,
**no save in 15 minutes** -- the 15 s quiet never came. Cause: SENSE_0's pot
noise (fault R3, [`reva-bring-up.md`](reva-bring-up.md)), plus SENSE_1-3 at up
to 23 counts. Fix `b3350925`: `pot_hysteresis(sense)`, 64 on SENSE_0 and 32 on
SENSE_1-3. Re-run: the save came about 14 s after the last change; in the
following three minutes hands off `chg` rose by 4 (FLUXFB_A 457 ↔ 458, SMOOTH_A
once, both SENSE_0). Each such toggle can at most cause one save per floor.
No dead-band in `persist_differs()`; the change detection stays exact.

### 4. Boot window (`probe4*.wav`, not committed)

FILT A turned shut while off, the record holding it open: the first ~30 ms of
audio played the record's filter (−59 dB, spectral centroid ~2.9 kHz) before
the take landed (−42 dB, ~1.1 kHz). Fix `b35bc515`: the output is muted until
the take is done, then faded in over 20 ms. Re-run: first program at +55 ms
after the codec starts, already dark, rising over ~15 ms. A −70 dB decaying
residue precedes it (codec start, unaffected by the gate, ~45 dB under the
program).

Also seen: the board boots from 12 V alone (USB does not restart it); a
−22 dB thump at 12 V on comes from the output stage before the firmware runs,
and the firmware's audio starts about 3.1 s later (bootloader wait).

### 5. BBD boot (`…-bbd-boot.txt`)

- Deck A on BBD, entered live (excite set), saved as seq 576, power-cycled
  unchanged: `src=flash seq=576`, `boot ex=1,0 eng=5,1 take=1`.
- Saved on Sampler (seq 577), ENGINE A turned onto BBD while off:
  `src=flash seq=577`, `boot ex=0,0 eng=5,1 take=1`, `chg=0` -- excite as
  recorded, the engine from the pot, and no FLUX drop after the take (a
  bbd_edge would have zeroed FLUX and counted as a change).

ENGINE on the panel runs 0..5 (`control/params.hpp`): BBD is round(5 v) = 4,
about 70-90 % of travel.

## Found on the way

- **`System::GetNow()` runs slow on the board.** The save after the 5-min floor
  came 404 s after the previous one; the first quiet save about 18 s after the
  last pot move. Why HAL's SysTick count loses time is not measured. The
  policy's real times are therefore about 18-20 s quiet and 6.7 min floor:
  fewer saves than specified, which is the safe side. Anything timed with
  `GetNow()` in the shell is affected ([`docs/gotchas.md`](../gotchas.md)).
- The `boot` line went out once, before the host had the port open, and no
  capture held it; `f0be1105` latches it and repeats it beside every `src` line.

## Still owed

- The status-register (WRSR) endurance and whether a same-value WRSR costs a
  cycle: datasheet, not measured.
- A torn write on the board (more power cuts, or a cut timed into a write).
- C_SENSE0 (fault R3) would let SENSE_0's band come down from 64.

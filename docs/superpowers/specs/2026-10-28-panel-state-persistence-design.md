# Panel state persistence: the board wakes up as it was switched off

Date: 2026-10-28. Status: design, not built. Firmware: `shell/` (Rev A play image).

## 1. Intent

Bastian wants the Rev A board to be WYSIWYG across a power cycle. After a
restart it should sound "almost exactly" as it did when it was switched off,
MOD settings included, not like the init patch.

Two kinds of state exist on the panel:

- **What the pots show.** At boot every pot is read, and its position is the
  value. A pot turned while the board was off is honoured, because the panel
  is the truth.
- **What the pots cannot show.** This is the MOD layer's 50 depth twins, the
  values a pot is not addressing right now (STAGES while the deck is off BBD,
  ATTACK while it is on BBD), every parameter that has no pot, and a few
  law-side flags (§4.3). These come from a snapshot in QSPI flash.

Rejected alternative: restore everything from the snapshot and let pots take
over only on a move, as they do today. Bastian chose "the pots count" (2026-10-28).

## 2. Goals, non-goals, anti-goals

Goals
- After power-on the board plays the pot positions plus the saved hidden state.
- MOD depths survive a power cycle.
- Flash wear is negligible at 2–3 sessions a week (§5.3).

Non-goals
- **Saving every change.** Bastian accepts that a change made shortly before
  power-off may be lost. Saving is deliberately rare (§5).
- **A "back to init patch" key combo.** Bastian 2026-10-28: not needed now.
  The pots set every visible value anyway, and SHIFT + MOD already clears the
  MOD layer.
- **Restoring transient state.** The sampler buffer, LFO / sequencer phase,
  random state and the clock law's period history are not saved.
- **The coupon image, bench images and VCV.** VCV already has its own patch
  persistence.
- **Patch compatibility across firmware versions.** In dev alpha a
  parameter-list change simply invalidates the snapshot (§4.2).

Anti-goals (a failure even if it "works")
- An audible click, dropout or callback overrun while a save is running.
- The board booting with garbage or a half-written snapshot.
- The board booting with REC armed, i.e. recording without being asked.
- Pot positions being ignored at boot.

## 3. Constraints and facts (read from the source 2026-10-28, not measured)

- The knob vector `g_knobs[ffctl::NUM_PARAMS]` (`shell/panel_scan.cpp`) is the
  whole parameter state in VCV units. Today `control_boot()` fills it from
  `ffctl::kInitParamDefaults` and calls `g_law.on_restore()`
  (`shell/control_tick.cpp`).
- Pots write only once owned. `pot_own()` takes a pot after a move past
  `kPotTouch` (144 counts) from its first valid reading (`shell/pot_owner.h`).
  Before the first valid span sweep `span.valid` is false and no pot writes
  (`g_span`, `panel_scan.cpp`).
- With MOD latched, a pot whose param has a depth twin writes the twin
  (`pot_target()`, `shell/controls.cpp`). The ATTACK rows write STAGES while
  their deck is on BBD (`knob_target()`).
- `on_restore()` re-arms the BBD edge detectors, so a boot onto a BBD deck
  fires **no** `bbd_edge` -- but only until the restored engine has landed:
  `_bbdRestoring[p]` clears once `inst.engine_id(p)` matches, a few blocks
  after boot (`control/control_law.h`). An ENGINE write by the boot take
  (§4.4), which lands later, is a genuine edge unless §4.4 step 4 re-arms it. `excite_other_deck` lives in `g_opt`
  (`control_tick.cpp`). It is set only by `bbd_edge` and cleared on every tick
  the deck is off BBD (`apply_excite_events()`). A plain restore therefore loses
  it: before power-off a BBD deck had it on, after reboot it would be off, and
  the BBD deck would sound different. See §4.3.
- QSPI is 8 MB (`0x90000000`–`0x90800000`). The SRAM image sits at
  `0x90040000`, the wavetable bank at `0x90100000`, `.qspi_cold` from
  `0x90110000`. The `QSPI_COLD` linker region currently runs to the end of the
  chip (`shell/qspi_cold.ld`, `itcm_placement.py`, `qspi_placement.py`).
- During playback the audio path is meant not to read QSPI. The bank is
  copied to D2 SRAM (`wt_bank_d2.cpp`), and `qspi_placement.py` guards that
  only boot-only code executes in place. libDaisy's `QSPIHandle::Write` /
  `EraseSector` leave memory-mapped mode for the duration, so **any** QSPI
  read in that window (code or data, ISR or foreground) is fatal.
- libDaisy's `PersistentStorage` erases and writes one slot in place. A power
  loss mid-write leaves no valid copy, which is why it is not used here.
- The foreground loop of the play image is `run_panel_scan_report()`: it
  prints and then calls `hw.Delay(500)`.
- The chip is IS25LP064-class (libDaisy's comment). Its endurance is taken
  from the datasheet, about 100 000 erase cycles per sector. This is not
  checked against the actual part.

## 4. What is saved and what boot does

### 4.1 Snapshot contents

My call: the **whole** knob vector, not a hidden subset. On boot the pots
overwrite whatever they address (§4.4). Saving everything removes any table of
"which params are hidden", which would drift whenever the panel map changes,
and it handles ATTACK/STAGES and the no-pot params for free.

Record layout (little-endian):

| Field | Content |
|---|---|
| magic | a fixed 32-bit tag |
| layout | `NUM_PARAMS`, a 32-bit hash over the `ParamId` names in order **and** each param's `kParamRange` (lo, hi, snap), plus a hand-bumped `kSnapshotVersion` for meaning changes the hash cannot see (an ENG slot reorder like 337fdde4). My call |
| seq | 32-bit, incremented per save |
| knobs | `float[NUM_PARAMS]` |
| flags | `excite_other_deck[2]` (§4.3) |
| crc | CRC-32 over everything above |

With `NUM_PARAMS` = 128 that is about 530 B, so 7 records fit in a 4 KB sector.

### 4.2 Validity

A record is valid when magic, layout and CRC all match. Boot takes the valid
record with the highest `seq`, comparing across both slots. If none is valid
(first flash, parameter list changed, corruption), the hidden state comes
from the init patch, and the boot take (§4.4) still applies. The pots count
whether or not a snapshot exists.

Gating (my call): a build switch `SHELL_PERSIST`, default 1 in the Rev A play
image. `SHELL_PERSIST=0` gives today's behaviour exactly: init patch, no boot
take, no save. Use it for desktop-render comparisons with the audio rig and
for any measurement that needs the init patch. `SHELL_BOOT_PATCH` ≠ 0 forces
`SHELL_PERSIST=0`, because those stress patches depend on unowned pots
("the pots at noon hold it"). The coupon image does not compile it at all. The
switch must be resolved at parse time (memory
`fireflow-bench-stale-object-trap`).

### 4.3 What is NOT restored as saved

- `REC_A`, `REC_B`: always boot at the init value (off). The sampler buffer is
  empty after power-up, and booting into a running recording is an anti-goal.
- `MODBTN` (the latch): always boot unlatched. My call: at boot the pots must
  land on their sound params. With the latch on, they would write the depth
  twins instead and wipe the saved depths with pot positions.
- `excite_other_deck[p]`: restored from the record, but only if deck p's
  restored ENGINE is BBD. Off BBD, `apply_excite_events()` clears it on the
  first tick anyway.

### 4.4 Boot sequence

1. Fill `g_knobs` from the init defaults, then overlay the valid record if
   there is one (minus §4.3). Set `g_opt.excite_other_deck` from it.
2. `g_law.on_restore()` as today.
3. **Boot take:** on each pot's first reading with a valid span, the pot
   takes ownership immediately (as `Own::Took`), so its current position is
   written to its current target. `pot_target()` at that moment uses
   `MODBTN` = off and the ENGINE value in `g_knobs`. The ENGINE pots are
   themselves taken in the same sweep, so an ATTACK row can be read before
   its deck's ENGINE row has landed and then write the wrong one of
   ATTACK/STAGES. The builder must make the outcome independent of row order.
   My suggestion: let the first valid sweep only collect readings, then apply
   ENGINE rows first and the rest after.
4. The boot take is a restore, not a player move (my call). If it changes a
   deck's ENGINE (the pot was moved while the board was off, or there was no
   record), the BBD edge detector is re-armed after the boot take, as
   `on_restore()` does. So no `bbd_edge` fires, FLUX stays at its pot, and
   `excite_other_deck` stays as the record had it. §4.3's "restored ENGINE"
   means the value after the boot take.
5. The boot take writes once, then releases every pot (my call). From then on
   ownership works exactly as today: a pot writes again only once moved past
   `kPotTouch`. Untouched pots therefore stay silent after boot, as they do
   now, which keeps idle jitter out of stepped params and out of the save
   trigger. Later releases (bbd_edge FLUX, TEMPO on clock loss, SHIFT + MOD)
   are unchanged.

Before the boot take the engine plays the restored vector with the saved pot
values. From the code that window is at least two sweeps: `g_span` becomes
valid only at the end of the first sweep (`panel_scan.cpp`), and the first
valid-span readings arrive during the second. That is about 100 ms at the 50 ms
per sweep quoted in the raw-spread comment, which has not been re-measured. It can still be
heard if a pot was moved while the board was off. My call: accept the window,
or keep the output muted until the boot take is done. The builder probes
whether the window is audible and chooses; muting is preferred if it costs
little.

## 5. When to save

### 5.1 Trigger

My call (Bastian accepted "rarely" in principle; the numbers are mine):

- The audio ISR publishes a copy of `g_knobs` plus the flags once per sweep,
  using a sequence-counter handshake like the `g_raw_win` pattern in
  `panel_scan.cpp`, so the foreground never reads a torn vector.
- The foreground compares that copy with the last saved record.
- A save happens once the vector has stayed **unchanged for 15 s** AND at
  least **5 min** have passed since the last save. If a change arrives within
  those 5 min, its save waits until the 5 min are up and the vector is still
  quiet.
- A change in `MODBTN` or `REC_*` alone does not count as a change, since
  neither is restored.

Pot jitter: an owned pot only writes on a filter emission, which already has
hysteresis. The builder probes whether an untouched board ever shows "changed"
(§6). If it does, the comparison ignores sub-step float differences on
continuous params.

### 5.2 Where and how

- Two dedicated 4 KB sectors at the very top of the chip, for example
  `0x907FE000` and `0x907FF000`. The `QSPI_COLD` region and both placement
  guards shrink so the linker can never place anything there. The builder
  must check that the ITCM load image's placement respects the new end.
- Records are appended inside the active sector. When it is full, the next
  record goes to the other sector after erasing it. The old sector is erased
  only after a valid record exists in the new one. At every moment at least
  one valid record exists.
- The save runs in the foreground loop, never in the ISR.
- Erase/write must not overlap any QSPI read (§3). The builder proves this
  by probe, not by reading.

### 5.3 Wear (arithmetic, not measured)

Worst case: 3 h of continuous tweaking per session, so 36 saves (5-min
floor), times 3 sessions a week, makes about 5 600 records a year. With 7
records per sector before an erase, two sectors see under 500 erases each
per year. At 100 000 cycles that is centuries. Wear is not the limiting
factor; the 5-min floor is about keeping saves rare and out of the way.

**The status register is written too, and the paragraph above did not count
it** (found in the 2026-10-28 branch review, read from libDaisy, not
measured). Every `QSPIHandle::SetMode()` runs a full `Init()`
(`lib/libDaisy/src/per/qspi.cpp`, `SetMode` and `QuadEnable`), and `Init()`
calls `QuadEnable()`, an unconditional WRSR with the QE bit set, i.e. a write
of the flash's non-volatile status register. A save switches modes twice (into
indirect mode and back for the program), a save that rolls four times (erase,
then program). At 7 saves per roll that is about 2.3 status-register writes
per save:

- at the worst case above (5 600 saves a year): about 13 000 writes a year;
- at the policy's ceiling (a save every 5 min, a board left on with an owned
  pot whose jitter keeps counting as a change): about 240 000 writes a year.

Owed, not known: whether the IS25LP064A spends an endurance cycle on a WRSR
that writes the value already there, and what its status-register endurance
is (datasheet check). libDaisy is not patched for this round. Whether the fix
is a dead-band in `persist_differs()` (so jitter stops counting as a change)
or a read-before-write in `QuadEnable()` is decided after probe 3's measured
change rate (`SHELL_PERSIST chg=N`, §6 and plan Task 6).

## 6. Probes owed before the plan is final (probe rule)

None of these may enter the plan as a fact until a probe has printed it:

1. **QSPI write safety:** with audio running, do 100 erase+write cycles of a
   top sector from the foreground and count callback overruns, faults and
   `cb_max`. This answers whether anything in the ISR (or a libDaisy
   interrupt) touches memory-mapped QSPI.
2. **Duration:** measure the time one `EraseSector` and one ~600 B `Write`
   take on this board, in DWT cycles or ms.
3. **Idle stability:** on a board left alone after a session -- every pot
   turned past `kPotTouch`, so it stays owned and only `kPotHysteresis`
   stands between its jitter and a change -- how often the published vector
   differs from the last one, over 10 min. An untouched board cannot show it:
   after the boot take every pot is released and writes nothing.
4. **Boot window:** how long it takes from `StartAudio()` to the first valid
   span, and whether a pot moved while the board was off produces an
   audible jump.
5. **BBD boot:** with a deck on BBD and `excite_other_deck` restored, the
   board after reboot matches the board before power-off: same engine, FLUX
   at the pot, excite on. A second case moves the ENGINE pot onto BBD while
   the board is off; after boot no `bbd_edge` has fired (§4.4 step 4). Checked with the audio measurement rig (memory
   `fireflow-audio-measurement-rig`), not by ear alone.

## 7. Verification

- Host tests (doctest, `tests/`): the record encode/decode, CRC, layout
  mismatch, highest-seq selection across two slots, a torn final record, a
  full sector rolling to the other one, the §4.3 overrides, the boot-take
  order independence (§4.4 step 3), and the save trigger timing as a pure
  function of the change history. Each gate's RED is proven once.
- Board: set a patch with MOD depths and a BBD deck, wait for the save, power
  cycle, and compare against the pre-power-off state, first with the pots
  untouched and then with one pot moved while off.
- The probe outputs from §6 go into `docs/hardware/` and their facts into
  `docs/engine-map.md` / `shell/README.md` where they belong.

## 8. Left to the builder

- File and function names, and whether the record and selection logic live
  in a new host-tested header (recommended, like `pot_owner.h`).
- The exact layout hash mechanism.
- Mute versus accept for the boot window (§4.4), decided by probe 4.
- A serial line reporting the snapshot source at boot
  (`SHELL_PERSIST src=flash|init seq=N`) plus a line per save (recommended, it
  makes the board test readable).

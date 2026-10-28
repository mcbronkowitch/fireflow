# Panel State Persistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Rev A play image boots with the pots' positions plus the hidden state (MOD depths, STAGES/ATTACK, `excite_other_deck`, no-pot params) from a QSPI snapshot, saved rarely into two A/B sectors.

**Architecture:** The pure logic goes into new host-tested units, following the `pot_owner.h` / `panel_step.h` pattern:
- `persist_record` — record format, CRC, slot scan.
- `persist_policy` — save trigger and boot overlay.
- `boot_take` — the one-shot pot read at boot.

The firmware side adds three pieces:
- `persist_flash.cpp` — QSPI erase/write.
- A seqlock publish of `g_knobs` in the ISR.
- The save step in the foreground loop.

A parse-time switch `SHELL_PERSIST` gates all of it.

**Tech Stack:** C++17, doctest (host, clang + Ninja via `env.sh`, Release), ARM GCC + libDaisy (`shell/`), Python generators.

**Spec:** `docs/superpowers/specs/2026-10-28-panel-state-persistence-design.md`

## Global Constraints

- Host build: `source env.sh`, `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, `cmake --build build`. **ctest does not build**, so always run `cmake --build build` first.
- Full ctest runs the minutes-long `reva_*` PCB guards. Use `ctest --test-dir build --output-on-failure -E "^reva_"`, since `hardware/` is untouched.
- Firmware shell: never `source env.sh`. `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"`, `make -C shell -j8 images`.
- Never prefix a command with `cd`. Never write files with shell redirection; use the Edit/Write tools. Long compounds go into a scratchpad script.
- Everything in the repo is English. Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- Spec values:
  - Save after **15 s** unchanged AND at least **5 min** since the last save.
  - Sectors at **`0x907FE000`** and **`0x907FF000`** (4 KB each).
  - `QSPI_COLD` and both placement guards end at **`0x907FE000`**.
  - Record = magic, layout (`NUM_PARAMS`, layout hash, `kSnapshotVersion`), seq, `float[NUM_PARAMS]`, `excite_other_deck[2]`, CRC-32.
  - Boot overrides: `REC_A`/`REC_B`/`MODBTN` = init value; excite restored only if the post-boot-take ENGINE is BBD.
  - `SHELL_PERSIST` defaults to 1 on the Rev A play image. `SHELL_BOOT_PATCH` ≠ 0 forces 0.
- A test that cannot go red gets fixed: prove each new gate's RED once, by sabotaging the implementation (not via `git checkout <file>`).
- Scripted edits eat CRLF. Edit files with the Edit tool only.
- No runtime claim enters docs until a probe printed it (Task 6).

## Review Focus

1. **Power loss mid-write** (torn last record, or erase done but write not): boot must pick the previous valid record, never garbage. *Task 1:* `record: torn last record falls back to the previous one`. *Task 1:* `record: erased new sector with old sector intact`.
2. **First boot after flashing** (both sectors 0xFF, or leftover bytes from an older image): init plus boot take, no crash. *Task 1:* `record: all-0xFF and random bytes yield no record`.
3. **Firmware with a changed parameter list** (layout hash or version mismatch): the old snapshot is ignored. *Task 1:* `record: layout mismatch is invalid`. *Task 3:* the hash guard.
4. **Board left untouched for hours:** no save storm and no flash wear from jitter. *Task 2:* `policy: MODBTN/REC-only change never saves`. *Task 2:* `policy: repeated quiet periods save once`. *Task 6:* probe 3.
5. **ENGINE turned onto BBD while the board was off:** no FLUX drop, and excite as recorded. *Task 4:* `boot take: ENGINE row last in table still lands first`. *Task 5:* re-arm after the boot take. *Task 6:* probe 5.

---

### Task 1: Record format, CRC and slot scan (host)

**Files:**
- Create: `shell/persist_record.h`, `shell/persist_record.cpp`
- Create: `tests/test_persist_record.cpp`
- Modify: `CMakeLists.txt`: add both to `spky_tests` next to `shell/boot_patch.cpp`

**Interfaces:**
- Consumes: `ffctl::NUM_PARAMS`, `ffctl::kParamLayoutHash` (Task 3 generates it; until then the test passes the hash explicitly, see below).
- Produces:
```cpp
namespace shell {
inline constexpr uint32_t kPersistMagic      = 0x46465053u; // "SPFF"
inline constexpr uint32_t kSnapshotVersion   = 1;           // bump on a meaning change the hash cannot see
inline constexpr uint32_t kPersistSectorSize = 4096;
inline constexpr uint32_t kPersistStride     = 576;         // 7 slots per sector
inline constexpr int      kPersistSlots      = 7;
struct PersistLayout { uint32_t num_params, hash, version; };
struct PersistRecord {          // POD, the exact bytes written to flash
    uint32_t magic;
    PersistLayout layout;
    uint32_t seq;
    float    knobs[ffctl::NUM_PARAMS];
    uint8_t  excite[2];
    uint8_t  pad[2];
    uint32_t crc;               // CRC-32 (poly 0xEDB88320) over all bytes before it
};
static_assert(sizeof(PersistRecord) <= kPersistStride);
uint32_t crc32(const uint8_t* p, uint32_t n);
void     record_seal(PersistRecord& r);                       // sets magic + crc
bool     record_valid(const PersistRecord& r, const PersistLayout& want);
struct   SlotPick { int sector; int slot; uint32_t seq; };    // sector -1 = none
SlotPick pick_latest(const uint8_t* sec0, const uint8_t* sec1, const PersistLayout& want);
struct   SlotNext { int sector; int slot; bool erase_first; };
SlotNext next_slot(const uint8_t* sec0, const uint8_t* sec1, const SlotPick& latest);
PersistLayout current_layout();   // {NUM_PARAMS, kParamLayoutHash, kSnapshotVersion}
}
```
  `next_slot` rules:
  - Write to the first all-0xFF slot after `latest` in the same sector.
  - If that sector is full, write slot 0 of the other sector, with `erase_first = true`.
  - If there is no record at all, write sector 0, slot 0, with `erase_first` = whether that slot is not all-0xFF.
  - The old sector is never erased by this path. It is overwritten only when the next roll comes back to it, by which time the other sector holds a valid record.

- [ ] **Step 1: Write the failing tests** in `tests/test_persist_record.cpp`. The fixture builds two `uint8_t[4096]` sectors filled with 0xFF and writes sealed records into them with `memcpy` at `slot * kPersistStride`.
  - `crc32: check value` — `crc32("123456789", 9) == 0xCBF43926u`.
  - `record: sealed record is valid` — and flipping any one byte of `knobs` makes it invalid.
  - `record: layout mismatch is invalid` — each of `num_params`, `hash` and `version` off by one gives `record_valid == false`.
  - `record: all-0xFF and random bytes yield no record` — `pick_latest(...).sector == -1` for all-0xFF, and for an xorshift-filled pair (seed 0x9E3779B9).
  - `record: highest seq wins across sectors` — seq 5 in sec0/slot 6 and seq 6 in sec1/slot 0 → `{1, 0, 6}`.
  - `record: torn last record falls back to the previous one` — seq 3, 4 valid, seq 5 with a bad CRC → picks seq 4.
  - `record: erased new sector with old sector intact` — sec0 full (seqs 1–7), sec1 all 0xFF → picks seq 7. `next_slot` gives `{1, 0, false}`.
  - `next_slot: appends in the active sector` — latest at sec0/slot 2 → `{0, 3, false}`.
  - `next_slot: full sector rolls with erase` — latest at sec1/slot 6, sec0 holds old records → `{0, 0, true}`.
- [ ] **Step 2:** `cmake --build build` → FAIL (missing header).
- [ ] **Step 3: Implement** in `shell/persist_record.cpp`. Use table-less bitwise CRC-32. Size is not critical here: about 530 B, once per save. `current_layout()` may temporarily use a local `constexpr uint32_t` placeholder until Task 3 lands `ffctl::kParamLayoutHash`. Mark it with a `// Task 3 replaces` comment.
- [ ] **Step 4:** `cmake --build build && ctest --test-dir build -R spky_tests --output-on-failure` → PASS. Prove the RED once: break the seq comparison in `pick_latest` → `highest seq wins` fails. Then restore it with Edit.
- [ ] **Step 5: Commit** `feat(shell): persist record format, CRC-32 and A/B slot scan`.

### Task 2: Save policy and boot overlay (host)

**Files:**
- Create: `shell/persist_policy.h`, `shell/persist_policy.cpp`, `tests/test_persist_policy.cpp`
- Modify: `CMakeLists.txt` (spky_tests)

**Interfaces:**
- Consumes: `PersistRecord` (Task 1), `ffctl::kInitParamDefaults`, `ffctl::REC_A/REC_B/MODBTN/ENGINE_A/ENGINE_B`, `shell::kEngineSlotBbd` (`controls.h`).
- Produces:
```cpp
namespace shell {
inline constexpr uint32_t kSaveQuietMs = 15000;
inline constexpr uint32_t kSaveFloorMs = 300000;
bool persist_differs(const float* a, const float* b);   // ignores REC_A, REC_B, MODBTN; exact compare otherwise
struct SavePolicy { uint32_t last_change_ms = 0; uint32_t last_save_ms = 0; bool saved_once = false; bool dirty = false; };
// Called each foreground pass with the published vector's "differs from last saved" verdict.
bool save_due(SavePolicy& s, bool differs, uint32_t now_ms);    // true = write now
void save_done(SavePolicy& s, uint32_t now_ms);
// Fills knobs from init, overlays rec (if non-null), applies the §4.3 overrides. Returns the excite bits from rec.
void boot_overlay(const PersistRecord* rec, float* knobs, bool excite_out[2]);
// After the boot take: excite only where the deck's ENGINE is BBD.
void excite_after_take(const bool rec_excite[2], const float* knobs, bool excite_out[2]);
}
```
  `save_due` semantics:
  - A `differs` value that turns true, or changes, stamps `last_change_ms`. The caller compares the published vector against the previous pass's vector to detect a change; the builder's choice is two buffers.
  - Due once `now - last_change_ms >= kSaveQuietMs`, and either `!saved_once` or `now - last_save_ms >= kSaveFloorMs`.
  - Uses `uint32_t` wrap-safe subtraction.

- [ ] **Step 1: Write the failing tests**:
  - `policy: MODBTN/REC-only change never saves` — `persist_differs` is false when only those three ids differ.
  - `policy: quiet 15 s then save` — change at t=0 → not due at 14 999 → due at 15 000.
  - `policy: 5 min floor` — after `save_done(…, 20 000)`, a change at 30 000 is not due at 45 000 and not due at 319 999, but due at 320 000.
  - `policy: repeated quiet periods save once` — no new change after `save_done` → `save_due` stays false for 1 h of 500 ms passes.
  - `policy: change during quiet restarts the 15 s` — changes at 0 and 10 000 → due at 25 000, not at 15 000.
  - `policy: millis wrap` — `last_change_ms = 0xFFFFF000`, now = 0x00002A98 → due (15 000 ms elapsed).
  - `overlay: no record is the init patch` — equals `kInitParamDefaults` bit for bit, and excite is false.
  - `overlay: overrides` — rec with `REC_A=1, REC_B=1, MODBTN=1` and a depth twin set → those three take the init value, and the depth twin is kept.
  - `overlay: excite only on BBD` — rec excite {1,1}, knobs `ENGINE_A = kEngineSlotBbd`, `ENGINE_B` ≠ BBD → {true, false}.
- [ ] **Step 2:** build → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** build + `ctest -R spky_tests` → PASS. RED proof: drop `MODBTN` from the ignore list → the first test fails.
- [ ] **Step 5: Commit** `feat(shell): persist save policy and boot overlay`.

### Task 3: Generated layout hash

**Files:**
- Modify: `host/vcv/res/gen_panel.py`: `control_params_header()` emits `inline constexpr uint32_t kParamLayoutHash = 0x…u;` after `kParamRange`.
- Modify: `host/vcv/res/test_panel.py`: guard that the emitted hash equals a recomputation.
- Regenerated: `control/params.hpp` (and whatever else `gen_panel.py` writes; commit them all).
- Modify: `shell/persist_record.cpp`: replace the placeholder.

**Interfaces:**
- Produces: `ffctl::kParamLayoutHash` (uint32_t). It is FNV-1a 32 over, for each param in `ParamId` order, the UTF-8 name, a `\0`, then `repr(lo)`, `repr(hi)` and `"1"`/`"0"` for snap, each `\0`-terminated. Taken from the same lists that produce `kParamRange`.

- [ ] **Step 1:** Add the test to `test_panel.py`. It parses `control/params.hpp`, recomputes the hash from the generator's own lists, and asserts they are equal. It also asserts that renaming one param in a copy of the list changes the hash. Run `python res/test_panel.py` from `host/vcv/` → FAIL (no constant).
- [ ] **Step 2:** Emit the constant and run `python res/gen_panel.py` from `host/vcv/`. Then `python res/test_panel.py` → PASS. `git diff --stat` must show only the hash line added in the generated files.
- [ ] **Step 3:** Wire `current_layout()` to `ffctl::kParamLayoutHash`. Host build + `ctest -R spky_tests` → PASS.
- [ ] **Step 4:** `host/vcv/build-local.sh install` (memory `fireflow-vcv-finish-with-install`). Tell Bastian to restart Rack.
- [ ] **Step 5: Commit** `feat(control): generated parameter layout hash for the shell snapshot`.

### Task 4: Boot take (host)

**Files:**
- Create: `shell/boot_take.h`, `shell/boot_take.cpp`, `tests/test_boot_take.cpp`
- Modify: `CMakeLists.txt` (spky_tests)

**Interfaces:**
- Consumes: `ControlTable`, `ControlEntry`, `knob_target()`, `knob_from_pot()` (`controls.h`), `Span`, `span_normalize()` / the value path in `scan_value.h`, `PotOwner`, `pot_release()`.
- Produces:
```cpp
namespace shell {
struct BootTake {
    uint16_t raw[kMaxRows];  // kMaxRows: a constexpr >= kRevaTable.count (73); static_assert it
    bool     seen[kMaxRows];
    bool     done = false;
};
// One reading of row r (index into t.entries) with a valid span; ignored once done.
void boot_take_collect(BootTake& b, int row, uint16_t raw);
// True once every row of t has been seen.
bool boot_take_ready(const BootTake& b, const ControlTable& t);
// Writes every row once: first all rows whose param is ENGINE_A/ENGINE_B, then the rest via
// knob_target() (so ATTACK vs STAGES follows the freshly written ENGINE), MODBTN treated as 0.
// Rows with param -1 are skipped. Sets done.
void boot_take_apply(BootTake& b, const ControlTable& t, const Span& span, float* knobs);
}
```
  The value written uses the same normalisation `pot_filter()` would emit for that raw word on a fresh filter: snap regions and span, but no hysteresis history. Release of the owners is Task 5's wiring, not this unit.

- [ ] **Step 1: Write the failing tests** with a small hand-built `ControlTable`: ENGINE_A row, ATTACK_A row with alt STAGES_A, a FLUX_A row, and one param −1 row.
  - `boot take: not ready until every row seen`.
  - `boot take: ENGINE row last in table still lands first` — table order ATTACK row then ENGINE row. The ENGINE raw maps to `kEngineSlotBbd`, so the ATTACK raw is written to STAGES_A, and ATTACK_A keeps its prior value.
  - `boot take: non-BBD engine writes ATTACK` — the mirror case.
  - `boot take: MOD latch ignored` — `knobs[MODBTN] = 1` before apply → FLUX_A (has a twin) is written, MODD_FLUX_A is untouched.
  - `boot take: value matches the filter path` — for raws at 0, mid and rail, the written value equals what `pot_row_step()` writes for a freshly taken pot at that raw.
  - `boot take: collect after done is ignored`.
- [ ] **Step 2:** build → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** build + `ctest -R spky_tests` → PASS. RED proof: apply rows in table order → test 2 fails.
- [ ] **Step 5: Commit** `feat(shell): boot take -- one-shot pot read, ENGINE rows first`.

### Task 5: Firmware wiring, flash driver and switch

**Files:**
- Create: `shell/persist_flash.h`, `shell/persist_flash.cpp` (firmware only: `QSPIHandle`)
- Create: `shell/write_shell_persist.py` (copy the shape of `write_shell_keys_nc.py`)
- Modify: `shell/Makefile`: `SHELL_PERSIST ?= 1`; 0/1 check; `$(error)` if 1 and not (`SHELL_PANEL_SCAN=1` without `SHELL_COUPON_PROBE`), unless `SHELL_PANEL_SCAN=0`, in which case it is silently inert. Add to `SWITCH_HEADERS` with `panel_scan.o control_tick.o persist_flash.o` as stale objects, and add the header deps. Add the three new `.cpp` files to the sources.
- Modify: `shell/qspi_cold.ld` (`LENGTH = 0x907FE000 - 0x90110000`), `shell/itcm_placement.py` and `shell/qspi_placement.py` (`HI = 0x907FE000`), `shell/test_qspi_placement.py` if it pins the old end.
- Modify: `shell/control_tick.h/.cpp`, `shell/panel_scan.cpp`, `shell/main.cpp` (only if the QSPI handle must be passed in).
- Modify: `shell/check_images_build.py`: add `("reva_play_nopersist", ["SHELL_PANEL_SCAN=1", "SHELL_KEYS_NC=1", "SHELL_PERSIST=0"])`.
- Modify: `shell/README.md`: switch table row for `SHELL_PERSIST`, plus the QSPI map with the two sectors.

**Interfaces:**
- Consumes: Tasks 1, 2 and 4.
- Produces:
```cpp
namespace shell {
inline constexpr uint32_t kPersistSector0 = 0x907FE000u, kPersistSector1 = 0x907FF000u;
// Effective switch: SHELL_PERSIST && !(SHELL_BOOT_PATCH != 0), one constexpr bool in shell_persist.h's consumer.
void persist_boot(float* knobs, bool excite_out[2]);           // reads memory-mapped, calls pick_latest + boot_overlay
bool persist_write(daisy::QSPIHandle& q, const PersistRecord& r); // next_slot, erase if needed, Write, verify by re-reading
void control_set_excite(const bool excite[2]);                  // control_tick.cpp: into g_opt
void control_rearm_after_take();                                // control_tick.cpp: g_law.on_restore()
}
```
  Behaviour:
  1. `control_boot()`: when persistence is effective, call `persist_boot()` instead of the init loop, keep the excite bits, then `on_restore()` as today.
  2. `panel_scan_tick()`: while `!g_take.done`, readings with `g_span.valid` go to `boot_take_collect()` instead of `pot_row_step()`. Once `boot_take_ready()`: `boot_take_apply()`, `pot_release()` on every owner, `control_rearm_after_take()`, `excite_after_take()` → `control_set_excite()`.
  3. Same function, at `step == kSteps - 1`: publish `g_knobs` and the current excite flags into a seqlock buffer (`volatile uint32_t g_pub_seq`: odd while writing).
  4. `run_panel_scan_report()`: each pass reads the buffer, retrying on an odd or changed seq, and feeds `save_due()` with `hw.system.GetNow()` (or `daisy::System::GetNow()`). When due, it seals a record with `seq = latest + 1` and calls `persist_write()`. It prints `SHELL_PERSIST save seq=N us=T ok=0|1` and, once at boot, `SHELL_PERSIST src=flash|init seq=N`. Every line stays under 128 B.
  5. With persistence not effective, every path is byte-for-byte today's: no boot take, no publish, no save.

- [ ] **Step 1:** Make the guard edits (linker region + both placement scripts). Run the placement tests, if wired, or `python shell/test_qspi_placement.py` → PASS.
- [ ] **Step 2:** Add the switch, the files and the wiring as above.
- [ ] **Step 3:** Run `python shell/check_images_build.py`, which builds reva_play (persist on), reva_play_nopersist and coupon_play. All link and the placement guards pass. Then `cmp` the reva_play_nopersist SRAM image against a build of the previous commit with the same flags. They may differ only by the git hash stamp and the guard constants. Note what differs.
- [ ] **Step 4:** Host build + `ctest -E "^reva_"` → PASS (includes the shell image build test from 3a42d9c9).
- [ ] **Step 5: Commit** `feat(shell): persist the panel state across power cycles (SHELL_PERSIST)`.

### Task 6: Board probes (with Bastian, before any doc claim)

This task needs the board and Bastian's hands. The controller does not mark it done from a subagent report.

**Already built (Task 5 fix waves, on the branch):**
- `SHELL_PERSIST_STRESS ?= 0` (parse-time header, `write_shell_persist_stress.py`; acts only where persistence is effective; image `reva_play_stress` in `check_images_build.py`). With 1: a save forced every 2 s (quiet 2 s, floor 0) with an erase on every save, alternating sectors, so 100 saves are 100 erase+write cycles and every save is a roll. Each save line is `SHELL_PERSIST save seq=N us=T ok=0|1 cb_max=C win=W gap=G`: `cb_max` since the first second, `win` the worst callback in cycles since the previous save line, `gap` the worst DWT interval between two callback entries in that window (a 2 ms block is 960 000 cycles).
- In every persisting image: `SHELL_PERSIST src=flash|init seq=N` and `SHELL_PERSIST chg=N` (passes with a changed vector since boot) on the first pass and every 10th (~5 s); `SHELL_PERSIST boot ex=a,b eng=a,b take=0|1` once, ~1 s after the first pass (each deck's excite bit and running `engine_id`); `take=partial seen=N/M` only if the boot take fell back.

**Files:**
- Create: `docs/hardware/persist-measured.md`

**Wear bound (spec §5.3).** Every save also writes the flash's status register (~2.3 WRSR per save, libDaisy's `QuadEnable()` in every `SetMode()`), and the stress image does four per save. Until the datasheet check is done (same-value WRSR, status-register endurance), keep every stress run **≤ 30 min** (≤ 900 saves).

- [ ] **Step 1: Probe 1 + 2 (QSPI write safety and duration).** Flash the stress image as the play image (memory `fireflow-reva-play-image-flags`: clean build, `SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1 SHELL_PERSIST_STRESS=1`, QSPI first). Run at least 100 forced erase+write cycles with audio playing, at most 30 min.
  - Record per save: the `us=` spread (erase + program), `cb_max`, `win`, `gap`, and any fault, hang or reset (a reset shows as a new `src=` line with a fresh `chg=0`).
  - The first `ok=1` also confirms that libDaisy's `CheckProgramMemory()` lets the write through on this image (VTOR in SRAM).
  - Pass: no fault and no reset; `win` during saves within noise of the pre-save `cb_max`; `gap` never above one block period plus noise (a callback held off by the QSPI shows there even when its cycles do not).
  - If it fails: stop, report, and go back to the spec (§3 assumption broken).
- [ ] **Step 2: Power-cut probe.** With the stress image saving, pull power about 10 times at random points mid-save, rolls included (every stress save is a roll). After each power-up: `src=flash`, no fault, and the booted `seq` is either the last `save seq=… ok=1` printed before the cut or exactly one above it (power cut after the write, before its line went out). Any lower seq is a lost record and fails the probe. Record each pair (last printed seq, booted seq).
- [ ] **Step 3: Probe 3 (change rate).** Flash the normal image. Turn every pot past `kPotTouch` so it stays owned, and park each stepped pot near a step boundary. Leave the board alone for 10 min and read `chg=` at the start and the end. Record the difference and the save lines. That number decides the dead-band question in spec §5.3: zero means no dead-band; anything else means a dead-band in `persist_differs()` or a libDaisy read-before-write, chosen with Bastian.
- [ ] **Step 4: Probe 4 (boot window).** Power-cycle with one pot moved while off and record the output with the audio rig (memory `fireflow-audio-measurement-rig`). Measure the time from power-on to the level change and judge whether the jump is audible.
  - If it is audible and Bastian wants it gone, add a mute until `g_take.done` as a follow-up commit.
- [ ] **Step 5: Probe 5 (BBD boot).** Set deck A to BBD with MOD depths set, wait for the save line, and power-cycle. Read the `boot ex=… eng=… take=1` line: deck A must show excite 1 and the BBD engine id (`spky::ENGINE_BBD`). Compare rig recordings before and after. Then repeat with ENGINE A moved onto BBD while off: same line check, `src=flash`, FLUX at its pot, no FLUX drop. (The restored excite bit is held across the engine switch by `apply_excite_hold()`; this line is its board check.)
- [ ] **Step 6: Reflash check.** After any reflash of `shell-qspi.bin`, read the first `src=` line: `flash` with the old seq means the top sectors survive a DFU flash, `init` means they do not. Record which.
- [ ] **Step 7:** Write `docs/hardware/persist-measured.md` with the printed numbers, and move any lasting fact into `shell/README.md` and `docs/engine-map.md`. Commit `docs(hardware): persist probes on Rev A`.

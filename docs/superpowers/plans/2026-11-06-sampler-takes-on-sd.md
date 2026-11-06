# Sampler Takes on SD Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Each deck's sampler take is saved to the SD card on its own and is back in the buffer after a power cycle, exactly as it was (frames, slice markers and strengths, overdub behaviour), without the card ever blocking audio or the remote reflash.

**Architecture:** The engine gets four portable pieces, each host-tested:
- a written-frame watermark in `SampleBuffer`, so the buffer never has to be zeroed;
- a content generation per deck;
- slice-map export and import;
- a loading state with `sampler_begin_load` / `sampler_restore`.

`shell/` gets two pure, host-tested units:
- `take_file` — the WAV layout, CRC and slot choice;
- `take_policy` — when to save, pacing, abort, load order.

One firmware unit, `take_card.cpp`, does the FatFs work in the foreground and talks to the callback only through a mailbox. A parse-time switch `SHELL_TAKES` gates all of it; `SHELL_TAKES_STRESS` is the hands-free measurement mode.

**Tech Stack:** C++17, doctest (host, clang + Ninja via `env.sh`, Release), ARM GCC + libDaisy + FatFs R0.12 (68300) (`shell/`), Python generators.

**Spec:** `docs/superpowers/specs/2026-11-06-sampler-takes-on-sd-design.md` (revised after review, §10 there).

## Global Constraints

- Host build:
  - `source env.sh`, then `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, then `cmake --build build`.
  - **ctest does not build**, so always run `cmake --build build` first.
  - Run `ctest --test-dir build --output-on-failure -E "^reva_"`, since `hardware/` is untouched.
- Firmware shell:
  - Never `source env.sh` in that shell.
  - `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"`, then `make -C shell -j8 images`.
- Never prefix a command with `cd`. Never write files with shell redirection; use the Edit/Write tools. Long compounds go into a scratchpad script.
- Everything in the repo is English. Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- **Do not edit `lib/libDaisy` or `lib/DaisySP`.**
- **The render hash gates stay green unchanged.** `spky_tests`, `ctrl_identity` and `tests/check_render_hash.cmake` are byte-identity gates.
  - The engine tasks (1–3) must not move a single desktop render. Desktop memory is zero, so the watermark is invisible there.
  - A moved hash is a bug in the task, not a hash to re-bake.
- A test that cannot go red gets fixed. Prove each new gate's RED once by sabotaging the implementation with Edit, then restore it with Edit (never `git checkout <file>`).
- Scripted edits eat CRLF. Edit existing files with the Edit tool only.
- No runtime claim enters docs until a probe or the board printed it (Tasks 9–10).
- **Parallel implementer etiquette** (memory `fireflow-parallel-implementer-etiquette`): one implementer writes into the tree at a time.
- Spec values (block = 2 ms = 96 samples at 48 kHz):

| constant | value | meaning |
|---|---|---|
| `kTakeQuietBlocks` | 2 500 | 5 s quiet before a save |
| `kTakePieceBytes` | 16 384 | one paced write |
| `kTakePieceEveryBlocks` | 21 | 42 ms between pieces; Task 9 may change it |
| `kTakeHeaderBytes` | 4 096 | header block; `data` starts here |
| `kTakeFailLimit` | 3 | failed saves in a row before saves stop for the session |
| `kTakeLoadDeadlineMs` | 10 000 | per deck, DWT-timed |

  Paths: `/FIREFLOW/A0/TAKE.WAV`, `A1`, `B0`, `B1`. Slot file size: 4 096 + 16 128 000 + 8 B.

## Review Focus

1. **Power loss mid-save:** the previous take must load, never garbage and never nothing.
   - *Task 4:* `take_file: seq 0 header is invalid`, `take_file: CRC mismatch falls back to the other slot`.
   - *Task 10:* power cut during a save.
2. **SDRAM garbage:** an empty deck must record exactly as on zeroed memory — after boot, after a clear, after a rejected load.
   - *Task 1:* `buffer: fresh take on dirty memory equals clean memory`.
   - *Task 3:* `restore: rejected load leaves the deck clean`.
3. **The callback never does bulk work:** no memset, no CRC, no card call; the clear is O(1).
   - *Task 1:* `buffer: clear does not touch the frames`.
   - *Task 10:* `cb_max` across a SHIFT + REC.
4. **A hung or missing card:** the board plays and stays reflashable.
   - *Task 8:* the card work runs only after `StartAudio()`, the listener and the boot take.
   - *Task 10:* no-card and corrupt-card boots, then `remote_run.py boot`.
5. **Races between foreground and callback:** a save never captures a half-overdubbed buffer or a torn slice map.
   - *Task 5:* the sequence-lock cases.
   - *Task 2:* `gen: late fade-out end moves the generation` (spec P5).

---

### Task 1: Written-frame watermark in `SampleBuffer` (engine)

**Files:**
- Modify: `engine/sampler/sample_buffer.h`, `engine/sampler/sample_buffer.cpp`
- Modify: `engine/sampler/sampler_engine.h` — rewrite the `sample_data()` note: hosts no longer have to inject zeroed memory.
- Modify: `tests/test_sample_buffer.cpp`

**Interfaces:**
```cpp
// SampleBuffer
size_t written() const { return _written; }   // frames at or past this count as silence
// write(): when _write_head >= _written, the old frame reads as {0, 0}; after the
//          store, _written = max(_written, _write_head + 1) (before the head advances)
// clear(): _written = 0, NO memset (state reset only)
// set_rec_size(n): _written = n (n clamped as today); n == 0 -> clear()
```
Behaviour:
- Reads (`read_linear`) are unchanged; they never pass `rec_size() <= written()`.
- The fade-out's rewind to 0 on an uncut take reads frames below `_written`. That is the take's own start, as today.

- [ ] **Step 1: Write the failing tests** in `tests/test_sample_buffer.cpp`:
  - `buffer: fresh take on dirty memory equals clean memory`:
    - Two buffers of 48 000 frames: one zeroed, one filled with xorshift garbage (seed 0x9E3779B9, values ±3).
    - Record the same 0.5 s sine take, release, then one overdub pass at the default feedback.
    - The first `rec_size()` frames must be bitwise equal.
  - `buffer: clear does not touch the frames`:
    - Record a take, then `clear()`.
    - The raw frames are unchanged (bitwise). `rec_size() == 0`, `written() == 0`.
    - A new take on top equals one on zeroed memory (as above).
  - `buffer: set_rec_size sets the watermark`: `set_rec_size(1000)` → `written() == 1000`; an overdub then reads frames below 1000 and none above.
- [ ] **Step 2:** `cmake --build build` → the new tests FAIL. `written()` is missing; once stubbed, the dirty-memory test fails on the garbage.
- [ ] **Step 3: Implement** as above. One compare per `write()`; no branch in `read_linear`.
- [ ] **Step 4:** `cmake --build build && ctest --test-dir build --output-on-failure -E "^reva_"` → PASS, **render hashes unchanged**.
  - Prove the RED once: make `write()` read the old frame unconditionally → `dirty memory` fails. Restore it.
- [ ] **Step 5: Commit** `feat(sampler): a written-frame watermark replaces zeroed memory`.

### Task 2: Content generation (engine)

**Files:**
- Modify: `engine/sampler/sample_buffer.h/.cpp` — the state machine's idle edges.
- Modify: `engine/sampler/sampler_engine.h/.cpp`, `engine/instrument.h`
- Modify: `tests/test_sampler_engine.cpp`

**Interfaces:**
```cpp
uint32_t SampleBuffer::gen() const;             // +1 on idle -> fadein and on -> idle
uint32_t SamplerEngine::content_gen() const;    // buffer gen + own bumps (clear, load_sample, restore)
uint32_t Instrument::sampler_content_gen(int p) const;
```
- `SampleBuffer::clear()` called from `init()` does **not** count; `SamplerEngine::clear()` does. Implement it as a separate counter in `SamplerEngine` added to the buffer's, so `init()` cannot bump it.

- [ ] **Step 1: Write the failing tests:**
  - `gen: take start and end move it, nothing else does`:
    - Record, release, run through the fade-out.
    - Exactly two increments, the second on the sample where `is_recording()` turns false.
    - 10 s of idle `process`, a feedback change and a SCAN change move nothing.
  - `gen: clear, load_sample and restore move it` — `restore` is added in Task 3, so extend this test there.
  - `gen: late fade-out end moves the generation` (spec P5), on `Instrument` with a static 10 s buffer:
    - SAMPLER, REC on, then SYNTH after 1 s, then REC off.
    - 2 s on SYNTH: `sampler_is_recording(0)` is still true and the generation has not moved since the start.
    - Back to SAMPLER: once `is_recording()` drops, the generation has moved by exactly 1.
- [ ] **Step 2:** build → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** build + ctest → PASS, hashes unchanged. RED: drop the bump on the `-> idle` edge → the P5 test fails. Restore it.
- [ ] **Step 5: Commit** `feat(sampler): a content generation per deck`.

### Task 3: Slice-map export/import, loading state, restore (engine)

**Files:**
- Modify: `engine/sampler/slice_map.h/.cpp`, `engine/sampler/sampler_engine.h/.cpp`, `engine/instrument.h`
- Modify: `tests/test_slice_map.cpp`, `tests/test_sampler_engine.cpp`

**Interfaces:**
```cpp
// SliceMap
int  export_markers(uint32_t* frame, uint8_t* strength, int max) const;   // returns count
bool import_markers(const uint32_t* frame, const uint8_t* strength, int n, size_t content);
     // false (map cleared) unless strictly ascending, each < content, n <= kMax;
     // detector left as scan() leaves it (_last_frame = SIZE_MAX, envelopes reset)
// SamplerEngine / Instrument (callback side)
void sampler_begin_load(int p);   // grains killed, clear() (O(1)), REC locked out
bool sampler_loading(int p) const;
bool sampler_restore(int p, size_t frames, const uint32_t* mf, const uint8_t* ms, int n);
     // set_rec_size(frames) over what is already in the buffer, import markers,
     // generation +1, REC unlocked; false and an empty deck if the markers are rejected
void sampler_abort_load(int p);   // empty deck, REC unlocked
SampleBuffer::Frame* sampler_buffer(int p); size_t sampler_capacity(int p) const;
```
- REC lock-out: while loading, `set_recording(true)` is ignored. The law's idempotent push (`control_law.h`, `wantRec`) therefore starts the take once the load ends with REC still latched. Write that down in a comment; it is intended.

- [ ] **Step 1: Write the failing tests:**
  - `slice_map: export/import round trip` — includes strengths, plus each rejection (unsorted, equal neighbours, `>= content`, `n > kMax`).
  - `restore: exact` (spec P4 with the engine):
    - Engine A records a 4 s burst take (P2's material) and one overdub.
    - Export the frames (copy `rec_size` frames) and the markers.
    - Engine B runs `begin_load`, gets the frames copied in, then `restore`.
    - Equal: `rec_size` and the markers (frame and strength).
    - Then the same overdub on both: the buffers are bitwise equal over `rec_size`.
    - Do not compare `process()` output between A and B. A's grain RNG and playhead ran during the recording and B's did not, so the outputs differ for reasons the restore does not cover. Playhead and cloud are transient state, outside the spec (§2).
  - `restore: load_sample is not exact` (the RED for the above, kept as a test): restoring B through `load_sample()` instead makes at least one marker strength differ. This pins spec P2. If it ever passes, the file format can drop the markers; report it, do not delete the test.
  - `restore: rejected load leaves the deck clean`:
    - On dirty memory: `begin_load`, garbage written into the buffer, `abort_load`.
    - A fresh take then equals one on zeroed memory.
  - `restore: REC is locked out while loading` — `sampler_record(p, true)` during the load leaves `is_recording()` false; after `restore`, the next push starts recording.
  - Extend Task 2's `gen` test with `restore`.
- [ ] **Step 2:** build → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** build + ctest → PASS, hashes unchanged. RED: skip `import_markers` in `restore` → `restore: exact` fails on the strengths. Restore it.
- [ ] **Step 5: Commit** `feat(sampler): restore a take with its slice map`.

### Task 4: `take_file` — layout, CRC, slot choice (shell, host)

**Files:**
- Create: `shell/take_file.h`, `shell/take_file.cpp`, `tests/test_take_file.cpp`
- Modify: `CMakeLists.txt` — add both to `spky_tests`, next to `shell/persist_record.cpp`.

**Interfaces:**
```cpp
namespace shell {
inline constexpr uint32_t kTakeMagic = 0x4D534646u;  // "FFSM"
inline constexpr uint32_t kTakeVersion = 1;
inline constexpr uint32_t kTakeHeaderBytes = 4096;
inline constexpr uint32_t kTakeMaxFrames = 2016000;  // static_assert == kSamplerFrames in sdram_mem.h's user
struct TakeMeta {
    uint32_t seq; uint32_t frames; uint8_t deck;
    int      n_markers; uint32_t marker_frame[512]; uint8_t marker_strength[512];
    uint32_t crc;            // over the meta fields before it, then the data bytes
};
// CRC-32, poly 0xEDB88320, incremental, table-driven (the boot load runs it over 16 MB)
uint32_t crc32_begin();
uint32_t crc32_update(uint32_t c, const uint8_t* p, uint32_t n);
uint32_t crc32_end(uint32_t c);
uint32_t take_meta_crc(uint32_t c, const TakeMeta& m);   // folds the meta fields in, fixed order
// The 4 096 B header block: RIFF size, fmt (format 3, 2 ch, 48 000, 32 bit, plus fact),
// ffsm, data header at 4 088 with size frames*8. The JUNK header is take_junk_header().
void take_header_encode(const TakeMeta& m, uint8_t out[kTakeHeaderBytes]);
bool take_header_decode(const uint8_t in[kTakeHeaderBytes], uint8_t deck, TakeMeta& m);
     // false on: magic, version, deck, seq == 0, frames > max, markers unsorted / >= frames / > 512
void take_junk_header(uint32_t frames, uint8_t out[8]);   // pads to the preallocated size
inline constexpr uint32_t kTakeFileBytes = kTakeHeaderBytes + kTakeMaxFrames * 8 + 8;
struct SlotOrder { int first; int second; };              // -1: none
SlotOrder take_slot_order(const bool valid[2], const uint32_t seq[2]);  // newest first
int take_next_slot(int loaded_slot);                      // the other one; 0 if none loaded
}
```

- [ ] **Step 1: Write the failing tests:**
  - `crc32: check value` — `"123456789"` → `0xCBF43926`. Incremental in three uneven pieces equals one call.
  - `take_file: header round trip` — includes 512 markers, and `frames = 0`.
  - `take_file: the header is a valid WAV`:
    - `RIFF` size equals `kTakeFileBytes - 8`.
    - `fmt` format tag 3, 2 channels, 48 000 Hz, block align 8, 32 bit.
    - `data` at offset 4 088 with size `frames * 8`.
    - The `JUNK` chunk ends exactly at `kTakeFileBytes`.
    - Checked by a small independent RIFF walker written inside the test.
  - `take_file: seq 0 header is invalid`, then one test per remaining rejection.
  - `take_file: CRC mismatch falls back to the other slot` — `take_slot_order` with both valid gives newest first. The caller tries `second` after a CRC failure; the test drives that sequence with a fake CRC result.
  - `take_file: neither slot valid` → `{-1, -1}`.
- [ ] **Step 2:** build → FAIL.
- [ ] **Step 3: Implement.** Plain byte writes, little-endian, no struct punning into the header.
- [ ] **Step 4:** build + ctest → PASS. RED: drop the `seq == 0` check → `seq 0 header is invalid` fails. Restore it.
- [ ] **Step 5: Commit** `feat(shell): take file layout, CRC-32 and slot order`.

### Task 5: `take_policy` — when to save, pacing, abort, load (shell, host)

**Files:**
- Create: `shell/take_policy.h`, `shell/take_policy.cpp`, `tests/test_take_policy.cpp`
- Modify: `CMakeLists.txt` (`spky_tests`)

**Interfaces:**
```cpp
namespace shell {
struct DeckView { uint32_t gen; bool recording; bool loading; };   // read by the caller (volatile)
enum class TakeStep : uint8_t { Idle, Begin, Piece, Finish, Abort, Off };
struct TakeSaver {
    // per deck
    uint32_t saved_gen[2] = {}; uint32_t quiet_since[2] = {}; uint32_t seen_gen[2] = {};
    // the save in flight
    int deck = -1; uint32_t gen_at_begin = 0; uint32_t next_offset = 0; uint32_t frames = 0;
    uint32_t next_piece_block = 0; int fails = 0; bool off = false;
};
void take_seed(TakeSaver& s, int deck, uint32_t gen);   // after a restore or an empty boot
// One call per foreground service tick. `now` in audio blocks (g_block_tick).
// It returns what to do next; the caller does the card work and reports back.
TakeStep take_next(TakeSaver& s, const DeckView d[2], uint32_t now, uint32_t rec_frames_of_deck);
// Sequence-lock check AFTER a piece or the slice-map copy: true = still consistent.
bool take_still_valid(const TakeSaver& s, const DeckView& d);
void take_piece_done(TakeSaver& s, uint32_t now);   // advances next_offset, schedules the next piece
void take_finished(TakeSaver& s, bool ok);          // ok: saved_gen = gen_at_begin; !ok: ++fails, Off at the limit
void take_aborted(TakeSaver& s);                    // back to waiting for quiet; not a failure
}
```
Rules (spec §4.3):
- A deck is due when `gen != saved_gen`, `!recording`, `!loading`, and the gen has been unchanged for `kTakeQuietBlocks`.
- A is before B. One save at a time.
- Pieces start no closer together than `kTakePieceEveryBlocks`.
- `Finish` comes once `next_offset >= frames * 8`.
- `Off` is sticky after `kTakeFailLimit` failures in a row.

- [ ] **Step 1: Write the failing tests:**
  - `policy: saves 5 s after a take, once`:
    - gen 0 → 1 (start) → 2 (end) at block 100.
    - `Begin` exactly at 2 600, not at 2 599.
    - After `take_finished(ok)`, nothing more for 100 000 blocks.
  - `policy: overdub within the quiet window saves once`.
  - `policy: no save right after a restore` — `take_seed(gen)`, then 100 000 quiet blocks → `Idle`.
  - `policy: pacing` — pieces 21 blocks apart, never closer, also when `take_next` is called every block.
  - `policy: REC mid-save aborts` — `take_still_valid` false once `recording` is set; the same on a gen change and on a clear (gen change). After `take_aborted`, the deck is due again only after a new quiet window.
  - `policy: clear saves frames 0` — `rec_frames_of_deck = 0` → `Begin` then `Finish` with no `Piece`.
  - `policy: A before B; one at a time`.
  - `policy: three failures turn saving off`; a success in between resets the count.
  - `policy: a loading deck is never due`.
  - `policy: the deck stuck in P5` — `recording` true forever → never due. Once `recording` drops with a new gen → due after the quiet window.
- [ ] **Step 2:** build → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4:** build + ctest → PASS. RED: compare `>=` instead of `>` on the quiet window → the off-by-one test fails. Restore it.
- [ ] **Step 5: Commit** `feat(shell): take save policy`.

### Task 6: SHIFT + REC clears a deck (shell, host)

**Files:**
- Modify: `shell/controls.h`, `shell/controls.cpp`, `tests/test_controls_map.cpp` (where `apply_key_edges` is tested today, next to `tests/test_panel_step.cpp`)

**Interfaces:**
```cpp
struct KeyResult { bool mod_changed = false; bool mod_cleared = false; bool rec_cleared[2] = {false, false}; };
// apply_key_edges: RecA/RecB with SHIFT held -> knobs[REC_x] = 0, rec_cleared[x] = true, no toggle
```
- The callback side (Task 8) turns `rec_cleared[p]` into `inst.sampler_clear(p)`. That is O(1) after Task 1.

- [ ] **Step 1:** tests:
  - `keys: SHIFT + REC A clears A and releases its latch` — from latched and from unlatched. B is untouched. MOD is untouched.
  - `keys: REC without SHIFT still toggles`.
- [ ] **Step 2:** FAIL. **Step 3:** implement. **Step 4:** PASS. RED: toggle instead of release → fails. Restore it.
- [ ] **Step 5: Commit** `feat(shell): SHIFT + REC clears the deck's take`.

### Task 7: The pot hold becomes a shared unit (shell)

**Files:**
- Create: `shell/sd_hold.h`, `shell/sd_hold.cpp` (firmware). It holds `g_sd_seen`, `sd_hold_block()` and an `SdMark` RAII guard; `SdHold` itself stays in `sd_schedule.h`, already host-tested.
- Modify: `shell/sd_probe.cpp`, `shell/sd_probe.h`, `shell/panel_scan.cpp` — `#if SHELL_SD` becomes `#if SHELL_SD || SHELL_TAKES` around the hold. `SHELL_TAKES` does not exist until Task 8; guard with `defined(SHELL_TAKES) && SHELL_TAKES` until then.
- Modify: `shell/Makefile` — add `sd_hold.cpp` to the sources.

Behaviour:
- In the SD probe images nothing changes: the same tail per mode (`sd_hold_tail()`).
- In the take image the tail is always 0 (A1).

- [ ] **Step 1:** Move the code; keep `sd_hold_tail()` in `sd_probe.cpp`. The take image's tail is a constant 0 in `sd_hold.cpp` behind the switch.
- [ ] **Step 2:** `python shell/check_images_build.py` → every existing image builds, the SD probe images included.
- [ ] **Step 3:** `ctest -E "^reva_"` → PASS. That includes the image build test.
- [ ] **Step 4: Commit** `refactor(shell): the SD pot hold as its own unit`.

### Task 8: Firmware wiring — `SHELL_TAKES`, card worker, mailbox, lines

**Files:**
- Create: `shell/take_card.h`, `shell/take_card.cpp` (firmware: FatFs, `SdmmcHandler`, `FsBlock`, as in `sd_probe.cpp`)
- Create: `shell/write_shell_takes.py`, `shell/write_shell_takes_stress.py` (copy the shape of `write_shell_persist_stress.py`)
- Modify: `shell/Makefile`:
  - `SHELL_TAKES ?= 0` and `SHELL_TAKES_STRESS ?= 0`, each with a 0/1 check.
  - `USE_FATFS = 1` when on.
  - `$(error)` for `SHELL_TAKES=1` together with `SHELL_SD=1`, `SHELL_REMOTE=1`, `SHELL_REMOTE_MEASURE=1`, `SHELL_RGRAIN≠0`, `SHELL_BOOT_PATCH≠0`, `SHELL_COUPON_PROBE=1`, or without `SHELL_PANEL_SCAN=1`.
  - `SHELL_TAKES_STRESS=1` needs `SHELL_TAKES=1`.
  - Both headers go into `SWITCH_HEADERS`, with `panel_scan.o take_card.o sd_hold.o main.o` as stale objects (gotcha: two switch positions must never yield the same image).
- Modify: `shell/sdram_mem.cpp` — `alignas(32)` on `g_sampler` plus `static_assert`s on the address alignment (at runtime, once at boot, printed) and on `sizeof(Frame) * kSamplerFrames % 32 == 0`.
- Modify: `shell/panel_scan.cpp`:
  - The callback applies the mailbox at the top of `panel_scan_tick`: `begin_load`, `restore`, `abort_load`, and `sampler_clear` from `KeyResult.rec_cleared`.
  - It publishes each deck's `DeckView` (volatile, written once per block).
  - It counts `g_block_tick`.
  - In `run_panel_scan_report`: the card work starts once `boot_take_ready`, and `hw.Delay(500)` becomes `take_service_for(500 ms)`, counted in blocks.
- Modify: `shell/check_images_build.py`:
  - `reva_play_takes` = `SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1 SHELL_TAKES=1`;
  - `reva_play_takes_stress` = the same plus `SHELL_TAKES_STRESS=1 SHELL_RAW_SPREAD=1`;
  - refused entries for `SHELL_TAKES=1 SHELL_SD=1` and `SHELL_TAKES=1 SHELL_REMOTE=1`, with their expected messages.
- Modify: `shell/README.md` — switch table rows, the card layout, the boot order.

**Interfaces:**
```cpp
namespace shell {
// Foreground only.
void take_card_boot(bench::Board& hw);        // after boot_take_ready(): mount, create, load both decks
void take_service_for(bench::Board& hw, uint32_t ms);   // runs take_policy + card work until ms are up
void take_report(bench::Board& hw);           // the latched SHELL_TAKE lines, every 10th pass (5 s)
// Callback side (panel_scan.cpp), one per block.
void take_mailbox_apply(spky::Instrument& inst);
void take_publish(const spky::Instrument& inst);
}
```
Behaviour:
1. **Boot.** Nothing before `StartAudio()`.
   - Once the boot take is done: mount with the bus clock stopped between transfers (`CLKCR.PWRSAV`, as `sd_probe.cpp` `kClockPowerSave`).
   - Create the four slot files where missing: `f_lseek` to `kTakeFileBytes`, then `f_sync`. Check the fast-seek cluster map once and log `frags=`.
   - Per deck:
     - mailbox `begin_load`, wait one block;
     - read both headers, `take_slot_order`;
     - read `data` back to back in 16 KB pieces into `sampler_buffer(p)`, under `SdMark`, while running the CRC;
     - on a mismatch, try the other slot;
     - mailbox `restore` (or `abort_load`), then `take_seed`.
   - A DWT deadline of `kTakeLoadDeadlineMs` per deck: on expiry, `abort_load`, and saves stay off.
2. **Save** (driven by `take_next`). Begin, Piece and Finish follow spec §4.2 exactly:
   - **Begin:** header with `seq = 0`, then `f_sync`.
   - **Piece:** one 16 KB `f_write` from the SDRAM buffer under `SdMark`, then the CRC over the same bytes, then `take_still_valid`.
   - **Finish:** copy the slice map (`export_markers`), run `take_still_valid` again, write the `JUNK` header, write the final header, then `f_sync`.
   - Any FatFs error → `take_finished(false)`.
3. **Lines**, each under 128 B, latched and repeated every 5 s:
   - `SHELL_TAKE mount fr= frags= sdram_align=`
   - `SHELL_TAKE load deck= slot= seq= frames= ms= crc=ok|bad|- src=card|empty|deadline`
   - `SHELL_TAKE save deck= slot= seq= frames= ms= worst_us= held= blocks= ok=0|1|abort`, where `held` counts the blocks the hold dropped during the save and `blocks` the save's length in blocks.
4. **`SHELL_TAKES_STRESS=1`**, hands-free measurement:
   - If deck A loads empty, the foreground writes a 10 s deterministic pattern (xorshift, ±0.5) into its buffer and restores it with no markers.
   - Then a save of deck A is forced every 30 s, ignoring the generation.
   - After each save, the file is read back and compared (`cmp=ok|bad`).
   - The CRC timing for both implementations is printed once: `SHELL_TAKE crc sw_ms= hw_ms=` over the 10 s pattern, if the builder adds the H7 CRC unit. The hardware path must equal `crc32_update`.
   - It prints the raw windows (`SHELL_RAW_SPREAD=1`), so Task 9 can judge the pots as A1 did.

- [ ] **Step 1:** Makefile switches, the two writer scripts, the refusals. `python shell/check_images_build.py` → the refused entries fail with their messages, and the rest still build.
- [ ] **Step 2:** `take_card.cpp`, the mailbox and the report-loop hook, as above.
- [ ] **Step 3:** `python shell/check_images_build.py` → all images link, `reva_play_takes` and `reva_play_takes_stress` included.
  - From `shell.map`, record where `ff.o`, `take_card.o` and `sd_hold.o` land (ITCM, SRAM, `QSPI_COLD`) and the AXI SRAM left. That is spec §6.5.
  - If FatFs lands in `QSPI_COLD`, confirm that no QSPI erase (the persistence save) can run while a FatFs call does. Both are foreground and sequential, so that is a code-reading check. Write it down.
- [ ] **Step 4:** `cmp` `reva_play` (`SHELL_TAKES=0`) against a build of the commit before Task 7 with the same flags. They may differ only by the git stamp and the hold's guard. Note what differs.
- [ ] **Step 5:** host build + `ctest -E "^reva_"` → PASS.
- [ ] **Step 6: Commit** `feat(shell): sampler takes on the SD card (SHELL_TAKES)`.

### Task 9: Board session A — hands-free probes (spec §6; Bastian's go first)

This task needs the board, and needs Bastian's go-ahead for this session: remote flashing was cleared for the CPU hunt, not as a standing rule. Flash per memory `fireflow-reva-play-image-flags`: clean build, QSPI first, `remote_run.py boot`. The controller does not mark this task done from a subagent report. Protect Bastian's persisted patch: no stress image that writes QSPI.

**Files:**
- Create: `docs/hardware/takes-measured.md`

- [ ] **Step 1 (§6.1): hold share.** Flash `reva_play_takes_stress` and let it run 20 min hands-off, then repeat with `kTakePieceEveryBlocks = 10` (21 ms; one commit per point, memory `fireflow-bench-clean-tree-guard`).
  - Per save: `held/blocks`, `worst_us`, `ms`. Judge the raw windows as A1 did (`read_sd.py` or `read_pots.py`; extend the reader if it needs the new line, with its `test_read_*.py`).
  - Gate: kept readings at the idle floor, no pot at or over `kPotTouch`, `held/blocks ≤ 10 %` at the chosen pace.
  - If 42 ms misses the gate: pick the next slower pace and re-run. Do not loosen the gate.
- [ ] **Step 2 (§6.2): the first write after mount.** Power-cycle the stress image twice through `remote_run.py boot` (the slots exist after the first boot). The first save's `worst_us` on the second boot must be ≤ 131 ms. Record both boots.
- [ ] **Step 3 (§6.3): load time** from the `load` lines: deck A at 10 s (the stress pattern), deck B empty. Two full decks need Bastian (Task 10).
- [ ] **Step 4 (§6.4): CRC cost** from the `crc` line. If the hardware unit is not built, record the software figure only, and the load's `ms` with and without CRC.
- [ ] **Step 5:** `cb_max` across the whole session, against the no-takes play image on the same patch, as D2 compared idle and busy.
- [ ] **Step 6:** Write `docs/hardware/takes-measured.md` with the printed numbers. Commit `docs(hardware): take-save probes on Rev A`.

### Task 10: Board session B — with Bastian's hands (spec §7)

**Files:**
- Modify: `docs/hardware/takes-measured.md`

- [ ] **Step 1:** Flash `reva_play_takes`. Put both decks on SAMPLER, record a take on each, wait for both `save … ok=1`, then power-cycle. Both play, and the `load` lines show `src=card` with the saved `seq`.
- [ ] **Step 2:** Overdub deck A, wait, power-cycle: the overdub is there.
- [ ] **Step 3:** SHIFT + REC A, wait for the `frames=0` save, then power-cycle: A is empty, B is not. `cb_max` around the clear stays at its level from before.
- [ ] **Step 4:** Record a long take on A, and cut power while its `save` is running, about 5 times at random points. After each boot, A loads the **previous** `seq` (never `src=empty` while a valid older slot exists). Record each pair.
- [ ] **Step 5:** Fill both decks to the full 42 s. Measure the load time of both, and the save time of one.
- [ ] **Step 6:** Turn pots during a save and during the boot load; the pots as in Task 9 Step 1, and Bastian's verdict on feel.
- [ ] **Step 7: Bad cards.**
  - Boot without a card: the board plays and logs `SHELL_TAKE mount fr=…`.
  - Then a card whose `A1/TAKE.WAV` header was overwritten on the PC: the slot is rejected and the other one loads. Bastian removes the card; Claude runs a scratchpad script against the drive letter with Bastian's go.
  - After each: `remote_run.py boot` reflashes without hands.
- [ ] **Step 8:** Copy `A0/TAKE.WAV` to the PC and open it in an audio editor: 48 kHz, float, stereo, the take audible, length = `frames`.
- [ ] **Step 9:** Persistence still works: `SHELL_PERSIST src=flash`. A QSPI save that falls due during a take save completes after it — via the persist stress image with `SHELL_TAKES=1`, if the combination builds; otherwise record why not.
- [ ] **Step 10:** Add the numbers to `docs/hardware/takes-measured.md`. Commit `docs(hardware): sampler takes on Rev A, by hand`.

### Task 11: Docs and the default

**Files:**
- Modify: `docs/roadmap.md` — an M6 entry; "Last updated".
- Modify: `docs/engine-map.md` — the watermark and the generation (measured facts only).
- Modify: `docs/gotchas.md` — anything Tasks 9–10 found that the source does not show.
- Modify: the spec — status line "built", plus a pointer to `takes-measured.md`.
- Modify: `shell/Makefile` default and memory `fireflow-reva-play-image-flags` — **only with Bastian's go**.

- [ ] **Step 1:** Write the docs from `takes-measured.md`; no number that the board did not print.
- [ ] **Step 2:** Ask Bastian whether `SHELL_TAKES` becomes 1 in the play image. If yes: change the default, update `check_images_build.py`'s play entries and the memory.
- [ ] **Step 3:** Ask about the REC lamp for "save pending" (spec §8); a yes is its own follow-up.
- [ ] **Step 4: Commit** `docs: sampler takes on SD`.

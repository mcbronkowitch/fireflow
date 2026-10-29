# CPU Hunt on Rev A (remote) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Find and land cheap CPU savings on Rev A across all engines, measured on the board by Claude alone after one setup visit by Bastian.

**Architecture:** Three layers. (1) A firmware listener (`SHELL_REMOTE`) on the Rev A play image: text commands over the existing USB CDC port, a bootloader entry that works while the callback is saturated, self-recovery on boot failures, workload loading through the shared control law, and an overrun guard. (2) A measurement switch (`SHELL_REMOTE_MEASURE`) on top: section timing, time outside the callback, late-block injection and runtime toggles for saving candidates. (3) Host tools: a desktop law-driven sweep that picks the workloads, and a runner that flashes, talks to the board, records the Twin and computes statistics. Then the measuring sessions, the candidate loop, and the landing.

**Tech Stack:** C++17 (engine, shell firmware via ARM GCC + make, desktop via clang + Ninja), doctest, Python 3 (pyserial 3.5 and numpy present; pytest absent — Python tests run as plain scripts under ctest), dfu-util, ffmpeg (dshow).

**Spec:** `docs/superpowers/specs/2026-10-29-cpu-hunt-remote-design.md` — read it first; § numbers below are its sections.

## Global Constraints

- Desktop: `source env.sh`, `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` (Release is mandatory), `cmake --build build`, then `ctest --test-dir build --output-on-failure -E "^reva_"` (no `hardware/` change in this plan). ctest does not build: always build first.
- Firmware: never `source env.sh` in that shell; `PATH="/c/Program Files/DaisyToolchain/bin:/c/Program Files/Git/usr/bin:$PATH"`; `make -C shell clean` then `make -C shell -j8 images <switches>` per image (make's 1-second mtime trap); `cmp` the new pair against the previous one before flashing.
- Every Rev A image of this round: `SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1 SHELL_REMOTE=1`. Measurement images add `SHELL_REMOTE_MEASURE=1`. Only the round's last image is a plain `SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1` image of `main` (§5.6).
- Flash always as a pair, QSPI first: `dfu-util -a 0 -s 0x90100000 -D shell/build/shell-qspi.bin`, then `dfu-util -a 0 -s 0x90040000:leave -D shell/build/shell-sram.bin`.
- The block is 96 samples = 960 000 DWT cycles; the click band is ~930 000–960 000; the target is worst block ≤ 880 000 (§3).
- Never `cd` in a shell command; no shell writes into the repo (memory `subagent-shell-auto-approval`).
- Everything written into the repo is English. Commit trailer: `Co-Authored-By: HAL 9000 <293417720+bea-ton-k@users.noreply.github.com>`.
- Every test that is added is shown RED once before it goes green.
- No runtime claim enters a doc until a probe or a board log printed it (CLAUDE.md, the probe rule).
- Work on branch `feat/cpu-hunt-remote`; never write into the repo while another agent implements there (memory `fireflow-parallel-implementer-etiquette`).

## Review Focus

1. **BOOT while the foreground is starved, the guard under saturation** — a `BOOT` line must reach the bootloader while the callback starves the foreground but still leaves gaps; the bootloader entry lives in the USB receive callback and calls no `Delay`. Under true saturation (the callback back to back) the USB interrupt is starved by the same NVIC priority tie and BOOT cannot arrive: there the overrun guard is the only way out, so it must trip on the back-to-back signature too and stop every injected burn once tripped. Pinned by `remote_wiring_guard` (Tasks 2, 5, 6 and the final fix wave), `tests/test_workload_run.cpp`, and the setup visit's guard and near-saturation checks (Task 7 Step 5).
2. **Commands torn across USB packets, CRLF, junk, overlong lines** — the assembler yields exactly the complete valid commands and discards an overlong line up to its newline. Pinned in `tests/test_remote_cmd.cpp` (Task 1).
3. **A pot drifting or touched during a run** — the run is marked void, not silently measured on a different patch. Pinned in `tests/test_workload_run.cpp` (Task 5).
4. **A toggle flipped mid-block** — the engine reads the toggle word once per `process()` call. Pinned by `remote_wiring_guard` (Task 6 extends it).
5. **COM port vanishing on BOOT or reset** — the runner reconnects and keeps its log continuous with host timestamps. Pinned in `tools/test_remote_lib.py` (Task 3).

---

### Task 0: Bastian's three answers (§9)

No code. Ask Bastian, record the answers in the spec's §9, commit the spec.

- [ ] **Step 1:** Ask the three §9 questions in one message: (1) buy a PC-switchable smart plug for the 12 V supply, yes/no (price it first: two-digit euros, memory `money-vs-bastians-time`); (2) cabling at the visit — both outputs to Twin inputs 1+2, Twin output to the board input or not, Twin at 48 kHz with input monitoring off for the round; (3) allow `dfu-util`, `ffmpeg` and the runner without prompts for this project.
- [ ] **Step 2:** If (3) is yes, add the allow rules through the `update-config` skill (project `.claude/settings.json`), nothing broader than those three commands.
- [ ] **Step 3:** Write the answers into spec §9 as *Bastian* decisions; commit `docs(spec): cpu hunt — Bastian's setup answers`.

---

### Task 1: Command protocol (pure, host-tested)

**Files:**
- Create: `shell/remote_cmd.h`, `shell/remote_cmd.cpp`
- Test: `tests/test_remote_cmd.cpp`
- Modify: `CMakeLists.txt` (add both `.cpp` files to `spky_tests`, beside `shell/boot_patch.cpp`)

**Interfaces:**
- Produces:
  - `enum class CmdKind : uint8_t { None, Ping, Boot, Home, Load, Gesture, Toggle, ResetMax, Late };`
  - `struct Command { CmdKind kind; int32_t a; int32_t b; };`
  - `bool parse_command_line(const char* line, int len, Command* out);`
  - `class LineAssembler { public: int feed(const uint8_t* buf, uint32_t len, Command* out, int max_out); };` — returns the number of commands written.
  - `class CmdRing { public: bool push(const Command& c); bool pop(Command* c); };` — single producer (USB ISR), single consumer (audio callback), capacity 8, lock-free with `volatile` indices.
- Syntax (ASCII, one command per line, `\n` ends a line, `\r` ignored, max 31 chars): `PING`, `BOOT`, `HOME` (back to the booted patch), `LOAD <id>`, `GEST <id>`, `TOG <bit 0..31> <0|1>`, `RST` (reset maxima), `LATE <kcycles 1..2000>`. Anything else → `parse_command_line` returns false.

- [ ] **Step 1: Write the failing tests** in `tests/test_remote_cmd.cpp`:
  - `"remote_cmd: every verb parses"` — each verb above to its kind; `LOAD 7` → a=7; `TOG 3 1` → a=3, b=1; `LATE 40` → a=40.
  - `"remote_cmd: out of range is rejected"` — `TOG 32 1`, `TOG 3 2`, `LATE 0`, `LATE 2001`, `LOAD -1`, `FOO`, empty line → false.
  - `"remote_cmd: torn packets"` — `"LO"`, `"AD 3\r"`, `"\nPI"`, `"NG\n"` fed in four calls yield exactly `Load(3)` then `Ping`.
  - `"remote_cmd: overlong line is discarded to its newline"` — 40 × `'A'` then `"\nPING\n"` yields exactly one `Ping`.
  - `"remote_cmd: ring keeps order and refuses the ninth"` — 8 pushes succeed, the 9th fails, pops come back in order.
- [ ] **Step 2:** Build and run `build/spky_tests -tc="remote_cmd*"`; expected: FAIL to compile (header missing).
- [ ] **Step 3:** Implement `shell/remote_cmd.{h,cpp}` with the interfaces above. No heap, no `<string>`; it must compile under `arm-none-eabi-g++` too.
- [ ] **Step 4:** Run `build/spky_tests -tc="remote_cmd*"`; expected: all pass. Run the whole suite; expected: green.
- [ ] **Step 5:** Commit `feat(shell): remote command protocol, host-tested`.

---

### Task 2: The listener image (`SHELL_REMOTE`): bootloader entry and self-recovery

**Files:**
- Create: `shell/remote_listen.h`, `shell/remote_listen.cpp`, `shell/write_shell_remote.py`, `shell/test_remote_wiring.py`
- Modify: `shell/Makefile` (switch `SHELL_REMOTE ?= 0`, generated header like `write_shell_persist.py`'s; `remote_cmd.cpp` and `remote_listen.cpp` into the shell's sources), `shell/main.cpp:289-298` (the ITCM-failure path), `shell/panel_scan.cpp:540` (`run_panel_scan_report`, right after `hw.StartLog(false)`), `CMakeLists.txt` (`add_test(NAME remote_wiring_guard ...)` as `read_coupon_guard` does), `shell/README.md` (switch table row)

**Interfaces:**
- Consumes: Task 1's `LineAssembler`, `CmdRing`, `Command`.
- Produces:
  - `void remote_listen_start();` — after `StartLog`: `daisy::UsbHandle{}.SetReceiveCallback(&on_rx, daisy::UsbHandle::FS_INTERNAL)` (UsbHandle is stateless; libDaisy's logger relies on the same fact).
  - `bool remote_poll(Command* c);` — the audio callback's once-per-block pop.
  - `[[noreturn]] void remote_enter_bootloader();` — `daisy::System::ResetToBootloader(DAISY_INFINITE_TIMEOUT)`, no `Delay`.
  - `void remote_install_fault_vectors();` — first thing in `main()` when `SHELL_REMOTE`.
- Behaviour: `on_rx` runs in the USB ISR. It feeds the assembler; `Boot` calls `remote_enter_bootloader()` right there, `Ping` is answered by the foreground (`SHELL_REMOTE pong`), and everything else is pushed into the ring. With `SHELL_REMOTE`, a failed `itcm_load()` calls `remote_enter_bootloader()` instead of blinking. `remote_install_fault_vectors()` copies the vector table into a 1 KB-aligned RAM array, sets `SCB->VTOR`, and points HardFault/MemManage/BusFault/UsageFault at a handler that calls `remote_enter_bootloader()`. First check `ResetToBootloader`'s body in `lib/libDaisy/src/sys/system.cpp` (~line 320) for any delay or blocking call; if it has one, write the backup-register flag and `NVIC_SystemReset()` directly, as it does. The boot print gains `SHELL_REMOTE on build=<git hash>`.

- [ ] **Step 1: Write the failing guard** `shell/test_remote_wiring.py` (plain script, exit code is the verdict). Asserts:
  - `remote_listen.cpp`'s `on_rx` body contains `remote_enter_bootloader` and no `Delay`;
  - `main.cpp`'s ITCM-failure branch calls `remote_enter_bootloader` under `SHELL_REMOTE`;
  - `remote_install_fault_vectors()` is the first statement of `main()` under `SHELL_REMOTE`;
  - `remote_listen_start()` follows `StartLog` in `run_panel_scan_report`.
- [ ] **Step 2:** Run `python shell/test_remote_wiring.py`; expected: FAIL (files missing).
- [ ] **Step 3:** Implement as above.
- [ ] **Step 4:** Run the guard; expected: PASS. Sabotage once (put `hw.Delay(1)` into `on_rx`), see it go RED, revert by editing back, never with `git checkout` (memory `fireflow-scripted-edits-crlf`).
- [ ] **Step 5:** Build the firmware: `make -C shell clean`, then `make -C shell -j8 images SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1 SHELL_REMOTE=1`. Expected: link OK, `itcm_placement.py` passes. Note `SRAM_EXEC` and `ITCM_HOT` free from the map, to be recorded in Task 8.
- [ ] **Step 6:** Desktop build and ctest; expected: green, `remote_wiring_guard` among the passes.
- [ ] **Step 7:** Commit `feat(shell): SHELL_REMOTE listener — USB commands, bootloader entry, fault recovery`.

---

### Task 3: Host runner and statistics

**Files:**
- Create: `tools/remote_lib.py`, `tools/remote_run.py`, `tools/run_stats.py`, `tools/test_remote_lib.py`
- Modify: `CMakeLists.txt` (`add_test(NAME remote_lib_guard ...)`, plain script)

**Interfaces:**
- Produces (`tools/remote_lib.py`):
  - `class Port` with injectable opener: `Port(name, opener=serial.Serial)`; `send(line: str)`; `lines() -> Iterator[tuple[float, str]]` — host `time.time()` stamps; reopens after `SerialException` or a vanished port, polling every 0.5 s for up to 60 s, and logs a `#reconnect` line.
  - `def flash_pair(qspi: Path, sram: Path, run=subprocess.run, wait_s=30) -> None` — waits until `dfu-util -l` lists the Daisy DFU device, then flashes QSPI first and SRAM with `:leave`; raises on a non-zero exit.
  - `def start_capture(out_wav: Path, device: str, seconds: float) -> subprocess.Popen` — ffmpeg dshow, 2 channels, 48 kHz, wall-clock start written beside it (`<wav>.start`). Device name and flags from memory `fireflow-audio-measurement-rig`.
- Produces (`tools/run_stats.py`):
  - `def summarize(cb: Sequence[int]) -> dict` — keys `n, p50, p99, p999, max, over_930k`.
  - `def aa_floor(a: dict, b: dict) -> dict` — absolute differences per key.
  - `def verdict(base: dict, cand: dict, floor: dict) -> str` — `"saves"`, `"costs"` or `"noise"`, by p99 and max against the floor.
- CLI (`tools/remote_run.py`): `flash QSPI SRAM [--expect-build STAMP]`, `boot [--expect-build STAMP]` (sends `BOOT`, then waits for DFU; refused while a begun pair is unfinished), `cmd LINE`, `run --load ID [--gest ID] [--expect-tog HEX] [--tog BIT=V ...] --seconds S --out DIR [--record]` (sets the board's toggle word before the LOAD and checks it on every counted frame; final fix wave) (writes `DIR/serial.txt` with host stamps, `DIR/audio.wav` when recording, `DIR/summary.json` from the frame lines). The COM port is found by USB VID/PID, not hard-coded.

- [ ] **Step 1: Write the failing tests** in `tools/test_remote_lib.py`:
  - `port reconnects` — a fake opener that raises once mid-stream; `lines()` resumes and emits `#reconnect`, with stamps monotonic.
  - `flash order` — a fake `run` records calls; QSPI comes before SRAM, SRAM carries `:leave`, and a failing QSPI step stops before SRAM.
  - `summarize` — a known list gives exact p50/p99/max/over_930k.
  - `verdict` — inside the floor gives `noise`; lower beyond the floor gives `saves`.
- [ ] **Step 2:** Run `python tools/test_remote_lib.py`; expected: FAIL (import error).
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Run; expected: PASS. Desktop ctest; expected: `remote_lib_guard` passes.
- [ ] **Step 5:** Commit `feat(tools): remote runner — flash, serial with host stamps, capture, stats`.

---

### Task 4: Desktop law-driven sweep and hot-path scan

**Files:**
- Create: `host/sweep/main.cpp`, `tools/pick_workloads.py`, `tools/hotpath_libm.py`, `tools/test_hotpath_libm.py`
- Modify: `CMakeLists.txt` (target `sweep`, linked like `spky_tests` against `spky_engine` plus `shell/controls.cpp`, `shell/boot_patch.cpp` and the control law; `add_test(NAME hotpath_libm_guard ...)`)

**Interfaces:**
- Consumes: `shell::apply_boot_patch_rows(const ControlTable&, const int*, int, float*)`, `control::ControlLaw` (as `tests/test_control_law.cpp` drives it), the panel's `ControlTable`.
- Produces:
  - `build/sweep.exe --pair <engA>,<engB> (--random N --seed S | --corners | --vector FILE) --seconds 100 --out CSV` — per vector, it ticks the law once per 96-sample block and times `process(..., 96)` with `steady_clock`. CSV columns: `vector_id, eng_a, eng_b, p50_ns, p99_ns, max_ns, silent_frac, rows_x1000...`. `silent_frac` is the fraction of blocks whose output peak is below −60 dBFS.
  - `tools/pick_workloads.py CSV... --top 5 --out shell/workloads.json` — per pairing the top 5 by `max_ns` with `silent_frac < 0.5`, plus every corner vector; the gestures of spec §5.2 are added by hand.
  - `tools/hotpath_libm.py ELF` — runs `arm-none-eabi-objdump -d`, lists every `bl` to a libm symbol (`powf, expf, logf, sinf, cosf, tanhf, pow, exp, log, sin, cos, fmodf`) inside functions placed in `.itcm_hot` or called from `AudioCallback`, and prints `function → symbol × count`.
- Pairings (§5.1): each engine on both decks; each engine against SYNTH and against WAVE (Bastian's other decks); Bastian's patch vector (from his boot print `SHELL_PLAY_V` frame, captured in Task 7).
- Corners: for each deck, all of that deck's pots at 0 and then at 1000, with the other deck at noon.

- [ ] **Step 1: Write the failing test** `tools/test_hotpath_libm.py` against a small fixture disassembly text in the test file: two hot functions, one calling `powf` twice, one cold function calling `expf`. Expected output: `hot_a → powf × 2` only.
- [ ] **Step 2:** Run it; expected: FAIL.
- [ ] **Step 3:** Implement `hotpath_libm.py`, then `sweep` and `pick_workloads.py`.
- [ ] **Step 4:** Run the test; expected: PASS. Build `sweep`; run `build/sweep.exe --pair SYNTH,WAVE --corners --seconds 10 --out <scratch>/smoke.csv`; expected: one row per corner vector with nonzero `p99_ns`.
- [ ] **Step 5:** Run the full sweep (all pairings, `--random 200` plus `--corners`, 100 s each) in the background; write `shell/workloads.json` with `pick_workloads.py`, then add the gesture entries (ENGINE turn, TIDE/PULL/MORPH sweep, deck swap onto BBD, reverb clear, MOD clear) as row/value sequences at a fixed rate.
- [ ] **Step 6:** Run `hotpath_libm.py` on the Task 2 ELF; save its output beside the sweep results in the scratchpad for Task 9.
- [ ] **Step 7:** Commit `feat(host): law-driven sweep, workload picker, hot-path libm scan` (with `shell/workloads.json`).

---

### Task 5: Workloads, gestures and the overrun guard on the board

**Files:**
- Create: `shell/gen_workloads.py` → `shell/generated_workloads.h`, `shell/test_gen_workloads.py`, `shell/workload_run.h`, `shell/workload_run.cpp`, `tests/test_workload_run.cpp`
- Modify: `shell/panel_scan.cpp` (`panel_scan_tick`: poll the ring once per block and apply the commands), `shell/panel_scan.h`, `shell/main.cpp` (the overrun guard in `AudioCallback` under `SHELL_REMOTE`), the persistence save trigger (`shell/persist_policy.cpp` call site in `panel_scan.cpp`), `CMakeLists.txt`, `shell/README.md`

**Interfaces:**
- Consumes: Task 1's `Command`, Task 2's `remote_poll`, `apply_boot_patch_rows`, `control_rearm_after_take()`, `shell/workloads.json` from Task 4.
- Produces:
  - `class OverrunGuard { public: OverrunGuard(uint32_t limit, uint32_t n); bool block(uint32_t cb); bool tripped() const; void clear(); };` — limit 950 000, n 8. Once tripped, the callback writes silence and skips `process()` until `clear()`. `LOAD`, `HOME` and `RST` clear it.
  - `class WorkloadRun { public: void start(int id, uint32_t now_block); void on_pot_take(); bool counting(uint32_t now_block) const; bool void_run() const; };` — `counting` is false for the first 500 blocks after `start` (settle); `on_pot_take` during a run sets `void_run`.
  - `void panel_scan_load_knobs(const float* knobs);` — writes the knob vector, re-bases pot ownership so a pot must move `kPotTouch` from its current reading to take over, then calls `control_rearm_after_take()`.
  - Generated `struct WorkloadDef { const char* name; const int* rows_x1000; int n; };` `struct GestureStep { uint32_t block; int16_t row; int16_t value_x1000; };` plus `workload_by_id(int)` and `gesture_by_id(int, int* n)`.
  - Serial: `SHELL_REMOTE load id=<id> name=<name>`, `SHELL_REMOTE run id=<id> counting=<0|1> void=<0|1>` on every report pass, `SHELL_REMOTE guard tripped blk=<n>`, `SHELL_PERSIST save suppressed` in place of a save.
- Determinism: before writing code, search `engine/instrument.h` for a transport reset and a seed setter. If both exist, `LOAD` calls them. If not, do **not** add engine API in this task: record that runs are not seed-identical and rely on the A/A floor (Task 8).

- [ ] **Step 1: Write the failing tests** in `tests/test_workload_run.cpp`:
  - `"overrun guard trips on the 8th consecutive block"` — 7 × 951 000 → not tripped; one 900 000 resets the streak; 8 × 951 000 → tripped.
  - `"workload run settles 500 blocks"` — `counting(start+499)` false, `counting(start+500)` true.
  - `"pot take voids the run"` — `on_pot_take()` after start gives `void_run()` true; a new `start` clears it.
  
  And `shell/test_gen_workloads.py`: the generator rejects a vector whose length differs from the table's row count, and its output round-trips the JSON's values.
- [ ] **Step 2:** Run both; expected: FAIL.
- [ ] **Step 3:** Implement. The guard check reads the cycles `panel_scan_block_end` already computes; gesture steps are applied in `panel_scan_tick` at their block offset through the same knob path.
- [ ] **Step 4:** Run both; expected: PASS. Desktop suite green. Firmware build with `SHELL_REMOTE=1` links.
- [ ] **Step 5:** Commit `feat(shell): remote workloads, gestures, overrun guard, persistence held`.

---

### Task 6: The measurement switch (`SHELL_REMOTE_MEASURE`)

**Files:**
- Create: `engine/util/probe_toggles.h`
- Modify: `shell/Makefile` (switch `SHELL_REMOTE_MEASURE ?= 0`; it implies `-DSPKY_PROBE_MARKS=1 -DSPKY_PROBE_TOGGLES=1` and requires `SHELL_REMOTE=1`), `engine/instrument.h/.cpp` (read the toggle word once at the top of `process()` into a member), `shell/panel_scan.cpp` (section stamps; the foreground's outside-window loop; `LATE`; `TOG`; `RST`), `shell/cb_profile.cpp` (shell sections in `CBP_WORST`), `shell/test_remote_wiring.py`, `shell/README.md`

**Interfaces:**
- Produces:
  - `namespace spky::probe { uint32_t toggles(); void set_toggles(uint32_t); }` — under `SPKY_PROBE_TOGGLES`, otherwise `constexpr uint32_t toggles() { return 0; }`, so default and VCV builds compile every candidate branch away. Each candidate claims one bit, documented in this header (bit, name, file).
  - Section stamps per block: `scan`, `law`, `led`, `gate`, `persist` (shell) plus `cb_profile`'s `a`, `b`, `mix`. Per report frame: `SHELL_SEC worst=<cb> scan= law= led= gate= persist= a= b= mix=` — the worst block's own breakdown — and `SHELL_SECMAX` with per-section maxima.
  - Outside-window: under `SHELL_REMOTE_MEASURE`, the report loop's `hw.Delay(500)` (`panel_scan.cpp:860`) becomes a DWT busy-loop of the same length. It adds up every jump of more than 64 cycles between two consecutive reads as interrupt time and prints `SHELL_OUT busy_max=<cycles/block> busy_p50= out_max=` (busy minus `cb` of the same block). Known side effect (`docs/gotchas.md`, idle fill): a busy foreground changes the block-rate noise floor, so this image's recordings are not compared by level against plain images.
  - `LATE k` makes the next callback burn `k × 1000` cycles before `process()`. `TOG b v` sets the bit. `RST` clears the maxima, and also clears them 200 blocks after any `TOG`.
  - Two toggle bits are reserved for the shell, in the measurement image only: bit 31 disables the overrun guard, and bit 30 repeats the last `LATE` on every block. Together they saturate the callback on purpose for Task 7's guard and near-saturation tests (Step 5). Candidates use bits 0–29.

- [ ] **Step 1: Extend `shell/test_remote_wiring.py`** (failing first): `probe::toggles()` is called exactly once in `engine/instrument.cpp`, inside `Instrument::process` before its sample loop; no other `engine/` file calls it.
- [ ] **Step 2:** Run it; expected: FAIL.
- [ ] **Step 3:** Implement.
- [ ] **Step 4:** Guard PASS. Desktop suite green, render hashes unchanged (toggles compile to 0). Firmware: `make -C shell clean`, then `make -C shell -j8 images SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1 SHELL_REMOTE=1 SHELL_REMOTE_MEASURE=1` links, and `itcm_placement.py` passes.
- [ ] **Step 5:** Commit `feat(shell): SHELL_REMOTE_MEASURE — sections, outside-window, late inject, toggles`.

---

### Task 7: The setup visit (Bastian at the board, once)

Bastian's session. Claude drives; every result goes into a new `docs/hardware/cpu-hunt-measured.md` §1.

- [ ] **Step 1:** Build the measurement image (Task 6 switches) and `cmp` it against any earlier pair. Bastian flashes it by hand (BOOT + RESET, then the pair commands under Global Constraints).
- [ ] **Step 2:** Bastian patches the cables as answered in Task 0.
- [ ] **Step 3:** `python tools/remote_run.py cmd PING` → expected `SHELL_REMOTE pong`. Capture 20 s of frames and save Bastian's patch vector from `SHELL_PLAY_V` into `shell/workloads.json` as `bastian`.
- [ ] **Step 4: Remote flash rehearsal.** `remote_run.py boot --expect-build <hash>` → the DFU device appears within 30 s → `remote_run.py flash QSPI SRAM --expect-build <hash>` with the same pair → `SHELL_REMOTE on build=<hash>` comes back (the runner confirms it). Runner rule from here on: a failed flash step is retried as a whole pair (`flash_pair()` does it); while a begun pair is unfinished, nothing boots or resets the board (`boot` refuses). Bastian stays until this has passed.
- [ ] **Step 5: Overrun guard, then near-saturated BOOT.** Under true saturation BOOT cannot arrive (the USB interrupt is starved by the same priority tie, spec §5.3/§8), so this step tests the guard there and BOOT only where the callback still leaves gaps.
  - **Before 5a:** note `SHELL_PLAY_WIN gap=` over a few quiet frames — the entry-to-entry period, which the guard's period threshold (965 000, `shell/workload_run.h`) assumes is 960 000 plus jitter well under 5 000. Record it in §1; if it is not, the threshold moves before anything else runs.
  - **5a — the guard under a full burn.** `TOG 30 1`, then `LATE 1000` (every block burns 1 000 000 cycles, over the 960 000 period). Expected: `SHELL_REMOTE guard tripped blk= by=cb` within 8 blocks, the output goes silent, and the log keeps coming (the burn stops once tripped). Then `TOG 30 0` **before** `RST` → audio back. (`RST` alone would re-trip 8 blocks later: bit 30 repeats the burn.)
  - **5b — BOOT near saturation.** Read the current `cb_last` from `SHELL_PLAY_LAW`, then size the burn so the callback stays just under the period: k = (950 000 − cb_last) / 1000 − margin (a margin of ~10 to start). `TOG 31 1` (guard off), `TOG 30 1`, `LATE k`. Expected: the serial log stalls (foreground starved) and `SHELL_PLAY_WIN gap=` (if a frame gets out) sits near 960 000. Send `BOOT`; expected: the DFU device appears within 30 s. If the log does not stall, raise k by a few and repeat; if the board stops answering altogether (no DFU), it went back to back: that is 5c, and it needs hands.
  - **5c (optional, last, only with Bastian present and only if he wants it) — full saturation with the guard off.** `TOG 31 1`, `TOG 30 1`, `LATE 1000`. Expected outcome: the board wedges — no log, no BOOT — and needs BOOT + RESET by hand. It confirms the documented limit; it tests nothing the runner relies on.
  - Re-flash remotely.
- [ ] **Step 6: Self-recovery.** Flash a deliberately mismatched pair: this round's SRAM over a QSPI image of the previous commit. Expected: no 5 Hz blink; the board enters DFU by itself. Re-flash the right pair remotely. Note: the SRAM image first runs the other build's cold QSPI code in `board_init()` before the stamp check (spec §8); a pass here shows that pairing recovers, not every pairing.
- [ ] **Step 6b (optional): does the IWDG survive a reset into the bootloader?** With Bastian present: a probe image that arms the IWDG (a long timeout, ~10 s) and then sends itself into the bootloader (`BOOT`). Expected either way, record which: the board leaves DFU by itself after the timeout (the IWDG survives: it would cover a hang in foreign cold code, but would also cut every DFU session short) or stays in DFU (it does not). Only a recorded result lets spec §5.3's optional IWDG be relied on.
- [ ] **Step 7 (only if the plug was bought):** power-cycle by plug, and run `dfu-util` in a wait loop during the bootloader's ~3 s window without BOOT pressed. Record whether DFU is accepted.
- [ ] **Step 8:** Record the results and commit `docs(hardware): cpu hunt setup visit`. From here on Bastian is not needed at the board.

---

### Task 8: Baseline, A/A floor, outside-window, and the clicks

Claude alone, measurement image. Results go into `cpu-hunt-measured.md` §2–§3.

- [ ] **Step 1:** Re-measure §5.0: `SRAM_EXEC` and `ITCM_HOT` free from the map; Bastian's patch for 10 min (`run --load bastian --seconds 600 --record`); the section split from `SHELL_SEC`.
- [ ] **Step 2: A/A.** Run `bastian` twice for 300 s each, then compute the floor with `aa_floor`. Repeat the A/A on the heaviest corner workload. Stop criterion for decks without a pattern: the shortest duration at which two A/A runs' p99.9 agree within the floor.
- [ ] **Step 3: Outside the callback.** Read `SHELL_OUT` for `bastian` on the measurement image. Remotely flash a plain listener image (`SHELL_REMOTE=1`, no measure), run `bastian` for 300 s, and compare `cb_max`/`SHELL_PLAY_WIN`. That gives the listener-vs-plain and measure-vs-listener offsets (§5.3, "Which image judges what").
- [ ] **Step 4: Clicks (§5.5).** Back on the measurement image: inject `LATE` at host-known times (10 injections, ≥ 5 s apart, sizes 100/200/400 kcycles), record both channels, and run `tools/click_detect.py` on the recording. This describes a real late block on Rev A (shape, channels, score) and measures the Twin's clock drift from the injection times. Then a 10 min `bastian` run with recording. Compare each detected click's shape and host-time alignment against the injected ones, and against a constant-load reference run (the lightest corner workload, 300 s).
- [ ] **Step 5:** Write the verdict: are the 2026-10-28 clicks late blocks, yes, no, or undecided with the reason. Commit `docs(hardware): cpu hunt baseline, A/A floor, clicks verdict`.

---

### Task 9: Engine worst cases and the candidate list

- [ ] **Step 1:** For each workload in `shell/workloads.json` (static vectors and gestures), run it on the board for its stop criterion and write `summary.json`. Runs with `void=1` are repeated.
- [ ] **Step 2:** Rank by worst block. Compute the rank correlation between desktop `max_ns` and board `max` per pairing, and record it. If it is below 0.5 for a pairing, add 20 more random vectors of that pairing on the board.
- [ ] **Step 3:** Build the candidate list. Sources: the worst blocks' section breakdowns, `hotpath_libm.py`, setters that recompute unchanged values (grep the setters the law calls per block for work done before an unchanged-value check), the shell's sections (scan/led/gate/persist), the `-O3` exceptions (`control_tick`, `mux_plan`), and `-ffast-math` as a class-2 build-switch candidate. For each: expected cycles, class (§4), the workloads it applies to, toggle or build switch.
- [ ] **Step 4:** Write the list into `cpu-hunt-measured.md` §4. Commit `docs(hardware): cpu hunt — worst cases per engine and the candidate list`.

---

### Task 10: The candidate loop (repeat per candidate, highest expected saving first)

Per candidate, its own commit series. Stop when every worst case is ≤ 880 000 or the list is exhausted.

- [ ] **Step 1: Equivalence test first.**
  - Class 1: a doctest showing the cheaper path gives the same output as today's path, within float noise, on a short render of the affected engine, with toggle on vs off. Shown RED once by breaking the new path.
  - Class 2: a desktop render pair (toggle off/on) of the workloads it applies to, written to the scratchpad for the listening batch.
- [ ] **Step 2: Implement** behind its toggle bit (or build switch), the bit documented in `probe_toggles.h`.
- [ ] **Step 3:** Desktop suite green (render hashes unchanged with toggles compiled out). `test_block_size_invariance` green. If the saving is in `engine/`, run a 44.1 kHz render to check behaviour holds at Rack-like rates.
- [ ] **Step 4:** Flash the new measurement image remotely (`boot`, `flash`, confirm `build=<hash>`). On each workload it applies to, run baseline and candidate interleaved (base, cand, base, cand) and compute `verdict` against the A/A floor.
- [ ] **Step 5:** Record the verdict in `cpu-hunt-measured.md` §5. Then:
  - Class 1 and `saves`: the default becomes ON (the toggle path becomes the only path, the toggle bit retires).
  - Class 2 and `saves`: queue it for Task 11.
  - Otherwise: revert the candidate.
- [ ] **Step 6:** Commit per candidate: `perf(<area>): <candidate> — <cycles> saved on <workload>`.

---

### Task 11: The listening (and looking) batch with Bastian

- [ ] **Step 1:** Prepare A/B pairs of every queued class-2 candidate:
  - board recordings of the workloads it affects, toggle off/on, level-matched, randomized as A/B with the key kept in the scratchpad;
  - panel-side class-2 candidates as a short description of what changes on the panel, to look at on the board.
- [ ] **Step 2:** Send the batch to Bastian; record each decision verbatim as *Bastian* in `cpu-hunt-measured.md` §6.
- [ ] **Step 3:** Accepted ones become default in the firmware build. Shared or firmware-only, whichever is simpler, per spec §4 VCV rule. Rejected ones are reverted. Commit.

---

### Task 12: Landing and the end of the round

- [ ] **Step 1:** Full desktop suite green in Release. Firmware images build. If anything in `engine/` or `host/vcv/` changed: `host/vcv/build-local.sh install`, then tell Bastian to restart Rack.
- [ ] **Step 2:** Re-measure every engine worst case and `bastian` on a final listener image of the branch. Record the table: worst block before → after, per workload.
- [ ] **Step 3:** Review (superpowers:requesting-code-review), then ask Bastian for the merge go-ahead. Merge to `main`.
- [ ] **Step 4:** Remotely flash the plain play image of `main` (`SHELL_PANEL_SCAN=1 SHELL_KEYS_NC=1`, no listener): `boot`, then `flash`. Confirm its boot print over USB. Run `bastian` for 10 min with recording and record the shipped number (`cb_max`, `SHELL_PLAY_WIN`, clicks).
- [ ] **Step 5:** Update `docs/engine-map.md` with the measured facts the next session needs, `docs/roadmap.md` with the status, and `docs/gotchas.md` with any new trap. Commit `docs: cpu hunt on Rev A — results`.
- [ ] **Step 6:** Tell Bastian the board now runs plain `main` and will need BOOT + RESET for the next flash.

# Coupon wait-sweep probe — implementation plan

**Spec:** [`2026-09-27-coupon-wait-sweep-probe-design.md`](../specs/2026-09-27-coupon-wait-sweep-probe-design.md)
**Scope of this plan:** everything up to a flashable image and a reader with
a guard. The board session — flash, capture, write-up — is Bastian's and is
not a task here.

Four tasks, in order. Each ends with its own verification, run by the
controller, not reported by an implementer (memory
`subagent-test-claims-are-not-evidence`).

## Task 1 — `shell/wait_plan.{h,cpp}` and the host test

Pure data, no HAL, host-compilable, like `tone_plan`:

- `kWaitGridUs[15]` = 0, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000,
  10000, 20000, 50000.
- `enum class WaitArm { Wait = 0, Discard = 1, Long = 2, Codec = 3 }`,
  `kWaitArmCount = 4`.
- `kWaitCodecGridUs[3]` = 200, 1000, 10000, and `wait_codec_grid_index(k)`
  returning each one's index in the main grid (or -1).
- `kWaitRepeats = kRepeats` (64), `kWaitLongRung` = the 387.5-cycle ladder
  index.
- `constexpr uint32_t us_to_cycles(uint32_t us)` in 64-bit arithmetic —
  never through `ns_to_cycles()`, which wraps above ~8.9 ms.
- `wait_grid_order(k, n, sweep_dir)` — the index walked at step `k`.
- `wait_block_estimate_ms()` — the §6 estimate, for a bound test.

`tests/test_wait_plan.cpp`, registered in `CMakeLists.txt` beside
`test_tone_plan.cpp`. Every assertion in spec §8's first bullet. **RED
proof:** temporarily compute the 50 000 µs conversion through
`ns_to_cycles(us * 1000)` and watch the exactness test fail; restore.

Verify: `cmake --build build && ctest --test-dir build -R wait_plan
--output-on-failure`, Release build (CLAUDE.md), and the full `ctest` once.

## Task 2 — the firmware

- `shell/wait_probe.{h,cpp}`: `run_wait_probe(hw)`, never returns. Boot:
  `cycles_init`, `probe_adc::init`, `StartLog(false)`, chain init, warm-up and
  clock pass on `R_SP10` (round two's choice). Per block: CFG, CLK, CAL (the
  latency pass copied from `tone_probe.cpp`), arms A, B, L with the codec
  stopped, arm C with `StartAudio(zero callback)` and G7 accounting, then
  `StopAudio`, SPAN and G5, GATES, HEALTH, END. Sweep direction alternates by
  block.
- `measure_wait_point(W, discard)`: prime, idle from the prime's end with
  interrupts enabled, optional discard, measured `sample_now(&span)` with the
  timeout excluded.
- `shell/write_shell_wait_probe.py`, `SHELL_WAIT_PROBE ?= 0` in
  `shell/Makefile` with the same exclusivity checks the tone switch carries
  (needs `SHELL_COUPON_PROBE=1`; exclusive with settle, xtalk and tone),
  `wait_probe.cpp`/`wait_plan.cpp` in `CPP_SOURCES`, `wait_probe.o` in
  `SWITCH_OBJECTS` and in the git-stamp list, dispatch in `main.cpp`.

Verify: from a shell **without** `env.sh`, build both the default image and
`SHELL_COUPON_PROBE=1 SHELL_WAIT_PROBE=1`; `cmp` that the default image is
unchanged in size class and that the probe image differs (memory
`fireflow-bench-stale-object-trap`); record SRAM_EXEC usage.

## Task 3 — `shell/read_wait.py` and its guard

Parse by `(tag, case)`, refuse an incomplete block, compute the shift table
per arm, G9 host-side, print round two's four-capture bridge values beside
arm C's three points, exit 1 on `gates_ok=0` or G9 fail.
`shell/test_read_wait.py`, CTest `read_wait_guard`, with G9's four boundary
fixtures and the red proof that a boundary moved by one fails the guard.

Verify: `ctest -R read_wait_guard`.

## Task 4 — docs

Spec status line; `docs/roadmap.md` M6 entry naming the image as built and
unmeasured; the flash-and-capture commands in the spec or the reader's
docstring.

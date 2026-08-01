# Task 4 report: injected stereo tape FLUX

## Outcome

FLUX now owns two caller-backed `TapeEcho<262144>` lines, preserves independent stereo input, and uses its existing SoftSwitch, wet-level law, feedback control, synced RATE ladder, LINK/THIN behavior, and one shared 30 ms delay-time slew. `FXT_FLUX_TIME` maps x1/4..x4 through the prebuilt tape LUT. The BBD-only DRIVE/STAGES surface and FLUX clock/stage/drive/feedback observers are gone.

`FxMem::echo` is now `[PART_COUNT][2]`. Render, benchmark, and audition hosts allocate the resulting 4 MiB tape arena statically/in SDRAM; VCV allocates it as `std::vector<float> echoMem[PART_COUNT][2]` and binds it before `Instrument::init`. Expanded desktop-test tape fixtures are heap-backed.

## TDD evidence

- RED: stereo/time/null-buffer contracts failed to compile against the old two-argument `Flux::init` and missing delay observers.
- RED: VCV source contract reported missing heap-backed stereo storage and the retained by-value arena.
- RED: benchmark protocol contract reported 15 rows and the four retired STAGES rows before the runner was changed.
- RED: audition routing contract failed before `apply_engine_stages` existed.
- GREEN: focused FLUX suite passed 25 cases / 172057 assertions; audition routing passed 1 case / 2 assertions.
- During GREEN, a fixture crash was traced to `vector<float>{Flux::kMaxSamples}` constructing one element; the fixture was corrected to sized construction and the formerly crashing case passed.

## Interface and host changes

- Added `kTapeSamples = 262144`; `Flux::kMaxSamples` aliases it.
- Added stereo `Flux::init`, `PartFx::init`, `Part::init`, `Instrument::init`, and exact `FxMem::echo[PART_COUNT][2]` forwarding.
- Feedback clamps to 0..1, scales by 1.2, and reaches both tape lines. Either missing tape pointer leaves FLUX disengaged.
- Removed FLUX/PartFx/Instrument DRIVE and STAGES methods and retired scenario dispatch actions. VCV parameter IDs remain append-only: DRIVE is saved state with no FLUX destination; STAGES maps to `LANE_PITCH` only for `ENGINE_BBD`.
- Audition now resolves the engine before applying the same engine-specific STAGES routing.

## Benchmark dispositions

- Kept the five `sweep_flux_rate_*` rows; each now asserts/folds its achieved tape delay target. A desktop contract proves the five targets strictly decrease.
- Deleted all four false `sweep_stages_*` rows from C++, protocol, and tests.
- Kept `sweep_grit_no_bbd_mem` as the null-stereo-tape PartFx/storage guard comparison, without historical BBD/libm claims.
- Kept the `sweep_flux_lines_2ch` protocol name as a documented historical raw-BBD row, directly using the dedicated BBD pair at 8192 stages / 8192 Hz and never the tape arena.
- Renamed the matched Part helper to `configure_worst_tape_flux`; retained shortest RATE and feedback 0.9. `FxFluxHotGroup` now stores/asserts/folds only the tape delay target.
- Legacy system pair retains hot RATE/feedback until Task 7; DRIVE/STAGES and stale FLUX-BBD meaning were removed.

## Verification

- `cmake --build build -j 8`: PASS.
- `ctest --test-dir build --output-on-failure`: PASS, 4/4 tests.
- `python -m unittest bench.test_run_contract`: PASS, 74 tests.
- `python host/vcv/res/test_panel.py`: PASS.
- VCV `./build-local.sh`: PASS after the local wrapper was made to pass its configured `RACK_DIR` as a make argument (wrapper remains ignored and is not committed).
- Render smoke: PASS; WAV 11,520,044 bytes, CSV 16,048,846 bytes.
- Audition firmware `make -j2`: PASS; SDRAM 14,510,628 bytes (21.62% of 64 MiB).
- Embedded `system sweep instr` benchmark build: PASS; SDRAM 45,101,420 bytes (67.21%).
- Exact retired-call guard: only `Limiter::set_drive` receivers remain; no FLUX/PartFx/Instrument calls or retired observers remain.
- `git diff --check`: PASS.

## Storage

`llvm-size build/spky_tests.exe`:

- Before: data 1,615,041 + bss 0 = 1,615,041 bytes.
- After: data 1,588,025 + bss 0 = 1,588,025 bytes.
- Delta: -27,016 bytes, safely below the +1 MiB acceptance ceiling and confirming no expanded static test fixture leaked into the executable.

## Self-review and concerns

Reviewed the public API removal, all echo indexing/positional callers, stereo/null/off behavior, benchmark row set, VCV heap binding and preserved parameter IDs, audition routing, and forbidden FLUX storage/type searches. No Task 5+ behavior was implemented; DRAG remains intentionally transitional. The only non-Task warning observed was the pre-existing VCV compiler warning in `phrase_gen.h`; it did not fail the build.

## Fix Round 1

### Review fixes

- Replaced the vacuous stereo assertion with two delayed-wet observations. The dry impulse at sample zero is excluded; left-to-right and right-to-left isolation are both asserted.
- Replaced the direct `apply_engine_stages` test with a complete generated-ParamId snapshot dispatched through `apply_init_patch`. A controlled copy selects BBD on A, Sampler on B, and distinct `STAGES_A/B` values, proving engine decoding, saved ID/value selection, BBD-only routing, and the real dispatcher call. The helper is now private to the dispatcher.
- Restored the `871d98c` scheduler contracts, adapted only for stereo heap tape memory and the retired BBD surface: intermediate-DRAG RATE response, THIN phase preservation across DRAG-usability changes, direct THIN-to-DRAG gate recovery, and RATE/BPM thinning-pattern re-derivation.
- Rewrote the legacy `instrument_worst_bbd*` comments and generated verdict prose as the truthful transitional hot stereo tape-FLUX setup. Protocol names remain stable; Task 7 still owns repointing both rows to the BBD engine.

### Mutation RED/GREEN evidence

- Stereo RED command: `cmake --build build -j 8; build/spky_tests.exe --test-case="flux tape: stereo input remains stereo"` with `_echo_l.Process(...)` temporarily replaced by `0.f`.
  - Result: expected failure, 1 case / 4 assertions, `delayed_l` was `0` instead of `> 1e-6`.
- Stereo GREEN, same focused command after restoring the production return:
  - Result: PASS, 1 case / 4 assertions.
- Dispatcher test-first RED: `cmake --build build -j 8` before the complete-snapshot overload existed.
  - Result: expected compile failure: `apply_init_patch` received two arguments but only the one-argument generated-snapshot wrapper existed.
- Dispatcher mutation RED command: focused dispatcher test with the internal `apply_engine_stages(...)` call temporarily bypassed.
  - Result: expected failure, BBD deck A reported pitch `0.5` instead of saved `STAGES_A == 0.8125`.
- Dispatcher GREEN after restoring the call:
  - Result: PASS, 1 case / 4 assertions; BBD A receives 0.8125, non-BBD B retains 0.5, and both generated engine IDs land.
- Verdict-prose RED command: `python -m unittest bench.test_run_contract.ProfileContract.test_o2_verdict_uses_worst_transitional_tape_flux_repeat` before `bench/run.py` was updated.
  - Result: expected failure because the report still emitted `DTCM+BBD` / `BBD echo` prose.
- Verdict-prose GREEN command: both transitional verdict tests.
  - Result: PASS, 2 tests.

### Fresh final verification

- `cmake --build build -j 8` — PASS (`ninja: no work to do`).
- Focused stereo, generated-snapshot dispatcher, and five scheduler filters — PASS, 7/7 cases; assertion counts `4, 4, 2, 1, 2, 2, 2`.
- `python -m unittest bench.test_run_contract` — PASS, 74 tests.
- `ctest --test-dir build --output-on-failure` — PASS, 4/4 tests, 0 failures.
- `git diff --check` — PASS before staging.

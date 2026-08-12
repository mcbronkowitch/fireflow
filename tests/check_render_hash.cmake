# Render one scenario and compare the WAV's SHA-256 against a reference.
#
# The reference hashes are NOT in this file -- they are passed in as EXPECTED
# from the add_test() calls in the top-level CMakeLists.txt (ctrl_identity,
# wave_formant_sweep). To re-baseline after a change that legitimately moves a
# render (CLAUDE.md: renders are sanity checks, not checksums):
#
#   1. Build Release, run the scenario by hand, and take the hash:
#        ./build/render.exe host/render/scenarios/<name>.json out.wav out.csv
#        sha256sum out.wav
#      The failing ctest also prints it as the "got" value; the two must agree,
#      which is the cheapest proof the new value is reproducible and not a
#      one-off from a stale build.
#   2. Replace EXPECTED in CMakeLists.txt and write a comment above it saying
#      WHICH change moved it and why that change had to move it. Every past
#      re-baseline there carries one; a hash bumped without a reason is
#      indistinguishable from a regression that was silenced.
#   3. Re-run the full suite: only the hashes the change actually reaches may
#      move. An unexplained one is a finding, not a baseline.
if(NOT DEFINED RENDER OR NOT DEFINED SCENARIO OR NOT DEFINED EXPECTED OR NOT DEFINED OUT_DIR)
    message(FATAL_ERROR "RENDER, SCENARIO, EXPECTED, and OUT_DIR are required")
endif()

if(NOT DEFINED GATE_STEM)
    set(GATE_STEM "ctrl_identity")
endif()
if(NOT DEFINED REFERENCE)
    set(REFERENCE "SYNTH")
endif()

set(WAV "${OUT_DIR}/${GATE_STEM}_gate.wav")
set(CSV "${OUT_DIR}/${GATE_STEM}_gate.csv")
execute_process(
    COMMAND "${RENDER}" "${SCENARIO}" "${WAV}" "${CSV}"
    RESULT_VARIABLE render_result
    OUTPUT_VARIABLE render_stdout
    ERROR_VARIABLE render_stderr
)
if(NOT render_result EQUAL 0)
    message(FATAL_ERROR "render failed: ${render_stdout}\n${render_stderr}")
endif()
file(SHA256 "${WAV}" actual)
file(REMOVE "${WAV}" "${CSV}")
if(NOT actual STREQUAL EXPECTED)
    message(FATAL_ERROR "${REFERENCE} reference moved: expected ${EXPECTED}, got ${actual}")
endif()

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

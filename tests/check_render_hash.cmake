if(NOT DEFINED RENDER OR NOT DEFINED SCENARIO OR NOT DEFINED EXPECTED OR NOT DEFINED OUT_DIR)
    message(FATAL_ERROR "RENDER, SCENARIO, EXPECTED, and OUT_DIR are required")
endif()

set(WAV "${OUT_DIR}/ctrl_identity_gate.wav")
set(CSV "${OUT_DIR}/ctrl_identity_gate.csv")
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
    message(FATAL_ERROR "SYNTH reference moved: expected ${EXPECTED}, got ${actual}")
endif()

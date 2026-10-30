# Runs the two reverb_dump variants and requires identical output (see
# tests/reverb_dump.cpp). Arguments: -DDUMP_A=<exe> -DDUMP_B=<exe> -DOUT_DIR=<dir>
file(MAKE_DIRECTORY "${OUT_DIR}")
execute_process(COMMAND "${DUMP_A}" "${OUT_DIR}/dispatch.bin" RESULT_VARIABLE ra)
execute_process(COMMAND "${DUMP_B}" "${OUT_DIR}/only.bin" RESULT_VARIABLE rb)
if(NOT ra EQUAL 0)
  message(FATAL_ERROR "reverb_dump_dispatch failed (${ra})")
endif()
if(NOT rb EQUAL 0)
  message(FATAL_ERROR "reverb_dump_only failed (${rb})")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${OUT_DIR}/dispatch.bin" "${OUT_DIR}/only.bin" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the SHELL_REVERB_HALF=1 variant (SPKY_REVERB_HALF_ONLY) renders differently from the tested dispatch variant")
endif()

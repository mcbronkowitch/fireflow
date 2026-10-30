# Runs the two reverb_dump variants and requires identical output (see
# tests/reverb_dump.cpp). Arguments: -DDUMP_A=<exe> -DDUMP_B=<exe> -DOUT_DIR=<dir>
# -DWHAT=<failure message>
file(MAKE_DIRECTORY "${OUT_DIR}")
execute_process(COMMAND "${DUMP_A}" "${OUT_DIR}/a.bin" RESULT_VARIABLE ra)
execute_process(COMMAND "${DUMP_B}" "${OUT_DIR}/b.bin" RESULT_VARIABLE rb)
if(NOT ra EQUAL 0)
  message(FATAL_ERROR "reverb_dump_dispatch failed (${ra})")
endif()
if(NOT rb EQUAL 0)
  message(FATAL_ERROR "reverb_dump_only failed (${rb})")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${OUT_DIR}/a.bin" "${OUT_DIR}/b.bin" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "${WHAT}")
endif()

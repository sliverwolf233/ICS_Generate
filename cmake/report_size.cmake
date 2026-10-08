# report_size.cmake - prints the executable size and enforces the hard size gate.
#
# Called by the ICS_Generate POST_BUILD step, but usable stand-alone:
#   cmake -DICSG_EXE=path/to/ICS_Generate.exe -DICSG_SIZE_GATE_KB=96 \
#         -P cmake/report_size.cmake
#
# Fails the build (FATAL_ERROR) when the executable is missing or larger than
# the gate, so an oversized or unlinked image can never be published by CI.

if(NOT DEFINED ICSG_EXE OR "${ICSG_EXE}" STREQUAL "")
    message(FATAL_ERROR "report_size.cmake: ICSG_EXE is not set")
endif()

if(NOT EXISTS "${ICSG_EXE}")
    message(FATAL_ERROR
        "report_size.cmake: executable not found: ${ICSG_EXE}\n"
        "The link step did not produce the expected binary.")
endif()

file(SIZE "${ICSG_EXE}" ICSG_BYTES)
get_filename_component(ICSG_NAME "${ICSG_EXE}" NAME)

# 2 decimal KB without touching floating point.
math(EXPR ICSG_KB100 "${ICSG_BYTES} * 100 / 1024")
math(EXPR ICSG_KB_INT "${ICSG_KB100} / 100")
math(EXPR ICSG_KB_FRAC "${ICSG_KB100} % 100")
if(ICSG_KB_FRAC LESS 10)
    set(ICSG_KB_FRAC "0${ICSG_KB_FRAC}")
endif()

message(STATUS "size report: ${ICSG_NAME}: ${ICSG_BYTES} bytes (${ICSG_KB_INT}.${ICSG_KB_FRAC} KB)")

if(DEFINED ICSG_SIZE_TARGET_KB AND NOT ICSG_SIZE_TARGET_KB STREQUAL "")
    message(STATUS "size report: product target is ${ICSG_SIZE_TARGET_KB} KB")
endif()

if(DEFINED ICSG_SIZE_GATE_KB AND NOT ICSG_SIZE_GATE_KB STREQUAL "")
    math(EXPR ICSG_GATE_BYTES "${ICSG_SIZE_GATE_KB} * 1024")
    if(ICSG_BYTES GREATER ICSG_GATE_BYTES)
        message(FATAL_ERROR
            "size gate FAILED: ${ICSG_NAME} is ${ICSG_BYTES} bytes, "
            "over the ${ICSG_SIZE_GATE_KB} KB limit (${ICSG_GATE_BYTES} bytes).\n"
            "Check the tiny-flag list in CMakeLists.txt before raising the gate.")
    endif()
    message(STATUS "size gate: PASS (limit ${ICSG_SIZE_GATE_KB} KB = ${ICSG_GATE_BYTES} bytes)")
endif()

if(DEFINED ICSG_SIZE_TXT AND NOT ICSG_SIZE_TXT STREQUAL "")
    file(WRITE "${ICSG_SIZE_TXT}"
         "${ICSG_NAME} ${ICSG_BYTES} bytes ${ICSG_KB_INT}.${ICSG_KB_FRAC} KB\n")
endif()

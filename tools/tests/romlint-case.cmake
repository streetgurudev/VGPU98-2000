# One romlint test case: assemble tools/tests/lint_rom.asm with an optional define,
# finish it with romfix (unless the case builds a complete image itself), then lint it.
# Inputs: NASM, ROMFIX, ROMLINT, SOURCE, CARD, WORK, DEFINE (may be empty),
#         FIX (ON/OFF), EXPECT ("pass" or a regular expression the error must match).

file(MAKE_DIRECTORY "${WORK}")
set(raw "${WORK}/rom.raw")
set(rom "${WORK}/rom.bin")

set(defines)
if(DEFINE)
    set(defines "-D${DEFINE}")
endif()
execute_process(COMMAND "${NASM}" -f bin ${defines} -o "${raw}" "${SOURCE}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "nasm failed")
endif()

if(FIX)
    execute_process(COMMAND "${ROMFIX}" "${raw}" "${rom}" --size 512 RESULT_VARIABLE result OUTPUT_QUIET)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "romfix failed")
    endif()
else()
    file(COPY_FILE "${raw}" "${rom}")
endif()

execute_process(COMMAND "${ROMLINT}" "${rom}" --card "${CARD}" RESULT_VARIABLE result ERROR_VARIABLE err)
if(EXPECT STREQUAL "pass")
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "romlint rejected a valid ROM:\n${err}")
    endif()
else()
    if(result EQUAL 0)
        message(FATAL_ERROR "romlint accepted a broken ROM (${DEFINE})")
    endif()
    if(NOT err MATCHES "${EXPECT}")
        message(FATAL_ERROR "romlint failed for the wrong reason:\n${err}")
    endif()
endif()

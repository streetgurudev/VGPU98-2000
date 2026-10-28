# Runs cardc twice into separate directories and requires byte-identical outputs.
# Inputs: CARDC, CARD, MODES, DISPLAY, WORK (scratch directory).

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/a" "${WORK}/b")

foreach(run a b)
    execute_process(
        COMMAND "${CARDC}" --card "${CARD}" --modes "${MODES}" --display "${DISPLAY}" --out-dir "${WORK}/${run}"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "cardc failed (run ${run})")
    endif()
endforeach()

foreach(name card.inc vgamodes.inc palettes.inc vbemodes.inc drvmodes.inc vid_vgpu98_card.h)
    file(READ "${WORK}/a/${name}" a HEX)
    file(READ "${WORK}/b/${name}" b HEX)
    if(NOT a STREQUAL b)
        message(FATAL_ERROR "${name} differs between runs")
    endif()
    if(a MATCHES "0d0a")
        message(FATAL_ERROR "${name} contains CRLF line endings")
    endif()
endforeach()

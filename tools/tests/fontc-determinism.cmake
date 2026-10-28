# Runs fontc twice into separate directories and requires byte-identical outputs.
# Inputs: FONTC, FONT, WORK (scratch directory).

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/a" "${WORK}/b")

foreach(run a b)
    execute_process(
        COMMAND "${FONTC}" "${FONT}" --inc "${WORK}/${run}/font.inc" --label font --png "${WORK}/${run}/font.png"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "fontc failed (run ${run})")
    endif()
endforeach()

foreach(name font.inc font.png)
    file(READ "${WORK}/a/${name}" a HEX)
    file(READ "${WORK}/b/${name}" b HEX)
    if(NOT a STREQUAL b)
        message(FATAL_ERROR "${name} differs between runs")
    endif()
endforeach()

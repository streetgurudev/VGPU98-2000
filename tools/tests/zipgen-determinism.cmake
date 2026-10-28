# Writes the same zip twice, into folders of different names, and requires
# byte-identical results. zipgen reads each archive back itself before writing
# it, so both runs also check every entry against its source.
#
# Inputs: ZIPGEN, SRC (tools/tests), WORK.
#
# Copyright 2026 VGPU98 Project contributors
# SPDX-License-Identifier: GPL-2.0-or-later

file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK}/one ${WORK}/two-with-a-longer-name)

foreach(dir one two-with-a-longer-name)
    execute_process(
        COMMAND ${ZIPGEN} -o ${WORK}/${dir}/test.zip -t 1793184458
                top/res1.bin=${SRC}/vglink/res1.bin top/sub/res2.bin=${SRC}/vglink/res2.bin
                top/LICENSE.txt=${SRC}/../../LICENSE
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "zipgen failed (${dir})")
    endif()
endforeach()

execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORK}/one/test.zip ${WORK}/two-with-a-longer-name/test.zip
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the two zips differ")
endif()

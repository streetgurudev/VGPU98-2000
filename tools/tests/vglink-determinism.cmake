# Links the NE and LE test modules twice, from the objects in a different order of
# arguments-to-output paths, and requires byte-identical results.
#
# Inputs: VGLINK, SRC (tools/tests/vglink), OBJ_DIR (the assembled objects), WORK.
#
# Copyright 2026 VGPU98 Project contributors
# SPDX-License-Identifier: GPL-2.0-or-later

file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK}/one ${WORK}/two-with-a-longer-name)

foreach(dir one two-with-a-longer-name)
    execute_process(
        COMMAND ${VGLINK} -d ${SRC}/ne.lnk -o ${WORK}/${dir}/test.drv -m ${WORK}/${dir}/test.map
                ${OBJ_DIR}/ne_a.obj ${OBJ_DIR}/ne_b.obj
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "vglink failed (NE, ${dir})")
    endif()
    execute_process(
        COMMAND ${VGLINK} -d ${SRC}/le.lnk -o ${WORK}/${dir}/test.vxd -m ${WORK}/${dir}/test-le.map ${OBJ_DIR}/le.obj
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "vglink failed (LE, ${dir})")
    endif()
endforeach()

foreach(f test.drv test.map test.vxd test-le.map)
    execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORK}/one/${f} ${WORK}/two-with-a-longer-name/${f}
                    RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "${f} differs between two links")
    endif()
endforeach()

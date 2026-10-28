# Links the PE test DLL (pe.lnk) twice, from different output paths, and
# requires byte-identical results; then checks what drvlint shows of it: per-
# process sections, the export names as written, the version resource.
#
# Inputs: VGLINK, DRVLINT, SRC (tools/tests/vglink), OBJ (pe.obj), WORK.
#
# Copyright 2026 VGPU98 Project contributors
# SPDX-License-Identifier: GPL-2.0-or-later

file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK}/one ${WORK}/two-with-a-longer-name)

foreach(dir one two-with-a-longer-name)
    execute_process(
        COMMAND ${VGLINK} -d ${SRC}/pe.lnk -o ${WORK}/${dir}/test.dll -m ${WORK}/${dir}/test.map ${OBJ}
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "vglink failed (PE, ${dir})")
    endif()
endforeach()
foreach(f test.dll test.map)
    execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORK}/one/${f} ${WORK}/two-with-a-longer-name/${f}
                    RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "${f} differs between two links")
    endif()
endforeach()

execute_process(
    COMMAND ${DRVLINT} --dump --strict --glide --exports ${SRC}/pe.exports ${WORK}/one/test.dll
    OUTPUT_VARIABLE dump ERROR_VARIABLE errors RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "drvlint refused the PE test DLL:\n${errors}")
endif()
foreach(want
        "image base 10000000, 4 sections"
        "section .text    RVA 00001000"
        "section .data    RVA 00002000"
        "section .rsrc    RVA 00003000"
        "section .reloc   RVA 00004000"
        "at    400, flags 60000020\n"
        "flags C0000040\n"
        "flags 40000040\n"
        "flags 42000040\n"
        "exports of test.dll: 2 functions, 2 names"
        "    _TestOne@4 -> "
        "    __TestTwo@8 -> "
        "2 base relocations"
        "version resource: file 2.61.0.658, product 2.61.0.658, OS 10004, type 3, subtype 8"
        "    string table 040904E4\n"
        "    Translation: 0409 04E4\n"
        "    CompanyName: VGPU98 Project\n"
        "    FileDescription: vglink's PE test DLL\n"
        "    FileVersion: 2.61.00.0658\n"
        "    InternalName: test\n"
        "    LegalCopyright: Copyright 2026 VGPU98 Project contributors. GPL-2.0-or-later.\n"
        "    OriginalFilename: test.dll\n"
        "    ProductName: VGPU98 2000\n"
        "    ProductVersion: 2.61.00.0658\n")
    string(FIND "${dump}" "${want}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "drvlint's dump of the PE test DLL lacks: ${want}\n${dump}")
    endif()
endforeach()

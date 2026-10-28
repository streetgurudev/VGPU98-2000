# Runs infgen on the real card definition twice, requires identical output, and
# checks the lines Windows 98 needs to find and install the driver.
#
# Inputs: INFGEN, DRIVER, CARD, MODES, WORK.
#
# Copyright 2026 VGPU98 Project contributors
# SPDX-License-Identifier: GPL-2.0-or-later

file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK})
foreach(n 1 2)
    execute_process(
        COMMAND ${INFGEN} --driver ${DRIVER} --card ${CARD} --modes ${MODES} -o ${WORK}/vgpu98-${n}.inf
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "infgen failed")
    endif()
endforeach()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORK}/vgpu98-1.inf ${WORK}/vgpu98-2.inf
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "infgen is not deterministic")
endif()

# CRLF line ends: checked on the bytes, since file(READ) drops carriage returns.
file(READ ${WORK}/vgpu98-1.inf hex HEX)
string(REGEX REPLACE "(..)" "\\1 " bytes "${hex}")
string(REGEX MATCHALL "0a " lf_all "${bytes}")
string(REGEX MATCHALL "0d 0a " crlf_all "${bytes}")
list(LENGTH lf_all n_lf)
list(LENGTH crlf_all n_crlf)
if((n_lf EQUAL 0) OR (NOT n_lf EQUAL n_crlf))
    message(FATAL_ERROR "the INF must use CRLF line ends (${n_lf} LF, ${n_crlf} CRLF)")
endif()

file(READ ${WORK}/vgpu98-1.inf inf)
foreach(want
        "Signature=\"$CHICAGO$\"\n"
        "Class=DISPLAY\n"
        "DefaultDestDir=11\n"
        "[SourceDisksFiles]\nvgpu98.drv=1\nvgpu98.vxd=1\nvgpu98dx.dll=1\nglide2x.dll=1\nglide3x.dll=1\nvgpu98gl.dll=1\nvgpu98cp.cpl=1\n\n"
        "[VGPU98.Copy]\nvgpu98.drv\nvgpu98.vxd\nvgpu98dx.dll\nglide2x.dll\nglide3x.dll\nvgpu98gl.dll\nvgpu98cp.cpl\n\n"
        "%Device%=VGPU98,PCI\\VEN_9898&DEV_2000&SUBSYS_20009898,PCI\\VEN_9898&DEV_2000\n"
        "HKR,,DevLoader,,*vdd\n"
        "HKR,DEFAULT,drv,,vgpu98.drv\n"
        "HKR,DEFAULT,minivdd,,vgpu98.vxd\n"
        "HKLM,\"Software\\Microsoft\\Windows\\CurrentVersion\\OpenGLdrivers\",\"VGPU98\",,\"vgpu98gl.dll\"\n"
        "HKCR,CLSID\\{79D3DDE2-6AC4-43A5-860C-5C7E56C689F2},,,\"VGPU98 2000\"\n"
        "HKCR,CLSID\\{79D3DDE2-6AC4-43A5-860C-5C7E56C689F2}\\InProcServer32,,,\"%11%\\vgpu98cp.cpl\"\n"
        "HKCR,CLSID\\{79D3DDE2-6AC4-43A5-860C-5C7E56C689F2}\\InProcServer32,ThreadingModel,,\"Apartment\"\n"
        "HKLM,\"Software\\Microsoft\\Windows\\CurrentVersion\\Controls Folder\\Device\\shellex\\PropertySheetHandlers\\VGPU98 2000\",,,\"{79D3DDE2-6AC4-43A5-860C-5C7E56C689F2}\"\n"
        "HKR,DEFAULT,Mode,,\"8,640,480\"\n"
        "HKR,\"MODES\\8\\640,480\",,,\"60,72,75,85,100,120,140,160\"\n"
        "HKR,\"MODES\\8\\800,600\",,,\"56,60,72,75,85,100,120,140,160\"\n"
        "HKR,\"MODES\\16\\1024,768\",,,\"60,70,75,85,100,120\"\n"
        "HKR,\"MODES\\32\\1920,1440\",,,\"60,75\"\n")
    string(FIND "${inf}" "${want}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "the INF lacks: ${want}")
    endif()
endforeach()

# One model line: Windows lists every matching line as a model of its own.
string(REGEX MATCHALL "%Device%=" models "${inf}")
list(LENGTH models n)
if(NOT n EQUAL 1)
    message(FATAL_ERROR "the INF has ${n} model lines, expected 1")
endif()

# Every desktop mode at 8, 16 and 32 bpp, nothing else.
string(REGEX MATCHALL "HKR,\"MODES\\\\(8|16|32)\\\\[0-9]+,[0-9]+\",,," modes "${inf}")
list(LENGTH modes n)
if(NOT n EQUAL 42)
    message(FATAL_ERROR "the INF lists ${n} modes, expected 42 (14 resolutions at 3 depths)")
endif()
string(FIND "${inf}" "1280,720" wide)
if(NOT wide EQUAL -1)
    message(FATAL_ERROR "a disabled widescreen mode is in the INF")
endif()

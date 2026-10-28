# Checks the driver package folder: exactly the INF and the files it installs,
# each identical to what the build made.
# cmake -DPACKAGE=<folder> -DDRV=<vgpu98.drv> -DVXD=<vgpu98.vxd> -DDLL=<vgpu98dx.dll>
#       -DGLIDE2=<glide2x.dll> -DGLIDE3=<glide3x.dll> -DGL=<vgpu98gl.dll> -DCPL=<vgpu98cp.cpl>
#       -P package-check.cmake

set(installed glide2x.dll glide3x.dll vgpu98.drv vgpu98.vxd vgpu98cp.cpl vgpu98dx.dll vgpu98gl.dll)

file(GLOB present RELATIVE ${PACKAGE} ${PACKAGE}/*)
list(SORT present)
set(expected ${installed} vgpu98.inf)
list(SORT expected)
if(NOT present STREQUAL expected)
    message(FATAL_ERROR "the package holds [${present}], expected [${expected}]")
endif()

# Every file the INF names in [SourceDisksFiles] is in the folder.
file(STRINGS ${PACKAGE}/vgpu98.inf inf)
set(in_section FALSE)
set(listed "")
foreach(l IN LISTS inf)
    if(l MATCHES "^\\[")
        set(in_section FALSE)
        if(l STREQUAL "[SourceDisksFiles]")
            set(in_section TRUE)
        endif()
    elseif(in_section AND l MATCHES "^([^=]+)=1$")
        list(APPEND listed ${CMAKE_MATCH_1})
    endif()
endforeach()
list(SORT listed)
if(NOT listed STREQUAL "${installed}")
    message(FATAL_ERROR "the INF installs [${listed}], expected [${installed}]")
endif()

foreach(pair "vgpu98.drv;${DRV}" "vgpu98.vxd;${VXD}" "vgpu98dx.dll;${DLL}" "glide2x.dll;${GLIDE2}"
             "glide3x.dll;${GLIDE3}" "vgpu98gl.dll;${GL}" "vgpu98cp.cpl;${CPL}")
    list(GET pair 0 name)
    list(GET pair 1 built)
    execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${PACKAGE}/${name} ${built} RESULT_VARIABLE diff)
    if(NOT diff EQUAL 0)
        message(FATAL_ERROR "${name} in the package differs from ${built}")
    endif()
endforeach()

# The DirectX driver: vgpu98dx.dll, a 32-bit PE DLL (spec/directx-hal.md).
# Compiled by the host's gcc in 32-bit freestanding mode (with its own three
# freestanding headers in include/) to pe-i386 COFF
# objects, linked by vglink (spec/pe-format.md) and checked by drvlint. It
# links no C library: driver32/rt.c is its runtime, KERNEL32 its only import.

set(DLL_DIR ${CMAKE_BINARY_DIR}/driver32)
file(MAKE_DIRECTORY ${DLL_DIR})

if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "vgpu98dx.dll needs gcc (its -m32 freestanding mode)")
endif()

set(DLL_CFLAGS ${VGPU98_DLL_CFLAGS}
    -I ${CMAKE_CURRENT_SOURCE_DIR} -I ${VGPU98_GEN_DIR} -I ${CMAKE_SOURCE_DIR}/card)

file(GLOB DLL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
file(GLOB DLL_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h)

set(DLL_OBJECTS)
foreach(src ${DLL_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    set(obj ${DLL_DIR}/${name}.obj)
    add_custom_command(
        OUTPUT ${obj}
        COMMAND ${CMAKE_C_COMPILER} ${DLL_CFLAGS} -c ${src} -o ${obj}
        DEPENDS ${src} ${DLL_HEADERS} card_gen ${CMAKE_SOURCE_DIR}/card/vid_vgpu98_gpu.h
                ${CMAKE_SOURCE_DIR}/card/vid_vgpu98_prog.h build_stamp
                ${VGPU98_GEN_DIR}/stamp.h
        COMMENT "Compiling ${name}.c for vgpu98dx.dll"
        VERBATIM)
    list(APPEND DLL_OBJECTS ${obj})
endforeach()

# Only a DLL that passes drvlint becomes vgpu98dx.dll.
add_custom_command(
    OUTPUT ${DLL_DIR}/vgpu98dx.dll ${DLL_DIR}/vgpu98dx.map
    BYPRODUCTS ${DLL_DIR}/vgpu98dx.unchecked.dll
    COMMAND vglink -d ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98dx.lnk
            -o ${DLL_DIR}/vgpu98dx.unchecked.dll -m ${DLL_DIR}/vgpu98dx.map ${DLL_OBJECTS}
    COMMAND drvlint --strict --pe ${DLL_DIR}/vgpu98dx.unchecked.dll
    COMMAND ${CMAKE_COMMAND} -E copy ${DLL_DIR}/vgpu98dx.unchecked.dll ${DLL_DIR}/vgpu98dx.dll
    DEPENDS vglink drvlint ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98dx.lnk ${DLL_OBJECTS}
    COMMENT "Linking and checking vgpu98dx.dll"
    VERBATIM)
add_custom_target(driver32 ALL DEPENDS ${DLL_DIR}/vgpu98dx.dll)

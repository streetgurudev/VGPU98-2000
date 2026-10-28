# The VGPU98 2000's settings: vgpu98cp.cpl, a 32-bit PE DLL that the
# Display Properties' Advanced dialog, the Control Panel and rundll32 load.
# Compiled as vgpu98dx.dll is (VGPU98_DLL_CFLAGS), from its
# own folder and two files of the DirectX DLL's: its runtime (rt.c) and its
# log lines (logline.c). Linked by vglink, and only a DLL that passes
# drvlint, its exports exactly its list's, is kept.

set(CPL_DIR ${CMAKE_BINARY_DIR}/cpl)
file(MAKE_DIRECTORY ${CPL_DIR})

file(GLOB CPL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
file(GLOB CPL_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
set(CPL_DRIVER32_SOURCES ${CMAKE_SOURCE_DIR}/driver32/rt.c ${CMAKE_SOURCE_DIR}/driver32/logline.c)
file(GLOB CPL_FREESTANDING_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/driver32/include/*.h)
set(CPL_DRIVER32_HEADERS ${CMAKE_SOURCE_DIR}/driver32/shared.h ${CMAKE_SOURCE_DIR}/driver32/ddi.h
    ${CMAKE_SOURCE_DIR}/driver32/logline.h ${CPL_FREESTANDING_HEADERS})
set(CPL_FLAGS ${VGPU98_DLL_CFLAGS} -I ${CMAKE_CURRENT_SOURCE_DIR} -I ${CMAKE_SOURCE_DIR}/driver32 -I ${VGPU98_GEN_DIR}
    -I ${CMAKE_SOURCE_DIR}/card)

set(CPL_OBJECTS)
foreach(src ${CPL_SOURCES} ${CPL_DRIVER32_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    set(obj ${CPL_DIR}/${name}.obj)
    add_custom_command(
        OUTPUT ${obj}
        COMMAND ${CMAKE_C_COMPILER} ${CPL_FLAGS} -c ${src} -o ${obj}
        DEPENDS ${src} ${CPL_HEADERS} ${CPL_DRIVER32_HEADERS} card_gen build_stamp ${VGPU98_GEN_DIR}/stamp.h
        COMMENT "Compiling ${name}.c for vgpu98cp.cpl"
        VERBATIM)
    list(APPEND CPL_OBJECTS ${obj})
endforeach()

add_custom_command(
    OUTPUT ${CPL_DIR}/vgpu98cp.cpl ${CPL_DIR}/vgpu98cp.map
    BYPRODUCTS ${CPL_DIR}/vgpu98cp.unchecked.cpl
    COMMAND vglink -d ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98cp.lnk -o ${CPL_DIR}/vgpu98cp.unchecked.cpl
            -m ${CPL_DIR}/vgpu98cp.map ${CPL_OBJECTS}
    COMMAND drvlint --strict --cpl --exports ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98cp.exports ${CPL_DIR}/vgpu98cp.unchecked.cpl
    COMMAND ${CMAKE_COMMAND} -E copy ${CPL_DIR}/vgpu98cp.unchecked.cpl ${CPL_DIR}/vgpu98cp.cpl
    DEPENDS vglink drvlint ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98cp.lnk ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98cp.exports
            ${CPL_OBJECTS}
    COMMENT "Linking and checking vgpu98cp.cpl"
    VERBATIM)

add_custom_target(cpl ALL DEPENDS ${CPL_DIR}/vgpu98cp.cpl)

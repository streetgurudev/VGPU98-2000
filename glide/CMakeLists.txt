# The Glide DLLs: glide2x.dll and glide3x.dll, 32-bit PE DLLs loaded into the
# programs that use Glide (spec/glide.md). Each is
# compiled as vgpu98dx.dll is (VGPU98_DLL_CFLAGS), from its own folder, the
# part both share (glide/*.c) and three files of the DirectX DLL's: its
# runtime (rt.c), its log lines (logline.c) and its ring (gpu.c, which
# VGPU98_GLIDE_DLL has take glide/gpuenv.h). Linked
# by vglink, and only a DLL that passes drvlint's Glide checks, its exports
# exactly its list's, is kept.

set(GLIDE_DIR ${CMAKE_BINARY_DIR}/glide)

file(GLOB GLIDE_COMMON_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
file(GLOB GLIDE_COMMON_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
set(GLIDE_DRIVER32_SOURCES ${CMAKE_SOURCE_DIR}/driver32/rt.c ${CMAKE_SOURCE_DIR}/driver32/logline.c
    ${CMAKE_SOURCE_DIR}/driver32/gpu.c)
file(GLOB GLIDE_FREESTANDING_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/driver32/include/*.h)
set(GLIDE_DRIVER32_HEADERS ${CMAKE_SOURCE_DIR}/driver32/shared.h ${CMAKE_SOURCE_DIR}/driver32/ddi.h
    ${CMAKE_SOURCE_DIR}/driver32/logline.h ${CMAKE_SOURCE_DIR}/driver32/gpu.h ${GLIDE_FREESTANDING_HEADERS})

set(GLIDE_TARGETS)
foreach(dll glide2x glide3x)
    set(src_dir ${CMAKE_CURRENT_SOURCE_DIR}/${dll})
    set(out_dir ${GLIDE_DIR}/${dll})
    file(MAKE_DIRECTORY ${out_dir})
    file(GLOB sources CONFIGURE_DEPENDS ${src_dir}/*.c)
    file(GLOB headers CONFIGURE_DEPENDS ${src_dir}/*.h)
    set(flags ${VGPU98_DLL_CFLAGS} -DVGPU98_GLIDE_DLL -I ${src_dir} -I ${CMAKE_CURRENT_SOURCE_DIR}
        -I ${CMAKE_SOURCE_DIR}/driver32 -I ${VGPU98_GEN_DIR} -I ${CMAKE_SOURCE_DIR}/card)

    set(objects)
    foreach(src ${sources} ${GLIDE_COMMON_SOURCES} ${GLIDE_DRIVER32_SOURCES})
        get_filename_component(name ${src} NAME_WE)
        set(obj ${out_dir}/${name}.obj)
        add_custom_command(
            OUTPUT ${obj}
            COMMAND ${CMAKE_C_COMPILER} ${flags} -c ${src} -o ${obj}
            DEPENDS ${src} ${headers} ${GLIDE_COMMON_HEADERS} ${GLIDE_DRIVER32_HEADERS} card_gen build_stamp
                    ${VGPU98_GEN_DIR}/stamp.h
            COMMENT "Compiling ${name}.c for ${dll}.dll"
            VERBATIM)
        list(APPEND objects ${obj})
    endforeach()

    add_custom_command(
        OUTPUT ${GLIDE_DIR}/${dll}.dll ${GLIDE_DIR}/${dll}.map
        BYPRODUCTS ${out_dir}/${dll}.unchecked.dll
        COMMAND vglink -d ${src_dir}/${dll}.lnk -o ${out_dir}/${dll}.unchecked.dll -m ${GLIDE_DIR}/${dll}.map
                ${objects}
        COMMAND drvlint --strict --glide --exports ${src_dir}/${dll}.exports ${out_dir}/${dll}.unchecked.dll
        COMMAND ${CMAKE_COMMAND} -E copy ${out_dir}/${dll}.unchecked.dll ${GLIDE_DIR}/${dll}.dll
        DEPENDS vglink drvlint ${src_dir}/${dll}.lnk ${src_dir}/${dll}.exports ${objects}
        COMMENT "Linking and checking ${dll}.dll"
        VERBATIM)
    list(APPEND GLIDE_TARGETS ${GLIDE_DIR}/${dll}.dll)
endforeach()

add_custom_target(glide ALL DEPENDS ${GLIDE_TARGETS})

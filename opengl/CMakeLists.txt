# The OpenGL ICD: vgpu98gl.dll, a 32-bit PE DLL Windows' opengl32.dll loads
# into the programs that use OpenGL (spec/opengl.md).
# Compiled as vgpu98dx.dll is (VGPU98_DLL_CFLAGS), from its own folder and
# three files of the DirectX DLL's: its runtime (rt.c), its ring (gpu.c,
# which VGPU98_ICD_DLL has take opengl/gpuenv.h) and its line writer (logline.c).
# Linked by vglink, and only a DLL
# that passes drvlint's ICD checks, its exports exactly its list's, is kept.

set(GL_DIR ${CMAKE_BINARY_DIR}/opengl)
file(MAKE_DIRECTORY ${GL_DIR})

file(GLOB GL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
file(GLOB GL_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
set(GL_DRIVER32_SOURCES ${CMAKE_SOURCE_DIR}/driver32/rt.c ${CMAKE_SOURCE_DIR}/driver32/gpu.c ${CMAKE_SOURCE_DIR}/driver32/logline.c)
file(GLOB GL_FREESTANDING_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/driver32/include/*.h)
set(GL_DRIVER32_HEADERS ${CMAKE_SOURCE_DIR}/driver32/shared.h ${CMAKE_SOURCE_DIR}/driver32/ddi.h ${CMAKE_SOURCE_DIR}/driver32/logline.h
    ${CMAKE_SOURCE_DIR}/driver32/gpu.h ${GL_FREESTANDING_HEADERS})
set(GL_FLAGS ${VGPU98_DLL_CFLAGS} -DVGPU98_ICD_DLL -I ${CMAKE_CURRENT_SOURCE_DIR} -I ${CMAKE_SOURCE_DIR}/driver32
    -I ${VGPU98_GEN_DIR} -I ${CMAKE_SOURCE_DIR}/card)

set(GL_OBJECTS)
foreach(src ${GL_SOURCES} ${GL_DRIVER32_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    set(obj ${GL_DIR}/${name}.obj)
    add_custom_command(
        OUTPUT ${obj}
        COMMAND ${CMAKE_C_COMPILER} ${GL_FLAGS} -c ${src} -o ${obj}
        DEPENDS ${src} ${GL_HEADERS} ${GL_DRIVER32_HEADERS} card_gen build_stamp ${VGPU98_GEN_DIR}/stamp.h
                ${CMAKE_SOURCE_DIR}/card/vid_vgpu98_gpu.h ${CMAKE_SOURCE_DIR}/card/vid_vgpu98_oglcmd.h
        COMMENT "Compiling ${name}.c for vgpu98gl.dll"
        VERBATIM)
    list(APPEND GL_OBJECTS ${obj})
endforeach()

add_custom_command(
    OUTPUT ${GL_DIR}/vgpu98gl.dll ${GL_DIR}/vgpu98gl.map
    BYPRODUCTS ${GL_DIR}/vgpu98gl.unchecked.dll
    COMMAND vglink -d ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98gl.lnk -o ${GL_DIR}/vgpu98gl.unchecked.dll
            -m ${GL_DIR}/vgpu98gl.map ${GL_OBJECTS}
    COMMAND drvlint --strict --icd --exports ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98gl.exports ${GL_DIR}/vgpu98gl.unchecked.dll
    COMMAND ${CMAKE_COMMAND} -E copy ${GL_DIR}/vgpu98gl.unchecked.dll ${GL_DIR}/vgpu98gl.dll
    DEPENDS vglink drvlint ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98gl.lnk ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98gl.exports
            ${GL_OBJECTS}
    COMMENT "Linking and checking vgpu98gl.dll"
    VERBATIM)

add_custom_target(opengl ALL DEPENDS ${GL_DIR}/vgpu98gl.dll)

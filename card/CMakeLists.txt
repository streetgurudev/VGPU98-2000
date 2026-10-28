set(CARD_DEF   ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98.card)
set(CARD_MODES ${CMAKE_CURRENT_SOURCE_DIR}/vga-modes.card)
set(CARD_DISPLAY ${CMAKE_CURRENT_SOURCE_DIR}/display-modes.card)

set(CARD_OUTPUTS
    ${VGPU98_GEN_DIR}/card.inc
    ${VGPU98_GEN_DIR}/vgamodes.inc
    ${VGPU98_GEN_DIR}/palettes.inc
    ${VGPU98_GEN_DIR}/vbemodes.inc
    ${VGPU98_GEN_DIR}/drvmodes.inc
    ${VGPU98_GEN_DIR}/vid_vgpu98_card.h)

add_custom_command(
    OUTPUT ${CARD_OUTPUTS}
    COMMAND cardc --card ${CARD_DEF} --modes ${CARD_MODES} --display ${CARD_DISPLAY} --out-dir ${VGPU98_GEN_DIR}
    DEPENDS cardc ${CARD_DEF} ${CARD_MODES} ${CARD_DISPLAY}
    COMMENT "Compiling the card definition"
    VERBATIM)
add_custom_target(card_gen ALL DEPENDS ${CARD_OUTPUTS})

# The headers 86Box builds from: the one cardc generates and the command set
# (vid_vgpu98_gpu.h, kept here and shared with the Windows 98 driver DLL), and the
# programs' instruction format with its validator (vid_vgpu98_prog.h, likewise), and the
# Glide device's commands and arithmetic (vid_vgpu98_glcmd.h, vid_vgpu98_glcpu.h, shared
# with the Glide DLLs), and the OpenGL device's commands (vid_vgpu98_oglcmd.h, shared with
# the OpenGL ICD), and Diagnostics "Display"'s arithmetic (vid_vgpu98_dispdiag.h, shared
# with the host tests).
set(VGPU98_86BOX_INCLUDE ${VGPU98_86BOX_DIR}/src/include/86box)
set(VGPU98_GPU_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/vid_vgpu98_gpu.h)
set(VGPU98_PROG_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/vid_vgpu98_prog.h)
set(VGPU98_GLCMD_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/vid_vgpu98_glcmd.h)
set(VGPU98_GLCPU_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/vid_vgpu98_glcpu.h)
set(VGPU98_OGLCMD_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/vid_vgpu98_oglcmd.h)
set(VGPU98_DISPDIAG_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/vid_vgpu98_dispdiag.h)

# Copies the headers into the 86Box tree. Run it after changing the card definition or the
# command set, then commit the headers on the 86Box vgpu98 branch.
add_custom_target(update-86box-header
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VGPU98_GEN_DIR}/vid_vgpu98_card.h ${VGPU98_86BOX_INCLUDE}/vid_vgpu98_card.h
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VGPU98_GPU_HEADER} ${VGPU98_86BOX_INCLUDE}/vid_vgpu98_gpu.h
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VGPU98_PROG_HEADER} ${VGPU98_86BOX_INCLUDE}/vid_vgpu98_prog.h
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VGPU98_GLCMD_HEADER} ${VGPU98_86BOX_INCLUDE}/vid_vgpu98_glcmd.h
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VGPU98_GLCPU_HEADER} ${VGPU98_86BOX_INCLUDE}/vid_vgpu98_glcpu.h
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VGPU98_OGLCMD_HEADER} ${VGPU98_86BOX_INCLUDE}/vid_vgpu98_oglcmd.h
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VGPU98_DISPDIAG_HEADER} ${VGPU98_86BOX_INCLUDE}/vid_vgpu98_dispdiag.h
    DEPENDS card_gen
    COMMENT "Updating the VGPU98 headers in ${VGPU98_86BOX_INCLUDE}"
    VERBATIM)

# Fails the build when a header in the 86Box tree no longer matches its source.
# 86Box builds from those copies, so they must never drift apart.
if(EXISTS ${VGPU98_86BOX_INCLUDE})
    add_custom_target(check-86box-header ALL
        COMMAND ${CMAKE_COMMAND}
            -DGENERATED=${VGPU98_GEN_DIR}/vid_vgpu98_card.h
            -DCOMMITTED=${VGPU98_86BOX_INCLUDE}/vid_vgpu98_card.h
            -DREPO=${VGPU98_86BOX_DIR}
            -DREPO_PATH=src/include/86box/vid_vgpu98_card.h
            -P ${CMAKE_SOURCE_DIR}/cmake/check-86box-header.cmake
        COMMAND ${CMAKE_COMMAND}
            -DGENERATED=${VGPU98_GPU_HEADER}
            -DCOMMITTED=${VGPU98_86BOX_INCLUDE}/vid_vgpu98_gpu.h
            -DREPO=${VGPU98_86BOX_DIR}
            -DREPO_PATH=src/include/86box/vid_vgpu98_gpu.h
            -P ${CMAKE_SOURCE_DIR}/cmake/check-86box-header.cmake
        COMMAND ${CMAKE_COMMAND}
            -DGENERATED=${VGPU98_PROG_HEADER}
            -DCOMMITTED=${VGPU98_86BOX_INCLUDE}/vid_vgpu98_prog.h
            -DREPO=${VGPU98_86BOX_DIR}
            -DREPO_PATH=src/include/86box/vid_vgpu98_prog.h
            -P ${CMAKE_SOURCE_DIR}/cmake/check-86box-header.cmake
        COMMAND ${CMAKE_COMMAND}
            -DGENERATED=${VGPU98_GLCMD_HEADER}
            -DCOMMITTED=${VGPU98_86BOX_INCLUDE}/vid_vgpu98_glcmd.h
            -DREPO=${VGPU98_86BOX_DIR}
            -DREPO_PATH=src/include/86box/vid_vgpu98_glcmd.h
            -P ${CMAKE_SOURCE_DIR}/cmake/check-86box-header.cmake
        COMMAND ${CMAKE_COMMAND}
            -DGENERATED=${VGPU98_GLCPU_HEADER}
            -DCOMMITTED=${VGPU98_86BOX_INCLUDE}/vid_vgpu98_glcpu.h
            -DREPO=${VGPU98_86BOX_DIR}
            -DREPO_PATH=src/include/86box/vid_vgpu98_glcpu.h
            -P ${CMAKE_SOURCE_DIR}/cmake/check-86box-header.cmake
        COMMAND ${CMAKE_COMMAND}
            -DGENERATED=${VGPU98_OGLCMD_HEADER}
            -DCOMMITTED=${VGPU98_86BOX_INCLUDE}/vid_vgpu98_oglcmd.h
            -DREPO=${VGPU98_86BOX_DIR}
            -DREPO_PATH=src/include/86box/vid_vgpu98_oglcmd.h
            -P ${CMAKE_SOURCE_DIR}/cmake/check-86box-header.cmake
        COMMAND ${CMAKE_COMMAND}
            -DGENERATED=${VGPU98_DISPDIAG_HEADER}
            -DCOMMITTED=${VGPU98_86BOX_INCLUDE}/vid_vgpu98_dispdiag.h
            -DREPO=${VGPU98_86BOX_DIR}
            -DREPO_PATH=src/include/86box/vid_vgpu98_dispdiag.h
            -P ${CMAKE_SOURCE_DIR}/cmake/check-86box-header.cmake
        DEPENDS card_gen ${VGPU98_GPU_HEADER} ${VGPU98_PROG_HEADER} ${VGPU98_GLCMD_HEADER} ${VGPU98_GLCPU_HEADER}
                ${VGPU98_OGLCMD_HEADER} ${VGPU98_DISPDIAG_HEADER}
        VERBATIM)
endif()

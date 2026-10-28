# The Windows 98 mini-VDD: vgpu98.vxd (LE). Assembled by NASM to OMF, linked
# by vglink, checked by drvlint and run on the host by vxdtest.

set(MINIVDD_DIR ${CMAKE_BINARY_DIR}/minivdd)
file(MAKE_DIRECTORY ${MINIVDD_DIR})

file(GLOB MINIVDD_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.asm
    ${CMAKE_CURRENT_SOURCE_DIR}/*.inc)

add_custom_command(
    OUTPUT ${MINIVDD_DIR}/minivdd.obj
    COMMAND ${NASM_EXECUTABLE} -f obj -w+error
            -I ${VGPU98_GEN_DIR}/ -I ${CMAKE_CURRENT_SOURCE_DIR}/
            -o ${MINIVDD_DIR}/minivdd.obj ${CMAKE_CURRENT_SOURCE_DIR}/minivdd.asm
    DEPENDS ${MINIVDD_SOURCES} card_gen ${VGPU98_GEN_DIR}/card.inc build_stamp ${VGPU98_GEN_DIR}/stamp.inc
    COMMENT "Assembling the mini-VDD"
    VERBATIM)

# Only a VxD that passes drvlint becomes vgpu98.vxd.
add_custom_command(
    OUTPUT ${MINIVDD_DIR}/vgpu98.vxd ${MINIVDD_DIR}/vgpu98.map
    BYPRODUCTS ${MINIVDD_DIR}/vgpu98.unchecked.vxd
    COMMAND vglink -d ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98.lnk
            -o ${MINIVDD_DIR}/vgpu98.unchecked.vxd -m ${MINIVDD_DIR}/vgpu98.map ${MINIVDD_DIR}/minivdd.obj
    COMMAND drvlint --strict --vxd ${MINIVDD_DIR}/vgpu98.unchecked.vxd
    COMMAND ${CMAKE_COMMAND} -E copy ${MINIVDD_DIR}/vgpu98.unchecked.vxd ${MINIVDD_DIR}/vgpu98.vxd
    DEPENDS vglink drvlint ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98.lnk ${MINIVDD_DIR}/minivdd.obj
    COMMENT "Linking and checking vgpu98.vxd"
    VERBATIM)
add_custom_target(minivdd ALL DEPENDS ${MINIVDD_DIR}/vgpu98.vxd)

if(TARGET vxdtest)
    add_test(NAME minivdd_on_host COMMAND vxdtest ${MINIVDD_DIR}/vgpu98.vxd)
endif()

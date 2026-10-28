# The Windows 98 display driver: vgpu98.drv (DIB engine minidriver, NE).
# Assembled by NASM to OMF, resources assembled to binary files, linked by
# vglink, checked by drvlint and run on the host by drvtest.

set(DRIVER_DIR ${CMAKE_BINARY_DIR}/driver)
file(MAKE_DIRECTORY ${DRIVER_DIR})

file(GLOB DRIVER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.asm
    ${CMAKE_CURRENT_SOURCE_DIR}/*.inc)

# The shared block's layout, from the DirectX DLL's header (driver32/shared.h).
add_custom_command(
    OUTPUT ${VGPU98_GEN_DIR}/shared.inc
    COMMAND sbgen ${VGPU98_GEN_DIR}/shared.inc
    DEPENDS sbgen ${CMAKE_SOURCE_DIR}/driver32/shared.h ${CMAKE_SOURCE_DIR}/driver32/ddi.h
    COMMENT "Writing the shared block's layout"
    VERBATIM)

add_custom_command(
    OUTPUT ${DRIVER_DIR}/vgpu98.obj
    COMMAND ${NASM_EXECUTABLE} -f obj -w+error
            -I ${VGPU98_GEN_DIR}/ -I ${CMAKE_CURRENT_SOURCE_DIR}/
            -o ${DRIVER_DIR}/vgpu98.obj ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98.asm
    DEPENDS ${DRIVER_SOURCES} card_gen ${VGPU98_GEN_DIR}/card.inc ${VGPU98_GEN_DIR}/drvmodes.inc
            ${VGPU98_GEN_DIR}/shared.inc build_stamp ${VGPU98_GEN_DIR}/stamp.inc
    COMMENT "Assembling the display driver"
    VERBATIM)

set(DRIVER_RESOURCES)
foreach(res config colortab fonts96 fonts120)
    add_custom_command(
        OUTPUT ${DRIVER_DIR}/${res}.bin
        COMMAND ${NASM_EXECUTABLE} -f bin -w+error -I ${CMAKE_CURRENT_SOURCE_DIR}/res/
                -o ${DRIVER_DIR}/${res}.bin ${CMAKE_CURRENT_SOURCE_DIR}/res/${res}.asm
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/${res}.asm ${CMAKE_CURRENT_SOURCE_DIR}/res/fonts.inc
        VERBATIM)
    list(APPEND DRIVER_RESOURCES ${DRIVER_DIR}/${res}.bin)
endforeach()

# Only a driver that passes drvlint becomes vgpu98.drv.
add_custom_command(
    OUTPUT ${DRIVER_DIR}/vgpu98.drv ${DRIVER_DIR}/vgpu98.map
    BYPRODUCTS ${DRIVER_DIR}/vgpu98.unchecked.drv
    COMMAND vglink -d ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98.lnk -r ${DRIVER_DIR}
            -o ${DRIVER_DIR}/vgpu98.unchecked.drv -m ${DRIVER_DIR}/vgpu98.map ${DRIVER_DIR}/vgpu98.obj
    COMMAND drvlint --strict --display ${DRIVER_DIR}/vgpu98.unchecked.drv
    COMMAND ${CMAKE_COMMAND} -E copy ${DRIVER_DIR}/vgpu98.unchecked.drv ${DRIVER_DIR}/vgpu98.drv
    DEPENDS vglink drvlint ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98.lnk ${DRIVER_DIR}/vgpu98.obj ${DRIVER_RESOURCES}
    COMMENT "Linking and checking vgpu98.drv"
    VERBATIM)
add_custom_target(driver ALL DEPENDS ${DRIVER_DIR}/vgpu98.drv)

if(TARGET drvtest)
    add_test(NAME driver_on_host
        COMMAND drvtest ${DRIVER_DIR}/vgpu98.drv ${CMAKE_SOURCE_DIR}/card/display-modes.card)
endif()

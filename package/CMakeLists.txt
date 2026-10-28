# The Windows 98 driver package (spec/driver-package.md): one folder,
# build/package/vgpu98-driver/, with the INF (written by infgen), the display
# driver, the mini-VDD, the DirectX driver DLL, the two Glide DLLs, the OpenGL
# ICD and the control program (vgpu98cp.cpl). Windows installs from it as it
# is (Have Disk), and 86Box can mount it as a CD
# (Media > CD-ROM > Folder). Beside it, build/package/vgpu98-driver.iso holds
# the same files as a CD image (isogen, padded at the end, checked by reading
# it back). scripts/release.sh copies both to dist/.
#
# mkdisk (tools/mkdisk) can still put these files on a 1.44 MB floppy image;
# the package does not use it.

set(PACKAGE_DIR ${CMAKE_BINARY_DIR}/package/vgpu98-driver)
file(MAKE_DIRECTORY ${PACKAGE_DIR})

set(DRIVER_CARD ${CMAKE_SOURCE_DIR}/card/driver.card)
set(DISPLAY_CARD ${CMAKE_SOURCE_DIR}/card/display-modes.card)
set(VGPU98_CARD ${CMAKE_SOURCE_DIR}/card/vgpu98.card)

# The files the INF installs, as the build makes them.
set(PACKAGE_DRV ${CMAKE_BINARY_DIR}/driver/vgpu98.drv)
set(PACKAGE_VXD ${CMAKE_BINARY_DIR}/minivdd/vgpu98.vxd)
set(PACKAGE_DLL ${CMAKE_BINARY_DIR}/driver32/vgpu98dx.dll)
set(PACKAGE_GLIDE2 ${CMAKE_BINARY_DIR}/glide/glide2x.dll)
set(PACKAGE_GLIDE3 ${CMAKE_BINARY_DIR}/glide/glide3x.dll)
set(PACKAGE_GL ${CMAKE_BINARY_DIR}/opengl/vgpu98gl.dll)
set(PACKAGE_CPL ${CMAKE_BINARY_DIR}/cpl/vgpu98cp.cpl)
set(PACKAGE_FILES ${PACKAGE_DIR}/vgpu98.inf ${PACKAGE_DIR}/vgpu98.drv ${PACKAGE_DIR}/vgpu98.vxd
    ${PACKAGE_DIR}/vgpu98dx.dll ${PACKAGE_DIR}/glide2x.dll ${PACKAGE_DIR}/glide3x.dll ${PACKAGE_DIR}/vgpu98gl.dll
    ${PACKAGE_DIR}/vgpu98cp.cpl)

add_custom_command(
    OUTPUT ${PACKAGE_DIR}/vgpu98.inf
    COMMAND infgen --driver ${DRIVER_CARD} --card ${VGPU98_CARD} --modes ${DISPLAY_CARD}
            -o ${PACKAGE_DIR}/vgpu98.inf
    DEPENDS infgen ${DRIVER_CARD} ${VGPU98_CARD} ${DISPLAY_CARD}
    COMMENT "Writing vgpu98.inf"
    VERBATIM)

add_custom_command(
    OUTPUT ${PACKAGE_DIR}/vgpu98.drv ${PACKAGE_DIR}/vgpu98.vxd ${PACKAGE_DIR}/vgpu98dx.dll ${PACKAGE_DIR}/glide2x.dll
           ${PACKAGE_DIR}/glide3x.dll ${PACKAGE_DIR}/vgpu98gl.dll ${PACKAGE_DIR}/vgpu98cp.cpl
    COMMAND ${CMAKE_COMMAND} -E copy ${PACKAGE_DRV} ${PACKAGE_DIR}/vgpu98.drv
    COMMAND ${CMAKE_COMMAND} -E copy ${PACKAGE_VXD} ${PACKAGE_DIR}/vgpu98.vxd
    COMMAND ${CMAKE_COMMAND} -E copy ${PACKAGE_DLL} ${PACKAGE_DIR}/vgpu98dx.dll
    COMMAND ${CMAKE_COMMAND} -E copy ${PACKAGE_GLIDE2} ${PACKAGE_DIR}/glide2x.dll
    COMMAND ${CMAKE_COMMAND} -E copy ${PACKAGE_GLIDE3} ${PACKAGE_DIR}/glide3x.dll
    COMMAND ${CMAKE_COMMAND} -E copy ${PACKAGE_GL} ${PACKAGE_DIR}/vgpu98gl.dll
    COMMAND ${CMAKE_COMMAND} -E copy ${PACKAGE_CPL} ${PACKAGE_DIR}/vgpu98cp.cpl
    DEPENDS driver minivdd driver32 glide opengl cpl ${PACKAGE_DRV} ${PACKAGE_VXD} ${PACKAGE_DLL} ${PACKAGE_GLIDE2}
            ${PACKAGE_GLIDE3} ${PACKAGE_GL} ${PACKAGE_CPL}
    COMMENT "Copying the driver files into the package"
    VERBATIM)

# The CD image: dated with the build's commit time (cmake/build-stamp.cmake).
set(PACKAGE_ISO ${CMAKE_BINARY_DIR}/package/vgpu98-driver.iso)
add_custom_command(
    OUTPUT ${PACKAGE_ISO}
    COMMAND isogen -o ${PACKAGE_ISO} -l VGPU98 -t @${VGPU98_GEN_DIR}/stamp.txt ${PACKAGE_FILES}
    DEPENDS isogen build_stamp ${VGPU98_GEN_DIR}/stamp.txt ${PACKAGE_FILES}
    COMMENT "Writing and checking the driver package's CD image"
    VERBATIM)

add_custom_target(driver_package ALL DEPENDS ${PACKAGE_FILES} ${PACKAGE_ISO})

add_test(NAME driver_package_folder
    COMMAND ${CMAKE_COMMAND} -DPACKAGE=${PACKAGE_DIR} -DDRV=${PACKAGE_DRV} -DVXD=${PACKAGE_VXD} -DDLL=${PACKAGE_DLL}
            -DGLIDE2=${PACKAGE_GLIDE2} -DGLIDE3=${PACKAGE_GLIDE3} -DGL=${PACKAGE_GL} -DCPL=${PACKAGE_CPL}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/package-check.cmake)

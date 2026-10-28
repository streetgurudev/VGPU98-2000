set(ROM_DIR ${CMAKE_BINARY_DIR}/rom)
file(MAKE_DIRECTORY ${ROM_DIR})

set(BIOS_MAIN ${CMAKE_CURRENT_SOURCE_DIR}/vgpu98.asm)
file(GLOB BIOS_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.asm
    ${CMAKE_CURRENT_SOURCE_DIR}/*.inc)

set(BIOS_RAW      ${CMAKE_CURRENT_BINARY_DIR}/vgpu98.raw)
set(BIOS_LISTING  ${ROM_DIR}/vgpu98.lst)
set(BIOS_ROM      ${ROM_DIR}/vgpu98.bin)
set(BIOS_UNCHECKED ${CMAKE_CURRENT_BINARY_DIR}/vgpu98.unchecked.bin)
set(BIOS_MANIFEST ${ROM_DIR}/manifest.txt)

set(GEN_INCLUDES
    ${VGPU98_GEN_DIR}/card.inc
    ${VGPU98_GEN_DIR}/vgamodes.inc
    ${VGPU98_GEN_DIR}/vbemodes.inc
    ${VGPU98_GEN_DIR}/palettes.inc
    ${VGPU98_GEN_DIR}/font8x8.inc
    ${VGPU98_GEN_DIR}/font8x14.inc
    ${VGPU98_GEN_DIR}/font8x16.inc)

add_custom_command(
    OUTPUT ${BIOS_RAW} ${BIOS_LISTING}
    COMMAND ${NASM_EXECUTABLE} -f bin -w+error
            -I ${VGPU98_GEN_DIR}/ -I ${CMAKE_CURRENT_SOURCE_DIR}/
            -l ${BIOS_LISTING} -o ${BIOS_RAW} ${BIOS_MAIN}
    DEPENDS ${BIOS_SOURCES} ${GEN_INCLUDES} card_gen fonts_gen
    COMMENT "Assembling the VGA BIOS"
    VERBATIM)

# romfix and romlint work on a temporary image; only an image that passes romlint
# becomes vgpu98.bin. A failed check leaves the old ROM older than its inputs, so
# the next build tries again instead of treating a bad image as up to date.
add_custom_command(
    OUTPUT ${BIOS_ROM}
    BYPRODUCTS ${BIOS_UNCHECKED}
    COMMAND romfix ${BIOS_RAW} ${BIOS_UNCHECKED} --card ${CMAKE_SOURCE_DIR}/card/vgpu98.card
    COMMAND romlint ${BIOS_UNCHECKED} --card ${CMAKE_SOURCE_DIR}/card/vgpu98.card
    COMMAND ${CMAKE_COMMAND} -E copy ${BIOS_UNCHECKED} ${BIOS_ROM}
    DEPENDS ${BIOS_RAW} romfix romlint ${CMAKE_SOURCE_DIR}/card/vgpu98.card
    COMMENT "Finishing and checking the ROM image"
    VERBATIM)

# Everything the ROM is made from: definitions, fonts, BIOS sources, the tools,
# every build script and the toolchain pins.
file(GLOB_RECURSE MANIFEST_INPUTS CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/card/*.card
    ${CMAKE_SOURCE_DIR}/fonts/*.fnt
    ${CMAKE_SOURCE_DIR}/bios/*.asm
    ${CMAKE_SOURCE_DIR}/bios/*.inc
    ${CMAKE_SOURCE_DIR}/tools/*.c
    ${CMAKE_SOURCE_DIR}/tools/*.h
    ${CMAKE_SOURCE_DIR}/tools/*.asm
    ${CMAKE_SOURCE_DIR}/tools/*.card
    ${CMAKE_SOURCE_DIR}/tools/CMakeLists.txt
    ${CMAKE_SOURCE_DIR}/cmake/*.cmake
    ${CMAKE_SOURCE_DIR}/scripts/*.sh)
list(APPEND MANIFEST_INPUTS
    ${CMAKE_SOURCE_DIR}/card/CMakeLists.txt
    ${CMAKE_SOURCE_DIR}/fonts/CMakeLists.txt
    ${CMAKE_SOURCE_DIR}/bios/CMakeLists.txt
    ${CMAKE_SOURCE_DIR}/toolchain.lock
    ${CMAKE_SOURCE_DIR}/CMakeLists.txt
    ${CMAKE_SOURCE_DIR}/CMakePresets.json)

# The input list goes to the manifest tool in a file: on the command line it
# passes cmd.exe's 8191-character limit in a deep build directory.
list(JOIN MANIFEST_INPUTS "\n" manifest_input_lines)
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/manifest-inputs.txt CONTENT "${manifest_input_lines}\n")

execute_process(COMMAND ${NASM_EXECUTABLE} -v OUTPUT_VARIABLE nasm_version OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${CMAKE_MAKE_PROGRAM} --version OUTPUT_VARIABLE make_version OUTPUT_STRIP_TRAILING_WHITESPACE)

add_custom_command(
    OUTPUT ${BIOS_MANIFEST}
    COMMAND manifest ${BIOS_MANIFEST}
            --source-root ${CMAKE_SOURCE_DIR} --build-root ${CMAKE_BINARY_DIR}
            --note "nasm: ${nasm_version}"
            --note "cmake: ${CMAKE_VERSION}"
            --note "build tool: ${CMAKE_GENERATOR} ${make_version}"
            --note "tools compiler: ${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}"
            --tool nasm ${NASM_EXECUTABLE}
            --inputs-from ${CMAKE_CURRENT_BINARY_DIR}/manifest-inputs.txt
            --outputs ${BIOS_ROM} ${VGPU98_GEN_DIR}/vid_vgpu98_card.h
    DEPENDS ${BIOS_ROM} ${MANIFEST_INPUTS} ${CMAKE_CURRENT_BINARY_DIR}/manifest-inputs.txt ${NASM_EXECUTABLE} manifest
    COMMENT "Writing the build manifest"
    VERBATIM)

add_custom_target(rom ALL DEPENDS ${BIOS_ROM} ${BIOS_MANIFEST})

# Copies the ROM to where 86Box looks for it. scripts/release.sh runs this.
set(VGPU98_STAGE_DIR ${CMAKE_SOURCE_DIR}/bin CACHE PATH "86Box directory that holds roms/")
add_custom_target(stage-rom
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VGPU98_STAGE_DIR}/roms/video/vgpu98
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BIOS_ROM} ${VGPU98_STAGE_DIR}/roms/video/vgpu98/vgpu98.bin
    DEPENDS rom
    COMMENT "Copying the ROM to ${VGPU98_STAGE_DIR}/roms/video/vgpu98/"
    VERBATIM)

# Runs POST and INT 10h on the host under Unicorn (tools/biostest).
if(TARGET biostest)
    add_test(NAME bios_on_host COMMAND biostest ${BIOS_ROM} ${VGPU98_GEN_DIR}
             ${CMAKE_SOURCE_DIR}/card/display-modes.card)
endif()

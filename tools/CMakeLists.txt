add_library(vgpu98_common STATIC
    common/util.c
    common/cfg.c
    common/sha256.c
    common/modes.c
    common/edid.c)
target_include_directories(vgpu98_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)

add_executable(cardc
    cardc/cardc.c
    cardc/palettes.c)
target_link_libraries(cardc PRIVATE vgpu98_common)

add_executable(fontc
    fontc/fontc.c)
target_link_libraries(fontc PRIVATE vgpu98_common)

add_executable(romfix
    romfix/romfix.c)
target_link_libraries(romfix PRIVATE vgpu98_common)

add_executable(romlint
    romlint/romlint.c)
target_link_libraries(romlint PRIVATE vgpu98_common)

add_executable(manifest
    manifest/manifest.c)
target_link_libraries(manifest PRIVATE vgpu98_common)

# Driver toolchain (spec/vglink.md, spec/ne-format.md, spec/le-format.md).
add_executable(vglink
    vglink/vglink.c
    vglink/omf.c
    vglink/ne.c
    vglink/le.c
    vglink/pe.c)
target_link_libraries(vglink PRIVATE vgpu98_common)

add_executable(drvlint
    drvlint/drvlint.c)
target_link_libraries(drvlint PRIVATE vgpu98_common)

add_executable(infgen
    infgen/infgen.c)
target_link_libraries(infgen PRIVATE vgpu98_common)

# The shared block's layout for the 16-bit display driver (driver32/shared.h).
add_executable(sbgen
    sbgen/sbgen.c)
target_include_directories(sbgen PRIVATE ${CMAKE_SOURCE_DIR}/driver32)
target_link_libraries(sbgen PRIVATE vgpu98_common)

add_executable(mkdisk
    mkdisk/mkdisk.c)
target_link_libraries(mkdisk PRIVATE vgpu98_common)

# The driver package's CD image (spec/driver-package.md).
add_executable(isogen
    isogen/isogen.c)
target_link_libraries(isogen PRIVATE vgpu98_common)

# The release download's zip (scripts/release.sh), deflated with zlib, linked statically.
set(ZLIB_USE_STATIC_LIBS ON)
find_package(ZLIB REQUIRED)
add_executable(zipgen
    zipgen/zipgen.c)
target_link_libraries(zipgen PRIVATE vgpu98_common ZLIB::ZLIB)

add_subdirectory(tests)

# Host-side BIOS tests need the Unicorn CPU emulator (test-only dependency).
# scripts/release.sh turns VGPU98_REQUIRE_BIOSTEST on, so a release never skips them.
option(VGPU98_REQUIRE_BIOSTEST "Fail the configure step if the host-side BIOS tests cannot be built" OFF)
find_path(UNICORN_INCLUDE_DIR unicorn/unicorn.h)
find_library(UNICORN_LIBRARY unicorn)
if(UNICORN_INCLUDE_DIR AND UNICORN_LIBRARY)
    add_executable(biostest
        biostest/biostest.c
        biostest/vga_model.c
        cardc/palettes.c)
    target_include_directories(biostest PRIVATE cardc ${VGPU98_GEN_DIR})
    add_dependencies(biostest card_gen) # vid_vgpu98_card.h: display engine registers, EDID
    target_include_directories(biostest SYSTEM PRIVATE ${UNICORN_INCLUDE_DIR})
    # Unicorn's hook API takes callbacks as void pointers.
    target_compile_options(biostest PRIVATE -Wno-pedantic)
    target_link_libraries(biostest PRIVATE vgpu98_common ${UNICORN_LIBRARY})

    # The display driver on the host: its own NE loader, Windows stubs, the card model.
    add_executable(drvtest
        drvtest/drvtest.c
        biostest/vga_model.c)
    target_include_directories(drvtest PRIVATE biostest ${VGPU98_GEN_DIR} ${CMAKE_SOURCE_DIR}/driver32)
    target_include_directories(drvtest SYSTEM PRIVATE ${UNICORN_INCLUDE_DIR})
    target_compile_options(drvtest PRIVATE -Wno-pedantic)
    target_link_libraries(drvtest PRIVATE vgpu98_common ${UNICORN_LIBRARY})
    add_dependencies(drvtest card_gen build_stamp) # the build stamp: the escape's answer carries it

    # The mini-VDD on the host: its own LE loader, VMM and VDD stubs, the card model.
    add_executable(vxdtest
        vxdtest/vxdtest.c
        biostest/vga_model.c)
    target_include_directories(vxdtest PRIVATE biostest ${VGPU98_GEN_DIR})
    target_include_directories(vxdtest SYSTEM PRIVATE ${UNICORN_INCLUDE_DIR})
    target_compile_options(vxdtest PRIVATE -Wno-pedantic)
    target_link_libraries(vxdtest PRIVATE vgpu98_common ${UNICORN_LIBRARY})
    add_dependencies(vxdtest card_gen)
elseif(VGPU98_REQUIRE_BIOSTEST)
    message(FATAL_ERROR "Unicorn not found, and VGPU98_REQUIRE_BIOSTEST is on: install mingw-w64-ucrt-x86_64-unicorn (toolchain.lock)")
else()
    message(WARNING "Unicorn not found: the host-side BIOS tests (bios_on_host) are skipped")
endif()

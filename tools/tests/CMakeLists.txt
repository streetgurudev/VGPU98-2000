set(REAL_CARD  ${CMAKE_SOURCE_DIR}/card/vgpu98.card)
set(REAL_MODES ${CMAKE_SOURCE_DIR}/card/vga-modes.card)
set(REAL_DISPLAY ${CMAKE_SOURCE_DIR}/card/display-modes.card)

# --- Unit tests -------------------------------------------------------------

add_executable(test_cfg test_cfg.c)
target_link_libraries(test_cfg PRIVATE vgpu98_common)
add_test(NAME cfg_parser COMMAND test_cfg WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# The parser must reject these files with a clear message (tools/tests/test_cfg_bad.c).
add_executable(test_cfg_bad test_cfg_bad.c)
target_link_libraries(test_cfg_bad PRIVATE vgpu98_common)
foreach(case_expect IN ITEMS
        "bom|byte order mark"
        "unterminated|unterminated string"
        "no_equals|expected 'key = value', found 'just some words'")
    string(REPLACE "|" ";" parts "${case_expect}")
    list(GET parts 0 case)
    list(GET parts 1 expect)
    add_test(NAME cfg_rejects_${case} COMMAND test_cfg_bad ${case} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cfg_rejects_${case} PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cfg_message_${case} COMMAND test_cfg_bad ${case} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(cfg_message_${case} PROPERTIES PASS_REGULAR_EXPRESSION "${expect}")
endforeach()

add_executable(test_palettes test_palettes.c ${CMAKE_SOURCE_DIR}/tools/cardc/palettes.c)
target_include_directories(test_palettes PRIVATE ${CMAKE_SOURCE_DIR}/tools/cardc)
add_test(NAME palettes_standard_values COMMAND test_palettes)

add_executable(test_edid test_edid.c)
target_link_libraries(test_edid PRIVATE vgpu98_common)
add_test(NAME edid_and_modes COMMAND test_edid ${REAL_DISPLAY})

add_executable(test_header test_header.c)
target_include_directories(test_header PRIVATE ${VGPU98_GEN_DIR})
add_dependencies(test_header card_gen)
add_test(NAME generated_header_compiles COMMAND test_header)

# --- Generated files --------------------------------------------------------

add_test(NAME generated_includes_assemble
    COMMAND ${NASM_EXECUTABLE} -f bin -w+error -I ${VGPU98_GEN_DIR}/
            -o ${CMAKE_CURRENT_BINARY_DIR}/gen_includes.bin ${CMAKE_CURRENT_SOURCE_DIR}/gen_includes.asm)

add_test(NAME cardc_deterministic
    COMMAND ${CMAKE_COMMAND}
        -DCARDC=$<TARGET_FILE:cardc> -DCARD=${REAL_CARD} -DMODES=${REAL_MODES} -DDISPLAY=${REAL_DISPLAY}
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/determinism
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cardc-determinism.cmake)

# --- cardc must reject broken definitions ------------------------------------
# Each case copies the real definition with one deliberate mistake, so the cases stay
# in step with the real files. A case fails if its replacement text is no longer found.

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${REAL_CARD} ${REAL_MODES} ${REAL_DISPLAY})
file(READ ${REAL_CARD} real_card_text)
file(READ ${REAL_MODES} real_modes_text)
file(READ ${REAL_DISPLAY} real_display_text)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bad)

function(bad_case name which from to expect)
    if(which STREQUAL "card")
        set(text "${real_card_text}")
    elseif(which STREQUAL "display")
        set(text "${real_display_text}")
    else()
        set(text "${real_modes_text}")
    endif()
    string(FIND "${text}" "${from}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "bad_case ${name}: text to replace not found: ${from}")
    endif()
    string(REPLACE "${from}" "${to}" text "${text}")

    set(card    ${REAL_CARD})
    set(modes   ${REAL_MODES})
    set(display ${REAL_DISPLAY})
    if(which STREQUAL "card")
        set(card ${CMAKE_CURRENT_BINARY_DIR}/bad/${name}.card)
        file(WRITE ${card} "${text}")
    elseif(which STREQUAL "display")
        set(display ${CMAKE_CURRENT_BINARY_DIR}/bad/${name}.card)
        file(WRITE ${display} "${text}")
    else()
        set(modes ${CMAKE_CURRENT_BINARY_DIR}/bad/${name}.card)
        file(WRITE ${modes} "${text}")
    endif()

    add_test(NAME cardc_rejects_${name}
        COMMAND cardc --card ${card} --modes ${modes} --display ${display} --out-dir ${CMAKE_CURRENT_BINARY_DIR}/bad)
    set_tests_properties(cardc_rejects_${name} PROPERTIES WILL_FAIL TRUE)
    add_test(NAME cardc_message_${name}
        COMMAND cardc --card ${card} --modes ${modes} --display ${display} --out-dir ${CMAKE_CURRENT_BINARY_DIR}/bad)
    set_tests_properties(cardc_message_${name} PROPERTIES PASS_REGULAR_EXPRESSION "${expect}")
endfunction()

bad_case(vendor_ffff card
    "vendor_id           = 0x9898" "vendor_id           = 0xFFFF"
    "'vendor_id' = 0xFFFF is out of range")
bad_case(unknown_key card
    "[memory]\n" "[memory]\ncolour = 1\n"
    "unknown key 'colour' in \\[memory\\]")
bad_case(vram_default card
    "vram_default_mb = 128" "vram_default_mb = 12"
    "vram_default_mb must be one of vram_mb")
bad_case(signon_too_long card
    "signon       = \"VGPU98" "signon       = \"0123456789012345678901234567890123456789012345678901234567890123456789 VGPU98"
    "'signon' is [0-9]+ characters long")
bad_case(rom_size_not_pow2 card
    "rom_size     = 32768" "rom_size     = 24576"
    "rom_size must be a power of two")
bad_case(mono_mismatch modes
    "[mode 0x03]\nmodel    = text\nmono     = 0" "[mode 0x03]\nmodel    = text\nmono     = 1"
    "misc bit 0 does not match mono = 1")
bad_case(vga256_palette modes
    "params   = 0x1C\npalettes = vga256" "params   = 0x1C\npalettes = ega64"
    "the vga256 palette goes with model vga256 only")
bad_case(short_crtc modes
    "crtc        = 5F 4F 50 82 54 80 BF 1F 00 41 00 00 00 00 00 00 9C 8E 8F 28 40 96 B9 A3 FF"
    "crtc        = 5F 4F 50 82 54 80 BF 1F 00 41 00 00 00 00 00 00 9C 8E 8F 28 40 96 B9 A3"
    "'crtc' needs exactly 25 bytes, found 24")
bad_case(long_crtc modes
    "crtc        = 5F 4F 50 82 54 80 BF 1F 00 41 00 00 00 00 00 00 9C 8E 8F 28 40 96 B9 A3 FF"
    "crtc        = 5F 4F 50 82 54 80 BF 1F 00 41 00 00 00 00 00 00 9C 8E 8F 28 40 96 B9 A3 FF 00 00"
    "'crtc' needs exactly 25 bytes, found 27")
bad_case(repeated_mode modes
    "[mode 0x12]" "[mode 0x11]"
    "\\[mode 0x11\\] repeated")
bad_case(mode_width display
    "[mode 960x720]" "[mode 961x720]"
    "width 961 must be a multiple of 8")
bad_case(mode_repeated display
    "[mode 1152x864]" "[mode 1024x768]"
    "1024x768 repeated")
bad_case(vbe_twice display
    "vbe8    = 0x105" "vbe8    = 0x101"
    "VBE mode 101h used twice")
bad_case(refresh_range display
    "refresh = 60 75\n\n[mode 1856x1392]" "refresh = 60 175\n\n[mode 1856x1392]"
    "refresh rate 175 Hz is outside 50-160")
bad_case(disabled_vbe display
    "[mode 1280x720]\ndesktop = 1\nenabled = 0" "[mode 1280x720]\ndesktop = 1\nenabled = 0\nvbe8 = 0x1F0"
    "a disabled mode has no VBE numbers")
bad_case(edid_preferred card
    "preferred = 1024x768 75" "preferred = 1024x768 61"
    "EDID preferred mode 1024x768 at 61 Hz is not an enabled desktop mode")
bad_case(display_overlap card
    "first_index = 0x80" "first_index = 0xC0"
    "the display registers overlap the debug register")
bad_case(display_too_tall card
    "max_height  = 1440" "max_height  = 1600"
    "'max_height' = 1600 is out of range 480-1536")
bad_case(reserved_param_used modes
    "params   = 0x1B" "params   = 0x0B"
    "param entry 0x0B is reserved")

# --- The DLL's shader translator, on the host -------------------------------

# DirectX 8 shaders into the GPU's programs (driver32/xlate.c), checked by
# the GPU's own validator (card/vid_vgpu98_prog.h).
add_executable(test_xlate test_xlate.c ${CMAKE_SOURCE_DIR}/driver32/xlate.c)
target_include_directories(test_xlate PRIVATE ${CMAKE_SOURCE_DIR}/card ${CMAKE_SOURCE_DIR}/driver32)
add_test(NAME shader_translator COMMAND test_xlate)

# DirectX 8 vertex shader declarations into DECLARE elements (driver32/vdecl.c).
add_executable(test_vdecl test_vdecl.c ${CMAKE_SOURCE_DIR}/driver32/vdecl.c)
target_include_directories(test_vdecl PRIVATE ${CMAKE_SOURCE_DIR}/card ${CMAKE_SOURCE_DIR}/driver32)
add_test(NAME vertex_declarations COMMAND test_vdecl)

# The Glide device's arithmetic, which the DLLs and 86Box share
# (card/vid_vgpu98_glcpu.h): texture layout and memory,
# texel decoding, the W-buffer float and the fog table as glide.frag has them.
add_executable(test_gltex test_gltex.c)
target_include_directories(test_gltex PRIVATE ${CMAKE_SOURCE_DIR}/card)
add_test(NAME glide_arithmetic COMMAND test_gltex)

# The Display diagnostics' arithmetic, which 86Box shares (card/vid_vgpu98_dispdiag.h):
# svga_poll's frame counting after the timings change
# mid-frame, the stretched test, the pixel decode as display.comp has it, the hash.
add_executable(test_dispdiag test_dispdiag.c)
target_include_directories(test_dispdiag PRIVATE ${CMAKE_SOURCE_DIR}/card)
add_test(NAME display_diagnostics COMMAND test_dispdiag)

# The control program's gamma ramp (cpl/ramp.c): the identity,
# each setting's effect, the channels apart, clamping, and its own power
# function against the C library's.
add_executable(test_ramp test_ramp.c ${CMAKE_SOURCE_DIR}/cpl/ramp.c)
target_include_directories(test_ramp PRIVATE ${CMAKE_SOURCE_DIR}/cpl)
target_link_libraries(test_ramp PRIVATE m)
add_test(NAME gamma_ramp COMMAND test_ramp)

# The control program's dialog templates (cpl/tmpl.c),
# walked from their bytes as Windows reads them: the header and font, the
# item count, items on dwords, both class forms, texts, the template's end,
# room that runs out anywhere, and a page-sized template in a page's buffer.
add_executable(test_tmpl test_tmpl.c ${CMAKE_SOURCE_DIR}/cpl/tmpl.c)
target_include_directories(test_tmpl PRIVATE ${CMAKE_SOURCE_DIR}/cpl)
add_test(NAME cpl_templates COMMAND test_tmpl)

# The tab's class ID in the INF (card/driver.card) is the one cpl/main.c answers.
add_test(NAME cpl_clsid_matches_inf
    COMMAND ${CMAKE_COMMAND} -DCARD=${CMAKE_SOURCE_DIR}/card/driver.card -DSOURCE=${CMAKE_SOURCE_DIR}/cpl/main.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cpl-clsid-check.cmake)

# The Glide DLLs' card-found path: glide/card.c and
# each DLL's own sources with stand-ins for GDI's escape, KERNEL32 and the log
# (test_glide.c), the shared block and BAR0 in memory below 4 GB
# (test_glide_low.c). Each scenario is a process of its own.
foreach(dll glide2x glide3x)
    file(GLOB glide_sources CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/glide/${dll}/*.c ${CMAKE_SOURCE_DIR}/glide/*.c)
    add_executable(test_${dll} test_glide.c test_glide_low.c ${CMAKE_SOURCE_DIR}/driver32/logline.c
                   ${CMAKE_SOURCE_DIR}/driver32/gpu.c ${glide_sources})
    target_include_directories(test_${dll} PRIVATE ${CMAKE_SOURCE_DIR}/glide/${dll} ${CMAKE_SOURCE_DIR}/glide
                               ${CMAKE_SOURCE_DIR}/driver32 ${VGPU98_GEN_DIR} ${CMAKE_SOURCE_DIR}/card)
    # The DLLs' stdcall and their 32-bit addresses mean nothing to the 64-bit host;
    # the stand-ins have names of their own, apart from the C runtime's imports.
    target_compile_definitions(test_${dll} PRIVATE DLLIMPORT= VGPU98_GLIDE_DLL LOG_PUT_HOOK=test_put
        REG_WRITE_HOOK=test_reg_write Sleep=test_Sleep GetTickCount=test_GetTickCount
        GetCurrentProcessId=test_GetCurrentProcessId GetCurrentThreadId=test_GetCurrentThreadId
        GetActiveWindow=test_GetActiveWindow GetForegroundWindow=test_GetForegroundWindow
        GetClassNameA=test_GetClassNameA GetWindowTextA=test_GetWindowTextA GetWindowRect=test_GetWindowRect
        GetWindowLongA=test_GetWindowLongA GetWindowThreadProcessId=test_GetWindowThreadProcessId
        DirectDrawCreate=test_DirectDrawCreate
        GetModuleFileNameA=test_GetModuleFileNameA
        GetSystemDirectoryA=test_GetSystemDirectoryA GetEnvironmentStringsA=test_GetEnvironmentStringsA
        FreeEnvironmentStringsA=test_FreeEnvironmentStringsA CreateDCA=test_CreateDCA ExtEscape=test_ExtEscape
        DeleteDC=test_DeleteDC CreateFileA=test_CreateFileA ReadFile=test_ReadFile SetFilePointer=test_SetFilePointer
        CloseHandle=test_CloseHandle SetUnhandledExceptionFilter=test_SetUnhandledExceptionFilter
        IsBadReadPtr=test_IsBadReadPtr)
    target_compile_options(test_${dll} PRIVATE -Wno-attributes -Wno-int-to-pointer-cast -Wno-pedantic)
    add_dependencies(test_${dll} card_gen build_stamp)
endforeach()
target_compile_definitions(test_glide3x PRIVATE TEST_GLIDE3)
foreach(scenario glide minimal directx stale head other none long folder screen draw snap bad textures recorder fault
        nomode unload free toobig unknown desktop held restart lost quiet)
    add_test(NAME glide2x_${scenario} COMMAND test_glide2x ${scenario})
endforeach()
foreach(scenario glide screen draw textures)
    add_test(NAME glide3x_${scenario} COMMAND test_glide3x ${scenario})
endforeach()

# The OpenGL ICD: opengl/'s sources and the ring's,
# with stand-ins for KERNEL32, GDI's escape, USER32's windows, DirectDraw's
# clippers and the GPU (test_ogl.c), the shared block, BAR0 and video memory
# below 4 GB (test_glide_low.c). Each scenario is a process of its own.
file(GLOB ogl_sources CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/opengl/*.c)
add_executable(test_ogl test_ogl.c test_glide_low.c ${CMAKE_SOURCE_DIR}/driver32/gpu.c ${CMAKE_SOURCE_DIR}/driver32/logline.c
               ${ogl_sources})
target_include_directories(test_ogl PRIVATE ${CMAKE_SOURCE_DIR}/opengl ${CMAKE_SOURCE_DIR}/driver32 ${VGPU98_GEN_DIR}
                           ${CMAKE_SOURCE_DIR}/card)
target_compile_definitions(test_ogl PRIVATE DLLIMPORT= VGPU98_ICD_DLL REG_WRITE_HOOK=test_reg_write
    TlsAlloc=test_TlsAlloc TlsFree=test_TlsFree TlsGetValue=test_TlsGetValue TlsSetValue=test_TlsSetValue
    GetProcessHeap=test_GetProcessHeap HeapAlloc=test_HeapAlloc HeapReAlloc=test_HeapReAlloc HeapFree=test_HeapFree
    Sleep=test_Sleep SetLastError=test_SetLastError GetTickCount=test_GetTickCount GetCurrentProcessId=test_GetCurrentProcessId
    GetCurrentThreadId=test_GetCurrentThreadId CreateDCA=test_CreateDCA ExtEscape=test_ExtEscape DeleteDC=test_DeleteDC
    WindowFromDC=test_WindowFromDC GetClientRect=test_GetClientRect ClientToScreen=test_ClientToScreen
    IsWindow=test_IsWindow DirectDrawCreate=test_DirectDrawCreate GetModuleFileNameA=test_GetModuleFileNameA
    GetSystemDirectoryA=test_GetSystemDirectoryA SetUnhandledExceptionFilter=test_SetUnhandledExceptionFilter
    IsBadReadPtr=test_IsBadReadPtr LOG_PUT_HOOK=test_log_put)
target_compile_options(test_ogl PRIVATE -Wno-attributes -Wno-int-to-pointer-cast -Wno-pedantic)
target_link_libraries(test_ogl PRIVATE m)
add_dependencies(test_ogl card_gen build_stamp)
foreach(scenario none noogl formats context clear swap resize held restart detach threads rehwnd queries attrib lists select
        feedback eval rasterpos errors texobj draw cpudraw colormat teximage pixels readpixels share front ext ext11 swapint swapint1 swapint0 cva cva2 multitex multitex2 pointparam palette palette2 cross gate25
        diag diagmin diagoff diagstale diaghead diagother diagnoogl edgeclamp ext26 patience)
    add_test(NAME ogl_${scenario} COMMAND test_ogl ${scenario})
endforeach()

# The DLL's heap allocator (driver32/heapcore.c), the heap checked whole
# through random allocations and frees.
add_executable(test_heap test_heap.c ${CMAKE_SOURCE_DIR}/driver32/heapcore.c)
target_include_directories(test_heap PRIVATE ${CMAKE_SOURCE_DIR}/driver32)
add_test(NAME heap_allocator COMMAND test_heap)

# --- ROM tools ---------------------------------------------------------------

add_executable(test_sha256 test_sha256.c)
target_link_libraries(test_sha256 PRIVATE vgpu98_common)
add_test(NAME sha256_vectors COMMAND test_sha256)

# romlint must accept a valid image and reject each kind of broken one.
function(romlint_case name define fix expect)
    add_test(NAME romlint_${name}
        COMMAND ${CMAKE_COMMAND}
            -DNASM=${NASM_EXECUTABLE} -DROMFIX=$<TARGET_FILE:romfix> -DROMLINT=$<TARGET_FILE:romlint>
            -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/lint_rom.asm -DCARD=${CMAKE_CURRENT_SOURCE_DIR}/data/lint.card
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/romlint/${name} -DDEFINE=${define} -DFIX=${fix}
            "-DEXPECT=${expect}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/romlint-case.cmake)
endfunction()

romlint_case(accepts_valid      ""              ON  "pass")
romlint_case(accepts_pnp        WITH_PNP        ON  "pass")
romlint_case(rejects_signature  BAD_SIGNATURE   ON  "missing 55h AAh signature")
romlint_case(rejects_entry      BAD_ENTRY       ON  "entry point at offset 3 is not a jump")
romlint_case(rejects_vendor     BAD_VENDOR      ON  "PCIR IDs 1234:2000 do not match")
romlint_case(rejects_alignment  UNALIGNED_PCIR  ON  "must be 4-byte aligned")
romlint_case(rejects_checksum   WRONG_CHECKSUM  OFF "checksum: bytes sum to")
romlint_case(rejects_pnp_sum    BAD_PNP_SUM     ON  "PnP header checksum")
romlint_case(rejects_entry_pcir   ENTRY_IN_PCIR   ON "inside the PCI data structure")
romlint_case(rejects_entry_header ENTRY_IN_HEADER ON "inside the ROM header")
romlint_case(rejects_indicator    BAD_INDICATOR   ON "PCIR indicator must be 80h")
romlint_case(rejects_reserved     BAD_RESERVED    ON "PCIR reserved word")
romlint_case(rejects_dirty_header HEADER_DIRTY    ON "reserved header bytes")
romlint_case(rejects_vga_sig      BAD_VGA_SIG     ON "must carry")
romlint_case(rejects_size_byte    BAD_SIZE_BYTE   ON "size byte says 1024 bytes")
romlint_case(rejects_image_len    BAD_IMAGE_LEN   ON "PCIR image length says 1024 bytes")
romlint_case(rejects_class        BAD_CLASS       ON "PCIR class code 038000 does not match")
romlint_case(rejects_code_type    BAD_CODE_TYPE   ON "PCIR code type must be 0")

# --- Font independence --------------------------------------------------------
# Compares the fonts with those in a reference VGA BIOS image and fails if more
# glyphs are identical than tools/tests/test_font_similarity.c allows. The image is
# only read, never copied; without it the check is skipped.
set(VGPU98_REFERENCE_VGA_ROM ${CMAKE_SOURCE_DIR}/bin/roms/video/vga/ibm_vga.bin
    CACHE FILEPATH "Reference VGA BIOS image for the font independence check (optional)")
add_executable(test_font_similarity test_font_similarity.c)
target_link_libraries(test_font_similarity PRIVATE vgpu98_common)
if(EXISTS ${VGPU98_REFERENCE_VGA_ROM})
    add_test(NAME fonts_independent COMMAND test_font_similarity ${VGPU98_REFERENCE_VGA_ROM} ${VGPU98_GEN_DIR})
else()
    message(STATUS "Font independence check skipped: no reference VGA BIOS at ${VGPU98_REFERENCE_VGA_ROM}")
endif()

# --- Driver toolchain: vglink and drvlint -------------------------------------
# The test modules in vglink/ are assembled, linked to an NE and an LE file, and
# loaded by test_vglink's own loaders, which check every relocated value against
# the offsets worked out in the .asm files.

set(VGLINK_SRC ${CMAKE_CURRENT_SOURCE_DIR}/vglink)
set(VGLINK_OUT ${CMAKE_CURRENT_BINARY_DIR}/vglink)
file(MAKE_DIRECTORY ${VGLINK_OUT})

function(vglink_object name src)
    add_custom_command(
        OUTPUT ${VGLINK_OUT}/${name}.obj
        COMMAND ${NASM_EXECUTABLE} -f obj -w+error ${ARGN} -o ${VGLINK_OUT}/${name}.obj ${VGLINK_SRC}/${src}
        DEPENDS ${VGLINK_SRC}/${src}
        VERBATIM)
endfunction()

vglink_object(ne_a ne_a.asm)
vglink_object(ne_b ne_b.asm)
vglink_object(le le.asm)
vglink_object(bad16 bad16.asm)
foreach(bad UNDEFINED IMPORT32 IMPORT_PLUS NEAR_OTHER BIG UNPLACED TWICE)
    vglink_object(bad16_${bad} bad16.asm -DBAD_${bad})
    list(APPEND bad_objects ${VGLINK_OUT}/bad16_${bad}.obj)
endforeach()

add_custom_command(
    OUTPUT ${VGLINK_OUT}/test.drv ${VGLINK_OUT}/test.map
    COMMAND vglink -d ${VGLINK_SRC}/ne.lnk -o ${VGLINK_OUT}/test.drv -m ${VGLINK_OUT}/test.map
            ${VGLINK_OUT}/ne_a.obj ${VGLINK_OUT}/ne_b.obj
    DEPENDS vglink ${VGLINK_SRC}/ne.lnk ${VGLINK_SRC}/res1.bin ${VGLINK_SRC}/res2.bin
            ${VGLINK_OUT}/ne_a.obj ${VGLINK_OUT}/ne_b.obj
    VERBATIM)
add_custom_command(
    OUTPUT ${VGLINK_OUT}/test.vxd
    COMMAND vglink -d ${VGLINK_SRC}/le.lnk -o ${VGLINK_OUT}/test.vxd ${VGLINK_OUT}/le.obj
    DEPENDS vglink ${VGLINK_SRC}/le.lnk ${VGLINK_OUT}/le.obj
    VERBATIM)
# The test VxD again without its version resource, which drvlint --strict
# --vxd must refuse.
add_custom_command(
    OUTPUT ${VGLINK_OUT}/test-noversion.vxd
    COMMAND vglink -d ${VGLINK_SRC}/le-noversion.lnk -o ${VGLINK_OUT}/test-noversion.vxd ${VGLINK_OUT}/le.obj
    DEPENDS vglink ${VGLINK_SRC}/le-noversion.lnk ${VGLINK_OUT}/le.obj
    VERBATIM)
add_custom_target(vglink_test_modules ALL
    DEPENDS ${VGLINK_OUT}/test.drv ${VGLINK_OUT}/test.vxd ${VGLINK_OUT}/test-noversion.vxd ${VGLINK_OUT}/bad16.obj
            ${bad_objects})

add_executable(test_vglink test_vglink.c)
target_link_libraries(test_vglink PRIVATE vgpu98_common)
add_test(NAME vglink_loads_ne_and_le
    COMMAND test_vglink ${VGLINK_OUT}/test.drv ${VGLINK_OUT}/test.vxd ${VGLINK_SRC})
add_test(NAME drvlint_strict_ne COMMAND drvlint --strict ${VGLINK_OUT}/test.drv)
add_test(NAME drvlint_strict_le COMMAND drvlint --strict --vxd ${VGLINK_OUT}/test.vxd)
add_test(NAME drvlint_vxd_needs_version COMMAND drvlint --strict --vxd ${VGLINK_OUT}/test-noversion.vxd)
set_tests_properties(drvlint_vxd_needs_version PROPERTIES PASS_REGULAR_EXPRESSION "no version resource")
add_test(NAME vglink_deterministic
    COMMAND ${CMAKE_COMMAND} -DVGLINK=$<TARGET_FILE:vglink> -DSRC=${VGLINK_SRC} -DOBJ_DIR=${VGLINK_OUT}
            -DWORK=${VGLINK_OUT}/determinism -P ${CMAKE_CURRENT_SOURCE_DIR}/vglink-determinism.cmake)

# vglink must refuse what it cannot link correctly, with a clear message.
function(vglink_rejects name lnk obj expect)
    add_test(NAME vglink_rejects_${name}
        COMMAND vglink -d ${VGLINK_SRC}/${lnk} -o ${VGLINK_OUT}/bad_${name}.out ${VGLINK_OUT}/${obj}.obj)
    set_tests_properties(vglink_rejects_${name} PROPERTIES PASS_REGULAR_EXPRESSION "${expect}")
endfunction()
vglink_rejects(undefined   bad16.lnk        bad16_UNDEFINED   "undefined symbol Missing")
vglink_rejects(import32    bad16.lnk        bad16_IMPORT32    "a 32-bit offset to an import")
vglink_rejects(import_plus bad16.lnk        bad16_IMPORT_PLUS "an import plus an offset")
vglink_rejects(near_other  bad16-other.lnk  bad16_NEAR_OTHER  "near reference to another segment")
vglink_rejects(too_big     bad16.lnk        bad16_BIG         "NE segments hold at most 64 KB")
vglink_rejects(unplaced    bad16.lnk        bad16_UNPLACED    "segment _EXTRA is not placed")
vglink_rejects(twice       bad16-twice.lnk  bad16_TWICE       "symbol ImpFar2 is defined twice")
vglink_rejects(export      bad16-export.lnk bad16             "symbol NoSuchSymbol is not defined")
vglink_rejects(le_16bit    bad16-le.lnk     bad16             "LE output takes 32-bit segments")
vglink_rejects(le_version  bad-le-version.lnk le             "a VxD's file_type is 5")

# A per-process PE DLL with its export names as written, from a list file,
# and a version resource, as a Glide DLL is linked:
# linked twice for determinism and checked by drvlint (vglink-pe.cmake).
add_custom_command(
    OUTPUT ${VGLINK_OUT}/pe.obj
    COMMAND ${NASM_EXECUTABLE} -f win32 -w+error -o ${VGLINK_OUT}/pe.obj ${VGLINK_SRC}/pe.asm
    DEPENDS ${VGLINK_SRC}/pe.asm
    VERBATIM)
add_custom_command(
    OUTPUT ${VGLINK_OUT}/test.dll
    COMMAND vglink -d ${VGLINK_SRC}/pe.lnk -o ${VGLINK_OUT}/test.dll ${VGLINK_OUT}/pe.obj
    DEPENDS vglink ${VGLINK_SRC}/pe.lnk ${VGLINK_SRC}/pe.exports ${VGLINK_OUT}/pe.obj
    VERBATIM)
add_custom_target(vglink_test_pe ALL DEPENDS ${VGLINK_OUT}/pe.obj ${VGLINK_OUT}/test.dll)
add_test(NAME vglink_pe_dll
    COMMAND ${CMAKE_COMMAND} -DVGLINK=$<TARGET_FILE:vglink> -DDRVLINT=$<TARGET_FILE:drvlint> -DSRC=${VGLINK_SRC}
            -DOBJ=${VGLINK_OUT}/pe.obj -DWORK=${VGLINK_OUT}/pe -P ${CMAKE_CURRENT_SOURCE_DIR}/vglink-pe.cmake)
vglink_rejects(pe_names    bad-pe-names.lnk   pe "'names' is undecorated or decorated")
vglink_rejects(pe_both     bad-pe-both.lnk    pe "takes either 'symbols' or 'file'")
vglink_rejects(pe_missing  bad-pe-missing.lnk pe "export _TestThree@4: symbol _TestThree@4 is not defined")
# drvlint's export check names what the table lacks.
add_test(NAME drvlint_exports_missing
    COMMAND drvlint --exports ${VGLINK_SRC}/pe-missing.exports ${VGLINK_OUT}/test.dll)
set_tests_properties(drvlint_exports_missing PROPERTIES
    PASS_REGULAR_EXPRESSION "export _TestThree@4 \\(in [^)]*pe-missing.exports\\) is missing")
# drvlint --glide refuses what a Glide DLL must not be: shared sections, a C
# runtime's import, no version resource (the test DLL linked three ways).
set(glide_refusals)
foreach(case_expect IN ITEMS
        "shared|section .text is shared: a Glide DLL's data belongs to its process"
        "import|imports from MSVCRT.dll: a Glide DLL imports only from Windows' own"
        "noversion|no version resource")
    string(REPLACE "|" ";" parts "${case_expect}")
    list(GET parts 0 case)
    list(GET parts 1 expect)
    add_custom_command(
        OUTPUT ${VGLINK_OUT}/glide-${case}.dll
        COMMAND vglink -d ${VGLINK_SRC}/glide-${case}.lnk -o ${VGLINK_OUT}/glide-${case}.dll ${VGLINK_OUT}/pe.obj
        DEPENDS vglink ${VGLINK_SRC}/glide-${case}.lnk ${VGLINK_SRC}/pe.exports ${VGLINK_OUT}/pe.obj
        VERBATIM)
    list(APPEND glide_refusals ${VGLINK_OUT}/glide-${case}.dll)
    add_test(NAME drvlint_glide_refuses_${case} COMMAND drvlint --glide ${VGLINK_OUT}/glide-${case}.dll)
    set_tests_properties(drvlint_glide_refuses_${case} PROPERTIES PASS_REGULAR_EXPRESSION "${expect}")
endforeach()
add_custom_target(vglink_test_glide_refusals ALL DEPENDS ${glide_refusals})

# drvlint against Windows' own files, which Windows accepts: its default checks must
# pass them. The files are read in place from the local reference library, never
# copied; without it the checks are skipped.
set(VGPU98_REFERENCE_LIBRARY "${CMAKE_SOURCE_DIR}/archive/research_notes/Win98 virtual GPU reference library/originals"
    CACHE PATH "Local reference library with Windows driver files for drvlint's checks (optional)")
set(ddk_setup "${VGPU98_REFERENCE_LIBRARY}/Microsoft DDK and SDK/98ddk (extracted)/_setup")
set(voodoo3 "${VGPU98_REFERENCE_LIBRARY}/3dfx Voodoo3 Win9x drivers/voodoo3-10700 (extracted)/Voodoo3-10700/driver9x")
if(EXISTS "${ddk_setup}/DIBENG.DLL" AND EXISTS "${voodoo3}/3dfx16v3.drv")
    add_test(NAME drvlint_windows_ne COMMAND drvlint "${ddk_setup}/DIBENG.DLL")
    add_test(NAME drvlint_windows_display COMMAND drvlint --display "${voodoo3}/3dfx16v3.drv")
    add_test(NAME drvlint_windows_vxd
        COMMAND drvlint --vxd "${ddk_setup}/VDD.VXD" "${ddk_setup}/BIOS.VXD" "${ddk_setup}/VXDLDR.VXD"
                "${voodoo3}/3dfxv3.vxd"
                "${VGPU98_REFERENCE_LIBRARY}/Microsoft DDK and SDK/98ddk (extracted)/DX_DDK/SRC_DISPLAY_VDD_S3_S3.VXD")
    # The Voodoo3's own Glide DLLs pass the Glide checks, and their export
    # tables are exactly our lists.
    add_test(NAME drvlint_windows_glide2x
        COMMAND drvlint --glide --exports ${CMAKE_SOURCE_DIR}/glide/glide2x/glide2x.exports "${voodoo3}/glide2x.dll")
    add_test(NAME drvlint_windows_glide3x
        COMMAND drvlint --glide --exports ${CMAKE_SOURCE_DIR}/glide/glide3x/glide3x.exports "${voodoo3}/glide3x.dll")
else()
    message(STATUS "drvlint checks against Windows files skipped: no reference library at ${VGPU98_REFERENCE_LIBRARY}")
endif()

# --- Driver package: infgen and mkdisk -----------------------------------------

set(REAL_DRIVER ${CMAKE_SOURCE_DIR}/card/driver.card)
add_test(NAME infgen_writes_the_inf
    COMMAND ${CMAKE_COMMAND} -DINFGEN=$<TARGET_FILE:infgen> -DDRIVER=${REAL_DRIVER} -DCARD=${REAL_CARD}
            -DMODES=${REAL_DISPLAY} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/infgen -P ${CMAKE_CURRENT_SOURCE_DIR}/infgen-check.cmake)

# infgen must reject a package definition with a mistake.
file(READ ${REAL_DRIVER} real_driver_text)
function(infgen_rejects name from to expect)
    string(FIND "${real_driver_text}" "${from}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "infgen_rejects ${name}: text to replace not found: ${from}")
    endif()
    string(REPLACE "${from}" "${to}" text "${real_driver_text}")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bad/driver_${name}.card "${text}")
    add_test(NAME infgen_rejects_${name}
        COMMAND infgen --driver ${CMAKE_CURRENT_BINARY_DIR}/bad/driver_${name}.card --card ${REAL_CARD}
                --modes ${REAL_DISPLAY} -o ${CMAKE_CURRENT_BINARY_DIR}/bad/driver_${name}.inf)
    set_tests_properties(infgen_rejects_${name} PROPERTIES PASS_REGULAR_EXPRESSION "${expect}")
endfunction()
infgen_rejects(default_mode "default_mode   = 8 640 480" "default_mode   = 8 1280 720" "1280x720 is not a desktop mode")
infgen_rejects(depth        "default_mode   = 8 640 480" "default_mode   = 24 640 480" "default depth must be 8, 16 or 32")
infgen_rejects(file_name    "minivdd        = vgpu98.vxd" "minivdd        = vgpu98-minivdd.vxd" "must be an 8.3 file name")
infgen_rejects(glide_name   "glide3_driver  = glide3x.dll" "glide3_driver  = Glide3x.dll" "write file names in lower case")
infgen_rejects(date         "date           = 10/10/2026" "date           = 2026-10-10" "unexpected character '-'")
infgen_rejects(clsid        "control_clsid  = {79D3DDE2-6AC4" "control_clsid  = {79d3dde2-6AC4" "must be a class ID")

# A disk with files of several sizes (one longer than many clusters), read back.
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/test-disk.img
    COMMAND mkdisk -o ${CMAKE_CURRENT_BINARY_DIR}/test-disk.img -l VGPU98TEST -d 2026-10-10
            ${VGLINK_OUT}/test.drv ${VGLINK_OUT}/test.vxd ${VGLINK_SRC}/res1.bin
    DEPENDS mkdisk ${VGLINK_OUT}/test.drv ${VGLINK_OUT}/test.vxd ${VGLINK_SRC}/res1.bin
    VERBATIM)
add_custom_target(mkdisk_test_image ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/test-disk.img)
add_executable(test_mkdisk test_mkdisk.c)
target_link_libraries(test_mkdisk PRIVATE vgpu98_common)
add_test(NAME mkdisk_reads_back
    COMMAND test_mkdisk ${CMAKE_CURRENT_BINARY_DIR}/test-disk.img VGPU98TEST 2026-10-10
            ${VGLINK_OUT}/test.drv ${VGLINK_OUT}/test.vxd ${VGLINK_SRC}/res1.bin)
# The release zip's writer: the same archive every time, and no name that could
# land outside the folder it is unpacked into.
add_test(NAME zipgen_deterministic
    COMMAND ${CMAKE_COMMAND} -DZIPGEN=$<TARGET_FILE:zipgen> -DSRC=${CMAKE_CURRENT_SOURCE_DIR}
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/zipgen -P ${CMAKE_CURRENT_SOURCE_DIR}/zipgen-determinism.cmake)
add_test(NAME zipgen_rejects_parent_name
    COMMAND zipgen -o ${CMAKE_CURRENT_BINARY_DIR}/bad/parent.zip -t 0 top/../../res1.bin=${VGLINK_SRC}/res1.bin)
set_tests_properties(zipgen_rejects_parent_name PROPERTIES PASS_REGULAR_EXPRESSION "'\\.' or '\\.\\.' part")

add_test(NAME mkdisk_rejects_long_name
    COMMAND mkdisk -o ${CMAKE_CURRENT_BINARY_DIR}/bad/long.img -l VGPU98 -d 2026-10-10 ${CMAKE_CURRENT_SOURCE_DIR}/vglink-determinism.cmake)
set_tests_properties(mkdisk_rejects_long_name PROPERTIES PASS_REGULAR_EXPRESSION "not an 8.3 file name")

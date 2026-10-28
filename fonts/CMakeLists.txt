set(FONT_PREVIEW_DIR ${CMAKE_BINARY_DIR}/font-previews)
file(MAKE_DIRECTORY ${FONT_PREVIEW_DIR})

set(FONT_INCS)

# compile_font(NAME LABEL [EXTRA_DEPENDS...]): fonts/NAME.fnt -> build/gen/LABEL.inc,
# plus a preview sheet in build/font-previews/NAME.png.
function(compile_font name label)
    set(src ${CMAKE_CURRENT_SOURCE_DIR}/${name}.fnt)
    set(inc ${VGPU98_GEN_DIR}/${label}.inc)
    set(png ${FONT_PREVIEW_DIR}/${name}.png)
    add_custom_command(
        OUTPUT ${inc} ${png}
        COMMAND fontc ${src} --inc ${inc} --label ${label} --png ${png}
        DEPENDS fontc ${src} ${ARGN}
        COMMENT "Compiling font ${name}"
        VERBATIM)
    set(FONT_INCS ${FONT_INCS} ${inc} PARENT_SCOPE)
endfunction()

compile_font(vga8x16 font8x16)
compile_font(vga8x14 font8x14 ${CMAKE_CURRENT_SOURCE_DIR}/vga8x16.fnt)
compile_font(vga8x8 font8x8)
add_custom_target(fonts_gen ALL DEPENDS ${FONT_INCS})

# --- Tests ------------------------------------------------------------------

add_test(NAME fontc_deterministic
    COMMAND ${CMAKE_COMMAND}
        -DFONTC=$<TARGET_FILE:fontc> -DFONT=${CMAKE_CURRENT_SOURCE_DIR}/vga8x14.fnt
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/determinism
        -P ${CMAKE_SOURCE_DIR}/tools/tests/fontc-determinism.cmake)

# fontc must reject broken fonts. Each case copies the real 8x8 font with one mistake.
set(REAL_FONT ${CMAKE_CURRENT_SOURCE_DIR}/vga8x8.fnt)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${REAL_FONT})
file(READ ${REAL_FONT} real_font_text)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bad)

function(bad_font name regex replacement expect)
    string(REGEX MATCH "${regex}" found "${real_font_text}")
    if(NOT found)
        message(FATAL_ERROR "bad_font ${name}: pattern not found: ${regex}")
    endif()
    string(REGEX REPLACE "${regex}" "${replacement}" text "${real_font_text}")
    set(file ${CMAKE_CURRENT_BINARY_DIR}/bad/${name}.fnt)
    file(WRITE ${file} "${text}")
    add_test(NAME fontc_rejects_${name}
        COMMAND fontc ${file} --inc ${CMAKE_CURRENT_BINARY_DIR}/bad/${name}.inc --label x)
    set_tests_properties(fontc_rejects_${name} PROPERTIES WILL_FAIL TRUE)
    add_test(NAME fontc_message_${name}
        COMMAND fontc ${file} --inc ${CMAKE_CURRENT_BINARY_DIR}/bad/${name}.inc --label x)
    set_tests_properties(fontc_message_${name} PROPERTIES PASS_REGULAR_EXPRESSION "${expect}")
endfunction()

bad_font(missing_glyph "glyph 41 A\n([.#]+\n)+" "" "glyph 41h is missing")
bad_font(duplicate_glyph "glyph 41 A\n" "glyph 42 A\n" "glyph 42h: defined twice")
bad_font(wide_row "glyph 41 A\n[.]+###[.]+\n" "glyph 41 A\n..###....\n" "pixel rows are exactly 8 characters")
bad_font(column_7 "glyph 41 A\n[.]+###[.]+\n" "glyph 41 A\n..###..#\n" "printable ASCII must leave column 7 clear")
bad_font(box_edge "(glyph C4 @3 light horizontal\n)########" "\\1#######." "glyph C4h: right edge does not match")
bad_font(glyph_sign "glyph 41 A\n" "glyph +1 A\n" "two-digit hex code")
bad_font(unknown_line "glyph 41 A\n" "glyf 41 A\n" "unknown line 'glyf 41 A'")

# Deriving the 8x14 font must not cut pixels off a glyph: give the 8x16 'A' a mark on
# row 0, which the derive line drops.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/vga8x16.fnt real_16_text)
string(FIND "${real_16_text}" "glyph 41 @3 A\n" pos)
if(pos EQUAL -1)
    message(FATAL_ERROR "derive-loss test: 'glyph 41 @3 A' not found in vga8x16.fnt")
endif()
string(REPLACE "glyph 41 @3 A\n" "glyph 41 @0 A\n###.....\n........\n........\n" lost_16_text "${real_16_text}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bad/lost16.fnt "${lost_16_text}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bad/lost14.fnt "font 8x14\nderive lost16.fnt drop-rows 0 15\nlines 6 5 7 3 2 4\n")
add_test(NAME fontc_message_derive_loss
    COMMAND fontc ${CMAKE_CURRENT_BINARY_DIR}/bad/lost14.fnt --inc ${CMAKE_CURRENT_BINARY_DIR}/bad/lost14.inc --label x)
set_tests_properties(fontc_message_derive_loss PROPERTIES PASS_REGULAR_EXPRESSION "glyph 41h: deriving drops row 0")

# A font that derives from itself must stop with a message, not recurse forever.
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/bad/self_derive.fnt "font 8x14\nderive self_derive.fnt drop-rows 0 15\n")
add_test(NAME fontc_message_self_derive
    COMMAND fontc ${CMAKE_CURRENT_BINARY_DIR}/bad/self_derive.fnt --inc ${CMAKE_CURRENT_BINARY_DIR}/bad/self_derive.inc --label x)
set_tests_properties(fontc_message_self_derive PROPERTIES PASS_REGULAR_EXPRESSION "chains more than 4 fonts deep")
bad_font(half_block "(glyph DC @4 lower half block\n)########" "\\1#######." "lower half block must fill exactly the lower half")

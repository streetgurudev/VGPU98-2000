; Assembles the generated BIOS include files on their own, so syntax errors and
; table-size mistakes show up before any BIOS code uses them.

bits 16
cpu 386

%include "card.inc"
%include "vgamodes.inc"
%include "palettes.inc"

%if (vga_mode_table - vga_param_table) != VGA_PARAM_ENTRIES * VGA_PARAM_SIZE
%error "parameter table has the wrong size"
%endif
%if (pal_cga64 - vga_mode_table) != VGA_MODE_COUNT * MODE_SIZE
%error "mode table has the wrong size"
%endif
%if (pal_vga256 - pal_cga64) != 3 * 64 * 3
%error "64-entry palettes have the wrong size"
%endif
%if ($ - pal_vga256) != 256 * 3
%error "256-entry palette has the wrong size"
%endif

signon: db CARD_STR_SIGNON, 0

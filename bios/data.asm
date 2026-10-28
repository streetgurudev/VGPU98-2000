; VGPU98 VGA BIOS: tables and strings.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

; The ROM always runs at C000h, where the system BIOS places the primary VGA BIOS.
ROM_SEG equ 0xc000

; Video save pointer table (pointed to by 40:A8h)
save_pointer_table:
    dw vga_param_table, ROM_SEG     ; video parameter table
    dd 0                            ; dynamic save area
    dd 0                            ; alphanumeric character set override
    dd 0                            ; graphics character set override
    dw secondary_save_table, ROM_SEG
    dd 0
    dd 0

secondary_save_table:
    dw 0x1a                         ; length of this table
    dw dcc_table_data, ROM_SEG      ; display combination code table
    dd 0                            ; secondary alphanumeric character set override
    dd 0                            ; user palette profile table
    dd 0, 0, 0                      ; reserved

; Display combination code table: entry count, version, highest display code,
; reserved, then (alternate, active) display code pairs.
dcc_table_data:
    db 0x10, 0x01, 0x08, 0x00
    db 0x00, 0x00                   ; 00h: no display
    db 0x00, 0x01                   ; 01h: MDA
    db 0x00, 0x02                   ; 02h: CGA
    db 0x02, 0x01                   ; 03h: MDA + CGA
    db 0x00, 0x04                   ; 04h: EGA colour
    db 0x04, 0x01                   ; 05h: MDA + EGA colour
    db 0x00, 0x05                   ; 06h: EGA monochrome
    db 0x02, 0x05                   ; 07h: EGA monochrome + CGA
    db 0x00, 0x06                   ; 08h: PGA
    db 0x01, 0x06                   ; 09h: PGA + MDA
    db 0x05, 0x06                   ; 0Ah: PGA + EGA monochrome
    db 0x00, 0x08                   ; 0Bh: VGA colour
    db 0x01, 0x08                   ; 0Ch: VGA colour + MDA
    db 0x00, 0x07                   ; 0Dh: VGA monochrome
    db 0x02, 0x07                   ; 0Eh: VGA monochrome + CGA
    db 0x06, 0x07                   ; 0Fh: VGA monochrome + PGA

; CGA mode select register values for modes 00h-07h (kept in 40:65h)
cga_mode_values:
    db 0x2c, 0x28, 0x2d, 0x29, 0x2a, 0x2e, 0x1e, 0x29

; Per mode 00h-13h: number of colours (word), display pages, scan-line code
; (0 = 200, 1 = 350, 2 = 400, 3 = 480; FFh = the text-mode setting). Used by AH=1Bh.
mode_info:
    dw 16
    db 8, 0xff                      ; 00h
    dw 16
    db 8, 0xff                      ; 01h
    dw 16
    db 8, 0xff                      ; 02h
    dw 16
    db 8, 0xff                      ; 03h
    dw 4
    db 1, 0                         ; 04h
    dw 4
    db 1, 0                         ; 05h
    dw 2
    db 1, 0                         ; 06h
    dw 0
    db 8, 0xff                      ; 07h (monochrome)
    times 5 dd 0                    ; 08h-0Ch
    dw 16
    db 8, 0                         ; 0Dh
    dw 16
    db 4, 0                         ; 0Eh
    dw 0
    db 2, 1                         ; 0Fh (monochrome)
    dw 16
    db 2, 1                         ; 10h
    dw 2
    db 1, 3                         ; 11h
    dw 16
    db 1, 3                         ; 12h
    dw 256
    db 1, 0                         ; 13h

; Static functionality table (AH=1Bh)
static_functionality:
    db 0xff, 0xe0, 0x0f             ; modes 00h-07h, 0Dh-0Fh, 10h-13h
    db 0, 0, 0, 0
    db 0x07                         ; 200, 350 and 400 scan lines
    db 0x08                         ; character blocks in text modes
    db 0x02                         ; active character blocks at once
    db 0xff                         ; all-modes, gray summing, fonts, palette loading, cursor emulation,
                                    ; EGA palette, DAC, colour paging
    db 0x0e                         ; save/restore, intensity/blink, display combination code
    db 0, 0
    db 0x23                         ; save pointer: 512-character set, dynamic save area, DCC extension
    db 0

; 9-dot alternate glyph tables: empty. The fonts need no 9-dot replacements.
alt_9x14:
    db 0
alt_9x16:
    db 0

signon_text:
    db CARD_STR_SIGNON, 13, 10
    db CARD_STR_COPYRIGHT, 13, 10, 13, 10, 0

; Generated tables
%include "vgamodes.inc"
%include "palettes.inc"
%include "font8x8.inc"
%include "font8x14.inc"
%include "font8x16.inc"

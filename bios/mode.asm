; VGPU98 VGA BIOS: mode set, palette loading and font loading.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

; ---------------------------------------------------------------------------
; set_mode: AL = mode, bit 7 set keeps video memory. DS = BIOS data area.
; Clobbers AX, BX, CX, DX, SI, DI, ES. Unknown modes are ignored.
; ---------------------------------------------------------------------------

; DEBUG_MODE: logs "mode NNh" (expects sm_mode).
%macro DEBUG_MODE 0
    call debug_inline
    db "mode ", 0
    mov al, sm_mode
    call debug_hex8
    call debug_inline
    db "h", 10, 0
%endmacro

%define sm_mode     byte [bp - 1]
%define sm_keep     byte [bp - 2]       ; 80h = keep video memory
%define sm_desc     word [bp - 4]
%define sm_param    [bp - 8]            ; far pointer to the parameter entry (offset, then segment at bp - 6)
%define sm_palette  byte [bp - 9]
SM_LOCALS equ 10

set_mode:
    push bp
    mov bp, sp
    sub sp, SM_LOCALS

    mov ah, al
    and ah, 0x80
    mov sm_keep, ah
    and al, 0x7f
    mov sm_mode, al
    call mode_desc
    jc .done
    mov sm_desc, si

    DEBUG_MODE

    call ext_engine_off             ; the VGA core drives the display again

    ; Scan-line setting picks the 200-, 350- or 400-line variant.
    call scan_index
    mov al, [cs:si + MODE_PALETTES + bx]
    mov sm_palette, al
    movzx bx, byte [cs:si + MODE_PARAMS + bx]
    shl bx, 6
    call param_table
    add di, bx
    mov [bp - 8], di
    mov [bp - 6], es

    ; Equipment word: 80x25 colour, or monochrome.
    mov ax, [BDA_EQUIPMENT]
    and al, 0xcf
    or al, 0x20
    test byte [cs:si + MODE_FLAGS], MODE_FLAG_MONO
    jz .equipment
    or al, 0x30
.equipment:
    mov [BDA_EQUIPMENT], ax

    call program_regs               ; screen stays blanked until the end

    mov al, sm_palette
    call load_default_palette

    ; Clear video memory.
    mov si, sm_desc
    test sm_keep, 0x80
    jnz .cleared
    mov es, [cs:si + MODE_SEGMENT]
    xor di, di
    mov cx, 0x4000                  ; 32 KB
    xor ax, ax
    cmp byte [cs:si + MODE_MODEL], MODEL_TEXT
    jne .not_text
    mov ax, 0x0720                  ; space, light gray on black
    jmp .fill
.not_text:
    cmp byte [cs:si + MODE_MODEL], MODEL_CGA2
    jbe .fill                       ; CGA modes: 32 KB at B800h
    mov cx, 0x8000                  ; planar and 256-colour: 64 KB at A000h
.fill:
    rep stosw
.cleared:

    ; Data area.
    les di, sm_param
    mov al, sm_mode
    mov [BDA_MODE], al
    movzx ax, byte [es:di + P_COLUMNS]
    mov [BDA_COLUMNS], ax
    mov ax, [es:di + P_PAGE_SIZE]
    mov [BDA_PAGE_SIZE], ax
    xor ax, ax
    mov [BDA_PAGE_START], ax
    mov [BDA_ACTIVE_PAGE], al
    mov bx, BDA_CURSOR_POS
.clear_cursors:
    mov [bx], ax
    add bx, 2
    cmp bx, BDA_CURSOR_POS + 16
    jb .clear_cursors
    mov dx, PORT_CRTC_COLOR
    test byte [es:di + P_MISC], 1
    jnz .port
    mov dx, PORT_CRTC_MONO
.port:
    mov [BDA_CRTC_PORT], dx
    mov al, [es:di + P_ROWS]
    mov [BDA_ROWS], al
    movzx ax, byte [es:di + P_CHAR_HEIGHT]
    mov [BDA_CHAR_HEIGHT], ax
    mov al, [BDA_VIDEO_CTL]
    and al, ~VIDEO_CTL_NOCLEAR & 0xff
    or al, sm_keep
    mov [BDA_VIDEO_CTL], al
    mov ah, [es:di + P_CRTC + 0x0a] ; cursor shape: the mode's own start and end lines
    mov al, [es:di + P_CRTC + 0x0b]
    mov [BDA_CURSOR_TYPE], ax
    ; CGA mode select and colour select values. Modes above 07h keep 40:66h.
    movzx bx, sm_mode
    cmp bl, 7
    ja .cga_done
    mov al, [cs:cga_mode_values + bx]
    mov [BDA_CGA_MODE], al
    mov al, 0x30
    cmp bl, 6
    jne .cga_palette
    mov al, 0x3f
.cga_palette:
    mov [BDA_CGA_PALETTE], al
.cga_done:

    ; Text modes need their font; graphics modes point INT 43h at theirs.
    mov si, sm_desc
    cmp byte [cs:si + MODE_MODEL], MODEL_TEXT
    jne .graphics_font
    mov al, [es:di + P_CHAR_HEIGHT]
    call font_for_height
    push ds
    push cs
    pop ds
    mov bh, al                      ; bytes per character
    xor bl, bl                      ; block 0
    mov cx, 256
    xor dx, dx
    call load_font
    pop ds
    jmp .font_done
.graphics_font:
    mov al, [es:di + P_CHAR_HEIGHT]
    call font_for_height
    push es
    xor ax, ax
    mov es, ax
    mov [es:0x43 * 4], si
    mov [es:0x43 * 4 + 2], cs
    pop es
.font_done:

    ; Dynamic save area: keep a copy of the palette registers if software asked for one.
    les di, [BDA_SAVE_PTR]
    mov ax, es
    or ax, di
    jz .no_save_area
    les di, [es:di + 4]
    mov ax, es
    or ax, di
    jz .no_save_area
    push ds
    lds si, sm_param
    add si, P_ATTR
    mov cx, 16
    rep movsb
    mov al, [si + 1]                ; overscan, attribute register 11h
    stosb
    pop ds
.no_save_area:

    call screen_on

.done:
    mov sp, bp
    pop bp
    ret

; Out: BX = 0, 1 or 2 for the 200-, 350- or 400-line setting in the data area.
; Preserves the rest.
scan_index:
    mov bx, 2
    test byte [BDA_MODESET_CTL], MODESET_400
    jnz .done
    xor bx, bx
    test byte [BDA_MODESET_CTL], MODESET_200
    jnz .done
    inc bx
.done:
    ret

; Out: ES:DI = video parameter table. It comes from the save pointer table, so
; software can substitute its own; a null pointer falls back to the BIOS table.
; Preserves the rest.
param_table:
    push ax
    les di, [BDA_SAVE_PTR]
    mov ax, es
    or ax, di
    jz .rom
    les di, [es:di]
    mov ax, es
    or ax, di
    jnz .done
.rom:
    push cs
    pop es
    mov di, vga_param_table
.done:
    pop ax
    ret

; Out: ES:DI = parameter table entry of the current mode at the current
; scan-line setting; CF set if the current mode is unknown. Preserves the rest.
param_entry:
    push ax
    push bx
    push si
    mov al, [BDA_MODE]
    and al, 0x7f
    call mode_desc
    jc .done
    call scan_index
    movzx bx, byte [cs:si + MODE_PARAMS + bx]
    shl bx, 6
    call param_table
    add di, bx
    clc
.done:
    pop si
    pop bx
    pop ax
    ret

; In: AL = character height. Out: SI = matching font in CS (8x8 if none matches),
; AL unchanged. Preserves the rest.
font_for_height:
    mov si, font8x16
    cmp al, 16
    je .done
    mov si, font8x14
    cmp al, 14
    je .done
    mov si, font8x8
.done:
    ret

; ---------------------------------------------------------------------------
; program_regs: loads every VGA register from the parameter entry at ES:DI and
; leaves the screen blanked (attribute palette address source off).
; Clobbers AX, BX, CX, DX.
; ---------------------------------------------------------------------------

program_regs:
    mov dx, PORT_STATUS_COLOR       ; reset the attribute flip-flop, whichever
    in al, dx                       ; port set is currently decoded
    mov dx, PORT_STATUS_MONO
    in al, dx
    mov dx, PORT_ATTR
    xor al, al                      ; blank
    out dx, al

    mov dx, PORT_SEQ_INDEX
    mov ax, 0x0100                  ; synchronous reset while the clock changes
    out dx, ax
    xor bx, bx
.seq:
    lea ax, [bx + 1]
    mov ah, [es:di + P_SEQ + bx]
    out dx, ax
    inc bx
    cmp bx, 4
    jb .seq

    mov al, [es:di + P_MISC]
    mov dx, PORT_MISC_WRITE
    out dx, al

    mov dx, PORT_SEQ_INDEX
    mov ax, 0x0300                  ; end of reset
    out dx, ax

    mov dx, PORT_CRTC_COLOR
    test byte [es:di + P_MISC], 1
    jnz .crtc
    mov dx, PORT_CRTC_MONO
.crtc:
    mov al, 0x11                    ; lift the write protection of registers 0-7
    out dx, al
    inc dx
    in al, dx
    and al, 0x7f
    out dx, al
    dec dx
    xor bx, bx
.crtc_next:
    mov al, bl
    mov ah, [es:di + P_CRTC + bx]
    out dx, ax
    inc bx
    cmp bx, 25
    jb .crtc_next

    add dx, 6                       ; input status register of the new port set
    in al, dx
    mov dx, PORT_ATTR
    xor bx, bx
.attr:
    mov al, bl
    out dx, al
    mov al, [es:di + P_ATTR + bx]
    out dx, al
    inc bx
    cmp bx, 20
    jb .attr
    mov al, 0x14                    ; colour select
    out dx, al
    xor al, al
    out dx, al

    mov dx, PORT_GC_INDEX
    xor bx, bx
.gc:
    mov al, bl
    mov ah, [es:di + P_GC + bx]
    out dx, ax
    inc bx
    cmp bx, 9
    jb .gc

    mov dx, PORT_DAC_MASK
    mov al, 0xff
    out dx, al
    ret

; Turns the display back on (palette address source on). Preserves all registers.
screen_on:
    push ax
    push dx
    call attr_reset
    mov dx, PORT_ATTR
    mov al, 0x20
    out dx, al
    pop dx
    pop ax
    ret

; Blanks the display (palette address source off). Preserves all registers.
screen_off:
    push ax
    push dx
    call attr_reset
    mov dx, PORT_ATTR
    xor al, al
    out dx, al
    pop dx
    pop ax
    ret

; ---------------------------------------------------------------------------
; Palettes
; ---------------------------------------------------------------------------

; In: AL = PAL_* palette. Loads it into the DAC (rest of the 256 entries black)
; unless the data area disables default palette loading. Clobbers AX, BX, CX, DX, SI, DI.
load_default_palette:
    test byte [BDA_MODESET_CTL], MODESET_NO_PALETTE
    jnz .done
    mov si, pal_vga256
    mov cx, 256
    cmp al, PAL_VGA256
    je .load
    mov cx, 64
    mov si, pal_cga64
    cmp al, PAL_CGA64
    je .load
    mov si, pal_ega64
    cmp al, PAL_EGA64
    je .load
    mov si, pal_mono64
.load:
    mov dx, PORT_DAC_WRITE_INDEX
    xor al, al
    out dx, al
    inc dx
    mov di, 256
    sub di, cx                      ; entries left black afterwards
.entry:
    mov al, [cs:si]
    mov ah, [cs:si + 1]
    mov bl, [cs:si + 2]
    add si, 3
    call dac_out_rgb
    loop .entry
    mov cx, di
    jcxz .done
.black:
    xor ax, ax
    xor bl, bl
    call dac_out_rgb
    loop .black
.done:
    ret

; In: AL = red, AH = green, BL = blue, DX = DAC data port. Writes one entry,
; converted to gray when gray-scale summing is on. Clobbers AX, BX.
dac_out_rgb:
    test byte [BDA_MODESET_CTL], MODESET_GRAY
    jz .out
    call gray_sum
    mov ah, al
    mov bl, al
.out:
    out dx, al
    mov al, ah
    out dx, al
    mov al, bl
    out dx, al
    ret

; In: AL = red, AH = green, BL = blue. Out: AL = luminance (30% red, 59% green,
; 11% blue, rounded). Preserves the rest except BH.
gray_sum:
    push cx
    push dx
    mov cl, ah
    mov ch, bl
    mov ah, 76                      ; weights 76/151/28: they sum to 255, so white stays white
    mul ah                          ; AX = red * 76
    mov bx, ax
    mov al, cl
    mov ah, 151
    mul ah
    add bx, ax
    mov al, ch
    mov ah, 28
    mul ah
    add ax, bx
    add ax, 0x80
    mov al, ah                      ; / 256
    pop dx
    pop cx
    ret

; ---------------------------------------------------------------------------
; Fonts
; ---------------------------------------------------------------------------

; Maps plane 2 at A000h for font access. Clobbers AX, DX.
font_access_begin:
    mov dx, PORT_SEQ_INDEX
    mov ax, 0x0402                  ; map mask: plane 2
    out dx, ax
    mov ax, 0x0704                  ; memory mode: sequential, no odd/even
    out dx, ax
    mov dx, PORT_GC_INDEX
    mov ax, 0x0204                  ; read map select: plane 2
    out dx, ax
    mov ax, 0x0005                  ; write mode 0, no odd/even
    out dx, ax
    mov ax, 0x0406                  ; A000h, 64 KB, no chaining
    out dx, ax
    ret

; Restores the memory access registers (map mask, memory mode, read map,
; graphics mode, memory map) of the current mode from its parameter entry.
; Clobbers AX, DX.
font_access_end:
    push di
    push es
    push ds
    mov ax, BDA_SEG                 ; callers may have DS pointing at the glyphs
    mov ds, ax
    call param_entry
    pop ds
    jc .fallback
    mov dx, PORT_SEQ_INDEX
    mov al, 0x02
    mov ah, [es:di + P_SEQ + 1]
    out dx, ax
    mov al, 0x04
    mov ah, [es:di + P_SEQ + 3]
    out dx, ax
    mov dx, PORT_GC_INDEX
    mov al, 0x04
    mov ah, [es:di + P_GC + 4]
    out dx, ax
    mov al, 0x05
    mov ah, [es:di + P_GC + 5]
    out dx, ax
    mov al, 0x06
    mov ah, [es:di + P_GC + 6]
    out dx, ax
    jmp .done
.fallback:                          ; unknown mode: colour text values
    mov dx, PORT_SEQ_INDEX
    mov ax, 0x0302
    out dx, ax
    mov ax, 0x0204
    out dx, ax
    mov dx, PORT_GC_INDEX
    mov ax, 0x0004
    out dx, ax
    mov ax, 0x1005
    out dx, ax
    mov ax, 0x0e06
    out dx, ax
.done:
    pop es
    pop di
    ret

; In: DS:SI = glyphs, BH = bytes per glyph, BL = block (0-7), CX = glyph count,
; DX = first character. Writes the glyphs into character generator block BL.
; Preserves all registers except flags.
load_font:
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push es
    pushf
    cli                             ; no interrupt handler may touch video memory meanwhile
    push dx
    call font_access_begin
    pop dx
    mov ax, 0xa000
    mov es, ax
    ; Block base: blocks 0-3 at 0, 16, 32, 48 KB; blocks 4-7 at 8, 24, 40, 56 KB.
    movzx di, bl
    and di, 3
    shl di, 14
    test bl, 4
    jz .base
    add di, 0x2000
.base:
    shl dx, 5
    add di, dx
    jcxz .end
.glyph:
    push cx
    push di
    movzx cx, bh
    rep movsb
    pop di
    pop cx
    add di, 32
    loop .glyph
.end:
    call font_access_end
    popf
    pop es
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret

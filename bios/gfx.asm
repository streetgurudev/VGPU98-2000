; VGPU98 VGA BIOS: graphics-mode character and pixel services, CGA palette.
;
; Memory layouts:
;   CGA 4-colour (04h/05h)  B800h, 2 bits per pixel, even lines at 0, odd lines at 2000h
;   CGA 2-colour (06h)      B800h, 1 bit per pixel, same interleave
;   planar (0Dh-12h)        A000h, one bit per pixel in each plane, set/reset writes
;   256-colour (13h)        A000h, one byte per pixel, 320 bytes per line
; Colour bit 7 means XOR in the CGA and planar modes.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

; ---------------------------------------------------------------------------
; Geometry helpers
; ---------------------------------------------------------------------------

; In: AL = model, BH = page, DH = row, DL = column.
; Out: DI = offset of the character cell's top scan line. Preserves the rest.
gfx_cell_offset:
    push ax
    push cx
    push dx
    cmp al, MODEL_VGA256
    je .vga256
    cmp al, MODEL_PLANAR
    je .planar
    movzx di, dh                    ; CGA: a character row is 4 even lines of 80 bytes
    imul di, di, 320
    movzx cx, dl
    cmp al, MODEL_CGA4
    jne .cga_col
    add cx, cx                      ; 2 bytes per character column
.cga_col:
    add di, cx
    jmp .done
.vga256:
    movzx di, dh
    imul di, di, 320 * 8
    movzx cx, dl
    shl cx, 3
    add di, cx
    jmp .done
.planar:
    movzx ax, dh
    mul word [BDA_CHAR_HEIGHT]
    mul word [BDA_COLUMNS]
    mov di, ax
    pop dx
    push dx
    movzx cx, dl
    add di, cx
    movzx ax, bh
    mul word [BDA_PAGE_SIZE]
    add di, ax
.done:
    pop dx
    pop cx
    pop ax
    ret

; In: AX = scan line, CH = model, BL = page (planar). DS = BIOS data area.
; Out: DI = offset of the line's first byte. Preserves the rest.
gfx_line_offset:
    push ax
    push dx
    cmp ch, MODEL_VGA256
    je .vga256
    cmp ch, MODEL_PLANAR
    je .planar
    mov di, ax                      ; CGA interleave
    shr di, 1
    imul di, di, 80
    test al, 1
    jz .done
    add di, 0x2000
    jmp .done
.vga256:
    imul di, ax, 320
    jmp .done
.planar:
    mul word [BDA_COLUMNS]
    mov di, ax
    movzx ax, bl
    mul word [BDA_PAGE_SIZE]
    add di, ax
.done:
    pop dx
    pop ax
    ret

; In: DI = offset of a CGA cell, DL = line in the cell. Out: DI = that line. Clobbers AX.
cga_cell_line:
    movzx ax, dl
    shr ax, 1
    imul ax, ax, 80
    add di, ax
    test dl, 1
    jz .even
    add di, 0x2000
.even:
    ret

; In: AL = character, CH = model. Out: FS:SI = glyph in the current graphics font
; (INT 43h; INT 1Fh for characters 80h-FFh in the CGA modes). Preserves the rest.
gfx_glyph:
    push ax
    push bx
    push dx
    push es
    xor bx, bx
    mov es, bx
    movzx si, al
    cmp ch, MODEL_CGA2
    ja .int43
    cmp al, 0x80
    jb .int43
    sub si, 0x80
    shl si, 3
    add si, [es:0x1f * 4]
    mov fs, [es:0x1f * 4 + 2]
    jmp .done
.int43:
    mov ax, si
    mul word [BDA_CHAR_HEIGHT]
    mov si, ax
    add si, [es:0x43 * 4]
    mov fs, [es:0x43 * 4 + 2]
.done:
    pop es
    pop dx
    pop bx
    pop ax
    ret

; In: AL = model. Out: CX = scan lines per character cell. Preserves the rest.
gfx_cell_height:
    mov cx, 8
    cmp al, MODEL_PLANAR
    jne .done
    mov cx, [BDA_CHAR_HEIGHT]
.done:
    ret

; Restores the graphics controller registers the planar routines change:
; set/reset 0, set/reset enable 0, replace, bit mask FFh. Preserves all registers.
gfx_restore_gc:
    push ax
    push dx
    mov dx, PORT_GC_INDEX
    mov ax, 0x0000
    out dx, ax
    mov ax, 0x0001
    out dx, ax
    mov ax, 0x0003
    out dx, ax
    mov ax, 0xff08
    out dx, ax
    pop dx
    pop ax
    ret

; ---------------------------------------------------------------------------
; gfx_draw_char: AL = character, BL = colour (bit 7 = XOR), BH = page,
; DH = row, DL = column. Draws one character cell. Preserves all registers.
; ---------------------------------------------------------------------------

gfx_draw_char:
    pusha
    push es
    push fs
    mov cl, al
    call cur_model
    mov ch, al
    mov al, cl
    call gfx_glyph
    mov al, ch
    call gfx_cell_offset
    call regen_seg
    cmp ch, MODEL_CGA4
    je .cga4
    cmp ch, MODEL_CGA2
    je .cga2
    cmp ch, MODEL_PLANAR
    je .planar

    ; 256 colours: one byte per pixel, background 0.
    mov dx, 8
.v_line:
    mov ah, [fs:si]
    inc si
    mov cx, 8
.v_pixel:
    xor al, al
    shl ah, 1
    jnc .v_store
    mov al, bl
.v_store:
    stosb
    loop .v_pixel
    add di, 320 - 8
    dec dx
    jnz .v_line
    jmp .done

.cga4:
    mov dh, bl                      ; DH bit 7 = XOR
    and bx, 3                       ; BX = 2-bit colour
    xor dl, dl                      ; DL = line in the cell
.c4_line:
    mov ah, [fs:si]
    inc si
    xor bp, bp
    mov cx, 8
.c4_bit:
    shl bp, 2
    shl ah, 1
    jnc .c4_next
    or bp, bx
.c4_next:
    loop .c4_bit
    push di
    call cga_cell_line
    mov ax, bp
    xchg al, ah                     ; leftmost pixels in the first byte
    test dh, 0x80
    jz .c4_store
    xor [es:di], ax
    jmp .c4_stored
.c4_store:
    mov [es:di], ax
.c4_stored:
    pop di
    inc dl
    cmp dl, 8
    jb .c4_line
    jmp .done

.cga2:                              ; 2 colours: the glyph is drawn whatever the colour; bit 7 = XOR
    mov dh, bl
    xor dl, dl
.c2_line:
    mov ah, [fs:si]
    inc si
    push di
    push ax
    call cga_cell_line
    pop ax
    test dh, 0x80
    jz .c2_store
    xor [es:di], ah
    jmp .c2_stored
.c2_store:
    mov [es:di], ah
.c2_stored:
    pop di
    inc dl
    cmp dl, 8
    jb .c2_line
    jmp .done

.planar:
    mov al, [BDA_MODE]
    and al, 0x7f
    cmp al, 0x11
    jne .p_colour
    or bl, 0x0f                     ; mode 11h has 2 colours: draw in every plane, whatever the colour
.p_colour:
    mov dx, PORT_GC_INDEX
    mov ax, 0x0f01                  ; set/reset on all planes
    out dx, ax
    mov ax, 0x0003                  ; replace
    test bl, 0x80
    jz .p_func
    mov ah, 0x18                    ; XOR
.p_func:
    out dx, ax
    mov al, MODEL_PLANAR
    call gfx_cell_height
    mov bp, [BDA_COLUMNS]
.p_line:
    test bl, 0x80
    jnz .p_glyph
    mov ax, 0x0000                  ; clear the line to colour 0
    out dx, ax
    mov ax, 0xff08
    out dx, ax
    mov al, [es:di]                 ; load the latches
    mov [es:di], al
.p_glyph:
    mov ah, bl
    and ah, 0x0f
    xor al, al
    out dx, ax                      ; set/reset = colour
    mov al, 0x08
    mov ah, [fs:si]
    inc si
    out dx, ax                      ; bit mask = glyph line
    mov al, [es:di]
    mov [es:di], al
    add di, bp
    loop .p_line
    call gfx_restore_gc

.done:
    pop fs
    pop es
    popa
    ret

; ---------------------------------------------------------------------------
; Character services in graphics modes
; ---------------------------------------------------------------------------

; AH=09h/0Ah: AL = character, BL = colour, BH = page, CX = count, at the cursor.
; Characters go across the row and stop at its end.
gfx_write_char:
    jcxz .done
    call get_cursor
.next:
    call gfx_draw_char
    inc dl
    cmp dl, [BDA_COLUMNS]
    jae .done
    loop .next
.done:
    ret

; Teletype character: AL = character, BL = colour, BH = page, DX = position.
gfx_put_char:
    jmp gfx_draw_char

; AH=08h: BH = page -> AL = character at the cursor (0 if none matches), AH = 0.
; The cell is matched against every glyph of the current graphics font, treating
; any non-background pixel as set.
%define gr_model  byte [bp - 17]
%define gr_lines  byte [bp - 18]
%define gr_dontc  byte [bp - 19]
%define gr_gmode  byte [bp - 20]
%define gr_ccomp  byte [bp - 21]
GR_LOCALS equ 22                    ; pattern at [bp - 16] .. [bp - 1]

gfx_read_char:
    push bp
    mov bp, sp
    sub sp, GR_LOCALS
    call get_cursor
    call cur_model
    mov gr_model, al
    call gfx_cell_offset
    call regen_seg
    call gfx_cell_height
    mov gr_lines, cl

    cmp gr_model, MODEL_PLANAR
    jne .read
    ; Planar: read mode 1 comparing all planes against colour 0 returns the
    ; background bits of each line.
    mov dx, PORT_GC_INDEX
    mov al, 0x05
    out dx, al
    inc dx
    in al, dx
    mov gr_gmode, al
    or al, 0x08
    out dx, al
    dec dx
    mov al, 0x07
    out dx, al
    inc dx
    in al, dx
    mov gr_dontc, al
    mov al, 0x0f
    out dx, al
    dec dx
    mov al, 0x02
    out dx, al
    inc dx
    in al, dx
    mov gr_ccomp, al
    xor al, al
    out dx, al
    dec dx

.read:
    xor si, si                      ; SI = line
.line:
    mov al, gr_model
    cmp al, MODEL_CGA4
    je .cga4
    cmp al, MODEL_CGA2
    je .cga2
    cmp al, MODEL_PLANAR
    je .planar
    ; 256 colours
    xor ah, ah
    mov cx, 8
.v_pixel:
    shl ah, 1
    cmp byte [es:di], 0
    je .v_background
    or ah, 1
.v_background:
    inc di
    loop .v_pixel
    add di, 320 - 8
    jmp .store
.cga4:
    push di
    mov dx, si
    call cga_cell_line
    mov dx, [es:di]
    xchg dl, dh                     ; DX = 8 pixels, leftmost in the top bits
    xor ah, ah
    mov cx, 8
.c4_pixel:
    shl ah, 1
    test dh, 0xc0
    jz .c4_background
    or ah, 1
.c4_background:
    shl dx, 2
    loop .c4_pixel
    pop di
    jmp .store
.cga2:
    push di
    mov dx, si
    call cga_cell_line
    mov ah, [es:di]
    pop di
    jmp .store
.planar:
    mov ah, [es:di]
    not ah
    add di, [BDA_COLUMNS]
.store:
    mov [bp + si - 16], ah
    inc si
    mov ax, si
    cmp al, gr_lines
    jb .line

    cmp gr_model, MODEL_PLANAR
    jne .search
    mov dx, PORT_GC_INDEX
    mov al, 0x05
    mov ah, gr_gmode
    out dx, ax
    mov al, 0x07
    mov ah, gr_dontc
    out dx, ax
    mov al, 0x02
    mov ah, gr_ccomp
    out dx, ax

.search:
    xor ax, ax                      ; AL = candidate character
    mov ch, gr_model
.candidate:
    call gfx_glyph                  ; FS:SI
    xor di, di
.compare:
    mov dl, [fs:si]
    inc si
    cmp dl, [bp + di - 16]
    jne .next
    inc di
    mov dx, di
    cmp dl, gr_lines
    jb .compare
    jmp .found
.next:
    inc al
    jnz .candidate
.found:
    xor ah, ah
    mov sp, bp
    pop bp
    mov [bp + F_AX], ax
    ret

; ---------------------------------------------------------------------------
; gfx_scroll: AH = 06h (up) or 07h (down), AL = lines (0 = blank the window),
; BH = fill colour, BL = page, CX = top-left, DX = bottom-right (character cells).
; DS = BIOS data area. Clobbers AX, CX, DX, SI, DI, ES. Preserves BX and DS.
; ---------------------------------------------------------------------------

%define gs_bpc     word [bp - 2]        ; bytes per character column
%define gs_width   word [bp - 4]        ; bytes per window line
%define gs_x0      word [bp - 6]        ; byte offset of the left column
%define gs_h       word [bp - 8]        ; scan lines per character row
%define gs_left    byte [bp - 10]       ; CL
%define gs_top     byte [bp - 9]        ; CH
%define gs_right   byte [bp - 12]       ; DL
%define gs_bottom  byte [bp - 11]       ; DH
%define gs_moves   byte [bp - 13]
%define gs_lines   byte [bp - 14]
%define gs_dir     byte [bp - 15]
%define gs_request byte [bp - 16]
%define gs_model   byte [bp - 17]
%define gs_fill    byte [bp - 18]
%define gs_gmode   byte [bp - 19]
%define gs_src     byte [bp - 20]
%define gs_dst     byte [bp - 21]
%define gs_page    byte [bp - 22]
%define gs_byte    byte [bp - 23]
%define gs_line    word [bp - 26]
GS_LOCALS equ 26

gfx_scroll:
    push bp
    mov bp, sp
    sub sp, GS_LOCALS
    push bx
    mov gs_dir, ah
    mov gs_request, al
    mov gs_fill, bh
    mov gs_page, bl
    call cur_model
    mov gs_model, al

    cmp dh, [BDA_ROWS]
    jbe .rows_ok
    mov dh, [BDA_ROWS]
.rows_ok:
    mov ax, [BDA_COLUMNS]
    dec ax
    cmp dl, al
    jbe .cols_ok
    mov dl, al
.cols_ok:
    cmp ch, dh
    ja .done
    cmp cl, dl
    ja .done
    mov [bp - 10], cx
    mov [bp - 12], dx

    mov ah, dh
    sub ah, ch
    inc ah                          ; window height in character rows
    mov al, gs_request
    test al, al
    jz .all
    cmp al, ah
    jb .some
.all:
    mov al, ah
.some:
    mov gs_lines, al
    sub ah, al
    mov gs_moves, ah

    ; Geometry of the current mode.
    mov gs_bpc, 1
    cmp gs_model, MODEL_CGA4
    jne .bpc_vga
    mov gs_bpc, 2
.bpc_vga:
    cmp gs_model, MODEL_VGA256
    jne .bpc_done
    mov gs_bpc, 8
.bpc_done:
    mov al, gs_model
    call gfx_cell_height
    mov gs_h, cx
    movzx ax, gs_left
    mul gs_bpc
    mov gs_x0, ax
    mov al, gs_right
    sub al, gs_left
    inc al
    movzx ax, al
    mul gs_bpc
    mov gs_width, ax
    call regen_seg

    cmp gs_model, MODEL_PLANAR
    jne .modes_saved
    mov dx, PORT_GC_INDEX           ; keep the graphics mode register to restore it
    mov al, 0x05
    out dx, al
    inc dx
    in al, dx
    mov gs_gmode, al
.modes_saved:

    cmp gs_dir, 0x07
    je .down

    ; Up: rows move towards the top, the bottom rows are blanked.
    xor dx, dx                      ; DL = rows copied so far
.up_copy:
    cmp dl, gs_moves
    jae .up_fill_start
    mov al, gs_top
    add al, dl                      ; destination row
    mov ah, al
    add ah, gs_lines                ; source row
    call .copy_row
    inc dl
    jmp .up_copy
.up_fill_start:
    mov al, gs_top
    add al, gs_moves
.up_fill:
    cmp al, gs_bottom
    ja .finish
    call .fill_row
    inc al
    jmp .up_fill

    ; Down: rows move towards the bottom, the top rows are blanked.
.down:
    xor dx, dx
.down_copy:
    cmp dl, gs_moves
    jae .down_fill_start
    mov al, gs_bottom
    sub al, dl
    mov ah, al
    sub ah, gs_lines
    call .copy_row
    inc dl
    jmp .down_copy
.down_fill_start:
    mov al, gs_top
    mov ah, gs_top
    add ah, gs_lines                ; first row not blanked
.down_fill:
    cmp al, ah
    jae .finish
    call .fill_row
    inc al
    jmp .down_fill

.finish:
    cmp gs_model, MODEL_PLANAR
    jne .done
    mov dx, PORT_GC_INDEX
    mov al, 0x05
    mov ah, gs_gmode
    out dx, ax
    call gfx_restore_gc
.done:
    pop bx
    mov sp, bp
    pop bp
    ret

; AL = destination character row, AH = source character row. Copies the
; window's part of each scan line. Preserves AX and DX.
.copy_row:
    push ax
    push dx
    mov gs_dst, al
    mov gs_src, ah
    cmp gs_model, MODEL_PLANAR
    jne .copy_start
    mov dx, PORT_GC_INDEX           ; write mode 1: each byte copies all four planes
    mov al, 0x05
    mov ah, gs_gmode
    and ah, 0xfc
    or ah, 0x01
    out dx, ax
.copy_start:
    mov gs_line, 0
.copy_line:
    mov ch, gs_model
    mov bl, gs_page
    movzx ax, gs_src
    mul gs_h
    add ax, gs_line
    call gfx_line_offset
    add di, gs_x0
    mov si, di
    movzx ax, gs_dst
    mul gs_h
    add ax, gs_line
    call gfx_line_offset
    add di, gs_x0
    mov cx, gs_width
    push ds
    push es
    pop ds
    rep movsb
    pop ds
    inc gs_line
    mov ax, gs_line
    cmp ax, gs_h
    jb .copy_line
    pop dx
    pop ax
    ret

; AL = character row to blank with the fill colour. Preserves AX and DX.
.fill_row:
    push ax
    push dx
    mov gs_dst, al
    mov al, gs_fill
    cmp gs_model, MODEL_CGA4
    jne .f_cga2
    and al, 3
    mov ah, 0x55
    mul ah                          ; the 2-bit colour in every pixel
    jmp .f_ready
.f_cga2:
    cmp gs_model, MODEL_CGA2
    jne .f_planar
    xor al, al                      ; colour bit 0 sets or clears every pixel
    test gs_fill, 1
    jz .f_ready
    mov al, 0xff
    jmp .f_ready
.f_planar:
    cmp gs_model, MODEL_PLANAR
    jne .f_ready
    mov dx, PORT_GC_INDEX
    mov al, 0x05
    mov ah, gs_gmode
    and ah, 0xfc                    ; write mode 0
    out dx, ax
    mov ah, gs_fill
    and ah, 0x0f
    xor al, al
    out dx, ax                      ; set/reset = fill colour
    mov ax, 0x0f01
    out dx, ax
    mov ax, 0xff08
    out dx, ax
    mov al, 0xff
.f_ready:
    mov gs_byte, al
    mov gs_line, 0
.fill_line:
    mov ch, gs_model
    mov bl, gs_page
    movzx ax, gs_dst
    mul gs_h
    add ax, gs_line
    call gfx_line_offset
    add di, gs_x0
    mov al, gs_byte
    mov cx, gs_width
    rep stosb
    inc gs_line
    mov ax, gs_line
    cmp ax, gs_h
    jb .fill_line
    pop dx
    pop ax
    ret

; ---------------------------------------------------------------------------
; AH=0Ch: write pixel. AL = colour (bit 7 = XOR, CGA and planar), BH = page,
; CX = column, DX = row. AH=0Dh: read pixel -> AL = colour. Text modes: nothing.
; ---------------------------------------------------------------------------

; In: CX = x, DX = y, BH = page, AL = model. Out: DI = byte offset, AH = bit or
; pixel mask position: CGA4: AH = shift (0, 2, 4, 6); CGA2 and planar: AH = bit mask.
; Preserves the rest except AH.
pixel_addr:
    push bx
    push cx
    push dx
    cmp al, MODEL_VGA256
    je .vga256
    cmp al, MODEL_PLANAR
    je .planar
    mov di, dx                      ; CGA interleave
    shr di, 1
    imul di, di, 80
    test dl, 1
    jz .even
    add di, 0x2000
.even:
    mov bx, cx
    cmp al, MODEL_CGA4
    jne .cga2
    shr bx, 2
    add di, bx
    mov ah, cl
    and ah, 3
    xor ah, 3
    shl ah, 1                       ; shift of the pixel's 2 bits
    jmp .done
.cga2:
    shr bx, 3
    add di, bx
    jmp .bitmask
.planar:
    push ax
    mov ax, dx
    mul word [BDA_COLUMNS]
    mov di, ax
    movzx ax, bh
    mul word [BDA_PAGE_SIZE]
    add di, ax
    pop ax
    pop dx
    pop cx
    push cx
    push dx
    mov bx, cx
    shr bx, 3
    add di, bx
.bitmask:
    mov ah, 0x80
    and cl, 7
    shr ah, cl
    jmp .done
.vga256:
    imul di, dx, 320
    add di, cx
.done:
    pop dx
    pop cx
    pop bx
    ret

fn_write_pixel:
    mov si, ax                      ; SI low byte = colour
    call cur_model
    cmp al, MODEL_TEXT
    jbe .done
    call pixel_addr
    call regen_seg
    mov bx, si                      ; BL = colour
    cmp al, MODEL_VGA256
    je .vga256
    cmp al, MODEL_PLANAR
    je .planar
    cmp al, MODEL_CGA2
    je .cga2
    ; CGA 4 colours
    mov cl, ah
    mov al, bl
    and al, 3
    shl al, cl
    mov ah, 3
    shl ah, cl
    test bl, 0x80
    jz .c4_set
    xor [es:di], al
    ret
.c4_set:
    not ah
    and [es:di], ah
    or [es:di], al
    ret
.cga2:
    test bl, 0x80
    jz .c2_set
    test bl, 1
    jz .done
    xor [es:di], ah
    ret
.c2_set:
    test bl, 1
    jz .c2_clear
    or [es:di], ah
    ret
.c2_clear:
    not ah
    and [es:di], ah
    ret
.planar:
    mov dx, PORT_GC_INDEX
    mov al, 0x08
    out dx, ax                      ; bit mask (AH)
    mov ax, 0x0f01
    out dx, ax
    mov ah, bl
    and ah, 0x0f
    xor al, al
    out dx, ax                      ; set/reset = colour
    mov ax, 0x0003
    test bl, 0x80
    jz .p_func
    mov ah, 0x18
.p_func:
    out dx, ax
    mov al, [es:di]
    mov [es:di], al
    jmp gfx_restore_gc
.vga256:
    mov [es:di], bl
.done:
    ret

fn_read_pixel:
    call cur_model
    cmp al, MODEL_TEXT
    jbe .done
    call pixel_addr
    call regen_seg
    cmp al, MODEL_VGA256
    je .vga256
    cmp al, MODEL_PLANAR
    je .planar
    cmp al, MODEL_CGA2
    je .cga2
    mov cl, ah
    mov al, [es:di]
    shr al, cl
    and al, 3
    jmp .store
.cga2:
    xor al, al
    test [es:di], ah
    jz .store
    inc al
    jmp .store
.planar:
    mov bl, ah                      ; bit mask
    xor bh, bh                      ; colour
    mov dx, PORT_GC_INDEX
    mov cx, 4
.plane:
    dec cl
    mov al, 0x04
    mov ah, cl
    out dx, ax                      ; read map select
    shl bh, 1
    test [es:di], bl
    jz .plane_next
    or bh, 1
.plane_next:
    inc cl
    loop .plane
    mov ax, 0x0004
    out dx, ax
    mov al, bh
    jmp .store
.vga256:
    mov al, [es:di]
.store:
    mov [bp + F_AL], al
.done:
    ret

; ---------------------------------------------------------------------------
; AH=0Bh: CGA palette. BH = 00h: background/border colour BL (bit 4 = intensity);
; BH = 01h: palette BL (0 = green/red/brown, 1 = cyan/magenta/white).
; ---------------------------------------------------------------------------

; In: AL = attribute register index, AH = value. Writes it with the display kept on.
; Preserves all registers.
attr_write:
    push ax
    push dx
    call attr_reset
    mov dx, PORT_ATTR
    or al, 0x20
    out dx, al
    mov al, ah
    out dx, al
    pop dx
    pop ax
    ret

; In: AL = attribute register index. Out: AH = value. Preserves the rest.
attr_read:
    push bx
    push dx
    mov bl, al
    call attr_reset
    mov dx, PORT_ATTR
    or al, 0x20
    out dx, al
    inc dx
    in al, dx
    mov ah, al
    mov al, bl
    call attr_reset
    pop dx
    pop bx
    ret

fn_cga_palette:
    call cur_model
    mov cl, al                      ; CL = model
    test bh, bh
    jnz .palette
    ; BH=0: border, and in graphics modes the background, from BL bits 0-3;
    ; BL bit 4 selects the bright foreground colours.
    mov al, [BDA_CGA_PALETTE]
    and al, 0xe0
    mov ah, bl
    and ah, 0x1f
    or al, ah
    mov [BDA_CGA_PALETTE], al
    mov ah, bl
    and ah, 0x0f
    mov al, 0x11
    call attr_write                 ; border
    cmp cl, MODEL_TEXT
    jbe .done
    xor al, al
    call attr_write                 ; background
    jmp .foreground
.palette:
    ; BH=1: BL bit 0 picks palette 0 (green/red/brown) or 1 (cyan/magenta/white).
    ; Text modes ignore it.
    cmp bh, 1
    jne .done
    cmp cl, MODEL_TEXT
    jbe .done
    mov al, [BDA_CGA_PALETTE]
    and al, 0xdf
    test bl, 1
    jz .store_palette
    or al, 0x20
.store_palette:
    mov [BDA_CGA_PALETTE], al
.foreground:
    ; Palette registers 1-3: 2/4/6 or 3/5/7, plus intensity. Every graphics mode
    ; gets them, as with IBM's BIOS.
    mov ah, 2
    test byte [BDA_CGA_PALETTE], 0x20
    jz .base
    mov ah, 3
.base:
    test byte [BDA_CGA_PALETTE], 0x10
    jz .loop_start
    or ah, 0x10
.loop_start:
    mov al, 1
.foreground_next:
    call attr_write
    add ah, 2
    inc al
    cmp al, 4
    jb .foreground_next
.done:
    ret

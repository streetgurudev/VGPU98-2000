; VGPU98 VGA BIOS: character output, scrolling and the teletype.
;
; Text modes are handled here; graphics modes are passed to gfx.asm.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

; ---------------------------------------------------------------------------
; AH=06h/07h: scroll a window of the active page up (06h) or down (07h).
; AL = lines (0 = blank the window), BH = fill attribute (text) or colour
; (graphics), CH/CL = top row/column, DH/DL = bottom row/column.
; ---------------------------------------------------------------------------

fn_scroll:
    mov si, ax
    call cur_model
    cmp al, MODEL_TEXT
    mov ax, si
    mov bl, [BDA_ACTIVE_PAGE]
    je text_scroll
    jmp gfx_scroll

; ---------------------------------------------------------------------------
; text_scroll: AH = 06h (up) or 07h (down), AL = lines (0 = blank the window),
; BH = fill attribute, BL = page, CX = top-left, DX = bottom-right.
; DS = BIOS data area. Clobbers AX, CX, DX, SI, DI, ES. Preserves BX and DS.
; ---------------------------------------------------------------------------

%define ts_stride  word [bp - 2]        ; bytes per text row
%define ts_width   word [bp - 4]        ; window width in cells
%define ts_fill    word [bp - 6]        ; blank cell
%define ts_base    word [bp - 8]        ; page offset
%define ts_left    byte [bp - 10]       ; CL
%define ts_top     byte [bp - 9]        ; CH
%define ts_right   byte [bp - 12]       ; DL
%define ts_bottom  byte [bp - 11]       ; DH
%define ts_moves   byte [bp - 13]       ; rows copied
%define ts_lines   byte [bp - 14]       ; rows blanked
%define ts_dir     byte [bp - 15]
%define ts_request byte [bp - 16]
TS_LOCALS equ 16

text_scroll:
    push bp
    mov bp, sp
    sub sp, TS_LOCALS
    push bx
    push ds
    mov ts_dir, ah
    mov ts_request, al

    ; Clip the bottom-right corner to the screen.
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

    ; Rows to blank and rows to copy.
    mov ah, dh
    sub ah, ch
    inc ah                          ; window height
    mov al, ts_request
    test al, al
    jz .all
    cmp al, ah
    jb .some
.all:
    mov al, ah
.some:
    mov ts_lines, al
    sub ah, al
    mov ts_moves, ah

    mov al, dl
    sub al, cl
    inc al
    movzx ax, al
    mov ts_width, ax
    mov ax, [BDA_COLUMNS]
    add ax, ax
    mov ts_stride, ax
    mov ah, bh
    mov al, ' '
    mov ts_fill, ax
    movzx ax, bl
    mul word [BDA_PAGE_SIZE]
    mov ts_base, ax

    call regen_seg
    push es
    pop ds                          ; DS = ES = screen memory from here on

    cmp ts_dir, 0x07
    je .down

    ; Up: rows move towards the top, the bottom rows are blanked.
    mov al, ts_top
    call .row_offset
    mov di, ax
    movzx ax, ts_lines
    mul ts_stride
    add ax, di
    mov si, ax
    mov dx, ts_stride
    movzx bx, ts_moves
.up_copy:
    test bx, bx
    jz .up_blank
    push si
    push di
    mov cx, ts_width
    rep movsw
    pop di
    pop si
    add si, dx
    add di, dx
    dec bx
    jmp .up_copy
.up_blank:
    movzx bx, ts_lines
    mov ax, ts_fill
.up_fill:
    test bx, bx
    jz .done
    push di
    mov cx, ts_width
    rep stosw
    pop di
    add di, dx
    dec bx
    jmp .up_fill

    ; Down: rows move towards the bottom, the top rows are blanked.
.down:
    mov al, ts_bottom
    call .row_offset
    mov di, ax
    movzx ax, ts_lines
    mul ts_stride
    mov si, di
    sub si, ax
    mov dx, ts_stride
    movzx bx, ts_moves
.down_copy:
    test bx, bx
    jz .down_blank
    push si
    push di
    mov cx, ts_width
    rep movsw
    pop di
    pop si
    sub si, dx
    sub di, dx
    dec bx
    jmp .down_copy
.down_blank:
    movzx bx, ts_lines
    mov ax, ts_fill
.down_fill:
    test bx, bx
    jz .done
    push di
    mov cx, ts_width
    rep stosw
    pop di
    sub di, dx
    dec bx
    jmp .down_fill

.done:
    pop ds
    pop bx
    mov sp, bp
    pop bp
    ret

; In: AL = row. Out: AX = offset of (row, left column) in the page. Clobbers DX.
.row_offset:
    movzx ax, al
    mul ts_stride
    add ax, ts_base
    movzx dx, ts_left
    add ax, dx
    add ax, dx
    ret

; ---------------------------------------------------------------------------
; AH=08h: read character and attribute at the cursor (BH = page)
;   -> AL = character, AH = attribute (text) or 0 (graphics)
; ---------------------------------------------------------------------------

fn_read_char:
    call cur_model
    cmp al, MODEL_TEXT
    jne gfx_read_char
    call get_cursor
    call text_addr
    mov ax, [es:di]
    mov [bp + F_AX], ax
    ret

; ---------------------------------------------------------------------------
; AH=09h: write character and attribute CX times at the cursor (no cursor move).
; AH=0Ah: the same, keeping the attributes already on screen.
; AL = character, BH = page, BL = attribute (graphics: colour, bit 7 = XOR).
; ---------------------------------------------------------------------------

fn_write_char_attr:
    mov si, ax
    call cur_model
    cmp al, MODEL_TEXT
    mov ax, si
    jne gfx_write_char
    jcxz .done
    call get_cursor
    call text_addr
    mov ah, bl
    rep stosw
.done:
    ret

fn_write_char:
    mov si, ax
    call cur_model
    cmp al, MODEL_TEXT
    mov ax, si
    jne gfx_write_char
    jcxz .done
    call get_cursor
    call text_addr
.next:
    stosb
    inc di
    loop .next
.done:
    ret

; ---------------------------------------------------------------------------
; AH=0Eh: teletype output on the active page. AL = character,
; BL = foreground colour (graphics modes). Text keeps the attribute on screen.
; ---------------------------------------------------------------------------

fn_teletype:
    mov bh, [BDA_ACTIVE_PAGE]
    xor ah, ah
    jmp tty

; ---------------------------------------------------------------------------
; tty: AL = character, AH = 1 to also write attribute BL (text modes),
; BL = attribute or colour, BH = page. Handles bell, backspace, line feed and
; carriage return, wraps at the right edge and scrolls at the bottom.
; DS = BIOS data area. Clobbers AX, CX, DX, SI, DI, ES. Preserves BX.
; ---------------------------------------------------------------------------

tty:
    call get_cursor                 ; DX = position
    cmp al, 0x0d
    je .cr
    cmp al, 0x0a
    je .lf
    cmp al, 0x08
    je .bs
    cmp al, 0x07
    je beep

    mov cx, ax                      ; CL = character, CH = write-attribute flag
    call cur_model
    cmp al, MODEL_TEXT
    mov al, cl
    jne .graphics
    call text_addr
    stosb
    test ch, ch
    jz .advance
    mov [es:di], bl
    jmp .advance
.graphics:
    mov cx, 1
    call gfx_put_char               ; AL, BL, BH, DX
.advance:
    inc dl
    cmp dl, [BDA_COLUMNS]
    jb .setpos
    xor dl, dl
.lf:
    inc dh
    cmp dh, [BDA_ROWS]
    jbe .setpos
    mov dh, [BDA_ROWS]
    ; Scroll the page up a line. Text modes fill with the attribute under the cursor.
    push bx
    push dx
    call cur_model
    mov ah, 0
    cmp al, MODEL_TEXT
    jne .fill
    call text_addr
    mov ah, [es:di + 1]
.fill:
    mov bl, bh                      ; page
    mov bh, ah                      ; fill attribute or colour
    mov ax, 0x0601
    xor cx, cx
    mov dh, [BDA_ROWS]
    mov dl, [BDA_COLUMNS]
    dec dl
    call cur_model
    cmp al, MODEL_TEXT
    mov ax, 0x0601
    jne .gfx_scroll
    call text_scroll
    jmp .scrolled
.gfx_scroll:
    call gfx_scroll
.scrolled:
    pop dx
    pop bx
    jmp .setpos
.cr:
    xor dl, dl
    jmp .setpos
.bs:
    test dl, dl
    jz .setpos
    dec dl
.setpos:
    jmp set_cursor

; ---------------------------------------------------------------------------
; AH=13h: write string. AL bit 0 = leave the cursor after the string,
; bit 1 = string holds character/attribute pairs. BH = page, BL = attribute,
; CX = length, DH/DL = start position, ES:BP = string.
; ---------------------------------------------------------------------------

fn_write_string:
    cmp al, 3
    ja .done
    jcxz .done                      ; nothing to write: the cursor stays put
    mov si, [bp + F_BP]
    mov di, dx
    call get_cursor
    xchg dx, di                     ; DI = original cursor, DX = start
    push di
    call set_cursor
.next:
    jcxz .end
    mov al, [es:si]
    inc si
    test byte [bp + F_AL], 2
    jz .write
    ; Character/attribute strings: BEL, BS, LF and CR take no attribute byte,
    ; as with IBM's BIOS.
    cmp al, 7
    je .write
    cmp al, 8
    je .write
    cmp al, 10
    je .write
    cmp al, 13
    je .write
    mov bl, [es:si]
    inc si
.write:
    push cx
    push si
    push es
    mov ah, 1
    call tty
    pop es
    pop si
    pop cx
    dec cx
    jmp .next
.end:
    pop dx
    test byte [bp + F_AL], 1
    jnz .done
    call set_cursor
.done:
    ret

; VGPU98 VGA BIOS: INT 10h entry, dispatch and small services.
;
; Conventions inside INT 10h functions:
;   DS = BIOS data area (40h), BP = INT 10h stack frame (defs.inc F_*), DF = 0.
;   On entry the caller's AX, BX, CX, DX, SI, DI and ES are in their registers.
;   Functions return results by writing the frame; all registers, FS and GS
;   included, are restored on exit.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

INT10_LAST equ 0x1c

int10_handler:
    sti
    cld
    push ds
    push es
    push fs
    push gs
    pusha
    mov bp, sp
    mov ax, BDA_SEG
    mov ds, ax
    mov ax, [bp + F_AX]
    cmp ah, 0x4f
    je .vbe
    cmp ah, INT10_LAST
    ja .done
    movzx bx, ah
    add bx, bx
    push word .done                 ; return address for the function
    push word [cs:int10_table + bx] ; function address, entered via RET
    mov bx, [bp + F_BX]
    ret
.vbe:
    mov bx, [bp + F_BX]
    call fn_vbe
.done:
    popa
    pop gs
    pop fs
    pop es
    pop ds
    iret

int10_table:
    dw fn_set_mode                  ; 00h
    dw fn_set_cursor_type           ; 01h
    dw fn_set_cursor_pos            ; 02h
    dw fn_get_cursor                ; 03h
    dw fn_light_pen                 ; 04h
    dw fn_set_page                  ; 05h
    dw fn_scroll                    ; 06h
    dw fn_scroll                    ; 07h
    dw fn_read_char                 ; 08h
    dw fn_write_char_attr           ; 09h
    dw fn_write_char                ; 0Ah
    dw fn_cga_palette               ; 0Bh
    dw fn_write_pixel               ; 0Ch
    dw fn_read_pixel                ; 0Dh
    dw fn_teletype                  ; 0Eh
    dw fn_get_mode                  ; 0Fh
    dw fn_palette                   ; 10h
    dw fn_font                      ; 11h
    dw fn_alt_select                ; 12h
    dw fn_write_string              ; 13h
    dw fn_none                      ; 14h
    dw fn_none                      ; 15h
    dw fn_none                      ; 16h
    dw fn_none                      ; 17h
    dw fn_none                      ; 18h
    dw fn_none                      ; 19h
    dw fn_dcc                       ; 1Ah
    dw fn_state_info                ; 1Bh
    dw fn_save_restore              ; 1Ch

fn_none:
    ret

; ---------------------------------------------------------------------------
; Helpers
; ---------------------------------------------------------------------------

; In: AL = mode. Out: SI = mode descriptor (in CS); CF set if the mode does not exist.
mode_desc:
    cmp al, VGA_MODE_COUNT
    jae .bad
    movzx si, al
    imul si, si, MODE_SIZE
    add si, vga_mode_table
    cmp byte [cs:si + MODE_MODEL], MODEL_NONE
    je .bad
    clc
    ret
.bad:
    stc
    ret

; Out: AL = memory model of the current mode (MODEL_NONE if unknown). Preserves the rest.
cur_model:
    push si
    mov al, [BDA_MODE]
    and al, 0x7f
    call mode_desc
    mov al, MODEL_NONE
    jc .done
    mov al, [cs:si + MODE_MODEL]
.done:
    pop si
    ret

; Out: ES = screen memory segment of the current mode. Preserves the rest.
regen_seg:
    push ax
    push si
    mov al, [BDA_MODE]
    and al, 0x7f
    call mode_desc
    mov ax, 0xb800
    jc .set
    mov ax, [cs:si + MODE_SEGMENT]
.set:
    mov es, ax
    pop si
    pop ax
    ret

; In: BH = page, DH = row, DL = column (text modes).
; Out: ES:DI = that character cell. Preserves the rest.
text_addr:
    push ax
    push cx
    push dx
    mov cx, dx
    movzx ax, ch
    mul word [BDA_COLUMNS]
    movzx dx, cl
    add ax, dx
    add ax, ax
    mov di, ax
    movzx ax, bh
    mul word [BDA_PAGE_SIZE]
    add di, ax
    call regen_seg
    pop dx
    pop cx
    pop ax
    ret

; In: BH = page. Out: DX = cursor position of that page. Preserves the rest.
get_cursor:
    push bx
    movzx bx, bh
    and bl, 7
    add bx, bx
    mov dx, [BDA_CURSOR_POS + bx]
    pop bx
    ret

; In: BH = page, DH = row, DL = column. Stores the position and, for the active
; page, moves the hardware cursor. Preserves all registers.
set_cursor:
    push ax
    push bx
    push dx
    movzx bx, bh
    and bl, 7
    add bx, bx
    mov [BDA_CURSOR_POS + bx], dx
    shr bx, 1
    cmp bl, [BDA_ACTIVE_PAGE]
    jne .done
    mov bx, dx
    movzx ax, bh
    mul word [BDA_COLUMNS]
    movzx dx, bl
    add ax, dx
    mov dx, [BDA_PAGE_START]
    shr dx, 1
    add ax, dx                      ; character address
    mov bx, ax
    mov dx, [BDA_CRTC_PORT]
    mov al, 0x0e
    mov ah, bh
    out dx, ax
    mov al, 0x0f
    mov ah, bl
    out dx, ax
.done:
    pop dx
    pop bx
    pop ax
    ret

; In: AL = CRTC index. Out: AL = value. Preserves the rest.
crtc_read:
    push dx
    mov dx, [BDA_CRTC_PORT]
    out dx, al
    inc dx
    in al, dx
    pop dx
    ret

; Resets the attribute controller flip-flop to its index state. Preserves all registers.
attr_reset:
    push ax
    push dx
    mov dx, [BDA_CRTC_PORT]
    add dx, 6
    in al, dx
    pop dx
    pop ax
    ret

; Beeps the speaker for about a quarter of a second. Preserves all registers.
beep:
    push ax
    push bx
    push cx
    mov al, 0xb6
    out 0x43, al
    mov ax, 1331                    ; 1193182 Hz / 1331 = about 896 Hz
    out 0x42, al
    mov al, ah
    out 0x42, al
    in al, 0x61
    mov bl, al
    or al, 0x03
    out 0x61, al
    mov cx, 16572                   ; refresh toggles every 15.085 us
.tick:
    in al, 0x61
    and al, 0x10
    mov ah, al
    mov bh, 0                       ; 256 reads without a toggle: the bit is stuck,
.wait:                              ; so stop rather than stall
    in al, 0x61
    and al, 0x10
    cmp al, ah
    jne .next
    dec bh
    jnz .wait
    jmp .stop
.next:
    loop .tick
.stop:
    mov al, bl
    out 0x61, al
    pop cx
    pop bx
    pop ax
    ret

; ---------------------------------------------------------------------------
; AH=00h: set video mode (AL = mode; bit 7 set keeps video memory)
; ---------------------------------------------------------------------------

; Returns AL = 30h for modes 00h-05h, 07h and 0Fh, 3Fh for mode 06h, 20h for the
; other modes (the values IBM-compatible BIOSes return); an unknown mode changes nothing.
fn_set_mode:
    mov ah, al
    and al, 0x7f
    call mode_desc
    mov al, ah
    jc set_mode                     ; ignored there
    call set_mode
    mov al, [BDA_MODE]
    mov ah, 0x3f
    cmp al, 6
    je .flag
    mov ah, 0x30
    cmp al, 7
    jbe .flag
    cmp al, 0x0f
    je .flag
    mov ah, 0x20
.flag:
    mov [bp + F_AL], ah
    ret

; ---------------------------------------------------------------------------
; AH=01h: set cursor shape (CH = start line, CL = end line; CH bit 5 hides it)
;
; With cursor emulation on (the default), shapes written for an 8-line cell are
; mapped onto the current character height H (bottom line M = H - 1; the cursor's
; base line B = M for H up to 13, M - 1 from 14 on). With s = start, e = end,
; checked in this order:
;   hidden (CH bit 5), e = 0                         unchanged
;   s > e                                            e .. M
;   e = M, s <= e <= 3, or s = e = M - 1             unchanged
;   e = s, s + 1, s + 2                              B - (e - s) .. B
;   otherwise, s <= 2                                s .. M
;   otherwise                                        H / 2 .. M
; So 0607h is an underline and 0007h a full block on any font. These are the
; results IBM's VGA BIOS gives (spec/bios.md).
; ---------------------------------------------------------------------------

fn_set_cursor_type:
    mov [BDA_CURSOR_TYPE], cx
    test byte [BDA_VIDEO_CTL], VIDEO_CTL_NOEMUL
    jnz .program
    test ch, 0x20
    jnz .program
    mov bx, cx
    and bx, 0x1f1f                  ; BH = s, BL = e (unchanged shapes keep all of CX)
    mov dl, [BDA_CHAR_HEIGHT]       ; DL = H
    mov dh, dl
    dec dh                          ; DH = M
    test bl, bl
    jz .program
    cmp bh, bl
    ja .wrapped
    cmp bl, dh
    je .program
    cmp bl, 3
    jbe .program
    mov al, bl
    sub al, bh                      ; AL = e - s
    jnz .shape
    inc bl
    cmp bl, dh                      ; s = e = M - 1
    je .program
.shape:
    cmp al, 2
    ja .long
    mov ah, dh                      ; AH = B
    cmp dl, 13
    jbe .base
    dec ah
.base:
    mov cl, ah
    sub ah, al
    mov ch, ah
    jmp .program
.wrapped:
    mov ch, bl
    mov cl, dh
    jmp .program
.long:
    mov cl, dh
    mov ch, bh
    cmp bh, 2
    jbe .program
    mov ch, dl
    shr ch, 1
.program:
    mov dx, [BDA_CRTC_PORT]
    mov al, 0x0a
    mov ah, ch
    out dx, ax
    mov al, 0x0b
    mov ah, cl
    out dx, ax
    ret

; ---------------------------------------------------------------------------
; AH=02h: set cursor position (BH = page, DH = row, DL = column)
; ---------------------------------------------------------------------------

fn_set_cursor_pos:
    jmp set_cursor

; ---------------------------------------------------------------------------
; AH=03h: get cursor (BH = page) -> DX = position, CX = shape
; ---------------------------------------------------------------------------

fn_get_cursor:
    call get_cursor
    mov [bp + F_DX], dx
    mov ax, [BDA_CURSOR_TYPE]
    mov [bp + F_CX], ax
    ret

; ---------------------------------------------------------------------------
; AH=04h: light pen. VGA has none: AH = 0 (not triggered).
; ---------------------------------------------------------------------------

fn_light_pen:
    mov byte [bp + F_AH], 0
    ret

; ---------------------------------------------------------------------------
; AH=05h: select active page (AL = page)
; ---------------------------------------------------------------------------

fn_set_page:
    and al, 7
    mov [BDA_ACTIVE_PAGE], al
    movzx ax, al
    mul word [BDA_PAGE_SIZE]
    mov [BDA_PAGE_START], ax
    mov bx, ax
    ; Start address units follow the CRTC addressing mode: bytes, words or doublewords.
    mov al, 0x14
    call crtc_read
    test al, 0x40
    jz .not_dword
    shr bx, 2
    jmp .start
.not_dword:
    mov al, 0x17
    call crtc_read
    test al, 0x40
    jnz .start
    shr bx, 1
.start:
    mov dx, [BDA_CRTC_PORT]
    mov al, 0x0c
    mov ah, bh
    out dx, ax
    mov al, 0x0d
    mov ah, bl
    out dx, ax
    mov bh, [BDA_ACTIVE_PAGE]
    call get_cursor
    jmp set_cursor

; ---------------------------------------------------------------------------
; AH=0Fh: get mode -> AL = mode (bit 7 = memory kept), AH = columns, BH = page
; ---------------------------------------------------------------------------

fn_get_mode:
    mov al, [BDA_VIDEO_CTL]
    and al, VIDEO_CTL_NOCLEAR
    or al, [BDA_MODE]
    mov ah, [BDA_COLUMNS]
    mov [bp + F_AX], ax
    mov al, [BDA_ACTIVE_PAGE]
    mov [bp + F_BH], al
    ret

; ---------------------------------------------------------------------------
; AH=1Ah: display combination code.
;   AL=00h: -> AL = 1Ah, BL = active display, BH = alternate display
;   AL=01h: BL = active, BH = alternate -> AL = 1Ah
; ---------------------------------------------------------------------------

; Out: ES:DI = display combination code table (from the save pointer chain,
; falling back to the BIOS's own). Preserves the rest.
dcc_table:
    push ax
    les di, [BDA_SAVE_PTR]
    mov ax, es
    or ax, di
    jz .rom
    les di, [es:di + 16]            ; secondary save pointer table
    mov ax, es
    or ax, di
    jz .rom
    les di, [es:di + 2]             ; display combination code table
    mov ax, es
    or ax, di
    jnz .done
.rom:
    push cs
    pop es
    mov di, dcc_table_data
.done:
    pop ax
    ret

; Out: AL = active display code, AH = alternate display code (FFFFh if the
; index is out of range). Clobbers BX, DI, ES.
dcc_get:
    call dcc_table
    movzx bx, byte [BDA_DCC_INDEX]
    cmp bl, [es:di]
    jae .unknown
    add bx, bx
    mov ax, [es:di + 4 + bx]        ; low byte = alternate, high byte = active
    xchg al, ah
    ret
.unknown:
    mov ax, 0xffff
    ret

fn_dcc:
    cmp al, 1
    ja .done
    jb .get
    call dcc_table
    jmp .set
.get:
    call dcc_get
    mov [bp + F_BX], ax
    jmp .ok
.set:
    movzx cx, byte [es:di]
    lea si, [di + 4]
    xor dx, dx                      ; entry index
.find:
    jcxz .ok
    mov ax, [es:si]
    xchg al, ah                     ; AL = active, AH = alternate
    cmp ax, bx
    je .found
    add si, 2
    inc dx
    dec cx
    jmp .find
.found:
    mov [BDA_DCC_INDEX], dl
.ok:
    mov word [bp + F_AX], 0x001a
.done:
    ret

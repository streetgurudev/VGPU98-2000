; VGPU98 VGA BIOS: palette, font, alternate-select, state and save/restore services.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

; ===========================================================================
; AH=10h: palette registers and DAC
; ===========================================================================

fn_palette:
    cmp al, 0x1b
    ja .done
    movzx si, al
    add si, si
    jmp [cs:palette_table + si]
.done:
    ret

palette_table:
    dw pal_set_reg, pal_set_overscan, pal_set_all, pal_blink                ; 00h-03h
    dw fn_none, fn_none, fn_none, pal_get_reg                               ; 04h-07h
    dw pal_get_overscan, pal_get_all, fn_none, fn_none                      ; 08h-0Bh
    dw fn_none, fn_none, fn_none, fn_none                                   ; 0Ch-0Fh
    dw dac_set_reg, fn_none, dac_set_block, pal_paging                      ; 10h-13h
    dw fn_none, dac_get_reg, fn_none, dac_get_block                         ; 14h-17h
    dw dac_set_mask, dac_get_mask, pal_get_paging, dac_gray                 ; 18h-1Bh

; AL=00h: BL = palette register (0-15), BH = value
pal_set_reg:
    mov al, bl
    and al, 0x1f
    mov ah, bh
    jmp attr_write

; AL=01h: BH = border (overscan) colour
pal_set_overscan:
    mov al, 0x11
    mov ah, bh
    jmp attr_write

; AL=02h: ES:DX = 16 palette registers followed by the border colour
pal_set_all:
    mov si, dx
    xor al, al
.next:
    mov ah, [es:si]
    inc si
    call attr_write
    inc al
    cmp al, 16
    jb .next
    mov ah, [es:si]
    mov al, 0x11
    jmp attr_write

; AL=03h: BL = 0 intensity, 1 blinking (attribute bit 7)
pal_blink:
    mov al, 0x10
    call attr_read
    and ah, 0xf7
    and byte [BDA_CGA_MODE], 0xdf
    test bl, 1
    jz .write
    or ah, 0x08
    or byte [BDA_CGA_MODE], 0x20
.write:
    jmp attr_write

; AL=07h: BL = palette register -> BH = value
pal_get_reg:
    mov al, bl
    and al, 0x1f
    call attr_read
    mov [bp + F_BH], ah
    ret

; AL=08h: -> BH = border colour
pal_get_overscan:
    mov al, 0x11
    call attr_read
    mov [bp + F_BH], ah
    ret

; AL=09h: ES:DX <- 16 palette registers and the border colour
pal_get_all:
    mov di, dx
    xor al, al
.next:
    call attr_read
    mov [es:di], ah
    inc di
    inc al
    cmp al, 16
    jb .next
    mov al, 0x11
    call attr_read
    mov [es:di], ah
    ret

; AL=10h: BX = DAC register, DH = red, CH = green, CL = blue
dac_set_reg:
    mov si, dx
    mov dx, PORT_DAC_WRITE_INDEX
    mov al, bl
    out dx, al
    inc dx
    mov ax, si
    mov al, ah                      ; red
    mov ah, ch                      ; green
    mov bl, cl                      ; blue
    jmp dac_out_rgb

; AL=12h: BX = first register, CX = count, ES:DX = red/green/blue triples
dac_set_block:
    mov si, dx
    mov dx, PORT_DAC_WRITE_INDEX
    mov al, bl
    out dx, al
    inc dx
    jcxz .done
.next:
    mov al, [es:si]
    mov ah, [es:si + 1]
    mov bl, [es:si + 2]
    add si, 3
    call dac_out_rgb
    loop .next
.done:
    ret

; AL=13h: BL = 00h: BH = paging mode (0 = 4 pages of 64, 1 = 16 pages of 16);
;         BL = 01h: BH = page to select
pal_paging:
    test bl, bl
    jnz .page
    mov al, 0x10
    call attr_read
    and ah, 0x7f
    test bh, 1
    jz .write
    or ah, 0x80
.write:
    jmp attr_write
.page:
    cmp bl, 1
    jne .done
    mov al, 0x10
    call attr_read
    test ah, 0x80
    mov ah, bh
    jnz .sixteen
    shl ah, 2
    and ah, 0x0c                    ; 4 x 64: page supplies colour bits 7-6
    jmp .select
.sixteen:
    and ah, 0x0f                    ; 16 x 16: page supplies colour bits 7-4
.select:
    mov al, 0x14
    jmp attr_write
.done:
    ret

; AL=15h: BX = DAC register -> DH = red, CH = green, CL = blue
dac_get_reg:
    mov dx, PORT_DAC_READ_INDEX
    mov al, bl
    out dx, al
    mov dx, PORT_DAC_DATA
    in al, dx
    mov [bp + F_DH], al
    in al, dx
    mov [bp + F_CH], al
    in al, dx
    mov [bp + F_CL], al
    ret

; AL=17h: BX = first register, CX = count, ES:DX <- red/green/blue triples
dac_get_block:
    mov di, dx
    mov dx, PORT_DAC_READ_INDEX
    mov al, bl
    out dx, al
    mov dx, PORT_DAC_DATA
    jcxz .done
.next:
    in al, dx
    stosb
    in al, dx
    stosb
    in al, dx
    stosb
    loop .next
.done:
    ret

; AL=18h: BL = PEL mask
dac_set_mask:
    mov dx, PORT_DAC_MASK
    mov al, bl
    out dx, al
    ret

; AL=19h: -> BL = PEL mask
dac_get_mask:
    mov dx, PORT_DAC_MASK
    in al, dx
    mov [bp + F_BL], al
    ret

; AL=1Ah: -> BL = paging mode, BH = current page
pal_get_paging:
    mov al, 0x10
    call attr_read
    mov bl, ah
    shr bl, 7
    mov al, 0x14
    call attr_read
    mov bh, ah
    and bh, 0x0f
    test bl, bl
    jnz .store
    shr bh, 2
    and bh, 3
.store:
    mov [bp + F_BX], bx
    ret

; AL=1Bh: BX = first register, CX = count: replace each with its gray-scale sum
dac_gray:
    jcxz .done
.next:
    push bx
    push cx
    mov dx, PORT_DAC_READ_INDEX
    mov al, bl
    out dx, al
    mov dx, PORT_DAC_DATA
    in al, dx
    mov ch, al                      ; red
    in al, dx
    mov ah, al                      ; green
    in al, dx
    mov bl, al                      ; blue
    mov al, ch
    call gray_sum
    pop cx
    pop bx
    mov ah, al
    mov dx, PORT_DAC_WRITE_INDEX
    mov al, bl
    out dx, al
    inc dx
    mov al, ah
    out dx, al
    out dx, al
    out dx, al
    inc bx
    loop .next
.done:
    ret

; ===========================================================================
; AH=11h: character generator
;   AL=00h/10h  user font: ES:BP = glyphs, CX = count, DX = first character,
;               BL = block, BH = bytes per character
;   AL=01h/11h  8x14 font into block BL      AL=02h/12h  8x8 font
;   AL=04h/14h  8x16 font                    AL=03h      BL = block specifier (SR3)
;   AL=1xh also adjusts the text screen to the new height (rows, cursor, data area)
;   AL=20h      INT 1Fh = ES:BP (upper half of the 8x8 graphics font)
;   AL=21h      INT 43h = ES:BP, CX = bytes per character, BL = rows (00h: DL)
;   AL=22h-24h  INT 43h = 8x14 / 8x8 / 8x16 font, BL = rows (00h: DL)
;               rows code: 01h = 14, 02h = 25, 03h = 43
;   AL=30h      BH = font -> ES:BP = font, CX = character height, DL = rows - 1
; ===========================================================================

fn_font:
    cmp al, 0x30
    je font_info
    mov ah, al
    and ah, 0xf0
    cmp ah, 0x20
    je font_graphics
    cmp ah, 0x10
    ja .done
    mov ah, al
    and ah, 0x0f
    cmp al, 0x03
    je font_block_select
    cmp ah, 4
    ja .done
    cmp ah, 3
    je .done
    test ah, ah
    jz .user
    push ds
    push cs
    pop ds
    mov si, font8x14
    mov bh, 14
    cmp ah, 1
    je .rom
    mov si, font8x8
    mov bh, 8
    cmp ah, 2
    je .rom
    mov si, font8x16
    mov bh, 16
.rom:
    mov cx, 256
    xor dx, dx
    call load_font
    pop ds
    jmp .loaded
.user:
    mov si, [bp + F_BP]
    push ds
    push es
    pop ds
    call load_font
    pop ds
.loaded:
    test byte [bp + F_AL], 0x10
    jz .done
    mov al, bh
    call font_recalc
.done:
    ret

; AL=03h: BL = character map select (sequencer register 3)
font_block_select:
    mov dx, PORT_SEQ_INDEX
    mov al, 0x03
    mov ah, bl
    out dx, ax
    ret


; In: AL = new character height (text modes). Reprograms the character height,
; the cursor and the displayed lines for a whole number of rows, and updates the
; data area (rows, height, page size, cursor shape). The displayed lines come
; from the mode's parameter table entry, not the live CRTC, so a sequence of font
; loads never loses rows. DS = data area. Clobbers AX, BX, CX, DX, SI, DI.
font_recalc:
    push es
    push ax
    call cur_model
    cmp al, MODEL_TEXT
    pop ax
    jne .done
    cmp al, 2
    jb .done
    cmp al, 32
    ja .done
    mov bl, al                      ; BL = height

    call param_entry                ; ES:DI = the mode's parameter table entry
    jc .done
    movzx si, byte [es:di + P_CRTC + 0x12]
    mov al, [es:di + P_CRTC + 0x07]
    test al, 0x02
    jz .table_bit8
    add si, 0x100
.table_bit8:
    test al, 0x40
    jz .table_bit9
    add si, 0x200
.table_bit9:
    inc si                          ; SI = displayed scan lines
    mov ch, [es:di + P_CRTC + 0x09] ; CH bit 7 = double scanning
    test ch, 0x80
    jz .single
    shr si, 1                       ; character lines
.single:

    mov dx, [BDA_CRTC_PORT]
    mov al, 0x11                    ; lift the protection of registers 0-7
    out dx, al
    inc dx
    in al, dx
    mov bh, al                      ; BH = saved CR11
    and al, 0x7f
    out dx, al
    dec dx

    mov al, 0x09                    ; maximum scan line; bits 5-7 stay
    out dx, al
    inc dx
    in al, dx
    and al, 0xe0
    mov ah, bl
    dec ah
    or al, ah
    out dx, al
    dec dx

    mov ax, si
    div bl                          ; AL = whole rows
    xor ah, ah
    mov di, ax                      ; DI = rows
    mul bl                          ; AX = rows * height
    test ch, 0x80
    jz .end_single
    shl ax, 1
.end_single:
    dec ax                          ; new vertical display end
    mov si, ax
    mov ah, al
    mov al, 0x12
    out dx, ax
    mov al, 0x07                    ; overflow bits 1 and 6 carry display-end bits 8 and 9
    out dx, al
    inc dx
    in al, dx
    and al, 0xbd
    test si, 0x100
    jz .no_out8
    or al, 0x02
.no_out8:
    test si, 0x200
    jz .no_out9
    or al, 0x40
.no_out9:
    out dx, al
    dec dx

    ; Cursor: the bottom two lines up to height 13, the two lines above the
    ; bottom line from 14 on (the shapes IBM's BIOS sets).
    mov ah, bl
    dec ah
    cmp bl, 13
    jbe .cursor
    dec ah
.cursor:
    mov cl, ah
    mov ch, ah
    dec ch
    mov [BDA_CURSOR_TYPE], cx
    mov al, 0x0a
    mov ah, ch
    out dx, ax
    mov al, 0x0b
    mov ah, cl
    out dx, ax

    cmp dx, PORT_CRTC_MONO          ; monochrome: underline on the bottom line
    jne .underline_done
    mov al, 0x14
    mov ah, bl
    dec ah
    out dx, ax
.underline_done:

    mov al, 0x11                    ; protection back as it was
    mov ah, bh
    out dx, ax

    mov ax, di
    dec al
    mov [BDA_ROWS], al
    movzx ax, bl
    mov [BDA_CHAR_HEIGHT], ax
    mov ax, di
    mul word [BDA_COLUMNS]
    shl ax, 1
    add ax, 0x100                   ; page size: the screen plus 256 bytes, as IBM's BIOS sets it
    mov [BDA_PAGE_SIZE], ax
.done:
    pop es
    ret

; AL=20h-24h: graphics fonts
font_graphics:
    push fs
    xor si, si
    mov fs, si                      ; FS = interrupt table
    cmp al, 0x20
    jne .int43
    mov si, [bp + F_BP]
    mov [fs:0x1f * 4], si
    mov [fs:0x1f * 4 + 2], es
    jmp .done
.int43:
    cmp al, 0x24
    ja .done
    cmp al, 0x21
    jne .rom
    mov si, [bp + F_BP]             ; user font, CX = bytes per character
    mov ax, es
    jmp .set
.rom:
    mov ax, cs
    mov si, font8x14
    mov cx, 14
    cmp byte [bp + F_AL], 0x22
    je .set
    mov si, font8x8
    mov cx, 8
    cmp byte [bp + F_AL], 0x23
    je .set
    mov si, font8x16
    mov cx, 16
.set:
    mov [fs:0x43 * 4], si
    mov [fs:0x43 * 4 + 2], ax
    mov [BDA_CHAR_HEIGHT], cx
    mov al, dl                      ; rows: BL = 0 takes DL
    test bl, bl
    jz .rows
    mov al, 14
    cmp bl, 1
    je .rows
    mov al, 25
    cmp bl, 2
    je .rows
    mov al, 43
.rows:
    dec al
    mov [BDA_ROWS], al
.done:
    pop fs
    ret

; AL=30h: BH = 0 INT 1Fh, 1 INT 43h, 2 8x14, 3 8x8, 4 8x8 upper half,
; 5 9x14 alternates, 6 8x16, 7 9x16 alternates
; -> ES:BP = font, CX = character height, DL = rows - 1
font_info:
    cmp bh, 7
    ja .done
    cmp bh, 1
    ja .rom
    xor ax, ax
    mov es, ax
    mov si, 0x1f * 4
    test bh, bh
    jz .vector
    mov si, 0x43 * 4
.vector:
    mov ax, [es:si]
    mov dx, [es:si + 2]
    jmp .store
.rom:
    movzx si, bh
    sub si, 2
    add si, si
    mov ax, [cs:font_info_table + si]
    mov dx, cs
.store:
    mov [bp + F_BP], ax
    mov [bp + F_ES], dx
    mov ax, [BDA_CHAR_HEIGHT]
    mov [bp + F_CX], ax
    mov al, [BDA_ROWS]
    mov [bp + F_DL], al
.done:
    ret

font_info_table:
    dw font8x14, font8x8, font8x8 + 128 * 8, alt_9x14, font8x16, alt_9x16

; ===========================================================================
; AH=12h: alternate select
;   BL=10h  -> BH = 0 colour / 1 monochrome, BL = 3 (256 KB), CH = feature bits,
;              CL = switch settings
;   BL=30h  AL = 0/1/2: 200/350/400 scan lines for the next text mode set
;   BL=31h  AL = 0/1: default palette loading on/off
;   BL=32h  AL = 0/1: video addressing on/off
;   BL=33h  AL = 0/1: gray-scale summing on/off
;   BL=34h  AL = 0/1: cursor emulation on/off
;   BL=36h  AL = 0/1: screen on/off
;   Supported subfunctions other than 10h return AL = 12h.
; ===========================================================================

fn_alt_select:
    cmp bl, 0x10
    je .ega_info
    cmp al, 2
    ja .done
    cmp bl, 0x30
    je .scan_lines
    cmp al, 1
    ja .done
    cmp bl, 0x31
    je .palette_loading
    cmp bl, 0x32
    je .video_enable
    cmp bl, 0x33
    je .gray
    cmp bl, 0x34
    je .cursor_emulation
    cmp bl, 0x36
    je .refresh
.done:
    ret

.ega_info:
    mov al, [BDA_SWITCHES]
    mov ah, al
    and al, 0x0f
    shr ah, 4
    mov [bp + F_CL], al
    mov [bp + F_CH], ah
    mov bx, 0x0003
    cmp word [BDA_CRTC_PORT], PORT_CRTC_MONO
    jne .colour
    mov bh, 1
.colour:
    mov [bp + F_BX], bx
    ret

.scan_lines:
    mov ah, [BDA_MODESET_CTL]
    and ah, ~(MODESET_200 | MODESET_400) & 0xff
    cmp al, 1
    je .scan_store                  ; 350 lines: neither bit
    or ah, MODESET_400
    test al, al
    jnz .scan_store
    xor ah, MODESET_400 | MODESET_200
.scan_store:
    mov [BDA_MODESET_CTL], ah
    jmp .ok

.palette_loading:
    and byte [BDA_MODESET_CTL], ~MODESET_NO_PALETTE & 0xff
    test al, al
    jz .ok
    or byte [BDA_MODESET_CTL], MODESET_NO_PALETTE
    jmp .ok

.video_enable:
    mov dx, PORT_MISC_READ
    in al, dx
    or al, 0x02                     ; RAM enable
    test byte [bp + F_AL], 1
    jz .video_write
    and al, 0xfd
.video_write:
    mov dx, PORT_MISC_WRITE
    out dx, al
    jmp .ok

.gray:
    and byte [BDA_MODESET_CTL], ~MODESET_GRAY & 0xff
    test al, al
    jnz .ok
    or byte [BDA_MODESET_CTL], MODESET_GRAY
    jmp .ok

.cursor_emulation:
    and byte [BDA_VIDEO_CTL], ~VIDEO_CTL_NOEMUL & 0xff
    test al, al
    jz .ok
    or byte [BDA_VIDEO_CTL], VIDEO_CTL_NOEMUL
    jmp .ok

.refresh:
    mov dx, PORT_SEQ_INDEX
    mov al, 0x01
    out dx, al
    inc dx
    in al, dx
    and al, 0xdf
    test byte [bp + F_AL], 1
    jz .refresh_write
    or al, 0x20                     ; screen off
.refresh_write:
    out dx, al
.ok:
    mov byte [bp + F_AL], 0x12
    ret

; ===========================================================================
; AH=1Bh: functionality and state information. BX = 0, ES:DI = 64-byte buffer.
; -> AL = 1Bh. Layout: spec/bios.md.
; ===========================================================================

fn_state_info:
    test bx, bx
    jnz .done
    push di
    mov cx, 32
    xor ax, ax
    rep stosw
    pop di
    mov word [es:di], static_functionality
    mov [es:di + 2], cs
    push di
    add di, 4
    mov si, BDA_MODE                ; 40:49h-40:66h
    mov cx, 0x1e
    rep movsb
    pop di
    mov al, [BDA_ROWS]
    inc al
    mov [es:di + 0x22], al
    mov ax, [BDA_CHAR_HEIGHT]
    mov [es:di + 0x23], ax
    push di
    push es
    call dcc_get
    pop es
    pop di
    mov [es:di + 0x25], al          ; active display code
    mov [es:di + 0x26], ah          ; alternate display code

    mov al, [BDA_MODE]
    and al, 0x7f
    cmp al, VGA_MODE_COUNT
    jae .blocks
    movzx si, al
    shl si, 2
    add si, mode_info
    mov ax, [cs:si]
    mov [es:di + 0x27], ax          ; colours
    mov al, [cs:si + 2]
    mov [es:di + 0x29], al          ; pages
    mov al, [cs:si + 3]             ; scan lines (FFh: the text setting)
    cmp al, 0xff
    jne .scan
    call scan_index
    mov al, bl
.scan:
    mov [es:di + 0x2a], al

.blocks:
    mov dx, PORT_SEQ_INDEX
    in al, dx
    mov ah, al                      ; caller's sequencer index
    mov al, 0x03
    out dx, al
    inc dx
    in al, dx
    dec dx
    xchg al, ah
    out dx, al
    mov al, ah                      ; AL = character map select
    and al, 0x03
    mov bl, ah
    shr bl, 2
    and bl, 0x04
    or al, bl
    mov [es:di + 0x2b], al          ; primary character block
    mov al, ah
    shr al, 2
    and al, 0x03
    mov bl, ah
    shr bl, 3
    and bl, 0x04
    or al, bl
    mov [es:di + 0x2c], al          ; secondary character block

    mov al, [BDA_MODESET_CTL]
    and al, 0x0f                    ; VGA active, gray summing, monochrome, no default palette
    test byte [BDA_VIDEO_CTL], VIDEO_CTL_NOEMUL
    jnz .no_emulation
    or al, 0x10
.no_emulation:
    push ax
    mov al, 0x10
    call attr_read
    test ah, 0x08
    pop ax
    jz .no_blink
    or al, 0x20
.no_blink:
    mov [es:di + 0x2d], al
    mov byte [es:di + 0x31], 3      ; 256 KB of display memory

    push di
    push es
    les si, [BDA_SAVE_PTR]
    les si, [es:si + 4]             ; dynamic save area
    mov ax, es
    or ax, si
    pop es
    pop di
    jz .no_save_area
    or byte [es:di + 0x32], 0x02
.no_save_area:
    mov byte [bp + F_AL], 0x1b
.done:
    ret

; ===========================================================================
; AH=1Ch: save/restore video state. CX bit 0 = hardware, bit 1 = BIOS data,
; bit 2 = DAC.
;   AL=00h -> BX = buffer size in 64-byte blocks
;   AL=01h save to ES:BX     AL=02h restore from ES:BX
; Returns AL = 1Ch. Buffer layout: spec/bios.md.
; ===========================================================================

SR_HEADER   equ 32
SR_HW_SIZE  equ 66
SR_BDA_SIZE equ 49
SR_DAC_SIZE equ 771
SR_EXT_SIZE equ 18                  ; display engine: SR90h-9Fh and the BIOS scratch word

fn_save_restore:
    cmp al, 2
    ja .done
    and cx, 7
    call state_xfer
    mov word [bp + F_AX], 0x001c     ; AH = 0, as IBM's BIOS returns
.done:
    ret

; Shared by AH=1Ch and VBE function 04h.
; In: AL = 0 (size -> frame BX), 1 (save) or 2 (restore); CX = parts: bit 0
; hardware, bit 1 BIOS data, bit 2 DAC, bit 3 display engine (VBE only); ES:BX =
; buffer. Header words 0-3 hold the offsets of the four parts (0 = not saved).
; The display engine registers must be unlocked for bit 3.
state_xfer:
    test al, al
    jnz .transfer
    mov ax, SR_HEADER
    test cl, 1
    jz .no_hw
    add ax, SR_HW_SIZE
.no_hw:
    test cl, 2
    jz .no_bda
    add ax, SR_BDA_SIZE
.no_bda:
    test cl, 4
    jz .no_dac
    add ax, SR_DAC_SIZE
.no_dac:
    test cl, 8
    jz .no_ext
    add ax, SR_EXT_SIZE
.no_ext:
    add ax, 63
    shr ax, 6
    mov [bp + F_BX], ax
    ret
.transfer:
    cmp al, 1
    jne .restore

    ; Save. BX = buffer start, DI = write position.
    mov di, bx
    push cx
    mov cx, SR_HEADER / 2
    xor ax, ax
    rep stosw
    pop cx
    test cl, 1
    jz .save_bda
    mov ax, di
    sub ax, bx
    mov [es:bx], ax
    push cx
    call save_hw
    pop cx
.save_bda:
    test cl, 2
    jz .save_dac
    mov ax, di
    sub ax, bx
    mov [es:bx + 2], ax
    push cx
    call save_bda
    pop cx
.save_dac:
    test cl, 4
    jz .save_ext
    mov ax, di
    sub ax, bx
    mov [es:bx + 4], ax
    push cx
    call save_dac
    pop cx
.save_ext:
    test cl, 8
    jz .ok
    mov ax, di
    sub ax, bx
    mov [es:bx + 6], ax
    call save_ext
    ret

.restore:
    test cl, 2                      ; data area first: the hardware restore needs the CRTC port
    jz .restore_hw
    mov si, [es:bx + 2]
    test si, si
    jz .restore_hw
    add si, bx
    push bx
    push cx
    call restore_bda
    pop cx
    pop bx
.restore_hw:
    test cl, 1
    jz .restore_dac
    mov si, [es:bx]
    test si, si
    jz .restore_dac
    add si, bx
    push bx
    push cx
    call restore_hw
    pop cx
    pop bx
.restore_dac:
    test cl, 4
    jz .restore_ext
    mov si, [es:bx + 4]
    test si, si
    jz .restore_ext
    add si, bx
    push bx
    push cx
    call restore_dac
    pop cx
    pop bx
.restore_ext:
    test cl, 8                      ; last, so the engine comes back over a restored VGA
    jz .ok
    mov si, [es:bx + 6]
    test si, si
    jz .ok
    add si, bx
    call restore_ext
.ok:
    ret

; Hardware state, 66 bytes at ES:DI (DI advances):
;   0 sequencer index, 1-4 SR1-SR4, 5 CRTC index, 6-30 CR00-CR18,
;   31 attribute index, 32-52 AR00-AR14, 53 graphics index, 54-62 GR0-GR8,
;   63 miscellaneous output, 64-65 CRTC port. Clobbers AX, CX, DX.
save_hw:
    mov dx, PORT_SEQ_INDEX
    in al, dx
    stosb
    mov cl, 1
.seq:
    mov al, cl
    out dx, al
    inc dx
    in al, dx
    dec dx
    stosb
    inc cl
    cmp cl, 5
    jb .seq
    mov al, [es:di - 5]
    out dx, al

    mov dx, [BDA_CRTC_PORT]
    in al, dx
    stosb
    xor cl, cl
.crtc:
    mov al, cl
    out dx, al
    inc dx
    in al, dx
    dec dx
    stosb
    inc cl
    cmp cl, 25
    jb .crtc
    mov al, [es:di - 26]
    out dx, al

    call attr_reset
    mov dx, PORT_ATTR
    in al, dx
    stosb
    xor al, al
.attr:
    call attr_read
    mov [es:di], ah
    inc di
    inc al
    cmp al, 21
    jb .attr
    call attr_reset
    mov al, [es:di - 22]
    out dx, al
    call attr_reset

    mov dx, PORT_GC_INDEX
    in al, dx
    stosb
    xor cl, cl
.gc:
    mov al, cl
    out dx, al
    inc dx
    in al, dx
    dec dx
    stosb
    inc cl
    cmp cl, 9
    jb .gc
    mov al, [es:di - 10]
    out dx, al

    mov dx, PORT_MISC_READ
    in al, dx
    stosb
    mov ax, [BDA_CRTC_PORT]
    stosw
    ret

; Restores the hardware state at ES:SI (SI advances). Clobbers AX, BX, DX.
restore_hw:
    mov dx, PORT_SEQ_INDEX
    mov ax, 0x0100                  ; synchronous reset while the clock may change
    out dx, ax
    mov bx, 1
.seq:
    mov al, bl
    mov ah, [es:si + bx]
    out dx, ax
    inc bx
    cmp bx, 5
    jb .seq
    mov al, [es:si + 63]
    mov dx, PORT_MISC_WRITE
    out dx, al
    mov dx, PORT_SEQ_INDEX
    mov ax, 0x0300
    out dx, ax
    mov al, [es:si]
    out dx, al

    mov dx, [es:si + 64]
    mov al, 0x11                    ; lift the protection of registers 0-7
    mov ah, [es:si + 6 + 0x11]
    and ah, 0x7f
    out dx, ax
    xor bx, bx
.crtc:
    mov al, bl
    mov ah, [es:si + 6 + bx]
    out dx, ax
    inc bx
    cmp bx, 25
    jb .crtc
    mov al, [es:si + 5]
    out dx, al

    add dx, 6
    in al, dx                       ; attribute flip-flop to index
    mov dx, PORT_ATTR
    xor bx, bx
.attr:
    mov al, bl
    out dx, al
    mov al, [es:si + 32 + bx]
    out dx, al
    inc bx
    cmp bx, 21
    jb .attr
    mov al, [es:si + 31]
    out dx, al

    mov dx, PORT_GC_INDEX
    xor bx, bx
.gc:
    mov al, bl
    mov ah, [es:si + 54 + bx]
    out dx, ax
    inc bx
    cmp bx, 9
    jb .gc
    mov al, [es:si + 53]
    out dx, al

    mov dx, [es:si + 64]
    add dx, 6
    in al, dx
    add si, SR_HW_SIZE
    ret

; BIOS data, 49 bytes at ES:DI (DI advances): 40:49h-40:66h, 40:84h-40:8Ah,
; 40:A8h, then the INT 1Fh and INT 43h vectors. Clobbers AX, CX, SI.
save_bda:
    mov si, BDA_MODE
    mov cx, 0x1e
    rep movsb
    mov si, BDA_ROWS
    mov cx, 7
    rep movsb
    mov si, BDA_SAVE_PTR
    mov cx, 4
    rep movsb
    push ds
    xor ax, ax
    mov ds, ax
    mov si, 0x1f * 4
    movsw
    movsw
    mov si, 0x43 * 4
    movsw
    movsw
    pop ds
    ret

; Restores the BIOS data at ES:SI (SI advances). Clobbers AX, CX, DI.
restore_bda:
    push ds
    push es
    mov ax, ds
    push es
    pop ds                          ; DS:SI = buffer
    mov es, ax                      ; ES = data area
    mov di, BDA_MODE
    mov cx, 0x1e
    rep movsb
    mov di, BDA_ROWS
    mov cx, 7
    rep movsb
    mov di, BDA_SAVE_PTR
    mov cx, 4
    rep movsb
    xor ax, ax
    mov es, ax
    mov di, 0x1f * 4
    movsw
    movsw
    mov di, 0x43 * 4
    movsw
    movsw
    pop es
    pop ds
    ret

; DAC state, 771 bytes at ES:DI (DI advances): write index, PEL mask, DAC
; state, then 256 red/green/blue entries. Clobbers AX, CX, DX.
save_dac:
    mov dx, PORT_DAC_WRITE_INDEX
    in al, dx
    stosb
    mov dx, PORT_DAC_MASK
    in al, dx
    stosb
    mov dx, PORT_DAC_READ_INDEX
    in al, dx
    stosb
    xor al, al
    out dx, al
    mov dx, PORT_DAC_DATA
    mov cx, 768
.next:
    in al, dx
    stosb
    loop .next
    ret

; Restores the DAC state at ES:SI (SI advances). Clobbers AX, BX, CX, DX.
restore_dac:
    mov dx, PORT_DAC_WRITE_INDEX
    xor al, al
    out dx, al
    inc dx
    mov bx, 3
    mov cx, 768
.next:
    mov al, [es:si + bx]
    out dx, al
    inc bx
    loop .next
    mov dx, PORT_DAC_MASK
    mov al, [es:si + 1]
    out dx, al
    mov dx, PORT_DAC_WRITE_INDEX
    mov al, [es:si]
    out dx, al
    add si, SR_DAC_SIZE
    ret

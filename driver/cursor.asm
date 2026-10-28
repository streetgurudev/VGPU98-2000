; VGPU98 display driver: the cursor.
;
; The card's hardware cursor (spec/gpu-registers.md, The hardware cursor)
; shows Windows' own shapes at scan-out, so the cursor is never in video
; memory. A shape the card cannot hold, every shape while pointer trails are
; on, and every shape on a card without PROFILE.CURSOR go to the DIB
; engine's software cursor, as before; changing over hides the other
; cursor first, so there are never two. Windows may call SetCursor
; asynchronously (C1_COLORCURSOR) and calls MoveCursor at the mouse's
; interrupt time: neither waits for anything, and the cursor's place goes
; in one register write, from the place and the hot spot each stored as one
; dword (spec/win98-display-driver.md, The cursor).
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

extern DIB_SetCursorExt, DIB_MoveCursorExt

CN_CARD   equ 01h                       ; cursor_noted: the card's cursor took a shape
CN_SHAPE  equ 02h                       ; a shape the card cannot show
CN_TRAILS equ 04h                       ; pointer trails
CN_NONE   equ 08h                       ; no hardware cursor on the card

; Whether the card shows the cursor: PROFILE.CURSOR, with its registers
; mapped. At DriverInit, after map_registers. Preserves all registers.
cursor_init:
    cmp word [regs_sel], 0
    je .done
    push es
    mov es, [regs_sel]
    test dword [es:GPU_REG_PROFILE], GPU_PROFILE_CURSOR
    pop es
    setnz byte [cursor_card]
.done:
    ret

; Out: CF clear if the card can show the shape at ES:SI (a CURSORSHAPE) at
; the mode's depth: 1 to 64 pixels each way, one plane, its AND mask's rows
; holding its width, 1 bit a pixel or the screen's. Preserves all registers.
cursor_fits:
    push ax
    mov ax, [es:si + CS_cx]
    dec ax
    cmp ax, GPU_CURSOR_SIZE - 1
    ja .no                              ; none, or more than 64
    mov ax, [es:si + CS_cy]
    dec ax
    cmp ax, GPU_CURSOR_SIZE - 1
    ja .no
    cmp byte [es:si + CS_Planes], 1
    jne .no
    mov ax, [es:si + CS_cx]
    add ax, 7
    shr ax, 3
    cmp [es:si + CS_cbWidth], ax
    jb .no
    cmp word [es:si + CS_cbWidth], 16
    ja .no
    movzx ax, byte [es:si + CS_BitsPixel]
    cmp ax, 1
    je .yes
    cmp ax, [scr_bpp]
    jne .no
.yes:
    pop ax
    clc
    ret
.no:
    pop ax
    stc
    ret

; Copies the shape at ES:SI, one cursor_fits passed, to cursor_copy: its
; header and both masks. A colour image's rows are its pixels at the
; screen's bytes, in whole words as Windows' bitmaps. Preserves all
; registers.
cursor_keep:
    push ds
    push es
    pusha
    mov ax, [es:si + CS_cbWidth]
    imul ax, [es:si + CS_cy]            ; the AND mask
    mov cx, ax
    cmp byte [es:si + CS_BitsPixel], 1
    je .mono
    movzx ax, byte [es:si + CS_BitsPixel]
    shr ax, 3
    imul ax, [es:si + CS_cx]
    inc ax
    and ax, ~1
    imul ax, [es:si + CS_cy]
.mono:
    add cx, ax                          ; the image: as the mask, or its rows
    add cx, CS_SIZE
    push ds
    push es
    pop ds                              ; DS:SI = the shape
    pop es                              ; ES:DI = the copy
    mov di, cursor_copy
    cld
    rep movsb
    popa
    pop es
    pop ds
    mov byte [cursor_valid], 1
    ret

; Writes cursor_copy into slot BL's pattern in video memory: the AND mask,
; padded to 64x64 with ones (the screen shows through), then the image at
; the mode's bytes a pixel, padded with zeros, a mono image's set bits as
; all ones. In: DS = _DATA. Out: EDI = the slot's offset. Clobbers EAX,
; EBX, ECX, EDX, SI, ES.
cursor_write:
    mov es, [screen_sel]
    movzx edi, bl
    shl edi, 15                         ; CURSOR_SLOT_SIZE
    add edi, [shared + SB_reserve_base]
    add edi, CURSOR_SLOTS
    mov ax, [scr_bpp]
    shr al, 3
    mov [cursor_bytes], al              ; 1, 2 or 4

    ; Transparent everywhere first.
    push edi
    cld
    mov ecx, GPU_CURSOR_XOR / 4
    or eax, -1
    a32 rep stosd
    movzx ecx, byte [cursor_bytes]
    shl ecx, 10                         ; 64 x 64 pixels, in dwords
    xor eax, eax
    a32 rep stosd
    pop edi

    ; The AND mask's rows. Bits of the last byte past the width stay ones.
    mov si, cursor_copy + CS_SIZE
    mov edx, edi
    mov ax, [cursor_copy + CS_cy]
    mov [cursor_rows], ax
.and_row:
    push si
    mov ebx, edx
    mov cx, [cursor_copy + CS_cx]
    add cx, 7
    shr cx, 3
.and_byte:
    lodsb
    mov [es:ebx], al
    inc ebx
    loop .and_byte
    mov cx, [cursor_copy + CS_cx]
    and cl, 7
    jz .and_next
    mov al, 0ffh
    shr al, cl
    or [es:ebx - 1], al
.and_next:
    pop si
    add si, [cursor_copy + CS_cbWidth]
    add edx, GPU_CURSOR_SIZE / 8
    dec word [cursor_rows]
    jnz .and_row

    ; The image's rows, which follow the AND mask.
    lea edx, [edi + GPU_CURSOR_XOR]
    mov ax, [cursor_copy + CS_cy]
    mov [cursor_rows], ax
    cmp byte [cursor_copy + CS_BitsPixel], 1
    jne .colour_row
.mono_row:
    mov ebx, edx
    xor cx, cx
.mono_px:
    mov ax, cx
    xor ax, 7                           ; a mask's left pixel is its byte's bit 7
    bt word [si], ax
    jnc .mono_next
    cmp byte [cursor_bytes], 2
    jb .mono_8
    je .mono_16
    mov dword [es:ebx], 0ffffffffh
    jmp .mono_next
.mono_16:
    mov word [es:ebx], 0ffffh
    jmp .mono_next
.mono_8:
    mov byte [es:ebx], 0ffh
.mono_next:
    movzx eax, byte [cursor_bytes]
    add ebx, eax
    inc cx
    cmp cx, [cursor_copy + CS_cx]
    jb .mono_px
    add si, [cursor_copy + CS_cbWidth]
    movzx eax, byte [cursor_bytes]
    shl eax, 6                          ; a row of the image: 64 pixels
    add edx, eax
    dec word [cursor_rows]
    jnz .mono_row
    ret
.colour_row:
    mov ebx, edx
    movzx cx, byte [cursor_bytes]
    imul cx, [cursor_copy + CS_cx]
    push si
.colour_byte:
    lodsb
    mov [es:ebx], al
    inc ebx
    loop .colour_byte
    pop si
    movzx ax, byte [cursor_bytes]
    imul ax, [cursor_copy + CS_cx]
    inc ax
    and ax, ~1                          ; whole words, as Windows' bitmaps
    add si, ax
    movzx eax, byte [cursor_bytes]
    shl eax, 6
    add edx, eax
    dec word [cursor_rows]
    jnz .colour_row
    ret

; In: EAX = x | y << 16, EDX = hot x | hot y << 16. Out: EAX = the
; pattern's corner for CURSOR_POS, each half less the hot spot's. Clobbers
; EDX.
cursor_pos:
    push bx
    mov bx, ax
    sub bx, dx
    shr eax, 16
    shr edx, 16
    sub ax, dx
    shl eax, 16
    mov ax, bx
    pop bx
    ret

; Writes the cursor's place from MoveCursor's last, and again if a
; MoveCursor came meanwhile (it writes its own place too, unless a shape is
; on its way). In: DS = _DATA, ES = regs_sel. Clobbers EAX, ECX, EDX.
cursor_place:
.again:
    mov ecx, [cursor_moves]
    mov eax, [cursor_x]                 ; x, then y: one dword
    mov edx, [cursor_hot]
    call cursor_pos
    mov [es:GPU_REG_CURSOR_POS], eax
    cmp ecx, [cursor_moves]
    jne .again
    ret

; Gives the card the shape in cursor_copy, in the slot it is not showing,
; then its place. While a DOS session owns the screen only the copy is
; kept: the desktop's return writes it. A colour shape of another depth
; than the mode's (after a mode change) is hidden until Windows gives
; another. In: DS = _DATA. Preserves all registers.
cursor_load:
    pushad
    push es
    cmp byte [cursor_valid], 0
    je .off
    mov al, [cursor_copy + CS_BitsPixel]
    cmp al, 1
    je .depth
    cmp al, [scr_bpp]
    jne .off
.depth:
    cmp dword [pdevice], 0
    je .done
    les di, [pdevice]
    test word [es:di + DE_deFlags], BUSY
    jnz .done
    mov byte [cursor_busy], 1
    mov bl, [cursor_slot]
    xor bl, 1
    mov [cursor_slot], bl
    call cursor_write                   ; EDI = the slot
    mov eax, [cursor_copy + CS_xHot]    ; hot x, then hot y: one dword
    mov [cursor_hot], eax
    mov es, [regs_sel]
    mov [es:GPU_REG_CURSOR_BASE], edi
    mov dword [es:GPU_REG_CURSOR_CONTROL], GPU_CURSOR_ENABLE
    mov byte [cursor_busy], 0
    call cursor_place
    jmp .done
.off:
    mov es, [regs_sel]
    mov dword [es:GPU_REG_CURSOR_CONTROL], 0
.done:
    pop es
    popad
    ret

; From the card's cursor to the DIB engine's: the card's off, and the DIB
; engine told where the cursor is (MoveCursor did not tell it meanwhile).
; In: DS = _DATA. Preserves all registers.
cursor_to_dib:
    cmp byte [cursor_on_card], 0
    je .done
    push es
    pushad
    mov byte [cursor_on_card], 0
    mov es, [regs_sel]
    mov dword [es:GPU_REG_CURSOR_CONTROL], 0
    push word [cursor_x]
    push word [cursor_y]
    push dword [pdevice]
    call far DIB_MoveCursorExt
    popad
    pop es
.done:
    ret

; ---------------------------------------------------------------------------
; SetCursor(lpCursorShape) -> AX = 1. NULL hides the cursor.
; ---------------------------------------------------------------------------

global SetCursor
SetCursor:
    EXPORT_ENTER
    cmp byte [cursor_busy], 0
    jne .done                           ; called within a SetCursor: the shape on its way stands
    mov eax, [bp + 6]
    test eax, eax
    setnz byte [cursor_shown]           ; for the OpenGL ICD's escape (ddraw.asm)
    jz .hide
    cmp byte [cursor_card], 0
    je .none
    les si, [bp + 6]
    call cursor_fits
    jc .unfit
    call cursor_keep
    cmp word [cursor_trails], 0
    jne .dib                            ; the DIB engine draws the trails
    ; The card's: the DIB engine's cursor goes first.
    cmp byte [cursor_on_card], 0
    jne .load
    push dword 0
    push dword [pdevice]
    call far DIB_SetCursorExt
    mov byte [cursor_on_card], 1
    test byte [cursor_noted], CN_CARD
    jnz .load
    or byte [cursor_noted], CN_CARD
    DEBUG_TEXT DRV_TAG, "cursor: the card's (hardware)", 10
.load:
    call cursor_load
    jmp .done
.none:
    test byte [cursor_noted], CN_NONE
    jnz .dib
    or byte [cursor_noted], CN_NONE
    DEBUG_TEXT DRV_TAG, "cursor: the DIB engine's (the card has no hardware cursor)", 10
    jmp .dib
.unfit:
    mov byte [cursor_valid], 0
    test byte [cursor_noted], CN_SHAPE
    jnz .dib
    or byte [cursor_noted], CN_SHAPE
    DEBUG_TEXT DRV_TAG, "cursor: the DIB engine's for a shape the card cannot show ("
    movzx eax, word [es:si + CS_cx]
    call debug_dec
    DEBUG_TEXT "x"
    movzx eax, word [es:si + CS_cy]
    call debug_dec
    DEBUG_TEXT ", "
    movzx eax, byte [es:si + CS_Planes]
    call debug_dec
    DEBUG_TEXT " planes of "
    movzx eax, byte [es:si + CS_BitsPixel]
    call debug_dec
    DEBUG_TEXT " bits)", 10
.dib:
    call cursor_to_dib
    push dword [bp + 6]
    push dword [pdevice]
    call far DIB_SetCursorExt
    jmp .done
.hide:
    mov byte [cursor_valid], 0
    cmp byte [cursor_on_card], 0
    je .hide_dib
    mov es, [regs_sel]
    mov dword [es:GPU_REG_CURSOR_CONTROL], 0
.hide_dib:
    push dword 0
    push dword [pdevice]
    call far DIB_SetCursorExt
.done:
    mov ax, 1
    EXPORT_LEAVE 4

; ---------------------------------------------------------------------------
; MoveCursor(wAbsX, wAbsY), at the mouse's interrupt time.
; ---------------------------------------------------------------------------

global MoveCursor
MoveCursor:
    push bp
    mov bp, sp
    push ds
    push es
    push edx
    mov ax, _DATA
    mov ds, ax
    mov ax, [bp + 6]                    ; wAbsY
    shl eax, 16
    mov ax, [bp + 8]                    ; wAbsX
    mov [cursor_x], eax                 ; x, then y: one store
    cmp byte [cursor_on_card], 0
    je .dib
    cmp byte [cursor_busy], 0
    jne .counted                        ; the shape on its way places it
    mov edx, [cursor_hot]
    call cursor_pos
    mov es, [regs_sel]
    mov [es:GPU_REG_CURSOR_POS], eax
.counted:
    inc dword [cursor_moves]
    jmp .done
.dib:
    push word [bp + 8]
    push word [bp + 6]
    push dword [pdevice]
    call far DIB_MoveCursorExt
.done:
    pop edx
    pop es
    pop ds
    pop bp
    retf 4

; ---------------------------------------------------------------------------
; Pointer trails, the mode and DOS sessions
; ---------------------------------------------------------------------------

; Control's MOUSETRAILS, before the DIB engine sees it: AX = the trails'
; count, 0 for none. The DIB engine draws trails, so while they are on it
; has the cursor. In: DS = _DATA. Clobbers EAX, EBX.
cursor_set_trails:
    mov bx, [cursor_trails]
    mov [cursor_trails], ax
    test ax, ax
    jz .off
    test bx, bx
    jnz .done                           ; on already
    cmp byte [cursor_on_card], 0
    je .done
    test byte [cursor_noted], CN_TRAILS
    jnz .noted
    or byte [cursor_noted], CN_TRAILS
    DEBUG_TEXT DRV_TAG, "cursor: the DIB engine's while pointer trails are on", 10
.noted:
    call cursor_to_dib
    cmp byte [cursor_valid], 0
    je .done                            ; hidden: nothing to hand over
    push ds
    push word cursor_copy
    push dword [pdevice]
    call far DIB_SetCursorExt
    ret
.off:
    test bx, bx
    jz .done
    cmp byte [cursor_valid], 0
    je .done                            ; the DIB engine's shape is one the card cannot show
    push dword 0
    push dword [pdevice]
    call far DIB_SetCursorExt
    mov byte [cursor_on_card], 1
    call cursor_load
.done:
    ret

; After a mode set, and when the desktop comes back from a full-screen DOS
; session (which may have written over the GPU's reserve): the card's
; cursor written again, at the mode's depth. In: DS = _DATA. Preserves all
; registers.
cursor_reload:
    cmp byte [cursor_on_card], 0
    je .done
    call cursor_load
.done:
    ret

; The card's cursor off, what it shows kept: at Disable, for the text mode,
; and before every mode set and the desktop's return, so no frame shows the
; pattern at another depth or one a DOS session wrote over (cursor_reload
; brings it back). In: DS = _DATA. Preserves all registers.
cursor_disable:
    cmp byte [cursor_on_card], 0
    je .done
    push es
    mov es, [regs_sel]
    mov dword [es:GPU_REG_CURSOR_CONTROL], 0
    pop es
.done:
    ret

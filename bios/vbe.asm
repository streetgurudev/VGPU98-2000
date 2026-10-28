; VGPU98 VGA BIOS: VESA BIOS Extensions 2.0 and VBE/DDC.
;
; The extended modes come from the display mode table (card/display-modes.card,
; spec/modes.md) and run on the display engine (spec/display-engine.md). The
; engine's registers sit at sequencer indices behind a key; every VBE call
; unlocks them and leaves the lock as it found it. Behaviour: spec/bios.md.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

VBE_OK          equ 0x004f
VBE_FAILED      equ 0x014f
VBE_NO_HW       equ 0x024f          ; not supported by the hardware configuration
VBE_BAD_MODE    equ 0x034f          ; invalid in the current video mode

VBE_FLAG_LFB    equ 0x4000          ; mode number bit 14: linear framebuffer
VBE_FLAG_KEEP   equ 0x8000          ; mode number bit 15: keep video memory

; ---------------------------------------------------------------------------
; Display engine register access
; ---------------------------------------------------------------------------

; In: AL = sequencer index. Out: AL = value. Preserves the rest, including the
; caller's sequencer index.
ext_read:
    push dx
    push bx
    mov bl, al
    mov dx, PORT_SEQ_INDEX
    in al, dx
    mov bh, al                      ; caller's index
    mov al, bl
    out dx, al
    inc dx
    in al, dx
    dec dx
    xchg al, bh
    out dx, al
    mov al, bh
    pop bx
    pop dx
    ret

; In: AL = sequencer index, AH = value. Preserves all registers.
ext_write:
    push ax
    push dx
    push bx
    mov bx, ax                      ; BL = index, BH = value
    mov dx, PORT_SEQ_INDEX
    in al, dx
    push ax                         ; caller's index
    mov al, bl
    out dx, al
    inc dx
    mov al, bh
    out dx, al
    dec dx
    pop ax
    out dx, al
    pop bx
    pop dx
    pop ax
    ret

; In: AL = first index, CX = value: writes CL to AL and CH to AL + 1.
ext_write_word:
    push ax
    mov ah, cl
    call ext_write
    inc al
    mov ah, ch
    call ext_write
    pop ax
    ret

; In: AL = first index, ECX = value: writes four bytes from AL upward.
ext_write_dword:
    push ax
    push ecx
    call ext_write_word
    add al, 2
    shr ecx, 16
    call ext_write_word
    pop ecx
    pop ax
    ret

; In: AL = first index. Out: AX = the word at AL, AL + 1. Preserves the rest.
ext_read_word:
    push bx
    mov bl, al
    inc al
    call ext_read
    mov bh, al
    mov al, bl
    call ext_read
    mov ah, bh
    pop bx
    ret

; In: AL = first index. Out: EAX = the dword from AL upward. Preserves the rest.
ext_read_dword:
    push bx
    mov bl, al
    add al, 2
    call ext_read_word
    shl eax, 16
    mov al, bl
    call ext_read_word
    pop bx
    ret

; Unlocks the engine registers. Out: AL = 01h if they were already unlocked,
; 00h if they were locked (pass it to ext_relock). Preserves the rest.
ext_unlock:
    push bx
    mov al, SR_DISP_KEY
    call ext_read
    mov bl, al
    mov ax, (DISP_KEY << 8) | SR_DISP_KEY
    call ext_write
    mov al, bl
    pop bx
    ret

; In: AL = state from ext_unlock: locks the registers again if they were locked.
; Preserves all registers.
ext_relock:
    push ax
    test al, al
    jnz .done
    mov ax, SR_DISP_KEY             ; AH = 0: lock
    call ext_write
.done:
    pop ax
    ret

; Turns the display engine off, so the VGA core drives the display again.
; Used by every VGA mode set. Preserves all registers.
ext_engine_off:
    push ax
    call ext_unlock
    push ax
    mov ax, SR_DISP_CONTROL         ; AH = 0
    call ext_write
    pop ax
    call ext_relock
    pop ax
    ret

; Out: ZF clear if the engine is on. Registers unlocked. Preserves the rest.
ext_engine_on:
    push ax
    mov al, SR_DISP_CONTROL
    call ext_read
    test al, DISP_CTRL_ENABLE
    pop ax
    ret

; Out: EAX = video memory in bytes. Registers unlocked. Preserves the rest.
ext_vram_size:
    mov al, SR_DISP_VRAM_SIZE
    jmp ext_read_dword

; Out: AL = bits per pixel of the current extended mode (8, 15, 16 or 32), AH = 0.
ext_bpp:
    mov al, SR_DISP_FORMAT
    call ext_read
    xor ah, ah
    test al, al
    jnz .done
    mov al, 8                       ; FORMAT 0 acts as 8 bpp
.done:
    ret

; Out: EAX = the visible width the engine uses: WIDTH rounded down to a
; multiple of 8, within 8-1920 (spec/display-engine.md). Registers unlocked.
ext_width:
    mov al, SR_DISP_WIDTH
    call ext_read_word
    movzx eax, ax
    and al, 0xf8
    cmp eax, DISP_MAX_WIDTH
    jbe .not_wide
    mov eax, DISP_MAX_WIDTH
.not_wide:
    test eax, eax
    jnz .done
    mov al, 8
.done:
    ret

; Out: EAX = the visible height the engine uses: HEIGHT within 1-1440.
; Registers unlocked.
ext_height:
    mov al, SR_DISP_HEIGHT
    call ext_read_word
    movzx eax, ax
    cmp eax, DISP_MAX_HEIGHT
    jbe .not_tall
    mov eax, DISP_MAX_HEIGHT
.not_tall:
    test eax, eax
    jnz .done
    inc eax
.done:
    ret

; In: AL = bits per pixel. Out: AX = bytes per pixel.
bytes_per_pixel:
    add al, 7
    shr al, 3
    xor ah, ah
    ret

; Out: ESI = bytes per pixel, EDI = bytes per line of the current extended mode
; (PITCH, or width x bytes per pixel when PITCH is 0). Registers unlocked.
; Clobbers EAX.
ext_layout:
    call ext_bpp
    call bytes_per_pixel
    movzx esi, ax
    mov al, SR_DISP_PITCH
    call ext_read_word
    and ax, ~3
    movzx edi, ax
    test edi, edi
    jnz .done
    call ext_width
    mov edi, eax
    imul edi, esi
.done:
    ret

; Waits for the start of vertical retrace (bounded). Preserves all registers.
wait_retrace:
    push ax
    push cx
    push dx
    mov dx, PORT_STATUS_COLOR
    mov cx, 0xffff
.in_retrace:                        ; let a retrace in progress end first
    in al, dx
    test al, 0x08
    jz .wait_start
    loop .in_retrace
.wait_start:
    mov cx, 0xffff
.not_yet:
    in al, dx
    test al, 0x08
    jnz .done
    loop .not_yet
.done:
    pop dx
    pop cx
    pop ax
    ret

; ---------------------------------------------------------------------------
; Mode table
; ---------------------------------------------------------------------------

; In: AX = VBE mode number (flag bits ignored). Out: SI = table entry (in CS) and
; CF clear if the mode exists and fits in video memory; CF set otherwise.
; Registers unlocked. Preserves the rest.
vbe_find:
    push eax
    push edx
    and ax, 0x1ff
    mov si, vbe_mode_table
.next:
    mov dx, [cs:si + VBE_ENTRY_NUMBER]
    cmp dx, 0xffff
    je .missing
    cmp dx, ax
    je .found
    add si, VBE_ENTRY_SIZE
    jmp .next
.found:
    call vbe_fits
    jmp .done
.missing:
    stc
.done:
    pop edx
    pop eax
    ret

; In: SI = table entry. Out: CF clear if pitch x height fits in video memory.
; Registers unlocked. Preserves the rest.
vbe_fits:
    push eax
    push edx
    movzx eax, word [cs:si + VBE_ENTRY_PITCH]
    movzx edx, word [cs:si + VBE_ENTRY_HEIGHT]
    imul eax, edx
    mov edx, eax
    call ext_vram_size
    cmp eax, edx                    ; CF set if VRAM < needed
    pop edx
    pop eax
    ret

; In: SI = table entry. Out: EAX = how many whole frames fit in video memory,
; at most 256 (4F01h reports one less as NumberOfImagePages), ECX = bytes per
; frame. Registers unlocked. Clobbers EDX.
vbe_pages:
    movzx ecx, word [cs:si + VBE_ENTRY_PITCH]
    movzx eax, word [cs:si + VBE_ENTRY_HEIGHT]
    imul ecx, eax
    call ext_vram_size
    xor edx, edx
    div ecx
    cmp eax, 256
    jbe .done
    mov eax, 256
.done:
    ret

; ---------------------------------------------------------------------------
; AH=4Fh: dispatch
; ---------------------------------------------------------------------------

vbe_table:
    dw vbe_info                     ; 00h
    dw vbe_mode_info                ; 01h
    dw vbe_set_mode                 ; 02h
    dw vbe_get_mode                 ; 03h
    dw vbe_state                    ; 04h
    dw vbe_window                   ; 05h
    dw vbe_scan_line                ; 06h
    dw vbe_display_start            ; 07h
    dw vbe_dac_format               ; 08h
    dw vbe_palette                  ; 09h
    dw vbe_pm_interface             ; 0Ah
VBE_LAST_FUNCTION equ 0x0a

; Functions report through the frame: AX = 004Fh success, 014Fh failed, 024Fh not
; supported by this hardware, 034Fh invalid in the current mode. A function the
; BIOS does not have returns AX unchanged (AL is then not 4Fh).
;
; The VBE functions use 32-bit registers. The INT 10h entry saves only their low
; halves, so the whole registers are saved here: callers (DOS extenders, Windows'
; V86 calls) get their upper halves back.
fn_vbe:
    cmp al, 0x15
    je .ddc
    cmp al, VBE_LAST_FUNCTION
    ja .done
    movzx bx, al
    add bx, bx
    mov bx, [cs:vbe_table + bx]
    jmp .call
.ddc:
    mov bx, vbe_ddc
.call:
    pushad
    mov word [bp + F_AX], VBE_FAILED
    call ext_unlock
    push ax                         ; lock state to restore
    mov ax, bx
    mov bx, [bp + F_BX]
    call ax
    pop ax
    call ext_relock
    popad
.done:
    ret

; ---------------------------------------------------------------------------
; 00h: controller information. ES:DI = 256-byte block (512 if it starts "VBE2").
; ---------------------------------------------------------------------------

VBE_INFO_MODES  equ 34              ; mode list built in the reserved area
VBE_INFO_OEM    equ 256             ; VBE 2.0 OEM data area

vbe_info:
    xor dx, dx                      ; DX = 1: VBE 2.0 block
    cmp dword [es:di], 'VBE2'
    jne .clear
    inc dx
.clear:
    push di
    mov cx, 128
    test dx, dx
    jz .clear_size
    mov cx, 256
.clear_size:
    xor ax, ax
    rep stosw
    pop di

    mov dword [es:di], 'VESA'
    mov word [es:di + 4], 0x0200    ; VBE 2.0
    mov dword [es:di + 10], 0       ; 6-bit DAC, VGA compatible, normal RAMDAC
    call ext_vram_size
    shr eax, 16
    mov [es:di + 18], ax            ; total memory in 64 KB units

    ; Mode list in the reserved area: only modes that fit in video memory.
    lea bx, [di + VBE_INFO_MODES]
    mov [es:di + 14], bx
    mov [es:di + 16], es
    mov si, vbe_mode_table
.mode:
    mov ax, [cs:si + VBE_ENTRY_NUMBER]
    cmp ax, 0xffff
    je .modes_done
    call vbe_fits
    jc .skip
    mov [es:bx], ax
    add bx, 2
.skip:
    add si, VBE_ENTRY_SIZE
    jmp .mode
.modes_done:
    mov word [es:bx], 0xffff

    ; Strings: pointers into the ROM for VBE 1.x callers, copied into the
    ; block's OEM area for VBE 2.0 callers.
    mov word [es:di + 20], CARD_BIOS_REVISION
    test dx, dx
    jnz .vbe2
    mov word [es:di + 6], str_vbe_oem
    mov [es:di + 8], cs
    mov word [es:di + 22], str_vbe_vendor
    mov [es:di + 24], cs
    mov word [es:di + 26], str_vbe_product
    mov [es:di + 28], cs
    mov word [es:di + 30], str_vbe_revision
    mov [es:di + 32], cs
    jmp .ok
.vbe2:
    lea bx, [di + VBE_INFO_OEM]
    mov si, str_vbe_oem
    mov ax, 6
    call .copy
    mov si, str_vbe_vendor
    mov ax, 22
    call .copy
    mov si, str_vbe_product
    mov ax, 26
    call .copy
    mov si, str_vbe_revision
    mov ax, 30
    call .copy
.ok:
    mov word [bp + F_AX], VBE_OK
    ret

; Copies the CS string at SI to ES:BX, stores its far pointer at ES:DI + AX.
.copy:
    push di
    add di, ax
    mov [es:di], bx
    mov [es:di + 2], es
    pop di
.copy_byte:
    mov al, [cs:si]
    mov [es:bx], al
    inc si
    inc bx
    test al, al
    jnz .copy_byte
    ret

; ---------------------------------------------------------------------------
; 01h: mode information. CX = mode, ES:DI = 256-byte block.
; ---------------------------------------------------------------------------

vbe_mode_info:
    mov ax, cx
    call vbe_find
    jc .done                        ; status stays "failed"
    push di
    mov cx, 128
    xor ax, ax
    rep stosw
    pop di

    mov word [es:di + 0], 0x00bb    ; supported, extra info, colour, graphics,
                                    ; not VGA-compatible, windowed and linear
    mov al, SR_DISP_LFB_BASE
    call ext_read_dword
    mov [es:di + 40], eax           ; linear framebuffer
    test eax, eax
    jnz .lfb
    and byte [es:di + 0], 0x7f      ; BAR1 unassigned: no linear framebuffer
.lfb:
    mov byte [es:di + 2], 0x07      ; window A: exists, readable, writable
    mov word [es:di + 4], 64        ; granularity, KB
    mov word [es:di + 6], 64        ; window size, KB
    mov word [es:di + 8], 0xa000
    mov word [es:di + 12], vbe_window_far
    mov [es:di + 14], cs
    mov ax, [cs:si + VBE_ENTRY_PITCH]
    mov [es:di + 16], ax
    mov ax, [cs:si + VBE_ENTRY_WIDTH]
    mov [es:di + 18], ax
    mov ax, [cs:si + VBE_ENTRY_HEIGHT]
    mov [es:di + 20], ax
    mov byte [es:di + 22], 8        ; character cell
    mov byte [es:di + 23], 16
    mov byte [es:di + 24], 1        ; planes
    mov al, [cs:si + VBE_ENTRY_BPP]
    mov [es:di + 25], al
    mov byte [es:di + 26], 1        ; banks
    mov byte [es:di + 27], 4        ; packed pixel
    cmp al, 8
    je .pages
    mov byte [es:di + 27], 6        ; direct colour
    ; Field sizes and positions: red, green, blue, reserved.
    mov dword [es:di + 31], 0x05050a05      ; 15 bpp: R 5@10, G 5@5
    mov dword [es:di + 35], 0x0f010005      ; B 5@0, reserved 1@15
    cmp al, 15
    je .pages
    mov dword [es:di + 31], 0x05060b05      ; 16 bpp: R 5@11, G 6@5
    mov dword [es:di + 35], 0x00000005      ; B 5@0, no reserved field
    cmp al, 16
    je .pages
    mov dword [es:di + 31], 0x08081008      ; 32 bpp: R 8@16, G 8@8
    mov dword [es:di + 35], 0x18080008      ; B 8@0, reserved 8@24
.pages:
    call vbe_pages
    dec eax                         ; image pages beyond the first
    mov [es:di + 29], al
    mov byte [es:di + 30], 1        ; reserved, must be 1
                                    ; off-screen memory fields (44-49) stay 0
    mov word [bp + F_AX], VBE_OK
.done:
    ret

; ---------------------------------------------------------------------------
; 02h: set mode. BX = mode, bit 14 = linear framebuffer, bit 15 = keep memory.
; ---------------------------------------------------------------------------

vbe_set_mode:
    mov ax, bx
    and ax, 0x1ff
    cmp ax, 0x100
    jae .extended

    ; A VGA mode (7-bit number): the standard mode set, which also turns the
    ; engine off. VGA modes have no linear framebuffer.
    test bh, VBE_FLAG_LFB >> 8
    jnz .no_lfb
    mov al, bl
    call mode_desc
    jc .done
    mov al, bl
    test bh, 0x80
    jz .vga
    or al, 0x80
.vga:
    call set_mode
    jmp .ok

.extended:
    call vbe_find
    jc .done
    test bh, VBE_FLAG_LFB >> 8
    jz .lfb_ok
    mov al, SR_DISP_LFB_BASE
    call ext_read_dword
    test eax, eax
    jz .no_lfb                      ; BAR1 unassigned
.lfb_ok:
    ; Start from the packed 256-colour VGA state (mode 13h, memory kept): it
    ; loads the default palette for 8 bpp and leaves the VGA core consistent.
    push si
    mov al, 0x93
    call set_mode
    pop si
    mov bx, [bp + F_BX]             ; set_mode clobbers BX

    mov al, [cs:si + VBE_ENTRY_BPP]
    mov ah, al
    mov al, SR_DISP_FORMAT
    call ext_write
    mov cx, [cs:si + VBE_ENTRY_WIDTH]
    mov al, SR_DISP_WIDTH
    call ext_write_word
    mov cx, [cs:si + VBE_ENTRY_HEIGHT]
    mov al, SR_DISP_HEIGHT
    call ext_write_word
    mov cx, [cs:si + VBE_ENTRY_PITCH]
    mov al, SR_DISP_PITCH
    call ext_write_word
    xor ecx, ecx
    mov al, SR_DISP_START
    call ext_write_dword
    mov al, SR_DISP_BANK
    call ext_write_word
    mov ah, [cs:si + VBE_ENTRY_REFRESH]
    mov al, SR_DISP_REFRESH
    call ext_write
    mov cx, bx
    and cx, 0x1ff | VBE_FLAG_LFB | VBE_FLAG_KEEP
    mov al, SR_DISP_SCRATCH
    call ext_write_word
    mov ax, (DISP_CTRL_ENABLE << 8) | SR_DISP_CONTROL
    call ext_write

    ; 40:87h bit 7 follows the keep-memory flag, as for VGA modes (the mode 93h
    ; set left it set).
    test bh, 0x80
    jnz .ok
    and byte [BDA_VIDEO_CTL], ~VIDEO_CTL_NOCLEAR & 0xff
    call vbe_clear
.ok:
    mov word [bp + F_AX], VBE_OK
.done:
    ret
.no_lfb:
    mov word [bp + F_AX], VBE_NO_HW ; no linear framebuffer for this mode
    ret

; Clears every image page 4F01h reports (VBE 2.0: memory beyond them is left
; alone) through the A0000h window. SI = table entry. Clobbers EAX, ECX, EDX,
; DI, ES.
vbe_clear:
    call vbe_pages
    imul eax, ecx
    mov edx, eax                    ; EDX = bytes left
    mov ax, 0xa000
    mov es, ax
    xor cx, cx                      ; bank
.bank:
    mov al, SR_DISP_BANK
    call ext_write_word
    push cx
    mov ecx, 0x10000
    cmp edx, ecx
    jae .full
    mov ecx, edx
.full:
    sub edx, ecx
    shr ecx, 2
    xor di, di
    xor eax, eax
    rep stosd
    pop cx
    inc cx
    test edx, edx
    jnz .bank
    xor cx, cx
    mov al, SR_DISP_BANK
    jmp ext_write_word

; ---------------------------------------------------------------------------
; 03h: current mode -> BX
; ---------------------------------------------------------------------------

vbe_get_mode:
    call ext_engine_on
    jz .vga
    mov al, SR_DISP_SCRATCH
    call ext_read_word
    jmp .store
.vga:
    movzx ax, byte [BDA_MODE]
    and al, 0x7f
    test byte [BDA_VIDEO_CTL], VIDEO_CTL_NOCLEAR
    jz .store
    or ax, VBE_FLAG_KEEP
.store:
    mov [bp + F_BX], ax
    mov word [bp + F_AX], VBE_OK
    ret

; ---------------------------------------------------------------------------
; 04h: save/restore state. DL = 0 size, 1 save, 2 restore; CX = parts
; (bits 0-2 as AH=1Ch, bit 3 display engine); ES:BX = buffer.
; ---------------------------------------------------------------------------

vbe_state:
    cmp dl, 2
    ja .done
    mov al, dl
    and cx, 0x0f
    call state_xfer
    mov word [bp + F_AX], VBE_OK
.done:
    ret

; Display engine state, SR_EXT_SIZE bytes at ES:DI (DI advances): the mode
; registers SR90h-9Fh, then the BIOS scratch word. Clobbers AX, CX.
save_ext:
    mov cl, SR_DISP_CONTROL
.next:
    mov al, cl
    call ext_read
    stosb
    inc cl
    cmp cl, SR_DISP_CONTROL + 16
    jb .next
    mov al, SR_DISP_SCRATCH
    call ext_read_word
    stosw
    ret

; Restores the state at ES:SI, CONTROL last. Clobbers AX, CX.
restore_ext:
    mov cl, SR_DISP_CONTROL + 1
.next:
    mov al, cl
    mov ah, [es:si + 1]
    call ext_write
    inc si
    inc cl
    cmp cl, SR_DISP_CONTROL + 16
    jb .next
    mov ax, [es:si + 1]             ; scratch word
    mov cx, ax
    mov al, SR_DISP_SCRATCH
    call ext_write_word
    mov ah, [es:si - 15]            ; CONTROL, saved first
    mov al, SR_DISP_CONTROL
    jmp ext_write

; ---------------------------------------------------------------------------
; 05h: window control. BH = 0 set (BL = window, DX = 64 KB bank), 1 get.
; Also reachable by far call through the mode information (vbe_window_far).
; ---------------------------------------------------------------------------

; In: BH, BL, DX as for function 05h; registers unlocked.
; Out: AX = status, DX = bank (get). Preserves the rest.
window_ctl:
    call ext_engine_on
    jz .bad_mode
    push ax
    mov al, SR_DISP_SCRATCH + 1
    call ext_read
    test al, VBE_FLAG_LFB >> 8
    pop ax
    jnz .bad_mode                   ; not in a linear framebuffer mode
    test bl, bl
    jnz .failed                     ; only window A
    cmp bh, 1
    ja .failed
    je .get
    push cx
    mov cx, dx
    mov al, SR_DISP_BANK
    call ext_write_word
    pop cx
    jmp .ok
.get:
    mov al, SR_DISP_BANK
    call ext_read_word
    mov dx, ax
.ok:
    mov ax, VBE_OK
    ret
.failed:
    mov ax, VBE_FAILED
    ret
.bad_mode:
    mov ax, VBE_BAD_MODE
    ret

vbe_window:
    call window_ctl
    mov [bp + F_AX], ax
    cmp bh, 1
    jne .done
    mov [bp + F_DX], dx
.done:
    ret

; Direct far call version (no INT 10h frame). Destroys AX and DX.
vbe_window_far:
    push bx
    call ext_unlock
    push ax
    call window_ctl
    mov bx, ax                      ; status
    pop ax
    call ext_relock
    mov ax, bx
    pop bx
    retf

; ---------------------------------------------------------------------------
; 06h: logical scan line length. BL = 0 set pixels, 1 get, 2 set bytes, 3 get max.
; ---------------------------------------------------------------------------

vbe_scan_line:
    call ext_engine_on
    jz .bad_mode
    cmp bl, 3
    ja .done
    call ext_layout                 ; ESI = bytes per pixel, EDI = pitch
    cmp bl, 3
    je .max
    cmp bl, 1
    je .report
    ; Set: BL = 0 pixels, 2 bytes. Rounded up to whole pixels and 32-bit words,
    ; and never below the visible width.
    movzx eax, cx
    cmp bl, 2
    je .bytes
    imul eax, esi
.bytes:
    add eax, esi
    dec eax
    xor edx, edx
    div esi
    imul eax, esi                   ; whole pixels
    add eax, 3
    and eax, ~3
    push eax
    call ext_width
    mov edx, eax
    imul edx, esi
    pop eax
    cmp eax, edx
    jae .wide_enough
    mov eax, edx
.wide_enough:
    call .max_bytes
    cmp eax, edx
    ja .too_long
    mov edi, eax
    mov cx, ax
    mov al, SR_DISP_PITCH
    call ext_write_word
.report:
    mov eax, edi
.answer:                            ; EAX = bytes per line
    mov [bp + F_BX], ax
    mov ecx, eax
    xor edx, edx
    div esi
    mov [bp + F_CX], ax
    call ext_vram_size
    xor edx, edx
    div ecx
    cmp eax, 0xffff
    jbe .lines
    mov ax, 0xffff
.lines:
    mov [bp + F_DX], ax
    mov word [bp + F_AX], VBE_OK
    ret
.max:
    call .max_bytes
    mov eax, edx
    jmp .answer
.too_long:
    mov word [bp + F_AX], VBE_NO_HW
    ret
.bad_mode:
    mov word [bp + F_AX], VBE_BAD_MODE
.done:
    ret

; Out: EDX = longest line in bytes: the lesser of 65532 and video memory / height,
; in 32-bit words. Preserves EAX.
.max_bytes:
    push eax
    push ecx
    call ext_height
    mov ecx, eax
    call ext_vram_size
    xor edx, edx
    div ecx
    cmp eax, 65532
    jbe .max_fits
    mov eax, 65532
.max_fits:
    and eax, ~3
    mov edx, eax
    pop ecx
    pop eax
    ret

; ---------------------------------------------------------------------------
; 07h: display start. BL = 0 or 80h set (CX = pixel, DX = line), 1 get.
; ---------------------------------------------------------------------------

vbe_display_start:
    call ext_engine_on
    jz .bad_mode
    call ext_layout                 ; ESI = bytes per pixel, EDI = pitch
    cmp bl, 1
    je .get
    test bl, 0x7f
    jnz .done
    ; The first line must lie in video memory (keeps the products below 32 bits).
    call ext_vram_size
    push edx
    xor edx, edx
    div edi                         ; EAX = lines in video memory
    pop edx
    push ecx
    movzx ecx, dx
    cmp ecx, eax
    pop ecx
    jae .done
    ; start = line x pitch + pixel x bytes per pixel, in 32-bit words
    movzx eax, dx
    imul eax, edi
    movzx ecx, cx
    imul ecx, esi
    add eax, ecx
    and eax, ~3
    ; The whole visible frame must fit in video memory.
    push eax
    mov ecx, eax
    call ext_height
    dec eax
    imul eax, edi
    add ecx, eax
    call ext_width
    imul eax, esi
    add ecx, eax                    ; ECX = end of the frame
    call ext_vram_size
    cmp ecx, eax
    pop ecx                         ; ECX = start
    ja .done                        ; fails, no change
    mov al, SR_DISP_START           ; the engine takes it with the high byte
    call ext_write_dword
    test bl, 0x80
    jz .set
    call wait_retrace               ; returns once the new start is on screen
.set:
    mov word [bp + F_AX], VBE_OK
    ret
.get:
    mov al, SR_DISP_START
    call ext_read_dword
    xor edx, edx
    div edi                         ; EAX = line, EDX = byte in line
    mov [bp + F_DX], ax
    mov eax, edx
    xor edx, edx
    div esi
    mov [bp + F_CX], ax
    mov byte [bp + F_BH], 0
    mov word [bp + F_AX], VBE_OK
    ret
.bad_mode:
    mov word [bp + F_AX], VBE_BAD_MODE
.done:
    ret

; ---------------------------------------------------------------------------
; 08h: DAC palette format. The DAC is fixed at 6 bits per primary.
; ---------------------------------------------------------------------------

vbe_dac_format:
    cmp bl, 1
    ja .done
    call ext_engine_on
    jz .ok
    call ext_bpp
    cmp al, 8
    jne .bad_mode                   ; direct colour mode
.ok:
    mov byte [bp + F_BH], 6
    mov word [bp + F_AX], VBE_OK
.done:
    ret
.bad_mode:
    mov word [bp + F_AX], VBE_BAD_MODE
    ret

; ---------------------------------------------------------------------------
; 09h: palette data. BL = 0 or 80h set, 1 get, 2/3 secondary (none);
; CX = count, DX = first, ES:DI = entries of blue, green, red, alignment.
; ---------------------------------------------------------------------------

vbe_palette:
    cmp bl, 1
    je .get
    cmp bl, 2
    je .no_secondary
    cmp bl, 3
    je .no_secondary
    test bl, 0x7f
    jnz .done
    test bl, 0x80
    jz .set
    call wait_retrace
.set:
    call dac_write_entries
    jmp .ok
.get:
    mov ax, dx
    mov dx, PORT_DAC_READ_INDEX
    out dx, al
    mov dx, PORT_DAC_DATA
    jcxz .ok
.get_next:
    in al, dx
    mov [es:di + 2], al             ; red
    in al, dx
    mov [es:di + 1], al             ; green
    in al, dx
    mov [es:di], al                 ; blue
    mov byte [es:di + 3], 0
    add di, 4
    loop .get_next
.ok:
    mov word [bp + F_AX], VBE_OK
.done:
    ret
.no_secondary:
    mov word [bp + F_AX], VBE_NO_HW
    ret

; In: CX = count, DX = first entry, ES:DI = blue, green, red, alignment entries.
; Clobbers AX, CX, DX, DI.
dac_write_entries:
    mov ax, dx
    mov dx, PORT_DAC_WRITE_INDEX
    out dx, al
    inc dx
    jcxz .done
.next:
    mov al, [es:di + 2]
    out dx, al
    mov al, [es:di + 1]
    out dx, al
    mov al, [es:di]
    out dx, al
    add di, 4
    loop .next
.done:
    ret

; ---------------------------------------------------------------------------
; 0Ah: protected mode interface -> ES:DI = table, CX = its length
; ---------------------------------------------------------------------------

vbe_pm_interface:
    test bl, bl
    jnz .done
    mov [bp + F_ES], cs
    mov word [bp + F_DI], vbe_pm_table
    mov word [bp + F_CX], vbe_pm_end - vbe_pm_table
    mov word [bp + F_AX], VBE_OK
.done:
    ret

; ---------------------------------------------------------------------------
; 15h: VBE/DDC. BL = 0 capabilities, 1 read EDID (DX = block, ES:DI = 128 bytes).
; ---------------------------------------------------------------------------

vbe_ddc:
    test bl, bl
    jz .capabilities
    cmp bl, 1
    jne .done
    test dx, dx
    jnz .done                       ; one block only
    mov ax, SR_DISP_EDID_INDEX      ; AH = 0: from byte 0
    call ext_write
    mov cx, 128
.byte:
    mov al, SR_DISP_EDID_DATA
    call ext_read
    stosb
    loop .byte
    jmp .ok
.capabilities:
    mov word [bp + F_BX], 0x0102    ; DDC2, about one second per block
.ok:
    mov word [bp + F_AX], VBE_OK
.done:
    ret

; ---------------------------------------------------------------------------
; Protected mode code (32-bit, position independent; called with near CALL)
; ---------------------------------------------------------------------------

vbe_pm_table:
    dw pm_set_window - vbe_pm_table
    dw pm_display_start - vbe_pm_table
    dw pm_palette - vbe_pm_table
    dw 0                            ; no extra ports or memory: all are VGA ports

bits 32

; Writes AH to engine register AL, unlocking around the write. Clobbers EAX, EDX.
pm_ext_write:
    push ebx
    push eax
    mov dx, PORT_SEQ_INDEX
    in al, dx
    mov bh, al                      ; caller's index
    mov al, SR_DISP_KEY
    out dx, al
    inc dx
    in al, dx
    mov bl, al                      ; lock state
    mov al, DISP_KEY
    out dx, al
    dec dx
    pop eax
    out dx, al
    inc dx
    xchg al, ah
    out dx, al
    xchg al, ah
    dec dx
    test bl, bl
    jnz .unlocked
    push eax
    mov al, SR_DISP_KEY
    out dx, al
    inc dx
    xor al, al
    out dx, al
    dec dx
    pop eax
.unlocked:
    mov al, bh
    out dx, al
    pop ebx
    ret

; Waits for the next vertical retrace to begin (bounded). Clobbers EAX, ECX, EDX.
pm_wait_retrace:
    mov dx, PORT_STATUS_COLOR
    mov ecx, 0x100000
.in_retrace:                        ; let a retrace in progress end first
    in al, dx
    test al, 0x08
    jz .wait_start
    loop .in_retrace
.wait_start:
    mov ecx, 0x100000
.not_yet:
    in al, dx
    test al, 0x08
    jnz .done
    loop .not_yet
.done:
    ret

; The functions below preserve every register except EAX.

; BL = window (0), DX = bank
pm_set_window:
    push edx
    mov ah, dl
    mov al, SR_DISP_BANK
    call pm_ext_write
    mov edx, [esp]
    mov ah, dh
    mov al, SR_DISP_BANK + 1
    call pm_ext_write
    pop edx
    ret

; BL = 0 or 80h, DX:CX = start address / 4
pm_display_start:
    push ecx
    push edx
    shl edx, 16
    mov dx, cx
    shl edx, 2                      ; byte address
    push edx
    mov ah, dl
    mov al, SR_DISP_START
    call pm_ext_write
    mov edx, [esp]
    mov ah, dh
    mov al, SR_DISP_START + 1
    call pm_ext_write
    mov edx, [esp]
    shr edx, 16
    mov ah, dl
    mov al, SR_DISP_START + 2
    call pm_ext_write
    pop edx
    shr edx, 24
    mov ah, dl
    mov al, SR_DISP_START + 3
    call pm_ext_write               ; the engine takes the start with this byte
    test bl, 0x80
    jz .done
    call pm_wait_retrace            ; returns once the new start is on screen
.done:
    pop edx
    pop ecx
    ret

; BL = 0 or 80h, CX = count, DX = first, ES:EDI = blue, green, red, alignment
pm_palette:
    push ecx
    push edx
    push edi
    test bl, 0x80
    jz .load
    call pm_wait_retrace
    mov edx, [esp + 4]
    mov ecx, [esp + 8]
.load:
    mov al, dl
    mov dx, PORT_DAC_WRITE_INDEX
    out dx, al
    inc dx
    movzx ecx, cx
    jecxz .done
.next:
    mov al, [es:edi + 2]
    out dx, al
    mov al, [es:edi + 1]
    out dx, al
    mov al, [es:edi]
    out dx, al
    add edi, 4
    loop .next
.done:
    pop edi
    pop edx
    pop ecx
    ret

bits 16

vbe_pm_end:

str_vbe_oem:        db "VGPU98", 0
str_vbe_vendor:     db "VGPU98 Project", 0
str_vbe_product:    db "VGPU98", 0
str_vbe_revision:   db "1.00", 0

%include "vbemodes.inc"

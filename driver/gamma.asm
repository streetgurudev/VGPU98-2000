; VGPU98 display driver: the gamma table.
;
; Windows' SetDeviceGammaRamp and DirectDraw's gamma control (and with it
; Direct3D's) come to DDIGammaRamp, ordinal 32, which C1_GAMMA_RAMP in
; dpCaps1 announces: a ramp of 768 words, red, green and blue, 256 each,
; from 0 to FFFFh, of which an 8-bit DAC takes each word's high byte. The
; ramp is global and stays until the next set; DirectDraw saves and restores
; a full-screen program's itself. It is for the direct colour modes only: at
; 8 bpp the DAC is the palette. (The Windows 98 DDK's gamma interface, read
; for facts.)
;
; The card takes its table at the vertical blank after GAMMA_BASE or
; GAMMA_CONTROL is written, from the slot it is not showing, so no frame
; shows half a table. Nothing here waits for the GPU.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

GN_SETS   equ 01h                       ; gamma_noted: the later sets go uncounted in the log
GN_GETS   equ 02h                       ; and the later gets
GN_8BPP   equ 04h                       ; a refusal at 8 bpp
GN_NONE   equ 08h                       ; a refusal without the card's gamma
GAMMA_LOGGED equ 4                      ; the first sets and gets logged one by one

; Whether the card has the gamma table: PROFILE.GAMMA, with its registers
; mapped. At DriverInit, after map_registers. Preserves all registers.
gamma_init:
    cmp word [regs_sel], 0
    je .no_regs
    push es
    mov es, [regs_sel]
    test dword [es:GPU_REG_PROFILE], GPU_PROFILE_GAMMA
    pop es
    setnz byte [gamma_card]
    jz .none
    DEBUG_TEXT DRV_TAG, "gamma: the card's table (DDIGammaRamp, C1_GAMMA_RAMP)", 10
    ret
.none:
    DEBUG_TEXT DRV_TAG, "gamma: none (the card has no gamma table: 86Box's Gamma setting is Off)", 10
    ret
.no_regs:
    DEBUG_TEXT DRV_TAG, "gamma: none (no register window)", 10
    ret

; Out: ZF set if the kept ramp is the identity, each word's high byte its
; index. Preserves all registers.
gamma_is_identity:
    push ax
    push cx
    push si
    xor si, si
    xor cx, cx
.entry:
    mov al, [gamma_words + si + 1]
    cmp al, cl
    jne .out
    mov al, [gamma_words + 512 + si + 1]
    cmp al, cl
    jne .out
    mov al, [gamma_words + 1024 + si + 1]
    cmp al, cl
    jne .out
    add si, 2
    inc cx
    cmp cx, GPU_GAMMA_ENTRIES
    jne .entry
    cmp cx, cx                          ; ZF: the identity
.out:
    pop si
    pop cx
    pop ax
    ret

; Writes the kept ramp into slot BL's table in video memory: entry i is the
; high bytes of red's, green's and blue's words i, R << 16 | G << 8 | B.
; In: DS = _DATA. Out: EDI = the slot's offset. Clobbers EAX, ECX, SI, ES.
gamma_write:
    mov es, [screen_sel]
    movzx edi, bl
    shl edi, 10                         ; GPU_GAMMA_ALIGN: 1 KB a slot
    add edi, [shared + SB_reserve_base]
    add edi, GAMMA_SLOTS
    push edi
    cld
    xor si, si
    mov cx, GPU_GAMMA_ENTRIES
.entry:
    xor eax, eax
    mov al, [gamma_words + si + 1]      ; red
    shl eax, 8
    mov al, [gamma_words + 512 + si + 1] ; green
    shl eax, 8
    mov al, [gamma_words + 1024 + si + 1] ; blue
    a32 stosd
    add si, 2
    loop .entry
    pop edi
    ret

; Gives the card the kept ramp, in the slot it is not using: GAMMA_BASE
; names it and GAMMA_CONTROL turns it on; the identity, no ramp yet, and
; 8 bpp turn it off. While a DOS session owns the screen only the ramp is
; kept: the desktop's return writes it. In: DS = _DATA. Preserves all
; registers.
gamma_load:
    cmp byte [gamma_card], 0
    je .done
    pushad
    push es
    cmp dword [pdevice], 0
    je .out
    les di, [pdevice]
    test word [es:di + DE_deFlags], BUSY
    jnz .out
    xor eax, eax                        ; GAMMA_CONTROL: off
    cmp byte [gamma_valid], 0
    je .control
    cmp word [scr_bpp], 8
    je .control
    call gamma_is_identity
    je .control
    mov bl, [gamma_slot]
    xor bl, 1
    mov [gamma_slot], bl
    call gamma_write                    ; EDI = the slot
    mov es, [regs_sel]
    mov [es:GPU_REG_GAMMA_BASE], edi
    mov eax, GPU_GAMMA_ENABLE
.control:
    mov es, [regs_sel]
    mov [es:GPU_REG_GAMMA_CONTROL], eax
.out:
    pop es
    popad
.done:
    ret

; After a mode set, and when the desktop comes back from a full-screen DOS
; session (which may have written over the GPU's reserve): the table
; written again (the ramp stays until the next set, whatever the mode). In:
; DS = _DATA. Preserves all registers.
gamma_reload:
    call gamma_load
    ret

; The card's gamma off, the ramp kept: at Disable, and before every mode set
; and the desktop's return, so the card never reads a slot a DOS session
; wrote over (gamma_reload brings it back). In: DS = _DATA. Preserves all
; registers.
gamma_disable:
    cmp byte [gamma_card], 0
    je .done
    push es
    mov es, [regs_sel]
    mov dword [es:GPU_REG_GAMMA_CONTROL], 0
    pop es
.done:
    ret

; At Disable: the sets and gets the log did not show one by one. In: DS =
; _DATA. Preserves all registers.
gamma_totals:
    cmp dword [gamma_sets], GAMMA_LOGGED
    ja .write
    cmp dword [gamma_gets], GAMMA_LOGGED
    jbe .done
.write:
    push eax
    DEBUG_TEXT DRV_TAG, "gamma: "
    mov eax, [gamma_sets]
    call debug_dec
    DEBUG_TEXT " sets and "
    mov eax, [gamma_gets]
    call debug_dec
    DEBUG_TEXT " gets in all", 10
    pop eax
.done:
    ret

; ---------------------------------------------------------------------------
; DDIGammaRamp(lpDevice, fGetSet, lpGammaRamp) -> AX = 1, or 0 refused.
; fGetSet nonzero sets the ramp at lpGammaRamp, 0 gets the ramp into it
; (the sense the DDK's sample and the community's drivers use).
; ---------------------------------------------------------------------------

global DDIGammaRamp
DDIGammaRamp:
    EXPORT_ENTER
    cmp byte [gamma_card], 0
    je .none
    cmp word [scr_bpp], 8
    je .palette
    cmp word [bp + 10], 0               ; fGetSet
    je .get

    ; Set: the ramp kept, then the card's table.
    push ds
    lds si, [bp + 6]
    mov ax, _DATA
    mov es, ax
    mov di, gamma_words
    mov cx, 768
    cld
    rep movsw
    pop ds
    mov byte [gamma_valid], 1
    call gamma_load
    inc dword [gamma_sets]
    cmp dword [gamma_sets], GAMMA_LOGGED
    ja .set_quiet
    DEBUG_TEXT DRV_TAG, "gamma: set "
    mov eax, [gamma_sets]
    call debug_dec
    DEBUG_TEXT " (red, green, blue at 128: "
    movzx eax, byte [gamma_words + 256 + 1]
    call debug_dec
    DEBUG_TEXT "/"
    movzx eax, byte [gamma_words + 512 + 256 + 1]
    call debug_dec
    DEBUG_TEXT "/"
    movzx eax, byte [gamma_words + 1024 + 256 + 1]
    call debug_dec
    DEBUG_TEXT ")", 10
    jmp .ok
.set_quiet:
    test byte [gamma_noted], GN_SETS
    jnz .ok
    or byte [gamma_noted], GN_SETS
    DEBUG_TEXT DRV_TAG, "gamma: later sets counted, not logged", 10
    jmp .ok

.get:
    ; Get: the ramp kept, or the identity before any set.
    les di, [bp + 6]
    cld
    cmp byte [gamma_valid], 0
    je .identity
    mov si, gamma_words
    mov cx, 768
    rep movsw
    jmp .got
.identity:
    mov cx, 3
.channel:
    xor ax, ax
.word:
    stosw
    add ax, 0101h                       ; i * 257: 0 to FFFFh
    jnc .word
    loop .channel
.got:
    inc dword [gamma_gets]
    cmp dword [gamma_gets], GAMMA_LOGGED
    ja .get_quiet
    DEBUG_TEXT DRV_TAG, "gamma: get "
    mov eax, [gamma_gets]
    call debug_dec
    DEBUG_TEXT 10
    jmp .ok
.get_quiet:
    test byte [gamma_noted], GN_GETS
    jnz .ok
    or byte [gamma_noted], GN_GETS
    DEBUG_TEXT DRV_TAG, "gamma: later gets counted, not logged", 10
.ok:
    mov ax, 1
    jmp .done

.palette:
    test byte [gamma_noted], GN_8BPP
    jnz .refused
    or byte [gamma_noted], GN_8BPP
    DEBUG_TEXT DRV_TAG, "gamma: refused at 8 bpp (the DAC is the palette)", 10
    jmp .refused
.none:
    test byte [gamma_noted], GN_NONE
    jnz .refused
    or byte [gamma_noted], GN_NONE
    DEBUG_TEXT DRV_TAG, "gamma: refused (the card has no gamma table)", 10
.refused:
    xor ax, ax
.done:
    EXPORT_LEAVE 10

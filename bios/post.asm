; VGPU98 VGA BIOS: power-on initialization.
;
; The system BIOS far-calls offset 3 after copying the ROM to C0000h.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

post:
    pushf
    push ds
    push es
    pusha
    cld
    mov ax, BDA_SEG
    mov ds, ax

    DEBUG "POST: start"

    ; Interrupt vectors. The motherboard's own video handler moves to INT 42h,
    ; unless INT 10h is already ours (initialization run a second time).
    xor ax, ax
    mov es, ax
    mov ax, cs
    cmp [es:0x10 * 4 + 2], ax
    je .vectors
    mov ax, [es:0x10 * 4]
    mov [es:0x42 * 4], ax
    mov ax, [es:0x10 * 4 + 2]
    mov [es:0x42 * 4 + 2], ax
.vectors:
    mov word [es:0x10 * 4], int10_handler
    mov [es:0x10 * 4 + 2], cs
    mov word [es:0x6d * 4], int10_handler
    mov [es:0x6d * 4 + 2], cs
    mov word [es:0x1f * 4], font8x8 + 128 * 8   ; upper half of the 8x8 font
    mov [es:0x1f * 4 + 2], cs
    mov word [es:0x43 * 4], font8x8
    mov [es:0x43 * 4 + 2], cs

    ; Data area.
    mov byte [BDA_VIDEO_CTL], INIT_VIDEO_CTL
    mov byte [BDA_SWITCHES], INIT_SWITCHES
    mov byte [BDA_MODESET_CTL], INIT_MODESET_CTL
    mov byte [BDA_DCC_INDEX], INIT_DCC_INDEX
    mov word [BDA_SAVE_PTR], save_pointer_table
    mov [BDA_SAVE_PTR + 2], cs

    ; The GPU to its power-on state (GPU_RESET): whatever reset brought us
    ; here, a GPU the last boot left running writes no memory of this one.
    call ext_unlock
    push ax
    mov ax, (DISP_GPU_RESET << 8) | SR_DISP_GPU_RESET
    call ext_write
    pop ax
    call ext_relock

    mov al, 0x03
    call set_mode

    mov si, signon_text
.signon:
    mov al, [cs:si]
    inc si
    test al, al
    jz .signed_on
    push si
    mov bh, [BDA_ACTIVE_PAGE]
    mov bl, 0x07
    xor ah, ah
    call tty
    pop si
    jmp .signon
.signed_on:

    DEBUG "POST: done"

    popa
    pop es
    pop ds
    popf
    retf

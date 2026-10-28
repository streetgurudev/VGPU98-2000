; VGPU98 display driver: developer debug output.
;
; Text goes to the card's debug register (sequencer index E0h,
; spec/registers.md), one character at a time, and only while the register
; reads 01h: with 86Box's debug log setting off, or on other hardware,
; nothing is written. Every routine preserves all registers and flags.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

%include "stamp.inc"

; The start of every line the driver writes: its name and build stamp.
%define DRV_TAG "VGPU98 driver ", BUILD_STAMP, ": "

; DEBUG_TEXT "text"[, 10]: writes the text (CS-relative, inline).
%macro DEBUG_TEXT 1+
    call debug_inline
    db %1, 0
%endmacro

; In: AL = character.
debug_putc:
    pushf
    push ax
    push bx
    push dx
    mov bl, al
    mov dx, 0x3c4
    in al, dx
    mov bh, al                          ; the caller's sequencer index
    mov al, CARD_DEBUG_SEQ_INDEX
    out dx, al
    inc dx
    in al, dx
    cmp al, 1
    jne .quiet
    mov al, bl
    out dx, al
.quiet:
    dec dx
    mov al, bh
    out dx, al
    pop dx
    pop bx
    pop ax
    popf
    ret

; Writes the NUL-terminated string that follows the call, and returns past it.
debug_inline:
    push bp
    mov bp, sp
    push ax
    push si
    mov si, [bp + 2]
.next:
    mov al, [cs:si]
    inc si
    test al, al
    jz .end
    call debug_putc
    jmp .next
.end:
    mov [bp + 2], si
    pop si
    pop ax
    pop bp
    ret

; In: EAX = number, written in decimal.
debug_dec:
    pushf
    push eax
    push ecx
    push edx
    xor cx, cx
.digit:
    xor edx, edx
    push ebx
    mov ebx, 10
    div ebx
    pop ebx
    push dx
    inc cx
    test eax, eax
    jnz .digit
.out:
    pop ax
    add al, '0'
    call debug_putc
    loop .out
    pop edx
    pop ecx
    pop eax
    popf
    ret

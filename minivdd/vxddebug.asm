; VGPU98 mini-VDD: developer debug output to the card's debug register
; (sequencer index E0h, spec/registers.md), written only while it reads 01h.
; Every routine preserves all registers and flags.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

%include "stamp.inc"

; The start of every line the mini-VDD writes: its name and build stamp.
%define VXD_TAG "VGPU98 VxD ", BUILD_STAMP, ": "

; DEBUG_TEXT "text"[, 10]: writes the inline text.
%macro DEBUG_TEXT 1+
    call debug_inline
    db %1, 0
%endmacro

; In: AL = character.
debug_putc:
    pushfd
    push eax
    push ebx
    push edx
    mov bl, al
    mov dx, 3c4h
    in al, dx
    mov bh, al
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
    pop edx
    pop ebx
    pop eax
    popfd
    ret

; Writes the NUL-terminated string after the call and returns past it.
debug_inline:
    pushfd
    push eax
    push esi
    mov esi, [esp + 12]
.next:
    mov al, [esi]
    inc esi
    test al, al
    jz .end
    call debug_putc
    jmp .next
.end:
    mov [esp + 12], esi
    pop esi
    pop eax
    popfd
    ret

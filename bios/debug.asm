; VGPU98 VGA BIOS: developer debug output.
;
; Text written to sequencer index CARD_DEBUG_SEQ_INDEX appears in 86Box's log when
; the card's debug setting is on. The index reads 01h only then, and each
; character is written only after that check, so with the log off (or under a
; virtualizer that does not know the register) nothing is written there. The
; sequencer index register is restored afterwards, so a debug print never
; disturbs a caller.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

; DEBUG "text": prints the text and a line feed.
%macro DEBUG 1
    call debug_inline
    db %1, 10, 0
%endmacro

; Prints the zero-terminated string that follows the call instruction, then
; returns past it. Preserves all registers and flags.
debug_inline:
    push bp
    mov bp, sp
    pushf
    push ax
    push si
    mov si, [bp + 2]
    call debug_cs_string
    mov [bp + 2], si
    pop si
    pop ax
    popf
    pop bp
    ret

; In: CS:SI = zero-terminated string. Out: SI past the terminator. Preserves the rest.
debug_cs_string:
    push ax
.next:
    mov al, [cs:si]
    inc si
    test al, al
    jz .done
    call debug_putc
    jmp .next
.done:
    pop ax
    ret

; In: AL = character. Preserves all registers.
debug_putc:
    push ax
    push dx
    push bx
    mov bl, al
    mov dx, PORT_SEQ_INDEX
    in al, dx
    mov bh, al                      ; caller's sequencer index
    mov al, CARD_DEBUG_SEQ_INDEX
    out dx, al
    inc dx
    in al, dx
    cmp al, 0x01                    ; log listening?
    jne .skip
    mov al, bl
    out dx, al
.skip:
    dec dx
    mov al, bh
    out dx, al
    pop bx
    pop dx
    pop ax
    ret

; In: AL = byte, printed as two hex digits. Preserves all registers.
debug_hex8:
    push ax
    push ax
    shr al, 4
    call .digit
    pop ax
    and al, 0x0f
    call .digit
    pop ax
    ret
.digit:
    add al, '0'
    cmp al, '9'
    jbe .out
    add al, 'A' - '9' - 1
.out:
    jmp debug_putc

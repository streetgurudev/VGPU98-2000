; vglink test module (LE). Every label sits at a fixed offset so test_vglink
; can check the loaded values by hand-worked expectations.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

segment _LTEXT class=LCODE use32 flat align=16
segment _LDATA class=LCODE use32 flat align=16
segment _BIG   class=PCODE use32 flat align=16
segment _ITEXT class=ICODE use32 flat align=16

global TEST_DDB

segment _LTEXT
Control:                        ; LCOD 0000
    mov eax, [counter]          ; 0000: LCOD+0030 at 0001
    call FarFn                    ; 0005: relative to PCOD+12172 at 0006
    call Init                   ; 000A: relative to ICOD+0000 at 000B
    call LdataFn                ; 000F: same object, resolved by the linker: 00000020 at 0010
    ret                         ; 0014
    times 0x20 - ($ - $$) db 0xcc

segment _LDATA                  ; LCOD 0020
TEST_DDB:
    dd 0                        ; 0020
    dd Control                  ; 0024: LCOD+0000
    dd TEST_DDB + 8             ; 0028: LCOD+0028
    dd FarFn                     ; 002C: PCOD+12172 (a 32-bit target offset)
counter:
    dd 0                        ; 0030
LdataFn:
    ret                         ; 0034

segment _BIG
    times 4094 db 0
Straddle:
    dd Control                  ; PCOD 0FFE: runs across the first page boundary
    resb 70000
FarFn:                          ; PCOD 12172
    ret

segment _ITEXT
Init:                           ; ICOD 0000
    call Control                ; 0000: relative to LCOD+0000 at 0001
    mov eax, FarFn               ; 0005: PCOD+12172 at 0006
    ret

; vglink test module (NE), object A. Every label sits at a fixed offset so
; test_vglink can check the loaded values by hand-worked expectations.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

segment _TEXT class=CODE use16 align=16
segment _DATA class=DATA use16 align=16
segment _INIT class=CODE use16 align=16

extern ImpFar                   ; DIBENG.1
extern ImpSeg                   ; KERNEL.175
extern ImpOff                   ; KERNEL.18
extern FarB                     ; object B, _TEXT
global Entry, Exp2, Exp5, Exp400, Exp700, InitProc

segment _TEXT
Entry:                          ; 0000
    mov ax, _DATA               ; 0000: selector of _DATA at 0001
    mov ds, ax                  ; 0003
    call far ImpFar             ; 0005: far address of DIBENG.1 at 0006
    mov ax, seg ImpSeg          ; 000A: selector of KERNEL.175 at 000B
    mov bx, ImpOff              ; 000D: offset of KERNEL.18 at 000E
    call far FarB               ; 0010: offset 0060 at 0011, selector of _TEXT at 0013
    mov ax, 1                   ; 0015
    retf                        ; 0018
    times 0x40 - ($ - $$) db 0xcc
Exp2:                           ; 0040
    retf
    times 0x44 - ($ - $$) db 0xcc
Exp5:                           ; 0044
    retf
    times 0x48 - ($ - $$) db 0xcc
Exp400:                         ; 0048
    retf
    times 0x4c - ($ - $$) db 0xcc
Exp700:                         ; 004C
    retf
    times 0x5a - ($ - $$) db 0xcc  ; object B's _TEXT starts at 0060 (16-byte aligned)

segment _DATA
    dw Exp2, seg Exp2           ; 0000: 0040, selector of _TEXT
    dw FarB, seg FarB           ; 0004: 0060, selector of _TEXT
    dw ImpFar, seg ImpFar       ; 0008: far address of DIBENG.1
    dd 0x12345678               ; 000C: untouched
    dw InitProc, seg InitProc   ; 0010: 0004, selector of _INIT
    resb 12                     ; 0014-001F: zero

segment _INIT
    db 0x90, 0x90, 0x90, 0x90
InitProc:                       ; 0004
    retf

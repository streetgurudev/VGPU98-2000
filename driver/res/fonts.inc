; VGPU98 display driver: the stock fonts GDI asks the display for (OEM fixed,
; ANSI fixed, ANSI variable), as three LOGFONT records, each followed directly
; by its face name and a NUL (no padding), as Windows' display drivers store
; them.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

; LOGFONT height, width, charset, pitch and family, face
%macro LOGFONT 5
    dw %1               ; height
    dw %2               ; width
    dw 0                ; escapement
    dw 0                ; orientation
    dw 0                ; weight: default
    db 0, 0, 0          ; italic, underline, strike-out
    db %3               ; character set
    db 0                ; output precision
    db 2                ; clip precision: stroke
    db 2                ; quality: proof
    db %4               ; pitch: 1 fixed, 2 variable
    db %5, 0
%endmacro

OEM_CHARSET     equ 255
ANSI_CHARSET    equ 0
FIXED_PITCH     equ 1
VARIABLE_PITCH  equ 2

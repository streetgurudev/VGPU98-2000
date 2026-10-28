; vglink test module (NE), object B: a second _TEXT piece, combined after A's.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

segment _TEXT class=CODE use16 align=16

extern Exp2
global FarB

segment _TEXT
FarB:                           ; 0060 in the linked segment
    call far Exp2               ; 0060: offset 0040 at 0061, selector of _TEXT at 0063
    retf                        ; 0065

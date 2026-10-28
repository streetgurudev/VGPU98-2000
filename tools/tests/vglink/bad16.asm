; vglink negative tests (NE): each BAD_* define makes one mistake.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

segment _TEXT class=CODE use16
segment _DATA class=DATA use16

extern ImpFar
global Entry

segment _TEXT
Entry:
%ifdef BAD_UNDEFINED
extern Missing
    call far Missing
%endif
%ifdef BAD_IMPORT32
    dd ImpFar                   ; a 32-bit offset to an import
%endif
%ifdef BAD_IMPORT_PLUS
    call far ImpFar + 2         ; an import plus an offset
%endif
%ifdef BAD_NEAR_OTHER
extern InOther
    call InOther                ; near call into another segment
%endif
%ifdef BAD_BIG
    times 0x10001 db 0          ; more than 64 KB
%endif
    retf

segment _DATA
    dw 0

%ifdef BAD_UNPLACED
segment _EXTRA class=CODE use16
    db 0
%endif
%ifdef BAD_NEAR_OTHER
segment _OTHER class=CODE use16
global InOther
InOther:
    ret
%endif
%ifdef BAD_TWICE
global ImpFar2
ImpFar2:                        ; also an import in bad16-twice.lnk
%endif

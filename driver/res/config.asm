; VGPU98 display driver resource OEMBIN 1: the display's metrics and default
; system colours, in the layout Windows' display drivers have carried since
; Windows 2.x. Colours are COLORREF values: red, green, blue, 0.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

    dw 17       ; vertical scroll thumb height
    dw 17       ; horizontal scroll thumb width
    dw 2        ; icon width ratio
    dw 2        ; icon height ratio
    dw 1        ; cursor width ratio
    dw 1        ; cursor height ratio
    dw 0        ; reserved
    dw 1        ; vertical line width
    dw 1        ; horizontal line width

    db 0xc0, 0xc0, 0xc0, 0     ; scroll bar
    db 0xc0, 0xc0, 0xc0, 0     ; desktop
    db 0x00, 0x00, 0x80, 0     ; active caption
    db 0xff, 0xff, 0xff, 0     ; inactive caption
    db 0xff, 0xff, 0xff, 0     ; menu
    db 0xff, 0xff, 0xff, 0     ; window
    db 0x00, 0x00, 0x00, 0     ; window frame
    db 0x00, 0x00, 0x00, 0     ; menu text
    db 0x00, 0x00, 0x00, 0     ; window text
    db 0xff, 0xff, 0xff, 0     ; caption text
    db 0xc0, 0xc0, 0xc0, 0     ; active border
    db 0xc0, 0xc0, 0xc0, 0     ; inactive border
    db 0xff, 0xff, 0xff, 0     ; application workspace
    db 0x00, 0x00, 0x80, 0     ; highlight
    db 0xff, 0xff, 0xff, 0     ; highlighted text
    db 0xc0, 0xc0, 0xc0, 0     ; button face
    db 0x80, 0x80, 0x80, 0     ; button shadow
    db 0xc0, 0xc0, 0xc0, 0     ; grey text
    db 0x00, 0x00, 0x00, 0     ; button text
    db 0x00, 0x00, 0x00, 0     ; inactive caption text

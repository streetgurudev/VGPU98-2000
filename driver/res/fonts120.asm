; VGPU98 display driver resource OEMBIN 2003: stock fonts at 120 DPI (large
; fonts), returned by GetDriverResourceID above 96 DPI. The sizes are the
; 96 DPI ones scaled by 1.25.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

%include "fonts.inc"

    LOGFONT 15, 10, OEM_CHARSET, FIXED_PITCH, "Terminal"
    LOGFONT 15, 11, ANSI_CHARSET, FIXED_PITCH, "Courier"
    LOGFONT 15, 11, ANSI_CHARSET, VARIABLE_PITCH, "MS Sans Serif"

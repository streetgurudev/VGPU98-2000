; VGPU98 display driver: exports the DIB engine handles on its own.
;
; Most of the display driver interface goes straight to DIBENG.DLL, which
; draws into the framebuffer. A forwarder is a far jump: the DIB engine sees
; the caller's arguments and return address and returns to the caller itself.
; Some DIB engine routines take one more argument than the export (the "Ext"
; variants): the thunk slips it in under the return address, so it is the
; last Pascal argument, and jumps.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

%macro FORWARD 2                        ; export, DIB engine routine
global %1
extern %2
%1:
    jmp far %2
%endmacro

%macro WITH_PDEVICE 2                   ; export, DIB engine routine: + lpPDevice
global %1
extern %2
%1:
    mov ax, _DATA
    mov es, ax
    pop ecx                             ; the caller's return address
    push dword [es:pdevice]
    push ecx
    jmp far %2
%endmacro

%macro WITH_PALETTIZED 2                ; export, DIB engine routine: + wPalettized
global %1
extern %2
%1:
    mov ax, _DATA
    mov es, ax
    pop ecx
    push word [es:palettized]
    push ecx
    jmp far %2
%endmacro

FORWARD         BitBlt,                 DIB_BitBlt
FORWARD         ColorInfo,              DIB_ColorInfo
FORWARD         EnumDFonts,             DIB_EnumDFonts
WITH_PDEVICE    EnumObj,                DIB_EnumObjExt
FORWARD         Output,                 DIB_Output
FORWARD         Pixel,                  DIB_Pixel
WITH_PDEVICE    RealizeObject,          DIB_RealizeObjectExt
FORWARD         StrBlt,                 DIB_StrBlt
FORWARD         ScanLR,                 DIB_ScanLR
FORWARD         DeviceMode,             DIB_DeviceMode
FORWARD         ExtTextOut,             DIB_ExtTextOut
FORWARD         GetCharWidth,           DIB_GetCharWidth
FORWARD         DeviceBitmap,           DIB_DeviceBitmap
FORWARD         FastBorder,             DIB_FastBorder
FORWARD         SetAttribute,           DIB_SetAttribute
WITH_PALETTIZED DibBlt,                 DIB_DibBltExt
FORWARD         CreateDIBitmap,         DIB_CreateDIBitmap
FORWARD         DibToDevice,            DIB_DibToDevice
WITH_PDEVICE    GetPalette,             DIB_GetPaletteExt
WITH_PDEVICE    SetPaletteTranslate,    DIB_SetPaletteTranslateExt
WITH_PDEVICE    GetPaletteTranslate,    DIB_GetPaletteTranslateExt
WITH_PDEVICE    UpdateColors,           DIB_UpdateColorsExt
FORWARD         StretchBlt,             DIB_StretchBlt
FORWARD         StretchDIBits,          DIB_StretchDIBits
FORWARD         SelectBitmap,           DIB_SelectBitmap
FORWARD         BitmapBits,             DIB_BitmapBits
FORWARD         Inquire,                DIB_Inquire
; SetCursor and MoveCursor: cursor.asm.

; VGPU98 display driver: the Windows 98 interfaces it uses, restated from the
; Windows 95/98 DDK documentation and headers and checked against Windows 98's
; own binaries (spec/win98-display-driver.md). Facts only: names, offsets,
; values.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

; ---------------------------------------------------------------------------
; GDIINFO: what Enable fills when GDI asks for the device's capabilities
; ---------------------------------------------------------------------------

GI_dpVersion        equ 0
GI_dpTechnology     equ 2
GI_dpHorzSize       equ 4       ; mm
GI_dpVertSize       equ 6
GI_dpHorzRes        equ 8       ; pixels
GI_dpVertRes        equ 10
GI_dpBitsPixel      equ 12
GI_dpPlanes         equ 14
GI_dpNumBrushes     equ 16
GI_dpNumPens        equ 18
GI_dpCapsFE         equ 20
GI_dpNumFonts       equ 22
GI_dpNumColors      equ 24
GI_dpDEVICEsize     equ 26
GI_dpCurves         equ 28      ; 28-46: set by the DIB engine
GI_dpText           equ 34
GI_dpRaster         equ 38
GI_dpMLoWin         equ 48      ; mapping modes: window x, y then viewport x, y
GI_dpMLoVpt         equ 52
GI_dpMHiWin         equ 56
GI_dpMHiVpt         equ 60
GI_dpELoWin         equ 64
GI_dpELoVpt         equ 68
GI_dpEHiWin         equ 72
GI_dpEHiVpt         equ 76
GI_dpTwpWin         equ 80
GI_dpTwpVpt         equ 84
GI_dpLogPixelsX     equ 88
GI_dpLogPixelsY     equ 90
GI_dpDCManage       equ 92
GI_dpCaps1          equ 94
GI_dpNumPalReg      equ 104
GI_dpPalReserved    equ 106
GI_dpColorRes       equ 108
GDIINFO_SIZE        equ 110

DRV_VERSION         equ 0400h   ; a Windows 4.0 driver
DT_RASDISPLAY       equ 1
DC_IgnoreDFNP       equ 0004h

RC_SAVEBITMAP       equ 0040h
RC_PALETTE          equ 0100h
RC_DIBTODEV         equ 0200h

C1_GAMMA_RAMP       equ 0020h   ; the driver takes gamma ramps (DDIGammaRamp, ordinal 32)
C1_REINIT_ABLE      equ 0080h   ; ReEnable changes the mode without a restart
C1_BYTE_PACKED      equ 0400h
C1_COLORCURSOR      equ 0800h

OBJ_FONT            equ 3

; ---------------------------------------------------------------------------
; DIBENGINE: the PDEVICE header the DIB engine draws through
; ---------------------------------------------------------------------------

DE_deType           equ 0
DE_deWidth          equ 2
DE_deHeight         equ 4
DE_deWidthBytes     equ 6
DE_dePlanes         equ 8
DE_deBitsPixel      equ 9
DE_deDeltaScan      equ 14
DE_deBitsOffset     equ 22
DE_deBitsSelector   equ 26
DE_deFlags          equ 28
DE_deVersion        equ 30
DE_deBitmapInfo     equ 32
DE_deBeginAccess    equ 36
DE_deEndAccess      equ 40
DIBENGINE_SIZE      equ 48

; deFlags
MINIDRIVER          equ 0001h
PALETTIZED          equ 0002h
BUSY                equ 0010h
FIVE6FIVE           equ 0040h
VRAM                equ 8000h

CURSOREXCLUDE       equ 0008h   ; DIB_BeginAccess / DIB_EndAccess flag

; CURSORSHAPE, which SetCursor gets: the header, then the AND mask (1 bit a
; pixel, CS_cbWidth bytes a row), then the image (the same, or with
; C1_COLORCURSOR the screen's bits a pixel)
CS_xHot             equ 0
CS_yHot             equ 2
CS_cx               equ 4
CS_cy               equ 6
CS_cbWidth          equ 8
CS_Planes           equ 10
CS_BitsPixel        equ 11
CS_SIZE             equ 12

MOUSETRAILS         equ 39      ; Control: pointer trails, a count (0 none)

; DIBColorEntry: blue, green, red, flags
MAPTOWHITE          equ 01h
NONSTATIC           equ 80h

; BITMAPINFOHEADER
BIH_biSize          equ 0
BIH_biWidth         equ 4
BIH_biHeight        equ 8
BIH_biPlanes        equ 12
BIH_biBitCount      equ 14
BIH_SIZE            equ 40

; ---------------------------------------------------------------------------
; ValidateMode
; ---------------------------------------------------------------------------

DVM_dvmBpp          equ 2       ; DISPVALMODE
DVM_dvmXRes         equ 4
DVM_dvmYRes         equ 6

VALMODE_YES         equ 0
VALMODE_NO_WRONGDRV equ 1       ; the hardware is not there
VALMODE_NO_NOMEM    equ 2       ; not enough video memory
VALMODE_NO_NODAC    equ 3       ; colour depth not supported
VALMODE_NO_UNKNOWN  equ 4

; ---------------------------------------------------------------------------
; VDD: the virtual display device, through its protected-mode API
; (INT 2Fh AX=1684h, BX = 000Ah); EAX = function, EBX = VM handle
; ---------------------------------------------------------------------------

VDD_DEVICE_ID           equ 000Ah
VDD_DRIVER_REGISTER     equ 80h ; ECX = visible bytes, EDX = -1 (no windowed VGA graphics), ES:DI = restore callback
VDD_DRIVER_UNREGISTER   equ 81h
VDD_SAVE_DRIVER_STATE   equ 82h
VDD_REGISTER_DISPLAY_DRIVER_INFO equ 83h ; ECX = video memory size, DX = extra screen selector; calls the mini-VDD's function 0
VDD_GET_DISPLAY_CONFIG  equ 85h ; ECX = size, EDX = 0, ES:EDI = DISPLAYINFO; EAX = 85h no VDD, -1 failed
VDD_PRE_MODE_CHANGE     equ 86h
VDD_POST_MODE_CHANGE    equ 87h

; DISPLAYINFO
DI_diHdrSize            equ 0
DI_diInfoFlags          equ 2
DI_diXRes               equ 24
DI_diYRes               equ 26
DI_diDPI                equ 28
DI_diBpp                equ 31
DI_diRefreshRateMax     equ 32
DI_diHighVert           equ 42
DISPLAYINFO_SIZE        equ 50  ; through diVertSyncPolarity

; INT 2Fh
INT2F_GET_ENTRY         equ 1684h   ; BX = device ID: ES:DI = API entry
INT2F_GET_VM            equ 1683h   ; BX = current VM handle
INT2F_STOP_IO_TRAP      equ 4000h   ; the driver owns the video hardware
INT2F_SWITCH_OUT        equ 4001h   ; a full-screen VM takes the screen
INT2F_SWITCH_IN         equ 4002h   ; the desktop gets it back
INT2F_START_IO_TRAP     equ 4007h   ; the VDD owns the video hardware again

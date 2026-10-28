; VGPU98 VGA BIOS: shared constants.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

; ---------------------------------------------------------------------------
; BIOS data area, segment 40h
; ---------------------------------------------------------------------------

BDA_SEG             equ 0x40
BDA_EQUIPMENT       equ 0x10    ; word; bits 5-4 = initial video mode
BDA_MODE            equ 0x49    ; byte: current mode
BDA_COLUMNS         equ 0x4a    ; word: text columns
BDA_PAGE_SIZE       equ 0x4c    ; word: bytes per display page
BDA_PAGE_START      equ 0x4e    ; word: offset of the active page
BDA_CURSOR_POS      equ 0x50    ; 8 words: column (low), row (high) per page
BDA_CURSOR_TYPE     equ 0x60    ; word: end line (low), start line (high)
BDA_ACTIVE_PAGE     equ 0x62    ; byte
BDA_CRTC_PORT       equ 0x63    ; word: 3D4h or 3B4h
BDA_CGA_MODE        equ 0x65    ; byte: CGA mode select value
BDA_CGA_PALETTE     equ 0x66    ; byte: CGA colour select value
BDA_ROWS            equ 0x84    ; byte: text rows - 1
BDA_CHAR_HEIGHT     equ 0x85    ; word: scan lines per character
BDA_VIDEO_CTL       equ 0x87    ; byte: bit 7 = memory not cleared, 6-5 = 256 KB, 0 = cursor emulation off
BDA_SWITCHES        equ 0x88    ; byte: feature bits and configuration switches
BDA_MODESET_CTL     equ 0x89    ; byte: bit 7/4 = scan lines, 3 = no default palette, 1 = gray summing, 0 = VGA active
BDA_DCC_INDEX       equ 0x8a    ; byte: index into the display combination code table
BDA_SAVE_PTR        equ 0xa8    ; dword: video save pointer table

VIDEO_CTL_NOCLEAR   equ 0x80
VIDEO_CTL_NOEMUL    equ 0x01
MODESET_200         equ 0x80
MODESET_400         equ 0x10
MODESET_NO_PALETTE  equ 0x08
MODESET_GRAY        equ 0x02

; Values a VGA BIOS leaves in the data area at power-on
INIT_VIDEO_CTL      equ 0x60    ; 256 KB, cursor emulation on
INIT_SWITCHES       equ 0x09    ; switch settings 9 (colour display), no feature connector bits
INIT_MODESET_CTL    equ 0x11    ; 400 lines, VGA active
INIT_DCC_INDEX      equ 0x0b    ; DCC table entry: VGA colour, no alternate display

; ---------------------------------------------------------------------------
; INT 10h stack frame. The handler pushes DS, ES, then PUSHA, and points BP at it.
; Functions read their inputs and write their results here.
; ---------------------------------------------------------------------------

F_DI                equ 0
F_SI                equ 2
F_BP                equ 4
F_BX                equ 8
F_BL                equ 8
F_BH                equ 9
F_DX                equ 10
F_DL                equ 10
F_DH                equ 11
F_CX                equ 12
F_CL                equ 12
F_CH                equ 13
F_AX                equ 14
F_AL                equ 14
F_AH                equ 15
F_GS                equ 16
F_FS                equ 18
F_ES                equ 20
F_DS                equ 22
F_FLAGS             equ 28

; ---------------------------------------------------------------------------
; VGA ports
; ---------------------------------------------------------------------------

PORT_ATTR           equ 0x3c0
PORT_ATTR_READ      equ 0x3c1
PORT_MISC_WRITE     equ 0x3c2
PORT_SEQ_INDEX      equ 0x3c4
PORT_SEQ_DATA       equ 0x3c5
PORT_DAC_MASK       equ 0x3c6
PORT_DAC_READ_INDEX equ 0x3c7
PORT_DAC_WRITE_INDEX equ 0x3c8
PORT_DAC_DATA       equ 0x3c9
PORT_MISC_READ      equ 0x3cc
PORT_GC_INDEX       equ 0x3ce
PORT_GC_DATA        equ 0x3cf
PORT_CRTC_COLOR     equ 0x3d4
PORT_CRTC_MONO      equ 0x3b4
PORT_STATUS_COLOR   equ 0x3da
PORT_STATUS_MONO    equ 0x3ba

; Offsets inside a 64-byte video parameter table entry
P_COLUMNS           equ 0
P_ROWS              equ 1
P_CHAR_HEIGHT       equ 2
P_PAGE_SIZE         equ 3
P_SEQ               equ 5
P_MISC              equ 9
P_CRTC              equ 10
P_ATTR              equ 35
P_GC                equ 55

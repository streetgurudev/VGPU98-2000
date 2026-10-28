; VGPU98 VGA BIOS
;
; A PCI expansion ROM for the VGPU98 2000 card. The system BIOS copies it to C0000h
; and far-calls the initialization entry at offset 3. Layout: spec/rom-layout.md.
;
; Assembled by NASM as a flat binary; romfix pads it and sets the checksum byte.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

bits 16
cpu 386
org 0

%include "card.inc"
%include "defs.inc"

; ---------------------------------------------------------------------------
; Expansion ROM header
; ---------------------------------------------------------------------------

rom_header:
    db 0x55, 0xaa
    db CARD_ROM_SIZE / 512
    jmp near post                   ; 0003h: initialization entry

    times 0x18 - ($ - $$) db 0
    dw pci_data                     ; 0018h: PCI data structure
    dw 0                            ; 001Ah: no $PnP expansion header
    dw 0                            ; 001Ch: reserved
    db 'IBM VGA Compatible'         ; 001Eh: signature some software checks to detect a VGA BIOS

; ---------------------------------------------------------------------------
; PCI data structure (PCI Local Bus Specification 2.2, section 6.3.1.2)
; ---------------------------------------------------------------------------

    align 4, db 0
pci_data:
    db 'PCIR'
    dw CARD_PCI_VENDOR_ID
    dw CARD_PCI_DEVICE_ID
    dw 0                            ; no vital product data
    dw 0x18                         ; structure length
    db 0                            ; structure revision
    db CARD_PCI_CLASS_PROGIF, CARD_PCI_CLASS_SUB, CARD_PCI_CLASS_BASE
    dw CARD_ROM_SIZE / 512          ; image length in 512-byte units
    dw CARD_BIOS_REVISION           ; revision level of code and data
    db 0                            ; code type: x86 PC-AT compatible
    db 0x80                         ; last image
    dw 0

; ---------------------------------------------------------------------------
; Code
; ---------------------------------------------------------------------------

%include "debug.asm"
%include "post.asm"
%include "int10.asm"
%include "mode.asm"
%include "text.asm"
%include "gfx.asm"
%include "funcs.asm"
%include "vbe.asm"

; ---------------------------------------------------------------------------
; Data
; ---------------------------------------------------------------------------

%include "data.asm"

; A minimal option ROM for romlint tests, laid out like the VGPU98 BIOS
; (spec/rom-layout.md). Defines select one deliberate mistake:
;   BAD_SIGNATURE   first bytes are not 55h AAh
;   BAD_SIZE_BYTE   the size byte at offset 2 says 1 KB for a 512-byte image
;   BAD_IMAGE_LEN   the PCI data structure's image length says 1 KB
;   BAD_CLASS       the PCI data structure carries class 03h 80h 00h
;   BAD_CODE_TYPE   the PCI data structure says code type 1 (Open Firmware)
;   BAD_ENTRY       offset 3 holds NOPs instead of a jump
;   ENTRY_IN_PCIR   the entry jump lands inside the PCI data structure
;   ENTRY_IN_HEADER the entry jump lands inside the ROM header
;   BAD_VENDOR      PCI data structure carries the wrong vendor ID
;   UNALIGNED_PCIR  PCI data structure is not 4-byte aligned
;   BAD_INDICATOR   PCI data structure indicator has a reserved bit set
;   BAD_RESERVED    PCI data structure reserved word is not zero
;   HEADER_DIRTY    a reserved header byte is not zero
;   BAD_VGA_SIG     the "IBM VGA Compatible" signature is misspelt
;   WRONG_CHECKSUM  a complete 512-byte image whose bytes do not sum to zero
;   WITH_PNP        adds a valid $PnP header
;   BAD_PNP_SUM     adds a $PnP header with a wrong checksum

bits 16
org 0

%ifdef BAD_VENDOR
%define VENDOR 0x1234
%else
%define VENDOR 0x9898
%endif

%ifdef BAD_SIGNATURE
    db 0x55, 0x55
%else
    db 0x55, 0xaa
%endif
%ifdef BAD_SIZE_BYTE
    db 2
%else
    db 1
%endif
%ifdef BAD_ENTRY
    nop
    nop
    nop
%elifdef ENTRY_IN_PCIR
    jmp near pcir + 4
%elifdef ENTRY_IN_HEADER
    jmp near vga_signature
%else
    jmp near entry
%endif
%ifdef HEADER_DIRTY
    db 0x01
%endif
    times 0x18 - ($ - $$) db 0
    dw pcir
%ifdef WITH_PNP
    dw pnp
%elifdef BAD_PNP_SUM
    dw pnp
%else
    dw 0
%endif
    dw 0
vga_signature:
%ifdef BAD_VGA_SIG
    db 'IBM VGA Compatable'
%else
    db 'IBM VGA Compatible'
%endif

    align 4, db 0
%ifdef UNALIGNED_PCIR
    db 0
%endif
pcir:
    db 'PCIR'
    dw VENDOR, 0x2000, 0, 0x18
%ifdef BAD_CLASS
    db 0, 0x00, 0x80, 0x03
%else
    db 0, 0x00, 0x00, 0x03
%endif
%ifdef BAD_IMAGE_LEN
    dw 2, 0x0100
%else
    dw 1, 0x0100
%endif
%ifdef BAD_CODE_TYPE
    db 1
%else
    db 0
%endif
%ifdef BAD_INDICATOR
    db 0x81
%else
    db 0x80
%endif
%ifdef BAD_RESERVED
    dw 0x5aa5
%else
    dw 0
%endif

%ifdef WITH_PNP
%define PNP_SUM_FIX 0
%elifdef BAD_PNP_SUM
%define PNP_SUM_FIX 1
%endif
%ifdef PNP_SUM_FIX
    align 16, db 0
pnp:
    db '$PnP', 0x01, 0x02
    dw 0
    db 0
    db (0x100 - ((('$' + 'P' + 'n' + 'P' + 0x01 + 0x02 + 0x03 + 0x61 + ((vendor_str - $$) & 0xff) + ((vendor_str - $$) >> 8)) & 0xff)) + PNP_SUM_FIX) & 0xff
    dd 0
    dw vendor_str, 0
    db 0x03, 0x00, 0x00
    db 0x61
    dw 0, 0, 0, 0, 0
vendor_str:
    db 'Test', 0
%endif

entry:
    retf

%ifdef WRONG_CHECKSUM
    times 511 - ($ - $$) db 0
    db 0x12
%endif

; VGPU98 display driver for Windows 98 SE: a DIB engine minidriver.
;
; The DIB engine (DIBENG.DLL) draws everything into the linear framebuffer;
; this driver sets modes on the card's display engine, describes the
; framebuffer to the DIB engine, keeps the DAC in step with the palette at
; 8 bpp, and cooperates with the VDD when full-screen DOS sessions take the
; screen. It also answers DirectDraw (ddraw.asm) and stops the GPU around
; mode changes (gpu.asm). Interfaces: spec/win98-display-driver.md,
; spec/directx-hal.md. Linked by vglink with driver/vgpu98.lnk.
;
; Calling convention of the exports: Pascal (arguments pushed left to right,
; the callee pops them), far; SI, DI, BP and DS preserved.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

bits 16
cpu 386

%include "card.inc"
%include "winapi.inc"
%include "shared.inc"

segment _TEXT class=CODE use16 align=16
segment _DATA class=DATA use16 align=16

; ---------------------------------------------------------------------------
; Imports (ordinals in vgpu98.lnk)
; ---------------------------------------------------------------------------

extern GlobalSmartPageLock, AllocCStoDSAlias, FreeSelector, GetModuleHandle, GetProcAddress
extern CreateDIBPDevice, DIB_Enable, DIB_BeginAccess, DIB_EndAccess
extern DIB_SetPaletteExt, DIB_SetPaletteTranslateExt, DIB_CheckCursorExt

global DriverInit, Enable, Disable, ReEnable, SetPalette, CheckCursor
global ValidateMode, GetDriverResourceID, UserRepaintDisable

; Stack frame of an export after EXPORT_ENTER: arguments start at [bp + 6].
%macro EXPORT_ENTER 0
    push bp
    mov bp, sp
    push ds
    push esi
    push edi
    mov ax, _DATA
    mov ds, ax
%endmacro

%macro EXPORT_LEAVE 1                   ; argument bytes
    pop edi
    pop esi
    pop ds
    pop bp
    retf %1
%endmacro

; ---------------------------------------------------------------------------
; Data
; ---------------------------------------------------------------------------

segment _DATA

vdd_entry       dd 0                    ; VDD protected-mode API, 0 if there is none
vm_handle       dw 0                    ; the system VM
hw_present      db 0
lfb_base        dd 0                    ; physical address of the framebuffer (BAR1)
vram_size       dd 0
screen_sel      dw 0                    ; selector of the whole framebuffer

; The mode the configuration asks for (VDD_GET_DISPLAY_CONFIG).
cfg_width       dw 640
cfg_height      dw 480
cfg_bpp         dw 8
cfg_dpi         dw 96
cfg_refresh     dw 0                    ; requested rate: 0 default, FFFFh optimal
cfg_max_vert    dw 0                    ; the monitor's highest frame rate, 0 unknown

; The mode on the screen.
scr_width       dw 0
scr_height      dw 0
scr_bpp         dw 0
scr_pitch       dd 0
scr_refresh     dw 0
scr_vbe         dw 0                    ; VBE mode number with bits 14 and 15, for SCRATCH

pdevice         dd 0                    ; GDI's PDEVICE (a DIBENGINE)
cursor_shown    db 0                    ; SetCursor gave a shape (the OpenGL ICD's escape asks)
cursor_x        dw 0                    ; MoveCursor's last place: x, then y, written as one dword
cursor_y        dw 0

; The hardware cursor (cursor.asm)
CURSOR_SLOTS    equ 20000h              ; its two patterns in the GPU's reserve, past the ring's 128 KB
CURSOR_COPY_MAX equ 16 * 64 + 64 * 64 * 4 ; a shape's masks at their largest
cursor_card     db 0                    ; the card has one: PROFILE.CURSOR, its registers mapped
cursor_on_card  db 0                    ; the card's cursor is in use (the DIB engine's has no shape)
cursor_busy     db 0                    ; a shape on its way to the card: MoveCursor leaves the place to it
cursor_slot     db 0                    ; the pattern slot last given to the card
cursor_valid    db 0                    ; cursor_copy holds the last shape, one the card can show
cursor_noted    db 0                    ; the reasons in the log already (CN_*)
cursor_bytes    db 0                    ; cursor_write: the mode's bytes a pixel
cursor_rows     dw 0                    ; cursor_write: rows left
cursor_trails   dw 0                    ; pointer trails asked for (MOUSETRAILS), 0 for none
cursor_hot      dd 0                    ; the shape's hot spot: x, then y
cursor_moves    dd 0                    ; MoveCursor's count: SetCursor places the cursor again after one
cursor_copy     times CS_SIZE + CURSOR_COPY_MAX db 0 ; the last shape as Windows gave it

; The gamma table (gamma.asm)
GAMMA_SLOTS     equ 30000h              ; its two 1 KB tables in the GPU's reserve, past the cursor's patterns
gamma_card      db 0                    ; the card has one: PROFILE.GAMMA, its registers mapped
gamma_valid     db 0                    ; gamma_words holds a ramp a program set
gamma_slot      db 0                    ; the table slot last given to the card
gamma_noted     db 0                    ; the log's notes written already (GN_*)
gamma_sets      dd 0                    ; DDIGammaRamp's sets and gets, for the log
gamma_gets      dd 0
gamma_words     times 768 dw 0          ; the ramp as the program gave it: red, green, blue, 256 words each
color_table     dd 0                    ; 256 DIBColorEntry in the PDEVICE, at 8 bpp
dib_pdev_size   dw DIBENGINE_SIZE       ; the DIB engine's part of the PDEVICE
enabled         dw 0
palettized      dw 0
was_palettized  dw 0                    ; palettized before a ReEnable
reenabling      db 0
hooked          db 0
no_repaint      db 0
paint_pending   db 0
repaint_func    dd 0                    ; USER's RepaintScreen

begin_access    dw begin_access_hook, seg begin_access_hook
end_access      dw DIB_EndAccess, seg DIB_EndAccess

user_module     db "USER", 0
dispinfo        times DISPLAYINFO_SIZE db 0

; The GPU and DirectDraw
regs_phys       dd 0                    ; physical address of BAR0's registers
regs_sel        dw 0                    ; selector of the registers, 0 if not mapped
shared_linear   dd 0                    ; linear address of the shared block
set_info        dd 0                    ; DirectDraw's SetInfo (16:16), 0 without a driver object

; The block shared with the DirectX DLL (driver32/shared.h): in this fixed,
; locked segment, so its linear address never changes.
align 16
shared          times SB_SIZE db 0

; ---------------------------------------------------------------------------
; Code
; ---------------------------------------------------------------------------

segment _TEXT

; The previous INT 2Fh handler, in the code segment so the hook can chain to
; it without touching any register. Written through a data alias.
old_int2f       dd 0

%include "drvmodes.inc"

; The 20 static colours of the 8 bpp palette (DIBColorEntry: blue, green,
; red, flags): entries 0-9 and 246-255.
static_colors_low:
    db 0x00, 0x00, 0x00, 0
    db 0x00, 0x00, 0x80, 0
    db 0x00, 0x80, 0x00, 0
    db 0x00, 0x80, 0x80, 0
    db 0x80, 0x00, 0x00, 0
    db 0x80, 0x00, 0x80, 0
    db 0x80, 0x80, 0x00, 0
    db 0xc0, 0xc0, 0xc0, MAPTOWHITE
    db 0xc0, 0xdc, 0xc0, MAPTOWHITE | NONSTATIC
    db 0xf0, 0xca, 0xa6, MAPTOWHITE | NONSTATIC
static_colors_high:
    db 0xf0, 0xfb, 0xff, MAPTOWHITE | NONSTATIC
    db 0xa4, 0xa0, 0xa0, MAPTOWHITE | NONSTATIC
    db 0x80, 0x80, 0x80, MAPTOWHITE
    db 0x00, 0x00, 0xff, 0
    db 0x00, 0xff, 0x00, MAPTOWHITE
    db 0x00, 0xff, 0xff, MAPTOWHITE
    db 0xff, 0x00, 0x00, 0
    db 0xff, 0x00, 0xff, 0
    db 0xff, 0xff, 0x00, MAPTOWHITE
    db 0xff, 0xff, 0xff, MAPTOWHITE

%include "drvdebug.asm"
%include "hw.asm"
%include "gpu.asm"
%include "ddraw.asm"
%include "dibthunk.asm"
%include "cursor.asm"
%include "gamma.asm"

; ---------------------------------------------------------------------------
; DriverInit: the library entry point. CX = heap size, DI = instance,
; DS = data segment, ES:SI = command line. Returns AX = 1, or 0 to refuse to
; load (no VGPU98 card).
; ---------------------------------------------------------------------------

DriverInit:
    push bp
    mov bp, sp
    push ds
    push esi
    push edi
    mov ax, _DATA
    mov ds, ax

    ; Keep the code and data in memory: the VDD calls back into the driver
    ; and the cursor is checked at interrupt time.
    push cs
    call far GlobalSmartPageLock
    push ds
    call far GlobalSmartPageLock

    mov ax, INT2F_GET_ENTRY
    mov bx, VDD_DEVICE_ID
    xor di, di
    mov es, di
    int 2fh
    mov [vdd_entry], di
    mov [vdd_entry + 2], es
    mov ax, INT2F_GET_VM
    int 2fh
    mov [vm_handle], bx

    call hw_detect
    jc .absent
    call shared_init
    call map_registers
    call cursor_init
    call gamma_init
    call build_ddmodes
    call read_config
    DEBUG_TEXT DRV_TAG, "loaded, "
    mov eax, [vram_size]
    shr eax, 20
    call debug_dec
    DEBUG_TEXT " MB", 10
    mov ax, 1
    jmp .done
.absent:
    DEBUG_TEXT DRV_TAG, "no VGPU98 display engine", 10
    xor ax, ax
.done:
    pop edi
    pop esi
    pop ds
    pop bp
    retf

; Fills in the shared block's fixed part and finds its linear address, for
; the DirectX DLL (DDGET32BITDRIVERNAME passes it). Preserves all registers.
shared_init:
    pushad
    mov dword [shared + SB_magic], SB_MAGIC
    mov dword [shared + SB_version], SB_VERSION
    mov dword [shared + SB_size], SB_SIZE
    mov eax, [vram_size]
    mov [shared + SB_vram_size], eax
    mov bx, ds
    mov ax, 0006h                       ; DPMI: base of the data segment in CX:DX
    int 31h
    jc .done
    mov [shared_linear], dx
    mov [shared_linear + 2], cx
    add dword [shared_linear], shared
.done:
    popad
    ret

; ---------------------------------------------------------------------------
; Enable(lpDevice, style, lpDeviceType, lpOutputFile, lpStuff) -> AX
; Style bit 0 set: fill the GDIINFO at lpDevice, return its size.
; Style bit 0 clear: set the mode and build the PDEVICE at lpDevice, return 1
; (0 on failure).
; ---------------------------------------------------------------------------

%define en_lpDevice     bp + 20
%define en_style        bp + 18
%define en_lpDeviceType bp + 14
%define en_lpOutputFile bp + 10
%define en_lpStuff      bp + 6

%macro PUSH_ENABLE_ARGS 0
    push dword [en_lpDevice]
    push word [en_style]
    push dword [en_lpDeviceType]
    push dword [en_lpOutputFile]
    push dword [en_lpStuff]
%endmacro

Enable:
    EXPORT_ENTER
    test byte [en_style], 1
    jnz .gdiinfo

    ; --- PDEVICE ---
    mov eax, [en_lpDevice]
    mov [pdevice], eax
    cmp byte [reenabling], 0
    jne .trapping_off
    ; Take the hardware from the VDD before touching it: the display engine's
    ; registers are sequencer registers, which the VDD traps until then.
    mov ax, INT2F_STOP_IO_TRAP
    int 2fh
.trapping_off:
    call physical_enable
    jc .fail

    PUSH_ENABLE_ARGS
    call far DIB_Enable

    ; The bitmap header after the DIB engine's part of the PDEVICE.
    les di, [en_lpDevice]
    add di, [dib_pdev_size]
    push di
    xor eax, eax
    mov cx, BIH_SIZE / 4
    cld
    rep stosd
    pop di
    mov dword [es:di + BIH_biSize], BIH_SIZE
    movzx eax, word [scr_width]
    mov [es:di + BIH_biWidth], eax
    movzx eax, word [scr_height]
    mov [es:di + BIH_biHeight], eax
    mov word [es:di + BIH_biPlanes], 1
    mov ax, [scr_bpp]
    mov [es:di + BIH_biBitCount], ax

    mov bx, MINIDRIVER | VRAM
    cmp word [scr_bpp], 16
    jne .not_565
    or bx, FIVE6FIVE
.not_565:
    cmp word [palettized], 0
    je .flags_done
    or bx, PALETTIZED
    ; The colour table follows the header. A resolution change at 8 bpp keeps
    ; the palette Windows has built; a first enable, or a change from another
    ; depth, starts from the static colours.
    lea ax, [di + BIH_SIZE]
    mov [color_table], ax
    mov [color_table + 2], es
    cmp byte [reenabling], 0
    je .default_colors
    cmp word [was_palettized], 0
    jne .flags_done
.default_colors:
    call default_color_table
.flags_done:
    ; CreateDIBPDevice(lpInfo, lpDevice, lpBits, wFlags) returns in EAX.
    push es
    push di
    push dword [en_lpDevice]
    push word [screen_sel]
    push word 0
    push bx
    call far CreateDIBPDevice
    test eax, eax
    jz .fail

    ; The DIB engine's own cursor exclusion, as its access callbacks.
    les di, [en_lpDevice]
    mov eax, [begin_access]
    mov [es:di + DE_deBeginAccess], eax
    mov eax, [end_access]
    mov [es:di + DE_deEndAccess], eax

    cmp word [palettized], 0
    je .no_palette
    push dword 0                        ; translate table: identity
    push dword [pdevice]
    call far DIB_SetPaletteTranslateExt
    xor ax, ax
    mov cx, 256
    call load_dac
.no_palette:
    cmp byte [reenabling], 0
    jne .hooked
    call hook_int2f
.hooked:
    mov word [enabled], 1
    call ddraw_mode_changed             ; DirectDraw learns the new mode
    mov ax, 1
    jmp .done
.fail:
    DEBUG_TEXT DRV_TAG, "enable failed", 10
    xor ax, ax
.done:
    EXPORT_LEAVE 18

    ; --- GDIINFO ---
.gdiinfo:
    PUSH_ENABLE_ARGS
    call far DIB_Enable
    les di, [en_lpDevice]
    call fill_gdiinfo
    mov ax, GDIINFO_SIZE
    jmp .done

; Fills the GDIINFO at ES:DI for the configured mode; the DIB engine has set
; the drawing capabilities. Clobbers EAX, EBX, ECX, EDX.
fill_gdiinfo:
    mov word [es:di + GI_dpVersion], DRV_VERSION
    mov word [es:di + GI_dpTechnology], DT_RASDISPLAY
    mov ax, [cfg_width]
    mov [es:di + GI_dpHorzRes], ax
    call size_mm
    mov [es:di + GI_dpHorzSize], ax
    mov ax, [cfg_height]
    mov [es:di + GI_dpVertRes], ax
    call size_mm
    mov [es:di + GI_dpVertSize], ax
    mov ax, [cfg_bpp]
    mov [es:di + GI_dpBitsPixel], ax
    mov word [es:di + GI_dpPlanes], 1
    mov word [es:di + GI_dpNumBrushes], -1
    mov word [es:di + GI_dpCapsFE], 0
    mov word [es:di + GI_dpNumFonts], 0

    ; Mapping modes from the logical inch (dpi pixels): window extents in the
    ; mode's units per inch, viewport extents in pixels (y grows down).
    mov ax, [cfg_dpi]
    mov bx, ax
    neg bx
    mov word [es:di + GI_dpMLoWin], 254         ; 0.1 mm
    mov word [es:di + GI_dpMLoWin + 2], 254
    mov word [es:di + GI_dpMHiWin], 2540        ; 0.01 mm
    mov word [es:di + GI_dpMHiWin + 2], 2540
    mov word [es:di + GI_dpELoWin], 100         ; 0.01 inch
    mov word [es:di + GI_dpELoWin + 2], 100
    mov word [es:di + GI_dpEHiWin], 1000        ; 0.001 inch
    mov word [es:di + GI_dpEHiWin + 2], 1000
    mov word [es:di + GI_dpTwpWin], 1440        ; twips
    mov word [es:di + GI_dpTwpWin + 2], 1440
%assign vpt GI_dpMLoVpt
%rep 5
    mov [es:di + vpt], ax
    mov [es:di + vpt + 2], bx
%assign vpt vpt + 8
%endrep
    mov [es:di + GI_dpLogPixelsX], ax
    mov [es:di + GI_dpLogPixelsY], ax
    mov word [es:di + GI_dpDCManage], DC_IgnoreDFNP
    or word [es:di + GI_dpCaps1], C1_BYTE_PACKED | C1_COLORCURSOR | C1_REINIT_ABLE
    cmp byte [gamma_card], 0            ; DDIGammaRamp (gamma.asm)
    je .caps_done
    or word [es:di + GI_dpCaps1], C1_GAMMA_RAMP
.caps_done:

    ; The PDEVICE: the DIB engine's part, then a bitmap header and room for a
    ; 256-entry colour table, whatever the depth, so a later ReEnable can
    ; change the depth within the same PDEVICE.
    mov ax, [es:di + GI_dpDEVICEsize]
    mov [dib_pdev_size], ax
    add word [es:di + GI_dpDEVICEsize], BIH_SIZE + 256 * 4

    cmp word [cfg_bpp], 8
    jne .direct
    mov word [es:di + GI_dpNumPens], 16
    mov word [es:di + GI_dpNumColors], 20
    mov word [es:di + GI_dpNumPalReg], 256
    mov word [es:di + GI_dpPalReserved], 20
    mov word [es:di + GI_dpColorRes], 18        ; 6 bits per primary
    or word [es:di + GI_dpRaster], RC_DIBTODEV | RC_PALETTE | RC_SAVEBITMAP
    ret
.direct:
    mov word [es:di + GI_dpNumPens], -1
    mov word [es:di + GI_dpNumColors], -1
    mov word [es:di + GI_dpNumPalReg], 0
    mov word [es:di + GI_dpPalReserved], 0
    mov word [es:di + GI_dpColorRes], 0
    or word [es:di + GI_dpRaster], RC_DIBTODEV
    ret

; In: AX = pixels. Out: AX = millimetres at the configured DPI, rounded.
; Clobbers EBX, EDX.
size_mm:
    movzx eax, ax
    imul eax, 254
    movzx ebx, word [cfg_dpi]
    imul ebx, 10
    xor edx, edx
    push ebx
    shr ebx, 1
    add eax, ebx
    pop ebx
    div ebx
    ret

; Fills the colour table at [color_table] with the static colours and the
; free entries in between (black, not static).
default_color_table:
    push ds
    push es
    push si
    push di
    push cx
    les di, [color_table]
    push cs
    pop ds
    mov si, static_colors_low
    mov cx, 10
    cld
    rep movsd
    mov eax, NONSTATIC << 24
    mov cx, 236
    rep stosd
    mov si, static_colors_high
    mov cx, 10
    rep movsd
    pop cx
    pop di
    pop si
    pop es
    pop ds
    ret

; ---------------------------------------------------------------------------
; ReEnable(lpDevice, lpInfo) -> AX: changes the mode to the one the
; configuration now names, keeping GDI's PDEVICE; fills the GDIINFO at lpInfo.
; On failure the previous mode comes back and AX = 0.
; ---------------------------------------------------------------------------

%define re_lpDevice     bp + 10
%define re_lpInfo       bp + 6

ReEnable:
    EXPORT_ENTER
    ; Keep the current configuration to return to.
    push word [cfg_width]
    push word [cfg_height]
    push word [cfg_bpp]
    push word [cfg_dpi]
    push word [cfg_refresh]
    call read_config
    mov byte [reenabling], 1
    mov ax, [palettized]                ; the mode before, also for a failed change
    mov [was_palettized], ax

    ; No cursor drawing while the mode changes.
    push dword [re_lpDevice]
    push word 0
    push word 0
    mov ax, [scr_width]
    dec ax
    push ax
    mov ax, [scr_height]
    dec ax
    push ax
    push word CURSOREXCLUDE
    call far DIB_BeginAccess

    push dword [re_lpDevice]
    push word 0                         ; style: PDEVICE
    push dword 0
    push dword 0
    push dword 0
    push cs
    call Enable
    mov si, ax

    push dword [re_lpDevice]
    push word CURSOREXCLUDE
    call far DIB_EndAccess

    test si, si
    jz .failed
    add sp, 10                          ; the saved configuration
    push dword [re_lpInfo]
    push word 1                         ; style: GDIINFO
    push dword 0
    push dword 0
    push dword 0
    push cs
    call Enable
    mov ax, 1
    jmp .done
.failed:
    pop word [cfg_refresh]
    pop word [cfg_dpi]
    pop word [cfg_bpp]
    pop word [cfg_height]
    pop word [cfg_width]
    push dword [re_lpDevice]
    push word 0
    push dword 0
    push dword 0
    push dword 0
    push cs
    call Enable
    call repaint
    xor ax, ax
.done:
    mov byte [reenabling], 0
    EXPORT_LEAVE 8

; ---------------------------------------------------------------------------
; Disable(lpDevice) -> AX = 1: back to VGA text mode for the VDD.
; ---------------------------------------------------------------------------

Disable:
    EXPORT_ENTER
    DEBUG_TEXT DRV_TAG, "disable", 10
    mov word [enabled], 0
    les di, [bp + 6]
    lock or word [es:di + DE_deFlags], BUSY
    call gpu_stop                       ; the GPU stays held until the next Enable
    call cursor_disable
    call gamma_disable
    call gamma_totals
    call engine_off                     ; while the hardware is still ours
    mov ax, INT2F_START_IO_TRAP
    int 2fh
    mov ax, VDD_DRIVER_UNREGISTER
    call call_vdd
    mov ax, 0003h                       ; 80x25 text through the BIOS
    int 10h
    call unhook_int2f
    mov ax, 1
    EXPORT_LEAVE 4

; ---------------------------------------------------------------------------
; SetPalette(wStartIndex, wNumEntries, lpPalette) -> AX = 0
; ---------------------------------------------------------------------------

SetPalette:
    EXPORT_ENTER
    push word [bp + 12]
    push word [bp + 10]
    push dword [bp + 6]
    push dword [pdevice]
    call far DIB_SetPaletteExt          ; updates the colour table
    cmp word [palettized], 0
    je .done
    les di, [pdevice]
    test word [es:di + DE_deFlags], BUSY
    jnz .done                           ; a DOS session owns the DAC
    mov ax, [bp + 12]
    mov cx, [bp + 10]
    call load_dac
.done:
    xor ax, ax
    EXPORT_LEAVE 8

; ---------------------------------------------------------------------------
; CheckCursor(): called at timer time to let the software cursor catch up.
; ---------------------------------------------------------------------------

CheckCursor:
    push ds
    mov ax, _DATA
    mov ds, ax
    cmp word [enabled], 0
    je .done
    push dword [pdevice]
    call far DIB_CheckCursorExt
.done:
    pop ds
    retf

; ---------------------------------------------------------------------------
; ValidateMode(lpDispValMode) -> AX = VALMODE_*: can this mode run? Linked by
; name; may be called while another driver is active.
; ---------------------------------------------------------------------------

ValidateMode:
    EXPORT_ENTER
    cmp byte [hw_present], 0
    jne .present
    call hw_detect
    mov ax, VALMODE_NO_WRONGDRV
    jc .done
.present:
    les di, [bp + 6]
    mov bx, [es:di + DVM_dvmBpp]
    mov cx, [es:di + DVM_dvmXRes]
    mov dx, [es:di + DVM_dvmYRes]
    cmp bx, 4                           ; 16 colours: Windows' VGA driver (the INF)
    jne .ours
    mov ax, VALMODE_YES
    cmp cx, 640
    jne .no_mode
    cmp dx, 480
    je .done
.no_mode:
    mov ax, VALMODE_NO_UNKNOWN
    jmp .done
.ours:
    call check_mode
.done:
    EXPORT_LEAVE 4

; ---------------------------------------------------------------------------
; GetDriverResourceID(wResID, lpResType) -> DX:AX: the resource USER should
; load: the large-font OEM fonts (2003) above 96 DPI.
; ---------------------------------------------------------------------------

GetDriverResourceID:
    EXPORT_ENTER
    mov ax, [bp + 10]
    cmp ax, OBJ_FONT
    jne .done
    cmp word [cfg_dpi], 96
    je .done
    mov ax, 2003
.done:
    xor dx, dx
    EXPORT_LEAVE 6

; ---------------------------------------------------------------------------
; UserRepaintDisable(bDisable): USER holds repaints off while it switches.
; ---------------------------------------------------------------------------

UserRepaintDisable:
    EXPORT_ENTER
    mov ax, [bp + 6]
    test ax, ax
    setnz byte [no_repaint]
    jnz .done
    cmp byte [paint_pending], 0
    je .done
    mov byte [paint_pending], 0
    call repaint
.done:
    EXPORT_LEAVE 2

%include "switch.asm"

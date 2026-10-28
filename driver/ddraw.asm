; VGPU98 display driver: DirectDraw (spec/directx-hal.md).
;
; DirectDraw finds the driver through GDI's Control escape: QUERYESCSUPPORT
; for DCICOMMAND, then the DCICOMMAND escapes. The driver names its DirectX
; DLL (vgpu98dx.dll) with the shared block's address; the DLL fills in its
; part of the driver description there (caps, callbacks, Direct3D), and the
; driver completes it (the mode, video memory, the mode list) and hands it
; to DirectDraw's SetInfo, again on every mode change. The Glide DLLs find
; the shared block through an escape of the driver's own (VGPU98_ESC_INFO).
; Windows' opengl32.dll asks for the OpenGL ICD's name (OPENGL_GETINFO).
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

QUERYESCSUPPORT         equ 8
DCICOMMAND              equ 3075
OPENGL_GETINFO          equ 0x1101
DD_VERSION              equ 0x200
DD_HAL_VERSION          equ 0x00ff
DD_RUNTIME_VERSION      equ 0x50a
DDCREATEDRIVEROBJECT    equ 10
DDGET32BITDRIVERNAME    equ 11
DDNEWCALLBACKFNS        equ 12
DDVERSIONINFO           equ 13

DCI_dwCommand           equ 0       ; DCICMD
DCI_dwParam1            equ 4
DCI_dwVersion           equ 12
FNS_lpSetInfo           equ 4       ; DDHALDDRAWFNS
D32_szEntryPoint        equ 260     ; DD32BITDRIVERDATA
D32_dwContext           equ 324
D32_SIZE                equ 328
DDD_ddRVal              equ 4       ; DDHAL_DESTROYDRIVERDATA

; ---------------------------------------------------------------------------
; Control(lpDevice, wFunction, lpInData, lpOutData) -> DX:AX. The DirectDraw
; escapes, the Glide DLLs' (VGPU98_ESC_INFO) and OpenGL's (OPENGL_GETINFO);
; everything else goes to the DIB engine untouched, MOUSETRAILS after the
; cursor has noted it. A null pointer (selector 0) is never followed.
; ---------------------------------------------------------------------------

%define ct_lpOut    bp + 6
%define ct_lpIn     bp + 10
%define ct_func     bp + 14
%define ct_lpDevice bp + 16

global Control
extern DIB_Control, DIB_BeginAccess, DIB_EndAccess

Control:
    push bp
    mov bp, sp
    cmp word [ct_func], QUERYESCSUPPORT
    je .query
    cmp word [ct_func], DCICOMMAND
    je .dci
    cmp word [ct_func], VGPU98_ESC_INFO
    je .info
    cmp word [ct_func], OPENGL_GETINFO
    je .ogl
    cmp word [ct_func], VGPU98_ESC_ACCESS
    je .access
    cmp word [ct_func], MOUSETRAILS
    je .trails
.forward:
    pop bp
    jmp far DIB_Control

; Pointer trails: the cursor goes to the DIB
; engine while they are on, and back to the card after; then the DIB engine
; takes the escape as always, for its trails.
.trails:
    cmp word [ct_lpIn + 2], 0
    je .forward
    push ds
    push es
    pushad
    les bx, [ct_lpIn]
    mov ax, [es:bx]
    mov bx, _DATA
    mov ds, bx
    call cursor_set_trails
    popad
    pop es
    pop ds
    jmp .forward
.query:
    cmp word [ct_lpIn + 2], 0
    je .forward
    push es
    push bx
    les bx, [ct_lpIn]
    mov ax, [es:bx]
    pop bx
    pop es
    cmp ax, VGPU98_ESC_INFO
    je .query_info
    cmp ax, OPENGL_GETINFO
    je .query_ogl
    cmp ax, DCICOMMAND
    jne .forward
    mov ax, DD_HAL_VERSION              ; a DirectDraw HAL
    xor dx, dx
    pop bp
    retf 14
.query_info:
    mov ax, 1
    xor dx, dx
    pop bp
    retf 14
.query_ogl:
    call ogl_offered
    jc .forward                         ; no OpenGL on this card: the DIB engine's answer (none)
    mov ax, 1
    xor dx, dx
    pop bp
    retf 14

; OpenGL's escape: the ICD's name, which
; opengl32.dll looks up under HKLM\Software\Microsoft\Windows\CurrentVersion\
; OpenGLdrivers ("VGPU98" = vgpu98gl.dll, the INF's). The answer as Windows 98's
; opengl32.dll reads it: a version (2), a driver version (1), then the name in
; ANSI characters. Only while the GPU draws OpenGL (PROFILE.OGL with 3D);
; otherwise the DIB engine answers, and Windows draws OpenGL in software.
.ogl:
    call ogl_offered
    jc .forward
    cmp word [ct_lpOut + 2], 0
    je .forward
    push es
    push di
    les di, [ct_lpOut]
    mov dword [es:di], 2                ; version
    mov dword [es:di + 4], 1            ; driver version
    mov dword [es:di + 8], 'VGPU'       ; "VGPU98", NUL-terminated
    mov dword [es:di + 12], '98'
    pop di
    pop es
    mov ax, 1
    xor dx, dx
    pop bp
    retf 14

; The OpenGL ICD's escape: the DIB engine's
; software cursor kept out of a rectangle of the screen while the GPU writes
; a window's pixels there (vgpu98_escaccess_t: begin 1, DIB_BeginAccess with
; CURSOREXCLUDE), and let back (begin 0, DIB_EndAccess), which draws it
; again over the new pixels. A begin answers 1 when it kept the cursor out,
; 0 when the cursor is hidden or not within 32 pixels of the rectangle (its
; shape is 32 by 32 at most, around its hot spot): then there is nothing to
; let back, and the ICD does not wait for the GPU. Only while the GPU draws
; OpenGL. While the card shows the cursor (cursor.asm) it is never in the
; window's pixels: a begin answers 0.
.access:
    call ogl_offered
    jc .forward
    cmp word [ct_lpIn + 2], 0
    je .forward
    push es
    push bx
    les bx, [ct_lpIn]
    cmp word [es:bx + EA_begin], 0
    je .access_end
    push ds
    mov ax, _DATA
    mov ds, ax
    cmp byte [cursor_on_card], 0
    jne .access_none_ds
    cmp byte [cursor_shown], 0
    je .access_none_ds
    mov ax, [cursor_x]
    add ax, 32
    cmp ax, [es:bx + EA_left]
    jl .access_none_ds
    sub ax, 64
    cmp ax, [es:bx + EA_right]
    jg .access_none_ds
    mov ax, [cursor_y]
    add ax, 32
    cmp ax, [es:bx + EA_top]
    jl .access_none_ds
    sub ax, 64
    cmp ax, [es:bx + EA_bottom]
    jg .access_none_ds
    pop ds
    push dword [ct_lpDevice]
    push word [es:bx + EA_left]
    push word [es:bx + EA_top]
    push word [es:bx + EA_right]
    push word [es:bx + EA_bottom]
    push word CURSOREXCLUDE
    call far DIB_BeginAccess
    jmp .access_done
.access_end:
    push dword [ct_lpDevice]
    push word CURSOREXCLUDE
    call far DIB_EndAccess
.access_done:
    pop bx
    pop es
    mov ax, 1
    xor dx, dx
    pop bp
    retf 14
.access_none_ds:
    pop ds
    pop bx
    pop es
    xor ax, ax
    xor dx, dx
    pop bp
    retf 14

; The Glide DLLs' escape: the shared block's
; address, with a magic number, the answer's size and the block's version
; (vgpu98_escinfo_t, driver32/shared.h), then the driver's build stamp
; when there is room for it. Only for the
; question asked (vgpu98_escquery_t), with room for the answer's head:
; Control is never told the output buffer's size.
%if %strlen(BUILD_STAMP) > 15
%error "the build stamp does not fit the escape's answer (15 characters at most)"
%endif
.info:
    push ds
    push esi
    push edi
    cmp word [ct_lpIn + 2], 0
    je .info_refused
    cmp word [ct_lpOut + 2], 0
    je .info_refused
    les di, [ct_lpIn]
    cmp dword [es:di + EQ_magic], ESC_INFO_MAGIC
    jne .info_refused
    mov ecx, [es:di + EQ_out_size]
    cmp ecx, EI_stamp                   ; the head: everything before the stamp
    jb .info_refused
    mov ax, _DATA
    mov ds, ax
    les di, [ct_lpOut]
    mov dword [es:di + EI_magic], ESC_INFO_MAGIC
    mov dword [es:di + EI_size], EI_stamp
    mov eax, [shared_linear]
    mov [es:di + EI_shared_linear], eax
    mov dword [es:di + EI_sb_version], SB_VERSION
    cmp ecx, EI_SIZE
    jb .info_done
    mov dword [es:di + EI_size], EI_SIZE
    push di                             ; the stamp, padded with NULs
    add di, EI_stamp
    xor al, al
    mov cx, EI_SIZE - EI_stamp
    cld
    rep stosb
    pop di
    push di
    add di, EI_stamp
    mov si, build_stamp
    call copy_cs_string
    pop di
.info_done:
    pop edi
    pop esi
    pop ds
    mov ax, 1
    xor dx, dx
    pop bp
    retf 14
.info_refused:
    pop edi
    pop esi
    pop ds
    mov ax, -1
    mov dx, -1
    pop bp
    retf 14

.dci:
    push ds
    push esi
    push edi
    mov ax, _DATA
    mov ds, ax
    cmp word [ct_lpIn + 2], 0
    je .dci_forward
    les di, [ct_lpIn]
    cmp dword [es:di + DCI_dwVersion], DD_VERSION
    jne .dci_forward
    mov eax, [es:di + DCI_dwCommand]
    cmp eax, DDNEWCALLBACKFNS
    je .newfns
    cmp eax, DDVERSIONINFO
    je .version
    cmp eax, DDGET32BITDRIVERNAME
    je .name
    cmp eax, DDCREATEDRIVEROBJECT
    je .create
.dci_forward:
    pop edi
    pop esi
    pop ds
    jmp .forward

.newfns:                                ; dwParam1: 16:16 pointer to DDHALDDRAWFNS
    cmp word [es:di + DCI_dwParam1 + 2], 0
    je .bad
    les di, [es:di + DCI_dwParam1]
    mov eax, [es:di + FNS_lpSetInfo]
    mov [set_info], eax
    jmp .ok

.version:                               ; DDVERSIONDATA
    cmp word [ct_lpOut + 2], 0
    je .bad
    les di, [ct_lpOut]
    mov dword [es:di], DD_RUNTIME_VERSION
    mov dword [es:di + 4], 0
    mov dword [es:di + 8], 0
    jmp .ok

.name:                                  ; DD32BITDRIVERDATA: the DLL, its entry, the context
    cmp word [ct_lpOut + 2], 0
    je .bad
    les di, [ct_lpOut]
    push di
    xor al, al
    mov cx, D32_SIZE
    cld
    rep stosb
    pop di
    push di
    mov si, dll_name
    call copy_cs_string
    pop di
    push di
    add di, D32_szEntryPoint
    mov si, dll_entry
    call copy_cs_string
    pop di
    mov eax, [shared_linear]
    mov [es:di + D32_dwContext], eax
    jmp .ok

.create:
    cmp word [ct_lpOut + 2], 0
    je .no_output
    cmp dword [set_info], 0
    je .no_callbacks
    call build_halinfo
    push ds
    push word shared + SB_halinfo
    push word 0                         ; reset: FALSE, a new driver object
    call far [set_info]
    test ax, ax
    jz .refused
    les di, [ct_lpOut]
    mov eax, [shared + SB_dll_hinstance]
    mov [es:di], eax
    DEBUG_TEXT DRV_TAG, "DirectDraw driver object created", 10
.ok:
    mov ax, 1
    xor dx, dx
    jmp .done
; Why the driver object was not created, for the log.
.no_output:
    DEBUG_TEXT DRV_TAG, "DDCREATEDRIVEROBJECT without an output buffer", 10
    jmp .bad
.no_callbacks:
    DEBUG_TEXT DRV_TAG, "DirectDraw never registered its callbacks (DDNEWCALLBACKFNS)", 10
    jmp .bad
.refused:
    cmp dword [shared + SB_dll_ready], 0
    jne .rejected
    ; The DLL never ran DriverInit: a damaged DLL cannot say so itself.
    DEBUG_TEXT DRV_TAG, "vgpu98dx.dll did not start (missing, damaged or not loaded); DirectDraw and Direct3D unavailable", 10
    jmp .bad
.rejected:
    DEBUG_TEXT DRV_TAG, "DirectDraw refused the driver description (SetInfo)", 10
.bad:
    mov ax, -1
    mov dx, -1
.done:
    pop edi
    pop esi
    pop ds
    pop bp
    retf 14

dll_name    db "vgpu98dx.dll", 0
dll_entry   db "DriverInit", 0
build_stamp db BUILD_STAMP, 0           ; the Glide DLLs' escape's answer

; Copies the zero-terminated string at CS:SI to ES:DI.
copy_cs_string:
    push ax
.next:
    mov al, [cs:si]
    mov [es:di], al
    inc si
    inc di
    test al, al
    jnz .next
    pop ax
    ret

; ---------------------------------------------------------------------------
; DestroyDriver(lpDestroyDriverData) -> DX:AX: DirectDraw's driver object is
; gone; SetInfo must not be called until DirectDraw makes a new one. A 16-bit
; callback (no CB32 flag), far Pascal.
; ---------------------------------------------------------------------------

DestroyDriver16:
    push bp
    mov bp, sp
    push ds
    push es
    push bx
    mov ax, _DATA
    mov ds, ax
    mov dword [set_info], 0
    les bx, [bp + 6]
    mov dword [es:bx + DDD_ddRVal], 0   ; DD_OK
    pop bx
    pop es
    pop ds
    pop bp
    mov ax, 1                           ; DDHAL_DRIVER_HANDLED
    xor dx, dx
    retf 4

; ---------------------------------------------------------------------------
; The driver description
; ---------------------------------------------------------------------------

; Appends a mode to the DirectDraw mode list. In: CX = width, DX = height,
; BX = bits per pixel. Out: EAX = its index, or -1 if the list is full.
; Preserves the rest.
add_ddmode:
    push edi
    push ecx
    push edx
    mov eax, [shared + SB_num_modes]
    cmp eax, SB_MAX_MODES
    jae .full
    imul di, ax, MI_SIZE
    add di, shared + SB_modes
    movzx ecx, cx
    movzx edx, dx
    mov [di + MI_dwWidth], ecx
    mov [di + MI_dwHeight], edx
    push eax
    movzx eax, bx
    mov [di + MI_dwBPP], eax
    shr eax, 3
    imul eax, ecx
    mov [di + MI_lPitch], eax
    pop eax
    mov word [di + MI_wFlags], 0
    mov word [di + MI_wRefreshRate], 0
    mov dword [di + MI_dwRBitMask], 0
    mov dword [di + MI_dwGBitMask], 0
    mov dword [di + MI_dwBBitMask], 0
    mov dword [di + MI_dwAlphaBitMask], 0
    cmp bx, 8
    jne .not8
    mov word [di + MI_wFlags], DDMODEINFO_PALETTIZED
    jmp .masks_done
.not8:
    cmp bx, 16
    jne .is32
    mov dword [di + MI_dwRBitMask], 0f800h
    mov dword [di + MI_dwGBitMask], 07e0h
    mov dword [di + MI_dwBBitMask], 001fh
    jmp .masks_done
.is32:
    mov dword [di + MI_dwRBitMask], 0ff0000h
    mov dword [di + MI_dwGBitMask], 0ff00h
    mov dword [di + MI_dwBBitMask], 0ffh
.masks_done:
    inc dword [shared + SB_num_modes]
    jmp .done
.full:
    mov eax, -1
.done:
    pop edx
    pop ecx
    pop edi
    ret

; Builds DirectDraw's mode list: every enabled resolution of the mode table,
; the low-resolution game modes included, at 8, 16 and 32 bpp, where it
; fits below the GPU's reserve. Preserves all registers.
build_ddmodes:
    pushad
    mov dword [shared + SB_num_modes], 0
    mov si, drv_ddmode_table
    mov bp, DRV_DDMODE_COUNT
.mode:
    mov cx, [cs:si]
    mov dx, [cs:si + 2]
    mov bx, 8
.depth:
    push bx
    push cx
    push dx
    call check_mode
    pop dx
    pop cx
    pop bx
    cmp ax, VALMODE_YES
    jne .next_depth
    call add_ddmode
.next_depth:
    shl bx, 1                           ; 8, 16, 32
    cmp bx, 32
    jbe .depth
    add si, 4
    dec bp
    jnz .mode
    popad
    ret

; Out: EAX = the index of the mode on the screen in the mode list, added if
; it is not there (a mode from the registry the table does not have).
; Preserves the rest.
find_ddmode:
    push ebx
    push ecx
    push edx
    push si
    movzx ecx, word [scr_width]
    movzx edx, word [scr_height]
    movzx ebx, word [scr_bpp]
    xor eax, eax
    mov si, shared + SB_modes
.next:
    cmp eax, [shared + SB_num_modes]
    jae .add
    cmp [si + MI_dwWidth], ecx
    jne .skip
    cmp [si + MI_dwHeight], edx
    jne .skip
    cmp [si + MI_dwBPP], ebx
    je .found
.skip:
    add si, MI_SIZE
    inc eax
    jmp .next
.add:
    call add_ddmode
    cmp eax, -1
    jne .found
    xor eax, eax
.found:
    pop si
    pop edx
    pop ecx
    pop ebx
    ret

; Completes the driver description in the shared block for the mode on the
; screen. The DLL has filled in the caps, its callbacks, GetDriverInfo and
; the Direct3D pointers. Preserves all registers.
build_halinfo:
    push es
    pushad
    mov di, shared + SB_halinfo
    mov dword [di + HI_dwSize], HI_SIZE
    mov word [di + HI_lpDDCallbacks], shared + SB_cb_dd
    mov [di + HI_lpDDCallbacks + 2], ds
    mov word [di + HI_lpDDSurfaceCallbacks], shared + SB_cb_surf
    mov [di + HI_lpDDSurfaceCallbacks + 2], ds
    mov word [di + HI_lpDDPaletteCallbacks], shared + SB_cb_pal
    mov [di + HI_lpDDPaletteCallbacks + 2], ds
    mov dword [shared + SB_cb_dd + CBDD_dwSize], CBDD_SIZE
    mov word [shared + SB_cb_dd + CBDD_DestroyDriver], DestroyDriver16
    mov [shared + SB_cb_dd + CBDD_DestroyDriver + 2], cs
    mov dword [shared + SB_cb_surf + CBSURF_dwSize], CBSURF_SIZE
    mov dword [shared + SB_cb_pal + CBPAL_dwSize], CBPAL_SIZE
    mov dword [di + HI_ddCaps + CAPS_dwSize], CAPS_SIZE

    ; The primary surface: the frame on the screen, at video memory offset 0.
    lea si, [di + HI_vmiData]
    mov eax, [shared + SB_fb_linear]
    mov [si + VMI_fpPrimary], eax
    mov dword [si + VMI_dwFlags], 0
    movzx eax, word [scr_width]
    mov [si + VMI_dwDisplayWidth], eax
    movzx eax, word [scr_height]
    mov [si + VMI_dwDisplayHeight], eax
    mov eax, [scr_pitch]
    mov [si + VMI_lDisplayPitch], eax
    lea bx, [si + VMI_ddpfDisplay]
    mov dword [bx + PF_dwSize], PF_SIZE
    mov dword [bx + PF_dwFlags], DDPF_RGB
    mov dword [bx + PF_dwFourCC], 0
    movzx eax, word [scr_bpp]
    mov [bx + PF_dwRGBBitCount], eax
    mov dword [bx + PF_dwRBitMask], 0
    mov dword [bx + PF_dwGBitMask], 0
    mov dword [bx + PF_dwBBitMask], 0
    mov dword [bx + PF_dwRGBAlphaBitMask], 0
    cmp word [scr_bpp], 8
    jne .rgb
    or dword [bx + PF_dwFlags], DDPF_PALETTEINDEXED8
    jmp .aligns
.rgb:
    cmp word [scr_bpp], 16
    jne .rgb32
    mov dword [bx + PF_dwRBitMask], 0f800h
    mov dword [bx + PF_dwGBitMask], 07e0h
    mov dword [bx + PF_dwBBitMask], 001fh
    jmp .aligns
.rgb32:
    mov dword [bx + PF_dwRBitMask], 0ff0000h
    mov dword [bx + PF_dwGBitMask], 0ff00h
    mov dword [bx + PF_dwBBitMask], 0ffh
.aligns:
    mov dword [si + VMI_dwOffscreenAlign], 16
    mov dword [si + VMI_dwOverlayAlign], 16
    mov dword [si + VMI_dwTextureAlign], 16
    mov dword [si + VMI_dwZBufferAlign], 16
    mov dword [si + VMI_dwAlphaAlign], 16

    ; One linear heap: from the first 4 KB boundary after the primary frame
    ; to below the GPU's reserve (inclusive end).
    mov word [si + VMI_pvmList], shared + SB_heap
    mov [si + VMI_pvmList + 2], ds
    movzx eax, word [scr_height]
    imul eax, [scr_pitch]
    add eax, 0fffh
    and eax, ~0fffh
    mov ecx, [shared + SB_reserve_base]
    mov dword [si + VMI_dwNumHeaps], 0
    cmp eax, ecx
    jae .no_heap
    mov dword [si + VMI_dwNumHeaps], 1
    mov edx, [shared + SB_fb_linear]
    add eax, edx
    mov [shared + SB_heap + VM_fpStart], eax
    lea eax, [ecx + edx - 1]
    mov [shared + SB_heap + VM_fpEnd], eax
    mov dword [shared + SB_heap + VM_dwFlags], VIDMEM_ISLINEAR
    mov dword [shared + SB_heap + VM_ddsCaps], 0
    mov dword [shared + SB_heap + VM_ddsCapsAlt], 0
    mov dword [shared + SB_heap + VM_lpHeap], 0
.no_heap:

    movzx eax, word [scr_refresh]
    mov [di + HI_dwMonitorFrequency], eax
    call find_ddmode
    mov [di + HI_dwModeIndex], eax
    mov dword [di + HI_lpdwFourCC], 0
    mov eax, [shared + SB_num_modes]
    mov [di + HI_dwNumModes], eax
    mov word [di + HI_lpModeInfo], shared + SB_modes
    mov [di + HI_lpModeInfo + 2], ds
    mov dword [di + HI_dwFlags], DDHALINFO_ISPRIMARYDISPLAY
    cmp dword [di + HI_GetDriverInfo], 0
    je .no_gdi
    or dword [di + HI_dwFlags], DDHALINFO_GETDRIVERINFOSET
    ; and what the DLL asks for: GETDRIVERINFO2 when it answers DirectX 8's
    ; GetDriverInfo2 (0 from a DLL of an older build).
    mov eax, [shared + SB_hal_flags]
    or [di + HI_dwFlags], eax
.no_gdi:
    mov eax, [pdevice]
    mov [di + HI_lpPDevice], eax
    mov eax, [shared + SB_dll_hinstance]
    mov [di + HI_hInstance], eax
    ; Execute buffers of the driver's own (in the GPU's pool) when the DLL
    ; filled in their callbacks; otherwise DirectDraw allocates them.
    mov dword [di + HI_lpDDExeBufCallbacks], 0
    cmp dword [shared + SB_cb_exebuf + CBEXE_dwFlags], 0
    je .no_exebuf
    mov dword [shared + SB_cb_exebuf + CBEXE_dwSize], CBEXE_SIZE
    mov word [di + HI_lpDDExeBufCallbacks], shared + SB_cb_exebuf
    mov [di + HI_lpDDExeBufCallbacks + 2], ds
.no_exebuf:
    popad
    pop es
    ret

; After a mode change: DirectDraw gets the new description, if it has a
; driver object. Preserves all registers.
ddraw_mode_changed:
    cmp dword [set_info], 0
    je .done
    pushad
    push es
    call build_halinfo
    push ds
    push word shared + SB_halinfo
    push word 1                         ; reset: TRUE, the same driver object
    call far [set_info]
    pop es
    popad
.done:
    ret

; ---------------------------------------------------------------------------
; ogl_offered: carry clear if the GPU draws OpenGL now (PROFILE has OGL and
; RENDER3D: the host started its OpenGL device), set otherwise or without a
; register window. Preserves every register but the flags.
; ---------------------------------------------------------------------------

ogl_offered:
    push ds
    push es
    push eax
    mov ax, _DATA
    mov ds, ax
    cmp word [regs_sel], 0
    je .no
    mov es, [regs_sel]
    mov eax, [es:GPU_REG_PROFILE]
    and eax, GPU_PROFILE_OGL | GPU_PROFILE_RENDER3D
    cmp eax, GPU_PROFILE_OGL | GPU_PROFILE_RENDER3D
    jne .no
    pop eax
    pop es
    pop ds
    clc
    ret
.no:
    pop eax
    pop es
    pop ds
    stc
    ret

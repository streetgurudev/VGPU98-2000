; VGPU98 display driver: the display engine, mode choice and the VDD.
;
; The engine's registers are sequencer registers behind a key
; (spec/display-engine.md); every routine here leaves the lock and the
; sequencer index as it found them.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

; ---------------------------------------------------------------------------
; Engine register access
; ---------------------------------------------------------------------------

; In: AL = index. Out: AL = value. Preserves the rest.
ext_in:
    push dx
    push bx
    mov bl, al
    mov dx, 0x3c4
    in al, dx
    mov bh, al
    mov al, bl
    out dx, al
    inc dx
    in al, dx
    dec dx
    xchg al, bh
    out dx, al
    mov al, bh
    pop bx
    pop dx
    ret

; In: AL = index, AH = value. Preserves all registers.
ext_out:
    push ax
    push dx
    push bx
    mov bx, ax
    mov dx, 0x3c4
    in al, dx
    push ax
    mov al, bl
    out dx, al
    inc dx
    mov al, bh
    out dx, al
    dec dx
    pop ax
    out dx, al
    pop bx
    pop dx
    pop ax
    ret

; In: AL = first index, CX = value (low byte first).
ext_out_word:
    push ax
    mov ah, cl
    call ext_out
    inc al
    mov ah, ch
    call ext_out
    pop ax
    ret

; In: AL = first index, ECX = value: the four bytes in ascending order, so
; the highest is written last.
ext_out_dword:
    push ax
    push ecx
    call ext_out_word
    add al, 2
    shr ecx, 16
    call ext_out_word
    pop ecx
    pop ax
    ret

; In: AL = first index. Out: EAX = the dword from AL upward. Preserves the rest.
ext_in_dword:
    push bx
    push cx
    mov bl, al
    mov cx, 4
.byte:
    shl eax, 8
    mov al, bl
    add al, cl
    dec al
    push eax
    call ext_in
    mov bh, al
    pop eax
    mov al, bh
    loop .byte
    pop cx
    pop bx
    ret

; Out: AL = 01h if the registers were unlocked, 00h if locked; they are
; unlocked now. Preserves the rest.
ext_unlock:
    push bx
    mov al, SR_DISP_KEY
    call ext_in
    mov bl, al
    mov ax, (DISP_KEY << 8) | SR_DISP_KEY
    call ext_out
    mov al, bl
    pop bx
    ret

; In: AL = state from ext_unlock. Preserves all registers.
ext_relock:
    push ax
    test al, al
    jnz .done
    mov ax, SR_DISP_KEY
    call ext_out
.done:
    pop ax
    ret

; ---------------------------------------------------------------------------
; Detection and configuration
; ---------------------------------------------------------------------------

; Looks for the display engine; reads its video memory size and framebuffer
; address. Out: CF set if there is no usable engine. Preserves the rest.
hw_detect:
    push eax
    call ext_unlock
    push ax
    mov al, SR_DISP_ID
    call ext_in
    cmp al, DISP_ID
    jne .absent
    mov al, SR_DISP_REVISION
    call ext_in
    cmp al, 1
    jb .absent
    mov al, SR_DISP_FEATURES
    call ext_in
    and al, 03h                         ; display engine and linear framebuffer
    cmp al, 03h
    jne .absent
    mov al, SR_DISP_VRAM_SIZE
    call ext_in_dword
    mov [vram_size], eax
    cmp eax, 100000h
    jb .absent
    sub eax, GPU_RESERVE                ; the top of video memory is the GPU's
    mov [shared + SB_reserve_base], eax
    mov al, SR_DISP_LFB_BASE
    call ext_in_dword
    mov [lfb_base], eax
    test eax, eax
    jz .absent                          ; BAR1 not assigned
    mov byte [hw_present], 1
    pop ax
    call ext_relock
    pop eax
    clc
    ret
.absent:
    pop ax
    call ext_relock
    pop eax
    stc
    ret

; Reads the mode the configuration asks for from the VDD (the registry's
; display settings), or takes 640x480 at 8 bpp; then makes it a mode the card
; can run. Preserves all registers.
read_config:
    push es
    pushad
    mov word [cfg_width], 640
    mov word [cfg_height], 480
    mov word [cfg_bpp], 8
    mov word [cfg_dpi], 96
    mov word [cfg_refresh], 0
    mov word [cfg_max_vert], 0
    cmp dword [vdd_entry], 0
    je .fix
    push ds
    pop es
    mov di, dispinfo
    mov cx, DISPLAYINFO_SIZE
    xor al, al
    cld
    rep stosb
    mov eax, VDD_GET_DISPLAY_CONFIG
    movzx ebx, word [vm_handle]
    mov ecx, DISPLAYINFO_SIZE
    xor edx, edx
    mov edi, dispinfo
    call far [vdd_entry]
    cmp eax, VDD_GET_DISPLAY_CONFIG     ; no VDD
    je .fix
    cmp eax, -1                         ; the VDD could not read the registry
    je .fix
    mov ax, [dispinfo + DI_diXRes]
    mov [cfg_width], ax
    mov ax, [dispinfo + DI_diYRes]
    mov [cfg_height], ax
    movzx ax, byte [dispinfo + DI_diBpp]
    mov [cfg_bpp], ax
    mov ax, [dispinfo + DI_diDPI]
    test ax, ax
    jz .dpi_default
    mov [cfg_dpi], ax
.dpi_default:
    mov ax, [dispinfo + DI_diRefreshRateMax]
    mov [cfg_refresh], ax
    mov ax, [dispinfo + DI_diHighVert]
    mov [cfg_max_vert], ax
.fix:
    ; A mode the card cannot run becomes 640x480 at the same depth, then 8 bpp.
    mov bx, [cfg_bpp]
    mov cx, [cfg_width]
    mov dx, [cfg_height]
    cmp bx, 8
    je .depth_ok
    cmp bx, 16
    je .depth_ok
    cmp bx, 32
    je .depth_ok
    mov bx, 8
.depth_ok:
    call check_mode
    cmp ax, VALMODE_YES
    je .store
    mov cx, 640
    mov dx, 480
    call check_mode
    cmp ax, VALMODE_YES
    je .store
    mov bx, 8
.store:
    mov [cfg_bpp], bx
    mov [cfg_width], cx
    mov [cfg_height], dx
    DEBUG_TEXT DRV_TAG, "configured "
    movzx eax, cx
    call debug_dec
    DEBUG_TEXT "x"
    movzx eax, dx
    call debug_dec
    DEBUG_TEXT "x"
    movzx eax, bx
    call debug_dec
    DEBUG_TEXT ", "
    movzx eax, word [cfg_dpi]
    call debug_dec
    DEBUG_TEXT " dpi", 10
    popad
    pop es
    ret

; In: BX = bits per pixel, CX = width, DX = height.
; Out: AX = VALMODE_*: whether the card can run the mode. Preserves the rest.
; From 640x480 up, any size the engine takes is accepted, not only the mode
; table's, so modes added to the registry work too; below, the mode table's
; low-resolution modes (shown with double scan below 640 pixels across).
; Every mode stays below the GPU's reserve at the top of video memory.
check_mode:
    push ebx
    push ecx
    push edx
    mov ax, VALMODE_NO_NODAC
    cmp bx, 8
    je .depth
    cmp bx, 16
    je .depth
    cmp bx, 32
    jne .done
.depth:
    mov ax, VALMODE_NO_UNKNOWN
    test cl, 7                          ; widths are multiples of 8
    jnz .done
    cmp cx, DISP_MAX_WIDTH
    ja .done
    cmp dx, DISP_MAX_HEIGHT
    ja .done
    cmp cx, 640
    jb .low
    cmp dx, 480
    jae .size_ok
.low:
    call find_low_mode
    jc .done
.size_ok:
    movzx eax, cx
    shr bx, 3
    movzx ebx, bx
    imul eax, ebx
    movzx edx, dx
    imul eax, edx                       ; bytes the frame needs
    cmp eax, [shared + SB_reserve_base]
    mov ax, VALMODE_NO_NOMEM
    ja .done
    mov ax, VALMODE_YES
.done:
    pop edx
    pop ecx
    pop ebx
    ret

; In: CX = width, DX = height. Out: CF clear if the full mode table (game
; modes included) has the resolution. Preserves all registers.
find_low_mode:
    push ax
    push si
    mov si, drv_ddmode_table
    mov ax, DRV_DDMODE_COUNT
.next:
    cmp [cs:si], cx
    jne .skip
    cmp [cs:si + 2], dx
    je .found
.skip:
    add si, 4
    dec ax
    jnz .next
    pop si
    pop ax
    stc
    ret
.found:
    pop si
    pop ax
    clc
    ret

; In: CX = width, DX = height. Out: SI = the mode table entry (in CS), or CF
; set if the table has no such resolution.
find_mode:
    push ax
    mov si, drv_mode_table
    mov ax, DRV_MODE_COUNT
.next:
    cmp [cs:si + DRV_MODE_WIDTH], cx
    jne .skip
    cmp [cs:si + DRV_MODE_HEIGHT], dx
    je .found
.skip:
    add si, DRV_MODE_SIZE
    dec ax
    jnz .next
    pop ax
    stc
    ret
.found:
    pop ax
    clc
    ret

; Sets scr_refresh and scr_vbe for the mode in scr_*: the requested rate if
; the mode has it; for "optimal" (FFFFh), its fastest rate the monitor takes;
; otherwise the mode's default rate. Modes outside the table run at 60 Hz.
choose_refresh:
    pushad
    mov word [scr_refresh], 60
    mov word [scr_vbe], 0C000h          ; no VBE number: linear, memory kept
    mov cx, [scr_width]
    mov dx, [scr_height]
    call find_mode
    jc .done
    mov bx, DRV_MODE_VBE8
    cmp word [scr_bpp], 16
    jne .not16
    mov bx, DRV_MODE_VBE16
.not16:
    cmp word [scr_bpp], 32
    jne .not32
    mov bx, DRV_MODE_VBE32
.not32:
    mov ax, [cs:si + bx]
    test ax, ax
    jz .rates
    or ax, 0C000h
    mov [scr_vbe], ax
.rates:
    movzx cx, byte [cs:si + DRV_MODE_RATES]
    movzx ax, byte [cs:si + DRV_MODE_RATE]
    mov [scr_refresh], ax               ; the default
    mov dx, [cfg_refresh]
    xor bx, bx                          ; the fastest acceptable rate so far
    lea di, [si + DRV_MODE_RATE]
.rate:
    movzx ax, byte [cs:di]
    cmp ax, dx
    je .take
    cmp dx, 0ffffh
    jne .next_rate
    cmp word [cfg_max_vert], 50
    jb .next_rate                       ; monitor limits unknown
    cmp ax, [cfg_max_vert]
    ja .next_rate
    cmp ax, bx
    jbe .next_rate
    mov bx, ax
.next_rate:
    inc di
    loop .rate
    test bx, bx
    jz .done
    mov ax, bx
.take:
    mov [scr_refresh], ax
.done:
    popad
    ret

; ---------------------------------------------------------------------------
; Setting the mode
; ---------------------------------------------------------------------------

; Sets the configured mode: programs the engine, maps the framebuffer once,
; registers with the VDD and clears the screen. Out: CF set on failure.
physical_enable:
    pushad
    cmp byte [hw_present], 0
    je .fail
    mov bx, [cfg_bpp]
    mov cx, [cfg_width]
    mov dx, [cfg_height]
    call check_mode
    cmp ax, VALMODE_YES
    jne .fail
    mov [scr_bpp], bx
    mov [scr_width], cx
    mov [scr_height], dx
    xor ax, ax
    cmp bx, 8
    sete al
    mov [palettized], ax
    movzx eax, cx
    shr bx, 3
    movzx ebx, bx
    imul eax, ebx
    mov [scr_pitch], eax
    call choose_refresh

    call gpu_stop                       ; held from here to the end of the change
    mov ax, VDD_PRE_MODE_CHANGE
    call call_vdd
    call cursor_disable                 ; its pattern is the old depth's until cursor_reload
    call gamma_disable                  ; until its table is written again (gamma_reload)
    call program_engine
    cmp word [screen_sel], 0
    jne .mapped
    call map_framebuffer
    jc .fail_held
.mapped:
    call register_vdd
    mov ax, VDD_POST_MODE_CHANGE
    call call_vdd
    mov ax, VDD_SAVE_DRIVER_STATE
    call call_vdd
    call clear_screen

    ; The mode for the DirectX DLL.
    movzx eax, word [scr_width]
    mov [shared + SB_mode_width], eax
    movzx eax, word [scr_height]
    mov [shared + SB_mode_height], eax
    movzx eax, word [scr_bpp]
    mov [shared + SB_mode_bpp], eax
    mov eax, [scr_pitch]
    mov [shared + SB_mode_pitch], eax
    movzx eax, word [scr_refresh]
    mov [shared + SB_mode_refresh], eax
    inc dword [shared + SB_mode_serial]
    mov dword [shared + SB_flip_start], 0
    call gpu_release
    call cursor_reload                  ; the card's cursor at the new depth
    call gamma_reload                   ; the ramp stays until the next set, whatever the mode

    DEBUG_TEXT DRV_TAG, "mode "
    movzx eax, word [scr_width]
    call debug_dec
    DEBUG_TEXT "x"
    movzx eax, word [scr_height]
    call debug_dec
    DEBUG_TEXT "x"
    movzx eax, word [scr_bpp]
    call debug_dec
    DEBUG_TEXT " at "
    movzx eax, word [scr_refresh]
    call debug_dec
    DEBUG_TEXT " Hz", 10
    popad
    clc
    ret
.fail_held:
    call gpu_release
.fail:
    popad
    stc
    ret

; Programs the engine with the mode in scr_*: everything first, ENABLE last;
; double scan for the low-resolution modes, below 640 pixels across.
; Preserves all registers.
program_engine:
    pushad
    call ext_unlock
    push ax
    mov ah, [scr_bpp]
    mov al, SR_DISP_FORMAT
    call ext_out
    mov cx, [scr_width]
    mov al, SR_DISP_WIDTH
    call ext_out_word
    mov cx, [scr_height]
    mov al, SR_DISP_HEIGHT
    call ext_out_word
    mov cx, [scr_pitch]
    mov al, SR_DISP_PITCH
    call ext_out_word
    xor ecx, ecx
    mov al, SR_DISP_START
    call ext_out_dword
    mov al, SR_DISP_BANK
    call ext_out_word
    mov ah, [scr_refresh]
    mov al, SR_DISP_REFRESH
    call ext_out
    mov cx, [scr_vbe]                   ; what VBE 4F03h reports in the system VM
    mov al, SR_DISP_SCRATCH
    call ext_out_word
    mov ax, (DISP_CTRL_ENABLE << 8) | SR_DISP_CONTROL
    cmp word [scr_width], 640
    jae .single
    mov ax, ((DISP_CTRL_ENABLE | DISP_CTRL_DSCAN) << 8) | SR_DISP_CONTROL
.single:
    call ext_out
    pop ax
    call ext_relock
    popad
    ret

; Turns the engine off. Preserves all registers.
engine_off:
    push ax
    call ext_unlock
    push ax
    mov ax, SR_DISP_CONTROL             ; AH = 0
    call ext_out
    pop ax
    call ext_relock
    pop ax
    ret

; Maps the framebuffer (all of video memory) and gives it a selector, through
; DPMI. Out: CF set on failure.
map_framebuffer:
    pushad
    xor ax, ax                          ; allocate one descriptor
    mov cx, 1
    int 31h
    jc .fail
    mov [screen_sel], ax
    mov bx, [lfb_base + 2]              ; physical address in BX:CX
    mov cx, [lfb_base]
    mov si, [vram_size + 2]             ; size in SI:DI
    mov di, [vram_size]
    mov ax, 0800h                       ; map physical memory: linear in BX:CX
    int 31h
    jc .free
    mov [shared + SB_fb_linear], cx     ; for DirectDraw and the DLL
    mov [shared + SB_fb_linear + 2], bx
    mov dx, cx
    mov cx, bx
    mov bx, [screen_sel]
    mov ax, 0007h                       ; base in CX:DX
    int 31h
    jc .free
    mov eax, [vram_size]
    dec eax
    mov dx, ax
    shr eax, 16
    mov cx, ax
    mov ax, 0008h                       ; limit in CX:DX
    int 31h
    jc .free
    popad
    clc
    ret
.free:
    mov bx, [screen_sel]
    mov ax, 0001h                       ; free the descriptor
    int 31h
    mov word [screen_sel], 0
.fail:
    DEBUG_TEXT DRV_TAG, "cannot map the framebuffer", 10
    popad
    stc
    ret

; Clears the visible frame. Preserves all registers.
clear_screen:
    push es
    pushad
    mov es, [screen_sel]
    xor edi, edi
    movzx ecx, word [scr_height]
    imul ecx, [scr_pitch]
    shr ecx, 2
    xor eax, eax
    cld
    a32 rep stosd
    popad
    pop es
    ret

; Loads DAC entries from the colour table. In: AX = first entry, CX = count.
; The table holds 8-bit DIBColorEntry values; the DAC takes 6 bits.
; Preserves all registers.
load_dac:
    push es
    pushad
    cmp ax, 256
    jae .done
    mov bx, 256
    sub bx, ax
    cmp cx, bx
    jbe .count
    mov cx, bx
.count:
    jcxz .done
    les si, [color_table]
    mov bx, ax
    shl bx, 2
    add si, bx
    mov dx, 0x3c8
    out dx, al
    inc dx
.next:
    mov al, [es:si + 2]                 ; red
    shr al, 2
    out dx, al
    mov al, [es:si + 1]                 ; green
    shr al, 2
    out dx, al
    mov al, [es:si]                     ; blue
    shr al, 2
    out dx, al
    add si, 4
    loop .next
.done:
    popad
    pop es
    ret

; ---------------------------------------------------------------------------
; The VDD
; ---------------------------------------------------------------------------

; In: AX = VDD function (no arguments beyond the VM handle). Preserves all
; registers.
call_vdd:
    cmp dword [vdd_entry], 0
    je .done
    push es
    pushad
    movzx eax, ax
    movzx ebx, word [vm_handle]
    call far [vdd_entry]
    popad
    pop es
.done:
    ret

; Registers with the VDD. First the driver information, which the VDD hands
; to the mini-VDD (its REGISTER_DISPLAY_DRIVER): the video memory size, no
; extra screen selector. Then the visible frame's size and where to call to
; restore the desktop after a full-screen session, with EDX = -1: DOS
; graphics are not run in a window (that needs planar VGA memory beside the
; desktop, which this card does not have). Preserves all registers.
register_vdd:
    cmp dword [vdd_entry], 0
    je .done
    push es
    pushad
    mov eax, VDD_REGISTER_DISPLAY_DRIVER_INFO
    movzx ebx, word [vm_handle]
    mov ecx, [vram_size]
    xor edx, edx
    xor edi, edi
    call far [vdd_entry]
    mov eax, VDD_DRIVER_REGISTER
    movzx ebx, word [vm_handle]
    movzx ecx, word [scr_height]
    imul ecx, [scr_pitch]
    mov edx, -1
    push cs
    pop es
    mov edi, RestoreDesktopMode
    call far [vdd_entry]
    popad
    pop es
.done:
    ret

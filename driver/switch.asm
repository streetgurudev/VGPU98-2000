; VGPU98 display driver: full-screen DOS sessions and repaints.
;
; When a DOS session goes full screen, the VDD broadcasts INT 2Fh AX=4001h in
; the system VM: the driver marks the PDEVICE busy so GDI stops drawing. When
; the desktop comes back, the VDD calls RestoreDesktopMode (registered with
; VDD_DRIVER_REGISTER) and broadcasts AX=4002h: the mode is set again and
; USER repaints everything.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

; INT 2Fh handler: the two switch notifications; everything else goes to the
; previous handler untouched.
int2f_hook:
    cmp ax, INT2F_SWITCH_OUT
    je .ours
    cmp ax, INT2F_SWITCH_IN
    je .ours
    jmp far [cs:old_int2f]
.ours:
    push ds
    pushad
    mov bp, sp
    mov bx, _DATA
    mov ds, bx
    and word [bp + 32 + 2 + 4], ~1      ; the caller's CF clear: switching is fine
    cmp ax, INT2F_SWITCH_IN
    je .in
    les di, [pdevice]                   ; out: GDI must not draw
    lock or word [es:di + DE_deFlags], BUSY
    jmp .done
.in:
    les di, [pdevice]
    test word [es:di + DE_deFlags], BUSY
    jz .repaint
    call restore_mode                   ; the VDD has not called back yet
.repaint:
    call repaint
.done:
    popad
    pop ds
    iret

; RestoreDesktopMode: far call from the VDD when the desktop gets the screen
; back. Preserves all registers.
RestoreDesktopMode:
    push ds
    push es
    pushad
    mov ax, _DATA
    mov ds, ax
    DEBUG_TEXT DRV_TAG, "desktop restored", 10
    call restore_mode
    popad
    pop es
    pop ds
    retf

; Sets the desktop mode again, reloads the DAC and lets GDI draw.
restore_mode:
    push es
    pushad
    mov ax, VDD_PRE_MODE_CHANGE
    call call_vdd
    call cursor_disable                 ; until its pattern is written again, below
    call gamma_disable                  ; likewise its table
    call program_engine
    cmp word [palettized], 0
    je .no_dac
    xor ax, ax
    mov cx, 256
    call load_dac
.no_dac:
    les di, [pdevice]
    lock and word [es:di + DE_deFlags], ~BUSY
    call cursor_reload                  ; the DOS session may have written over its pattern
    call gamma_reload                   ; and over its table
    mov ax, VDD_SAVE_DRIVER_STATE
    call call_vdd
    call clear_screen                   ; USER repaints it
    popad
    pop es
    ret

; Asks USER to repaint the screen, or remembers to while repaints are held off.
repaint:
    cmp dword [repaint_func], 0
    je .done
    cmp byte [no_repaint], 0
    je .now
    mov byte [paint_pending], 1
    ret
.now:
    push ds
    push es
    pushad
    call far [repaint_func]
    popad
    pop es
    pop ds
.done:
    ret

; Installs the INT 2Fh handler and finds USER's repaint routine.
hook_int2f:
    push es
    pushad
    cmp dword [repaint_func], 0
    jne .have_repaint
    push ds
    push word user_module
    call far GetModuleHandle
    test ax, ax
    jz .have_repaint
    push ax
    push word 0                         ; MAKEINTRESOURCE(275): RepaintScreen
    push word 275
    call far GetProcAddress
    mov [repaint_func], ax
    mov [repaint_func + 2], dx
.have_repaint:
    ; The previous handler's address goes in the code segment, written
    ; through a data alias of it.
    mov ax, 352fh
    int 21h                             ; ES:BX = the current handler
    mov si, bx
    mov di, es
    push cs
    call far AllocCStoDSAlias
    test ax, ax
    jz .done
    mov es, ax
    mov [es:old_int2f], si
    mov [es:old_int2f + 2], di
    push ax
    call far FreeSelector
    push ds
    push cs
    pop ds
    mov dx, int2f_hook
    mov ax, 252fh
    int 21h
    pop ds
    mov byte [hooked], 1
.done:
    popad
    pop es
    ret

; Puts the previous INT 2Fh handler back.
unhook_int2f:
    cmp byte [hooked], 0
    je .done
    push ds
    pusha
    lds dx, [cs:old_int2f]
    mov ax, 252fh
    int 21h
    popa
    pop ds
    mov byte [hooked], 0
.done:
    ret

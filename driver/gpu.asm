; VGPU98 display driver: the GPU's registers (spec/gpu-registers.md).
;
; The driver maps BAR0 for itself and for the DirectX DLL, stops and holds
; the GPU around every mode change (HOLD_DRIVER), and makes GDI wait for the
; GPU before the DIB engine draws on the screen (spec/directx-hal.md, "GDI
; and the GPU"). Waits never trust the GPU blindly: they end when it is
; held, stopped or in error, or when neither FENCE nor RING_HEAD has moved
; for about a second (counted in vertical blanks, which the display engine
; counts in every mode).
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

NO_PROGRESS_VBLANKS equ 60      ; about a second at any refresh rate the engine runs

; Maps BAR0 (its address from the display engine's REGS_BASE, revision 2 and
; later) with a selector for the driver and a linear address for the DLL.
; Leaves regs_sel 0 if there is no register window. Preserves all registers.
map_registers:
    pushad
    cmp word [regs_sel], 0
    jne .done
    call ext_unlock
    push ax
    mov al, SR_DISP_REVISION
    call ext_in
    cmp al, 2
    jb .none
    mov al, SR_DISP_REGS_BASE
    call ext_in_dword
    test eax, eax
    jz .none
    mov [regs_phys], eax
    pop ax
    call ext_relock

    xor ax, ax                          ; one descriptor
    mov cx, 1
    int 31h
    jc .fail
    mov [regs_sel], ax
    mov bx, [regs_phys + 2]             ; physical address in BX:CX
    mov cx, [regs_phys]
    xor si, si                          ; 64 KB in SI:DI
    mov di, 0
    inc si
    mov ax, 0800h                       ; map physical memory: linear in BX:CX
    int 31h
    jc .free
    mov [shared + SB_regs_linear], cx
    mov [shared + SB_regs_linear + 2], bx
    mov dx, cx
    mov cx, bx
    mov bx, [regs_sel]
    mov ax, 0007h                       ; base in CX:DX
    int 31h
    jc .free
    xor cx, cx
    mov dx, 0ffffh
    mov ax, 0008h                       ; limit 64 KB - 1
    int 31h
    jc .free
    DEBUG_TEXT DRV_TAG, "GPU registers mapped", 10
    jmp .done
.none:
    pop ax
    call ext_relock
    jmp .done
.free:
    mov bx, [regs_sel]
    mov ax, 0001h
    int 31h
    mov word [regs_sel], 0
    mov dword [shared + SB_regs_linear], 0
.fail:
    DEBUG_TEXT DRV_TAG, "cannot map the GPU registers", 10
.done:
    popad
    ret

; Takes the ring lock (spec/directx-hal.md) against the DirectX DLL. A DLL
; holder that has not let go after about two seconds without GPU progress
; has gone away: the lock is taken over. In: ES = regs_sel.
; Preserves all registers.
ring_lock:
    pushad
    mov ecx, [es:GPU_REG_VBLANK_COUNT]
    mov ebx, [es:GPU_REG_FENCE]
    mov edx, [es:GPU_REG_RING_HEAD]
.try:
    lock bts dword [shared + SB_lock], 0
    jnc .taken
    mov eax, [es:GPU_REG_FENCE]
    cmp eax, ebx
    jne .moved
    mov eax, [es:GPU_REG_RING_HEAD]
    cmp eax, edx
    jne .moved
    mov eax, [es:GPU_REG_VBLANK_COUNT]
    sub eax, ecx
    cmp eax, 2 * NO_PROGRESS_VBLANKS
    jb .try
    DEBUG_TEXT DRV_TAG, "taking the ring lock over", 10
    jmp .taken
.moved:
    mov ebx, [es:GPU_REG_FENCE]
    mov edx, [es:GPU_REG_RING_HEAD]
    mov ecx, [es:GPU_REG_VBLANK_COUNT]
    jmp .try
.taken:
    mov dword [shared + SB_lock_owner], SB_DRIVER_OWNER
    popad
    ret

ring_unlock:
    mov dword [shared + SB_lock_owner], 0
    mov dword [shared + SB_lock], 0
    ret

; Waits until the GPU has nothing left to do, it stops (held, in error, not
; running), or it makes no progress for about a second. In: ES = regs_sel.
; Preserves all registers.
gpu_wait_idle:
    pushad
    mov ecx, [es:GPU_REG_VBLANK_COUNT]
    mov ebx, [es:GPU_REG_FENCE]
    mov edx, [es:GPU_REG_RING_HEAD]
.loop:
    mov eax, [es:GPU_REG_STATUS]
    test eax, GPU_STATUS_RUNNING
    jz .done
    test eax, GPU_STATUS_HELD | GPU_STATUS_ERROR
    jnz .done
    test eax, GPU_STATUS_BUSY
    jz .done
    mov eax, [es:GPU_REG_FENCE]
    cmp eax, ebx
    jne .moved
    mov eax, [es:GPU_REG_RING_HEAD]
    cmp eax, edx
    jne .moved
    mov eax, [es:GPU_REG_VBLANK_COUNT]
    sub eax, ecx
    cmp eax, NO_PROGRESS_VBLANKS
    jb .loop
    DEBUG_TEXT DRV_TAG, "GPU made no progress; stopping it", 10
    jmp .done
.moved:
    mov ebx, [es:GPU_REG_FENCE]
    mov edx, [es:GPU_REG_RING_HEAD]
    mov ecx, [es:GPU_REG_VBLANK_COUNT]
    jmp .loop
.done:
    popad
    ret

; Before a mode change: under the ring lock, lets the GPU finish, then holds
; and resets it (HOLD_DRIVER with RESET), so nothing it held outside video
; memory survives and nothing can restart it. Every context sends its whole
; state again afterwards. Preserves all registers.
gpu_stop:
    cmp word [regs_sel], 0
    je .done
    push es
    mov es, [regs_sel]
    call ring_lock
    call gpu_wait_idle
    mov dword [es:GPU_REG_CONTROL], GPU_CTRL_HOLD_DRIVER | GPU_CTRL_HOLD_DRIVER_WE | GPU_CTRL_RESET
    inc dword [shared + SB_state_epoch]
    mov dword [shared + SB_state_owner], 0
    call ring_unlock
    pop es
.done:
    ret

; After a mode change: lets go of HOLD_DRIVER. The DLL programs the ring and
; starts the GPU on its next use. Preserves all registers.
gpu_release:
    cmp word [regs_sel], 0
    je .done
    push es
    mov es, [regs_sel]
    mov dword [es:GPU_REG_CONTROL], GPU_CTRL_HOLD_DRIVER_WE
    pop es
.done:
    ret

; Waits for the last fence the drivers had issued when it was called (the
; end of their last batch), so GDI never draws under a GPU packet still
; writing the screen; a batch issued meanwhile is not waited for, so a
; process that keeps submitting cannot keep GDI waiting (directx-hal.md,
; GDI and the GPU). Ends early when the GPU stops or makes no progress.
; Preserves all registers.
gpu_wait_last_fence:
    cmp word [regs_sel], 0
    je .quick
    push es
    pushad
    mov es, [regs_sel]
    mov esi, [shared + SB_fence_issued]
    mov ecx, [es:GPU_REG_VBLANK_COUNT]
    mov ebx, [es:GPU_REG_FENCE]
    mov edx, [es:GPU_REG_RING_HEAD]
.loop:
    mov eax, [es:GPU_REG_FENCE]
    sub eax, esi
    jns .done                           ; FENCE - issued >= 0 as a signed number
    mov eax, [es:GPU_REG_STATUS]
    test eax, GPU_STATUS_RUNNING
    jz .done
    test eax, GPU_STATUS_HELD | GPU_STATUS_ERROR
    jnz .done
    mov eax, [es:GPU_REG_FENCE]
    cmp eax, ebx
    jne .moved
    mov eax, [es:GPU_REG_RING_HEAD]
    cmp eax, edx
    jne .moved
    mov eax, [es:GPU_REG_VBLANK_COUNT]
    sub eax, ecx
    cmp eax, NO_PROGRESS_VBLANKS
    jb .loop
    jmp .done
.moved:
    mov ebx, [es:GPU_REG_FENCE]
    mov edx, [es:GPU_REG_RING_HEAD]
    mov ecx, [es:GPU_REG_VBLANK_COUNT]
    jmp .loop
.done:
    popad
    pop es
.quick:
    ret

; The PDEVICE's access callback: the DIB engine calls it before it draws on
; the screen. Waits for the GPU, then goes on to the DIB engine's own
; (cursor exclusion) with the caller's arguments untouched.
begin_access_hook:
    push ds
    push ax
    mov ax, _DATA
    mov ds, ax
    call gpu_wait_last_fence
    pop ax
    pop ds
    jmp far DIB_BeginAccess

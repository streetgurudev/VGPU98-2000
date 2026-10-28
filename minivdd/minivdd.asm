; VGPU98 mini-VDD for Windows 98 SE (vgpu98.vxd).
;
; Windows' VDD virtualizes the VGA for DOS sessions; this mini-VDD adds what
; is specific to the card: the display engine's registers, which are
; sequencer registers behind a key (spec/display-engine.md).
;
; - A VM that does not own the screen (a DOS session in a window, or in the
;   background) gets its own copy of the lock and the mode registers: its
;   BIOS calls cannot turn the desktop's display off. It reads the card's
;   identity, memory size, framebuffer address and EDID from the hardware.
; - Engine state is saved and restored per VM as full-screen sessions come
;   and go; the display driver also sets the desktop's mode again itself.
;   The VDD's blue-screen message mode gets the engine off.
; - Before the VDD gives the screen to a DOS session, the engine goes off.
; - A full-screen session with the engine on (a VBE program) cannot be
;   switched away from: the desktop would draw over its frame.
; - Before a VGA mode takes the screen, the GPU finishes, is reset and held
;   (HOLD_VDD, spec/gpu-registers.md); it is let go when the desktop's state
;   comes back.
; - At boot it allocates the command ring's buffer in main memory and names
;   it to the DirectX driver through BAR0's SYSBUF registers (interface 2.1).
; - At boot it allocates the GPU's page table, and maps main memory into it
;   in chunks as the DirectX driver asks through DeviceIoControl: the pool
;   the GPU reads vertex and index buffers from (interface 3.0).
;
; Interfaces: spec/minivdd.md. Linked by vglink with minivdd/vgpu98.lnk.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

bits 32
cpu 386

%include "card.inc"
%include "vxdapi.inc"

segment _LTEXT class=LCODE use32 flat align=16
segment _LDATA class=LCODE use32 flat align=16

SEQ_INDEX       equ 3c4h

; Per-VM state, in each VM's control block at cb_offset.
VS_UNLOCKED     equ 0                   ; byte: the VM's lock (1 unlocked)
VS_REGS         equ 1                   ; SR80h-BFh as the VM sees them, by index - 80h
VS_SIZE         equ VS_REGS + 40h

%define VS_REG(sr) (VS_REGS + (sr) - SR_DISP_KEY)

; ---------------------------------------------------------------------------
; Device descriptor block and data
; ---------------------------------------------------------------------------

segment _LDATA

global VGPU98_DDB
VGPU98_DDB:
    dd 0                                ; next (VMM's list)
    dw DDK_VERSION
    dw UNDEFINED_DEVICE_ID
    db 1, 0                             ; version 1.0
    dw 0                                ; flags
    db "VGPU98  "                       ; device name, 8 characters
    dd VDD_INIT_ORDER
    dd control_proc
    dd 0, 0, 0, 0                       ; no V86 or protected-mode API
    dd 0                                ; reference data
    dd 0, 0                             ; no services
    dd 0                                ; no Win32 services
    dd 50726576h                        ; 'Prev', as the VMM's MASM headers write it
    dd 50h                              ; size of this block
    dd 52737631h, 52737632h, 52737633h  ; 'Rsv1', 'Rsv2', 'Rsv3', likewise

initialized     db 0
display_on      db 0                    ; the display driver has registered
sys_saved       db 0                    ; the system VM's state was saved since then
msg_state_done  db 0                    ; the VDD has built its message mode state
sys_vm          dd 0
msg_vm          dd 0                    ; the VDD's message mode pseudo-VM ID
hw_vm           dd 0                    ; the VM whose engine state the hardware holds, 0: none
cb_offset       dd 0
vram_size       dd 0
regs            dd 0                    ; linear address of the GPU's registers (BAR0), 0: none
gpu_held        db 0                    ; HOLD_VDD is set
sysbuf_phys     dd 0                    ; the ring buffer in main memory: physical address

; The pool: main memory the GPU reads through its page table (interface 3.0).
POOL_CHUNKS     equ 256
CH_HANDLE       equ 0                   ; _PageAllocate's handle, 0: a free record
CH_LINEAR       equ 4
CH_ENTRY        equ 8                   ; its first entry in the GPU's page table
CH_PAGES        equ 12
CH_SIZE         equ 16
CH_CLAIMED      equ 0FFFFFFFFh          ; CH_HANDLE of a record a request is filling
GPUVM_CLAIMED   equ 2                   ; an entry a request is filling: not VALID

pool_table      dd 0                    ; the GPU's page table: linear address, 0: no pool
pool_table_phys dd 0
pool_cap        dd 0                    ; pages the pool maps at most: the table's entries
pool_used       dd 0                    ; pages mapped
pool_entries    dd 0                    ; GPUVM_ENTRIES as last written
pool_chunks     times POOL_CHUNKS * CH_SIZE db 0
demand_info     times DEMAND_INFO_SIZE db 0

segment _LTEXT

%include "vxddebug.asm"

; ---------------------------------------------------------------------------
; The hardware registers (ring 0: the ports are not trapped here)
; ---------------------------------------------------------------------------

; In: AL = index. Out: AL = value. Preserves the sequencer index and the
; other registers.
phys_in:
    push edx
    push ebx
    mov bl, al
    mov dx, SEQ_INDEX
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
    pop ebx
    pop edx
    ret

; In: AL = index, AH = value. Preserves all registers and the index.
phys_out:
    push eax
    push edx
    push ebx
    mov ebx, eax
    mov dx, SEQ_INDEX
    in al, dx
    push eax
    mov al, bl
    out dx, al
    inc dx
    mov al, bh
    out dx, al
    dec dx
    pop eax
    out dx, al
    pop ebx
    pop edx
    pop eax
    ret

; Out: AL = 01h if the engine was unlocked, 00h if locked; it is unlocked now.
; Preserves the other registers.
phys_unlock:
    push ebx
    push eax
    mov al, SR_DISP_KEY
    call phys_in
    mov bl, al
    mov ax, (DISP_KEY << 8) | SR_DISP_KEY
    call phys_out
    pop eax
    mov al, bl
    pop ebx
    ret

; In: AL = state from phys_unlock. Preserves all registers.
phys_relock:
    push eax
    test al, al
    jnz .done
    mov ax, SR_DISP_KEY
    call phys_out
.done:
    pop eax
    ret

; Reads a register with the engine unlocked for the read; the lock is left as
; it was. In: AL = index. Out: AL = value.
phys_keyed_in:
    push ebx
    mov bl, al
    call phys_unlock
    mov bh, al
    mov al, bl
    call phys_in
    xchg al, bh
    call phys_relock
    mov al, bh
    pop ebx
    ret

; Writes a register with the engine unlocked for the write. In: AL = index,
; AH = value. Preserves all registers.
phys_keyed_out:
    push eax
    push ebx
    mov ebx, eax
    call phys_unlock
    push eax
    mov eax, ebx
    call phys_out
    pop eax
    call phys_relock
    pop ebx
    pop eax
    ret

; Looks for the display engine and reads the video memory size.
; Out: CF set if there is none. Preserves all registers.
hw_detect:
    pushad
    call phys_unlock
    mov dl, al
    mov al, SR_DISP_ID
    call phys_in
    cmp al, DISP_ID
    jne .absent
    mov al, SR_DISP_REVISION
    call phys_in
    cmp al, 1
    jb .absent
    mov al, SR_DISP_FEATURES
    call phys_in
    and al, 03h
    cmp al, 03h
    jne .absent
    mov ecx, 4
    mov bl, SR_DISP_VRAM_SIZE + 3
.size:
    shl dword [vram_size], 8
    mov al, bl
    call phys_in
    or [vram_size], al
    dec bl
    loop .size
    mov al, dl
    call phys_relock
    popad
    clc
    ret
.absent:
    mov al, dl
    call phys_relock
    popad
    stc
    ret

; ---------------------------------------------------------------------------
; Per-VM state
; ---------------------------------------------------------------------------

; In: EBX = VM. Out: ESI = its state.
vm_state:
    mov esi, ebx
    add esi, [cb_offset]
    ret

; In: EBX = VM. Out: ZF set if it owns the screen. Preserves all registers.
is_screen_owner:
    push eax
    push ecx
    push edx
    push esi
    push edi
    VxDCall VDD_Get_VM_Info             ; EDI = the screen's owner
    cmp ebx, edi
    pop edi
    pop esi
    pop edx
    pop ecx
    pop eax
    ret

; In: EBX = VM. Out: ZF set if its engine register accesses go to the
; hardware: it owns the screen and, for the system VM, the VDD has built its
; message mode state (it does so with BIOS mode sets in the system VM, which
; must not reach the hardware). Preserves all registers.
direct_access:
    call is_screen_owner
    jne .done                           ; ZF clear
    cmp ebx, [sys_vm]
    je .system
    cmp ebx, ebx                        ; ZF set
    ret
.system:
    cmp byte [msg_state_done], 1
.done:
    ret

; In: CL = index. Out: ZF set if it is a mode register a VM keeps its own
; copy of (CONTROL through BANK, SCRATCH). Preserves all registers.
is_mode_reg:
    cmp cl, SR_DISP_CONTROL
    jb .no
    cmp cl, SR_DISP_BANK + 1
    jbe .yes
    cmp cl, SR_DISP_SCRATCH
    je .yes
    cmp cl, SR_DISP_SCRATCH + 1
    je .yes
.no:
    or cl, cl                           ; CL is never 0 here: ZF clear
    ret
.yes:
    cmp cl, cl
    ret

; Copies the hardware's engine state into the state at ESI. Preserves all
; registers.
save_state:
    pushad
    mov al, SR_DISP_KEY
    call phys_in
    mov [esi + VS_UNLOCKED], al
    call phys_unlock
    mov dl, al
    mov cl, SR_DISP_CONTROL
.next:
    call is_mode_reg
    jne .skip
    mov al, cl
    call phys_in
    movzx ebx, cl
    mov [esi + ebx + VS_REGS - SR_DISP_KEY], al
.skip:
    inc cl
    cmp cl, SR_DISP_SCRATCH + 2
    jb .next
    mov al, dl
    call phys_relock
    popad
    ret

; Puts the state at ESI into the hardware: every mode register, then CONTROL,
; then the VM's lock. Preserves all registers.
restore_state:
    pushad
    call phys_unlock
    mov cl, SR_DISP_CONTROL + 1
.next:
    call is_mode_reg
    jne .skip
    movzx ebx, cl
    mov ah, [esi + ebx + VS_REGS - SR_DISP_KEY]
    mov al, cl
    call phys_out
.skip:
    inc cl
    cmp cl, SR_DISP_SCRATCH + 2
    jb .next
    mov ah, [esi + VS_REG(SR_DISP_CONTROL)]
    mov al, SR_DISP_CONTROL
    call phys_out
    cmp byte [esi + VS_UNLOCKED], 0
    jne .unlocked
    mov ax, SR_DISP_KEY
    call phys_out
.unlocked:
    popad
    ret

; In: EBX = VM. Out: ZF clear if the engine is on for it: in the hardware
; for the screen's owner, in its own copy otherwise. Preserves all registers.
engine_on_for:
    call is_screen_owner
    je .hardware
    push esi
    call vm_state
    test byte [esi + VS_REG(SR_DISP_CONTROL)], DISP_CTRL_ENABLE
    pop esi
    ret
.hardware:
    push eax
    mov al, SR_DISP_CONTROL
    call phys_keyed_in
    test al, DISP_CTRL_ENABLE
    pop eax
    ret

; ---------------------------------------------------------------------------
; Initialization
; ---------------------------------------------------------------------------

; EAX = message, EBX = a VM handle. CF set fails the message.
control_proc:
    cmp eax, SYS_DYNAMIC_DEVICE_INIT
    je init
    cmp eax, DEVICE_INIT
    je init
    cmp eax, DESTROY_VM
    je destroy_vm
    cmp eax, SYSTEM_EXIT
    je system_exit
    cmp eax, W32_DEVICEIOCONTROL
    je w32_deviceiocontrol
    clc
    ret

; Windows is leaving, and the ring buffer and status page with it: the
; SYSBUF registers no longer name them, and the GPU stops writing its status
; there (a reset that keeps the card's registers must not hand them to the
; next boot).
system_exit:
    push edi
    mov edi, [regs]
    test edi, edi
    jz .done
    call sysbuf_clear
    call gpuvm_clear
.done:
    pop edi
    clc
    ret

; EBX = a VM about to go. Its handle may come back for a new VM, which must
; not be taken for the VM whose state the hardware holds.
destroy_vm:
    cmp ebx, [hw_vm]
    jne .done
    mov dword [hw_vm], 0
.done:
    clc
    ret

; EBX = the system VM. Registers the mini-VDD's functions with the VDD.
init:
    cmp byte [initialized], 0
    jne .ok
    pushad
    mov [sys_vm], ebx
    mov [hw_vm], ebx
    call hw_detect
    jc .fail
    call map_registers
    call alloc_ring_buffer
    call alloc_page_table
    push dword 0                        ; flags
    push dword VS_SIZE
    VxDCall _Allocate_Device_CB_Area
    add esp, 8
    test eax, eax
    jz .fail
    mov [cb_offset], eax
    VxDCall VDD_Get_Special_VM_IDs      ; forced planar (ESI) comes only as a memory owner
    mov [msg_vm], edi
    VxDCall VDD_Get_Mini_Dispatch_Table
    cmp ecx, MINI_VDD_FUNCTIONS_NEEDED
    jb .fail
    mov dword [edi + 4 * REGISTER_DISPLAY_DRIVER], register_display_driver
    mov dword [edi + 4 * PRE_HIRES_TO_VGA], pre_hires_to_vga
    mov dword [edi + 4 * POST_HIRES_TO_VGA], post_hires_to_vga
    mov dword [edi + 4 * SAVE_REGISTERS], save_registers
    mov dword [edi + 4 * RESTORE_REGISTERS], restore_registers
    mov dword [edi + 4 * VIRTUALIZE_SEQUENCER_IN], virtualize_sequencer_in
    mov dword [edi + 4 * VIRTUALIZE_SEQUENCER_OUT], virtualize_sequencer_out
    mov dword [edi + 4 * DISPLAY_DRIVER_DISABLING], display_driver_disabling
    mov dword [edi + 4 * CHECK_HIRES_MODE], check_hires_mode
    mov dword [edi + 4 * GET_TOTAL_VRAM_SIZE], get_total_vram_size
    mov dword [edi + 4 * CHECK_SCREEN_SWITCH_OK], check_screen_switch_ok
    mov dword [edi + 4 * SAVE_MESSAGE_MODE_STATE], save_message_mode_state
    mov byte [initialized], 1
    DEBUG_TEXT VXD_TAG, "loaded", 10
    popad
.ok:
    clc
    ret
.fail:
    DEBUG_TEXT VXD_TAG, "not loaded (no display engine, or the VDD is too old)", 10
    popad
    stc
    ret

; ---------------------------------------------------------------------------
; Mini-VDD functions (called by the VDD)
; ---------------------------------------------------------------------------

; REGISTER_DISPLAY_DRIVER: the display driver has set a mode and passed its
; information to the VDD (VDD_REGISTER_DISPLAY_DRIVER_INFO). A saved desktop
; state is from before that mode.
register_display_driver:
    mov byte [display_on], 1
    mov byte [sys_saved], 0
    ret

; DISPLAY_DRIVER_DISABLING: the display driver is going back to VGA text.
display_driver_disabling:
    mov byte [display_on], 0
    mov byte [sys_saved], 0
    ret

; SAVE_MESSAGE_MODE_STATE: the VDD has built its message mode state with
; virtual mode sets in the system VM; from now on the system VM, when it owns
; the screen, reaches the engine directly.
save_message_mode_state:
    mov byte [msg_state_done], 1
    ret

; PRE_HIRES_TO_VGA: the VDD is about to put the hardware into a VGA mode (a
; full-screen DOS session, message mode): the VGA core must drive the display
; again. The state the hardware holds is kept first, so the desktop's comes
; back whatever the VDD does next; then the hardware holds no VM's state.
pre_hires_to_vga:
    pushad
    mov ebx, [hw_vm]
    test ebx, ebx
    jz .off
    cmp ebx, [msg_vm]
    je .off
    call vm_state
    call save_state
    cmp ebx, [sys_vm]
    jne .off
    mov byte [sys_saved], 1
.off:
    call gpu_hold                       ; the GPU must not draw into the VGA mode's memory
    call phys_unlock
    push eax
    mov ax, SR_DISP_CONTROL             ; AH = 0
    call phys_out
    pop eax
    call phys_relock
    mov dword [hw_vm], 0
    DEBUG_TEXT VXD_TAG, "to VGA", 10
    popad
    ret

; POST_HIRES_TO_VGA: EBX = the new screen owner, whose VGA mode the BIOS has
; just set. Its state is what the hardware holds now (engine off), as the
; DDK's S3 sample takes it. The desktop's saved state is kept: the desktop
; is not running while the engine is off. Preserves all registers.
post_hires_to_vga:
    cmp ebx, [msg_vm]
    je .message_mode
    cmp ebx, [sys_vm]
    jne .save
    cmp byte [display_on], 0
    jne .done
.save:
    push esi
    call vm_state
    call save_state
    pop esi
.message_mode:
    mov [hw_vm], ebx
.done:
    ret

; SAVE_REGISTERS: EBX = the VM whose hardware state the VDD saves. Only the
; VM whose state the hardware holds is saved: any other VM's copy is already
; its state. The pseudo-VMs have no control block: message mode may hold the
; hardware, forced planar never does. Preserves EBX, EDX, EDI, ESI.
save_registers:
    cmp ebx, [hw_vm]
    jne .done
    cmp ebx, [msg_vm]
    je .done
    push esi
    call vm_state
    call save_state
    pop esi
    cmp ebx, [sys_vm]
    jne .done
    mov byte [sys_saved], 1
.done:
    ret

; RESTORE_REGISTERS: ESI = memory owner, ECX = screen owner. The screen
; owner's engine state goes into the hardware unless it is there already.
; Message mode (the VDD's blue screens) gets the engine off. The desktop gets
; the state saved when it lost the screen; without one (the mode has changed
; since), the display driver sets the mode itself, and with no display driver
; the engine is off. Forced planar (font loading) comes as the memory owner
; and changes nothing here. Preserves EBX, EDX.
restore_registers:
    pushad
    cmp ecx, [hw_vm]
    je .done
    cmp ecx, [msg_vm]
    je .engine_off
    cmp ecx, [sys_vm]
    jne .restore
    cmp byte [sys_saved], 0
    jne .restore
    cmp byte [display_on], 0
    jne .owner
.engine_off:
    call phys_unlock
    push eax
    mov ax, SR_DISP_CONTROL             ; AH = 0
    call phys_out
    pop eax
    call phys_relock
    jmp .owner
.restore:
    mov ebx, ecx
    call vm_state
    call restore_state
.owner:
    mov [hw_vm], ecx
.done:
    cmp ecx, [sys_vm]                   ; the desktop has the screen: the GPU may run
    jne .held
    call gpu_unhold
.held:
    popad
    ret

; ---------------------------------------------------------------------------
; The GPU (spec/gpu-registers.md)
; ---------------------------------------------------------------------------

NO_PROGRESS_VBLANKS equ 60              ; about a second at any refresh rate

; Maps the GPU's registers: BAR0's address from the display engine's
; REGS_BASE (revision 2 and later). Preserves all registers.
map_registers:
    pushad
    call phys_unlock
    mov dl, al
    mov al, SR_DISP_REVISION
    call phys_in
    cmp al, 2
    jb .none
    mov ecx, 4
    mov bl, SR_DISP_REGS_BASE + 3
    xor esi, esi
.byte:
    shl esi, 8
    mov al, bl
    call phys_in
    mov dh, al
    movzx eax, dh
    or esi, eax
    dec bl
    loop .byte
    mov al, dl
    call phys_relock
    test esi, esi
    jz .done
    push dword 0                        ; flags
    push dword 10000h                   ; 64 KB
    push esi
    VxDCall _MapPhysToLinear
    add esp, 12
    cmp eax, -1
    je .done
    mov [regs], eax
    DEBUG_TEXT VXD_TAG, "GPU registers mapped", 10
    jmp .done
.none:
    mov al, dl
    call phys_relock
.done:
    popad
    ret

SYSBUF_PAGES     equ 512                ; 2 MB
SYSBUF_PAGES_MIN equ 256                ; else 1 MB

; Clears the SYSBUF registers and STATUS_BASE: no buffer named, and the GPU
; writes no status block. EDI = the GPU's registers. Preserves the rest.
sysbuf_clear:
    push eax
    xor eax, eax
    mov [edi + GPU_REG_SYSBUF_SIZE], eax
    mov [edi + GPU_REG_SYSBUF_PHYS], eax
    mov [edi + GPU_REG_SYSBUF_LINEAR], eax
    mov [edi + GPU_REG_SYSBUF_STATUS], eax
    mov [edi + GPU_REG_STATUS_BASE], eax
    pop eax
    ret

; The command ring's buffer in main memory (interface 2.1) and, in the page
; after it, the GPU's status block (interface 2.2): contiguous, fixed,
; zeroed pages in the system arena, allocated during initialization while
; contiguous pages can still be had (as the DDK's S3 ViRGE mini-VDD
; allocates its DMA buffer). BAR0's SYSBUF registers name them to the
; DirectX driver, which writes the ring and reads the status page through
; the linear address. They are cleared first: a warm restart keeps them,
; and the last boot's buffer is gone. SYSBUF_SIZE, written last, says the
; buffer is ready. Only a GPU whose PROFILE has SYSRING can read a ring
; there. Preserves all registers.
alloc_ring_buffer:
    pushad
    mov edi, [regs]
    test edi, edi
    jz .done
    call sysbuf_clear
    test dword [edi + GPU_REG_PROFILE], GPU_PROFILE_SYSRING
    jz .done
    mov ebx, SYSBUF_PAGES
.try:
    push dword PAGECONTIG | PAGEFIXED | PAGELOCKED | PAGEUSEALIGN | PAGEZEROINIT
    push dword sysbuf_phys
    push dword 0FFFFFh                  ; max page: below 4 GB
    push dword 100h                     ; min page: above the first megabyte
    push dword 0                        ; alignment: any page
    push dword 0                        ; VM: none, the system arena
    push dword PG_SYS
    lea eax, [ebx + 1]                  ; the ring's pages and the status page
    push eax
    VxDCall _PageAllocate
    add esp, 32
    test eax, eax
    jz .smaller
    test edx, edx
    jnz .ready
.smaller:
    cmp ebx, SYSBUF_PAGES_MIN
    je .failed
    mov ebx, SYSBUF_PAGES_MIN
    jmp .try
.ready:
    mov eax, [sysbuf_phys]
    mov [edi + GPU_REG_SYSBUF_PHYS], eax
    mov [edi + GPU_REG_SYSBUF_LINEAR], edx
    shl ebx, 12
    add eax, ebx
    mov [edi + GPU_REG_SYSBUF_STATUS], eax  ; the page after the ring
    mov [edi + GPU_REG_SYSBUF_SIZE], ebx
    cmp ebx, SYSBUF_PAGES << 12
    jne .ready_min
    DEBUG_TEXT VXD_TAG, "ring buffer ready: 2048 KB in main memory", 10
    jmp .done
.ready_min:
    DEBUG_TEXT VXD_TAG, "ring buffer ready: 1024 KB in main memory", 10
    jmp .done
.failed:
    DEBUG_TEXT VXD_TAG, "no ring buffer (no contiguous main memory): the ring stays in video memory", 10
.done:
    popad
    ret

; Lets the GPU finish (while FENCE or RING_HEAD still move), then resets and
; holds it: nothing it held outside video memory survives, and nothing can
; restart it while a VGA mode has the screen. Preserves all registers.
gpu_hold:
    pushad
    mov edi, [regs]
    test edi, edi
    jz .done
    mov ecx, [edi + GPU_REG_VBLANK_COUNT]
    mov ebx, [edi + GPU_REG_FENCE]
    mov edx, [edi + GPU_REG_RING_HEAD]
    mov ebp, 10000000h                  ; and a bound on the loop itself
.loop:
    dec ebp
    jz .hold
    mov eax, [edi + GPU_REG_STATUS]
    test eax, GPU_STATUS_RUNNING
    jz .hold
    test eax, GPU_STATUS_HELD | GPU_STATUS_ERROR
    jnz .hold
    test eax, GPU_STATUS_BUSY
    jz .hold
    mov eax, [edi + GPU_REG_FENCE]
    cmp eax, ebx
    jne .moved
    mov eax, [edi + GPU_REG_RING_HEAD]
    cmp eax, edx
    jne .moved
    mov eax, [edi + GPU_REG_VBLANK_COUNT]
    sub eax, ecx
    cmp eax, NO_PROGRESS_VBLANKS
    jb .loop
    jmp .hold
.moved:
    mov ebx, [edi + GPU_REG_FENCE]
    mov edx, [edi + GPU_REG_RING_HEAD]
    mov ecx, [edi + GPU_REG_VBLANK_COUNT]
    jmp .loop
.hold:
    mov dword [edi + GPU_REG_CONTROL], GPU_CTRL_HOLD_VDD | GPU_CTRL_HOLD_VDD_WE | GPU_CTRL_RESET
    mov byte [gpu_held], 1
.done:
    popad
    ret

; Lets go of HOLD_VDD. The DirectX driver starts the GPU on its next use.
; Preserves all registers.
gpu_unhold:
    cmp byte [gpu_held], 0
    je .done
    push edi
    mov edi, [regs]
    mov dword [edi + GPU_REG_CONTROL], GPU_CTRL_HOLD_VDD_WE
    mov byte [gpu_held], 0
    pop edi
.done:
    ret

; ---------------------------------------------------------------------------
; The pool: main memory the GPU reads through its page table (interface
; 3.0, spec/minivdd.md). The DirectX driver asks for it in chunks through
; DeviceIoControl; each chunk is fixed, locked, zeroed pages in the system
; arena (every process sees them), and their physical addresses go into a
; run of the GPU's page table. A quarter of main memory at most.
; ---------------------------------------------------------------------------

POOL_GROW       equ 1                   ; DeviceIoControl codes
POOL_RELEASE    equ 2
POOL_QUERY      equ 3

; Clears GPUVM_BASE and GPUVM_ENTRIES: no page table. EDI = the GPU's
; registers. Preserves all registers.
gpuvm_clear:
    push eax
    xor eax, eax
    mov [edi + GPU_REG_GPUVM_ENTRIES], eax
    mov [edi + GPU_REG_GPUVM_BASE], eax
    pop eax
    ret

; The GPU's page table: an entry for every page of a quarter of main memory
; (at most 65536), contiguous, fixed and zeroed in the system arena, allocated
; during initialization while contiguous pages can be had. GPUVM_BASE names
; it; GPUVM_ENTRIES stays 0 until memory is mapped. The registers are cleared
; first: a warm restart keeps them. Only a GPU whose PROFILE has GPUVM reads
; main memory this way. Preserves all registers.
alloc_page_table:
    pushad
    mov edi, [regs]
    test edi, edi
    jz .done
    call gpuvm_clear
    test dword [edi + GPU_REG_PROFILE], GPU_PROFILE_GPUVM
    jz .done
    push dword 0                        ; flags
    push dword demand_info
    VxDCall _GetDemandPageInfo
    add esp, 8
    mov ebx, [demand_info + DI_PHYS_COUNT]
    shr ebx, 2                          ; a quarter of main memory
    cmp ebx, GPU_GPUVM_ENTRIES_MAX
    jbe .capped
    mov ebx, GPU_GPUVM_ENTRIES_MAX
.capped:
    test ebx, ebx
    jz .failed
    lea ecx, [ebx * 4 + 0fffh]
    shr ecx, 12                         ; the table's pages: 4 bytes an entry
    push dword PAGECONTIG | PAGEFIXED | PAGELOCKED | PAGEUSEALIGN | PAGEZEROINIT
    push dword pool_table_phys
    push dword 0FFFFFh                  ; max page: below 4 GB
    push dword 100h                     ; min page: above the first megabyte
    push dword 0                        ; alignment: any page
    push dword 0                        ; VM: none, the system arena
    push dword PG_SYS
    push ecx
    VxDCall _PageAllocate
    add esp, 32
    test eax, eax
    jz .failed
    test edx, edx
    jz .failed
    mov [pool_table], edx
    mov [pool_cap], ebx
    mov eax, [pool_table_phys]
    mov [edi + GPU_REG_GPUVM_BASE], eax
    DEBUG_TEXT VXD_TAG, "pool ready: the GPU's page table for a quarter of main memory", 10
    jmp .done
.failed:
    DEBUG_TEXT VXD_TAG, "no pool (no contiguous main memory for the GPU's page table)", 10
.done:
    popad
    ret

; Out: EBX = a free chunk record; CF set if there is none. Preserves the
; other registers.
chunk_free_record:
    push ecx
    mov ebx, pool_chunks
    mov ecx, POOL_CHUNKS
.next:
    cmp dword [ebx + CH_HANDLE], 0
    je .found
    add ebx, CH_SIZE
    loop .next
    pop ecx
    stc
    ret
.found:
    pop ecx
    clc
    ret

; The first run of ECX free entries in the GPU's page table (first fit).
; Out: EAX = its first entry; CF set if there is none. Preserves the other
; registers.
table_free_run:
    push ebx
    push edx
    push edi
    mov edi, [pool_table]
    xor eax, eax                        ; where the run starts
    xor ebx, ebx                        ; the entry looked at
    xor edx, edx                        ; free entries in the run so far
.next:
    cmp edx, ecx
    jae .found
    cmp ebx, [pool_cap]
    jae .none
    cmp dword [edi + ebx * 4], 0
    jne .used
    inc edx
    inc ebx
    jmp .next
.used:
    inc ebx
    mov eax, ebx
    xor edx, edx
    jmp .next
.found:
    pop edi
    pop edx
    pop ebx
    clc
    ret
.none:
    pop edi
    pop edx
    pop ebx
    stc
    ret

; W32_DEVICEIOCONTROL: ESI = DIOCParams. EAX = 0 or a Win32 error code, CF
; clear. Opening and closing a handle need nothing. Preserves the other
; registers.
w32_deviceiocontrol:
    mov eax, [esi + DIOC_CODE]
    cmp eax, POOL_GROW
    je pool_grow
    cmp eax, POOL_RELEASE
    je pool_release
    cmp eax, POOL_QUERY
    je pool_query
    cmp eax, DIOC_OPEN
    je .ok
    cmp eax, DIOC_CLOSEHANDLE
    je .ok
    mov eax, ERROR_NOT_SUPPORTED
    clc
    ret
.ok:
    xor eax, eax
    clc
    ret

; POOL_GROW. In: the pages wanted (a dword). Out: the chunk's linear address,
; its GPU address and its pages (three dwords). ERROR_NOT_ENOUGH_MEMORY past
; the cap, without a free run of the table or a chunk record, or without
; the memory; the output then holds 0 and the step that failed (GROW_*).
; The record, the run and the pages are claimed before _PageAllocate, which
; can block: a request meanwhile finds them taken. EBP carries the error
; code, with the step in its high word.
GROW_CAP        equ 1                   ; past the cap (or no pool)
GROW_RECORD     equ 2                   ; no chunk record
GROW_RUN        equ 3                   ; no free run of the page table
GROW_PAGES      equ 4                   ; _PageAllocate refused the pages
GROW_COPY       equ 5                   ; _CopyPageTable failed
GROW_ABSENT     equ 6                   ; a page not present
GROW_ADAPTER    equ 7                   ; a page in the adapter area, which the GPU cannot read

pool_grow:
    pushad
    mov ebp, ERROR_INVALID_PARAMETER
    cmp dword [esi + DIOC_IN_SIZE], 4
    jb .fail
    cmp dword [esi + DIOC_OUT_SIZE], 12
    jb .fail
    cmp dword [esi + DIOC_OUT], 0
    je .fail
    mov eax, [esi + DIOC_IN]
    test eax, eax
    jz .fail
    mov ecx, [eax]                      ; the pages wanted
    test ecx, ecx
    jz .fail
    mov ebp, ERROR_NOT_ENOUGH_MEMORY | (GROW_CAP << 16)
    cmp dword [pool_table], 0
    je .fail
    mov eax, [pool_cap]
    sub eax, [pool_used]
    cmp ecx, eax
    ja .fail
    mov ebp, ERROR_NOT_ENOUGH_MEMORY | (GROW_RECORD << 16)
    call chunk_free_record
    jc .fail
    mov ebp, ERROR_NOT_ENOUGH_MEMORY | (GROW_RUN << 16)
    call table_free_run
    jc .fail
    mov [ebx + CH_ENTRY], eax
    mov [ebx + CH_PAGES], ecx
    mov dword [ebx + CH_HANDLE], CH_CLAIMED
    mov edi, eax
    shl edi, 2
    add edi, [pool_table]
    mov eax, GPUVM_CLAIMED              ; not VALID: the GPU sees nothing mapped yet
    cld
    rep stosd
    mov ecx, [ebx + CH_PAGES]
    add [pool_used], ecx
    push dword PAGEFIXED | PAGELOCKED | PAGEZEROINIT
    push dword 0                        ; &phys: not contiguous, so none
    push dword 0                        ; max page
    push dword 0                        ; min page
    push dword 0                        ; alignment
    push dword 0                        ; VM: none, the system arena
    push dword PG_SYS
    push ecx
    VxDCall _PageAllocate
    add esp, 32
    mov ebp, ERROR_NOT_ENOUGH_MEMORY | (GROW_PAGES << 16)
    test eax, eax
    jz .unclaim
    mov [ebx + CH_HANDLE], eax
    mov [ebx + CH_LINEAR], edx
    mov ecx, [ebx + CH_PAGES]           ; the C convention leaves ECX and EDX to the callee
    test edx, edx
    jz .undo
    ; The pages' physical addresses, straight into their entries: the page
    ; number and VALID kept, the processor's bits dropped.
    mov edi, [ebx + CH_ENTRY]
    shl edi, 2
    add edi, [pool_table]
    push dword 0                        ; flags
    push edi                            ; where the entries go
    push ecx                            ; pages
    shr edx, 12
    push edx                            ; the first linear page
    VxDCall _CopyPageTable
    add esp, 16
    mov ecx, [ebx + CH_PAGES]           ; the C convention leaves ECX and EDX to the callee
    mov ebp, ERROR_NOT_ENOUGH_MEMORY | (GROW_COPY << 16)
    test eax, eax
    jz .undo
    mov edx, ecx
.entry:
    mov eax, [edi]
    mov ebp, ERROR_NOT_ENOUGH_MEMORY | (GROW_ABSENT << 16)
    test eax, PTE_PRESENT               ; locked pages always are
    jz .undo
    and eax, 0FFFFF000h
    ; Any page of guest RAM but the adapter area (A0000h-FFFFFh), which the
    ; GPU cannot read.
    cmp eax, 0A0000h
    jb .page
    mov ebp, ERROR_NOT_ENOUGH_MEMORY | (GROW_ADAPTER << 16)
    cmp eax, 100000h
    jb .undo
.page:
    or eax, GPU_GPUVM_VALID
    mov [edi], eax
    add edi, 4
    dec edx
    jnz .entry
    ; GPUVM_ENTRIES covers the run once its entries are written.
    mov eax, [ebx + CH_ENTRY]
    add eax, ecx
    cmp eax, [pool_entries]
    jbe .covered
    mov [pool_entries], eax
    mov edi, [regs]
    mov [edi + GPU_REG_GPUVM_ENTRIES], eax
.covered:
    mov edi, [esi + DIOC_OUT]
    mov eax, [ebx + CH_LINEAR]
    mov [edi], eax
    mov eax, [ebx + CH_ENTRY]
    shl eax, 12
    mov [edi + 4], eax
    mov [edi + 8], ecx
    mov edi, [esi + DIOC_RETURNED]
    test edi, edi
    jz .done
    mov dword [edi], 12
.done:
    popad
    xor eax, eax
    clc
    ret
.undo:
    ; The pages go.
    push dword 0                        ; flags
    push dword [ebx + CH_HANDLE]
    VxDCall _PageFree
    add esp, 8
.unclaim:
    ; Whatever went into the run's entries goes, with the claims.
    mov edi, [ebx + CH_ENTRY]
    shl edi, 2
    add edi, [pool_table]
    mov ecx, [ebx + CH_PAGES]
    sub [pool_used], ecx
    xor eax, eax
    cld
    rep stosd
    mov dword [ebx + CH_HANDLE], 0
.fail:
    ; A step that failed goes into the output (checked above for these).
    mov eax, ebp
    shr eax, 16
    jz .code
    mov edi, [esi + DIOC_OUT]
    mov dword [edi], 0
    mov [edi + 4], eax
    and ebp, 0FFFFh
.code:
    mov [esp + 28], ebp                 ; EAX in the pushad frame
    popad
    clc
    ret

; POOL_RELEASE. In: a chunk's GPU address (a dword). Its entries are cleared
; and its pages freed. The DirectX driver releases a chunk only once no
; queued packet reads it (spec/gpu-registers.md).
pool_release:
    pushad
    mov ebp, ERROR_INVALID_PARAMETER
    cmp dword [esi + DIOC_IN_SIZE], 4
    jb .fail
    mov eax, [esi + DIOC_IN]
    test eax, eax
    jz .fail
    mov eax, [eax]
    test eax, 0fffh
    jnz .fail
    shr eax, 12
    mov ebx, pool_chunks
    mov ecx, POOL_CHUNKS
.find:
    cmp dword [ebx + CH_HANDLE], 0
    je .next
    cmp dword [ebx + CH_HANDLE], CH_CLAIMED ; a chunk still being made
    je .next
    cmp [ebx + CH_ENTRY], eax
    je .found
.next:
    add ebx, CH_SIZE
    loop .find
    jmp .fail
.found:
    mov edi, eax
    shl edi, 2
    add edi, [pool_table]
    mov ecx, [ebx + CH_PAGES]
    sub [pool_used], ecx
    xor eax, eax
    cld
    rep stosd
    push dword 0                        ; flags
    push dword [ebx + CH_HANDLE]
    VxDCall _PageFree
    add esp, 8
    mov dword [ebx + CH_HANDLE], 0
    popad
    xor eax, eax
    clc
    ret
.fail:
    mov [esp + 28], ebp                 ; EAX in the pushad frame
    popad
    clc
    ret

; POOL_QUERY. Out: the pool's cap in pages, the pages mapped, and
; GPUVM_ENTRIES (three dwords); a cap of 0 means no pool.
pool_query:
    pushad
    mov ebp, ERROR_INVALID_PARAMETER
    cmp dword [esi + DIOC_OUT_SIZE], 12
    jb .fail
    mov edi, [esi + DIOC_OUT]
    test edi, edi
    jz .fail
    mov eax, [pool_cap]
    mov [edi], eax
    mov eax, [pool_used]
    mov [edi + 4], eax
    mov eax, [pool_entries]
    mov [edi + 8], eax
    mov edi, [esi + DIOC_RETURNED]
    test edi, edi
    jz .done
    mov dword [edi], 12
.done:
    popad
    xor eax, eax
    clc
    ret
.fail:
    mov [esp + 28], ebp                 ; EAX in the pushad frame
    popad
    clc
    ret

; VIRTUALIZE_SEQUENCER_OUT: AL = value, EBX = VM, ECX = index, EDX = port.
; CF set: handled here. Preserves every register.
virtualize_sequencer_out:
    cmp cl, CARD_DEBUG_SEQ_INDEX
    je .debug
    cmp cl, SR_DISP_KEY
    jb .not_ours
    cmp cl, SR_DISP_KEY + 3fh
    ja .not_ours
    cmp cl, SR_DISP_GPU_RESET
    je .swallowed                       ; the VGA BIOS's at POST: no VM resets the GPU under Windows
    call direct_access
    je .hardware
    push esi
    call vm_state
    call virtual_out
    pop esi
    stc
    ret
.hardware:                              ; the screen's owner (its own lock governs)
.debug:                                 ; and the debug register: the hardware
    push eax
    mov ah, al
    mov al, cl
    call phys_out
    pop eax
    stc
    ret
.not_ours:
    clc
    ret
.swallowed:
    stc
    ret

; A write by a VM that does not own the screen. In: ESI = its state,
; CL = index, AL = value. Preserves all registers.
virtual_out:
    push eax
    cmp cl, SR_DISP_KEY
    jne .keyed
    cmp al, DISP_KEY
    sete byte [esi + VS_UNLOCKED]
    jmp .done
.keyed:
    cmp byte [esi + VS_UNLOCKED], 0
    je .done                            ; locked: ignored, as the hardware does
    cmp cl, SR_DISP_EDID_INDEX
    jne .not_edid
    mov ah, al                          ; the EDID port is shared: hardware
    mov al, cl
    call phys_keyed_out
    jmp .done
.not_edid:
    call is_mode_reg
    jne .done                           ; read-only or reserved
    cmp cl, SR_DISP_CONTROL
    jne .not_control
    and al, DISP_CTRL_ENABLE
    jmp .store
.not_control:
    cmp cl, SR_DISP_FORMAT
    jne .store
    cmp al, 8
    je .store
    cmp al, 15
    je .store
    cmp al, 16
    je .store
    cmp al, 32
    jne .done                           ; other formats are ignored
.store:
    push ebx
    movzx ebx, cl
    mov [esi + ebx + VS_REGS - SR_DISP_KEY], al
    pop ebx
.done:
    pop eax
    ret

; VIRTUALIZE_SEQUENCER_IN: EBX = VM, ECX = index, EDX = port.
; CF set: handled here, AL = value. Preserves every other register.
virtualize_sequencer_in:
    cmp cl, CARD_DEBUG_SEQ_INDEX
    je .debug
    cmp cl, SR_DISP_KEY
    jb .not_ours
    cmp cl, SR_DISP_KEY + 3fh
    ja .not_ours
    call direct_access
    je .hardware
    push esi
    call vm_state
    call virtual_in
    pop esi
    stc
    ret
.hardware:
.debug:
    mov al, cl
    call phys_in
    stc
    ret
.not_ours:
    clc
    ret

; A read by a VM that does not own the screen. In: ESI = its state,
; CL = index. Out: AL = value. Preserves the other registers.
virtual_in:
    cmp cl, SR_DISP_KEY
    jne .keyed
    mov al, [esi + VS_UNLOCKED]
    ret
.keyed:
    xor al, al
    cmp byte [esi + VS_UNLOCKED], 0
    je .done                            ; locked: reads 00h
    call is_mode_reg
    jne .not_mode
    push ebx
    movzx ebx, cl
    mov al, [esi + ebx + VS_REGS - SR_DISP_KEY]
    pop ebx
    ret
.not_mode:
    cmp cl, SR_DISP_CONTROL             ; identity, memory, framebuffer: hardware
    jb .hardware
    cmp cl, SR_DISP_EDID_INDEX
    je .hardware
    cmp cl, SR_DISP_EDID_DATA
    je .hardware
    xor al, al                          ; reserved
    ret
.hardware:
    mov al, cl
    call phys_keyed_in
.done:
    ret

; CHECK_HIRES_MODE: EBX = VM. CF set if it runs an extended (display
; engine) mode. Preserves all registers.
check_hires_mode:
    cmp ebx, [sys_vm]
    je .vga
    call engine_on_for
    jz .vga
    stc
    ret
.vga:
    clc
    ret

; GET_TOTAL_VRAM_SIZE: ECX = video memory in bytes, CF set.
get_total_vram_size:
    mov ecx, [vram_size]
    stc
    ret

; CHECK_SCREEN_SWITCH_OK: EAX = 0 (VGA mode) or -1 (VESA mode), EBX = the
; system VM, ECX = BIOS mode. CF set refuses to switch away from the
; full-screen session that owns the screen while its engine is on: the desktop
; would take the frame of a program that cannot redraw it. Preserves EBX, EDI.
check_screen_switch_ok:
    pushad
    VxDCall VDD_Get_VM_Info             ; EDI = the screen's owner
    cmp edi, [sys_vm]
    je .allow
    cmp edi, [msg_vm]
    je .allow
    mov al, SR_DISP_CONTROL
    call phys_keyed_in
    test al, DISP_CTRL_ENABLE
    jz .allow
    popad
    DEBUG_TEXT VXD_TAG, "switch refused (VBE mode)", 10
    stc
    ret
.allow:
    popad
    xor eax, eax
    clc
    ret

; VGPU98 mini-VDD: the Windows 98 VMM and VDD interfaces it uses, restated
; from the Windows 95/98 DDK headers and samples and checked against the
; dynamic links in compiled VxDs (spec/minivdd.md). Facts only.
;
; Copyright 2026 VGPU98 Project contributors
; SPDX-License-Identifier: GPL-2.0-or-later

; VxD services are called with INT 20h followed by a dword: the device ID in
; the high word, the service number in the low word. VMM patches the call
; into a direct one the first time it runs.
%macro VxDCall 1
    int 20h
    dd %1
%endmacro

VMM_ID                          equ 0001h
VDD_ID                          equ 000Ah

_Allocate_Device_CB_Area        equ (VMM_ID << 16) | 00A7h ; C: (bytes, flags) -> EAX = offset, 0 on failure
_MapPhysToLinear                equ (VMM_ID << 16) | 006Ch ; C: (phys, bytes, flags) -> EAX = linear, -1 on failure
_PageAllocate                   equ (VMM_ID << 16) | 0053h ; C: (pages, type, VM, align mask, min page, max page,
                                                           ;    &phys, flags) -> EAX = handle, EDX = linear; 0 on failure
_PageFree                       equ (VMM_ID << 16) | 0055h ; C: (handle, flags) -> EAX nonzero on success
_CopyPageTable                  equ (VMM_ID << 16) | 0061h ; C: (first linear page, pages, &entries, flags)
                                                           ;    -> EAX nonzero on success; the page table entries
_GetDemandPageInfo              equ (VMM_ID << 16) | 006Fh ; C: (&DemandInfoStruc, flags)

; DemandInfoStruc
DI_PHYS_COUNT                   equ 4           ; DIPhys_Count: the physical pages the pager manages
DEMAND_INFO_SIZE                equ 64

; Page table entries as _CopyPageTable copies them
PTE_PRESENT                     equ 00000001h

; W32_DEVICEIOCONTROL: ESI = DIOCParams; EAX = 0 or an error code
W32_DEVICEIOCONTROL             equ 0023h
DIOC_OPEN                       equ 0
DIOC_CLOSEHANDLE                equ -1
DIOC_CODE                       equ 12          ; DIOCParams.dwIoControlCode
DIOC_IN                         equ 16          ; lpvInBuffer
DIOC_IN_SIZE                    equ 20          ; cbInBuffer
DIOC_OUT                        equ 24          ; lpvOutBuffer
DIOC_OUT_SIZE                   equ 28          ; cbOutBuffer
DIOC_RETURNED                   equ 32          ; lpcbBytesReturned
ERROR_NOT_ENOUGH_MEMORY         equ 8
ERROR_NOT_SUPPORTED             equ 50
ERROR_INVALID_PARAMETER         equ 87

; _PageAllocate
PG_SYS                          equ 1           ; the system arena: every VM and Win32 process sees the pages
PAGEZEROINIT                    equ 00000001h
PAGEUSEALIGN                    equ 00000002h   ; the alignment and physical range apply (needs PAGEFIXED)
PAGECONTIG                      equ 00000004h   ; physically contiguous (needs PAGEUSEALIGN)
PAGEFIXED                       equ 00000008h
PAGELOCKED                      equ 00000080h
VDD_Get_Mini_Dispatch_Table     equ (VDD_ID << 16) | 000Eh ; -> EDI = table, ECX = entries
VDD_Get_VM_Info                 equ (VDD_ID << 16) | 0010h ; -> ESI = memory owner VM, EDI = screen (CRTC) owner VM
VDD_Get_Special_VM_IDs          equ (VDD_ID << 16) | 0011h ; -> EDI = message mode ID, ESI = forced planar ID

; Control messages (EAX), with EBX = a VM handle
DEVICE_INIT                     equ 0001h
SYSTEM_EXIT                     equ 0005h
DESTROY_VM                      equ 000Ch
SYS_DYNAMIC_DEVICE_INIT         equ 001Bh

; The device descriptor block
DDK_VERSION                     equ 0400h
UNDEFINED_DEVICE_ID             equ 0000h
VDD_INIT_ORDER                  equ 28000000h

; Mini-VDD functions: entries in the dispatch table VDD hands out
REGISTER_DISPLAY_DRIVER         equ 0
PRE_HIRES_TO_VGA                equ 4
POST_HIRES_TO_VGA               equ 5
SAVE_REGISTERS                  equ 8
RESTORE_REGISTERS               equ 9
VIRTUALIZE_SEQUENCER_IN         equ 18
VIRTUALIZE_SEQUENCER_OUT        equ 19
DISPLAY_DRIVER_DISABLING        equ 26
CHECK_HIRES_MODE                equ 35
GET_TOTAL_VRAM_SIZE             equ 36
CHECK_SCREEN_SWITCH_OK          equ 43
SAVE_MESSAGE_MODE_STATE         equ 45
MINI_VDD_FUNCTIONS_NEEDED       equ 46  ; the table must reach SAVE_MESSAGE_MODE_STATE

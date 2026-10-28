# Windows 98 mini-VDD

`vgpu98.vxd` is the card's mini-VDD for Windows 98 SE: a 32-bit virtual device (LE format, [le-format.md](le-format.md)) that Windows' own VDD (VDD.VXD) loads for the display device. The VDD virtualizes the IBM VGA for DOS sessions. The mini-VDD adds the card's display engine, whose registers are sequencer registers behind a key ([display-engine.md](display-engine.md)), and which the VDD does not know about. Source: `minivdd/` (NASM), linked by `vglink` with `minivdd/vgpu98.lnk`; tested on the host by `vxdtest`.

The interfaces below are restated from the Windows 95/98 DDK documentation and headers, used as a reference for facts only.

## Module

| | |
|---|---|
| Module name | `VGPU98` |
| Description | `VGPU98 2000 display mini-VDD (Version 4.0)` |
| Objects | one: `LCOD` (code and data), preloaded; nothing in it is ever paged out, so every function can run while the VDD handles a trapped port |
| Entry | the device descriptor block, `VGPU98_DDB` (entry 1 of the entry table) |
| Loaded by | VDD.VXD, from the display key's `DEFAULT\minivdd` value ([driver-package.md](driver-package.md)), during its own initialization: the mini-VDD gets Sys_Dynamic_Device_Init (or Device_Init) |

## Device descriptor block

| Field | Value |
|---|---|
| SDK version | 400h |
| Device ID | Undefined_Device_ID: no services, no API |
| Version | 1.0 |
| Name | `VGPU98  ` |
| Init order | VDD_Init_Order (28000000h), as the VDD |
| Control procedure | handles Sys_Dynamic_Device_Init (1Bh) and Device_Init (01h), Destroy_VM (0Ch: a VM handle about to go may come back for a new VM, so it stops naming the VM whose state the hardware holds), System_Exit (05h: the ring buffer, the status page and the page table go with Windows, so it clears the SYSBUF registers, STATUS_BASE, GPUVM_BASE and GPUVM_ENTRIES), and W32_DEVICEIOCONTROL (23h: the pool's requests, below); every other message returns with carry clear |
| Prev, Rsv1-Rsv3 | the dwords the VMM's headers define, which are MASM character constants: `'Prev'` is 50726576h (bytes `verP` in the file), `'Rsv1'` 52737631h, and so on |
| Size | 50h |

## Services it calls

All through INT 20h dynamic links (the VMM patches them on first use).

| Service | ID | Use |
|---|---|---|
| _Allocate_Device_CB_Area | 000100A7h | 41h bytes in every VM's control block: that VM's view of the display engine (below). C convention (bytes, flags), EAX = offset, 0 on failure |
| _MapPhysToLinear | 0001006Ch | The GPU's registers (below). C convention (physical address, bytes, flags), EAX = linear address, -1 on failure |
| _PageAllocate | 00010053h | The command ring's buffer in main memory and the GPU's page table (below): C convention (pages, PG_SYS, VM 0, alignment mask 0, lowest page 100h, highest page FFFFFh, &physical address, PAGECONTIG + PAGEFIXED + PAGELOCKED + PAGEUSEALIGN + PAGEZEROINIT). The pool's chunks: (pages, PG_SYS, VM 0, 0, 0, 0, no physical address, PAGEFIXED + PAGELOCKED + PAGEZEROINIT), locked and zeroed pages anywhere (without PAGEUSEALIGN the VMM takes PAGEFIXED as locked). EAX = handle and EDX = linear address, 0 on failure |
| _PageFree | 00010055h | A released chunk of the pool. C convention (handle, flags) |
| _CopyPageTable | 00010061h | A chunk's physical pages, as page table entries copied into the GPU's table. C convention (first linear page, pages, &entries, flags), EAX nonzero on success |
| _GetDemandPageInfo | 0001006Fh | The physical memory the pager manages (DIPhys_Count), for the pool's cap. C convention (&DemandInfoStruc, flags) |
| VDD_Get_Mini_Dispatch_Table | 000A000Eh | EDI = the VDD's table of mini-VDD functions, ECX = its number of entries |
| VDD_Get_VM_Info | 000A0010h | EDI = the VM that owns the screen (CRTC owner), ESI = the VM that owns video memory |
| VDD_Get_Special_VM_IDs | 000A0011h | EDI = message mode's ID, ESI = forced planar's ID: the VDD's pseudo-VMs, which it passes where a VM handle goes but which have no control block. Message mode comes as a screen owner (below); forced planar (font loading) only as a memory owner, which the mini-VDD has nothing to do with |

## Initialization

The first of the two initialization messages:

1. Looks for the display engine: identity 98h, revision 1 or later, display engine and linear framebuffer features. Reads the video memory size. The lock is left as it was.
2. Maps the GPU's registers and allocates the command ring's buffer in main memory (below); neither failing stops it.
3. Allocates the per-VM area and gets the pseudo-VM IDs.
4. Gets the dispatch table. A table shorter than 46 entries (up to SAVE_MESSAGE_MODE_STATE) is a VDD too old for this driver.
5. Puts its functions into the table (below), and writes a line to the debug register.

Any failure sets carry: the mini-VDD is not used, and the VDD runs the card as a plain VGA.

## Functions

The VDD calls these with the registers below; each preserves every register it does not return in.

| # | Function | Entry | What the mini-VDD does |
|---|---|---|---|
| 0 | REGISTER_DISPLAY_DRIVER | EBX = system VM, EBP = its client registers | Called when the display driver sends `VDD_REGISTER_DISPLAY_DRIVER_INFO` (83h), on every mode it sets. Notes that the display driver runs the desktop, and forgets the desktop's saved state, which is from before this mode |
| 4 | PRE_HIRES_TO_VGA | | The VDD is about to put the hardware into a VGA mode (a full-screen DOS session, message mode). Saves the state the hardware holds into its VM's copy (for the system VM, as its saved desktop), stops the GPU (below), then turns the engine off (CONTROL = 0) so the VGA core drives the display again; the other registers and the lock stay. The hardware then holds no VM's state |
| 5 | POST_HIRES_TO_VGA | EBX = new screen owner | The BIOS has set the new owner's VGA mode: its state is what the hardware holds now (engine off), as the DDK's S3 sample takes it. Not for message mode, and not for the system VM while the display driver is registered (its saved desktop stays) |
| 8 | SAVE_REGISTERS | EBX = VM | If the hardware holds that VM's engine state (below), copies the hardware's lock, CONTROL through BANK and SCRATCH into the VM's state. Any other VM's copy is already its state; message mode has no control block |
| 9 | RESTORE_REGISTERS | ECX = screen owner, ESI = memory owner | Below |
| 18 | VIRTUALIZE_SEQUENCER_IN | EBX = VM, ECX = index, EDX = port | Below. Carry set: handled, AL = value |
| 19 | VIRTUALIZE_SEQUENCER_OUT | AL = value, EBX = VM, ECX = index, EDX = port | Below. Carry set: handled |
| 26 | DISPLAY_DRIVER_DISABLING | | The display driver is leaving (Disable): forgets the desktop's saved state |
| 35 | CHECK_HIRES_MODE | EBX = VM | Carry set if the VM, other than the system VM, has the engine on |
| 36 | GET_TOTAL_VRAM_SIZE | | ECX = video memory in bytes, carry set |
| 43 | CHECK_SCREEN_SWITCH_OK | EAX = 0 (VGA) or -1 (VESA), EBX = the system VM, ECX = BIOS mode | Carry set refuses to switch away from the full-screen session that owns the screen (VDD_Get_VM_Info) while the hardware's engine is on: the desktop would take the frame of a VBE program that cannot redraw it. Otherwise EAX = 0, carry clear |
| 45 | SAVE_MESSAGE_MODE_STATE | | The VDD has built its message mode state (below): from now on the system VM reaches the engine directly when it owns the screen |

"Has the engine on" (CHECK_HIRES_MODE) means the hardware's CONTROL for the screen's owner, and the VM's own copy for any other VM.

## The GPU

At initialization the mini-VDD maps the GPU's registers (BAR0, from the display engine's REGS_BASE, revision 2 and later) with `_MapPhysToLinear` (VMM service 6Ch). At PRE_HIRES_TO_VGA it lets the GPU finish (while FENCE or RING_HEAD still move, up to about a second without progress, counted in vertical blanks), then writes HOLD_VDD with RESET: the GPU forgets every host copy and cannot restart while the VGA mode has the screen ([gpu-registers.md](gpu-registers.md)). When RESTORE_REGISTERS gives the screen back to the system VM, it lets go of HOLD_VDD; the DirectX driver restarts the GPU on its next use. It never takes the DirectX driver's ring lock: a batch the driver is writing meets the hold bit and fails with DDERR_SURFACELOST.

**The ring buffer and status page (interfaces 2.1 and 2.2).** Right after mapping the registers, the mini-VDD clears SYSBUF_SIZE, SYSBUF_PHYS, SYSBUF_LINEAR, SYSBUF_STATUS and STATUS_BASE (a warm restart keeps the card's registers, and the last boot's buffer is gone). When PROFILE has SYSRING, it allocates 513 pages (2 MB and the status page), else 257 (1 MB and the page), with `_PageAllocate`: contiguous, fixed, locked and zeroed, in the system arena, above the first megabyte. Contiguous pages can be had only during initialization; the DDK's S3 ViRGE mini-VDD allocates its DMA buffer the same way and hands the linear address to its Win32 component. It writes the physical address, the linear address, the status page's physical address (the page after the ring) and, last, the ring's size, and the debug line `ring buffer ready: 2048 KB in main memory` (or 1024 KB); without the memory, `no ring buffer (no contiguous main memory): the ring stays in video memory`. The buffer is never freed: it lives as long as Windows, and at System_Exit the mini-VDD clears the SYSBUF registers and STATUS_BASE, so a reset that keeps the card's registers cannot hand it to the next boot (and the VGA BIOS resets the GPU at POST besides). A VM's write of the display engine's GPU_RESET is swallowed: it is the BIOS's, and a DOS program must not clear the holds or the status block under Windows. The DirectX driver takes it from the SYSBUF registers ([gpu-registers.md](gpu-registers.md), [directx-hal.md](directx-hal.md)).

**The pool (interface 3.0).** Main memory the GPU reads through its page table (GPUVM_BASE, GPUVM_ENTRIES: [gpu-registers.md](gpu-registers.md)): the DirectX driver's vertex and index buffers.

- *At initialization*, after the ring buffer, the mini-VDD clears GPUVM_ENTRIES and GPUVM_BASE (a warm restart keeps them). When PROFILE has GPUVM, it reads the physical memory the pager manages (`_GetDemandPageInfo`, VMM service 6Fh: `DIPhys_Count`), takes a quarter of it as the pool's cap (at most 65536 pages, 256 MB), and allocates the page table, 4 bytes an entry, as it allocates the ring buffer: contiguous, fixed, locked and zeroed, in the system arena, above the first megabyte (32 KB for a 128 MB machine). GPUVM_BASE names it; GPUVM_ENTRIES stays 0. The debug line is `pool ready: the GPU's page table for a quarter of main memory`, or `no pool (no contiguous main memory for the GPU's page table)`.
- *Requests* come through `DeviceIoControl` on `\\.\VGPU98` (the control message W32_DEVICEIOCONTROL, 23h, with ESI the DIOCParams; EAX 0 or a Win32 error code). Opening and closing a handle need nothing. The codes:

  | Code | Name | In | Out | |
  |---|---|---|---|---|
  | 1 | POOL_GROW | pages wanted (a dword) | the chunk's linear address, GPU address and pages (three dwords) | Claims a chunk record, the first run of free entries of the GPU's table (first fit) and the pages against the cap before `_PageAllocate`, which can block (a request meanwhile finds them taken); allocates the pages (locked, zeroed, in the system arena, so every process sees them; not contiguous); copies their page table entries with `_CopyPageTable` (VMM service 61h) straight into the run, keeping the page number and setting VALID (a page not present, or one in the adapter area A0000h-FFFFFh, which the GPU refuses, fails the request; conventional memory below A0000h is RAM like any other), then raises GPUVM_ENTRIES to cover the run. ERROR_NOT_ENOUGH_MEMORY (8) past the cap, without a free run or one of its 256 chunk records, or without the pages; the output then holds 0 and the step that failed: 1 past the cap (or no pool), 2 no chunk record, 3 no free run of the table, 4 `_PageAllocate` refused the pages, 5 `_CopyPageTable` failed, 6 a page not present, 7 a page in the adapter area. ERROR_INVALID_PARAMETER (87) for short buffers or 0 pages, the output untouched |
  | 2 | POOL_RELEASE | a chunk's GPU address (a dword) | | Clears its entries and frees its pages (`_PageFree`, VMM service 55h). The DirectX driver releases a chunk only once no queued packet reads it. ERROR_INVALID_PARAMETER for an address that starts no chunk |
  | 3 | POOL_QUERY | | the cap in pages (0: no pool), the pages mapped, GPUVM_ENTRIES (three dwords) | |
  | other | | | | ERROR_NOT_SUPPORTED (50) |

- Chunks belong to the DirectX driver as a whole, not to the process that asked (its sections are shared by every process): a process ending frees nothing here. GPUVM_ENTRIES only grows; a released run is reused by the next chunk that fits.
- At System_Exit the mini-VDD clears GPUVM_BASE and GPUVM_ENTRIES with the SYSBUF registers.

## Saving and restoring

The mini-VDD keeps track of which VM's engine state the hardware holds: the system VM's at first, none after PRE_HIRES_TO_VGA, the new owner after POST_HIRES_TO_VGA, and the screen owner of a RESTORE_REGISTERS that changed it. RESTORE_REGISTERS, for the screen owner in ECX:

| Screen owner | What goes into the hardware |
|---|---|
| the VM whose state the hardware holds | nothing (the VDD also calls it when only the memory owner changes, e.g. for a DOS window) |
| message mode (the VDD's blue screens) | the engine off (CONTROL = 0), so the VGA text shows |
| the system VM, with its state saved since the display driver last set a mode | that state |
| the system VM, without such a state, the display driver registered | nothing: the driver sets its mode again (RestoreDesktopMode, [win98-display-driver.md](win98-display-driver.md)) |
| the system VM, no display driver | the engine off |
| any other VM | its state |

A state goes in register by register in index order, CONTROL last, then the VM's lock. The display driver also sets the desktop's mode itself when the desktop comes back, and repaints.

## Message mode

At startup the VDD builds the state of its message mode (the blue text screens) by setting a text mode through the BIOS in the system VM, virtually: nothing may reach the screen. Until SAVE_MESSAGE_MODE_STATE, the system VM's engine register accesses therefore go to its own copy, like those of a VM that does not own the screen, even when it owns it. (The DDK's Video7 and S3 samples hold their extension registers back the same way.)

## Sequencer virtualization

The VDD hands the mini-VDD every sequencer data access it traps. The mini-VDD takes:

| Index | Screen owner | Any other VM |
|---|---|---|
| E0h (debug register) | the hardware | the hardware: the debug log is shared |
| 80h-BFh | the hardware: the VM's own lock governs, as on a real card (for the system VM, only after SAVE_MESSAGE_MODE_STATE) | the VM's own copy (below) |
| anything else | not handled (the VDD's) | not handled |

The physical sequencer index is kept as it was in every case.

A VM that does not own the screen (a DOS session in a window, or in the background) sees the engine like this:

| Register | Reads | Writes |
|---|---|---|
| KEY (80h) | 01h if the VM has unlocked it, else 00h | sets the VM's lock (98h unlocks, anything else locks); the hardware lock is not touched |
| any other, while the VM's lock is locked | 00h | ignored, as on the hardware |
| ID, REVISION, FEATURES, FORMATS, MAX_WIDTH, VRAM_SIZE, LFB_BASE (81h-8Fh) | the hardware | ignored (read-only) |
| CONTROL (90h) | the VM's copy | the VM's copy, reserved bits cleared |
| FORMAT (91h) | the VM's copy | the VM's copy; values other than 8, 15, 16 and 32 are ignored, as on the hardware |
| WIDTH through BANK (92h-9Fh), SCRATCH (A2h-A3h) | the VM's copy | the VM's copy |
| EDID_INDEX, EDID_DATA (A0h-A1h) | the hardware | the hardware |
| reserved (A4h-BFh) | 00h | ignored |

So a DOS program in a window that sets a mode through the BIOS (which turns the engine off first) cannot turn the desktop off. It still detects the card, reads its memory size and EDID, and gets its own mode registers back. If the session goes full screen, RESTORE_REGISTERS puts its copy into the hardware; if a full-screen session goes back to a window, SAVE_REGISTERS takes the hardware's state as its copy.

A new VM's copy is all zeros: locked, engine off.

## Debug output

Lines on the card's debug register (sequencer E0h, written only while it reads 01h): when the mini-VDD loads or is refused, when it hands the screen to VGA, and when it refuses a screen switch.

## Testing

`vxdtest` loads vgpu98.vxd with its own LE loader under Unicorn (flat 32-bit memory, the fixups applied), answers the services with stubs that record their calls, and wires the ports to the display engine model from `biostest`. It checks the DDB, initialization (the dispatch table entries, the per-VM area, the pseudo-VM IDs, no card, a VDD with too short a table), the virtualization for a windowed VM (its lock, identity and EDID from the hardware, its own mode registers, CONTROL and FORMAT rules, the hardware lock and index untouched, the desktop's engine left on), direct access for the screen owner, the system VM held back until SAVE_MESSAGE_MODE_STATE, every row of the restore table (CONTROL last), PRE_HIRES_TO_VGA and POST_HIRES_TO_VGA in any order with saves and restores, Destroy_VM, SAVE_REGISTERS only for the VM the hardware holds and never into a pseudo-VM, PRE_HIRES_TO_VGA, CHECK_HIRES_MODE, CHECK_SCREEN_SWITCH_OK, GET_TOTAL_VRAM_SIZE and register preservation; and the ring buffer: the registers mapped, the SYSBUF registers and STATUS_BASE cleared first (size first), `_PageAllocate`'s arguments (the ring's pages and one) and the balanced stack, 2 MB, 1 MB when 2 MB cannot be had, none without memory or without SYSRING, the status page after the ring, the size written last, the debug lines, and System_Exit clearing them all; and the pool: the page table at boot (its size from the physical memory, contiguous above the first megabyte, GPUVM_BASE naming it, GPUVM_ENTRIES 0), no table for a GPU without GPUVM, DeviceIoControl's open and close, the codes and their refusals (the cap, short buffers, unknown and unaligned chunks, a chunk whose pages cannot be found, freed again, a chunk Windows has no pages for), each refusal's step in the output, chunks of pages not in order mapped with VALID and nothing else, a chunk with a page in the adapter area or a page not present refused and one with a page of conventional memory mapped, GPUVM_ENTRIES raised after the entries, a released run reused first fit, and System_Exit clearing the table's registers. What it cannot check is Windows: the stubs implement the VMM and the VDD as this page describes them.

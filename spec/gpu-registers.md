# GPU registers (interface 3.6)

**Versions:** interface 2.1 adds the command ring in main memory; interface 2.2 the status block in main memory. Interface 3.0 comes with command set 2.0 and its vertex pipeline, and adds main memory the GPU reads through its own page table. Interface 3.1 adds PROFILE.FIXED8, command set 2.1's DirectX 8 fixed function. Interface 3.2 adds PROFILE.PROGRAMS, command set 2.2's vertex and pixel programs. Interface 3.3 adds PROFILE.DOWNLOAD, command set 2.3's DOWNLOAD, the GPU's first write to main memory through its page table. Command set 2.4 adds PROFILE.GLIDE, the Glide device's commands, with the interface still 3.3. Command set 2.5 adds PROFILE.OGL, the OpenGL device's commands, with the interface still 3.3. Command set 2.6 adds to the OpenGL device what the ICD's extensions need (SWAP's swap interval, a second texture unit, partial texture updates, point sizes by distance, index textures and their palettes with opcode 5Eh PALETTE), with no PROFILE bit of its own and the interface still 3.3. Command set 2.7 adds TEXPARAM's clamp to edge to the OpenGL device, likewise without a PROFILE bit and with the interface still 3.3. Interface 3.4 adds PROFILE.CURSOR and the hardware cursor's three registers. Interface 3.5 adds PROFILE.GAMMA and the gamma table's two registers. Interface 3.6 adds PROFILE.VSYNC and VSYNC_CONTROL, the V-sync overrides; with it the OpenGL SWAP packet's OVERRIDE flag, with the command set still 2.7 ([gpu-commands.md](gpu-commands.md)). The drivers check only the major version: a driver for 2.x does not run on 3.0, nor the reverse; a driver for 3.0 runs on 3.1, which adds only what it may ignore.

BAR0's register window holds the GPU's control registers: a command ring in video memory, a doorbell, fences, and the display timing a DirectDraw driver needs. The window's first four registers are unchanged from interface 1.0 ([registers.md](registers.md)). The commands themselves are in [gpu-commands.md](gpu-commands.md); how the GPU keeps video memory up to date is in [gpu-coherency.md](gpu-coherency.md).

Nothing in the guest knows how the GPU is implemented: it sees a PCI card with registers and video memory. In 86Box the command processor runs on the host and draws through Vulkan.

## Access rules

- All registers are 32 bits, little-endian. Reads of any width are allowed, as in interface 1.0. Writes must be 32-bit and aligned; byte and word writes are ignored.
- Offsets not listed read 0 and ignore writes.
- The registers are memory-mapped only; there are no I/O ports and nothing in the VGA register space changes. DOS programs never see them.

## Register map

| Offset | Name | Access | Contents |
|---|---|---|---|
| 00h | ID | R | 38394756h, the bytes `VG98` (unchanged) |
| 04h | HW_VERSION | R | 00030006h: interface 3.6 |
| 08h | PROFILE | R | What the GPU can do (below). 0 means VGA and display engine only |
| 0Ch | VRAM_SIZE | R | Video memory in bytes (unchanged) |
| 10h | COMMAND_SET | R | 00020007h: command set 2.7 ([gpu-commands.md](gpu-commands.md)). Major version in bits 31:16, minor in 15:0. 2.1's additions need PROFILE.FIXED8 as well, 2.2's PROFILE.PROGRAMS, 2.3's (DOWNLOAD) PROFILE.DOWNLOAD, 2.4's (the Glide commands) PROFILE.GLIDE, 2.5's (the OpenGL commands), 2.6's (the OpenGL device's additions for the extensions) and 2.7's (TEXPARAM's clamp to edge) PROFILE.OGL: without them (the settings "DirectX 8 interface", "DirectX 8 shaders" or "Locks through main memory" Off, or no Glide device, or no OpenGL device) the register still reads 2.7, and the GPU takes the older command set's values only. The OpenGL ICD reads it when it finds the card, and offers each extension only from the command set that draws it: seven extensions from 2.6, the two edge-clamp extensions from 2.7 ([opengl.md](opengl.md), The extensions) |
| 20h | RING_BASE | R/W | Where the command ring is. Bit 0 (SYSMEM) clear: its offset in video memory, a multiple of 4096. Bit 0 set (interface 2.1, PROFILE.SYSRING): bits 31:12 are its physical address in main memory, bits 11:1 zero |
| 24h | RING_SIZE | R/W | The ring's size in bytes: a power of two from 4096 to 1 MB in video memory, to 4 MB in main memory |
| 28h | RING_HEAD | R | Byte offset in the ring of the next dword the GPU will read |
| 2Ch | RING_TAIL | R/W | Byte offset in the ring just past the last dword the driver has written. Writing it is the doorbell |
| 30h | CONTROL | R/W | Bit 0 RUN, bit 1 RESET, bit 2 HOLD_DRIVER, bit 3 HOLD_VDD, bits 8 and 9 their write enables (below) |
| 34h | STATUS | R | Bit 0 BUSY, bit 1 ERROR, bit 2 RUNNING, bit 3 FLIP_PENDING, bit 4 HELD (below) |
| 38h | ERROR_OFFSET | R | Ring offset of the packet that stopped the GPU |
| 3Ch | ERROR_CODE | R | Why it stopped (below); 0 when STATUS.ERROR is clear |
| 40h | FENCE | R/W | The value of the last FENCE packet completed. Writable only while RUN is 0 (below) |
| 48h | VBLANK_COUNT | R | Counts the starts of vertical blank, wrapping at 2^32 |
| 4Ch | SCANLINE | R | Bits 15:0: the line being scanned out, 0 at the first visible line, counting on through the blanking lines. Bit 31: in vertical blank |
| 50h | DEBUG | R/W | Write: a character for the host's diagnostic log. Read: bit 0 while the log listens (not at Diagnostics "Minimal", "Glide", "OpenGL" or "Display"), bits 1-4 and 6 with the audits and the shader capture, bit 5 at those four, bit 7 at "Glide", bit 8 at "OpenGL", else 0 ([registers.md](registers.md)) |
| 54h | SYSBUF_PHYS | R/W | Interface 2.1: the physical address of the mini-VDD's ring buffer in main memory (below) |
| 58h | SYSBUF_LINEAR | R/W | Interface 2.1: its linear address in Windows' system arena |
| 5Ch | SYSBUF_SIZE | R/W | Interface 2.1: its size in bytes; 0 while there is none. Written last |
| 60h | SYSBUF_STATUS | R/W | Interface 2.2: the physical address of the mini-VDD's status page, the page after the ring buffer; 0 for none |
| 64h | STATUS_BASE | R/W | Interface 2.2: the physical address of the status block (below), 16-byte aligned, in guest RAM above the first megabyte; anything else reads back 0, off. Writing it writes the block at once |
| 68h | GPUVM_BASE | R/W | Interface 3.0: the physical address of the GPU's page table (below), a multiple of 4 KB in guest RAM above the first megabyte (its first entry at least: each entry is checked again when read); anything else reads back 0, no table |
| 6Ch | GPUVM_ENTRIES | R/W | Interface 3.0: the page table's number of entries, 0-65536 (the GPU's address space is 4 KB per entry, up to 256 MB); larger values read back 65536 |
| 70h | CURSOR_CONTROL | R/W | Interface 3.4: bit 0 ENABLE, the hardware cursor shown ([below](#the-hardware-cursor-interface-34)); other bits read 0 |
| 74h | CURSOR_POS | R/W | Interface 3.4: the cursor pattern's top-left corner on the screen, in the mode's pixels: x in bits 15:0, y in bits 31:16, each signed |
| 78h | CURSOR_BASE | R/W | Interface 3.4: the pattern's offset in video memory; bits 9:0 read 0 |
| 7Ch | GAMMA_CONTROL | R/W | Interface 3.5: bit 0 ENABLE, the gamma table applied ([below](#the-gamma-table-interface-35)); other bits read 0 |
| 80h | GAMMA_BASE | R/W | Interface 3.5: the table's offset in video memory; bits 9:0 read 0 |
| 84h | VSYNC_CONTROL | R/W | Interface 3.6: the V-sync overrides, bits 1:0 Direct3D, 3:2 OpenGL, 5:4 Glide ([below](#the-v-sync-overrides-interface-36)); other bits read 0 |

### PROFILE

| Bit | Name | Meaning |
|---|---|---|
| 0 | COMMANDS | The command processor exists: ring, fences, NOP, FENCE, SYNC, DISCARD, FLIP |
| 1 | BLIT | The 2D packets (FILL, BLT) |
| 2 | RENDER3D | The 3D packets |
| 3 | SYSRING | Interface 2.1: the GPU can read its ring in main memory (RING_BASE.SYSMEM) |
| 4 | STATUSWB | Interface 2.2: the GPU writes its status block in main memory (STATUS_BASE) |
| 5 | GPUVM | Interface 3.0: the GPU reads main memory through its page table (GPUVM_BASE): vertex and index buffers, UPLOAD sources |
| 6 | VERTEX | Interface 3.0: command set 2.0's vertex pipeline (declarations, streams, transform and lighting). It always comes with RENDER3D: command set 2.0 has no draw without it |
| 7 | FIXED8 | Interface 3.1: command set 2.1's DirectX 8 fixed function (the vertex element types and uses, indexed vertex blending, tweening, point sizes and sprites, blend operations, colour write masks, the texture operations MULTIPLYADD and LERP with their third argument, the TEMP register, volume textures, the formats [gpu-formats.md](gpu-formats.md) marks 2.1). It always comes with VERTEX. A GPU without it refuses every value command set 2.1 adds (ERROR_CODE 5), so it is exactly a command set 2.0 GPU |
| 8 | PROGRAMS | Interface 3.2: command set 2.2's vertex and pixel programs (PROGRAM, BIND, CONSTANTS, DECLARE's INPUT use) and its formats V16U16, G16R16 and A2W10V10U10 ([gpu-formats.md](gpu-formats.md)). It always comes with FIXED8. A GPU without it refuses them (ERROR_CODE 5), so it is exactly a command set 2.1 GPU |
| 9 | DOWNLOAD | Interface 3.3: command set 2.3's DOWNLOAD, which writes a surface's rectangle into main memory through the page table (it needs BLIT and GPUVM too). In 86Box it follows the setting "Locks through main memory" ([registers.md](registers.md)). A GPU without it refuses DOWNLOAD (ERROR_CODE 5), and the DirectX driver Locks surfaces through the frame buffer ([directx-hal.md](directx-hal.md)) |
| 10 | GLIDE | Command set 2.4: the Glide device, opcodes 40h-4Fh, for the Glide DLLs ([glide.md](glide.md)). It always comes with RENDER3D. A GPU without it refuses them (ERROR_CODE 5), so it is exactly a command set 2.3 GPU, and the Glide DLLs refuse every window |
| 11 | OGL | Command set 2.5, with 2.6's and 2.7's additions: the OpenGL device, opcodes 50h-5Fh, for the OpenGL ICD ([opengl.md](opengl.md)). It always comes with RENDER3D. A GPU without it refuses them (ERROR_CODE 5), so it is exactly a command set 2.4 GPU; the display driver then names no ICD (OPENGL_GETINFO), and Windows draws OpenGL in software |
| 12 | CURSOR | Interface 3.4: the hardware cursor ([below](#the-hardware-cursor-interface-34)). Without it CURSOR_CONTROL, CURSOR_POS and CURSOR_BASE read 0 and ignore writes, and the display driver keeps the DIB engine's software cursor |
| 13 | GAMMA | Interface 3.5: the gamma table ([below](#the-gamma-table-interface-35)). Without it GAMMA_CONTROL and GAMMA_BASE read 0 and ignore writes, the display driver announces no gamma ramp, and the control program's VGPU98 Color tab says so |
| 14 | VSYNC | Interface 3.6: the V-sync overrides ([below](#the-v-sync-overrides-interface-36)). Without it VSYNC_CONTROL reads 0 and ignores writes, every program's own wait is kept, the ICD sets no OVERRIDE flag, and the VGPU98 3D tab says the overrides are off |

The device sets the bits it can honour when the machine starts. In 86Box, COMMANDS, BLIT, SYSRING, STATUSWB and GPUVM always (the command processor and the 2D packets run on the host's CPU, and it reads and writes guest RAM directly), DOWNLOAD unless the setting "Locks through main memory" is Off, and RENDER3D and VERTEX when Vulkan starts on the host with the features the vertex pipeline needs, FIXED8 with VERTEX unless the setting "DirectX 8 interface" is Off, PROGRAMS with FIXED8 unless the setting "DirectX 8 shaders" is Off ([registers.md](registers.md)), and GLIDE with RENDER3D when the Glide device starts on the same Vulkan device (it needs 16-bit 5:6:5 colour images that blend and 16-bit depth images; without them, or when its shaders and pipeline objects cannot be made, it says so in the log and the profile lacks GLIDE), and OGL with RENDER3D when the OpenGL device starts on the renderer's Vulkan device too. The OpenGL device needs 5:6:5 and 8:8:8:8 colour images that blend, 16-bit depth images, and 24-bit depth with 8-bit stencil (D24S8) or, failing it, 32-bit float depth with 8-bit stencil (D32F-S8, its depth then kept on the 24-bit grid), each usable as a copy's source and destination; it uses Vulkan's `logicOp` where the host has it (GL's logic op is ignored where not). Without them, or when its shaders, pipeline objects or texel arena cannot be made, the profile lacks OGL and the start-up line names no OpenGL command set (2.6) ([opengl.md](opengl.md), The OpenGL device). CURSOR is there unless the setting "Cursor" is Software, whether or not Vulkan starts: the cursor is shown by whatever draws the frame. GAMMA likewise unless the setting "Gamma" is Off, and VSYNC unless the setting "V-sync overrides" is Off. The full profile is 7FFFh. With PROFILE = 0 the card behaves exactly as in interface 1.0, and the driver offers no acceleration; without RENDER3D it offers DirectDraw but not Direct3D. Windows always runs, through its own software paths for what the card does not offer.

### CONTROL

| Bit | Name | Meaning |
|---|---|---|
| 0 | RUN | 1: the GPU executes packets from RING_HEAD while RING_HEAD differs from RING_TAIL. 0: it stops after the packet in progress |
| 1 | RESET | Writing 1 stops the GPU (RUN becomes 0), sets RING_HEAD and RING_TAIL to 0, clears STATUS.ERROR and ERROR_CODE and FLIP_PENDING, and drops everything the GPU holds outside video memory: its copies of surfaces, its copies of buffers in main memory, the current state (back to its defaults: the declaration, streams, index buffer, matrices, lights, material and clip planes included), the inline buffer. Everything not yet copied back to video memory is lost, from discarded packets and finished ones alike; video memory keeps what it held. FENCE, GPUVM_BASE and GPUVM_ENTRIES are left as they were. Reads 0 |
| 2 | HOLD_DRIVER | Set by the display driver while it changes modes. While set, RUN reads 0 and writing RUN = 1 is ignored; setting it clears RUN |
| 3 | HOLD_VDD | Set by the mini-VDD while a VGA mode has the screen (a full-screen DOS session, the system's blue-screen messages). Same effect |
| 8 | HOLD_DRIVER_WE | Write only, reads 0. A write changes HOLD_DRIVER only when this bit is set |
| 9 | HOLD_VDD_WE | Write only, reads 0. A write changes HOLD_VDD only when this bit is set |

The write enables let each owner change its own hold bit without reading CONTROL first: a read-modify-write could lose a hold bit the other owner set in between. RUN and RESET act on every write.

RING_BASE, RING_SIZE and FENCE can be written only while RUN is 0. Setting RUN with a ring that is not a power of two of at least 4 KB, at a multiple of 4 KB, wholly inside video memory and at most 1 MB, or with SYSMEM, wholly inside main memory above the first megabyte and at most 4 MB, is an error (ERROR_CODE 3). After RESET, the driver writes FENCE with the last value it issued (so every wait on a packet that RESET discarded ends), programs the ring, then sets RUN. Each hold bit is set and cleared only by its owner; the GPU can run only when both are clear.

### STATUS

| Bit | Name | Meaning |
|---|---|---|
| 0 | BUSY | The GPU is running and still has work: packets waiting, packets executing (copies back to video memory included, the end-of-batch copy of the visible frame too), or a FLIP waiting for vertical blank. 0 whenever RUNNING is 0 |
| 1 | ERROR | A packet was rejected. The GPU has stopped; only RESET restarts it |
| 2 | RUNNING | CONTROL.RUN is 1 and the GPU has not stopped on an error |
| 3 | FLIP_PENDING | A FLIP has been read and has not yet taken effect |
| 4 | HELD | HOLD_DRIVER or HOLD_VDD is set |

**Every wait checks ERROR, RUNNING and HELD.** A driver waiting on a fence or ring space stops waiting when one of them says the GPU will not finish.
- **HELD:** it does not restart the GPU; DirectDraw calls fail with DDERR_SURFACELOST until the holder lets go.
- **ERROR, or RUNNING clear while not held:** it recovers. RESET, FENCE written with the last value issued, ring reprogrammed, each context's full state sent again. What the GPU had not copied back to video memory is lost; nothing in video memory is.
- **No progress:** a wait also gives up, and recovers, when neither FENCE nor RING_HEAD has moved for a second. A GPU that is slow but still advancing is never reset.

### ERROR_CODE

| Value | Meaning |
|---|---|
| 1 | Unknown opcode |
| 2 | Bad length: shorter than the opcode needs, or longer than the ring allows |
| 3 | Bad argument: a surface outside video memory, an unknown format or state id, an enumerated value with no meaning |
| 4 | The packet runs past the end of the ring |
| 5 | The opcode needs a PROFILE bit the device does not have |
| 6 | RING_TAIL outside the ring or not a multiple of 4 |
| 7 | A GPU address the page table does not map: past GPUVM_ENTRIES, through an entry without VALID, or with no table |
| 8 | A page table entry outside guest RAM, or naming a page outside guest RAM or in the adapter area (A0000h-FFFFFh) |

Errors are for packets no correct driver sends. Command set 2.4's Glide commands (40h-4Fh) are never errors beyond their header: the ring checks a Glide packet's opcode, length (its first dword at least: ERROR_CODE 2), place in the ring (4) and PROFILE bits (5), and the Glide device drops and counts a command it cannot take, the GPU going on ([gpu-commands.md](gpu-commands.md), Command set 2.4). Command set 2.5's OpenGL commands (50h-5Fh) likewise: the ring checks their header (their first dword at least, inside the ring, PROFILE's RENDER3D and OGL), and the OpenGL device drops and counts a command it cannot take, the GPU going on ([gpu-commands.md](gpu-commands.md), Command set 2.5). With Diagnostics on ([registers.md](registers.md)), 86Box names a rejected packet in its log: the packet's name, opcode and length; for a list, the item that failed (a STATE pair as `state id = value (pair n of m)`, a DECLARE element as `element n: d0 d1`); otherwise its first argument by name (TRANSFORM's `which`, LIGHT's `slot`, and so on); then its first four dwords. A packet refused before it was read (ERROR_CODE 1, 2, 4 or 5) is named by its header dword. Rejections of one kind (the error, the packet and its first argument or failing item) are logged in full the first three times, and again after 10 seconds without one; the others are counted in the summary's errors line (up to eight kinds at once), and the resets that follow them, unlogged too, in its first line. Numeric values that Direct3D programs pass through the runtime (an ALPHAREF above 255, a ZBIAS above 16) are reduced by the GPU itself, as [gpu-commands.md](gpu-commands.md) defines for each state; the driver maps every enumerated value Direct3D defines onto one the GPU accepts ([directx-hal.md](directx-hal.md)).

## The ring

- The ring is RING_SIZE bytes starting at RING_BASE, in video memory or (interface 2.1) in main memory. RING_HEAD and RING_TAIL are byte offsets within it, multiples of 4. The ring is empty when they are equal, so it holds at most RING_SIZE − 4 bytes of packets.
- The driver writes packets at RING_TAIL, through the framebuffer aperture (BAR1) or into main memory, then writes the new RING_TAIL. Everything before the new tail must already be written. The GPU reads only between RING_HEAD and RING_TAIL; the driver never writes there.
- A ring in main memory is read by the GPU like a bus-master card's command buffer: writing it costs the processor no bus cycles. The memory must stay where it is while the GPU may run: physically contiguous, fixed and locked.
- A packet never wraps around the end of the ring. When the next packet does not fit before the end, the driver fills the rest with NOP packets and continues at offset 0. (One NOP pads up to 256 KB.)
- RING_HEAD advances as packets are taken. Space the GPU has moved past is free again. Completion is not the same as being read: the driver learns that work is finished from FENCE (below).
- Packets execute in ring order. The GPU may overlap their work internally, but the result is always as if each finished before the next began.

### The ring buffer in main memory (interface 2.1)

Only the mini-VDD can allocate physically contiguous memory, and only while Windows starts; only the DirectX driver, the Glide DLLs and the OpenGL ICD write the ring. SYSBUF_PHYS, SYSBUF_LINEAR and SYSBUF_SIZE carry the buffer from one to the other, like a real card's scratch registers. The device only stores them, and clears them at a machine reset.

- **The mini-VDD at boot** clears them (a warm restart keeps the card's registers, and the last boot's buffer is gone), and clears them again at System_Exit, as Windows leaves. When PROFILE has SYSRING it allocates 2 MB (1 MB if 2 MB cannot be had) of contiguous, fixed, locked, zeroed pages in the system arena, above the first megabyte, and writes the physical address, the linear address and, last, the size.
- **The DirectX driver** uses the buffer when PROFILE has SYSRING, the physical address is a multiple of 4 KB above the first megabyte, the linear one is in the shared or system arena and the size is a power of two from 1 MB to 4 MB: it programs RING_BASE with the physical address and SYSMEM, and writes packets through the linear address. If the GPU refuses it at RUN, or without a buffer, the ring is 128 KB in video memory as in interface 2.0. The Glide DLLs and the OpenGL ICD, which drive the ring with the DirectX driver's code, choose by the same rule ([directx-hal.md](directx-hal.md), [opengl.md](opengl.md)).

### The status block in main memory (interface 2.2)

A bus-master card tells its driver how far it got by writing to main memory, so the driver polls memory instead of reading registers over the bus. With STATUS_BASE set, the GPU writes four dwords there:

| Offset | Contents |
|---|---|
| +0 | FENCE |
| +4 | RING_HEAD |
| +8 | STATUS |
| +12 | VBLANK_COUNT |

They are written whenever one of them changes: a packet taken, a FENCE completed, a flip queued or shown, an error, a RING_TAIL, CONTROL or FENCE write, a RESET, every vertical blank, and when STATUS_BASE is written. Each dword is current on its own; everything the GPU wrote before a FENCE value is visible before the value. The block is off after a machine reset, a GPU_RESET and a write of 0.

- **The mini-VDD** allocates the status page with the ring buffer (one page more) and names it in SYSBUF_STATUS; it clears SYSBUF_STATUS and STATUS_BASE with the other SYSBUF registers at boot and at System_Exit.
- **The DirectX driver** uses it when PROFILE has STATUSWB and SYSBUF_STATUS is SYSBUF_PHYS + SYSBUF_SIZE: it programs STATUS_BASE at its first DriverInit and at every ring start, and reads FENCE, RING_HEAD, STATUS and VBLANK_COUNT from the page's linear address (SYSBUF_LINEAR + SYSBUF_SIZE). If VBLANK_COUNT in the block has not moved for 100 ms (a reset turned the block off), it programs STATUS_BASE again. The Glide DLLs do the same, from their first window's open, and the OpenGL ICD from its finding of the card.
- **GPU_RESET** (the display engine's SR A8h, [display-engine.md](display-engine.md)): the VGA BIOS writes it at POST, so a GPU the last boot left running writes no memory of the next one, whatever the reset was; it puts the registers in their power-on state, the status block off first. The mini-VDD lets no VM write it.

### Main memory through the GPU's page table (interface 3.0)

A bus-master card reads its vertex and index buffers from main memory as it reads its command buffer, through a table of the pages they occupy (as AGP's address table does, over PCI). With PROFILE.GPUVM the GPU has its own address space: GPU address *a* is byte `a & FFFh` of the page entry `a >> 12` of the table names. STREAM, INDICES and UPLOAD packets read through it ([gpu-commands.md](gpu-commands.md)). The GPU writes through it only with DOWNLOAD (interface 3.3, PROFILE.DOWNLOAD), whose rows' pages are all checked before any byte is written; its destination is the DirectX driver's pool, data the guest's CPU never runs (86Box leaves its recompiler's page records alone).

| Entry bits | Contents |
|---|---|
| 0 | VALID: the entry maps a page |
| 11:1 | 0 |
| 31:12 | The page's physical address: any page of guest RAM but the adapter area (A0000h-FFFFFh, whose RAM is not what the processor sees there) |

- **GPUVM_BASE and GPUVM_ENTRIES** can be written at any time. The GPU reads an entry each time it uses an address through it, so pages can be mapped while it runs: whoever maps them writes the entries, then (when the table grew) GPUVM_ENTRIES. Each read of an entry checks that the entry itself lies in guest RAM (GPUVM_ENTRIES may have grown since GPUVM_BASE was checked). An entry must not be unmapped while a packet that reads through it is queued: the DirectX driver, which alone asks the mini-VDD to map and release memory (its chunks belong to the driver, not to a process, so none goes when a process ends), releases a chunk only once its fences say no queued packet reads it.
- A packet reading through an address the table does not map stops the GPU (ERROR_CODE 7); through an entry that lies outside guest RAM or names a page outside it or in the adapter area, ERROR_CODE 8. The table itself stays above the first megabyte (GPUVM_BASE).
- A machine reset and GPU_RESET clear both registers. RESET leaves them.
- **The mini-VDD** owns the table. At boot it allocates it (contiguous, fixed, locked, zeroed, in the system arena: 4 bytes per entry for a quarter of guest RAM, at most 65536 entries) and writes GPUVM_BASE (the table) and GPUVM_ENTRIES (0); it maps pages as the DirectX driver asks for memory, and clears both registers at System_Exit ([minivdd.md](minivdd.md)).
- **The DirectX driver** asks the mini-VDD for memory in chunks and gets their linear addresses (in the system arena, which every process sees) and GPU addresses ([directx-hal.md](directx-hal.md)).

## Fences

A FENCE packet carries a 32-bit value. When every packet before it has completed ([gpu-coherency.md](gpu-coherency.md)), FENCE takes the packet's value. The driver issues increasing values. It compares them modulo 2^32: a fence value v is complete when `(FENCE − v)` as a signed 32-bit number is 0 or more.

## Display timing

VBLANK_COUNT and SCANLINE come from the display engine's timing in every mode, VGA modes included. A DirectDraw driver uses them for WaitForVerticalBlank and GetScanLine. SCANLINE counts lines of the mode as programmed: with double scan, lines of the doubled output.

## Video memory the GPU reserves

The drivers keep the top 256 KB of video memory (from VRAM_SIZE − 40000h) for the GPU: a ring of 128 KB (when the ring is not in main memory) and the driver's own data, which is the hardware cursor's two patterns, 32 KB each, from 128 KB in ([win98-display-driver.md](win98-display-driver.md), The cursor), and the gamma table's two slots, 1 KB each, from 192 KB in ([win98-display-driver.md](win98-display-driver.md), Gamma). The display driver's modes must fit below it, and DirectDraw's heap ends below it. The device itself does not enforce this.

## When the GPU must be idle

The GPU's work must never land on a screen or in video memory that belongs to something else. Whoever stops it does the same three things: let the GPU finish (waiting while FENCE or RING_HEAD still advances, giving up after a second without progress), then write its hold bit together with RESET (so nothing can restart it; it copies back what it held newer and forgets every host copy), and clear the hold bit when the screen is back. Whoever uses the GPU next finds RUN clear and reprograms the ring ([directx-hal.md](directx-hal.md)).

| When | Who | Hold bit |
|---|---|---|
| A mode change: the display driver's Enable, ReEnable, Disable | `vgpu98.drv`, under the ring lock, from before the change until after it | HOLD_DRIVER |
| A VGA mode takes the screen: a full-screen DOS session, or the VDD's blue-screen message mode | `vgpu98.vxd`, at PRE_HIRES_TO_VGA; cleared when the system VM's state comes back (RESTORE_REGISTERS for the system VM) | HOLD_VDD |

While either bit is set, DirectDraw calls fail with DDERR_SURFACELOST, as on any card while a DOS session has the screen, and DirectDraw restores its surfaces afterwards. DOS programs never touch BAR0, but they can write anywhere in video memory, the reserved top included; VGA-mode writes are not tracked by the device either ([gpu-coherency.md](gpu-coherency.md)). The reset before the VGA mode makes both harmless: the GPU holds no copies, so it reads video memory afresh, and the driver reprograms the ring.

## The hardware cursor (interface 3.4)

With PROFILE.CURSOR the card shows a cursor of up to 64x64 pixels at scan-out, over the display engine's frame, so it is never in video memory.

- **Taking the registers.** Writes to CURSOR_CONTROL, CURSOR_POS and CURSOR_BASE take effect at the start of the next vertical blank, as START does. The pattern is read from video memory at the vertical blank after CURSOR_BASE or CURSOR_CONTROL is written, after the cursor is shown again, or when the mode's bits a pixel change. A pattern changed in video memory without one of those is not seen.
- **The pattern**, at CURSOR_BASE: 64 rows of the AND mask, 8 bytes a row, the left pixel in bit 7 of its byte (512 bytes); then at +200h 64 rows of 64 pixels of the image, in the mode's bytes a pixel (1 at 8 bpp, 2 at 15 and 16, 4 at 32). A pattern that would pass the end of video memory is not shown.
- **The rule.** Each pixel of the frame under the pattern becomes `(pixel AND mask) XOR image`, the mask's bit widened to the whole pixel, applied to the pixel's value in video memory before it is turned into a colour: at 8 bpp a palette index, then through the DAC. It is the rule of Windows' own cursors, so the card's cursor gives exactly the pixels the DIB engine's would. The corner may lie off the screen; the part on it is shown.
- **When it is shown:** with ENABLE set, while the display engine is on, and not over a Glide session's screen ([glide.md](glide.md), Showing Glide), as a Voodoo3's Glide turns its own cursor off. Never in VGA modes.
- **Resets.** A machine reset and POST's GPU_RESET clear the three registers (the cursor goes at the next vertical blank). The GPU's own RESET (CONTROL) does not touch them.

## The gamma table (interface 3.5)

With PROFILE.GAMMA the card applies a gamma table at scan-out, as a DAC of 1999 did.

- **The table**, at GAMMA_BASE (1 KB aligned): 256 dwords, entry *i* being `R << 16 | G << 8 | B`, the output for a channel's 8-bit level *i*.
- **Taking the registers.** Writes to GAMMA_CONTROL and GAMMA_BASE take effect at the start of the next vertical blank, and the table is read from video memory then, so no frame shows half a table. A table changed in video memory without a register write is not seen. The display driver keeps two tables and names the other one at each change ([win98-display-driver.md](win98-display-driver.md), Gamma).
- **Where it applies.** In the display engine's direct colour modes (15, 16 and 32 bpp), to each pixel once it is turned into 8 bits a channel, last of all: after the hardware cursor is laid over the frame, as a DAC follows the cursor. Not at 8 bpp (the palette is the DAC there), not in VGA modes, and not over a Glide session's screen, where the session's own table rules, as on a Voodoo3 ([glide.md](glide.md)). A blanked screen stays black.
- **In 86Box:** the display applies it as it composes the frame, and composes every row again when the table changes. When 86Box rebuilds the frame itself, its line rebuild applies it to the lines its renderer drew, so no line goes through the table twice.
- **Resets.** A machine reset and POST's GPU_RESET clear both registers. The GPU's own RESET does not touch them.
- **The off switch.** 86Box's setting "Gamma" Off takes PROFILE.GAMMA away ([registers.md](registers.md)).

## The V-sync overrides (interface 3.6)

With PROFILE.VSYNC the user can override each API's wait for the vertical blank: the control program's VGPU98 3D tab writes VSYNC_CONTROL ([driver-package.md](driver-package.md)).

| Bits | API | Where it applies |
|---|---|---|
| 1:0 | Direct3D (and DirectDraw) | FLIP ([gpu-commands.md](gpu-commands.md)): its IMMEDIATE flag and interval |
| 3:2 | OpenGL | The OpenGL device's SWAP, only on a SHOW with the OVERRIDE flag ([gpu-commands.md](gpu-commands.md), Command set 2.5) |
| 5:4 | Glide | The Glide device's SWAP: `grBufferSwap`'s interval |

- **Each field:** 0 the program's choice (the default), 1 always on, 2 always off; 3 reads back as written but acts as 0.
- **The rule** (`vgpu98_vsync_interval` in `card/vid_vgpu98_dispdiag.h`, shared with the host test): given the field and the interval the program asked for (0 for no wait), the program's choice keeps it, always on makes 0 into 1 and keeps a longer interval, always off gives 0. A FLIP's IMMEDIATE is an asked interval of 0, and a result of 0 is IMMEDIATE.
- **Nothing else waits differently:** the desktop, windowed blits, DOS, and programs that wait for the vertical blank themselves are untouched, as with any driver of the era. An immediate flip never tears on this card: the new START is taken at the next vertical blank, so "off" only stops the program from waiting.
- **Taking the register.** A write takes effect at the next flip or swap, and reads back as written.
- **Resets.** A machine reset and POST's GPU_RESET clear it. The GPU's own resets (every mode change holds and resets the GPU) do not.
- **The off switch.** 86Box's setting "V-sync overrides" Off takes PROFILE.VSYNC away ([registers.md](registers.md)).
- **The log:** a line at each change of the register's value, and per API, the first flip or swap whose wait an override changed after it, with what the program asked.

## The display (in 86Box)

While the display engine is on, 86Box composes its frame on the host GPU rather than rebuilding it line by line: a display thread keeps a mirror of video memory there (only changed pages are uploaded), takes the GPU's newer pixels from a layer the renderer copies them into on the host GPU, converts the frame exactly as 86Box's own renderers would, and reads back the rows that changed; at the next vertical blank the device puts them into 86Box's frame buffer, and 86Box blits them to its window as it does every card's frame. The guest sees no difference ([gpu-coherency.md](gpu-coherency.md)); the VGA modes are shown by 86Box's VGA core as always. While a Glide session shows, the display composes its frame from the session's front buffer instead of video memory, until the session ends ([glide.md](glide.md), Showing Glide). OpenGL's frames need nothing of the display: the OpenGL device writes them into video memory, into the window's visible rectangles ([opengl.md](opengl.md), SwapBuffers). The hardware cursor is laid over the frame as it is composed; when 86Box rebuilds the frame itself (no Vulkan, or the display's failure), its line rebuild lays it over the lines the cursor covers, with its own conversions.

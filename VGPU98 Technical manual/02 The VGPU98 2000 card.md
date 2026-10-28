# 02 The VGPU98 2000 card

The card as the guest sees it: its PCI identity, its memory windows, the registers in BAR0, the feature bits drivers check, and the settings 86Box offers for it. The full register reference is [`spec/registers.md`](../spec/registers.md) (PCI, VGA and the debug register), [`spec/gpu-registers.md`](../spec/gpu-registers.md) (BAR0) and [`spec/pci-config.md`](../spec/pci-config.md). The display engine's registers are in [05](05%20The%20display%20path.md).

## Identity

| Field | Value |
|---|---|
| Name in 86Box | VGPU98 2000 (PCI), under Display in the machine settings |
| Internal name | `vgpu98_pci`. 86Box's machine files store it, so it never changes |
| PCI vendor and device | 9898:2000, revision 01 |
| Subsystem | 9898:2000 |
| Class | 030000: a VGA-compatible display controller |
| Interrupt | Pin INTA is declared, but the card never raises it: drivers poll the fence and status registers |
| ROM | 32 KB, the file `roms/video/vgpu98/vgpu98.bin` beside 86Box's other ROMs ([03](03%20The%20video%20BIOS%20%28ROM%29.md)) |
| Video memory | 8, 16, 32, 64 or 128 MB; 128 by default |

The card's identity, sizes and register numbers all come from `card/vgpu98.card`. `cardc` compiles it into 86Box's `vid_vgpu98_card.h` and the BIOS's includes, so the two sides cannot disagree.

## The PCI windows

```
 PCI configuration space            what the guest maps
 ------------------------           ------------------------------------
 BAR0  64 KB, memory,       ----->  registers: identity, versions,
       non-prefetchable             PROFILE, the ring, CONTROL, STATUS,
                                    fences, vblank, cursor, gamma, V-sync
 BAR1  128 MB, memory,      ----->  the frame buffer aperture: offset N
       prefetchable                 is video memory byte N; beyond the
                                    installed memory reads FFh
 ROM   32 KB                ----->  the VGA/VBE BIOS, copied to C0000h
 legacy VGA (always)        ----->  ports 3B0h-3DFh, memory A0000h-BFFFFh
```

BAR1 is 128 MB whatever the memory size, so the aperture never moves when the memory setting changes. The legacy VGA ports and memory are 86Box's own VGA core (`svga`); the card adds its display engine registers behind the sequencer ([05](05%20The%20display%20path.md)).

## BAR0's registers

| Offset | Register | What it is for |
|---|---|---|
| 00h | ID | "VG98" (38394756h): the card's signature |
| 04h | HW_VERSION | The interface version: 00030006h, interface 3.6 |
| 08h | PROFILE | The feature bits (below) |
| 0Ch | VRAM_SIZE | Video memory, in bytes |
| 10h | COMMAND_SET | The packet language: 00020007h, command set 2.7 |
| 20h-2Ch | RING_BASE, RING_SIZE, RING_HEAD, RING_TAIL | The command ring: where it is (video memory, or guest RAM with bit 0 of RING_BASE), its size, where the GPU reads and where the guest has written to |
| 30h | CONTROL | RUN, RESET, and the hold bits HOLD_DRIVER and HOLD_VDD |
| 34h | STATUS | BUSY, ERROR, RUNNING, FLIP_PENDING, HELD |
| 38h, 3Ch | ERROR_OFFSET, ERROR_CODE | Where in the ring the GPU stopped, and why |
| 40h | FENCE | The value of the last fence the GPU reached |
| 48h, 4Ch | VBLANK_COUNT, SCANLINE | Vertical blanks counted; the scan line, bit 31 set in vertical blank |
| 50h | DEBUG | Read: which diagnostics the guest drivers should write. Written: the 32-bit DLLs' log text, one character at a time |
| 54h-60h | SYSBUF_PHYS, SYSBUF_LINEAR, SYSBUF_SIZE, SYSBUF_STATUS | The ring's buffer in guest RAM, which the mini-VDD allocates |
| 64h | STATUS_BASE | Where the card writes status back into guest RAM |
| 68h, 6Ch | GPUVM_BASE, GPUVM_ENTRIES | The GPU's page table for the pool (up to 65536 entries) |
| 70h-78h | CURSOR_CONTROL, CURSOR_POS, CURSOR_BASE | The hardware cursor, taken at vertical blank |
| 7Ch, 80h | GAMMA_CONTROL, GAMMA_BASE | The gamma table: 256 entries in the reserve, taken at vertical blank |
| 84h | VSYNC_CONTROL | The V-sync overrides: two bits each for Direct3D (1:0), OpenGL (3:2) and Glide (5:4); 0 is the program's choice, 1 always on, 2 always off |

## PROFILE: what the card offers

A driver uses a feature only when its PROFILE bit is set. With 3D available and every setting at its default, PROFILE reads 00007FFFh.

| Bit | Name | What it allows | Needs | Cleared by |
|---|---|---|---|---|
| 0 | COMMANDS | The command ring | | |
| 1 | BLIT | 2D packets: FILL and BLT | | |
| 2 | RENDER3D | 3D packets | A usable Vulkan 1.1 host GPU | No host GPU |
| 3 | SYSRING | The ring in guest RAM | | |
| 4 | STATUSWB | Status written back into guest RAM | | |
| 5 | GPUVM | The page table and the pool | | |
| 6 | VERTEX | The vertex pipeline: declarations, streams, T&L | Command set 2.0 | No host GPU |
| 7 | FIXED8 | DirectX 8's fixed function | Command set 2.1 | "DirectX 8 interface" Off |
| 8 | PROGRAMS | Shader programs | Command set 2.2, FIXED8 | "DirectX 8 shaders" Off |
| 9 | DOWNLOAD | Copies from video memory into guest RAM | Command set 2.3 | "Locks through main memory" Off |
| 10 | GLIDE | The Glide device | Command set 2.4, a host GPU | No host GPU |
| 11 | OGL | The OpenGL device | Command sets 2.5-2.7, a host GPU | No host GPU |
| 12 | CURSOR | The hardware cursor | Interface 3.4 | "Cursor" Software |
| 13 | GAMMA | The gamma table | Interface 3.5 | "Gamma" Off |
| 14 | VSYNC | The V-sync overrides | Interface 3.6 | "V-sync overrides" Off |

**Why this matters.** Turning a setting off clears its bit, and every driver then behaves exactly as it would on a card without the feature: the DirectX DLL becomes a DirectX 7 driver without FIXED8, without PROGRAMS vertex shaders go to the runtime's own vertex processing and no pixel shaders are offered, the display driver uses the DIB engine's software cursor without CURSOR. That is how each feature can be ruled in or out when something looks wrong.

## What the version numbers mean to drivers

The drivers check the numbers before using a feature, as they would on real hardware.

| Number | What it adds |
|---|---|
| Interface 2.x | The ring, CONTROL and its holds, STATUS, errors, fences, vertical blank; the ring in guest RAM; status written back |
| Interface 3.0 | The GPU's page table and the pool |
| Interfaces 3.4, 3.5, 3.6 | The hardware cursor; the gamma table; the V-sync overrides |
| Command set 1.0 | NOP, FENCE, SYNC, FLIP, DISCARD, FILL, BLT, UPLOAD and the first 3D packets |
| Command set 2.0 | The vertex pipeline: DECLARE, STREAM, INDICES, INLINE, DRAW, TRANSFORM, LIGHT, MATERIAL, CLIPPLANE and the rest |
| 2.1, 2.2 | DirectX 8's fixed function; shader programs (PROGRAM, BIND, CONSTANTS) |
| 2.3 | DOWNLOAD |
| 2.4 | The Glide device's packets |
| 2.5, 2.6, 2.7 | The OpenGL device: GL 1.1; its extensions (multitexture, compiled vertex arrays, paletted textures, point parameters, swap control); clamp-to-edge |

A packet whose PROFILE bit is clear is refused: the GPU stops with an error, and the log names the packet ([14](14%20Diagnostics%20and%20logs.md)).

## 86Box's settings for the card

In the machine settings, Display, then Configure beside the card. The saved values are never renumbered, so a machine's configuration keeps its meaning.

| Setting (config key) | Choices, default first | What it does |
|---|---|---|
| Memory size (`memory`) | 128 MB; 8, 16, 32, 64 MB | Video memory |
| Diagnostics (`diagnostics`) | Minimal; Off, DirectX, DirectX + capture, DirectX: texture audit, DirectX 7: vertex audit, DirectX 8: stream + texture audit (slow), Glide, OpenGL, Display, Developer: every packet (slow) | What the card writes to 86Box's log ([14](14%20Diagnostics%20and%20logs.md)) |
| 16-bit depth precision (`depth16`) | As the guest's; Full | Full keeps 16-bit depth buffers at a 32-bit float's precision |
| DirectX 8 interface (`dx8`) | On; Off | Off clears FIXED8: a DirectX 7 driver |
| DirectX 8 shaders (`dx8_shaders`) | On; Off; Capture; Capture, with GPU programs | Off clears PROGRAMS. The capture choices log each shader |
| Locks through main memory (`mainmem_locks`) | On; Off | Off clears DOWNLOAD |
| OpenGL GL_CLAMP (`ogl_clamp`) | As period drivers (clamp to edge); Exact (OpenGL 1.1) | How the OpenGL device samples GL_CLAMP |
| Cursor (`cursor`) | Hardware; Software (the DIB engine's) | Software clears CURSOR |
| Gamma (`gamma`) | On; Off | Off clears GAMMA |
| V-sync overrides (`vsync`) | On; Off | Off clears VSYNC: every program decides its own V-sync |
| Pipeline cache (`pipeline_cache`) | On; Start empty; Off | The host GPU's compiled pipelines, kept between sessions and shared by every machine ([06](06%20The%20host%20renderer%20%28Vulkan%29.md)) |

At power-on the card writes one line to 86Box's log naming the Diagnostics level, the memory, every setting that is not at its default, and the machine (board, processor and memory).

## Files the card writes on the host

| File | Where | What |
|---|---|---|
| 86Box's log | Wherever 86Box logs (`--logfile`) | Every diagnostics line |
| `vgpu98-pipelines.bin` | Beside 86Box's global configuration, with a second copy `vgpu98-pipelines.2.bin` | The pipeline cache ([`spec/pipeline-cache.md`](../spec/pipeline-cache.md)) |
| `vgpu98-textures/` | In the machine's folder | Captures, texture dumps and display captures ([14](14%20Diagnostics%20and%20logs.md)) |

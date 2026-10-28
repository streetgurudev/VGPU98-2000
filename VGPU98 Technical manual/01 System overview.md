# 01 System overview

The VGPU98 2000 is a graphics card that exists only in the 86Box emulator. This document says what it is, how its two halves divide the work, and how each kind of drawing travels from a program to the screen. Every later document takes one part of this picture and goes deeper.

## What the card is

To Windows 98 SE and its programs, the VGPU98 2000 is an ordinary PCI graphics card of 1999 or 2000:
- a register-exact VGA with its own VGA/VBE BIOS;
- a display engine for the high-resolution desktop modes, with a hardware cursor and a gamma table;
- a GPU that takes command packets from a ring: 2D blits and fills, Direct3D with hardware transform and lighting and DirectX 8 shaders, a Glide device that answers as a Voodoo3, and an OpenGL device;
- its own Windows 98 drivers, installed from one CD, for GDI, DirectDraw, Direct3D (the DirectX 6, 7 and 8 driver interfaces), Glide 2 and 3, and OpenGL 1.1.

Inside 86Box, none of that hardware exists as such. The card's registers and video memory are emulated, and every 3D packet is drawn by the host computer's own GPU through Vulkan.

## The two halves

| Half | Where it runs | What it is | Source |
|---|---|---|---|
| **The guest** | Inside the virtual machine, on the emulated CPU | The BIOS ROM and the eight driver files Windows 98 loads | [VGPU98-2000](https://github.com/streetgurudev/VGPU98-2000), this repository: `bios/`, `driver/`, `minivdd/`, `driver32/`, `glide/`, `opengl/`, `cpl/`, `card/` |
| **The host** | In 86Box, on the real computer | The card itself: PCI, VGA, the display engine, the command processor, the three 3D devices, the display on the host GPU | [86Box-VGPU98](https://github.com/streetgurudev/86Box-VGPU98), the 86Box fork, branch `vgpu98`, at `86Box/` as a submodule: `src/video/vid_vgpu98*.c`, `src/video/vgpu98/` (shaders), `src/include/86box/vid_vgpu98*.h` |

Both halves are written from the same definition. `card/vgpu98.card` (the card's identity, registers and sizes) and `card/display-modes.card` (its modes) are compiled by the project's `cardc` into the BIOS's include files, the display driver's mode table and 86Box's `vid_vgpu98_card.h`; the project's `infgen` writes the INF from them. Six more headers in `card/` define the packet formats and shared rules, and are copied unchanged into 86Box. The build checks that the copies match.

## The layers

```
 a program in the VM
       |
 Windows' interface runtime        GDI, DDRAW.DLL, D3D runtimes,
       |                           opengl32.dll; a Glide program
       |                           loads glide2x/glide3x itself
 the card's guest driver           vgpu98.drv, vgpu98dx.dll,
       |                           glide2x.dll, glide3x.dll,
       |                           vgpu98gl.dll
 the card's interfaces             VGA ports, the frame buffer
       |                           (BAR1), registers (BAR0), and
       |                           packets in the command ring
 ----- the virtual hardware boundary: 86Box's emulated PCI bus -----
       |
 the card in 86Box                 vid_vgpu98.c: PCI, VGA, the
       |                           display engine, video memory
 the command processor             vid_vgpu98_gpu.c: the ring,
       |                           fences, holds, 2D on the CPU
 a 3D device                       Direct3D (r3d), Glide, OpenGL
       |
 Vulkan on the host GPU            vid_vgpu98_vk.c and the shaders
       |
 the host display                  vid_vgpu98_disp.c: the frame
       |                           composed on the host GPU
 86Box's window
```

The boundary in the middle is the important one. Above it, the guest drivers know only what a real card would offer: ports, memory, registers and a command ring. Below it, 86Box is free to do the work any way it likes, as long as what the guest can observe stays exact.

## Who talks to whom

```
 guest DLL                    the card (BAR0)          command processor
 ---------                    ---------------          -----------------
 writes packets into the  --> RING_TAIL moves     -->  reads to RING_TAIL
 ring, ends with a FENCE                               runs each packet
 waits for FENCE          <-- FENCE register      <--  fence reached
                          <-- status written back      (into guest RAM)

 vgpu98.drv   maps BAR0, holds the GPU around mode changes, answers
              the DLLs' escapes, describes the frame buffer to GDI
 vgpu98.vxd   allocates the ring's buffer and the pool in guest RAM,
              fills the GPU's page table, holds the GPU for DOS sessions
```

Every writer of the ring takes the ring lock in the shared block first, so the DirectX DLL in several processes, the Glide DLLs, the OpenGL ICD and the display driver's mode changes never interleave their packets ([04](04%20Video%20memory%2C%20the%20ring%20and%20coherency.md)).

## Every path at a glance

| Path | Guest side | What the card receives | Host side | How it reaches the screen |
|---|---|---|---|---|
| **Boot, DOS, setup** | The BIOS (INT 10h, VBE) | VGA port writes, display engine registers | 86Box's VGA core, or the display engine | VGA modes: 86Box's own VGA renderer. VBE modes: the host display |
| **The desktop (GDI)** | GDI → DIBENG.DLL, set up by `vgpu98.drv` | Writes into the frame buffer | Video memory | The host display's mirror |
| **DirectDraw** | DDRAW.DLL → `vgpu98dx.dll` | FILL, BLT, UPLOAD, DOWNLOAD, FLIP packets | The command processor, on the CPU, in video memory | The mirror; a flip moves the scan-out start |
| **Direct3D** (DirectX 6, 7, 8; 9 runtimes) | D3D runtime → `vgpu98dx.dll` | State, texture, vertex and draw packets | The Direct3D device on Vulkan | The layer: the host display takes the newest pixels straight from the host images |
| **Glide** | `glide2x.dll` / `glide3x.dll` | Glide device packets | The Glide device on Vulkan | The session's front buffer, shown instead of video memory |
| **OpenGL** | opengl32.dll → `vgpu98gl.dll` | OpenGL device packets | The OpenGL device on Vulkan | Written into video memory through the window's visible rectangles |
| **Display controls** | `vgpu98.drv`, `vgpu98cp.cpl` | Cursor, gamma and V-sync registers | The host display | Applied at scan-out |

Documents 05 and 08 to 12 draw each of these out in full.

## Threads on the host

| Thread | What runs on it |
|---|---|
| 86Box's emulation thread | The guest's register and memory accesses, the VGA, the display engine's timing, vertical blank, and the copy of each composed frame into 86Box's frame buffer |
| The command processor | The ring: every packet, and every 3D device's recording and submitting. One mutex keeps the registers consistent between this thread and the emulation thread, and is never held while a packet runs |
| The host display | Composing each frame on the host GPU. It never touches 86Box's frame buffer itself |
| Writers | Captures and pictures, written to disk off the other threads |
| The pipeline cache's saver | Writes `vgpu98-pipelines.bin` |

## The design rules

These shape every part of the code.

- **Invisible to period software.** Programs should see a card of its time: real PCI identity, a real VGA, formats and caps that cards of 1999-2000 had. Nothing tells a program it runs under an emulator.
- **Exact by the interface's own rules.** Each translation layer follows the published behaviour of its interface (Direct3D's, Glide's, OpenGL 1.1's), not the habits of one game. There are no per-game fixes.
- **Feature bits, not assumptions.** Every capability beyond the VGA has a PROFILE bit. Drivers use only what the card reports, so 86Box's settings can turn a feature off and the card then behaves as an older card would ([02](02%20The%20VGPU98%202000%20card.md)).
- **The host does the work.** The guest drivers do what period drivers did; the heavy work (rasterizing, texturing, shading) runs on the host GPU, never on the emulated CPU.
- **Clean room.** No Microsoft DDK headers, samples or tools are in the tree, and no 3dfx code. Their documentation was read for facts only: interface layouts, export names, version fields. The project's interfaces are written from its own specs ([15](15%20Building%2C%20tools%20and%20file%20formats.md)).
- **The project's own tools.** NASM and the project's own linker and checkers build every guest file; nothing from a period SDK is needed.
- **A small footprint in 86Box.** The card lives in its own files, in plain C. Outside them it adds only a few hooks: a flag in the SVGA core so the card can compose its frame on the host GPU, a screenshot counter so screenshots can start captures, and padding after the last file of a folder mounted as a CD. 86Box's window, renderer and settings dialog are untouched.
- **Diagnostics stay outside the VM.** Everything the card and its drivers report goes to 86Box's log on the host, never into the guest ([14](14%20Diagnostics%20and%20logs.md)).

## Where to go next

- The card as hardware: [02](02%20The%20VGPU98%202000%20card.md), then [04](04%20Video%20memory%2C%20the%20ring%20and%20coherency.md) and [05](05%20The%20display%20path.md).
- The drivers: [07](07%20The%20Windows%2098%20driver%20stack.md), then the translation layer you need, [08](08%20DirectDraw%20translation.md) to [12](12%20OpenGL%20translation.md).
- What works today: [13](13%20Feature%20sets%20and%20compatibility.md).

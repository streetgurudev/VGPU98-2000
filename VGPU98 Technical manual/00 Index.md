# VGPU98 Technical manual

This manual explains how the VGPU98 project works, at the level of its systems: the virtual card, its BIOS, the Windows 98 driver stack, and the translation layers that carry each graphics interface from a program in the virtual machine to the host's GPU and onto the screen. It describes the code as it is.

For register-level and packet-level detail, each document names the page of the [`spec/`](../spec/README.md) folder that holds it. The spec is the reference; this manual is the map.

## Reading paths

| If you want to... | Read |
|---|---|
| Know what the card is and what it can run | [01](01%20System%20overview.md), [13](13%20Feature%20sets%20and%20compatibility.md) |
| Install and use it | [07](07%20The%20Windows%2098%20driver%20stack.md), [05](05%20The%20display%20path.md), [14](14%20Diagnostics%20and%20logs.md) |
| Work on a guest driver | [01](01%20System%20overview.md), [04](04%20Video%20memory%2C%20the%20ring%20and%20coherency.md), [07](07%20The%20Windows%2098%20driver%20stack.md), then the translation layer: [08](08%20DirectDraw%20translation.md) to [12](12%20OpenGL%20translation.md) |
| Work on 86Box's side | [01](01%20System%20overview.md), [02](02%20The%20VGPU98%202000%20card.md), [04](04%20Video%20memory%2C%20the%20ring%20and%20coherency.md), [05](05%20The%20display%20path.md), [06](06%20The%20host%20renderer%20%28Vulkan%29.md) |
| Follow one interface end to end | Its translation document, 08 to 12: each has the whole path drawn out |
| Build, test or release | [15](15%20Building%2C%20tools%20and%20file%20formats.md) |

## The documents

| Doc | Title | What it covers |
|---|---|---|
| 00 | Index | This page: reading paths, the document list, the whole system, a glossary |
| 01 | [System overview](01%20System%20overview.md) | What the card is, the guest and host halves, the layers, every path at a glance, the design rules |
| 02 | [The VGPU98 2000 card](02%20The%20VGPU98%202000%20card.md) | PCI identity, BARs, BAR0's registers, PROFILE and the versions, 86Box's settings |
| 03 | [The video BIOS (ROM)](03%20The%20video%20BIOS%20%28ROM%29.md) | What the ROM is, its layout, POST, INT 10h, VBE 2.0, DDC, the fonts, how it is built and checked |
| 04 | [Video memory, the ring and coherency](04%20Video%20memory%2C%20the%20ring%20and%20coherency.md) | The memory map, the reserve, the command ring, fences and holds, the page table and the pool, where the newest pixels live |
| 05 | [The display path](05%20The%20display%20path.md) | VGA and extended modes, the display engine, modes and the monitor, the host display, cursor, gamma, V-sync, mode changes |
| 06 | [The host renderer (Vulkan)](06%20The%20host%20renderer%20%28Vulkan%29.md) | Vulkan on the host, the command processor and its three 3D devices, host images, pipelines and their cache, the shaders, the threads |
| 07 | [The Windows 98 driver stack](07%20The%20Windows%2098%20driver%20stack.md) | The package, the INF, how Windows loads each file, the display driver, the mini-VDD, the shared block and escapes, the control program |
| 08 | [DirectDraw translation](08%20DirectDraw%20translation.md) | Surfaces, flips, blits, colour keys, fills, Locks |
| 09 | [Direct3D translation (DirectX 6 and 7)](09%20Direct3D%20translation%20%28DirectX%206%20and%207%29.md) | DrawPrimitives2, state, textures and palettes, hardware T&L, render targets |
| 10 | [Direct3D 8 translation (streams and shaders)](10%20Direct3D%208%20translation%20%28streams%20and%20shaders%29.md) | Streams and declarations, buffers in the pool, vertex and pixel shaders 1.x, DirectX 9 runtimes |
| 11 | [Glide translation](11%20Glide%20translation.md) | The Voodoo3 personality, sessions, the Glide device, TMU memory, the screen |
| 12 | [OpenGL translation](12%20OpenGL%20translation.md) | How the ICD is found, GL 1.1 in the ICD, batches, the OpenGL device, swaps into windows |
| 13 | [Feature sets and compatibility](13%20Feature%20sets%20and%20compatibility.md) | What each interface offers, the limits, what is missing, programs seen running |
| 14 | [Diagnostics and logs](14%20Diagnostics%20and%20logs.md) | The Diagnostics levels, where lines come from, captures, reading a run |
| 15 | [Building, tools and file formats](15%20Building%2C%20tools%20and%20file%20formats.md) | The toolchain, the project's own tools, NE/LE/PE, the card definition, reproducible builds, the tests, the clean room |

## The whole system

```
 GUEST: Windows 98 SE in an 86Box virtual machine
 +------------------------------------------------------------------+
 |  DOS, setup    GDI         DirectX 6-9    Glide        OpenGL    |
 |  programs      programs    programs       programs     programs  |
 |     |             |            |             |            |      |
 |  INT 10h         GDI       DDRAW, D3D    glide2x.dll  opengl32   |
 |     |             |        runtimes      glide3x.dll      |      |
 |     |         DIBENG.DLL       |             |       vgpu98gl.dll|
 |  VGPU98 BIOS  vgpu98.drv  vgpu98dx.dll       |            |      |
 |   (the ROM)   vgpu98.vxd       |             |            |      |
 |     |             |            +------+------+------------+      |
 |     |             |                   | packets in the ring      |
 +-----|-------------|-------------------|--------------------------+
       | VGA ports   | frame buffer      | BAR0 registers
       v             v (BAR1)            v
 HOST: 86Box   +--------------+   +----------------------------+
               | vid_vgpu98.c |   | the command processor      |
               | PCI, VGA,    |   | vid_vgpu98_gpu.c           |
               | display      |   |  2D on the CPU, in video   |
               | engine,      |   |  memory; 3D to a device:   |
               | video memory |   |  Direct3D | Glide | OpenGL |
               +------+-------+   +------+------+------+-------+
                      |                  |  Vulkan     |
                      |                  v             |
                      |           images on the host GPU
                      v                  |
               +--------------------------------------------+
               | the host display (vid_vgpu98_disp.c):      |
               | video memory's mirror, the 3D layer,       |
               | Glide's screen, the cursor and gamma       |
               +---------------------+----------------------+
                                     v
                              86Box's window
```

## Glossary

| Term | Meaning here |
|---|---|
| **BAR0, BAR1** | The card's two PCI memory windows: 64 KB of registers, and the 128 MB frame buffer aperture onto video memory |
| **The card** | The VGPU98 2000 as 86Box presents it: PCI device 9898:2000 |
| **The command processor** | The card's GPU front end in 86Box: it reads the ring on a thread of its own and runs each packet |
| **Command set** | The version of the packet language, now 2.7. Each packet needs its PROFILE bits |
| **Device** | One of the three 3D back ends on the host: the Direct3D device (the 3D renderer), the Glide device and the OpenGL device |
| **Display engine** | The card's own scan-out for the extended modes, beside the standard VGA |
| **Drawable** | The OpenGL device's object for a window's buffers |
| **Fence** | A packet that, once reached, sets BAR0's FENCE register to its value: how drivers know work is done |
| **Hold** | A bit in CONTROL that stops the GPU from starting: HOLD_DRIVER around mode changes, HOLD_VDD while a DOS session owns the screen |
| **Host image** | A Vulkan image on the host GPU standing for a surface or texture in video memory |
| **Interface version** | HW_VERSION, now 3.6: the version of BAR0's registers and behaviour |
| **The layer** | Pixels the GPU drew that are newer than video memory, which the host display takes straight from the host images |
| **The mirror** | The host display's copy of video memory on the host GPU, updated by changed pages |
| **Mini-VDD** | `vgpu98.vxd`, the card-specific part of Windows 98's virtual display driver |
| **Packet** | One command in the ring: a header dword (opcode and length) and its payload |
| **The pool** | Guest RAM the GPU reads through its page table: vertex and index buffers, and Lock copies |
| **PROFILE** | BAR0's feature bits. A driver uses a feature only when its bit is set |
| **The reserve** | The top 256 KB of video memory, kept for the ring, the cursor patterns and the gamma table |
| **The ring** | The circular command buffer the guest writes and the GPU reads, in video memory or guest RAM |
| **Session** | A Glide program's hold on the Glide device and the screen, from grSstWinOpen to grSstWinClose |
| **The shared block** | Memory in the display driver's data segment that the display driver, the DirectX DLL and the other DLLs share |
| **TMU** | A Voodoo texture unit; the Glide device has two, each with 8 MB of texture memory |

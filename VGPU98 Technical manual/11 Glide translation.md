# 11 Glide translation

Glide is 3dfx's own 3D interface. On the VGPU98 2000 it is two DLLs that answer every program as a Voodoo3 would, and a device of its own in 86Box that draws as a Voodoo does. This document follows a Glide call from the program to the screen. The reference is [`spec/glide.md`](../spec/glide.md) and [`spec/gpu-commands.md`](../spec/gpu-commands.md) (the Glide packets).

## The path

```
 Glide program (Windows)
   |  loads glide2x.dll or glide3x.dll from the system folder
   v
 glide2x.dll (2.61) / glide3x.dll (3.10): the API, answering as a
   |  Voodoo3 3000; each version's entry points and vertex layout
   |  on the shared Glide core (glide/):
   |    card.c    find the card (escape VGPU98_ESC_INFO, first call)
   |    screen.c  the session, the batch, the screen
   |    state.c   the pixel pipeline's state as device registers
   |    draw.c    every primitive as triangles, as a Voodoo3 builds
   |    tex.c     texture memory, downloads, palettes, NCC
   |    lfb.c     frame buffer locks through a staging surface
   |
   |  grSstWinOpen: DirectDraw's exclusive mode, 16 bpp, the
   |  program's resolution
   v
 VGPU98_GL_* packets (command set 2.4), in the DLL's own batch,
   |  copied into the ring under the ring lock
   v
 the ring --> the Glide device (vid_vgpu98_glide.c)
   |            a session: its buffers on the host GPU, both TMUs'
   |            memory (8 MB each), Glide's state
   |            glide.vert: vertices in screen space, unchanged
   |            glide.frag: the Voodoo pixel pipeline
   v
 grBufferSwap --> the front buffer handed to the display
   |              glide_show.comp: 565 to 86Box's pixels, through
   |              Glide's gamma table, scaled to the screen
   v
 86Box's window, instead of video memory, until the session ends
```

## The Voodoo3 personality

| What a program asks | What it is told |
|---|---|
| The board | A Voodoo3 3000: 16 MB, two TMUs, FBI and TMU revision 11002h |
| Glide 2 | `glide2x.dll`, version string 2.61.00.0658, the Voodoo3 1.07 driver's 133 exports by their decorated names |
| Glide 3 | `glide3x.dll`, 3.10.00.0658, 96 exports; grGet and grGetString answer for the board |
| File versions | 2.61.0.658 and 3.10.0.658, as a Voodoo3's DLLs have |

These are facts of the interface programs expect, taken from 3dfx's documentation and the Voodoo3 driver's export tables; no 3dfx code is used. The DLLs' export lists are checked against a Voodoo3's own DLLs in the tests, when the local reference library is present.

## Finding the card and the session

- **The card** is found once a process, at the program's first Glide call, not in DllMain (GDI's calls are kept out of the loader's lock). The display driver's escape VGPU98_ESC_INFO gives the shared block, which holds BAR0's address and the log's line lock.
- **grSstWinOpen** takes the screen as a Voodoo3's Glide does: DirectDraw's exclusive mode for the program's window, at the program's resolution and 16 bpp; DirectDraw gives the desktop back when the program closes or ends. Then it opens a session on the Glide device with the buffers the program asked for, and makes an off-screen staging surface in video memory for frame buffer access.
- **Commands** go into a batch of the DLL's own and are copied into the ring in one go under the ring lock. While the GPU is held, the batch waits.

## State and drawing

**State.** Each Glide call checks its arguments as a Voodoo3's Glide does and stores Glide's own numbers as the device's registers (`card/vid_vgpu98_glcmd.h`). The registers go to the device before the next command that reads them.

**Primitives.** Every primitive reaches the device as triangles, built as a Voodoo3's Glide builds them:
- a triangle is culled by its signed area; a zero-area one draws nothing;
- a point is a small triangle covering exactly the pixel its position falls in;
- a line is a one-pixel-wide parallelogram, its ends moved half a pixel across its major axis; points and lines are never culled;
- consecutive triangles with no state change between them go out as one DRAW.

**Coordinates are Voodoo-exact.** x and y are taken as a Voodoo's setup unit takes them: 12.4 fixed point in 16 bits, so whole multiples of 4096 pixels fall away, including a program's snap bias.

**Vertices.** Each vertex is 16 floats: screen x and y, 65535/z, 1/w, the colour (0-255), each TMU's s/w, t/w and 1/w, and the fog coordinate. `glide.vert` passes them on unchanged: a Voodoo iterates its parameters linearly in screen space and divides per pixel, which `glide.frag` does. TMU1's own 1/w follows 3dfx's active-TMU rule: a Voodoo3's Glide sends a TMU its own coordinates only while it is active.

## The pixel pipeline

`glide.frag` is one shader whose settings come from the session's registers, in a Voodoo's order:

```
 TMU1 --> TMU0 (the TMU chain: each TMU's combine, TMU1 into TMU0)
              |
              v
 chroma key on the colour combine's "other" colour
              |
 colour combine, alpha combine:
   ((other - local) x (factor + 1)) >> 8, plus local or local
   alpha, clamped, inverted on request; in 8-bit integers, as the
   hardware computes
              |
 alpha test --> fog --> depth (Z, or W as the Voodoo's 4.12 float,
              with the bias) --> dithering to 5:6:5
              |
 blending, the colour and depth masks, the depth test: the
 Vulkan pipeline's (vid_vgpu98_glide.c)
```

Each TMU samples as a Voodoo does: its level of detail by the Voodoo's rule, point or bilinear filtering, clamp or wrap, and its texture formats, palettes and NCC tables.

## Texture memory

Texture memory is the device's, a session's own, never video memory: each TMU's address space as a Voodoo3 reports it, 8 MB each, held on the host. The DLL computes what a Voodoo3's Glide computes (texture sizes, level offsets, the TMUs' ranges) with the device's own arithmetic, from `card/vid_vgpu98_glcpu.h`, which both sides share.

| Call | Packet |
|---|---|
| grTexDownloadMipMap, grTexDownloadMipMapLevel, partial downloads | TEXDATA |
| grTexDownloadTable (palettes, NCC tables) | PALETTE |
| grTexSource, grTexCombine, filtering and clamping | The TMUs' registers |

Glide 2's levels and aspect ratios are turned into Glide 3's numbers in the Glide 2 DLL, so the core works in one set.

## The frame buffer

The buffers live on the host GPU. A program reaches them through the staging surface in video memory, laid out as a Voodoo3's frame buffer is (2048 pixels a row).

| Operation | What happens |
|---|---|
| A read lock | The device writes the buffer into the staging surface; the DLL waits for it |
| A write lock | The surface is filled the same way first, in the program's format; at the unlock the device takes back the pixels the program changed |
| grLfbWriteRegion | Its pixels go to the device without a read |
| Formats | 565 colour, and the 16-bit auxiliary buffer |

## The screen

A Glide session does not draw into video memory at all. At each grBufferSwap, the device hands the front buffer to the host display, which shows it instead of video memory until the session ends ([05](05%20The%20display%20path.md)):
- `glide_show.comp` widens each 565 channel to 8 bits by repeating its high bits, as a Voodoo's video output does, then applies Glide's gamma table (Glide 2's grGammaCorrectionValue; Glide 3's grLoadGammaTable and guGammaCorrectionRGB);
- a frame of another size than the screen is scaled to it, nearest pixel;
- the hardware cursor is hidden, as on a Voodoo3.

**Swaps and V-sync.** grBufferSwap's interval waits that many vertical blanks; the Glide bits of VSYNC_CONTROL can force the wait on or off ([05](05%20The%20display%20path.md)). The number of swaps a program can have waiting is limited.

## Diagnostics

At Diagnostics "Glide" ([14](14%20Diagnostics%20and%20logs.md)):
- the DLLs write their full lines, including what each program called and how often;
- the device writes a report every few seconds: the blends, what the texture units sampled, the busiest textures, the texture cache's checks;
- findings, worded "possible ...", each said once, and captures at a finding, ten seconds into each session, and at an 86Box screenshot.

At every level, each process keeps a recorder of its last 32 Glide calls; at every level but Off, a fault catcher writes the recorder and the fault's place to the log if the program crashes inside Glide.

## Not offered

- Glide 3's extensions: `grGetString(GR_EXTENSION)` is empty and `grGetProcAddress` returns NULL;
- `guMovie*` and `_grGetSipValues`, which return 0 as a Voodoo3's Glide 2 does;
- a 3dfx PCI identity, for the few programs that look for one;
- DOS Glide.

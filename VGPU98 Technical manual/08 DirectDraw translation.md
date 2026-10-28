# 08 DirectDraw translation

DirectDraw is the base of every DirectX path: Direct3D's surfaces, flips and Locks are DirectDraw's, and the Glide DLLs and the OpenGL ICD use it for the screen and their staging surfaces. This document covers how `vgpu98dx.dll` answers DirectDraw: which callbacks it offers, and what each becomes on the card. The reference is [`spec/directx-hal.md`](../spec/directx-hal.md) and [`spec/gpu-commands.md`](../spec/gpu-commands.md); the caps are in [`spec/d3d-caps.md`](../spec/d3d-caps.md).

## The path

```
 program
   |  IDirectDraw / IDirectDrawSurface calls
   v
 DDRAW.DLL (Microsoft's runtime): video memory heap, clipping,
   |  surface objects, emulation of what the driver doesn't do
   |  driver callbacks (the HAL)
   v
 vgpu98dx.dll (driver32/ddraw.c, surface.c, shadow.c)
   |  each callback: packets in the ring, or DDHAL_DRIVER_NOTHANDLED
   |  so DirectDraw does it itself
   v
 the ring --> command processor (vid_vgpu98_gpu.c)
                FILL, BLT          on the host CPU, in video memory
                UPLOAD, DOWNLOAD   between guest RAM and video memory
                FLIP               the display start, at vertical blank
                SYNC, DISCARD      coherency with the host images
   |
   v
 video memory --> the host display (05) --> the window
```

## Discovery

DirectDraw finds the driver through GDI's Control escape on the display driver: QUERYESCSUPPORT for DCICOMMAND, then the DCICOMMAND escapes. `vgpu98.drv` names its 32-bit DLL with the shared block's address; DirectDraw's helper process (DDHELP) loads `vgpu98dx.dll` once, into the shared system arena, and calls DriverInit. The DLL fills in its part of the driver description (caps, callbacks, Direct3D's tables), the display driver completes it (the mode, video memory, the mode list), and DirectDraw receives it. This happens again at every mode change.

Every section of the DLL is shared, so its tables (surfaces, the pool, palettes, programs) are the same in every process that uses it, each under its own lock.

## The callbacks

| Callback | Offered when | What it does on the card |
|---|---|---|
| WaitForVerticalBlank, GetScanLine | Always, with the registers | Read VBLANK_COUNT and SCANLINE |
| CreateSurface, CanCreateSurface | PROFILE.BLIT | Check and describe surfaces in video memory: their format code and their place. DirectDraw's own heap manager allocates the memory, below the reserve |
| DestroySurface | BLIT | A DISCARD of the surface's range, so its host copies go |
| Blt | BLIT | FILL or BLT packets ([below](#blits)) |
| Flip, GetFlipStatus | BLIT | A FLIP packet; FLIP_PENDING ([below](#flips)) |
| FlipToGDISurface | BLIT | Back to the desktop's frame |
| Lock | BLIT | Make the surface safe for the CPU ([below](#locks)) |
| Unlock | With Locks through main memory | Send a Lock's copy back with UPLOAD |
| SetColorKey, AddAttachedSurface | BLIT | Record keys and chains for blits and flips |
| GetBltStatus | BLIT | Whether a surface's blits are done (its fence) |
| GetDriverInfo | BLIT | The DirectX 7 and 8 extensions: Direct3D's callbacks, surface handles, the DirectX 8 interface ([09](09%20Direct3D%20translation%20%28DirectX%206%20and%207%29.md), [10](10%20Direct3D%208%20translation%20%28streams%20and%20shaders%29.md)) |
| Execute buffer callbacks | With the pool | Execute and vertex buffers in guest RAM ([10](10%20Direct3D%208%20translation%20%28streams%20and%20shaders%29.md)) |
| Direct3D's HAL tables | PROFILE.RENDER3D | Contexts, state, DrawPrimitives2 ([09](09%20Direct3D%20translation%20%28DirectX%206%20and%207%29.md)) |

A card without PROFILE.BLIT gets only the display timing calls, and DirectDraw emulates the rest in software.

## Surfaces

A surface is something DirectDraw placed in video memory: the primary, back buffers, off-screen plains, textures, depth buffers. The DLL gives each the GPU's format code ([`spec/gpu-formats.md`](../spec/gpu-formats.md)) and its offset and pitch, which every packet that uses it carries. Surfaces in system memory stay DirectDraw's own; the GPU sees them only through UPLOAD.

## Blits

| Blit | Packet | Notes |
|---|---|---|
| Copy, same size | BLT | Overlapping rectangles copied in the right order |
| Stretch and shrink | BLT | Point sampling, as cards of the time stretched |
| Mirror, left-right and up-down | BLT | |
| Source and destination colour keys | BLT | DDBLT_KEYSRC, DDBLT_KEYDEST and their overrides |
| Colour fill, depth fill | FILL | |
| System memory to video memory | UPLOAD | A plain copy, as DDCAPS_CANBLTSYSMEM offers it |
| A presenting blit (windowed 3D) | BLT | The back buffer onto the primary, clipped to the window |
| Raster operations other than SRCCOPY | Not offered | DirectDraw does them in software |

FILL and BLT run on the host CPU directly in video memory, after the command processor has copied back any newer pixels the host images hold in the rectangles involved ([04](04%20Video%20memory%2C%20the%20ring%20and%20coherency.md)). A clipped blit becomes one packet per visible rectangle.

## Flips

A Flip writes a FLIP packet under the ring lock. The command processor waits for the vertical blank (unless the program asked for DDFLIP_NOVSYNC, or a V-sync override says otherwise) and moves the display engine's START to the new front buffer. Flip intervals (DDFLIP_INTERVAL2 to 4) wait that many vertical blanks. GetFlipStatus answers from FLIP_PENDING.

When Direct3D renders into a flipping chain, its render target follows the chain: after each flip, the next draw goes to the new back buffer even though DirectDraw swapped the surfaces' memory underneath ([09](09%20Direct3D%20translation%20%28DirectX%206%20and%207%29.md)).

## Locks

A Lock gives the program a pointer to the surface's bytes. The DLL must make sure no pending packet still writes it, and that the bytes are the newest.

```
 Lock(surface)
   |
   +-- the GPU is held?  --> DDERR_SURFACELOST (a mode change or a
   |                         DOS session; DirectDraw restores later)
   |
   +-- a surface the GPU never drew into (most textures), outside a
   |   flipping chain: wait only for its last use, then DirectDraw
   |   computes the pointer into the frame buffer itself
   |
   +-- a surface the GPU drew into, or in a flipping chain:
         |
         +-- Locks through main memory on (PROFILE.DOWNLOAD):
         |     DOWNLOAD the surface into a copy in the pool; the
         |     program reads and writes guest RAM; the last Unlock
         |     sends the copy back with UPLOAD
         |
         +-- otherwise: SYNC (copy the GPU's newer pixels back into
               video memory) and a FENCE; the pointer is into the
               frame buffer
```

**Why copies in guest RAM.** Reading the frame buffer through the emulated PCI bus is slow, and a program that reads what the GPU drew (a screenshot, a software effect, a game drawing its HUD into the back buffer) would read it a pixel at a time. A copy in guest RAM is read at memory speed. Textures Locked with DISCARDCONTENTS frame after frame go through copies too.

The setting "Locks through main memory" Off clears PROFILE.DOWNLOAD and every Lock goes through the frame buffer.

## Not offered

- **Overlays and YUV (FourCC) surfaces.** No overlay caps; video players fall back to blits.
- **Raster operations** other than SRCCOPY.
- **Hardware-accelerated GDI.** GDI draws through the DIB engine ([07](07%20The%20Windows%2098%20driver%20stack.md)).

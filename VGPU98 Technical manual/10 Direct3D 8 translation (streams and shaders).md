# 10 Direct3D 8 translation (streams and shaders)

What the DirectX 8 driver interface adds on top of [09](09%20Direct3D%20translation%20%28DirectX%206%20and%207%29.md): vertex streams and declarations, hardware vertex and index buffers in the pool, and vertex shaders 1.1 and pixel shaders 1.0-1.4, translated into the card's own program format. DirectX 9 runtimes use the same interface. The reference is [`spec/directx-hal.md`](../spec/directx-hal.md) ("The DirectX 8 interface", Shaders), [`spec/d3d-caps.md`](../spec/d3d-caps.md) and [`spec/gpu-commands.md`](../spec/gpu-commands.md).

## The path

```
 program (DirectX 8, or 9 on a DirectX 8 card)
   |  SetStreamSource, SetIndices, CreateVertexShader,
   |  CreatePixelShader, DrawIndexedPrimitive...
   v
 the DirectX 8 runtime --> DrawPrimitives2, with DirectX 8 commands
   v
 vgpu98dx.dll
   |  d3d8.c    GetDriverInfo2: D3DCAPS8, the formats, the runtime's
   |            version; vertex shaders and their declarations
   |  vdecl.c   declaration tokens --> DECLARE elements
   |  xlate.c   shader tokens --> the GPU's programs
   |  pshader.c pixel shaders kept, translated, bound
   |  prog.c    programs: ids every process shares, resent after
   |            a reset
   |  pool.c    vertex and index buffers in guest RAM
   v
 packets: DECLARE, STREAM (up to 16), INDICES, DRAW;
          PROGRAM, BIND, CONSTANTS; the DirectX 7 state packets
   |
   v
 the ring --> the Direct3D device
                vgpu98.vert runs the vertex program, or the fixed
                function, over the streams' bytes
                vgpu98.frag runs the pixel program in place of the
                texture stages
   v
 the host images --> the layer --> the host display (05)
```

## Turning the interface on

The DLL answers GetDriverInfo2, the DirectX 8 runtime's question, only when the card has PROFILE.FIXED8 (command set 2.1). Without it (the setting "DirectX 8 interface" Off) the runtime sees a DirectX 7 driver, and DirectX 8 programs run through the runtime's DirectX 7 path.

| Answer | What it says |
|---|---|
| D3DCAPS8 | DirectX 8's caps: streams, vertex shader 1.1 and pixel shader 1.4 versions (with PROGRAMS), point sprites, volume textures, the matrix palette |
| The format list | Every texture, render target and depth format, with what each can be used for |
| The runtime's version | Recorded, for the logs and for the runtime-specific rules below |

## Streams and declarations

A DirectX 8 draw reads its vertices from up to 16 streams, through the vertex shader's declaration:
- a fixed-function shader is an FVF code, which becomes a DECLARE as in DirectX 7;
- a programmable shader has a declaration of its own (CREATEVERTEXSHADER's tokens), which `vdecl.c` turns into DECLARE elements: each stream, each register load with its type, skips, and constants to load.

The GPU decodes the declaration itself in `vgpu98.vert` ([06](06%20The%20host%20renderer%20%28Vulkan%29.md)). Element types include DirectX 8's UBYTE4, SHORT2 and SHORT4, and the point size.

**Fixed-function additions** (command set 2.1): indexed vertex blending through the world matrix palette, tweening between two positions and normals, point sprites and point sizes up to 256, volume textures up to 256 on a side, the MULTIPLYADD and LERP texture operations with their third argument, the TEMP register and RESULTARG. Formats V16U16, G16R16 and A2W10V10U10, and a lockable D16.

## Buffers in the pool

```
 application's vertex or index buffer (D3DPOOL_DEFAULT)
   |
   | created through DirectDraw's execute buffer callbacks:
   | a block in the pool
   v
 the pool (guest RAM, mini-VDD chunks)  <---- the GPU reads it through
   |                                          its page table: STREAM
   | Lock                                     and INDICES carry GPU
   |   nothing queued reads it: the same      addresses
   |     memory, at once
   |   DISCARDCONTENTS while queued draws read it: renamed onto a
   |     fresh block or one of the buffer's spares; the GPU finishes
   |     with the old bytes
   |   NOOVERWRITE: the same memory, at once (the program promises
   |     not to touch what is queued)
   v
 Unlock: nothing to copy; the next draw reads the new bytes
```

- **Blocks** are 32-byte aligned and come from chunks the mini-VDD maps for the DLL as a whole, in shared tables under the pool lock, so every process sees the same buffers.
- **Spare blocks** let a buffer that is renamed every frame alternate between blocks it already owns, instead of taking new memory each time.
- **Give-back:** chunks that hold no buffer go back to Windows, so a program's peak use does not keep guest RAM for ever.
- **The runtime's own records** of resources live for one call only: it reuses them at once. So on a DirectDraw object a DirectX 8 device was made on, the DLL keeps copies of its own (CreateSurfaceEx), which last, and an application's pool buffer is recorded the same way.

Without the pool (no PROFILE.GPUVM), vertices go into the ring inline.

## Shaders

```
 shader tokens (vs 1.0-1.1, ps 1.0-1.4)
   |
   v
 xlate.c: every instruction checked, its registers mapped; texture
   |      instructions and macros lowered into the GPU's operations
   v
 the GPU's program format (card/vid_vgpu98_prog.h), checked again
   |      by the GPU's own validator, the same code 86Box runs
   v
 prog.c: an id from the table every process shares
   |
   v
 PROGRAM (the instructions), BIND (which program draws),
 CONSTANTS (registers c#; def constants at SETPIXELSHADER;
 a declaration's constants at SETVERTEXSHADER)
   |
   v
 86Box: vgpu98.vert runs a vertex program's instructions over the
 input registers the declaration loads, into the record the fixed
 function fills; vgpu98.frag runs a pixel program in place of the
 texture stages
```

| | Vertex shaders | Pixel shaders |
|---|---|---|
| Versions | 1.0, 1.1 | 1.0, 1.1, 1.2, 1.3, 1.4 |
| Constants | 192, as a Radeon 8500 has | 8; values range to 8 |
| Needs | PROFILE.PROGRAMS (command set 2.2) | The same |
| Without PROGRAMS | The runtime processes vertex shaders in software | No pixel shaders are offered |

A program the GPU loses (a reset loses every program) is sent again before its next use. The setting "DirectX 8 shaders": On; Off (clears PROGRAMS); Capture, and Capture with GPU programs, which log each distinct shader once a session, disassembled, with its first draw's states, for testing.

## DirectX 9 runtimes

A DirectX 9 runtime drives the card as it drives any DirectX 8 card, through the same interface. Programs that need DirectX 9's own interface or shaders 2.0 find them missing and fall back or skip, as they would on a Radeon 8500 or GeForce4 Ti: 3DMark03 completes, skipping its game test 4.

## Not offered

- DirectX 9's driver interface and shaders 2.0;
- N-patches and RT patches;
- multisampling;
- the pure device;
- Direct3D 8's own cursor calls (the desktop's hardware cursor is the display driver's).

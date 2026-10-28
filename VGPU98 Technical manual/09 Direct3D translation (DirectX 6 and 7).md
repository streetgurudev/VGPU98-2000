# 09 Direct3D translation (DirectX 6 and 7)

How Direct3D's driver interface becomes the card's packets, through the DirectX 7 interface that DirectX 6 and 7 programs use (and DirectX 3 and 5 programs, through the runtime). DirectX 8's additions are in [10](10%20Direct3D%208%20translation%20%28streams%20and%20shaders%29.md). The reference is [`spec/directx-hal.md`](../spec/directx-hal.md), [`spec/d3d-caps.md`](../spec/d3d-caps.md), [`spec/gpu-commands.md`](../spec/gpu-commands.md) and [`spec/gpu-formats.md`](../spec/gpu-formats.md).

## The path

```
 program (DirectX 3, 5, 6 or 7)
   |  IDirect3DDevice calls: states, textures, DrawPrimitive...
   v
 the Direct3D runtime (Microsoft's): builds a command buffer for
   |  DrawPrimitives2; DirectX 3 and 5 execute buffers are made in
   |  the pool and translated by the runtime
   v
 vgpu98dx.dll
   |  d3d.c   contexts, texture handles, render and stage state,
   |          transform and lighting state, render targets, Clear
   |  d3d7.c  surface handles, palettes, state blocks, TEXBLT
   |  dp2.c   DrawPrimitives2: the command buffer, command by command
   v
 packets: TARGET VIEWPORT WRANGE ZRANGE STATE TEXTURE PALETTE CLEAR
          TRANSFORM LIGHT LIGHTENABLE MATERIAL CLIPPLANE
          DECLARE STREAM INDICES INLINE DRAW
   |
   v
 the ring --> the Direct3D device (vid_vgpu98_r3d.c, _vtx.c)
   |            host images of the target, depth and textures
   |            vgpu98.vert: vertices fetched, transformed, lit
   |            vgpu98.frag: texture stages, fog, alpha test
   v
 the host GPU's images --> the layer --> the host display (05)
                       --> video memory, only when something reads
```

## Contexts and state

The runtime creates a context per Direct3D device. Each context keeps all of its Direct3D state in the DLL: render states, the eight texture stages' states, transforms, lights, materials, clip planes, the viewport and the render target.

**The GPU holds one state at a time.** Before a context draws, it sends only the state the GPU lacks; when another context drew last, or the GPU was reset, it sends everything. So two programs drawing at once each see their own state, and a single program pays only for what it changes.

**State blocks** (DirectX 7) are recorded and applied in the DLL; applying one is a set of state changes like any other.

## DrawPrimitives2

The runtime hands the driver a command buffer: a run of commands, each with its data. `dp2.c` reads them in order:
- **State commands** (render states, texture stage states, DirectX 7's transform, light, material, clip plane and viewport commands) update the context, or the state block being recorded.
- **Drawing commands** (triangle lists, strips and fans, lines, points, indexed or not) send what state the GPU lacks, the draw's vertex declaration, where its vertices are, its indices, then a DRAW.
- **CLEAR and TEXBLT** go into the same batch, in order, so they land between the right draws.

```
 one DrawPrimitives2 call
   |
   +-- for each command
         state?  --> update the context (or the recording block)
         draw?   --> missing state:  STATE, TEXTURE, TRANSFORM, ...
                     the vertices:   DECLARE (from the FVF code)
                                     STREAM  (a pool buffer, by its
                                              GPU address)
                                     or INLINE (the vertices copied
                                              into the ring)
                     the indices:    INDICES (or inline)
                     then            DRAW
         clear?  --> CLEAR
         texblt? --> the copy, in order
   |
   +-- the batch ends with a FENCE; RING_TAIL moves
```

**Vertices.** The flexible vertex format (FVF) becomes a DECLARE: the GPU decodes positions, normals, colours and up to eight sets of texture coordinates itself. Vertices in an application's vertex buffer are read from the pool through the GPU's page table; vertices the runtime passes inline are copied into the ring. Up to 1M vertices or indices a draw are taken, so no draw is ever split.

## Transform and lighting

With DirectX 7's hardware T&L, the vertex shader on the host GPU does all of it ([06](06%20The%20host%20renderer%20%28Vulkan%29.md)):
- world, view and projection transforms, up to 4 blend matrices;
- 8 lights: point, spot and directional, with a local viewer and material colour sources;
- 6 user clip planes;
- texture coordinate generation and the texture matrices;
- vertex fog and range fog.

Pre-transformed vertices (XYZRHW) skip the transform and are placed as given. A DirectX 6 runtime does not know DirectX 7's T&L caps, so to it the card is a rasterizer and the runtime transforms the vertices itself.

## Textures and palettes

- **Texture handles** (DirectX 6) and **surface handles** (DirectX 7, from CreateSurfaceEx) name the surfaces a stage samples. A flip moves handles along with the memory, as DirectX 7 requires.
- **A texture's description** (where it is, its format, its levels) is sent as a TEXTURE packet. The DLL caches descriptions: 4,096 kept, in sets of eight chosen by the handle; a cube map's is built at each use.
- **The host copies a texture** when it processes a draw that reads it, uploading the levels whose pages changed. Most textures are never drawn into, so they are never copied back.
- **Palettes** for 8-bit textures: up to 8,192, each with a version that changes with its colours and flags, so a draw always samples the palette it was given. The GPU applies the palette when it samples.
- **TEXBLT** (DirectX 7's texture load) copies between textures in order with the draws.

**Formats:** 565, 555, 1555, 4444, X888, 8888, 8-bit palettized, L8, A8L8, the bump formats V8U8, L6V5U5 and X8L8V8U8, and DXT1-5; textures up to 2048x2048, cube maps, and non-power-of-two sizes under the usual conditions.

## The pixel pipeline

`vgpu98.frag` is Direct3D 7's fixed-function pixel pipeline:
- 8 texture stages with every operation and argument, 8 textures at once;
- bump mapping (BUMPENVMAP and BUMPENVMAPLUMINANCE);
- colour keys, specular, the alpha test, dithering;
- vertex fog and table fog (linear, exp, exp2), W fog;
- point, linear, mipmapped and anisotropic filtering, up to 16x.

**Depth and stencil:** D16, D24X8 and D24S8, the W-buffer (kept as a float), the stencil buffer, depth bias.

## Render targets

**The render target by role.** Direct3D draws a flipping chain's frames into its back buffer, and DirectDraw swaps the surfaces' memory at every flip. The DLL follows the role, not the memory: after a flip the next draw goes to the new back buffer, so a program never draws into the surface being shown.

**SetRenderTarget** moves the target and depth buffer to another surface. The old target's host image stays GPU-newer until something reads it.

**Clear and Clear2** become CLEAR packets on the host image: colour, depth and stencil, whole or in rectangles.

## Faults

Every callback and every DrawPrimitives2 command runs inside the DLL's own exception frame (`guard.c`). A fault is logged with where it happened and the call fails, instead of the program going down silently or a lock staying held.

## What a DirectX 6 or 7 program sees

| Area | What is offered |
|---|---|
| Devices | The HAL, with hardware T&L to DirectX 7 runtimes |
| Texture stages | 8, all operations, 8 textures at once |
| Formats | As above |
| Lights, clip planes, blend matrices | 8, 6, 4 |
| Filtering | Up to 16x anisotropic |
| Depth | 16 and 24 bits, stencil, W-buffer |
| Not offered | Antialiasing (edge or full-scene) |

The complete caps are in [`spec/d3d-caps.md`](../spec/d3d-caps.md); what works in practice is in [13](13%20Feature%20sets%20and%20compatibility.md).

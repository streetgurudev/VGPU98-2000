# 06 The host renderer (Vulkan)

Everything the card draws in 3D is drawn by the host computer's GPU. This document covers how 86Box reaches it: the Vulkan device, the command processor and its three 3D devices, the host images that stand for video memory, the pipelines and their cache, the shaders, and which thread does what. The reference is [`spec/gpu-commands.md`](../spec/gpu-commands.md), [`spec/gpu-formats.md`](../spec/gpu-formats.md), [`spec/gpu-coherency.md`](../spec/gpu-coherency.md) and [`spec/pipeline-cache.md`](../spec/pipeline-cache.md).

## Vulkan on the host

| Fact | Value |
|---|---|
| Loader | Loaded at run time (`vulkan-1.dll` on Windows; the code also names `libvulkan.so.1` and `libvulkan.1.dylib`). 86Box has no link-time Vulkan dependency |
| Device | A headless Vulkan 1.1 device: no window, no surface. The GPU renders into images of its own, and the display composes the screen on the same device |
| Needs | A graphics queue, `shaderClipDistance`, `shaderInt64` |
| Uses when present | `VK_EXT_custom_border_color` |
| Without it | The card comes up without RENDER3D, VERTEX, GLIDE and OGL: the ring, 2D and DirectDraw keep working, Direct3D, Glide and OpenGL are not offered, and the display uses 86Box's own line renderer |
| Source | `vid_vgpu98_vk.c` (loading, choosing the device, its features), `vid_vgpu98_vk.h` (the shared command buffers) |

## The command processor and its devices

```
 the ring (guest RAM or video memory)
        |
        v
 +----------------------------------------------------------------+
 | command processor (vid_vgpu98_gpu.c), its own thread           |
 |   NOP FENCE SYNC FLIP DISCARD FILL BLT UPLOAD DOWNLOAD          |
 |     -> run on the host CPU, directly in video memory           |
 |   everything else -> the device that owns the opcode           |
 +--------+---------------------+-----------------------+---------+
          |                     |                       |
          v                     v                       v
 +------------------+  +------------------+   +-------------------+
 | Direct3D device  |  | Glide device     |   | OpenGL device     |
 | vid_vgpu98_r3d.c |  | vid_vgpu98_      |   | vid_vgpu98_ogl.c  |
 | + _vtx.c         |  |   glide.c        |   | command sets      |
 | command sets 1.0 |  | command set 2.4  |   | 2.5-2.7           |
 | to 2.3           |  |                  |   |                   |
 | host images of   |  | sessions, their  |   | share groups,     |
 | video memory     |  | buffers, TMU     |   | contexts,         |
 | surfaces         |  | memory           |   | drawables, the    |
 |                  |  |                  |   | texel arena       |
 +--------+---------+  +--------+---------+   +---------+---------+
          |                     |                       |
          +---------- one set of command buffers -------+
                                |
                      Vulkan queue (shared with the host
                      display, under the device's queue lock)
```

- **One recorder.** The Glide and OpenGL devices record into the 3D renderer's command buffers. Each ends its own render pass whenever another device records or the renderer submits, so the three never disturb each other's state.
- **Separate state.** Each device keeps its own state and images. The Glide device never touches the renderer's images or video memory's coherency; the OpenGL device touches video memory only when it writes a window's frame.
- **Errors.** The Direct3D packets follow the ring's rule: a bad packet stops the GPU with an error. The Glide and OpenGL devices drop a command they cannot take, count it, and go on; the OpenGL device names the first ones in the log.

## The Direct3D device

The 3D renderer draws the packets of command sets 1.0 to 2.3.

**Host images.** Every surface in video memory that the GPU draws into or samples gets a host image, keyed by its description: where it is in video memory and how it is laid out. Images follow the coherency rule of [04](04%20Video%20memory%2C%20the%20ring%20and%20coherency.md): uploaded when the pages under them change, copied back when something reads bytes they hold newer, published to the display without leaving the host GPU.

**Formats** ([`spec/gpu-formats.md`](../spec/gpu-formats.md)):
- the 16- and 32-bit colour formats, the luminance and alpha formats;
- 8-bit palettized textures through their palette;
- DXT1-5 decoded on the host CPU;
- the bump formats as floats;
- depth: D16, D24X8 and D24S8, and a W-buffer kept as a 32-bit float holding W. The setting "16-bit depth precision" Full keeps D16 at a float's precision.

**Vertices run on the host GPU.** For each draw, the renderer hands `vgpu98.vert` a copy of the stream bytes the draw's vertices occupy and the declaration resolved into its uniform block (`vid_vgpu98_vtx.c`). The shader fetches each vertex, takes it as given if pre-transformed or runs the fixed-function transform and lighting, and places it with Direct3D's pixel centres on a 1/16-pixel grid.
- **Plain triangles and points** use the host GPU's own primitive assembly and culling, which assemble and cull as Direct3D does.
- **Shapes that need a whole primitive** (lines by Direct3D's line rule, wireframe, point fill, texture wrapping across a primitive) are drawn from a list of the draw's primitives built on the CPU, one shader invocation per output vertex.
- **Indexed draws** have their indices turned into window numbers on the CPU, so the host GPU never sees the guest's index values, and an out-of-range vertex reads as 0.

**Pixels** go through `vgpu98.frag`, Direct3D 7's fixed-function pixel pipeline: up to eight texture stages with every operation and argument, bump mapping, colour keys, specular, vertex and table fog, the alpha test, dithering, and DirectX 8's additions. Where the specification leaves a detail open, it follows the behaviour of Microsoft's reference rasterizer.

**Shader programs.** DirectX 8 shaders arrive as the GPU's own program format ([10](10%20Direct3D%208%20translation%20%28streams%20and%20shaders%29.md)). A vertex program's instructions run inside `vgpu98.vert`, over the input registers the declaration loads; a pixel program runs inside `vgpu98.frag` in place of the texture stages. They run through these general shaders, not as one SPIR-V module per program.

## The Glide and OpenGL devices

| | Glide device | OpenGL device |
|---|---|---|
| Source | `vid_vgpu98_glide.c` | `vid_vgpu98_ogl.c` |
| Objects | Sessions: a program's buffers on the host and its two TMUs' memory | Share groups, contexts, drawables (a window's buffers) |
| Vertices | `glide.vert`: 16 floats each, passed on unchanged in screen space, as a Voodoo iterates them | `ogl.vert`: GL 1.1's per-vertex operations, from storage buffers |
| Pixels | `glide.frag`: the Voodoo's pixel pipeline in its own order and 8-bit arithmetic | `ogl.frag`: GL 1.1 texturing (fetched and filtered by hand from the texel arena), fog, stipple, clip planes, the alpha test |
| Where frames go | Handed to the display, shown instead of video memory while the session shows (`glide_show.comp`) | Written into video memory through the window's visible rectangles |
| Detail | [11](11%20Glide%20translation.md) | [12](12%20OpenGL%20translation.md) |

## Pipelines and the pipeline cache

Each device draws with Vulkan pipelines, one for each combination of the states a pipeline fixes (its key). Making a pipeline takes the host driver time, so the card keeps them:

- **In memory,** for the session.
- **On disk,** in `vgpu98-pipelines.bin` beside 86Box's global configuration (with a second copy `vgpu98-pipelines.2.bin`). The file checks itself; Vulkan checks that its data belongs to this host GPU and driver. It is shared by every machine and merged at each save, by a thread of its own.
- **Warm-up.** At start, the keys listed in the cache are made again before any program needs them. The OpenGL device warms its own shaders' first pipelines too, so the first OpenGL frame does not stall.
- **The setting** "Pipeline cache": On; Start empty (begin without what the file holds); Off.

## The shaders

All shaders are GLSL in `src/video/vgpu98/`, compiled to SPIR-V at build time by glslangValidator and built into `86Box.exe` as headers. Without glslang or the Vulkan headers, 86Box builds the card without 3D.

| Shader | Lines | Role |
|---|---|---|
| `vgpu98.vert` | 1524 | Direct3D's vertex pipeline: vertex pulling, fixed-function T&L, vertex blending and tweening, vertex programs, lines and points |
| `vgpu98.frag` | 822 | Direct3D's pixel pipeline, pixel programs; built a second time with computed depth (W-buffer, depth bias, 16-bit depth's grid) |
| `glide.vert`, `glide.frag` | 63, 493 | The Glide device |
| `ogl.vert`, `ogl.frag` | 636, 577 | The OpenGL device; `ogl.frag` built a second time with computed depth |
| `display.comp` | 210 | The host display: video memory's mirror and the layer into 86Box's pixels, the cursor, gamma |
| `glide_show.comp` | 80 | A Glide session's front buffer into 86Box's pixels, through Glide's gamma table |

## Threads

| Thread | What it does |
|---|---|
| Emulation (86Box's) | Register and memory accesses, the VGA, vertical blank, copying each composed frame into 86Box's frame buffer |
| Command processor | The ring, every packet, all recording and submitting for the three 3D devices |
| Host display | Composing frames on the host GPU, sharing the Vulkan queue under the queue lock |
| Pipeline cache | Saving `vgpu98-pipelines.bin` |
| Capture writers | Pictures and capture files |

One mutex keeps the card's registers consistent between the emulation thread and the command processor, and it is never held while a packet runs. 86Box's own threading caveat applies: a thread's handle may close as the thread ends, so the card waits for its threads with events, never by waiting on a handle.

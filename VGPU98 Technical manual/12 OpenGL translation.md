# 12 OpenGL translation

OpenGL reaches the card through an installable client driver (ICD), `vgpu98gl.dll`, which Windows' own `opengl32.dll` loads into each OpenGL program. The ICD implements OpenGL 1.1 itself and records commands for the card's OpenGL device in 86Box. This document follows that path. The reference is [`spec/opengl.md`](../spec/opengl.md) and [`spec/gpu-commands.md`](../spec/gpu-commands.md) (the OpenGL packets).

## The path

```
 OpenGL program
   |  wglCreateContext, glBegin, glTexImage2D, SwapBuffers...
   v
 opengl32.dll (Windows'): asks the display driver for its ICD
   |  escape OPENGL_GETINFO --> "VGPU98"
   |  HKLM\...\OpenGLdrivers\VGPU98 --> vgpu98gl.dll, loaded into
   |  the program; the 18 Drv* exports
   v
 vgpu98gl.dll (opengl/): GL 1.1's whole API and state
   |  main.c     entry points, pixel formats, contexts
   |  device.c   batches, the device's objects, windows, swaps
   |  draw.c     glBegin/glEnd and arrays as DRAW's vertices
   |  raster.c   the ICD's own path: render modes, GL's special
   |             rules
   |  lists.c, arrays.c, teximage.c, texobj.c, pixel.c, light.c,
   |  matrix.c, eval.c ... one file per part of the API
   v
 VGPU98_OGL_* packets (command sets 2.5-2.7), in each context's own
   |  batch, copied into the ring under the ring lock as one, ended
   |  by a FENCE
   v
 the ring --> the OpenGL device (vid_vgpu98_ogl.c)
   |            share groups, contexts, drawables (a window's
   |            buffers on the host GPU), the texel arena
   |            ogl.vert: GL 1.1's per-vertex operations
   |            ogl.frag: texturing, fog, stipple, alpha test
   v
 SwapBuffers --> SHOW: the front image written into video memory
   |              through the window's visible rectangles
   v
 video memory --> the host display (05) --> the window
```

## Finding and loading the ICD

1. A program's first OpenGL call loads `opengl32.dll`.
2. `opengl32.dll` asks the display driver which ICD it has: the escape OPENGL_GETINFO answers "VGPU98".
3. It reads `HKLM\Software\Microsoft\Windows\CurrentVersion\OpenGLdrivers\VGPU98` (the INF wrote `vgpu98gl.dll`) and loads the ICD into the program.
4. It calls the ICD's 18 Drv* exports. The ICD finds the card once a process, at the first call that needs it, through the display driver's VGPU98_ESC_INFO escape.

| Export | What it does |
|---|---|
| DrvDescribePixelFormat, DrvSetPixelFormat | GDI's pixel format calls on a window of the card's desktop: the ICD's formats come first, then Windows' own software ones |
| DrvCreateContext, DrvDeleteContext, DrvSetContext, DrvReleaseContext, DrvShareLists, DrvCopyContext | Contexts. DrvSetContext (wglMakeCurrent) returns the table of GL's 336 functions |
| DrvSwapBuffers | The swap ([below](#swaps)) |
| DrvGetProcAddress | The offered extensions' functions |
| The layer-plane calls | There are no layer planes |

**Pixel formats**, all RGBA, drawn to a window, single-buffered or double-buffered (swapped by copy):

| Desktop | Colour | Depth and stencil |
|---|---|---|
| 16 bpp | 565 | 16-bit depth, or 24-bit depth with 8-bit stencil |
| 32 bpp | XRGB8888, or ARGB8888 with destination alpha | 24-bit depth with stencil, or 16-bit depth |
| 8 bpp | None of the ICD's; only Windows' software formats | |

## GL 1.1 in the ICD

The ICD implements OpenGL 1.1 exactly, as the specification defines it: the whole API, display lists, evaluators, selection and feedback, the pixel transfer path, and every query. Its state is kept per context in the ICD; what the device needs is sent as the context's registers before the next draw (`card/vid_vgpu98_oglcmd.h`, shared by both sides).

| Part | Where it is done |
|---|---|
| State, errors, queries | The ICD |
| Display lists | The ICD: a list is a run of recorded operations, replayed through the same code |
| Vertex arrays | The ICD reads each element when the call is made, as GL defines it; with EXT_compiled_vertex_array, a locked array is converted once |
| Transform, lighting, texture coordinate generation, clip planes, fog distance | The device, in `ogl.vert`, from the context's registers |
| Rasterizing triangles | The host GPU, through Vulkan |
| Points, lines, bitmaps and images | Quads in window coordinates, made in `ogl.vert` or the ICD, with GL's rules for widths, stipple and smooth coverage |
| Texturing | `ogl.frag`, which fetches and filters texels from the device's texel arena by GL 1.1's rules |
| Scissor, stencil, depth, blending, the logic op, masks | The Vulkan pipeline |
| Render modes (selection, feedback), and what GL's rules need computed exactly | The ICD's own path (`raster.c`) |

## Batches

Each context records into a batch of its own, which is copied into the ring under the ring lock in one go and ended by a FENCE. A GL command is never lost: while the display driver or the mini-VDD holds the GPU (a mode change, a full-screen DOS session), a batch waits instead of being dropped. The ICD waits up to 5 seconds for the GPU's progress before it gives up, more than the DirectX DLL's second, as GL programs can send long batches.

**Drawing.** What glBegin and glEnd gathered, and the vertex arrays drawn directly, reach the device as a DRAW's unique vertices and 16-bit indices. A vertex is sent as given (object coordinates, the current colour, normal and texture coordinates), and the device transforms it.

**Textures.** Each level's image is unpacked through the pixel transfer path, made RGBA8 with its base format's components and its border, and sent to the device (TEXIMAGE), which keeps the texels in its texel arena on the host. glGetTexImage reads them back (TEXREAD). Paletted textures (EXT_paletted_texture) keep their indices and palette, shared or the texture's own.

## Swaps

```
 SwapBuffers (double-buffered window)
   |
   v
 the ICD: SHOW for the window's drawable, with the window's visible
   |      rectangles (a DirectDraw clipper); the software cursor is
   |      kept out of the way if it is over the window
   |      (escape VGPU98_ESC_ACCESS)
   v
 the OpenGL device:
   the front image read back to the host's memory
   newer DirectX pixels in those rectangles copied back first
   the rows written into video memory, rectangle by rectangle
   the pages marked changed
   |
   v
 the host display's mirror shows them, like any other pixels (05)
```

- **Windowed OpenGL** draws onto the desktop itself, so it shares the screen with GDI and DirectX correctly, under the coherency rule ([04](04%20Video%20memory%2C%20the%20ring%20and%20coherency.md)).
- **Single-buffered drawing** into the front buffer is shown soon after, without waiting for a swap.
- **Contexts survive mode changes:** a mode change resets the GPU, and the ICD sends a context's objects and state again.
- **The swap interval** (WGL_EXT_swap_control) waits that many vertical blanks for a full-screen window. A windowed program never waits, so GDI is never held behind it. The OpenGL bits of VSYNC_CONTROL can force the wait on or off ([05](05%20The%20display%20path.md)).

## Extensions

An extension is offered only on a device whose command set draws what it adds; an older 86Box gets GL 1.1 alone.

| Command set | Extensions |
|---|---|
| 2.6 | GL_ARB_multitexture, GL_SGIS_multitexture (two texture units), GL_EXT_compiled_vertex_array, GL_EXT_paletted_texture, GL_EXT_shared_texture_palette, GL_EXT_point_parameters, WGL_EXT_swap_control |
| 2.7 | GL_EXT_texture_edge_clamp, GL_SGIS_texture_edge_clamp |

**GL_CLAMP.** OpenGL 1.1's GL_CLAMP blends with the border colour at the edge; period drivers clamped to the edge instead, and games relied on it. The setting "OpenGL GL_CLAMP" chooses: as period drivers (clamp to edge, the default), or exact OpenGL 1.1.

**The strings:** GL_VENDOR "VGPU98", GL_RENDERER "VGPU98 2000/PCI", GL_VERSION "1.1.0".

## Diagnostics

At Diagnostics "OpenGL" ([14](14%20Diagnostics%20and%20logs.md)):
- the ICD's lines: the pixel formats a program sets, the extensions offered, GL errors by name, each function a program asked for that is not there;
- the device's report every 5 seconds while OpenGL works: what was drawn, the textures and their wrap modes, read-backs, drops, the pipelines made;
- findings, worded "possible ...", and captures.

At every level but Off, the ICD writes its event lines, the device names its first 16 dropped commands, and a fault catcher logs a crash inside the ICD with where it happened.

## Not offered

- OpenGL 1.2 and later;
- more than two texture units;
- S3TC, anisotropic filtering, LOD bias, the add and combine texture environments;
- WGL_ARB_extensions_string, ARB pixel formats, pbuffers;
- dithering, and GL_POLYGON_SMOOTH's coverage.

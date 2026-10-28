# The OpenGL ICD

`vgpu98gl.dll` gives OpenGL 1.1 to Windows programs on the VGPU98 2000: an installable client driver (ICD) that Windows 98's own `opengl32.dll` finds through the display driver and loads into every program that uses OpenGL on the card's desktop, in place of Windows' software OpenGL ("GDI Generic"). It also offers the extensions Quake 2 uses ([The extensions](#the-extensions)) and clamp-to-edge ([Clamp-to-edge](#clamp-to-edge-gl_ext_texture_edge_clamp-and-gl_sgis_texture_edge_clamp)). `opengl32.dll` keeps WGL and GDI's pixel format calls and forwards them to the ICD's 18 `Drv*` exports; a GL call jumps through the table of GL 1.1's 336 functions that `DrvSetContext` returns. The ICD keeps all of GL's state, and draws on the card's **OpenGL device**, a device of 86Box's own beside the DirectX renderer and the Glide device (command set 2.5, and 2.6 and 2.7 for the extensions: [gpu-commands.md](gpu-commands.md)): it sends the device its share groups, contexts and drawables, the state registers each context draws with, vertices, clears, textures, pixel rectangles and read-backs through the GPU's command ring, and the host GPU draws them into a drawable's images of the window's size, transformed, lit and shaded as GL 1.1 defines it. Unlike Glide's buffers, a window's frames reach the screen through video memory, the desktop's truth: at a swap the device writes the drawable's front image into the window's visible rectangles there. Source: `opengl/` (C), linked by `vglink` with `opengl/vgpu98gl.lnk`; the device in 86Box's `src/video/vid_vgpu98_ogl.c`, with its shaders `src/video/vgpu98/ogl.vert` and `ogl.frag`; tested on the host by `test_ogl` ([The host test](#the-host-test)).

**The OpenGL 1.1 specification itself is not in the reference library.** So the ICD and the device are built from GL 1.1's well-known rules and from facts restated in our own words: the enum values from the XP DDK's `gl.h` (checked against Wine's header), DirectDraw's method order from the DX SDK's `ddraw.h`, the ICD interface and the 336-entry table's order, types and stack bytes from ReactOS's `opengl32` (checked against ReactOS's and Wine's export lists), what Windows 98's `opengl32.dll` asks of a display driver from JHRobotics' vmdisp9x, and Mesa 2.6 (ReactOS's copy, LGPL), a 1998 implementation of GL 1.1, where a detail needed a second reading. All of it was read for facts only; nothing of it is in the tree. Where this page says "GL 1.1, section n", the section is the specification's as those sources cite it.

Nothing in the ICD is for the user to set; one 86Box setting, **OpenGL GL_CLAMP**, decides how the device samples GL_CLAMP ([Texturing and the texel arena](#texturing-and-the-texel-arena); [registers.md](registers.md), 86Box settings). OpenGL is accelerated whenever the card has 3D on the host GPU and the OpenGL device starts there (PROFILE.OGL, [gpu-registers.md](gpu-registers.md)); without it the display driver names no ICD, and Windows' software OpenGL serves. DirectX and Glide are unchanged by it: no OpenGL command reads or changes their state.

## The file

| File | Interface | Version information | Source |
|---|---|---|---|
| `vgpu98gl.dll` | The OpenGL ICD interface, OpenGL 1.1 | 4.10.0.1, the driver package's (`4.10.0.1`); product version 1.0 | `opengl/` |

The INF copies it into WINDOWS\SYSTEM (destination 11) with the other driver files, and adds the registry value that names it ([driver-package.md](driver-package.md); [How Windows finds the ICD](#how-windows-finds-the-icd)).

| Source | What it holds |
|---|---|
| `opengl/main.c` | DllMain; finding the card; the process's heap; the current context (TLS, the one-thread fast path); the pixel formats; the 18 `Drv*` exports and their lock |
| `opengl/ext.c` | The extensions: which are offered, GL_EXTENSIONS, DrvGetProcAddress's table of names, WGL_EXT_swap_control's two calls, the device's register count |
| `opengl/multitex.c` | The multitexture extensions' entry points: the active unit, the client unit, SGIS's coordinate set, each unit's current coordinates |
| `opengl/palette.c` | The palettes: `glColorTableEXT`, `glColorSubTableEXT`, `glGetColorTableEXT` and the parameter queries, each texture's palette and the context's shared one, and their PALETTE packets |
| `opengl/device.c` | The device's objects and their IDs, the windows and their drawables, the batches and the ring, the registers sent as they change, the DirectDraw clippers, the staging surface, the cursor's exclusion, swaps and the front image, the process's end |
| `opengl/draw.c` | Primitives as the DRAW command's vertices and 16-bit indices, from a vertex source (`glBegin`'s, or the arrays drawn directly): the device's path and the ICD's own path's output |
| `opengl/raster.c` | The ICD's own vertex path: transform, lighting, texture coordinate generation, clipping, the viewport; `glRasterPos`; the selection and feedback render modes |
| `opengl/prim.c` | `glBegin` and `glEnd`, each vertex with the current values, `glMaterial` between them |
| `opengl/current.c`, `arrays.c` | The current values and vertex calls, `glRect`; the client vertex arrays, the compiled vertex arrays' lock, the arrays drawn directly |
| `opengl/matrix.c`, `light.c`, `misc.c`, `enable.c`, `buffer.c` | The matrix stacks; lighting's state; points (with their parameters, and the size by distance), lines, polygons, fog, hints, the per-fragment tests, masks, buffers, clip planes, the texture environment and generation; `glEnable` and its kind; clears, `glFlush`, `glFinish`, the viewport, the depth range, the scissor box |
| `opengl/get.c`, `state.c`, `attrib.c` | Every `glGet*` and `glGetString`; GL's state for a new context and its errors; the attribute stacks and `wglCopyContext`'s copy |
| `opengl/lists.c`, `eval.c` | Display lists; evaluators |
| `opengl/pixel.c`, `pixops.c` | The pixel store, transfer and maps, unpacking and packing; `glDrawPixels`, `glBitmap`, `glReadPixels`, `glCopyPixels` |
| `opengl/teximage.c`, `texobj.c` | Texture images (`glTexImage*`, `glTexSubImage*`, `glCopyTex*`, `glGetTexImage`, the proxies; index levels); texture objects, parameters, completeness, each unit's texture drawn with |
| `opengl/regs.c` | GL's state as the device's registers |
| `opengl/table.c`, `glfuncs.h`, `glcalls.h` | The 336-entry table and its order; the entry points' declarations |
| `opengl/glmath.c` | The arithmetic: the x87's own instructions for the C library's functions, GL's matrix operations |
| `opengl/icd.h`, `state.h`, `glenum.h`, `gpuenv.h`, and the others' headers | What the files share; GL's state and limits; GL 1.1's types and values, the extensions' values (and PIXELFORMATDESCRIPTOR's); what `driver32/gpu.c` needs of the ICD |
| `opengl/diag.c` | The ICD's lines to the host's log and its diagnostics: the level, the check, GL errors by function, the names and formats asked for that are not there, the swap interval's decisions, the report and the findings, the fault catcher ([Diagnostics](#diagnostics)) |
| `driver32/rt.c`, `driver32/gpu.c`, `driver32/logline.c` | What gcc expects of a C library; the command ring, built in with `VGPU98_ICD_DLL`, which has it take `opengl/gpuenv.h` in place of the DirectX DLL's header ([Batches and the ring](#batches-and-the-ring)); the line writer the Glide DLLs use, through DEBUG ([Diagnostics](#diagnostics)) |
| `card/vid_vgpu98_oglcmd.h` | Command sets 2.5's and 2.6's values, shared with 86Box as the Glide header is |

It is compiled as `vgpu98dx.dll` is, with `VGPU98_DLL_CFLAGS`: gcc `-m32`, freestanding, against `driver32/include`'s own headers, x87 floating point. The ICD keeps GL's matrices in single precision (GL's double forms are taken to float). The build keeps the DLL only once drvlint has passed it ([The export list](#the-export-list)).

The version resource ([pe-format.md](pe-format.md)): 4.10.0.1 as file version and 1.0 as product version, file type 3 (a driver), subtype 8 (installable), as the Glide DLLs are typed; the strings `CompanyName` "VGPU98 Project", `FileDescription` "VGPU98 2000 OpenGL driver", `FileVersion` and `ProductVersion` "4.10.0.1", `InternalName` `vgpu98gl`, `OriginalFilename` `vgpu98gl.dll`, `ProductName` "VGPU98 2000", `LegalCopyright` "Copyright 2026 VGPU98 Project contributors. GPL-2.0-or-later."

## A per-process DLL

Like the Glide DLLs, the ICD is an ordinary DLL: image base 11800000h, clear of Glide's 10000000h, in the loading process's own address space, no section shared. Each process that loads it has its own windows, contexts and GL state. What processes share is the display driver's: the card's registers, the command ring and its lock, found through the shared block ([directx-hal.md](directx-hal.md)).

| DLL | Functions | For |
|---|---|---|
| KERNEL32 | TlsAlloc, TlsFree, TlsGetValue, TlsSetValue | The thread's current context |
| | GetProcessHeap, HeapAlloc, HeapReAlloc, HeapFree | Contexts, batches, GL's state, images |
| | Sleep, GetTickCount, GetCurrentProcessId, GetCurrentThreadId | Waits, the ring lock's owner, the IDs, the front buffer's 16 ms |
| | SetLastError | `wglSwapIntervalEXT`'s refusal of a negative interval |
| | GetModuleFileNameA, GetSystemDirectoryA, SetUnhandledExceptionFilter, IsBadReadPtr | The diagnostics: the program's and the DLL's files and the system folder, the fault catcher and its walk of the stack (all in Windows 98's KERNEL32, as the Glide DLLs import them) |
| GDI32 | CreateDCA, ExtEscape, DeleteDC | Finding the card; the cursor's exclusion |
| USER32 | WindowFromDC, GetClientRect, ClientToScreen, IsWindow | The window of a DC, its client size and place, a window gone |
| DDRAW | DirectDrawCreate | The clippers (the visible rectangles) and the staging surface |

The entry point, DllMain (`_DllMain@12`):
- **The process attach** keeps the DLL's instance (its file and its image, for the diagnostics), and takes the process's heap and a TLS index (none left: DllMain answers FALSE). It looks for no card: GDI's calls are kept out of the loader's lock, so the card is found at the first call that needs it.
- **A thread's detach** (DLL_THREAD_DETACH): a context current on the ending thread is let go, so another thread may make it current or delete it. Its batch is sent if the exports' lock is free at once; the detach runs under the loader's lock and never waits for the exports' lock (a first swap holding it may be loading DirectDraw's DLL, which waits for the loader's lock: the final audit), so otherwise the batch stays in the context and goes with its next flush, on whichever thread makes it current.
- **The process detach** closes every context and drawable on the device, waiting for nothing ([The process's end](#mode-changes-dos-sessions-gpu-restarts-and-the-processs-end)), and frees the TLS index. At the process's end (DllMain's `reserved` not NULL) it first lets go of the exports' lock and of the ring's, which a thread the system killed may hold; at a FreeLibrary (`reserved` NULL) it takes the fault catcher out ([Diagnostics](#diagnostics)), as the DLL's code goes. It returns TRUE.

## How Windows finds the ICD

Windows 98's `opengl32.dll` asks the display driver for an ICD's name with escape **OPENGL_GETINFO (1101h)**, then looks the name up under `HKLM\Software\Microsoft\Windows\CurrentVersion\OpenGLdrivers` and loads the DLL the value names.

- **The display driver** (`driver/ddraw.asm`, Control) answers QUERYESCSUPPORT (8) for OPENGL_GETINFO with 1, and the escape itself, only while PROFILE has OGL and RENDER3D. The answer, written to the output buffer: a dword 2 (the version), a dword 1 (the driver version), then the name `VGPU98` in ANSI characters, NUL-terminated (16 bytes in all, the layout a Windows 98 NVIDIA driver gives). Without PROFILE.OGL both go to the DIB engine, which answers neither, and Windows draws OpenGL in software.
- **The INF** adds the value ([driver-package.md](driver-package.md)): `HKLM,"Software\Microsoft\Windows\CurrentVersion\OpenGLdrivers","VGPU98",,"vgpu98gl.dll"`. The name is `driver.card`'s `opengl_name`, the file its `opengl_driver`.
- **The display driver's second escape for the ICD, VGPU98_ESC_ACCESS (7099h)**, keeps the DIB engine's software cursor out of a rectangle of the screen while the GPU writes a window's pixels there ([Swaps](#swapbuffers)). Its input, `vgpu98_escaccess_t` (`driver32/shared.h`, 10 bytes): `begin` (WORD: 1 begin, 0 end), then `left`, `top`, `right`, `bottom` (WORDs, on the screen, inclusive). The display driver notes, in its SetCursor and MoveCursor (`driver/cursor.asm`), whether a cursor shape is set (SetCursor's NULL hides it) and where the cursor last moved. While the card's hardware cursor has the cursor (interface 3.4), it is never in a window's pixels: a begin answers 0, so the ICD neither waits for its SHOW nor drops the swap interval ([win98-display-driver.md](win98-display-driver.md), The cursor). A begin keeps the cursor out only when a cursor is shown and within 32 pixels of the rectangle (a Windows 9x cursor is 32 by 32 at most, around its hot spot): it then calls the DIB engine's DIB_BeginAccess for the rectangle with CURSOREXCLUDE and answers 1; otherwise it does nothing and answers 0, and there is nothing to let back (the final audit). An end calls DIB_EndAccess with CURSOREXCLUDE, which draws the cursor again over the new pixels, and answers 1. Only while PROFILE has OGL and RENDER3D; otherwise the DIB engine has the escape, and answers 0. The display driver's QUERYESCSUPPORT does not answer it (the ICD calls it without asking).

## Finding the card

At the first call that needs the card (a pixel format's description or setting, a context's creation, DrvGetProcAddress), once a process (a second thread calling meanwhile waits, `Sleep(1)` at a time, for the first's answer), as the Glide DLLs find it ([glide.md](glide.md), Finding the card):

1. A DC for the display: `CreateDCA("DISPLAY", NULL, NULL, NULL)`.
2. ExtEscape QUERYESCSUPPORT for VGPU98_ESC_INFO (7098h) must answer above 0; then ExtEscape VGPU98_ESC_INFO with the question `vgpu98_escquery_t` (ESC_INFO_MAGIC, an output size of 32) and a 32-byte answer must answer above 0, with the answer's `magic` ESC_INFO_MAGIC and `size` 32 or 16. The DC is deleted.
3. The shared block at the answer's `shared_linear` must start with SB_MAGIC.
4. BAR0 at the block's `regs_linear` must read its ID 38394756h (`VG98`). From here the ICD can write to the host's log: the block's first fields keep their places in every build ([Diagnostics](#diagnostics)).
5. The block's `version` and `size`, and the answer's `sb_version`, must be this build's; otherwise the event `no OpenGL: another build's display driver`, under a log lock of the DLL's own, and nothing more from the process.
6. PROFILE must have OGL and RENDER3D; otherwise the event `no OpenGL: the GPU's profile` … (no 3D, or no OpenGL device).

With every check passed the ICD keeps the block, BAR0 and video memory's flat address (the block's `fb_linear`), chooses the ring ([Batches and the ring](#batches-and-the-ring)), reads COMMAND_SET, which decides the extensions offered ([The extensions](#the-extensions)), and starts its diagnostics: DEBUG read for the level, the `loaded` line, the check of its build, and the fault catcher ([Diagnostics](#diagnostics)). Otherwise it has no card: it describes no pixel format, sets none and makes no context, so `opengl32.dll` keeps Windows' own formats and software OpenGL. Before step 5 there is nobody to tell: another display driver, or no VGPU98 card, writes no line.

## Pixel formats

The formats for the desktop's depth now (the shared block's mode), double-buffered first, as ChoosePixelFormat favours the earlier. Each is a drawable format of the device ([gpu-commands.md](gpu-commands.md), Command set 2.5, DRAWABLE):

| Desktop | Format | Colour | Depth and stencil | Buffers |
|---|---|---|---|---|
| 16 bpp | 1 | RGB 565 | D16 | Double |
| | 2 | RGB 565 | D24S8 | Double |
| | 3 | RGB 565 | D16 | Single |
| | 4 | RGB 565 | D24S8 | Single |
| 32 bpp | 1 | XRGB 8888 | D24S8 | Double |
| | 2 | XRGB 8888 | D16 | Double |
| | 3 | ARGB 8888 (8 alpha bits kept) | D24S8 | Double |
| | 4 | XRGB 8888 | D24S8 | Single |
| | 5 | XRGB 8888 | D16 | Single |
| | 6 | ARGB 8888 | D24S8 | Single |
| 8 bpp | | none (as Direct3D) | | |

Each is described as a PIXELFORMATDESCRIPTOR of 40 bytes, version 1: `dwFlags` PFD_DRAW_TO_WINDOW and PFD_SUPPORT_OPENGL, with PFD_DOUBLEBUFFER and PFD_SWAP_COPY for the double-buffered (the back buffer is kept at a swap), never PFD_GENERIC_*; `iPixelType` PFD_TYPE_RGBA; `cColorBits` 16 for 565 and 32 for 8888, as Windows' own formats on a 32-bit desktop give it (a request for 24 still finds ours first); red, green and blue bits 5, 6, 5 at shifts 11, 5, 0, or 8, 8, 8 at 16, 8, 0; alpha 8 bits at shift 24 for ARGB, else none; `cDepthBits` 16 or 24, `cStencilBits` 8 with D24S8; `iLayerType` PFD_MAIN_PLANE; no accumulation, aux, stereo or overlay planes, no colour index, no bitmaps (a memory DC has no window, so the ICD takes none, and Windows' software path draws it).

## Windows, contexts and threads

### Windows

A window whose pixel format the ICD set is an entry of the process's 32 (OGL_WINDOWS), with the drawable the device keeps for it.

- **DrvSetPixelFormat(hdc, n)**: the card; `n` from 1 to the count at the desktop's depth now; the DC's window (WindowFromDC: a memory DC has none, and is refused). A window that has the format already answers TRUE. A window that has another: refused while a context draws into it (Windows sets a window's format once), else its entry is taken as a new window's (Windows 9x soon gives a gone window's HWND to a new one), its drawable closed. Otherwise a free entry, or failing that one whose window is gone (IsWindow), its drawable closed; with none, FALSE. An entry that takes a format starts with a swap interval of 1 ([WGL_EXT_swap_control](#wgl_ext_swap_control)).
- **The drawable** is made on the device when a context is first made current on the window, at the window's client size (each side at most 2048; a window with no client area, minimized, keeps the drawable it has, or gets one pixel), and resized when a later look finds another client size ([The device's objects](#the-devices-objects-and-their-ids)).

### Contexts

The process's table holds 64 contexts (OGL_CONTEXTS); a context's handle (DHGLRC) is its slot plus 1.

- **DrvCreateContext(hdc)**, **DrvCreateLayerContext(hdc, layer)**: layer 0 alone (no overlay or underlay planes); the card; a window with a pixel format the ICD set (else 0); a free slot. The context gets a 64 KB batch of its own, a new share group of its own, IDs for both, and GL's state as GL 1.1 starts it (`state.c`). It draws only into windows of its window's drawable format.
- **DrvSetContext(hdc, rc, set_table)** (wglMakeCurrent): refused (NULL) for an unknown handle, a DC without a window of the ICD's, a window of another drawable format, or a context current on another thread. The thread's context before, if another, has its batch sent and is let go. The context becomes the thread's current one, on the DC's window; the window's front image is marked as not holding the window's pixels ([The front buffer](#the-front-buffer)); its group, itself and the window's drawable are opened, resized and bound as needed. The first time a context is made current, GL's viewport and scissor box take the window's size (GL 1.1). It returns the table of the 336 functions; `set_table` is not called.
- **DrvReleaseContext(rc)**: for the calling thread's own context, its batch sent and the context let go; otherwise FALSE.
- **DrvDeleteContext(rc)**: refused for a context current on another thread (`opengl32.dll` refuses that first); current on the calling thread, it is let go first. Its batch is sent, its CONTEXT closed on the device, and with its group's last context the group closed too, its lists and textures freed. With the process's last context, every window's drawable is closed.

### Threads and the current context

The current context is the thread's, in a TLS slot. While only one thread has made a context current, it is also kept in a plain variable, so a GL call finds it without a call into KERNEL32; once a second thread makes one current, every GL call reads TLS. The `Drv*` exports always read the calling thread's own slot, as any thread may call them. A GL call with no current context does nothing.

### The exports' lock

The exports that change the ICD's tables take turns under a lock word of the ICD's (an atomic exchange; a waiter yields with `Sleep(0)` sixteen times, then `Sleep(1)`): DrvSetPixelFormat, DrvCreateContext and DrvCreateLayerContext, DrvDeleteContext, DrvSetContext, DrvReleaseContext, DrvCopyContext, DrvShareLists and DrvSwapBuffers (DrvSwapLayerBuffers through it). A thread's detach only tries it ([A per-process DLL](#a-per-process-dll)). GL's entry points do not take it: each runs on its thread's own context. The objects made when first needed, which GL's entry points may make too (the process's DirectDraw object, the windows' clippers, the staging surface, the display's DC for the cursor's escape), are made one thread at a time under a second lock word of their own, never the exports'.

### Every export

| Export | What it does |
|---|---|
| DrvValidateVersion(version) | TRUE, for any version (Windows 98's `opengl32.dll` passes the escape's, or nothing) |
| DrvSetCallbackProcs(n, procs) | Nothing: the ICD keeps its own current context per thread |
| DrvDescribePixelFormat(hdc, n, bytes, pfd) | GDI's DescribePixelFormat: the count of formats at the desktop's depth now (0 without the card, and at 8 bpp); with `pfd`, `n` one of them and `bytes` at least 40, the description written; with `pfd` and `n` not one of them, 0 |
| DrvSetPixelFormat(hdc, n) | [Windows](#windows) |
| DrvCreateContext(hdc), DrvCreateLayerContext(hdc, layer) | [Contexts](#contexts) |
| DrvDeleteContext(rc), DrvSetContext(hdc, rc, set_table), DrvReleaseContext(rc) | [Contexts](#contexts) |
| DrvShareLists(a, b), DrvCopyContext(src, dst, mask) | [Share groups](#share-groups-wglsharelists-and-wglcopycontext) |
| DrvSwapBuffers(hdc) | [SwapBuffers](#swapbuffers) |
| DrvSwapLayerBuffers(hdc, planes) | With the main plane's bit (1, WGL_SWAP_MAIN_PLANE), DrvSwapBuffers; else FALSE |
| DrvGetProcAddress(name) | An offered extension's function, by its exact name; NULL for any other name, and without the card ([DrvGetProcAddress](#drvgetprocaddress)). `opengl32.dll` answers GL 1.1's own names itself |
| DrvDescribeLayerPlane, DrvSetLayerPaletteEntries, DrvGetLayerPaletteEntries, DrvRealizeLayerPalette | 0: there are no layer planes |

`wglUseFontBitmaps` and `wglUseFontOutlines` are `opengl32.dll`'s own: it builds them from the ICD's display lists, bitmaps and polygons through the table.

## Share groups, wglShareLists and wglCopyContext

A **share group** holds display lists and texture objects: every context starts in one of its own; the device holds the group's textures by the group's ID. The default textures (name 0) of 1D and 2D, and the proxies, are each context's own; at a context's close its default textures' IDs go back to the group, and the device is told (TEXDELETE) with the context's last batch, as the group may outlive it.

- **DrvShareLists(a, b)** (wglShareLists): the second context takes the first's group, which GL allows only while the second has objects of none of its own. It is refused unless both are contexts; contexts already in one group answer TRUE. Refused too while the second's group has another context, any list or texture name, or a default texture with an image on the device. Otherwise the second's batch is sent; its group, if the device has it, is closed there (which ends the second context on the device with it); the group is freed; the second context joins the first's group, and is opened again on the device in it at its next use, its registers sent whole.
- **DrvCopyContext(src, dst, mask)** (wglCopyContext): the attribute groups of GL's `mask` (as `glPushAttrib` names them) are copied from the first context's state into the second's, which no thread may have current and which may not be between `glBegin` and `glEnd`; a context into itself is refused; FALSE without memory.

## The device's objects and their IDs

The device keeps three kinds of object for the ICD ([gpu-commands.md](gpu-commands.md), Command set 2.5): **share groups**, **contexts** (their registers, their group, the drawable they are bound to) and **drawables** (one per window: its front, back and depth/stencil images).

- **IDs** are the ICD's: the process's ID folded into the high 16 bits (`(pid >> 16) ^ (pid & FFFFh) ^ (pid >> 7)`, 9898h if that is 0), a count of the process's new objects in the low 16 bits (never 0). They are unique among the device's live objects as long as two live processes do not fold to the same high half (the 16-bit process hash is kept). Texture IDs are each share group's own (below).
- **When they are opened.** The group, the context and the window's drawable when the context is made current (and by the next command that needs them, should one be missing), recorded into the context's batch ahead of everything that names them.
- **Resized.** The window's client size is looked at again when a context is made current, and by every command that asks for the context's objects: `glClear`, a pixel rectangle drawn or read, a texture's image or parameters sent, `glGetTexImage`. A new size sends DRAWABLE RESIZE; the images are made anew, cleared, as GL allows. A primitive and a swap do not look (only when the drawable is missing), so a swap goes at the size its frame was drawn at and the new size comes at the next `glClear`.
- **Bound.** BIND names the drawable the context draws into, sent when it changes.
- **Closed.** A context at DrvDeleteContext; its group with the group's last context; a drawable with the process's last context, or when its window's entry is taken for a new window ([Windows](#windows)); all of them at the DLL's detach. The device takes back the least recently named object of a kind that is full, so the objects of a process that ended without its detach are reclaimed ([The OpenGL device](#objects)).

## Batches and the ring

The ICD drives the command ring with the DirectX DLL's own code, `driver32/gpu.c`, built in with `VGPU98_ICD_DLL` and `opengl/gpuenv.h`: the same ring lock, packets, fences, waits, takeovers and recoveries ([directx-hal.md](directx-hal.md), The 32-bit DLL), the ring chosen at the card's finding by the DirectX DLL's rule (the mini-VDD's ring in main memory when the GPU can read it, with the status block when the GPU can write it; else 128 KB in video memory). What instances of that code must agree on is in the shared block or the GPU's registers. The ring's lines go to the host's log after the ICD's own prefix ([Diagnostics](#diagnostics)); its counters are kept, never shown. The ICD's batches draw for no Direct3D context.

- **The batch**. Each context records into a buffer of its own, 64 KB (16384 dwords, OGL_BATCH_DWORDS): each a packet header and its payload ([gpu-commands.md](gpu-commands.md), Command set 2.5). It goes into the ring as one, under the ring lock, ended by a FENCE: when it fills; at a swap and at every SHOW of the front buffer; at `glFlush`, `glFinish` and every read-back (each band of `glReadPixels`, `glGetTexImage` and the copies, waited for); and when its context is let go, deleted, or replaced on its thread by another context made current. No packet passes F000h bytes (VGPU98_OGL_PACKET_BYTES); a packet longer than a batch is not recorded.
- **A held GPU.** While the display driver or the mini-VDD holds the GPU (a mode change, a full-screen DOS session), a batch waits, looking every 5 ms, instead of being dropped: a GL command is never lost. Inside the DLL's detach nothing waits for a held GPU or a fence: unsent batches are dropped, and the CLOSEs go only if the GPU can be had at once ([The process's end](#mode-changes-dos-sessions-gpu-restarts-and-the-processs-end)).
- **The ICD's patience**. A wait (for a fence, ring room or idle) calls the GPU stuck and restarts it when neither FENCE nor RING_HEAD has moved for **5 seconds**, not the DirectX DLL's one: `opengl/gpuenv.h` defines NO_PROGRESS_MS (5000) and NO_PROGRESS_TEXT ("5 seconds"), which `driver32/gpu.c` takes from a DLL's environment that defines them, else 1000 and "a second" (the DirectX DLL and the Glide DLLs, their limit and lines unchanged). The OpenGL device may spend over a second inside one packet making a pipeline that is not in the pipeline cache (1.2 s after a release changed the shaders), and a restart of the GPU can lose a batch. The device's warm-up makes the shaders' first pipeline ahead ([Pipelines, the uniform block and the data chunks](#pipelines-the-uniform-block-and-the-data-chunks)); the 5 seconds cover one it did not make, on a slower host GPU. A real hang in an OpenGL program takes 5 seconds to recover. The ring's line then says `made no progress for 5 seconds`. The same limit bounds how long a GPU found running another ring is let finish before the ICD moves it to its own (a second in the DirectX DLL). The DirectX DLL and the Glide DLLs keep their second: one of them waiting on the ring while the OpenGL device makes a pipeline cold still restarts the GPU after a second, and the display driver's wait before GDI draws stops waiting after about a second.
- **A GPU restart.** When the shared block's `state_epoch` has moved since the context's last batch went into the ring, or moved while it went, or that batch was cut short by a GPU that stopped (the rest of it is lost with the GPU), the next batch first sends the context's objects again, those it had opened: GROUP OPEN, CONTEXT OPEN, its bound window's DRAWABLE OPEN at its size and format and BIND, then every register as the device had it before the batch, in one STATE of all the device's registers (495 on a device of command set 2.6, 433 on a 2.5 one), and then the context's shared palette in a PALETTE when it is not the starting one (it is the context's state too: [GL_EXT_paletted_texture and GL_EXT_shared_texture_palette](#gl_ext_paletted_texture-and-gl_ext_shared_texture_palette)). This happens only once a batch of the context with its objects has gone. The device keeps its objects across restarts ([Resets](#resets)), so those OPENs change nothing there unless something was lost. What a cut batch carried beyond that is lost, its texture uploads among them ([What is not offered](#what-is-not-offered)).
- **The pending DRAW.** Consecutive primitives of one kind share a DRAW being built ([Drawing](#drawing)): it goes into the batch before any other command, when the next primitive needs another control word (unit 1's coordinates or none among it), or when it is full (768 vertices, 640 with unit 1's coordinates, or 4096 indices).
- **The registers as they change**. The ICD keeps the device's registers three times: as it has them, as sent (recorded into a batch), and as the device had them before the batch being recorded (what goes again after a restart). The device's registers are command set 2.6's 495 on a device whose COMMAND_SET reads 2.6 or later, else 2.5's 433: no register past 2.5's is changed or sent to an older device, which would drop the STATE ([gpu-commands.md](gpu-commands.md), Command set 2.6). A state call changes the registers its state touches and widens the range changed; before the next command that draws (a primitive, a clear, a pixel rectangle, a swap, a load or a show), the range goes as STATEs of the registers that differ from those sent, each from a changed register to a changed register: changed registers less than 16 apart share a STATE (the unchanged ones between them go in it), and one 16 or more past the last changed one starts a STATE of its own (unit 1's block lies past the rest). A context's registers go whole after its open: each is marked changed against a sent copy of FFFFFFFFh, so its first STATEs run over every register that differs from that (the device's own defaults have the polygon stipple's all-ones rows).

## GL's state in the ICD

The ICD keeps all of GL 1.1's state, laid out by GL's attribute groups (`state.h`): everything `glGet*` answers, the attribute stacks save and display lists change. All 336 functions do what GL 1.1 says:

- **Errors**: one flag each, the oldest set first out at `glGetError`; most commands are GL_INVALID_OPERATION between `glBegin` and `glEnd`.
- **Queries**: every value of GL 1.1's state tables by `glGetBooleanv`, `glGetIntegerv`, `glGetFloatv` and `glGetDoublev`, converted by section 6.1.2's rules; `glIsEnabled`; the `glGet*` of lights, materials, maps, pixel maps, texture parameters and levels, the texture environment and generation, clip planes and the polygon stipple. A texture unit's state answers for the active unit, a texture coordinate array's for the client unit ([Multitexture](#multitexture-gl_arb_multitexture-and-gl_sgis_multitexture)).
- **The attribute stacks**: `glPushAttrib` and `glPopAttrib` with every group (16 deep), GL_TEXTURE_BIT saving both texture units' state with their bound textures' names and parameters, the active unit and the shared palette's enable, its pop binding them again; GL_POINT_BIT the point parameters too; `glPushClientAttrib` and `glPopClientAttrib` for the pixel store and the vertex arrays (16 deep), GL_CLIENT_VERTEX_ARRAY_BIT with both units' coordinate arrays, the client unit, SGIS's coordinate set and the compiled vertex arrays' locked range.
- **The matrix stacks**: modelview 32, projection 2, texture 2 for each texture unit (the active unit's addressed); the modelview's inverse transpose computed again before it is next needed after a change.
- **What GL fixes when a command is called, the ICD computes then**: light positions and spot directions through the modelview at the call, clip planes and eye planes through the modelview's inverse at the call, all kept in eye coordinates.
- **Display lists** (`lists.c`), per share group: compiling and executing, nesting to 64, `glCallLists` by every type, and the commands GL 1.1 executes at once instead of compiling. A command is recorded in a canonical form (every variant as its one float or integer form, client data as it was unpacked at compile time), and executing a list calls each command's entry point again with recording off, so its rules and errors apply when the list runs. Lists stay in the ICD (keeping them on the host is deferred).
- **Vertex arrays**: an element read when the call is made, through the current-value commands then the vertex (`glArrayElement`, `glDrawArrays`, `glDrawElements`, `glInterleavedArrays`), so a list compiling the call keeps the values; each unit's coordinate array feeds that unit. `glDrawArrays` and `glDrawElements` go straight into the DRAW where they can, each element once a call, with the same result ([Arrays drawn directly](#arrays-drawn-directly)); under a compiled vertex arrays' lock the vertex array is read from its conversion ([GL_EXT_compiled_vertex_array](#gl_ext_compiled_vertex_array)).
- **Evaluators**: one- and two-dimensional maps (order at most 30), `glEvalCoord`, `glEvalPoint`, the grids and `glEvalMesh`, automatic normals; an evaluated vertex carries its evaluated values without changing the current ones. Evaluated texture coordinates feed unit 0, as `glTexCoord` does; unit 1 takes its current coordinates.
- **The pixel store, transfer and maps**: the unpack and pack states (swap bytes, LSB first, row length, skip rows and pixels, alignment), scale and bias, the ten pixel maps of 256 entries at most, index shift and offset.
- **Render modes**: GL_RENDER; GL_SELECT (the name stack, 64 deep, and its hit records) and GL_FEEDBACK (every type and token, the texture coordinates unit 0's, `glPassThrough`), through the ICD's own vertex path ([below](#the-icds-own-vertex-path)).

## Drawing

### Primitives on the device's path

`glBegin` and `glEnd` gather each vertex with the current colour, normal and each texture unit's coordinates. At `glEnd` a primitive in GL_RENDER that needs none of the ICD's own path goes to the device as it was given: object coordinates, the current colour as given, the normal, unit 0's texture coordinates ([gpu-commands.md](gpu-commands.md), DRAW's vertex), and while unit 1 draws with a texture (its TEXTURE_ID register not 0) unit 1's too, in vertices of 20 floats with DRAW_TEX1 (command set 2.6); otherwise 16. The device transforms and lights it. Before it the window's front image is loaded if the primitive draws into the front buffer ([The front buffer](#the-front-buffer)), and the registers changed go.

The DRAW builder takes its vertices from a **vertex source**: their count, a mark for each, and each vertex's values in DRAW's layout. A vertex whose mark is in the DRAW already is not put there again, its index reused. `glBegin`'s vertices are marked by their place in the primitive, so each is put in once; the arrays drawn directly mark theirs by element ([below](#arrays-drawn-directly)). The marks start again with each primitive: a later primitive's vertices are new to the DRAW. A primitive's indices make points, lines or triangles. Each triangle is a rotation of GL's order, so its winding is kept, with GL's provoking vertex first (GL 1.1, table 2.2), as Vulkan's flat shading takes the first; each line keeps GL's order, its provoking vertex second:

| GL's mode | The device's kind | Indices (vertex numbers of the primitive) |
|---|---|---|
| GL_POINTS | POINTS | Each vertex |
| GL_LINES | LINES | (0, 1), (2, 3), … |
| GL_LINE_STRIP | LINES | (0, 1), (1, 2), … |
| GL_LINE_LOOP | LINES | The strip's, then (n − 1, 0) |
| GL_TRIANGLES | TRIANGLES | (2, 0, 1), (5, 3, 4), … |
| GL_TRIANGLE_STRIP | TRIANGLES | Triangle i: (i + 2, i, i + 1), every odd one (i + 2, i + 1, i) |
| GL_TRIANGLE_FAN | TRIANGLES | Triangle i: (i + 1, 0, i), from i = 1 |
| GL_QUADS | TRIANGLES | Each quad split along the diagonal from its provoking vertex, the fourth: (3, 0, 1), (3, 1, 2) |
| GL_QUAD_STRIP | TRIANGLES | Quad j is 2j, 2j + 1, 2j + 3, 2j + 2; 2j + 3 provokes: (2j + 3, 2j + 2, 2j), (2j + 3, 2j, 2j + 1) |
| GL_POLYGON | TRIANGLES | A fan from its first vertex, which provokes: (0, i, i + 1) |

A primitive larger than a DRAW (768 vertices of 16 floats or 640 of 20, 4096 indices) is split across DRAWs, every triangle kept.

### Arrays drawn directly

`glDrawArrays` and `glDrawElements` build the DRAW straight from the arrays, without `glBegin` and `glEnd`, when all of these hold (whether or not an extension is offered):

- the render mode is GL_RENDER, and no list is being compiled (GL_COMPILE_AND_EXECUTE included);
- the vertex array is enabled and has a pointer;
- a primitive of the mode takes the device's path: for triangles, quads and polygons both faces' polygon modes GL_FILL, and no line strip or loop while line stipple is on;
- the count is from 1 to 100000h, `glDrawArrays`' last element within GL's int range, and every index of `glDrawElements` below 100000h.

Each vertex then takes its values from the arrays enabled, else from the current values: the position from the vertex array (from the lock's conversion while it applies: [GL_EXT_compiled_vertex_array](#gl_ext_compiled_vertex_array)), the colour, the normal and each unit's texture coordinates, an array's element converted as GL takes it (a vertex's missing z and w 0 and 1, a texture coordinate's t, r and q 0, 0 and 1, a colour's alpha 1, colours and normals of integer types normalized). **Each array element is sent once a call**: `glDrawArrays`' vertices are each its own element, and `glDrawElements`' are marked by index, so an index repeated is one vertex of the DRAW (and of each DRAW a split makes). The points, lines and triangles are the table's above, so the result is what `glBegin` and `glEnd` would draw. After the call the current values (the edge flag, each unit's texture coordinates, the colour, with colour material's tracking, the index and the normal) are its last element's, from the arrays enabled, as the calls through `glBegin` would leave them.

Otherwise the call goes through `glBegin`, each element through the current-value commands and then the vertex, and `glEnd`; so does a call when the DRAW builder cannot make room for its marks (6 bytes a mark, up to the largest element). Without the vertex array nothing is drawn, and the current values are still set.

### The ICD's own vertex path

A C copy of GL 1.1's per-vertex operations (`raster.c`): transform, lighting by GL's equation (colour material and two-sided lighting included), each texture unit's coordinate generation and texture matrix, clipping against the view volume and the six user planes (Sutherland-Hodgman; an edge's flag goes with the vertex that starts it, and an edge clipping makes is a boundary; at most 512 vertices through the clip, what does not fit dropped), and the viewport. It serves:

- **`glRasterPos`**: its valid bit (the point clip), window position, clip w, colour, each unit's texture coordinates and distance (from the eye's origin); a valid raster position is a selection hit;
- **GL_SELECT and GL_FEEDBACK**, for every primitive, bitmap and pixel rectangle;
- in GL_RENDER, the primitives the device cannot draw by GL's rules:
  - polygon modes GL_LINE and GL_POINT (each polygon culled by its facing in window coordinates and coloured by it, the face's polygon mode drawing its flagged edges or vertices; the stipple counter starts at a polygon's first edge and goes on along the rest);
  - `glMaterial` between `glBegin` and `glEnd` with lighting on (each vertex lit with the material in force at it);
  - stippled line strips and loops (the stipple counter runs on across segments).

What goes through it reaches the device in a DRAW with DRAW_CLIP: clipped clip coordinates, final colours (flat shading's provoking colour on every vertex, a back face's colour as its front), unit 0's final texture coordinates (and unit 1's with DRAW_TEX1, while it draws with a texture) and the fog distance; lines with DRAW_STIPPLE_ON and each vertex's stipple counter (the line's start and the fragments along its major axis before it); a polygon's edges and vertices with DRAW_POLYGON, so polygon offset applies to them. Its polygons go as fans of triangles from their first vertex.

## Textures

- **Names and IDs.** Texture names are the share group's (the defaults, name 0, and the proxies the context's own; a default texture is both units' when both are bound to name 0). `glBindTexture`, the images, the parameters and their queries address the active unit's bound texture; `glDeleteTextures` binds name 0 again on whichever unit had the texture bound. A texture gets a device ID at its first image: the lowest free of its group's own 16383 (1 to 16383, a bit each, given back at `glDeleteTextures`, so an ID never wraps onto a live texture); with none left the texture stays off the device and is not drawn with. TEXPARAM goes with the new ID and at every parameter change: wraps (GL_REPEAT, GL_CLAMP, and on a device of command set 2.7 GL_CLAMP_TO_EDGE: [Clamp-to-edge](#clamp-to-edge-gl_ext_texture_edge_clamp-and-gl_sgis_texture_edge_clamp)), filters and the border colour; with it, the texture's palette when it is not the starting one ([below](#gl_ext_paletted_texture-and-gl_ext_shared_texture_palette)). TEXDELETE goes at `glDeleteTextures`; the group's close takes all of its textures with it.
- **Images**. Every image is unpacked by the unpack state, through the pixel transfer path, into RGBA8 (red in the low byte) with its base format's components, 8 bits each (a sized format gets 8 bits, which GL allows, and `glGetTexLevelParameter` reports 8 for each of the base format's components): alpha as 0, 0, 0, A; luminance as L, L, L, FFh; luminance-alpha as L, L, L, A; intensity as I, I, I, I; RGB as R, G, B, FFh; RGBA as it is. An index level (GL_EXT_paletted_texture) keeps 8-bit indices instead, a byte a texel ([below](#gl_ext_paletted_texture-and-gl_ext_shared_texture_palette)). Unsigned bytes of GL_RGBA, GL_RGB, GL_LUMINANCE, GL_LUMINANCE_ALPHA or GL_ALPHA with a transfer path that changes nothing (no colour map, scales 1, biases 0), outside a list being compiled, go straight to texels without the floats of the general path. Borders are included; 1D images are one row high. A level goes as TEXIMAGE DEFINE with its texels when they fit one packet (15353 dwords at most: a texel each, or an index level's bytes four a dword, each row padded to a dword), else DEFINE with its size alone and then its rows, as many a packet as fit; `glTexSubImage*` sends its rectangle's rows likewise, at its place in the level (GL's border at −1 is the level's 0). A compiled `glTexImage*` keeps the unpacked image and sends it when the list runs.
- **Sizes**: each side 2^n plus twice the border (0 or 1), at most 2048 without the border, levels 0 to 11.
- **Completeness** is the ICD's: level 0 defined, and for a mipmap filter every level down to 1x1, each half the last, with the same border and internal format (GL 1.1, section 3.8.9). Each unit's texture drawn with (register TEXTURE_ID for unit 0, unit 1's TEXTURE_ID in its block) is the unit's bound 2D texture when its 2D texturing is on, else its bound 1D one when its 1D texturing is on; 0 when it is incomplete or has no ID.
- **Residency and priorities**: every texture is resident (`glAreTexturesResident` and GL_TEXTURE_RESIDENT answer TRUE); priorities are kept and answered, and change nothing.
- **Proxies** answer whether a level would fit (each side at most 2048 without the border), are never sent, and set no error for a level that does not fit.
- **`glCopyTexImage*` and `glCopyTexSubImage*`** read the read buffer's rectangle ([Read-backs](#read-backs-and-the-staging-surface)), through the transfer path once, then define or replace the level; never an index level (GL_INVALID_OPERATION).
- **`glGetTexImage`** reads the level's texels from the device's copy, TEXREAD into the staging surface 256 rows at a time, each band waited for; the texels come back as GL gives them (luminance and intensity as red, no transfer path; an index level's through the palette it draws with) and are packed by the pack state. With GL_COLOR_INDEX, on a device of command set 2.6, an index level gives its indices themselves, one value a pixel, and a colour level is GL_INVALID_OPERATION; on a 2.5 device GL_COLOR_INDEX is GL_INVALID_ENUM, as in GL 1.1.

## Pixel rectangles

Images are unpacked when the call is made (a list keeps them unpacked) and drawn through the transfer path when it runs.

- **`glDrawPixels`** at a valid raster position (else nothing): colour images as RGBA8, depth images as floats through the depth scale and bias, stencil images as bytes through the index shift, offset and map (a depth image without a depth buffer, or a stencil image without a stencil buffer, is GL_INVALID_OPERATION). It goes as PIXELS, a packet of at most 15338 data dwords each, each a rectangle of the image at its own place (the raster position plus the zoom times its first column and row), with the raster position's z, colour, unit 0's texture coordinates and distance and `glPixelZoom`'s factors, and while unit 1 draws with a texture its raster texture coordinates too (PIXELS_TEX1, command set 2.6). `glBitmap`'s PIXELS likewise. The device draws its fragments through the per-fragment operations ([Pixel rectangles on the device](#pixel-rectangles-on-the-device)).
- **`glBitmap`**: its bits unpacked one byte a pixel (rows of whole dwords for the device), PIXELS of kind BITMAP at the raster position less its origin, unzoomed; the raster position then moves by the bitmap's move, in every render mode.
- **In GL_FEEDBACK and GL_SELECT** nothing is drawn: in feedback each gives its token (GL_DRAW_PIXEL_TOKEN, GL_BITMAP_TOKEN, GL_COPY_PIXEL_TOKEN) with the raster position's values; in selection a pixel rectangle is a hit at the raster position, a bitmap none (GL 1.1, appendix B).
- **`glCopyPixels`**: a read of the rectangle as it is, then drawn as an image, through the transfer path once.

### Read-backs and the staging surface

The device writes read-backs into a **staging surface** in video memory, and the ICD converts and packs them:

- **The surface**: one offscreen plain DirectDraw surface in video memory (DDSCAPS_OFFSCREENPLAIN, DDSCAPS_VIDEOMEMORY, 32-bit RGB), 2112 pixels wide (a 2048 level with its border, OGL_STAGING_STRIDE 8448 bytes a row) and 256 rows (OGL_STAGING_ROWS), made once a process at the first read-back through the process's DirectDraw object (normal cooperation). At every read-back a Lock (DDLOCK_WAIT) finds where it is now, and it is restored once if a mode change lost it; it is taken only as one run of video memory at or above the block's `fb_linear`, its pitch 8448 and its 256 rows inside video memory. Without it, reads give zeros.
- **`glReadPixels`**: from the read buffer (front or back by GL_READ_BUFFER), the depth buffer or the stencil buffer (GL_INVALID_OPERATION without it, and for colour indices); READ in bands of up to 256 rows and as many columns as a row of the surface holds, each band followed by `glFinish`'s wait. The rows come from the bottom, GL's order. Colour 565 becomes red/31, green/63, blue/31 and alpha 1; 8888 (BGRA in memory) its channels/255, alpha only an ARGB drawable keeps (else 1); depth its integer over 65535 (D16) or 16777215 (D24S8); stencil its byte. Then the transfer path, and packing by the pack state. Reading the front buffer loads the window's pixels into the front image first.

## The front buffer

The drawable's images are on the host. The **front image mirrors the window's client area**:

- **Loads.** Before drawing into the front buffer (a primitive, a colour clear or a pixel rectangle while GL_DRAW_BUFFER names it; GL_FRONT is a single-buffered context's initial draw and read buffer, GL_BACK a double-buffered one's) or reading it, the front image takes the window's visible pixels from video memory with a SWAP of LOAD, unless it holds them already. It holds them after a load and after a swap (the front image is then the frame swapped); it does not after wglMakeCurrent of another context or onto another window (the same context made current again on the same window keeps its front drawing not shown yet), or after the drawable is made or resized. So a load comes at the first front drawing after wglMakeCurrent or a resize, not when the window's pixels change.
- **Shows.** Drawing into the front buffer marks it; a SWAP of SHOW (and its batch) goes at `glFlush`, at `glFinish` (then waited for), at a single-buffered window's SwapBuffers, and when a later drawing into it comes 16 ms or more after the last show. These shows never carry a swap interval, so they never wait for vertical blank.
- **`glDrawBuffer` and `glReadBuffer`** take every name; GL_FRONT_AND_BACK (and GL_LEFT) draw into both buffers, the device drawing each in a pass of its own ([What is not offered](#what-is-not-offered)). GL_NONE draws neither, and still updates depth and stencil. The right and aux buffers do not exist (GL_INVALID_OPERATION when none of the buffers named exists, as for the back buffers of a single-buffered window).

### SwapBuffers

**DrvSwapBuffers(hdc)** swaps the DC's window when the calling thread's current context draws into it; another thread's window answers FALSE, nothing done.

- **A single-buffered window**: the front buffer is shown (if drawn into) and the batch sent, never with a swap interval; TRUE.
- **A double-buffered window**:
  1. When the group, the context or the drawable is missing on the device, they are opened and bound (which looks at the client size); otherwise the client size is not looked at, and the new size comes at the next `glClear`.
  2. **At most two swaps in flight**: the swap waits for the one before last (the fences of the last two are kept for each window). With a swap interval, the device's waits for vertical blank hold the program to the refresh rate through this wait.
  3. **The cursor.** When there are rectangles to write, the ICD asks the display driver to keep the DIB engine's software cursor out of the client area (VGPU98_ESC_ACCESS with `begin` 1 and the client area on the screen, each edge kept to 0-7FFFh); the driver does when a cursor is shown over it or near it ([How Windows finds the ICD](#how-windows-finds-the-icd)). Otherwise the cursor's saved background, an old frame, would come back where the cursor moves away. A LOAD keeps the cursor out the same way, so the cursor's pixels are not taken into the front image.
  4. The registers changed go, then a SWAP of COPY and SHOW: the back image is copied into the front image, and the front image written to the window's visible rectangles. On a device of command set 2.6 or later, for a window whose client area is the whole screen and wholly visible (one visible rectangle, the screen) and when the cursor is not kept out (step 3), it carries the window's swap interval in INTERVAL, at most 4, none for 0; a window on the desktop swaps without one ([WGL_EXT_swap_control](#wgl_ext_swap_control)). It names the screen as the shared block has it (the primary surface at video memory's start, offset 0; the mode's pitch; its bits per pixel) and the client area's top-left corner on the screen (ClientToScreen).
  5. The batch goes. When the display driver kept the cursor out (it answered 1), the swap waits for its own SHOW, then lets the cursor back (`begin` 0), which draws it over the new frame; otherwise it does not wait, and two swaps can be in flight. SwapBuffers itself does not wait for vertical blank: with a swap interval the device waits for it between reading the front image back and writing it ([Swaps on the device](#swaps-on-the-device)); without one the frame is written as soon as the device takes the SWAP, as a windowed blit is. TRUE.
- **The visible rectangles** come from a DirectDraw clipper on the window (made once a window, through the process's DirectDraw object with DDSCL_NORMAL): its clip list (GetClipList, room for 256 rectangles), each cut to the client area and to the screen. A clip list the clipper does not give (it fails, or it does not fit) writes nothing, rather than over the windows covering it; without DirectDraw the whole client area, cut to the screen, is written. The rectangles are in screen coordinates; the device checks them against the screen it shows ([Swaps on the device](#swaps-on-the-device)).

## Mode changes, DOS sessions, GPU restarts and the process's end

GL 1.1 has no lost context: contexts and textures outlast mode changes, full-screen DOS sessions and GPU restarts, and end only at DrvDeleteContext, at the DLL's detach, and at a machine reset or the GPU_RESET of POST.

| Event | The ICD | The device |
|---|---|---|
| DrvDeleteContext | CONTEXT CLOSE (GROUP CLOSE with its group's last context; every DRAWABLE CLOSE with the process's last context) | The objects end; a group takes its contexts and textures with it |
| A mode change (the display driver's hold and RESET) | Its batches wait while the GPU is held. Its pixel formats follow the new depth; a context keeps its format | Every object is kept. A SWAP recorded before the change names a screen that is no longer shown, and writes nothing; while the desktop's depth is not the drawable's, LOAD and SHOW write nothing |
| A full-screen DOS session (the mini-VDD's hold and RESET) | Its batches wait | Every object is kept |
| A GPU restart (a driver starting the GPU, or restarting it after an error or a lack of progress) | The objects and registers go again with the next batch ([Batches and the ring](#batches-and-the-ring)) | Every object is kept; its arena copies and uniform blocks are made again when next needed |
| A window gone | Its drawable is closed when its entry is taken for a new window, with the process's last context, or at the DLL's detach | The drawable ends at its CLOSE, or is reclaimed when the drawables are full |
| A thread ending with a context current | The context let go; its batch sent if the exports' lock is free at once, else with the context's next flush | (nothing) |
| The DLL's detach: a FreeLibrary (DllMain's `reserved` NULL), or the process's end | Nothing waited for: each context's unsent batch dropped, its CONTEXT CLOSE (and its group's GROUP CLOSE) sent at once if the GPU can be had, then every window's DRAWABLE CLOSE. At the process's end only, the ring lock is let go first if a killed thread of the process held it (not at a FreeLibrary: another DLL of the process may hold it rightly) | The objects end; those whose CLOSE never came (a killed process, a held GPU) are taken back when their kind is full |
| A machine reset, or GPU_RESET at POST | (nothing) | Every drawable, group, context and texture ends |

## The identity and the limits

None of GL_VENDOR, GL_RENDERER and GL_VERSION contains a name that a period game matches on; GL_EXTENSIONS names the extensions offered ([The extensions](#the-extensions)).

| Query | Answer |
|---|---|
| GL_VENDOR | "VGPU98" |
| GL_RENDERER | "VGPU98 2000/PCI" |
| GL_VERSION | "1.1.0" |
| GL_EXTENSIONS | "GL_ARB_multitexture GL_EXT_compiled_vertex_array GL_EXT_paletted_texture GL_EXT_point_parameters GL_EXT_shared_texture_palette GL_EXT_texture_edge_clamp GL_SGIS_multitexture GL_SGIS_texture_edge_clamp WGL_EXT_swap_control " on a device of command set 2.7 or later; on a 2.6 device the same without the two `texture_edge_clamp` names; "" on an older one ([GL_EXTENSIONS](#gl_extensions)) |
| GL_MAX_TEXTURE_UNITS_ARB | 2, with GL_ARB_multitexture offered (else GL_INVALID_ENUM) |
| GL_MAX_LIGHTS | 8 |
| GL_MAX_CLIP_PLANES | 6 |
| GL_MAX_TEXTURE_SIZE | 2048 |
| GL_MAX_VIEWPORT_DIMS | 2048, 2048 |
| GL_SUBPIXEL_BITS | 4 |
| GL_MAX_MODELVIEW_STACK_DEPTH | 32 |
| GL_MAX_PROJECTION_STACK_DEPTH, GL_MAX_TEXTURE_STACK_DEPTH | 2 (the texture stack's each unit's) |
| GL_MAX_NAME_STACK_DEPTH | 64 |
| GL_MAX_ATTRIB_STACK_DEPTH, GL_MAX_CLIENT_ATTRIB_STACK_DEPTH | 16 |
| GL_MAX_LIST_NESTING | 64 |
| GL_MAX_EVAL_ORDER | 30 |
| GL_MAX_PIXEL_MAP_TABLE | 256 |
| GL_POINT_SIZE_RANGE, GL_POINT_SIZE_GRANULARITY | 1 to 64, 0.125 |
| GL_LINE_WIDTH_RANGE, GL_LINE_WIDTH_GRANULARITY | 1 to 16, 0.125 |
| GL_AUX_BUFFERS, GL_INDEX_BITS, GL_ACCUM_*_BITS | 0 |
| GL_RGBA_MODE, GL_INDEX_MODE, GL_STEREO | TRUE, FALSE, FALSE |
| GL_DOUBLEBUFFER | By the context's pixel format |
| GL_RED_BITS … GL_STENCIL_BITS | By the context's pixel format ([Pixel formats](#pixel-formats)) |

`glGetString` of another name is GL_INVALID_ENUM and NULL; between `glBegin` and `glEnd` it is GL_INVALID_OPERATION and NULL.

## The extensions

The ICD offers the OpenGL extensions Quake 2 uses: WGL_EXT_swap_control, GL_EXT_compiled_vertex_array, GL_SGIS_multitexture with GL_ARB_multitexture, GL_EXT_point_parameters, and GL_EXT_paletted_texture with GL_EXT_shared_texture_palette. An extension is offered once its pass is in: **all five passes are in, and all seven extensions are offered** on a device of command set 2.6 or later. It also offers GL_EXT_texture_edge_clamp and GL_SGIS_texture_edge_clamp, which games that know GL_CLAMP's rule ask for, offered on a device of command set 2.7 or later ([Clamp-to-edge](#clamp-to-edge-gl_ext_texture_edge_clamp-and-gl_sgis_texture_edge_clamp)): **nine extensions** on such a device. GL_VERSION stays "1.1.0", and nothing in the ICD is for the user to set.

**The extensions' own specification texts are not in the reference library.** Their rules here are taken without the texts: from the games that use them (what Quake 2, Quake 3 and GLQuake call, the token values they pass, where they look for a name), from Wine's generated GL header (`include/wine/wgl.h`, made from the Khronos registry) for the functions, their signatures and the token values, and from the extensions' well-known rules; where a rule was unclear, the games' use decided. Each extension's part below marks the rules taken so.

### Which are offered

The ICD reads the device's command set (COMMAND_SET, [gpu-registers.md](gpu-registers.md)) once a process, when it finds the card ([Finding the card](#finding-the-card)), and offers each extension only on a device whose command set draws what it adds: each extension has the least command set of a device that draws it (`ext.c`'s OGL_EXT_26 and OGL_EXT_27). The first seven need 2.6 (00020006h) ([gpu-commands.md](gpu-commands.md), Command set 2.6); GL_EXT_texture_edge_clamp and GL_SGIS_texture_edge_clamp need 2.7 (00020007h) ([gpu-commands.md](gpu-commands.md), Command set 2.7). So a device of 2.7 or later is offered all nine; a 2.6 device (an older 86Box) the seven alone: its GL_EXTENSIONS has neither edge-clamp name, `glTexParameter*` refuses GL_CLAMP_TO_EDGE (GL_INVALID_ENUM), and no TEXPARAM carries wrap value 2, which a 2.6 device drops. An older 86Box (command set 2.5) gets GL 1.1 alone: GL_EXTENSIONS is "", DrvGetProcAddress gives NULL for every name, the extensions' queries (the locked range, the active and client units, the unit count, the point parameters, GL_TEXTURE_INDEX_SIZE_EXT), the shared palette's enable and `glGetTexImage` of GL_COLOR_INDEX are GL_INVALID_ENUM, the index internal formats are not internal formats (GL_INVALID_VALUE, as any other), no SWAP carries a swap interval, and no STATE, DRAW, PIXELS, TEXIMAGE or PALETTE carries what 2.6 adds (a 2.5 device drops them all): the ICD keeps 2.5's 433 registers there ([Batches and the ring](#batches-and-the-ring)), and unit 1, which no call can reach, never draws. Without the card nothing is offered. The arrays drawn directly ([Arrays drawn directly](#arrays-drawn-directly)) are not an extension, and are drawn so on either device.

The extensions' token values are in `glenum.h`, in a section of their own after GL 1.1's, kept apart by extension. EXT_paletted_texture's, which a 1.1 header carries, stay with that header's other extension values, all but TEXTURE_INDEX_SIZE_EXT. SGIS_multitexture's two units are at the values Quake 2 and GLQuake pass (835Eh and 835Fh; the registry's later revision numbers them otherwise); its SELECTED_TEXTURE_SGIS is not there, as no source in the library gives its value. GL_CLAMP_TO_EDGE (812Fh), the edge-clamp extensions' one token, has a part of its own there, after EXT_point_parameters'. SetLastError's ERROR_INVALID_DATA (13, winerror.h's) is in `icd.h`.

### GL_EXTENSIONS

`glGetString(GL_EXTENSIONS)` gives one string, built when the card is found: the names of the extensions offered, from a table in `ext.c`, each followed by a space (the last too), in the table's fixed order: GL_ARB_multitexture, GL_EXT_compiled_vertex_array, GL_EXT_paletted_texture, GL_EXT_point_parameters, GL_EXT_shared_texture_palette, GL_EXT_texture_edge_clamp, GL_SGIS_multitexture, GL_SGIS_texture_edge_clamp, WGL_EXT_swap_control. On a device of 2.7 or later all nine are offered, and it is `"GL_ARB_multitexture GL_EXT_compiled_vertex_array GL_EXT_paletted_texture GL_EXT_point_parameters GL_EXT_shared_texture_palette GL_EXT_texture_edge_clamp GL_SGIS_multitexture GL_SGIS_texture_edge_clamp WGL_EXT_swap_control "` (222 characters). On a 2.6 device the seven of 2.6, `"GL_ARB_multitexture GL_EXT_compiled_vertex_array GL_EXT_paletted_texture GL_EXT_point_parameters GL_EXT_shared_texture_palette GL_SGIS_multitexture WGL_EXT_swap_control "` (169 characters); otherwise "".

- **Each name followed by a space**: GLQuake looks for `"GL_SGIS_multitexture "`, with its space.
- **Under 1 KB**: the string is built in 512 bytes (511 characters at most), well under the 1 KB some games copy it into.
- **WGL_EXT_swap_control in it**: a WGL extension, but Quake 2 and Quake 3 look for its name in GL_EXTENSIONS, and the ICD has no other list for it (WGL_ARB_extensions_string is not offered: [What is not offered](#what-is-not-offered)).

### DrvGetProcAddress

`opengl32.dll` answers `wglGetProcAddress` for GL 1.1's own names itself and passes other names to DrvGetProcAddress. DrvGetProcAddress finds the card first (without it, NULL), then looks the name up in `ext.c`'s table of the extensions' functions: a name that matches one exactly, every character in its case, gives that function if its extension is offered. Every other name gives NULL: NULL itself, a name in another case, a name longer or shorter than one in the table, GL 1.1's own names, a function of an extension not offered. The table:

| Functions | Extension |
|---|---|
| `wglSwapIntervalEXT`, `wglGetSwapIntervalEXT` | WGL_EXT_swap_control |
| `glLockArraysEXT`, `glUnlockArraysEXT` | GL_EXT_compiled_vertex_array |
| `glPointParameterfEXT`, `glPointParameterfvEXT` | GL_EXT_point_parameters |
| `glColorTableEXT`, `glColorSubTableEXT`, `glGetColorTableEXT`, `glGetColorTableParameterivEXT`, `glGetColorTableParameterfvEXT` | GL_EXT_paletted_texture |
| (none: its target and enable go through GL_EXT_paletted_texture's calls and `glEnable`) | GL_EXT_shared_texture_palette |
| `glActiveTextureARB`, `glClientActiveTextureARB`, and the 32 `glMultiTexCoord`n`{d,f,i,s}ARB` and `glMultiTexCoord`n`{d,f,i,s}vARB` (n 1-4) | GL_ARB_multitexture |
| `glSelectTextureSGIS`, `glSelectTextureCoordSetSGIS`, `glMultiTexCoordPointerSGIS`, `glMTexCoord2fSGIS`, `glMTexCoord2fvSGIS`, and the 32 `glMultiTexCoord`n`{d,f,i,s}SGIS` and `glMultiTexCoord`n`{d,f,i,s}vSGIS` | GL_SGIS_multitexture |
| (none: their one token, GL_CLAMP_TO_EDGE, goes through `glTexParameter*`) | GL_EXT_texture_edge_clamp, GL_SGIS_texture_edge_clamp |

Each offered extension gives every function Wine's generated header lists for it, as Quake 3 stops with a fatal error at a NULL one (the test `ext` checks each list against GL_EXTENSIONS).

The functions are not among the 18 exports ([The export list](#the-export-list)): a program reaches them through `wglGetProcAddress` alone. Each finds the calling thread's current context as the 336 entries do, and does nothing without one.

### WGL_EXT_swap_control

The rules here are taken without the extension's text. Quake 2's `gl_swapinterval` is 1 by default, so it turns vertical sync on as soon as the extension is offered.

- **`BOOL wglSwapIntervalEXT(int interval)`**: the current context's window swaps after `interval` vertical blanks (0: at once); TRUE. A negative interval is refused: FALSE, `SetLastError(ERROR_INVALID_DATA)`, the interval kept as it was. With no current context, FALSE, and no error is set.
- **`int wglGetSwapIntervalEXT(void)`**: the current context's window's interval as it was set, above 4 too; 0 with no current context.
- **The window's.** The interval is kept in the window's entry ([Windows](#windows)), not in the context: contexts drawing into one window share it, and each window has its own. An entry starts at 1 (the extension's default) when it takes a pixel format; setting the same format again keeps the interval (for a new window with a gone window's HWND too), and an entry taken as a new window's starts at 1 again.
- **The swaps that carry it.** A double-buffered window's SwapBuffers alone, and only when the window's client area is the whole screen, all of it visible (its top-left corner at 0, 0 on the screen, the drawable the mode's width and height, and its one visible rectangle the whole screen), and the swap does not keep the cursor out of the window ([SwapBuffers](#swapbuffers), step 3): its SWAP of COPY and SHOW then carries the interval in INTERVAL, at most 4 (a larger one goes as 4, the device's limit, as Glide's SWAP and FLIP have it), nothing for 0. A window on the desktop, or a full-screen one with a dialog or pop-up over part of it, swaps at once, as a windowed Direct3D Blt does: the device waits for the vertical blank in the command processor, which every later packet waits behind, and GDI waits for the last fence issued before the DIB engine draws ([directx-hal.md](directx-hal.md), GDI and the GPU), so a windowed program's waits would let GDI draw once a frame (the visible rectangle). A swap that keeps the cursor out would keep it hidden through the wait; so a full-screen game that shows the Windows cursor over its window swaps at once while it does (Quake 2 and 3 hide it). **Never**: a single-buffered window's SwapBuffers, the front buffer's shows (`glFlush`, `glFinish`, the 16 ms rule) and LOADs, so these never wait for vertical blank. An interval set on a single-buffered window is kept and answered, and never sent. On a device older than 2.6 no SWAP carries one.
- **The wait is the device's** ([Swaps on the device](#swaps-on-the-device)): it reads the front image back, waits until the interval's vertical blanks have passed since the drawable's last SHOW and one at least since the wait began, then writes the window's rectangles, so the write lands in the blank. SwapBuffers does not wait for the blank itself: the program is held to the refresh rate by its wait for the swap before last ([SwapBuffers](#swapbuffers), step 2).
- **The V-sync overrides** (interface 3.6). On a card with PROFILE.VSYNC, the swaps that can carry an interval (a double-buffered window's SwapBuffers, COPY and SHOW, the window the whole screen and all of it visible, the cursor not kept out) also carry the SWAP flag OVERRIDE (8), whatever their interval, 0 included. The device puts the interval of a SHOW with OVERRIDE through VSYNC_CONTROL's OpenGL field, which the control program's VGPU98 3D tab sets ([gpu-registers.md](gpu-registers.md), The V-sync overrides): so the user's Always on or Always off reaches a full-screen game's swaps alone, and every other SWAP waits as the ICD asked ([gpu-commands.md](gpu-commands.md), SWAP's swap interval). `wglGetSwapIntervalEXT` still answers the program's own interval. The ICD learns PROFILE.VSYNC when it finds the card.

### GL_EXT_compiled_vertex_array

The rules here are taken without the text, from the games' use. Quake 2 locks a model's arrays only with `gl_vertex_arrays 1` (its default is 0) and draws them with `glArrayElement`; Quake 3 locks positions, draws each pass with `glDrawElements`, and changes the colour and texture coordinate arrays between passes.

- **`void glLockArraysEXT(GLint first, GLsizei count)`** locks elements `first` to `first + count − 1`. GL_INVALID_VALUE for a negative `first` or a `count` of 0 or less; GL_INVALID_OPERATION while locked already, and between `glBegin` and `glEnd`.
- **`void glUnlockArraysEXT(void)`** ends the lock. GL_INVALID_OPERATION while not locked, and between `glBegin` and `glEnd`.
- **Client state.** The locked range is client vertex-array state: GL_ARRAY_ELEMENT_LOCK_FIRST_EXT (81A8h) and GL_ARRAY_ELEMENT_LOCK_COUNT_EXT (81A9h) answer it (0 and 0 while not locked; GL_INVALID_ENUM while the extension is not offered); `glPushClientAttrib` with GL_CLIENT_VERTEX_ARRAY_BIT saves it, and its pop puts the range back and takes the lock again from the arrays as they are then. Neither call is compiled into a display list: both run at once, as client state does.
- **What the lock converts**: the vertex array alone, when it is enabled with a pointer at the lock. At the first read of an element of the locked range, every element of the range is converted to four floats (as GL takes a vertex: z 0 and w 1 where missing), and each later read of an element in the range, by `glArrayElement`, `glDrawArrays` and `glDrawElements` alike, takes its values from there while the array's pointer, size, type and stride are those the conversion was made from. A `glVertexPointer` under the lock makes the conversion again, at the next read, from the array it points at; `glInterleavedArrays` takes the whole lock again. So a program that rewrites a locked array and points at it again draws what it wrote (Quake 3's fog pass does so with its colours). The other arrays, the vertex array when it was not enabled at the lock, and elements outside the range are read at each call. A range of more than 100000h elements, one whose last element passes GL's int range, or a conversion with no memory, is not converted: the vertex array is then read as it is. The unlock frees the conversion.
- **Nothing on the device**: DRAW already takes vertices shared by index, and each element goes once a call ([Arrays drawn directly](#arrays-drawn-directly)). Locked vertices kept on the device are deferred ([What is not offered](#what-is-not-offered)).

### Multitexture (GL_ARB_multitexture and GL_SGIS_multitexture)

The rules here are taken without the texts: ARB's from Quake 3's use, SGIS's from Wine's function list and the token values Quake 2 and GLQuake pass. Both extensions drive one mechanism, **two texture units** (GL_MAX_TEXTURE_UNITS_ARB 2), unit 0 being GL 1.1's own.

- **Each unit's state**: the 1D and 2D texture enables and the four generation enables, the bound 1D and 2D textures, the texture environment's mode and colour, coordinate generation's modes and planes, the texture matrix stack (2 deep), the current texture coordinates, the raster position's texture coordinates, and the texture coordinate array.
- **The active unit** (GL_ACTIVE_TEXTURE_ARB, 84E0h, which answers GL_TEXTURE0_ARB plus the unit's number): the unit every texture state call and query addresses. The texture enables (`glEnable`, `glDisable`, `glIsEnabled` and `glGet` of GL_TEXTURE_1D, GL_TEXTURE_2D and GL_TEXTURE_GEN_*), `glBindTexture` and the calls on the bound texture, `glTexEnv`, `glTexGen`, the texture matrix under GL_TEXTURE, and the queries of a unit's state, the current and raster texture coordinates included. `glActiveTextureARB` sets it from GL_TEXTURE0_ARB (84C0h) or GL_TEXTURE1_ARB (84C1h), `glSelectTextureSGIS` from GL_TEXTURE0_SGIS (835Eh) or GL_TEXTURE1_SGIS (835Fh); between `glBegin` and `glEnd`, GL_INVALID_OPERATION.
- **The client unit** (GL_CLIENT_ACTIVE_TEXTURE_ARB, 84E1h): the unit whose texture coordinate array `glTexCoordPointer`, `glEnableClientState`, `glDisableClientState` and `glIsEnabled` of GL_TEXTURE_COORD_ARRAY, `glInterleavedArrays`' coordinates, `glGetPointerv` and the array's queries address. `glClientActiveTextureARB` sets it from ARB's tokens. `glMultiTexCoordPointerSGIS(target, size, type, stride, pointer)` sets the array of the unit it names, as `glTexCoordPointer` would, the client unit unchanged.
- **SGIS's coordinate set.** `glSelectTextureCoordSetSGIS` sets the client unit and the unit `glTexCoord` sets. With ARB's calls alone `glTexCoord` sets unit 0, as in GL 1.1.
- **A unit's current coordinates.** The 32 `glMultiTexCoord*ARB` and the 32 `glMultiTexCoord*SGIS` (1 to 4 coordinates of each type, scalar and vector, the missing t, r and q 0, 0 and 1), and `glMTexCoord2fSGIS` and `glMTexCoord2fvSGIS` (GLQuake's and Quake 2's names), set the coordinates of the unit they name, between `glBegin` and `glEnd` too. Each vertex takes both units' current coordinates.
- **Tokens.** Each extension's calls take its own tokens: an ARB token given to an SGIS call, like any token that names no unit (a third unit among them), is GL_INVALID_ENUM. GL_ACTIVE_TEXTURE_ARB, GL_CLIENT_ACTIVE_TEXTURE_ARB and GL_MAX_TEXTURE_UNITS_ARB answer only while GL_ARB_multitexture is offered, else GL_INVALID_ENUM. SGIS's GL_SELECTED_TEXTURE_SGIS is not answered.
- **Saved state**. `glPushAttrib` with GL_TEXTURE_BIT saves both units' texture state (enables, bindings with the bound textures' parameters, environments, generation) and the active unit; with GL_ENABLE_BIT both units' enables; with GL_CURRENT_BIT both units' current and raster coordinates. `glPushClientAttrib` with GL_CLIENT_VERTEX_ARRAY_BIT saves both units' arrays, the client unit and the coordinate set. `wglCopyContext` copies both units with those groups.
- **Display lists.** `glActiveTextureARB`, `glSelectTextureSGIS`, the `glMultiTexCoord*` and the `glMTexCoord*` are compiled (a token that names no unit is recorded, and raises GL_INVALID_ENUM when the list runs); `glClientActiveTextureARB`, `glSelectTextureCoordSetSGIS` and `glMultiTexCoordPointerSGIS` run at once, as client state does.
- **Evaluators** feed unit 0, as `glTexCoord` does; **feedback** reports unit 0's coordinates; the **ICD's own path** generates and transforms each unit's coordinates by its own state, for `glRasterPos` too.
- **On the device**: unit 0's registers stay where 2.5 has them, and unit 1's 55 (its texture matrix, generation mode and planes, the texture it draws with, its environment's mode and colour) follow them, registers 433-487, with ENABLES' TEXTURE1_1D. A DRAW carries unit 1's coordinates (DRAW_TEX1, 20-float vertices) and PIXELS its raster coordinates (PIXELS_TEX1) while unit 1 draws with a texture ([gpu-commands.md](gpu-commands.md), Command set 2.6). The device generates and transforms each unit's coordinates, samples unit 0's texture and applies its environment, then unit 1's on that result, and keeps a draw's two textures in the texel arena together ([The OpenGL device](#the-opengl-device)).

### GL_EXT_point_parameters

The rules here are taken without the text, from Quake 2's use (its particles: size 40, minimum 2, maximum 40, attenuation 0.01, 0, 0.01, smooth points with texturing off).

- **`void glPointParameterfEXT(GLenum pname, GLfloat param)`** and **`void glPointParameterfvEXT(GLenum pname, const GLfloat *params)`** set the four values:

| Name | Value | Default |
|---|---|---|
| GL_POINT_SIZE_MIN_EXT (8126h) | The smallest size | 0 |
| GL_POINT_SIZE_MAX_EXT (8127h) | The largest size | 64 (the largest point) |
| GL_POINT_FADE_THRESHOLD_SIZE_EXT (8128h) | The fade threshold | 1 |
| GL_DISTANCE_ATTENUATION_EXT (8129h) | a, b and c, three floats (the vector call alone) | 1, 0, 0 |

- **Errors.** Another name, or GL_DISTANCE_ATTENUATION_EXT by the scalar call, GL_INVALID_ENUM; a minimum, maximum or threshold below 0 (or not a number), GL_INVALID_VALUE; between `glBegin` and `glEnd`, GL_INVALID_OPERATION. The attenuation is taken as given.
- **State.** The values are GL_POINT_BIT's (`glPushAttrib` saves them, `wglCopyContext` copies them); `glGet*` answers each by its name while the extension is offered, else GL_INVALID_ENUM. Both calls are compiled into display lists, a name, its values and which call it was, and the list runs that same call, so their errors are raised when the list runs (GL_DISTANCE_ATTENUATION_EXT given to the scalar call is refused then too).
- **The size** of a point at eye distance d (the length of its eye coordinates over their w): POINT_SIZE × √(1 / (a + b·d + c·d²)) where that sum is above 0, else POINT_SIZE; then raised to the minimum and lowered to the maximum (the maximum wins should they cross); below the threshold, the threshold's size, the point's alpha multiplied by (size / threshold)²; then kept to the device's 1-64, as every point. While all four values are their defaults, the size is POINT_SIZE: GL 1.1's points exactly.
- **On the device** (command set 2.6's point registers): the ICD sets R_POINT_PARAMS to 1 only while one of the values is not its default, and sends the four with it; `ogl.vert` then sizes each point of the device's path by its eye distance before building its quad, the fade in both faces' alpha.
- **The ICD's own path** (a polygon's vertices in polygon mode GL_POINT) computes the same size and fade: the size goes in the vertex's STIPPLE place (points have no stipple; 1/1024 where the rule gives none, so it stays above 0), the fade in its colour's alpha, and the device takes that size for the DRAW_CLIP point.

### GL_EXT_paletted_texture and GL_EXT_shared_texture_palette

The rules here are taken without the texts, from Quake 2's use, which needs both names: `glColorTableEXT(GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGB, 256, GL_RGB, GL_UNSIGNED_BYTE, ...)`, its solid textures and its cinematics' frames as GL_COLOR_INDEX8_EXT levels of GL_COLOR_INDEX unsigned bytes, the shared palette changed between the cinematics' frames.

- **Palettes.** Each texture has a palette of its own (the texture object's, so its share group's; the defaults and the proxies have theirs), and each context one shared palette (EXT_shared_texture_palette's). A palette is 1 to 256 entries, a power of two, kept as texels of its internal format's base format (RGBA8, filled in as texels are: [Textures](#textures)); each starts as one white entry of GL_RGBA. Palettes are texture images, not attribute state.
- **Targets**: GL_TEXTURE_1D and GL_TEXTURE_2D, the palette of the active unit's bound texture; GL_PROXY_TEXTURE_1D and GL_PROXY_TEXTURE_2D, for `glColorTableEXT` and the parameter queries alone; GL_SHARED_TEXTURE_PALETTE_EXT (81FBh), the context's shared palette. Another target is GL_INVALID_ENUM.
- **`glColorTableEXT(target, internalformat, width, format, type, table)`** gives the palette `width` entries of `internalformat`'s base format (a texture's internal formats but the index ones; else GL_INVALID_ENUM). They come from `table`, unpacked by the unpack state and taken through the pixel transfer path as a texture image is (GL_COLOR_INDEX data through the index-to-colour maps), in any format a colour image may have but GL_STENCIL_INDEX and GL_DEPTH_COMPONENT, of any type but GL_BITMAP (else GL_INVALID_ENUM); without `table` they are 0. A width that is not a power of two from 1 to 256 is GL_INVALID_VALUE; a proxy is then left with width 0 and no format, and a power of two above 256 sets no error for a proxy. A proxy is answered, never filled or sent. Between `glBegin` and `glEnd`, GL_INVALID_OPERATION.
- **`glColorSubTableEXT(target, start, count, format, type, data)`** replaces entries `start` to `start + count − 1` the same way (no proxy). A negative count, or entries outside the palette's width, are GL_INVALID_VALUE (a compiled call's when the list runs); a count of 0 does nothing.
- **`glGetColorTableEXT(target, format, type, data)`** packs the entries by the pack state, as the RGBA of the palette's base format (luminance and intensity as red, as `glGetTexImage` gives texels); GL_COLOR_INDEX and the proxies are GL_INVALID_ENUM.
- **`glGetColorTableParameterivEXT` and `glGetColorTableParameterfvEXT(target, pname, params)`**: GL_COLOR_TABLE_FORMAT_EXT (the internal format given), GL_COLOR_TABLE_WIDTH_EXT, and GL_COLOR_TABLE_RED_SIZE_EXT, _GREEN_, _BLUE_, _ALPHA_, _LUMINANCE_ and _INTENSITY_SIZE_EXT (8 for each of the base format's components, else 0); the proxies included. Another name is GL_INVALID_ENUM.
- **Index levels**. The internal formats GL_COLOR_INDEX1_EXT to GL_COLOR_INDEX16_EXT (80E2h-80E7h) make an index level, kept as **8-bit indices** whatever the size asked for: GL_TEXTURE_INDEX_SIZE_EXT (80EDh) answers 8 for an index level and 0 for another, GL_TEXTURE_INTERNAL_FORMAT the format given. Its image must be GL_COLOR_INDEX, for `glTexImage*` and `glTexSubImage*` alike (else GL_INVALID_OPERATION); each index goes through the index shift and offset and, with GL_MAP_COLOR, the I_TO_I map, and its low 8 bits are kept. Unsigned bytes with no index transfer (no shift, no offset, no map) go straight in as the unpack state gives them (Quake 2's textures and cinematics). `glCopyTexImage*` into an index format and `glCopyTexSubImage*` into an index level are GL_INVALID_OPERATION.
- **Drawing with an index texture**, on either unit: each texel is looked up in the palette, the shared one while GL_SHARED_TEXTURE_PALETTE_EXT is enabled, else the texture's own, its index taken within the palette's width (its low bits), before filtering; the texture's base format, for the texture environment, is the palette's. `glGetTexImage` gives an index level's texels as their colours through the same palette, or with GL_COLOR_INDEX its indices themselves, one value a pixel, packed by the pack state (GL_COLOR_INDEX from a colour level is GL_INVALID_OPERATION).
- **The shared palette's enable**, GL_SHARED_TEXTURE_PALETTE_EXT (`glEnable`, `glIsEnabled`, `glGet`; GL_INVALID_ENUM while the extension is not offered), is in GL_TEXTURE_BIT and GL_ENABLE_BIT, and goes to the device as ENABLES' SHARED_PALETTE.
- **Display lists.** `glColorTableEXT` (but for a proxy, which runs at once) and `glColorSubTableEXT` are compiled with their data as it was unpacked; a compiled index `glTexImage*` keeps its unpacked indices, as every compiled image does. The queries run at once.
- **On the device**: an index level goes in TEXIMAGE with base format INDEX (7), its bytes four a dword, each row padded to a whole dword; a palette goes whole in a PALETTE packet (the context, the texture's device ID or 0 for the shared palette, the base format, the width, the entries) at each change, so a palette change never sends the texels again. A texture's palette goes when it changes while the texture has a device ID, else with the texture's first TEXPARAM, before its first image; the shared palette at each change, and again after a GPU restart, with the context's registers ([Batches and the ring](#batches-and-the-ring)). The device keeps each texture's palette after its levels in the texel arena and each context's shared palette apart, and `ogl.frag` looks each tap up ([gpu-commands.md](gpu-commands.md), Command set 2.6).

### Clamp-to-edge (GL_EXT_texture_edge_clamp and GL_SGIS_texture_edge_clamp)

The rules here are taken without the texts, from the extensions' well-known rule (GL 1.2's GL_CLAMP_TO_EDGE, at the same value). GL 1.1's GL_CLAMP clamps s and t to 0-1, so LINEAR filtering at a texture's edge blends in its border (the border texels, or the border colour). Most drivers of the time sampled GL_CLAMP as clamp-to-edge instead, and games of the time were tuned on them: Quake 3 1.32 clamps its skies with GL_CLAMP, where the exact rule can show thin seams. A program that knows the difference asks for either name, and both give the one token.

- **The token**: GL_CLAMP_TO_EDGE (812Fh, both extensions' value, GL 1.2's), in `glenum.h`'s section of the extensions offered, a part of its own ("EXT_texture_edge_clamp and SGIS_texture_edge_clamp") after EXT_point_parameters'. Neither extension has a function.
- **`glTexParameter*`** (each of the four calls) takes it for GL_TEXTURE_WRAP_S and GL_TEXTURE_WRAP_T while the extensions are offered, on a device of command set 2.7 or later; on an older device it is GL_INVALID_ENUM and the wrap is unchanged, as for any value that is not a wrap. Given for another parameter (a filter), GL_INVALID_ENUM.
- **State.** It is kept as the other wraps are, a texture parameter: `glGetTexParameter*` answers it (812Fh; 33071.0 by the float call); `glPushAttrib` with GL_TEXTURE_BIT saves it with the bound textures' parameters, and `wglCopyContext` with that group copies it; a compiled `glTexParameter*` keeps it and sets it when the list runs (its error, on a device without the extensions, raised then). A texture still starts with GL_REPEAT.
- **On the device** (command set 2.7): TEXPARAM carries it as wrap value 2 (VGPU98_OGL_WRAP_CLAMP_EDGE), for s and t each; GL_CLAMP still goes as 1 and GL_REPEAT as 0 ([gpu-commands.md](gpu-commands.md), Command set 2.7). The device samples it inside each level, never reading the border texels or the border colour ([Texturing and the texel arena](#texturing-and-the-texel-arena)).
- **GL_CLAMP as period drivers sampled it** is the device's alone, by the 86Box setting **OpenGL GL_CLAMP** ([registers.md](registers.md), 86Box settings): at **As period drivers (clamp to edge)**, the default, the device samples GL_CLAMP as clamp-to-edge; at **Exact (OpenGL 1.1)**, by GL 1.1's rule. The ICD is the same either way: GL_CLAMP goes as GL_CLAMP, and `glGetTexParameter` answers GL_CLAMP. On an 86Box of command set 2.6, which has no such setting, GL_CLAMP is sampled by GL 1.1's rule.

## The OpenGL device

86Box's OpenGL device (`vid_vgpu98_ogl.c`) is a device of its own beside the DirectX renderer and the Glide device. It shares with them only the ring and its lock, the GPU thread, the renderer's Vulkan device and its command buffers in flight, the pipeline cache's file ([pipeline-cache.md](pipeline-cache.md)) and video memory. It records into the renderer's command buffers through the renderer's hook for each device: whichever device takes the command buffer ends the other device's render pass first, and the renderer ends it before it records or submits. An OpenGL command never changes DirectX's or Glide's state. It runs on the command processor's thread.

**Start-up**. The device starts with the renderer, on its Vulkan device, when the host GPU has: R5G6B5_UNORM_PACK16 and B8G8R8A8_UNORM as colour attachments that blend, usable as a copy's source and destination; D16_UNORM as a depth attachment, likewise usable in copies; and D24_UNORM_S8_UINT likewise, or failing it D32_SFLOAT_S8_UINT, whose depth is then kept on the 24-bit grid ([Depth](#depth)). It makes its render passes (565 and 8888, each without depth, with D16 and with the 24-bit format), its shaders (`ogl.vert`, `ogl.frag`, and `ogl.frag` built with COMPUTED_DEPTH), their layout, a descriptor pool a command buffer slot, and its texel arena. It uses Vulkan's `logicOp` where the host has it (DirectX does not use it). With all of that the GPU has PROFILE.OGL, and the start-up line names `OpenGL (command set 2.7)`; without any of it there is no device, no PROFILE.OGL, and nothing more is said ([Diagnostics](#diagnostics)). Last, with the pipeline cache On, it starts its pipeline warm-up ([Pipelines, the uniform block and the data chunks](#pipelines-the-uniform-block-and-the-data-chunks)).

### Objects

| Object | Limit | Holds | When the kind is full |
|---|---|---|---|
| Share group | 32 (VGPU98_OGL_GROUPS) | Its textures, by its ID | The least recently named group ends, its contexts and textures with it |
| Context | 64 (VGPU98_OGL_CONTEXTS) | Its group, the drawable it is bound to, its 495 registers (command set 2.6's), its shared palette and that palette's copy in the texel arena, its uniform block's copy | The least recently named context ends |
| Drawable | 32 (VGPU98_OGL_DRAWABLES) | Its size (1-2048 each way), format, colour images and depth image | The least recently named drawable ends |
| Texture | 16384 (VGPU98_OGL_TEXTURES), every group's | Its levels' texels in the host's memory (12 levels at most, 2048 down to 1, each side 2050 at most with a border; RGBA8, or an index level's bytes), its wraps, filters and border colour, its palette, its copy in the texel arena | Refused (the TEXIMAGE or TEXPARAM dropped) |

Every packet stamps the objects it names with the device's packet count: GROUP and the texture packets (TEXIMAGE, TEXPARAM, TEXDELETE, TEXREAD) stamp the group, DRAWABLE and SWAP the drawable, and the others the context, its group and its drawable. When a kind is full, the object with the oldest stamp goes: the objects of a process that ended without the DLL's detach are taken back so. All textures' texels take at most 512 MB of the host's memory.

### Drawables and their images

A drawable's images are on the host GPU, in Vulkan's GENERAL layout throughout: the **front image** (colour image 0), with a double-buffered format a **back image** (colour image 1), and with a depth format a **depth image** holding depth and stencil. The colour images are R5G6B5 for 565 and B8G8R8A8 for both 8888 formats; the depth image D16 for D16, and D24_UNORM_S8_UINT (or D32_SFLOAT_S8_UINT) for D24S8. At OPEN and RESIZE they are made anew and cleared: colour to black with alpha 1 (an XRGB drawable's alpha reads 1, and an ARGB one's starts there), depth to 1, stencil to 0. Image rows count down from the top, as video memory's do; GL's window coordinates count up from the bottom, so Vulkan's viewport is set upside down, and the scissor box and read-backs take their rows from the bottom ([The y flip](#the-y-flip)). A drawable whose images the host GPU refuses stays, without images: what is drawn into it is ignored.

### Depth

D16 and D24S8 hold depth as the host's formats do. **On a host without D24S8** the D24S8 drawables use D32F with S8: the pipelines that test or write depth into them take `ogl.frag`'s COMPUTED_DEPTH variant, which writes each fragment's depth as `floor(clamp(z, 0, 1) × 16777215 + 0.5) / 16777215`, the 24-bit grid, with polygon offset added before (`factor × m + units / 2^24`, m the larger of the depth's slopes across and down) in place of Vulkan's depth bias. Clears put the clear depth on the same grid, and READ gives the 24-bit integer.

### Drawing

A DRAW's vertices and 16-bit indices are read from storage buffers by `ogl.vert` (vertex pulling), which also builds every point and line as a quad of six vertices.

**The per-vertex operations** (`ogl.vert`), from the context's registers, exactly as GL 1.1 defines them:
1. **Transform.** Eye coordinates are MODELVIEW times the object coordinates, clip coordinates PROJECTION times the eye's. The normal is NORMAL (the modelview's inverse transpose, which the ICD computes) times the vertex's normal, normalized with ENABLES' NORMALIZE (a zero normal kept).
2. **Lighting** (with LIGHTING; GL 1.1, section 2.13.1), for the front face with the normal and, with two-sided lighting (MODEL_FLAGS bit 1), for the back face with the back material and the normal reversed (else the back colour is the front's). With COLOR_MATERIAL on, the current colour replaces the material's part COLOR_MATERIAL names (emission, ambient, diffuse, specular, or ambient and diffuse) for the faces it names. The colour is the emission plus the model ambient times the material's ambient, plus, for each light on (LIGHTS_ON), its attenuation times its spot factor times the sum of: the light's ambient times the material's; where n · VP is above 0 (VP the unit vector from the vertex to the light), n · VP times the light's diffuse times the material's; and where n · h is above 0 too, (n · h) to the shininess times the light's specular times the material's, h being VP plus the unit vector to the eye with a local viewer (MODEL_FLAGS bit 0), else plus (0, 0, 1), normalized. The attenuation is `1 / (constant + linear × d + quadratic × d²)` for a positional light at distance d (1 when that sum is not above 0, and for a directional light); the spot factor, with a cutoff other than 180, the cosine between the spot direction and the direction from the light to the vertex, raised to the spot exponent (1 with an exponent of 0), where it is at least the cutoff's cosine, else 0. The specular term is added before texturing (GL 1.1 has no separate specular). Red, green and blue are clamped to 0-1; alpha is the diffuse material's alpha, clamped. Without lighting the colour is the current colour, clamped to 0-1.
3. **Texture coordinates**, each unit's by its own registers: unit 0's from the vertex's S, T, R and Q, unit 1's from S1 to Q1 of a DRAW_TEX1 vertex (without DRAW_TEX1 unit 1's are (0, 0, 0, 1), neither generated nor transformed). Each of s, t, r and q is generated where the unit's TEXGEN_MODE says (object linear: its object plane times the object coordinates; eye linear: its eye plane, in eye coordinates, times the eye's; sphere map, for s and t: with u the unit vector to the vertex and n the normal, r = u − 2n(n · u), m = 2√(rx² + ry² + (rz + 1)²), the coordinate r/m + 1/2), then the unit's TEXTURE matrix times them.
4. **Clip distances**: each enabled user plane (CLIPS_ON), in eye coordinates, times the eye's coordinates.
5. **The fog distance**: the absolute value of eye z.

With DRAW_CLIP the vertex is the ICD's own: its clip coordinates, its colour clamped to 0-1 for both faces, its texture coordinates (unit 1's too with DRAW_TEX1), its fog distance in NX; no user plane applies.

**Triangles** go through GL's viewport, set as Vulkan's upside down and kept inside the host's limits (`maxViewportDimensions`, `viewportBoundsRange`; an empty viewport draws nothing), its depth range from DEPTH_RANGE clamped to 0-1. Their position is the clip coordinates with z mapped from GL's −w..w to Vulkan's 0..w, so Vulkan clips them against GL's view volume; the user's planes clip them per fragment (a fragment whose interpolated distance to an enabled plane is below 0 is discarded). Front faces: GL's counter-clockwise is Vulkan's counter-clockwise under the upside-down viewport, clockwise with FACE bit 0; with CULL_FACE the faces FACE names are culled, except for DRAW_CLIP triangles, which the ICD culled by the facing it lit them with.

**Points and lines** are placed in window coordinates in the whole drawable (Vulkan's viewport the drawable, upside down, depth 0-1, with DEPTH_RANGE applied to z in `ogl.vert`): GL clips them only at the window, not at its viewport. Each is a quad, so GL's own rules hold exactly:
- **A point** (device path) is drawn only when it is inside the view volume and every enabled user plane. Its size is POINT_SIZE, or with R_POINT_PARAMS (command set 2.6) the size by its eye distance, the length of its eye coordinates over their w: POINT_SIZE × √(1 / (a + b·d + c·d²)) where that sum is above 0, raised to POINT_MIN and lowered to POINT_MAX, and below POINT_FADE the threshold's size with both faces' alpha multiplied by (size / threshold)² ([GL_EXT_point_parameters](#gl_ext_point_parameters)). A DRAW_CLIP point (the ICD's own) takes its STIPPLE float as its size when that is above 0, else POINT_SIZE. The size is then kept to 1-64. Aliased, the size is rounded (at least 1) and the point is a square of that side, centred on a pixel's centre for an odd size and on a pixel's corner for an even one, around its window position. With POINT_SMOOTH, a disc of the size around the exact position, its coverage of each pixel multiplied into alpha (the quad a pixel larger each way).
- **A line** (device path) is clipped against the view volume and the enabled user planes, every value interpolated along it; a line of no length on the screen draws nothing. Its width is LINE_WIDTH kept to 1-16. Aliased, the width is rounded (at least 1): an x-major line (|dx| at least |dy|) is a column of that many fragments at each x, a y-major one a row at each y. With LINE_SMOOTH, a rectangle of the width along the line, its coverage across and at both ends multiplied into alpha. With LINE_STIPPLE, a fragment is drawn when bit `(counter / factor) mod 16` of the pattern is set (LINE_STIPPLE's pattern, its factor kept to 1-256), the counter being the line's start (0, or with DRAW_STIPPLE_ON its first vertex's STIPPLE) plus the fragment's distance from the first end along the major axis. A line's provoking vertex, for flat shading, is its second. Attributes interpolate in window space, as GL 1.1, section 3.4.1, defines them.

**The per-fragment operations in `ogl.frag`**, in order:
1. PIXELS' own fragments ([Pixel rectangles on the device](#pixel-rectangles-on-the-device)).
2. The user's clip planes, for device-path triangles.
3. Polygon stipple, for triangles with POLY_STIPPLE: the fragment is kept when bit `31 − (x mod 32)` of POLYGON_STIPPLE's row `y mod 32` is set (x and y GL's window coordinates).
4. The colour: smooth, or with SHADE 0 the provoking vertex's; a back-facing triangle's the back colour.
5. Line stipple, for lines (above).
6. Texturing ([Texturing](#texturing-and-the-texel-arena)): when unit 0 draws with a texture, its texel is sampled and unit 0's environment applied to the colour; then, when unit 1 draws with one, unit 1's texel is sampled at unit 1's coordinates and its environment applied to that result (ARB_multitexture's cascade).
7. Fog, with FOG, per fragment: the factor f from the fog distance z by FOG_MODE (linear `(end − z) / (end − start)`, 1 when end equals start; exp `e^(−density × z)`; exp2 `e^(−(density × z)²)`), clamped to 0-1; red, green and blue become `f × colour + (1 − f) × fog colour`.
8. The smooth points' and lines' coverage, multiplied into alpha.
9. The alpha test, with ALPHA_TEST, in 8-bit fixed point: `round(alpha × 255)` against `round(reference × 255)` (each clamped to 0-1 first) by ALPHA_FUNC.
10. With COMPUTED_DEPTH, the depth on the 24-bit grid ([Depth](#depth)).

**Then Vulkan's**: the scissor box (with SCISSOR, inside the drawable; an empty one draws nothing), the stencil test (with STENCIL_TEST, on a D24S8 drawable; without a stencil buffer the test passes, as GL 1.1 has it), the depth test (with DEPTH_TEST and a depth buffer, else it passes), blending (with BLEND: GL 1.1's factors, alpha blended by the same factors as colour, added), the logic op (with LOGIC_OP where the host has Vulkan's `logicOp`, GL's sixteen in GL's order, in place of blending), and the colour mask, cut to the components the drawable keeps (an XRGB or 565 drawable's alpha is never written). Draw buffers: each buffer DRAW_BUFFERS names that the drawable has, each in a pass of its own; with none named, one buffer with a colour mask of 0, so depth and stencil are still updated (GL 1.1, section 4.2.1). Dithering: none (ENABLES' DITHER is kept, not read).

The registers ENABLES' DITHER, POLY_SMOOTH, TEXTURE_1D and TEXTURE1_1D, and FOG_PER_PIXEL, are kept and not read: dithering and polygon antialiasing are not done, each texture's dimensions are its level 0's, and fog is always per fragment.

### Pipelines, the uniform block and the data chunks

- **Pipelines** are made for what Vulkan fixes, by a key of: the drawable's colour format and depth kind (its render pass); blending and its two factors; the logic op and which; the colour mask; the depth test, its function and depth writes; the stencil test, its function and three operations; the faces culled and the front face (triangles); depth bias (triangles with OFFSET_FILL, a polygon's lines or points with OFFSET_LINE or OFFSET_POINT, except on the 24-bit grid); the 24-bit grid's variant; and the clear quad's. All draw triangle lists, filled, one sample. The viewport, scissor, stencil compare mask, write mask and reference, and depth bias (POLYGON_OFFSET's units as the constant, its factor as the slope) are dynamic. They are made through the renderer's pipeline cache, so they go into the cache's file with DirectX's and Glide's ([pipeline-cache.md](pipeline-cache.md)). 256 are kept: with all made, the oldest is destroyed once the host GPU is idle. A pipeline the host refuses is a host GPU failure as the renderer has them: its line, `VGPU98 3D: making an OpenGL pipeline failed (VkResult` n`); 3D stops until the next reset`, and every OpenGL command dropped until then.
- **Making one**. The GPU thread makes a pipeline under the watchdog's step `making a pipeline` (VGPU98_STEP_PIPELINE, as the renderer's: [registers.md](registers.md), Diagnostics) and times it: the report's pipelines line gives the period's slowest ([The device's report](#the-devices-report)), and the first that takes 500 ms or more in the session is named in an event line ([The device's lines](#the-devices-lines)). Each one made counts toward the pipeline cache's next save. The OpenGL device's pipelines are not in the cache's key list ([pipeline-cache.md](pipeline-cache.md), The key list): listing them, to be readied from it as the renderer's are, is deferred.
- **The warm-up**. Making the shaders' first pipeline is what takes the time cold (over a second). As the device starts, when the renderer has a pipeline cache and the setting "Pipeline cache" is neither **Start empty** nor **Off**, a thread of its own ("VGPU98 OpenGL warm-up") makes a fixed set of 18 pipelines into the shared cache and destroys each at once: each colour format (565, XRGB, ARGB, each with the colour mask cut to the components it keeps) with each depth kind (none, D16, the 24-bit format with stencil; on a host keeping that depth as D32F, the 24-bit grid's variant), plain and alpha-blended (SRC_ALPHA, ONE_MINUS_SRC_ALPHA), with no other state. It takes the same path as the GPU thread's, without the watchdog's step, the timing or the cache's count, so the first draw finds that work done; its end is an event line, the pipelines made and how long. No thread to be had: nothing is warmed, and pipelines are made as draws need them. At close the device tells it to stop after the pipeline in hand and waits for it, up to 10 seconds, before it frees its shaders and render passes; its end line then adds `, stopped by the close`. When the warm-up is still running after the 10 seconds (a host driver that has not returned), the device ends its drawables and contexts as ever but leaves its own objects and itself as they are, not freed under the thread, as the renderer's own warm-up is left, and says so in an event line ([The device's lines](#the-devices-lines); [pipeline-cache.md](pipeline-cache.md), Warm-up).
- **The uniform block** (std140, 2496 bytes) is the context's 495 registers (124 vectors of four dwords), then 16 vectors for each unit's texture drawn with, unit 0's first (all zero for none): its base format, dimensions, wraps (as sampled: [Texturing and the texel arena](#texturing-and-the-texel-arena)) and filters; its levels; its border; its border colour; and for each level its first texel in the arena, its width and its height (border included). For an index texture (command set 2.6) the base format is its palette's, and the block also marks it indexed and names the palette: its width less one and its first entry in the arena ([gpu-commands.md](gpu-commands.md), Command set 2.6). The host's count of register vectors is checked at its build against the shaders' 124. It is written into the renderer's uniform buffer when the registers or a unit's texture part changed, or the command buffer slot changed, and is otherwise used again.
- **Push constants** carry the draw's control word (DRAW's control, or the device's own kinds for a clear quad and a PIXELS image), the drawable's size, and for PIXELS and clear quads the place, zoom, colour, texture coordinates (each unit's: unit 1's (0, 0, 0, 1) without PIXELS_TEX1), fog distance and stencil bit.
- **The data chunks.** A draw's vertices and indices (or a PIXELS image) are placed in a chunk of 1 MB of the slot's storage buffer, which keeps F000h bytes spare at its end, so the descriptors' fixed range of F000h bytes, at dynamic offsets, never runs past it. One descriptor set serves while the slot's buffers stay the same; a slot's pool holds 256 sets, and a full one has the slot submitted.

### The y flip

GL's window coordinates count rows up from the bottom; the images' rows count down from the top, as the window's rows in video memory do. Vulkan's viewport is set upside down (its y at GL's bottom edge in the image's rows, its height negative), which also makes Vulkan's facing GL's; the scissor box's rows are taken from the bottom; READ writes rows from the bottom; LOAD and SHOW copy rows as they are (the image's row 0 is the client area's top row).

### Texturing and the texel arena

A share group's textures are kept in the host's memory as the truth: each level's texels as TEXIMAGE gave them, RGBA8 with its border, or an index level's bytes (command set 2.6), a byte a texel, rows not padded. A texture is drawn from a copy in the **texel arena**, a storage buffer in the host GPU's own memory of 128 MB, or 64, 32 or 16 MB on a host that gives no more (no device without 16 MB), fetched and filtered in `ogl.frag`:

- **Its copy** is made when a draw uses the texture (either unit's TEXTURE_ID names it, and its level 0 is defined): the levels defined from 0 on, one after another, up to level 0's chain down to 1 (a level defined past it is never sampled), each of level 0's kind (indices or colours), packed one after another, each level starting on a whole dword; an index texture's palette after them, with room for 256 entries; their bytes rounded up to 256, in the first free range that holds them; the least recently drawn other textures' copies go for room, never the other texture of the same draw, so a draw's two textures are in the arena together. A texture larger than the arena, with the draw's other texture, is drawn untextured. The copy goes up through the slot's staging buffer.
- **Changes**. A level defined again (TEXIMAGE with DEFINE), a GPU reset, or a change to a texture without a copy, makes the copy again whole at its next use. Texels replaced in a level of a texture whose copy is up to date (TEXIMAGE without DEFINE: `glTexSubImage*`, `glCopyTexSubImage*`) widen that level's changed rectangle; at the texture's next use each changed row's span, inside the rectangle, is copied into the arena (a copy region a row), not the whole texture. A texture's palette changed (PALETTE) while its copy is up to date is copied in alone, its entries' bytes, at the next use.
- **The shared palette** (command set 2.6): each context's has a range of its own in the arena, 1 KB, made when an index texture first draws with it (other textures' copies going for room, never the draw's), kept until the context ends, and brought up to date at the next such draw after a PALETTE changes it or a GPU reset.
- **Index textures** (command set 2.6): each tap's index, a byte, is taken within the palette's width (its low bits) and its palette entry, RGBA8, read as the texel, before filtering; the palette is the context's shared one while ENABLES' SHARED_PALETTE is set, else the texture's own, and its base format is the texture's for the environment.
- **Sampling** (GL 1.1, section 3.8), each unit's texture at its own coordinates: s and t divided by q; λ = log2 ρ, ρ the larger of the lengths of the coordinates' derivatives across and down, in level 0's texels (without its border); c is 0.5 for a LINEAR magnification filter with NEAREST_MIPMAP_NEAREST or NEAREST_MIPMAP_LINEAR minification, else 0. At λ ≤ c the magnification filter samples level 0; otherwise the minification filter: NEAREST or LINEAR level 0; *_MIPMAP_NEAREST the level 0 when λ ≤ 0.5, else `ceil(λ + 0.5) − 1` (at most the last); *_MIPMAP_LINEAR the last level from λ at the last level's number on, else the two levels around λ, blended by its fraction. Within a level, NEAREST takes texel `floor(u)`, LINEAR the four texels around `(u − 1/2, v − 1/2)` blended; GL_REPEAT wraps the indices, GL_CLAMP clamps s and t to 0-1 (NEAREST then takes the last texel at 1, LINEAR reaches the border); a texel index outside the image (border included) reads the border colour. **Clamp-to-edge** (command set 2.7's wrap 2) clamps s and t to 0-1 likewise, NEAREST as GL_CLAMP's (already inside the level), and each of LINEAR's two taps in each direction to 0 through the level's width − 1 (height − 1), its border left out, so neither the border texels nor the border colour is read; each level by its own size. A 1D texture's t is ignored.
- **GL_CLAMP as sampled**. The texture block a draw samples holds the wraps as sampled, not as TEXPARAM gave them: unless the 86Box setting "OpenGL GL_CLAMP" is **Exact (OpenGL 1.1)** (the host option VGPU98_OPT_OGL_CLAMP_EXACT, 40h, read once as the device starts), the device writes a texture's GL_CLAMP (1) as clamp-to-edge (2) there, s and t each, so by default GL_CLAMP is sampled as period drivers sampled it, and at Exact by GL 1.1's rule. The texture keeps the wraps TEXPARAM gave; GL_REPEAT and clamp-to-edge are sampled as given whatever the setting.
- **The texture environment** (GL 1.1, tables 3.17 and 3.18), each unit's by its own TEX_ENV_MODE and TEX_ENV_COLOR (unit 1's in its block), by its texture's base format: Cf the fragment's colour (for unit 1, unit 0's result), Ct the texel's (luminance and intensity read from red), Cc the unit's TEX_ENV_COLOR:

| Base | REPLACE | MODULATE | DECAL | BLEND |
|---|---|---|---|---|
| ALPHA | Cf's RGB, At | Cf's RGB, Af × At | Cf | Cf's RGB, Af × At |
| LUMINANCE | Lt, Af | Cf × Lt, Af | Cf | mix(Cf, Cc, Lt), Af |
| LUMINANCE_ALPHA | Lt, At | Cf × Lt, Af × At | Cf | mix(Cf, Cc, Lt), Af × At |
| INTENSITY | It in all four | Cf × It in all four | Cf | mix(Cf, Cc, It) in all four |
| RGB | Ct's RGB, Af | Cf × Ct, Af | Ct's RGB, Af | mix(Cf, Cc, Ct), Af |
| RGBA | Ct | Cf × Ct | mix(Cf, Ct, At), Af | mix(Cf, Cc, Ct), Af × At |

### Pixel rectangles on the device

PIXELS draws an image's fragments, a quad over its zoomed rectangle with a pixel's margin, `ogl.frag` finding each fragment's pixel by GL's rule (GL 1.1, section 3.6.4): image pixel (i, j) covers the pixel centres from `(x + zoom x × i, y + zoom y × j)` to `(x + zoom x × (i + 1), y + zoom y × (j + 1))`; a bitmap's pixel (i, j) is at `(floor(x) + i, floor(y) + j)`, unzoomed (GL 1.1, section 3.7). The margin keeps Vulkan's edge rule from losing an edge row the test should keep.

- **Colour images** draw their own colours; **bitmaps** draw the raster position's colour where their byte is not 0; **depth images** the raster position's colour with their own depths (on the 24-bit grid, through COMPUTED_DEPTH). Each fragment takes the raster position's z, each unit's texture coordinates and distance (the fog distance), then the same per-fragment operations as a point's: texturing, fog, the alpha test, the scissor, stencil and depth tests, blending, the logic op and the masks, into the draw buffers.
- **Stencil images** write their indices one bit at a time (GL 1.1, section 4.3.1): for each bit the stencil write mask lets through, the fragments whose index has that bit set replace it with 1, then those whose bit is clear with 0, through the write mask of that bit alone; no colour is written and no test applies. Without a stencil buffer, or with a write mask of 0, nothing is drawn.

### Read-backs on the device

- **READ** copies the rectangle of a buffer (front, back, depth or stencil) that lies inside the drawable into the host's memory, waits for the host GPU, and writes it into video memory at the ICD's staging rows, from the bottom row up: colour as the drawable keeps it (565 in 2 bytes, BGRA 8888 in 4), depth as 32-bit integers (D16's 16-bit value; D24S8's 24 bits; D32F's float on the 24-bit grid), stencil as bytes. Pixels of the rectangle outside the drawable are left as video memory has them (GL leaves them undefined). The back of a single-buffered drawable is its front.
- **TEXREAD** copies rows of a level's texels from the host's copy, RGBA8 with its border (an index level's bytes, a byte a texel), the level's first row (t = 0) first.
- Both keep video memory coherent ([gpu-coherency.md](gpu-coherency.md)): newer DirectX pixels in each row's range are copied back first (`vgpu98_r3d_sync`), then the bytes written, then the pages marked (`vgpu98_gpu_wrote`). Rows that would land on the frame shown are refused: a staging surface found before a mode change may lie inside the new desktop. A RESET since the packet was taken (a mode change: video memory laid out anew) writes nothing.

### Swaps on the device

SWAP carries flags LOAD, COPY and SHOW, done in that order, those given, and from command set 2.6 SHOW's swap interval:
- **The checks.** For LOAD or SHOW, the rectangles are written only when the screen the SWAP names is the one shown: the visible mode on, its start the SWAP's offset, its pitch and bits per pixel the SWAP's, and every rectangle's bottom within its height; and only when the screen's bytes a pixel are the drawable's (2 for 565, 4 for 8888). Otherwise nothing is written: COPY alone is done, and SHOW's wait for vertical blank when it has an interval.
- **LOAD**: each rectangle's pixels are copied from video memory into the front image (newer DirectX pixels copied back first), at the rectangle's place less the client area's corner.
- **COPY**: a double-buffered drawable's back image is copied whole into its front image.
- **SHOW**: the front image's pixels under the rectangles' bounding box are read back through the slot's staging buffer (Step 0's simple path: presenting through the display's layer is deferred), the host GPU waited for, and the pixels copied out to memory of the SHOW's own at once (the wait and the coherency rule's copies back may begin other slots, which reuse the staging buffer); with a swap interval the vertical blanks are then waited for (below). Then, under the coherency rule, newer DirectX pixels under every rectangle are copied back, the rectangles written into video memory row by row under the display's layer lock when there is a display (its own setup can fail while the host GPU works), so the host's display composes all of the old frame or all of the new (as Glide's shows), and the pages marked after. A RESET since the packet was taken writes nothing.
- **The swap interval** (command set 2.6's INTERVAL, 0-4). The device keeps, for each drawable, the vertical blank count of its last SHOW: every SHOW sets it, with an interval or without, and a new drawable's is 0. With INTERVAL n, SHOW waits, after the read-back and before the writes, until the card's vertical blank count is n or more past that mark and has moved at least once since the wait began, so the writes land at the start of the blank. It is the command processor's wait, as Glide's SWAP and FLIP have it, from the drawable's own mark (Glide's count stays its own), and later packets wait behind it. A RESET (a mode change, a DOS session, a driver starting or restarting the GPU, a machine reset) ends the wait, and the SWAP then writes nothing. A SHOW with nothing to write (no visible rectangle, or the checks above leave none) still waits, so the swap keeps its pace; a drawable without images does not.

### Resets

At every RESET the device forgets what the renderer's command buffers held for it (its open render pass, the bound pipeline, its descriptor set and data chunk), each context's uniform block is written again at its next draw, and every texture's arena copy is made again at its next use, as is each context's shared palette's (an upload may have been dropped with the recording). Its objects are kept, palettes included: RESET's causes are mode changes (HOLD_DRIVER), DOS sessions (HOLD_VDD) and the drivers' starts and recoveries, all of which GL 1.1 outlives. **A machine reset and the GPU_RESET of POST** end every drawable, group, context and texture: the command processor tells the device a new machine from a mode change by a reset kind of OpenGL's own (Glide's kinds stay as they are, [gpu-registers.md](gpu-registers.md)).

## What is not offered

What GL 1.1 or a period card has and the ICD or the device does not do. None of these is said in a log line ([Diagnostics](#diagnostics)).

| What | Instead |
|---|---|
| Extensions beyond the nine offered: `WGL_ARB_extensions_string`; then GL 1.2, then 1.3 with its period extensions | Not in GL_EXTENSIONS, and DrvGetProcAddress gives NULL for their functions; GL_VERSION is 1.1 |
| Texture environments add and combine, texture compression (S3TC), more than two texture units | GL 1.1's four environments; uncompressed textures only; two units |
| Anisotropic filtering | GL 1.1's six filters, sampled as [Texturing and the texel arena](#texturing-and-the-texel-arena) gives them |
| LOD bias | λ as GL 1.1 computes it, with no bias |
| Swap intervals for a window on the desktop, a full-screen window partly covered, or while the cursor is kept out of the window | The swap goes at once, as a windowed Direct3D Blt does ([WGL_EXT_swap_control](#wgl_ext_swap_control)) |
| Locked vertices kept on the device | The lock's conversion stays in the ICD; each DRAW carries its vertices |
| Texture uploads gathered, so a frame's partial updates do not each end the render pass | Each texture's changed rows (the bounding box of a level's changes) are copied at its next use, never more than a whole copy |
| Index levels of more than 8 bits (GL_COLOR_INDEX12_EXT, GL_COLOR_INDEX16_EXT) | Kept as 8-bit indices, their low 8 bits; GL_TEXTURE_INDEX_SIZE_EXT answers 8 |
| Any extension on an 86Box of command set 2.5 | GL 1.1 alone: GL_EXTENSIONS is "", DrvGetProcAddress gives NULL, and SwapBuffers never waits for vertical blank ([Which are offered](#which-are-offered)) |
| The edge-clamp extensions on an 86Box of command set 2.6 | The seven of 2.6: GL_CLAMP_TO_EDGE is GL_INVALID_ENUM, and GL_CLAMP is sampled by GL 1.1's rule ([Which are offered](#which-are-offered)) |
| The accumulation buffer | No pixel format has accumulation bits; `glAccum` is GL_INVALID_OPERATION; `glClear`'s accumulation bit clears nothing |
| Colour index mode, aux buffers, stereo, overlay and underlay planes, drawing to bitmaps | No pixel format offers them; Windows' own formats and software path serve those requests |
| Pixel formats on an 8 bpp desktop | None: Windows' software OpenGL serves |
| Dithering of 16-bit targets | None: colours are rounded by the host's formats (GL allows the trivial dither) |
| GL_POLYGON_SMOOTH | Polygons are drawn without coverage; smooth points and lines have theirs |
| GL_FRONT_AND_BACK and GL_LEFT as one pass | Drawn twice, a pass a buffer: the second pass sees the first's depth and stencil |
| Front-buffer loads by page versions | The front image is loaded at the first front drawing or front read after wglMakeCurrent or a resize, and is not loaded again when the window's pixels change otherwise |
| Presenting through the display's layer | SHOW reads the front image back and writes video memory (the simple path) |
| Vertices from main memory | Vertices and indices go through the ring in each DRAW |
| Display lists kept on the host | Lists are kept and run in the ICD |
| Textures kept at period resolutions (16-bit textures at a 16-bit desktop) | Every texture is RGBA8 with its base format's components (an index texture's levels 8-bit indices, its palette RGBA8) |
| The OpenGL pipelines games used, listed in the pipeline cache's key list and readied from it at start | The device's fixed warm-up of 18 ([Pipelines, the uniform block and the data chunks](#pipelines-the-uniform-block-and-the-data-chunks)); a game's other pipelines are made at their first use, found in Vulkan's cache data when saved before, and the ICD waits 5 seconds for one ([Batches and the ring](#batches-and-the-ring)) |
| A batch cut short by a GPU restart keeping its texture uploads | Its objects, registers and shared palette go again; its texture uploads and texture palettes are lost, as Glide's are |
| Logic ops on a host without Vulkan's `logicOp` | GL_COLOR_LOGIC_OP is ignored there (blending applies if enabled) |
| Texture priorities | Kept and answered; every texture is resident |
| IDs unique across processes | Contexts, drawables and groups keep a 16-bit hash of the process's ID in their IDs; two live processes that fold to the same hash could collide |

Kept as they are, as GL 1.1's rules or of no consequence to a program: line attributes interpolate in window space (GL 1.1, section 3.4.1); the end pixels of lines drawn right to left; the scalar `gl*f` variants raise GL_INVALID_ENUM at compile time for a vector name; `glEvalMesh` over huge ranges; GL_COEFF's order (the reference manual's). And: `wglGetSwapIntervalEXT` answers the interval as set, above 4 too; a window that Windows gave a gone window's HWND, given the same pixel format, keeps the gone window's interval; GDI's wait reads the last fence issued once, so a SHOW issued while GDI waits is not waited for (a short race in the clip list).

## Diagnostics

OpenGL has a Diagnostics level of its own, **OpenGL** (value 9): **Minimal** for the host, the DirectX driver and Glide, with the ICD's full lines and the OpenGL device's report and findings; DEBUG reads EVENTS and OPENGL (bit 8), not LISTENING ([registers.md](registers.md), Diagnostics). Nothing is written inside the virtual machine; the counters on both halves' paths are plain increments, and nothing is written at Off.

| Level | The ICD | The OpenGL device |
|---|---|---|
| Off | Nothing, and no fault catcher | Nothing |
| Every other level (Minimal, the DirectX levels, Glide) | Its event lines: loaded, no OpenGL, a possible stale driver, a fault in the driver, the ring's events | The first 16 commands dropped; the warm-up's end; a slow pipeline |
| OpenGL | Its event lines and the rest: the extensions offered, the files and stamps, the windows' pixel formats, GL errors, names and pixel formats not there, the swap interval's decisions, the ring's placement, the report, the findings | The first 16 commands dropped, the warm-up's end, a slow pipeline, the report, the findings, the captures |

The GPU's start-up line lists the command sets the GPU has, and so names the OpenGL device's as it names Glide's: `VGPU98 GPU: profile 1FFF (3D on the host's GPU; command set 2.2, DirectX 8 with shaders; Glide (command set 2.4); OpenGL (command set 2.7); hardware cursor)` (PROFILE.CURSOR since interface 3.4). 86Box's power-on line names the setting "OpenGL GL_CLAMP" when it is Exact, `, OpenGL GL_CLAMP "Exact"`, as it names the other settings not at their defaults ([registers.md](registers.md), Diagnostics). A pipeline the host GPU refuses is reported by the renderer's own failure line.

### The ICD's lines

The ICD (`opengl/diag.c`) writes to the host's log through BAR0's DEBUG register with the Glide DLLs' line writer (`driver32/logline.c`), a line at a time under the shared block's log lock (a lock of the DLL's own while the block is another build's), so each appears after the host's `<date> <time> VGPU98 debug: ` ([registers.md](registers.md), Developer debug registers). Every line starts `VGPU98 opengl` *stamp* *PROGRAM*`: `, the ICD's build stamp and the program's file name without its folder, at most 20 characters (a name cut at a space loses the space; `?` without one), so the line fits the writer's 159 characters after it; the program's full path has a line of its own. Names in a line (a function's, one asked for) are cut to 64 characters.

**The level** is read once a process, from DEBUG, when the ICD reaches the card ([Finding the card](#finding-the-card), steps 5 and 6): the event lines while DEBUG has any of EVENTS, LISTENING, GLIDE or OPENGL (every level but Off), the rest only with OPENGL (**OpenGL**). Before that, and at Off, nothing is written. The counters on the ICD's paths are plain increments in the process's memory; the GL errors' names and counts, the misses, the swap decisions and the untextured draws are looked at only at OpenGL.

**Event lines**, at every level but Off:

| Line | When |
|---|---|
| `loaded; OpenGL 1.1 with` n `extensions (the device's command set` M`.`m`)` | The card found, on a device of command set 2.6 or later: n the names in GL_EXTENSIONS, 7 on a 2.6 device, 9 on a 2.7 one |
| `loaded; OpenGL 1.1 alone (the device's command set` M`.`m`: an older 86Box)` | The card found, on an older device |
| `no OpenGL: another build's display driver (block` version`/`size `bytes, answer` version`; this DLL's` version`/`size`)` | The shared block's version or size, or the escape's block version, not this build's (step 5); written under a lock of the DLL's own, and nothing more from the process |
| `no OpenGL: the GPU's profile` x `has no 3D (the host GPU unavailable)` | PROFILE without RENDER3D (step 6) |
| `no OpenGL: the GPU's profile` x `has no OpenGL device (an older 86Box)` | PROFILE with RENDER3D but without OGL |
| `possible stale OpenGL driver: built` stamp`, the display driver` stamp | The display driver's build stamp, in the escape's answer, not the ICD's: an old copy of one of them (Glide's rule) |
| `possible stale OpenGL driver: built` stamp`; the display driver gives no stamp (an older build)` | The escape answered its 16-byte head alone |
| `a fault in the OpenGL driver: code` cccccccc `at +`offset, then `  its addresses on the stack: +`a `+`b … (six) | A fault inside `vgpu98gl.dll` the program does not handle ([the fault catcher](#the-fault-catcher), below) |
| `ring:` … | The ring code's events (`driver32/gpu.c`, the DirectX DLL's own: [directx-hal.md](directx-hal.md)): waits over 2 seconds, again every 10 while they last, each with a line of the GPU's state; a ring lock taken over, or held by a process that ended; GPU errors and resets; a GPU that made no progress for 5 seconds (the ICD's patience: [Batches and the ring](#batches-and-the-ring)); a ring the GPU refused or another process's; a packet too long for the ring; a fence waited for and never issued |

At the card's finding they come in this order: `loaded`, at OpenGL the extensions offered, the stamps' check, at OpenGL the files, then the fault catcher is put in place.

**At OpenGL**, the rest:

| Line | When |
|---|---|
| `extensions offered:` names | Once, at the card's finding, right after `loaded`, with any offered: GL_EXTENSIONS' names, a space between, in as many lines as it takes, a line holding as many whole names as fit in 80 characters (a longer name alone, cut to 80); three lines on a 2.7 device and on a 2.6 one |
| `build` stamp`, the display driver's` stamp | At the card's finding, the stamps the same |
| `file (the system folder):` path, or `file (not the system folder,` folder`):` path (`not known` without a system folder), or `file not known` | The DLL's own file (GetModuleFileNameA) and whether its folder is the system folder (GetSystemDirectoryA, letters in either case), the verdict first, as a long path may run past the line's end |
| `program` path, or `program not known` | The program's full path |
| `GL error` GL_… `in` name | A GL error the ICD records ([below](#gl-errors-and-what-is-not-there)) |
| `more kinds of GL error are counted in the report, not named` | Once, after 64 kinds |
| `no function` name | A name DrvGetProcAddress does not have, each once (matched exactly); a NULL name as `(none)` |
| `more functions asked for are not named` | Once, after 32 names |
| `window` hwnd`: pixel format` n`: colour` r`-`g`-`b `, depth` d`, stencil` s`, double-buffered` (or `single-buffered`); `colour` r`-`g`-`b`-`a with alpha bits | DrvSetPixelFormat setting a format: the window (in hex), the format's number and its bits, as the window's entry has them ([Pixel formats](#pixel-formats)); each successful call, the first 16 a process |
| `pixel format` n `refused for DC` x`:` n `formats at` n `bits a pixel` | DrvSetPixelFormat refusing a format: out of range, or its window refused ([Windows](#windows)); 8 a process, the 8th ending ` (no more of these lines)` |
| `window` hwnd`: swap interval` … | The swap interval's decisions ([below](#the-swap-intervals-decisions)) |
| `no more swap interval lines` | Once, after 32 of them |
| `ring: ring` n `KB in main memory, status` …, or `ring: ring 128 KB in video memory (`why`)` | The ring chosen at the card's finding ([Batches and the ring](#batches-and-the-ring)) |
| `report,` … | Every 5 seconds ([below](#the-icds-report)) |
| `possible incomplete texture:` …, `possible slow path:` … | The findings ([below](#the-icds-findings)) |
| `the program faulted (code` cccccccc `at` address`), not in the OpenGL driver` | A fault outside the DLL that the program does not handle |

#### GL errors and what is not there

**GL errors**. Every error the ICD records is named by the function in the ICD's source that raised it: `ogl_error` is a macro passing its caller's name, and so is `ogl_inside`, so the GL_INVALID_OPERATION of a call between `glBegin` and `glEnd` names that call. An entry point is named by its GL name without the ICD's `ogl_` (`BindTexture`, `TexParameteri`); a helper by its own name, as `set_cap` for `glEnable` and `glDisable`. Each function's first of each code is said, `GL error GL_INVALID_ENUM in set_cap`; 64 kinds a process, then the one line saying more are counted only. The error is recorded as ever, and `glGetError` answers it; the report counts every one by code.

**What is not there**: each name DrvGetProcAddress does not have, once, 32 a process; each pixel format DrvSetPixelFormat refuses, 8 a process.

#### The swap interval's decisions

At a double-buffered window's SwapBuffers on a device with WGL_EXT_swap_control, what became of the window's interval (kept to 4), said only when it changes for that window (a window's first after it takes a pixel format is said; [WGL_EXT_swap_control](#wgl_ext_swap_control)):

| Line | The swap |
|---|---|
| `window` hwnd`: swap interval` n `sent (the window covers the screen)` | Carried the interval |
| `window` hwnd`: swap interval` n `not sent (a window on the desktop: it swaps at once)` | The client area is not the whole screen |
| `window` hwnd`: swap interval` n `not sent (covered in part,` n `rectangles visible: it swaps at once)` | The client area is the screen, but its visible part is not one rectangle of the whole screen |
| `window` hwnd`: swap interval` n `not sent (the cursor is kept out of it: it swaps at once)` | The cursor kept out for the SHOW |
| `window` hwnd`: swap interval 0: swaps at once` | The interval is 0 |

32 such lines a process, then `no more swap interval lines`.

#### The ICD's report

At **OpenGL**, at a SwapBuffers 5 seconds or more after the last report (a double-buffered window's or a single-buffered one's; the process's first SwapBuffers starts the clock), the process's counts since, started again each time:

```
VGPU98 opengl 1a2b3c4 QUAKE2.EXE: report, 5.0 s: 300 swaps; primitives: 9000 on the device, 40 on the ICD's path
VGPU98 opengl 1a2b3c4 QUAKE2.EXE:   arrays drawn direct 1200, display lists called 0
VGPU98 opengl 1a2b3c4 QUAKE2.EXE:   the ICD's path: materials in glBegin 0, polygon modes 40, stippled strips 0
VGPU98 opengl 1a2b3c4 QUAKE2.EXE:   GL errors 2: enum 2, value 0, operation 0, stack 0, out of memory 0
```

- `swaps`: the process's SwapBuffers that swapped (the calling thread's context drawing into the window), single-buffered windows' among them.
- `primitives: … on the device`: `glBegin`/`glEnd` primitives in GL_RENDER drawn by the device's path; `on the ICD's path` those drawn through the ICD's own path, by reason in the third line, which comes when there were any (`glMaterial` between `glBegin` and `glEnd` with lighting, polygon modes other than GL_FILL, stippled line strips and loops). Selection and feedback are not counted.
- `arrays drawn direct`: `glDrawArrays` and `glDrawElements` calls drawn straight from the arrays ([Arrays drawn directly](#arrays-drawn-directly)).
- `display lists called`: lists run, nested ones included. This second line comes only when either count is above 0.
- `GL errors`, by code, when there were any; `stack` is overflows and underflows together.

#### The ICD's findings

At **OpenGL**, each said once a process, worded `possible`:

| Line | When |
|---|---|
| `possible incomplete texture: unit` u`'s texture` name `drawn untextured (`n `draws)`, then `  its level 0` WxH`, format` x`;` n `levels defined; minification` filter | 50 draws (each `glBegin`/`glEnd` primitive in GL_RENDER and each array call drawn directly, by either path, counted over the process's life) while a unit's texturing is enabled and the texture its registers last chose cannot draw: incomplete ([Textures](#textures), Completeness), or without a device ID. Its level 0's size and internal format, the levels it has and its minification filter, by GL's name less `GL_` |
| `possible incomplete texture: unit` u`'s texture` name `has no image, drawn untextured (`n `draws)` | Likewise, for a texture never given an image |
| `possible slow path: the ICD's own path drew` n `of` n `primitives, three reports in a row` | Three reports in a row each with 1000 or more primitives (the device's, the ICD's own path's and the arrays drawn direct), at least half of them by the ICD's own path |

#### The fault catcher

The same catcher as the Glide DLLs'. When the card is found at any level but Off, the ICD puts its filter in place with SetUnhandledExceptionFilter, keeping the program's filter from before. A fault no handler of the program's takes reaches it: a fault inside `vgpu98gl.dll` (its instruction within the DLL's image) is an event with its code, its offset in the DLL and the first six return addresses into the DLL found in the stack's first 256 dwords (offsets, `vgpu98gl.map` names them; IsBadReadPtr ends the walk at memory that cannot be read), any other fault a line at OpenGL. Then the program's own filter from before runs, unless there was none, it is the ICD's own, it lies inside the DLL or cannot be read; else Windows goes on with the fault as without the ICD. A second fault while the filter writes its lines is not told. At a FreeLibrary the program's filter from before goes back, if the ICD's is still the one in place (one the program set since stays). There is no recorder of recent calls (deferred: immediate mode makes 100,000 GL calls a frame).

### The device's lines

86Box's OpenGL device writes its lines to 86Box's log after the date and time, starting `VGPU98 OpenGL:`. Its dropped commands and its pipelines' event lines are named at every level but Off; its report and findings come at **OpenGL** alone.

**Pipelines**, events at every level but Off (the third, at close, only when the warm-up is stuck):

| Line | When |
|---|---|
| `VGPU98 OpenGL:` n `pipelines readied in the background in` m `ms`, then ` (some could not be made)` when any failed, then `, stopped by the close` when the close stopped it short of the 18 | The warm-up's end ([Pipelines, the uniform block and the data chunks](#pipelines-the-uniform-block-and-the-data-chunks)): n of its 18 made, m its time; once, or not at all without a warm-up. When the close stopped it, n counts those made before |
| `VGPU98 OpenGL: the warm-up did not end within 10 s of the close; its objects left as they are` | At close, the warm-up still running after the device's 10-second wait: the device's pipelines, textures, texel arena, shaders, render passes and layout, and the device itself, are not freed (its drawables and contexts are ended first, as at every close); the GPU's close then keeps the renderer (the device and pipeline cache the thread uses) and its own record (through which the thread logs), as for the renderer's own threads |
| `VGPU98 OpenGL: making a pipeline took` n `ms (not in the pipeline cache: new shaders, or a state not made before)`, then `; the warm-up was still running` when it was | The first pipeline of the session the GPU thread took 500 ms or more to make (the time making it alone, not the draw's); once a session |

**Commands dropped**. A command the device cannot take is dropped and counted, and the GPU goes on with the next packet ([gpu-commands.md](gpu-commands.md), Command set 2.5). The first 16 since the machine started are named, at every level but Off, the 16th ending ` (more are counted, not named)`; the rest are counted in the report, by opcode:

`VGPU98 OpenGL: command` nnh `(`name`) dropped:` why

The opcode's name is the packet's (GROUP, CONTEXT, DRAWABLE, BIND, STATE, DRAW, CLEAR, SWAP, TEXIMAGE, TEXPARAM, TEXDELETE, PIXELS, READ, TEXREAD, PALETTE; 5Fh as `5Fh`). The reasons, each as the line gives it (IDs, counts and sizes in place of the italic words, IDs and flags in hex):

| Packet | Why |
|---|---|
| Any | `3D stopped on the host GPU` (every command after a host GPU failure, until the next reset) |
| STATE, CLEAR, DRAW, PIXELS, READ, PALETTE | `context` *id*`, which it does not have` |
| 5Fh (and any other opcode the device has no case for) | `not an OpenGL command` |
| GROUP | *n* `dwords` (too short); `a CLOSE of group` *id*`, which it does not have`; `group` *id*`: action` *op* (another op, or group 0) |
| CONTEXT | *n* `dwords`; `a CLOSE of context` *id*`, which it does not have`; `context` *id*`: action` *op*`, group` *id* (another op, context 0, or with ` (no such group)`); `context` *id*`: no context to take back`; `context` *id*`: no memory for it` |
| DRAWABLE | *n* `dwords`; `a CLOSE of drawable` *id*`, which it does not have`; `drawable` *id*`: action` *op*`,` *w*`x`*h*`, format` *f* (another op, a RESIZE of one it does not have, drawable 0, a size or format outside the checks); `drawable` *id*`: no room for its` *w*`x`*h* `images on the host GPU (what is drawn into it is dropped)` |
| BIND | *n* `dwords`; `context` *id*`, which it does not have`; `drawable` *id*`, which it does not have` |
| STATE | *n* `dwords`; `registers` *first* `to` *last*`, past the` *count* |
| DRAW | `kind` *k*`, control` *c*`,` *nv* `vertices,` *ni* `indices in` *n* `dwords` (any check of the packet's form); `an index` *i* `past its` *nv* `vertices` |
| CLEAR | `buffers` *mask* `in` *n* `dwords` |
| SWAP | `drawable` *id*`, which it does not have`; `drawable` *id*`: flags` *f*`,` *n* `rectangles,` *bpp* `bits a pixel` (flags, interval, rectangle count or bits a pixel outside the checks); `drawable` *id*`: a rectangle outside the screen or video memory` |
| TEXIMAGE | *n* `dwords`; `group` *id*`, which it does not have`; `texture` *id*`: control` *c*`,` *w*`x`*h*`, base format` *b* (texture 0, a control, dimension, level, base format or size outside the checks); `texture` *id* `level` *l*`:` *n* `dwords for` *w*`x`*h* (a DEFINE of the wrong length); `texture` *id*`: the device's 16384 textures, or their 512 MB, all in use`; `texture` *id* `level` *l*`: no memory for` *w*`x`*h*; `texture` *id* `level` *l*`:` *w*`x`*h* `at` *x*`,`*y* and `of a level not defined`, `of the other kind (indices or colours)`, `outside the level` or `in a packet of the wrong length` (a rectangle) |
| TEXPARAM | `texture` *id*`: no group, or no texture to make`; `texture` *id*`: wraps` *w*`, filters` *f* |
| PIXELS | `kind` *k*`,` *w*`x`*h* `in` *n* `dwords` |
| READ | `what` *w*`,` *w*`x`*h* (with ` (no drawable bound)`); `depth read from a drawable without depth`; `stencil read from a drawable without stencil`; *h* `rows at` *offset* `(stride` *s*`): outside video memory, or on the screen` |
| TEXREAD | `texture` *id* `level` *l*`: not defined`; `texture` *id* `level` *l*`: rows` *a* `to` *b* `at` *offset* `outside the level or video memory` |
| PALETTE | `texture` *id*`: base format` *b*`, width` *w* `in` *n* `dwords` (a length, base format or width outside the checks, or a texture it cannot make) |

TEXDELETE never drops: a texture the device does not have is nothing to delete.

### The device's report

Every 5 seconds at **OpenGL** while OpenGL works (a period with SHOWs, DRAWs, PIXELS, CLEARs, levels defined, rectangles or commands dropped), from the GPU thread's periodic work (after every packet it carries out and each time it wakes while idle, at least every 100 ms), at its first round 5 seconds or more after the last report; its counts start again each time (the first round only starts the clock). Two lines always: the drawing; then state, read-backs and drops. Then, each when it has something to say: the textures' two lines (what came in and what went to the host GPU; then what is kept and the arena), with texture work (levels defined, rectangles, palettes received, copies whole or patched, palettes copied, or textures dropped for room); the textures drawn by wrap mode, when any unit drew with a texture; the pipelines' line, when a pipeline was made (with the slowest), or only the render passes ended early when none was. For example:

```
VGPU98 OpenGL: 5.0 s: 300 SHOWs (worst frame 18 ms), 300 waits for vertical blanks (2100 ms); 9000 DRAWs of 210000 vertices (6000 with unit 1, 40 by the ICD's path), 0 PIXELS, 300 CLEARs
VGPU98 OpenGL:   9300 STATEs (41000 registers); 0 READs, 0 TEXREADs (each a stall); 0 commands dropped
VGPU98 OpenGL:   textures: 12 levels defined (12 of indices), 300 rectangles, 960 KB received, 3 palettes; to the host GPU 12 whole (340 KB), 300 patched (900 KB), 3 palettes
VGPU98 OpenGL:   textures kept: 840 (21000 KB); 0 dropped for room, 0 copied in again unchanged; the arena 25 of 128 MB in use
VGPU98 OpenGL:   textures drawn (one a unit a draw): 12000 repeating, 3000 with GL_CLAMP (sampled as clamp-to-edge), 0 with clamp-to-edge asked for
VGPU98 OpenGL:   41 pipelines made (41 kept, the slowest 1210 ms), 9900 render passes ended early, for work outside them
```

| Count | What it counts |
|---|---|
| s `s` | The period's seconds, from 86Box's millisecond ticks |
| SHOWs | SWAPs with SHOW the device carried out (a drawable with images), those with nothing visible to write among them; LOAD alone is not one |
| worst frame | The longest time in the period between two SHOWs of one drawable (each drawable's first SHOW starts its clock), in milliseconds |
| waits for vertical blanks | The SHOWs with a swap interval, timed: how many, and the milliseconds their waits took in all ([Swaps on the device](#swaps-on-the-device)) |
| DRAWs of … vertices | DRAWs taken (past their checks, whether or not a drawable is bound) and their vertices; `with unit 1` those with DRAW_TEX1, `by the ICD's path` those with DRAW_CLIP ([The ICD's own vertex path](#the-icds-own-vertex-path)) |
| PIXELS, CLEARs | Those taken |
| STATEs (registers) | STATE packets taken, and the register words they carried |
| READs, TEXREADs | Those taken: each waits for the host GPU, and the ICD waits for its fence, a stall for the program |
| commands dropped | All drops, those named and those not, then by opcode in brackets: `(TEXIMAGE 3, DRAW 1)` |
| levels defined (of indices) | TEXIMAGE packets with DEFINE taken, and of those the index levels |
| rectangles | TEXIMAGE packets without DEFINE taken (`glTexSubImage*`, `glCopyTexSubImage*`, a large level's rows) |
| KB received | Those TEXIMAGE packets' payloads |
| palettes | PALETTE packets taken |
| to the host GPU … whole | Whole copies of textures into the texel arena (an index texture's palette with it), and their kilobytes |
| patched | Textures whose changed rows were copied in alone ([Texturing and the texel arena](#texturing-and-the-texel-arena)), and those rows' kilobytes |
| palettes (to the host GPU) | Palettes copied in alone: a texture's palette changed while its copy was up to date, and the contexts' shared palettes |
| dropped for room | Textures whose arena copy went to make room for another texture or for a shared palette's range |
| copied in again unchanged | Whole copies of a texture whose texels had not changed since its last copy: one dropped from the arena (for room) and needed again |
| textures kept | The textures the device holds, every group's, and their texels' host memory (the second textures line) |
| the arena | The texel arena's megabytes in use, of its size |
| textures drawn (one a unit a draw): … repeating, … with GL_CLAMP (sampled …), … with clamp-to-edge asked for | Each unit's texture a DRAW or PIXELS drew with (so beside the DRAWs it can be nearly twice as many) (a drawable bound and something to draw, the texture defined and in the arena: once a unit a packet), by its wraps as TEXPARAM set them: `with GL_CLAMP` when either wrap is GL_CLAMP, else `with clamp-to-edge asked for` when either is clamp-to-edge, else `repeating`. `sampled as clamp-to-edge` or `sampled exactly`, by the setting "OpenGL GL_CLAMP" ([Texturing and the texel arena](#texturing-and-the-texel-arena)) |
| pipelines made (kept, the slowest) | Pipelines the GPU thread made in the period (not the warm-up's), those the device keeps (256 at most), and the longest one of the period took to make, in milliseconds; a period without one gives the render passes ended early alone ([Pipelines, the uniform block and the data chunks](#pipelines-the-uniform-block-and-the-data-chunks)) |
| render passes ended early, for work outside them | The times an open render pass was ended to record outside one: for a copy (a texture's whole copy or patch, a palette's copy, READ, LOAD, the SHOW's read-back, the swap's COPY, a drawable's images made or freed) and when drawing moves on to another drawable's or another buffer's render pass |

### The device's findings

Past real problems' thresholds, at **OpenGL**, each said once while the machine runs, worded `possible`, among the events, and each asking for a capture at the next SHOW ([The capture](#the-capture)):

| Line | When |
|---|---|
| `possible blank screen:` n `SHOWs in` s `s with nothing drawn` | A report's period with 10 SHOWs or more and no DRAW, PIXELS or CLEAR |
| `possible blank screen:` n `SHOWs in` s `s with nothing drawn but clears` | Three report periods in a row (15 s), each with 10 SHOWs or more and CLEARs but no DRAW or PIXELS, the counts the three periods' (Glide's rule: a program whose primitives never reach the device still clears every frame, and a black loading screen's few seconds are not a finding). A period with drawing, with fewer SHOWs, or without OpenGL's work starts the run again |
| `possible texture memory thrashing:` n `textures copied to the host GPU again in` s `s, unchanged, after being dropped for room (the arena` n `MB)` | 100 or more textures copied in again unchanged in a report's period |
| `possible missing palette:` n `draws in` s `s with an index texture whose palette has one entry, drawn in one colour (white unless set)` | 100 or more in a report's period: a DRAW or PIXELS with a unit drawing an index texture whose palette (its own, or the context's shared one while ENABLES' SHARED_PALETTE is set) has one entry, as every palette starts, counted for each such unit |

### The capture

At **OpenGL** the OpenGL device captures what a SHOW wrote, in a folder of the captures' folder: `vgpu98-textures` in the machine's folder, the same as the texture audit's dumps and the Glide device's captures, which 86Box sets at those levels and at OpenGL ([registers.md](registers.md), Texture dumps). It takes no GPU work of its own: the picture is the SHOW's own read-back, the textures the host's copies.

**When.** At a SHOW that wrote something visible (its rectangles written into video memory; a SHOW with nothing visible, or one that wrote nothing, is not looked at), one capture at most, the first that applies:

1. **An 86Box screenshot** (Action > Take screenshot) at OpenGL: the frame shown at the next SHOW, of whichever drawable; 10 a drawable, apart from the others' 8 (`an 86Box screenshot`).
2. **A finding** (any of the three, [The device's findings](#the-devices-findings)): at the next SHOW (`a finding`).
3. **10 seconds in**: once a drawable, at its first SHOW 10 seconds or more after its first SHOW (`10 seconds in`).

A finding and the 10-second capture count toward a drawable's 8. One that does not apply now waits for a later SHOW: a finding's behind a screenshot's, any of them while the last capture's files are still being written (the next SHOW tries again). A capture that cannot have its folder is used up without files: no captures' folder, a folder that cannot be made, or a path past 260 characters, which says once while the machine runs `VGPU98 OpenGL: no capture: the captures' folder's path is too long (`folder`)`.

**The folder** is `<date> <time> opengl capture` N (`2026-10-26 17-30-05 opengl capture 1`), N the device's count of captures from 1. The GPU thread takes the pixels, the textures and the text; a thread of its own writes the files (on the GPU thread itself if no thread can be made), and when they are done the event `VGPU98 OpenGL: capture` N `(`why`):` K `files in` folder.

**The files:**
- `front.png`: what the SHOW read back, the bounding box of its visible rectangles (so a part of it under another window is the front image's, not the screen's), the drawable's 565 or BGRA 8888 as RGBA, each channel widened to 8 bits, alpha opaque.
- `unit0.png`, `unit1.png`: the textures the drawable's last drawing context (the last whose DRAW went into it) draws with now, by its units' TEXTURE_ID registers: each one's level 0 with its border, from the host's copy, its first row (t = 0) at the top; RGBA8 as the device keeps it (luminance and intensity in all three colours, as TEXIMAGE fills them), an index level through the context's shared palette while its ENABLES' SHARED_PALETTE is set, else through the texture's own, each index within the palette's width. None for a unit without a texture, nor without such a context.
- `commands.txt`: a line `VGPU98 OpenGL capture` N `(`why`): drawable` id`,` WxH `(format` f`), shown` wxh`; context` id; then, with the context, `Registers:` and its 495 registers, `nnn: XXXXXXXX`, eight a line; then `The frame's packets (`n`):` a line each, its name, its length and its first six payload dwords (0 past its length), for the packets since the last SHOW looked at (of any drawable), the last 4096 of them.

**The frame's packets** are kept, opcode, length and first six dwords, in a ring of 4096, only while the level is on: the GPU thread's periodic work, which runs after every packet it carries out and each time it wakes while idle (at least every 100 ms), turns the noting on at its first round at OpenGL, before it looks at the report's 5-second period; at the other levels nothing is kept.

## The export list

`opengl/vgpu98gl.exports` lists the ICD interface's 18 functions under their plain names, as Windows' `opengl32.dll` looks them up: DrvCopyContext, DrvCreateContext, DrvCreateLayerContext, DrvDeleteContext, DrvDescribeLayerPlane, DrvDescribePixelFormat, DrvGetLayerPaletteEntries, DrvGetProcAddress, DrvRealizeLayerPalette, DrvReleaseContext, DrvSetCallbackProcs, DrvSetContext, DrvSetLayerPaletteEntries, DrvSetPixelFormat, DrvShareLists, DrvSwapBuffers, DrvSwapLayerBuffers, DrvValidateVersion; one name per line, sorted as the export table is. The extensions' functions are not exported: DrvGetProcAddress gives them ([DrvGetProcAddress](#drvgetprocaddress)).

- **The link.** The ICD's `[exports]` in `opengl/vgpu98gl.lnk` names the 18 stdcall symbols, with `names = undecorated` ([vglink.md](vglink.md)): each (`_DrvSetContext@12`) is exported under its plain name (`DrvSetContext`).
- **drvlint at the link.** `drvlint --strict --icd --exports` with the list: `--icd` applies the Glide DLLs' checks to the ICD (a DLL of its process's own: its base in a process's space, no section shared, an export directory, imports from Windows' own DLLs only, base relocations, a version resource; [pe-format.md](pe-format.md)), and the export table must hold exactly the list's names. The DLL is linked to `vgpu98gl.unchecked.dll` and copied to its name only once drvlint passes it.

## The host test

`tools/tests/test_ogl.c` runs the ICD on the host against a stand-in machine. It builds `opengl/*.c`, `driver32/gpu.c` and `driver32/logline.c` with `VGPU98_ICD_DLL`, natively, with `test_glide_low.c` for memory below 4 GB (the ICD keeps addresses in 32 bits). The stand-ins take names of their own, `test_` and the function's name, by compile definitions, and `DLLIMPORT` is empty:

- **KERNEL32.** TLS (a slot per stand-in thread), the heap (the host's, blocks zeroed), Sleep (counted, 5 ms of the clock each; with a held GPU and the scenario's count, the hold ends at that many sleeps), SetLastError (the error kept for the scenario to read), GetTickCount, GetCurrentProcessId (0A5C3E11h), GetCurrentThreadId (the scenario's thread: 1, 2 or 3), GetModuleFileNameA (the program `C:\GAMES\QUAKE2\QUAKE2.EXE`, the DLL `C:\WINDOWS\SYSTEM\VGPU98GL.DLL`), GetSystemDirectoryA (`C:\WINDOWS\SYSTEM`), SetUnhandledExceptionFilter (the filter kept, its calls counted), IsBadReadPtr (all memory readable).
- **GDI32.** CreateDCA (the display's alone), DeleteDC (no DC may be left open), ExtEscape: QUERYESCSUPPORT for VGPU98_ESC_INFO; VGPU98_ESC_INFO as the display driver answers it (its buffers checked, 8 and 32 bytes; this build's stamp, the scenario's, or the 16-byte head alone); VGPU98_ESC_ACCESS (its input size checked, begins and ends counted, never two begins without an end, the rectangle kept; a begin answers 0, nothing kept out, when the scenario puts the cursor far from the window).
- **USER32.** WindowFromDC (DC 4444h the window 5150h, 4445h the window 5160h, 4446h a memory DC with none), GetClientRect (640x480 and 320x200, or the scenario's), ClientToScreen (the first window's client area at 100,50), IsWindow.
- **DirectDraw.** DirectDrawCreate gives an object whose methods sit at their places: Release, CreateClipper, CreateSurface (the staging surface, checked: offscreen plain, 2112 by 256, at 400000h in video memory, its pitch 8448), SetCooperativeLevel (DDSCL_NORMAL alone), the clipper's SetHWnd and GetClipList (the scenario's rectangles; DDERR_REGIONTOOSMALL for a short buffer), the surface's Lock and Unlock.
- **The GPU.** BAR0's writes go to `test_reg_write` (REG_WRITE_HOOK): CONTROL's RESET and RUN, and at each RING_TAIL every packet from RING_HEAD to the new tail taken (none past the ring's end, the GPU running): the first 512 kept whole, a FENCE completing, READ and TEXREAD writing a pattern into the staging rows (565: red the column, green the row; 8888: BGRA, red the column, green the row, alpha 80h; depth 8000h plus the column; stencil the column plus twice the row; TEXREAD the scenario's texel, or for an index level the scenario's index plus the column, a byte each), and when the scenario asks, the GPU restarting (the block's `state_epoch` moved) as it takes the n-th packet, or a slow GPU, each FENCE landing a set number of milliseconds after its packet (at the first Sleep that passes it).
- **The log.** `driver32/logline.c`'s characters through LOG_PUT_HOOK (`test_log_put`), a line at each newline, each checked to go to the machine's BAR0 under the right lock (the block's while it is this build's, else the DLL's own); DEBUG reads 0 unless the scenario sets a level.
- **The machine.** A shared block of this build (16 MB of video memory, `mode_serial` 1, a 1024x768 desktop at the scenario's depth, its pitch the width's bytes) and BAR0 (its ID; the full profile less SYSRING and STATUSWB, so the ring is in video memory; COMMAND_SET 0, as GL 1.1's scenarios had it, so nothing is offered unless the scenario sets it). The first window is wholly visible (one clip rectangle, 100,50 to 740,530) unless the scenario covers it. DllMain gets the process attach (instance 11800000h) at the start.

Each scenario runs as a process of its own, `test_ogl` with its name, and is the suite's test `ogl_`name:

| Test | Checked |
|---|---|
| `ogl_none` | Another display driver (no escape): no formats, none set, no context, no DC left open |
| `ogl_noogl` | A card without PROFILE.OGL: no formats |
| `ogl_formats` | 16 bpp: 4 formats, each one's description (flags, colour and its shifts, depth, stencil, no accumulation or aux), format 5 refused; 32 bpp: 6 formats; 8 bpp: none; no layer plane; DrvGetProcAddress NULL for an extension's function (`glLockArraysEXT`) |
| `ogl_context` | No context before a pixel format; no format for a memory DC, nor format 9; no overlay context; the table's 336 entries, none NULL; GROUP OPEN, CONTEXT OPEN in it, DRAWABLE OPEN at 640x480 in format 1's drawable format, BIND; at the first clear the registers: the viewport and scissor box 640x480, DRAW_BUFFERS 2, the identity, ENABLES DITHER; DrvReleaseContext and DrvDeleteContext, then CONTEXT, GROUP and DRAWABLE CLOSE |
| `ogl_clear` | CLEAR's 8 dwords: colour and depth (no stencil on 565 with D16), the values clamped; `glClear` of another bit GL_INVALID_VALUE, once; a negative scissor box GL_INVALID_VALUE |
| `ogl_swap` | A window covered on its right half and its top: SWAP's COPY and SHOW, offset 0, pitch 2048, 16 bpp, the place 100,50, two rectangles, the second cut to the client area; three swaps, the third after the first's fence |
| `ogl_resize` | 32 bpp: a new client size at the next clear, DRAWABLE RESIZE to 800x600 in the 8888 D24S8 format; CLEAR's stencil value in its last dword |
| `ogl_held` | A GPU held: the batch waits (10 sleeps at least) and goes whole, the CONTEXT OPEN and CLEAR kept |
| `ogl_restart` | A GPU restart at the first batch's third packet: the next batch sends the group, the context, the drawable (OPEN), the binding and the registers again |
| `ogl_detach` | The process's end with a context current: CONTEXT and DRAWABLE CLOSE, nothing waited for |
| `ogl_threads` | A second thread's context leaves the first's; a context current on one thread refused on another; a thread ending with its context current lets it go, and a third thread makes it current and deletes it |
| `ogl_rehwnd` | Another format for a window being drawn into refused; a new window with the gone window's HWND takes format 2, its drawable in format 2's drawable format |
| `ogl_queries` | Every query's initial value and its setter's (GL 1.1, section 6.2): limits, enables, current values, matrices and stacks, lights and materials in eye coordinates, fog, the per-fragment state, the pixel store and transfer, maps, the identity strings, the errors |
| `ogl_attrib` | `glPushAttrib` and `glPopAttrib` with every group (the bound texture's parameters too), the stack's overflow and underflow, the client stack |
| `ogl_lists` | Display lists compiled, run, nested, by every `glCallLists` type, the nesting limit, the commands run at once, `glDeleteLists`, the errors |
| `ogl_select` | Selection's hit records on known geometry; the raster position's record; overflow |
| `ogl_feedback` | Feedback's tokens on known geometry, `glPassThrough`, arrays drawn, overflow |
| `ogl_eval` | Evaluators: curves, surfaces, grids and meshes through feedback, GL_ORDER, GL_DOMAIN, GL_COEFF, the errors |
| `ogl_rasterpos` | The raster position: transformed, its valid bit, colour and distance; clipped |
| `ogl_errors` | The error rules: between `glBegin` and `glEnd`, stacks, enums, the flags |
| `ogl_texobj` | Texture names, binding, parameters, priorities, residency, an undefined level's width, deletion unbinding |
| `ogl_draw` | Every primitive kind's DRAW: its kind and each index's vertex (the provoking vertex first in each triangle); the vertex as given; three primitives in one DRAW; a register's STATE between two DRAWs; a DRAW before a CLEAR recorded after it; a primitive of 1400 triangles split across DRAWs of at most 768 vertices and 4096 indices |
| `ogl_cpudraw` | The ICD's own path: GL_LINE's edges (DRAW_CLIP, DRAW_STIPPLE_ON, DRAW_POLYGON) in clip coordinates with their stipple counters; `glMaterial` inside `glBegin`, lit in the ICD; a stippled strip's counters; GL_LINES on the device's path; a concave polygon that clipping grows past its buffers, in feedback and through GL_LINE |
| `ogl_colormat` | Colour material's tracking at its enable, the device's material after its disable; a list operation over 65535 words |
| `ogl_teximage` | TEXIMAGE's level and texels, TEXPARAM first; completeness (TEXTURE_ID 0 until the filter needs no mipmaps); a luminance level through the transfer path, GL_TEXTURE_LUMINANCE_SIZE 8; `glTexSubImage`'s rectangle; the errors; proxies answered and never sent; a level larger than a packet as its size then rows; a border's texel at the level's corner; a 1D level and EN_TEXTURE_1D; a compiled `glTexImage1D` run |
| `ogl_pixels` | PIXELS' image, place, zoom and colours; a bitmap's bytes, origin and the raster position moved; nothing at an invalid raster position; the pixel tokens in feedback; the errors; a 300x300 image in several packets, every pixel sent; `glCopyPixels`' READ and PIXELS; a compiled `glDrawPixels` run |
| `ogl_readpixels` | READ's rectangle at the staging surface (made once), the pixels converted; luminance read; depth by the format's bits; the errors; `glGetTexImage`'s TEXREAD and texels; a luminance level read as red |
| `ogl_share` | `wglShareLists`: the second context runs the first's list, and opens in the first's group; a context with a list of its own refused; `wglCopyContext` of GL_LINE_BIT into a context no thread has, refused into a current one |
| `ogl_front` | A single-buffered window: LOAD before the first DRAW, `glFlush`'s SHOW, the cursor kept out of the client area (100,50 to 739,529) for the LOAD and for the SHOW, and let back each time; no second load; `glDrawBuffer(GL_FRONT)` loads first; a swap at the size drawn, the resize at the next `glClear`; no load after a swap; texture IDs the lowest free, a deleted one's used again |
| `ogl_ext` | A device of the card definition's command set (2.7): GL_EXTENSIONS exactly as the finished passes make it (the nine names), under 1 KB; for each extension every function of Wine's list given when the extension is in GL_EXTENSIONS and none when it is not (the edge-clamp extensions have none); NULL for a name in another case, one longer or shorter, NULL, and GL 1.1's `glBindTexture` |
| `ogl_ext11` | A device of command set 2.5: GL_EXTENSIONS "", no function of any of 2.6's extensions, the lock, active unit and unit count queries GL_INVALID_ENUM; no index level (GL_COLOR_INDEX8_EXT GL_INVALID_VALUE), the shared palette's enable, GL_TEXTURE_INDEX_SIZE_EXT and `glGetTexImage` of GL_COLOR_INDEX GL_INVALID_ENUM; SwapBuffers' SWAP with COPY and SHOW alone; no PALETTE; no STATE past 2.5's 433 registers |
| `ogl_swapint` | A window covering the screen, the cursor far: `wglSwapIntervalEXT` refused with no context current (`wglGetSwapIntervalEXT` 0); the default interval 1, sent as SWAP's INTERVAL 1; interval 0 sends none; 7 kept (`wglGetSwapIntervalEXT` answers 7) and sent as 4; −1 refused with ERROR_INVALID_DATA, 7 kept; a dialog over part of the window (two visible rectangles): the SWAP without an interval, and with it again once the dialog has gone; the cursor near the window: kept out once, the SWAP without an interval; a second window, not covering the screen, answers 1 and swaps without an interval; the first window's 7 again; a front-buffer `glFlush`'s SWAP is SHOW alone |
| `ogl_swapint1` | A single-buffered window covering the screen: SwapBuffers' SWAP is SHOW alone |
| `ogl_cva` | The lock's functions; its errors (a negative first, a count of 0, an unlock while not locked, a lock while locked, an unlock between `glBegin` and `glEnd`), its two queries, `glPushClientAttrib` and its pop putting the lock back; every mode by `glDrawArrays` and `glDrawElements` giving `glBegin`'s DRAWs; an element given twice one vertex (4 against 6) and the current colour the last element's; Quake 3's lock (positions alone, a colour array enabled after the lock and pointed at again read at each call, the locked positions from the conversion, `glArrayElement` reading it too, another vertex array under the lock read, the array read as it is after the unlock); 3000 vertices split across DRAWs of whole triangles; a draw while compiling a list sends nothing, and the list's run goes through `glBegin`; a polygon mode's draw on the ICD's own path |
| `ogl_cva2` | Further cases: Quake 3's fog (a locked colour array rewritten and pointed at again), the vertex array pointed at again under the lock, a draw partly outside the locked range against `glBegin`'s; every mode at counts 1, 2, 3, 4, 5 and 7 with an element repeated, against `glBegin`'s; byte colours, short normals and one-coordinate texture arrays converted; the current coordinates and normal after a direct draw; GL_COMPILE_AND_EXECUTE through `glBegin`; a stippled strip on the ICD's own path; no vertex array: nothing drawn, the current colour still set |
| `ogl_multitex` | Two units; the active and client units' first values; a third unit and an ARB token to SGIS refused; SGIS's unit 1 ARB's; unit 1's texture, enable, environment, generation and matrix apart from unit 0's, in queries and in the registers sent (unit 1's block); each unit's current coordinates by `glMultiTexCoord*ARB` and `glMTexCoord2fSGIS`, a third unit refused, `glTexCoord` into SGIS's coordinate set 1; a DRAW with unit 1 (DRAW_TEX1, 20 floats, unit 1's coordinates at the provoking vertex); the direct path with unit 1's array through the client unit, and the array's queries by client unit; a DRAW of 16 floats with unit 1 off; the ICD's own path's unit 1 through its matrix; unit 1's raster coordinates and PIXELS_TEX1; push and pop of GL_TEXTURE_BIT, GL_ENABLE_BIT and GL_CURRENT_BIT (the active unit, unit 1's environment, enable and coordinates) and of the client unit and unit 1's array; a list compiling `glActiveTextureARB` and `glMultiTexCoord2fARB` and running `glClientActiveTextureARB` at once; changes far apart in separate STATEs; a texture deleted while bound to unit 1 |
| `ogl_multitex2` | Further cases: over 40 rounds of changes spread across both units, the matrices, a light, fog, the viewport, the point parameters and the shared palette's enable, every STATE taken into a copy of the device's registers, which must equal the ICD's after each round; `glActiveTextureARB` refused between `glBegin` and `glEnd`, `glMultiTexCoord2fARB` taken there; a compiled `glTexCoord` run into SGIS's coordinate set 1; `glMultiTexCoordPointerSGIS` setting unit 1's array, the client unit kept; unit 1's bound texture's parameters restored by GL_TEXTURE_BIT's pop |
| `ogl_pointparam` | The four values' defaults and queries; R_POINT_PARAMS 0 with the defaults; the attenuation by the scalar call and another name GL_INVALID_ENUM, a negative minimum or threshold GL_INVALID_VALUE; Quake 2's values in the point registers, R_POINT_PARAMS 1; the size rule against hand-worked sizes (at eye distance 10 and 100, the minimum far away, the maximum close up, the fade below the threshold); the ICD's own path's point size in STIPPLE and its fade in alpha; GL_POINT_BIT's push and pop; both calls compiled and run |
| `ogl_palette` | A texture's palette given before its first image sent after its first TEXPARAM, before its TEXIMAGE; an index level's TEXIMAGE (base INDEX, rows padded to a dword, four bytes a dword), GL_TEXTURE_INDEX_SIZE_EXT 8 and the internal format; 4-bit indices kept as 8; an index sub-image through the index offset; colours into an index level, an index level from colours and from the colour buffer GL_INVALID_OPERATION; widths not a power of two or over 256 GL_INVALID_VALUE, an index table format, a table of depths and a target that names no palette GL_INVALID_ENUM; a proxy too wide left at width 0 without an error, one that fits answered and never sent; the parameter queries, `glGetColorTableEXT`'s entries and its refusal of GL_COLOR_INDEX; `glColorSubTableEXT` sending the palette whole and no texels, entries past the width and a proxy refused; `glGetTexImage` through the texture's palette, then through the shared palette (a 256-entry PALETTE for texture 0, ENABLES' SHARED_PALETTE), the enable kept across GL_ENABLE_BIT's pop; both table calls compiled, sent when the list runs; the shared palette sent again after a GPU restart |
| `ogl_palette2` | Further cases: Quake 2's start-up (the shared palette from 256 RGB bytes, enabled, then a mip chain of GL_COLOR_INDEX8_EXT levels, complete and drawn with); a colour level among index levels leaving the texture incomplete; the direct index path and the unpacking path (GL_COMPILE_AND_EXECUTE) giving the same TEXIMAGE under a row length, skipped pixels and rows and an alignment of 2; the index offset through the transfer path; an index sub-image compiled, sent when the list runs; `glGetTexImage` of an index level's indices, and of GL_COLOR_INDEX from a colour level GL_INVALID_OPERATION; `glPointParameterfEXT` with GL_DISTANCE_ATTENUATION_EXT refused at once and when its list runs, the attenuation unchanged; the ICD's own point sized by the eye distance with w taken out; a second context's shared palette its own, starting at one entry, the first context's unchanged |
| `ogl_cross` | The extensions together: unit 0 with an RGBA texture and unit 1 with an index texture and its own palette, then the shared palette; a locked `glDrawElements` with unit 1's array (DRAW_TEX1), Quake 3's array pointed at again; the ICD's sized points with unit 1's coordinates, and in feedback |
| `ogl_gate25` | A device of command set 2.5: the point parameter, client unit and lock queries, `glGetTexImage` of GL_COLOR_INDEX and the shared palette's enable GL_INVALID_ENUM; a DRAW of 16 floats and a PIXELS of 17 dwords with texturing on; no PALETTE, no INDEX base |
| `ogl_diag` | Diagnostics **OpenGL**: the prefix (`VGPU98 opengl` stamp `QUAKE2.EXE: `); `loaded` with 9 extensions (the device's command set 2.7), the build and the display driver's, the file in the system folder, the program; no stale line; the extensions offered in their three lines; window 5150's pixel format 1 (colour 5-6-5, depth 16, stencil 0, double-buffered); the fault catcher in place; `glEnable`'s GL_INVALID_ENUM named once as `set_cap`'s, `BindTexture`'s GL_INVALID_OPERATION between `glBegin` and `glEnd`, the errors still recorded; `glFooEXT` named once and no line for a name there; pixel format 99 refused; the swap decisions, each said once (on the desktop, interval 0, sent, covered in part with 2 rectangles, the cursor kept out); the report 6 seconds on (swaps, primitives by path, no arrays-and-lists line with both 0, the ICD's path's reasons, GL errors); an incomplete texture told at its 50th draw with its level 0, levels and filter, and not again; the slow path at the third report in a row, the first of those reports with a display list called and its arrays-and-lists line (`arrays drawn direct 0, display lists called 1`), once; every line fitting the writer with a 20-character program name |
| `ogl_diagmin` | Minimal: the `loaded` line alone (9 extensions, the device's command set 2.7), no error, miss or report lines; the fault catcher in place |
| `ogl_diagoff` | Off: no line, no fault catcher |
| `ogl_diagstale` | Another stamp in the escape's answer: the possible stale driver line, no `build` line, `loaded` still |
| `ogl_diaghead` | The escape's head alone: the possible stale driver line with no stamp |
| `ogl_diagother` | Another build's shared block: no formats, no function; one `no OpenGL: another build's display driver` line, under the DLL's own lock; no fault catcher |
| `ogl_diagnoogl` | A card without PROFILE.OGL: no formats; the `no OpenGL: the GPU's profile` … `has no OpenGL device (an older 86Box)` line |
| `ogl_edgeclamp` | A device of the card definition's command set (2.7): GL_CLAMP_TO_EDGE taken for GL_TEXTURE_WRAP_S by `glTexParameteri` and for _T by `glTexParameterf`, without an error, TEXPARAM's wraps 2 and 2; both answered by `glGetTexParameteriv` and `glGetTexParameterfv`; GL_CLAMP still sent as 1; the wrap kept across GL_TEXTURE_BIT's push and pop; a compiled `glTexParameteri` of it not set while compiling, set when the list runs; GL_CLAMP_TO_EDGE as a filter GL_INVALID_ENUM |
| `ogl_ext26` | A device of command set 2.6: GL_EXTENSIONS exactly the seven names of 2.6; GL_CLAMP_TO_EDGE GL_INVALID_ENUM, the wrap left GL_REPEAT; no TEXPARAM with a wrap above 1, GL_CLAMP sent as 1 |
| `ogl_patience` | The ICD's 5 seconds: with each FENCE landing 2 seconds late, `glFinish` waits for it and the GPU is not restarted; 6 seconds late, the GPU is restarted once |

A failing check prints its line in `test_ogl.c` and its words; a scenario with any failure exits 1, and an unknown scenario 2.

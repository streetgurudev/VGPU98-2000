# The Glide drivers

`glide2x.dll` and `glide3x.dll` give Glide, the Voodoo cards' own 3D interface, to Windows programs on the VGPU98 2000: the Glide 2.x and Glide 3.x interfaces, in the files and under the names a Voodoo3's driver installs. They answer as a Voodoo3 3000 to every program. Their log has a Diagnostics level of its own, **Glide**. `grSstWinOpen` takes the screen through DirectDraw's exclusive mode and opens a **session** on the card's **Glide device**, a device of 86Box's own beside the DirectX renderer (command set 2.4, [gpu-commands.md](gpu-commands.md)): the DLLs send Glide's state, vertices, texture downloads and frame buffer transfers through the GPU's command ring, the host GPU draws them into 16-bit buffers of the session's own, and while a session shows, the screen shows its front buffer. Every export does what a Voodoo3's Glide does, but what [is deferred](#what-is-deferred), which each DLL says in a line when a program first asks for it; four of Glide 2's do nothing, as they do nothing on a Voodoo3 either. Source: `glide/` (C), linked by `vglink` with `glide/glide2x/glide2x.lnk` and `glide/glide3x/glide3x.lnk`; the device in 86Box's `src/video/vid_vgpu98_glide.c`; tested on the host by `test_glide2x`, `test_glide3x` and `test_gltex` ([The host test](#the-host-test)).

The interfaces are restated from the Glide 2.4 and 3.0 reference manuals, the Glide programming guide and the 2.43 and 3.00 SDK headers; the Voodoo3's own facts (the export tables, the identity values, the version strings, the INF's file list, what its Glide does at each call) from its retail 1.07.00 driver files and 3dfx's Voodoo3 Glide sources, and the Voodoo's pixel arithmetic from the Voodoo specifications, in the reference library. All of it was read for facts only: 3dfx's sources carry 3dfx's own licence, and nothing of them is in the tree.

## The files

| File | Interface | Version information | Source |
|---|---|---|---|
| `glide2x.dll` | Glide 2.x, as Glide 2.61 | 2.61.0.658 (`2.61.00.0658`) | `glide/glide2x/` |
| `glide3x.dll` | Glide 3.x, as Glide 3.10 | 3.10.0.658 (`3.10.00.0658`) | `glide/glide3x/` |

The INF copies both into WINDOWS\SYSTEM (destination 11) beside `vgpu98dx.dll` ([driver-package.md](driver-package.md)), where a Voodoo3's driver puts its own, so a program that loads Glide by name finds them. Windows looks in the program's own folder first: a game that ships its own `glide2x.dll` loads that one. There is no `glide.dll` (Glide 2.1.1), `glide2x.ovl` (DOS Glide) or splash screen DLL.

Each DLL is built from its own folder's sources, the part both share in `glide/`, and three files of the DirectX DLL's:

| Source | What it holds |
|---|---|
| `glide/card.c` | The entry point, finding the card, the log, the DLL check, the call record, the recorder of the last calls and the fault catcher |
| `glide/screen.c` | The session, the batches, the state registers as the DLL keeps them, the screen (DirectDraw, the staging surface), swaps, gamma, the deferred features' lines |
| `glide/lfb.c` | The frame buffer: locks and regions |
| `glide/draw.c` | Triangles, points and lines as the device takes them; Glide 2's delta0 |
| `glide/state.c` | The pixel pipeline's state calls, the registers at the open, the saved state |
| `glide/tex.c` | Texture memory, downloads, palettes, a TMU's settings |
| `glide/g3df.c` | The utility library's `.3df` files |
| `glide/gu.c` | The utility library's arithmetic both DLLs share: guFogTableIndexToW, the fog tables, guEncodeRLE16 |
| `glide/glidedll.h`, `glcore.h`, `gpuenv.h` | What both DLLs share; the core's interface; what `driver32/gpu.c` needs of a Glide DLL |
| `glide/glide2x/` | `main.c` (the board, init, shutdown, the window), `board.c` (the saved state, the statistics, the board's other calls), `frame.c` (the screen, the frame buffer, the board's status), `pixel.c` (the pixel pipeline's calls and the utility library's presets), `vertex.c` (Glide 2's vertices and draws, the clipper, the multipass helpers), `texture.c` (textures, the texture manager, `.3df`, run-length data), `util.c` (the byte swaps), `stubs.c` (the four exports that do nothing), `glide2x.h` |
| `glide/glide3x/` | `main.c` (grGet, grGetString, grGetProcAddress, the window and its context, grReset, the saved state, grQueryResolutions, the board's other calls), `frame.c`, `pixel.c`, `vertex.c` (the vertex layout, the coordinate spaces, the draws and vertex arrays, grEnable), `texture.c`, `glide3x.h` |
| `driver32/rt.c`, `driver32/logline.c`, `driver32/gpu.c` | What gcc expects of a C library; the log's lines; the command ring, built in with `VGPU98_GLIDE_DLL`, which has it take `glide/gpuenv.h` in place of the DirectX DLL's header ([Batches and the ring](#batches-and-the-ring)) |
| `card/vid_vgpu98_glcmd.h`, `card/vid_vgpu98_glcpu.h` | Command set 2.4's values, and the device's arithmetic (a texture's layout in memory, texel decoding, the W float, the fog table), shared with 86Box as `vid_vgpu98_gpu.h` is |

It is compiled as `vgpu98dx.dll` is, with `VGPU98_DLL_CFLAGS`: gcc `-m32`, freestanding, against `driver32/include`'s own headers, for the i686 (a Pentium Pro or later, so CMOV may be used) without MMX or SSE, x87 floating point. The build keeps a DLL only once drvlint has passed it ([The export lists](#the-export-lists)).

The version resource ([pe-format.md](pe-format.md)): the version numbers above as file and product versions, file type 3 (a driver), subtype 8 (installable), OS 32-bit Windows on MS-DOS, and a U.S. English string table in code page 1252 (`040904E4`), as a Voodoo3's Glide DLLs have them; the strings `CompanyName` "VGPU98 Project", `FileDescription` "VGPU98 2000 driver for Glide 2 programs" (Glide 3), `FileVersion` and `ProductVersion` the version text, `InternalName` `glide2x` (`glide3x`), `OriginalFilename` the file's name, `ProductName` "VGPU98 2000", `LegalCopyright` "Copyright 2026 VGPU98 Project contributors. GPL-2.0-or-later." The numbers are those of the final Voodoo3 release's version strings (its glide2x.dll's own number is 2.60.0.658, below 2.61.0.658; its glide3x.dll's is 3.10.0.658), never older than a Voodoo3 release's copies (1.03 to 1.07), so Windows replaces those without asking which to keep.

## Per-process DLLs

Unlike `vgpu98dx.dll`, the Glide DLLs are ordinary DLLs: image base 10000000h (the Voodoo3's), in the loading process's own address space, no section shared ([pe-format.md](pe-format.md)). Each process that loads one has its own copy of its data, as Glide's state belongs to the program on a Voodoo3 too. What processes share is the display driver's: the card's registers, the command ring and its lock, and the log's line lock, all found through the shared block ([directx-hal.md](directx-hal.md)).

| DLL | Functions | For |
|---|---|---|
| KERNEL32 | GetModuleFileNameA, Sleep, GetSystemDirectoryA, GetEnvironmentStringsA, FreeEnvironmentStringsA | Finding the card, the DLL check, the waits |
| | GetTickCount, GetCurrentProcessId, GetCurrentThreadId | The ring's waits and its lock's owner, the session's ID, the recorder, the front buffer's flushes |
| | CreateFileA, ReadFile, SetFilePointer, CloseHandle | `.3df` files |
| | SetUnhandledExceptionFilter, IsBadReadPtr | The fault catcher |
| GDI32 | CreateDCA, ExtEscape, DeleteDC | Finding the card |
| USER32 | GetActiveWindow, GetForegroundWindow | The window, when the program gives none |
| | GetClassNameA, GetWindowTextA, GetWindowRect, GetWindowLongA, GetWindowThreadProcessId | The window's lines: the one taken at the open, the one in front when the mode is lost |
| DDRAW | DirectDrawCreate | The screen's mode and the staging surface |

Each is in Windows 98's own KERNEL32, GDI32, USER32 or DDRAW, the DLLs a Voodoo3's Glide imports from.

The entry point, DllMain (`_DllMain@12`), is `glide/card.c`'s, the same in both DLLs. At the process attach it keeps the DLL's instance handle, for its file's path ([The DLL check](#the-dll-check)), and looks for no card: GDI's calls are kept out of the loader's lock, so the card is found at the first Glide call instead. At the process detach:
1. A window still open closes. A FreeLibrary (DllMain's `reserved` NULL) closes it whole, as `grSstWinClose` does, DirectDraw and the desktop's mode included, as a Voodoo3's Glide does. As the process ends (`reserved` not NULL) the session closes on the device alone, waiting for nothing inside the loader's lock: the ring lock is let go first if a killed thread of the process held it, what the program left unsent is dropped, and the CLOSE goes only if the ring lock is free (the batch dropped at once if the GPU is held); DirectDraw, which gives the screen back itself as the process ends, is left alone.
2. The call record ([The call record](#the-call-record)).
3. Without a `grGlideShutdown` since the load, the last calls ([The recorder and the fault catcher](#the-recorder-and-the-fault-catcher)).

It returns TRUE.

## Finding the card

At the process's first call of any export, once a process (a second thread calling meanwhile waits, `Sleep(1)` at a time, for the first's answer):

1. A DC for the display: `CreateDCA("DISPLAY", NULL, NULL, NULL)`.
2. Every log line's prefix, `<tag> <PROGRAM>: ` ([The log](#the-log)): the program's file name from GetModuleFileNameA, without its folder, at most 20 characters of it with any spaces at its end dropped, `?` if Windows gives none. The program's full path is kept for the DLL check.
3. ExtEscape QUERYESCSUPPORT (8), its input the escape number VGPU98_ESC_INFO (7098h), must answer above 0; then ExtEscape VGPU98_ESC_INFO, its input the question (`vgpu98_escquery_t`: ESC_INFO_MAGIC and the output buffer's size, 32) and a 32-byte output buffer, must answer above 0 with the answer's `magic` ESC_INFO_MAGIC and `size` 32, or 16 from a display driver of before the build stamp, which writes the answer's head alone ([win98-display-driver.md](win98-display-driver.md), The Glide DLLs' escape). The DC is deleted.
4. The answer's `shared_linear` must not be 0, and the block there must start with SB_MAGIC (42534756h, `VGSB`).
5. The block's `regs_linear` (BAR0's flat address) must not be 0, and BAR0's ID must read 38394756h (`VG98`, [registers.md](registers.md)).
6. DEBUG is read for the log's levels ([The log](#the-log)), once: the level changes only with the machine's reset.
7. The block's `version` and `size`, and the answer's `sb_version`, must be this build's: SB_VERSION (2) and the block's size.

The question, `vgpu98_escquery_t`, and the answer, `vgpu98_escinfo_t` (`driver32/shared.h`). The question carries the output buffer's size because the display driver's Control is never told it:

| Structure | Offset | Field | Value |
|---|---|---|---|
| `vgpu98_escquery_t` (input, 8 bytes) | 0 | `magic` | ESC_INFO_MAGIC, 49454756h (`VGEI`) |
| | 4 | `out_size` | The output buffer's bytes: 32, `sizeof(vgpu98_escinfo_t)` |
| `vgpu98_escinfo_t` (output, 32 bytes) | 0 | `magic` | ESC_INFO_MAGIC |
| | 4 | `size` | The bytes written: 32, or 16 (ESC_INFO_HEAD, the head: every field before `stamp`) |
| | 8 | `shared_linear` | The shared block's flat address; 0 if the display driver could not find it |
| | 12 | `sb_version` | SB_VERSION as the display driver was built |
| | 16 | `stamp` | The display driver's build stamp ([registers.md](registers.md)), NUL-padded; only in a 32-byte answer |

With every check passed the card is there: the DLL keeps the block, BAR0 and video memory's flat address (the block's `fb_linear`), writes its lines under the shared block's line lock and reports one board. Its `loaded` line and the DLL check ([The DLL check](#the-dll-check)) follow at once. Otherwise it reports no board, as a Voodoo3's Glide does without its card, and opens no window:

| Where it stops | Cause | The log |
|---|---|---|
| Steps 1-4 | No display DC, another display driver, one without the escape, an answer of another size, no shared block | Nothing (no register window to write to) |
| Step 5 | No register window, or another card's | Nothing |
| Step 7 | A display driver from another build | One event line saying so (below), then nothing more from the process. The block's first five fields keep their offsets in every version, so BAR0 is reached all the same; that line goes under a lock word of the DLL's own, not the block's, and the DLL then drops the register window, as `vgpu98dx.dll` does |

Whether the card draws Glide is PROFILE bit 10, GLIDE ([gpu-registers.md](gpu-registers.md)), which 86Box sets when its Glide device starts on the host GPU: `grSstWinOpen` reads it at each call, and without it refuses the window ([The session and the screen](#the-session-and-the-screen)); everything else answers as with the card.

## The identity

A Voodoo3 3000 with 16 MB and two TMUs, whatever the card's video memory: the numbers are Glide's own, and no setting changes them.

### Glide 2

| Export | With the card | Without |
|---|---|---|
| grSstQueryBoards(hwconfig) | `num_sst` 1, nothing else written; FXTRUE | `num_sst` 0; FXTRUE |
| grSstQueryHardware(hwconfig) | The whole GrHwConfiguration cleared, then board 0 (below); FXTRUE | Cleared (`num_sst` 0); FXFALSE |
| grGlideGetVersion(version) | "2.61.00.0658" and its terminator, 13 bytes | The same |

grSstQueryBoards and grGlideGetVersion may be called before grGlideInit, as Glide allows.

Board 0 of GrHwConfiguration:

| Field | Value |
|---|---|
| `type` | GR_SSTTYPE_VOODOO (0): Glide 2's type for the Voodoo Graphics, Banshee and Voodoo3 |
| `fbRam` | 12 (MB): the 16 MB less the TMUs' 2 MB each |
| `fbiRev` | 11002h |
| `nTexelfx` | 2 |
| `sliDetect` | FXFALSE |
| `tmuConfig[0]`, `tmuConfig[1]` | `tmuRev` 11002h, `tmuRam` 2 (MB) |

GrHwConfiguration is 148 bytes: `num_sst`, then four boards of 36 bytes, each a `type` and a 32-byte union whose VoodooConfig is `fbRam`, `fbiRev`, `nTexelfx`, `sliDetect` and two `tmuConfig` (`tmuRev`, `tmuRam`), every field 32 bits. `glide/glide2x/glide2x.h` checks these sizes when it is compiled. The texture memory a program can use is not these 2 MB once a window is open but what its buffers leave ([Texture memory](#texture-memory)), as `grTexMaxAddress` says.

### Glide 3

**grGet(pname, plength, params)** returns the bytes it wrote, or 0, Glide's failure, with nothing written. `params` must not be NULL, and `plength` must be the query's size exactly; any other length fails. With the card:

| pname | Query | Bytes | Value |
|---|---|---|---|
| 01h | GR_BITS_DEPTH | 4 | 16 |
| 02h | GR_BITS_RGBA | 16 | 5, 6, 5, 0 (the frame buffer's 5-6-5) |
| 03h | GR_FIFO_FULLNESS | 8 | FFFFh, 0FFFF01Fh: the FIFO empty, and the status as grSstStatus gives it idle |
| 04h | GR_FOG_TABLE_ENTRIES | 4 | 64 |
| 05h | GR_GAMMA_TABLE_ENTRIES | 4 | 256 |
| 06h | GR_GLIDE_STATE_SIZE | 4 | 448: grGlideGetState's bytes ([Glide 3's contexts and the saved state](#glide-3s-contexts-and-the-saved-state)) |
| 07h | GR_GLIDE_VERTEXLAYOUT_SIZE | 4 | 108 |
| 08h | GR_IS_BUSY | 4 | 1 while the device is at work on what was sent (the batch sent first), else 0 |
| 09h | GR_LFB_PIXEL_PIPE | 4 | 1 (taken as plain writes: [deferred](#what-is-deferred)) |
| 0Ah | GR_MAX_TEXTURE_SIZE | 4 | 256 |
| 0Bh | GR_MAX_TEXTURE_ASPECT_RATIO | 4 | 3 (8:1, as its log2) |
| 0Ch | GR_MEMORY_FB | 4 | With a window, the bytes a Voodoo3 gives its buffers ([Texture memory](#texture-memory)); before one A00000h (10 MB): Glide 2's 12 MB less 2, as a Voodoo3's Glide 3 counts it then |
| 0Dh | GR_MEMORY_TMU | 4 | Each TMU's memory now: 200000h (2 MB) before a window ([Texture memory](#texture-memory)) |
| 0Eh | GR_MEMORY_UMA | 4 | 1000000h (16 MB) |
| 0Fh | GR_NUM_BOARDS | 4 | 1 |
| 10h | GR_NON_POWER_OF_TWO_TEXTURES | 4 | FXFALSE |
| 11h | GR_NUM_FB | 4 | 1 |
| 12h | GR_NUM_SWAP_HISTORY_BUFFER | 4 | 8 |
| 13h | GR_NUM_TMU | 4 | 2 |
| 14h | GR_PENDING_BUFFERSWAPS | 4 | The swaps sent and not yet shown ([Pacing and swaps](#pacing-and-swaps)) |
| 15h | GR_REVISION_FB | 4 | 11100h |
| 16h | GR_REVISION_TMU | 4 | 11100h |
| 17h | GR_STATS_LINES | 4 | The lines asked for since the load or the last grReset of them |
| 18h-1Ch | GR_STATS_PIXELS_AFUNC_FAIL, _CHROMA_FAIL, _DEPTHFUNC_FAIL, _IN, _OUT | 4 | 0: the device keeps no pixel counts |
| 1Eh | GR_STATS_POINTS | 4 | The points asked for |
| 1Fh | GR_STATS_TRIANGLES_IN | 4 | The triangles asked for |
| 20h | GR_STATS_TRIANGLES_OUT | 4 | The program's triangles sent to the device (not a point's or a line's own triangles) |
| 22h | GR_SWAP_HISTORY | 32 | Zeros |
| 23h | GR_SUPPORTS_PASSTHRU | 4 | FXFALSE |
| 24h | GR_TEXTURE_ALIGN | 4 | 16 |
| 25h | GR_VIDEO_POSITION | 8 | 0, 0 |
| 26h | GR_VIEWPORT | 16 | grViewport's x, y, width and height, as integers |
| 27h | GR_WDEPTH_MIN_MAX | 8 | 0, FFFFh |
| 28h | GR_ZDEPTH_MIN_MAX | 8 | FFFFh, 0 |
| 2Ah | GR_BITS_GAMMA | 4 | 8 |

Every other query fails (1Dh, 21h and 29h are grReset's alone); the first 16 such queries in a process are noted in the log, with the card (a wrong length is not noted). Without the card, GR_NUM_BOARDS answers 0 (4 bytes) and every other query fails.

**grGetString(pname):**

| pname | | String |
|---|---|---|
| A0h | GR_EXTENSION | " ", one space: the extension list is empty (with its spaces, so a program searching it for " NAME " finds nothing), as no extension is implemented |
| A1h | GR_HARDWARE | "Voodoo3 (tm)"; "ERROR" without the card |
| A2h | GR_RENDERER | "Glide" |
| A3h | GR_VENDOR | "3Dfx Interactive" |
| A4h | GR_VERSION | "3.10.00.0658" |
| others | | "ERROR" |

**grGetProcAddress(name)** returns NULL for every name: no extension is offered, so no extension's function. The first 16 calls in a process are noted in the log. (The extensions a Voodoo3's Glide 3 takes without `grGetProcAddress`, the fog coordinate, the 6666 palette and mirrored textures, are taken: [Drawing](#drawing), [Textures](#textures).)

## The session and the screen

A window is a **session** on the device: its buffers on the host GPU, its state registers, its texture memory. One a process (the DLL's statics are its process's), up to four on the device at once ([gpu-commands.md](gpu-commands.md), Command set 2.4).

### grSstWinOpen

`grSstWinOpen(hWnd, resolution, refresh, colour format, origin, colour buffers, aux buffers)`:

1. **The card.** Without it, refused (no line). Without PROFILE.GLIDE (no 3D on the host GPU, or an 86Box from before Glide drew), refused, with `the card draws no Glide: …` once a process and then the refused window's line ([The log](#the-log)).
2. **The arguments.** Colour buffers 1-3, aux buffers 0 or 1, a colour format 0-3 (GR_COLORFORMAT_ARGB, ABGR, RGBA, BGRA), an origin 0 or 1 (GR_ORIGIN_UPPER_LEFT, LOWER_LEFT); otherwise refused, with its line.
3. **A window already open** closes first, as at `grSstWinClose`.
4. **What a Voodoo3's Glide makes of the rest:** a resolution it does not know (GR_RESOLUTION_NONE FFh, the window's size, among them) is 640x480; a refresh rate it does not know (GR_REFRESH_NONE FFh among them) 60 Hz; two colour buffers come with an aux buffer. A window whose buffers (rows of 64-pixel tiles, 4 KB pages) leave the Voodoo3's 16 MB less than 2 MB of textures and the command FIFO's 64 KB is refused, as a Voodoo3's Glide refuses it (`grSstWinOpen:` WxH `with` c `colour and` a `aux buffers leaves a Voodoo3 no room for textures` at Glide, and the refused window's line): 1920x1440 with two colour buffers and an aux buffer, say. The resolutions are GR_RESOLUTION_320x200 (0) to GR_RESOLUTION_2048x2048 (17h), the refresh rates GR_REFRESH_60Hz (0) to 120Hz (8) as [the log](#the-log) names them.
5. **The ring** is chosen, as the DirectX DLL chooses it ([Batches and the ring](#batches-and-the-ring)).
6. **The screen**, through DirectDraw, as a Voodoo3's Glide takes it (its DLLs import DDRAW, and its source calls DirectDrawCreate, SetCooperativeLevel and SetDisplayMode):
   - The window is `hWnd`, or with none the active window (GetActiveWindow), else the foreground window.
   - `DirectDrawCreate(NULL)`, then IDirectDraw2 from it (QueryInterface), else IDirectDraw. Without DirectDraw the line `grSstWinOpen: DirectDraw is not there; the screen keeps its mode`, and the session opens without a screen of its own or a staging surface.
   - `SetCooperativeLevel(window, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN)` (11h). Refused, or no window: `DDSCL_NORMAL` instead, with a line; the mode is then set for the desktop.
   - `SetDisplayMode` at the window's size and 16 bpp, through IDirectDraw2 with the refresh rate in Hz, and if that fails at the mode's default (0); through IDirectDraw without a rate. When the desktop is already that size at 16 bpp (the shared block's mode), the mode is set at 8 bpp instead, as a Voodoo3's Glide sets it, so the desktop's return when the program ends, however it ends, is a change of mode, which ends the session on the device (below; the screen shows Glide's own buffers meanwhile). A mode Windows refuses is said in a line, and the window opens all the same: the screen shows it scaled to the mode Windows has ([Showing Glide](#showing-glide)).
   - **The staging surface**, through which the frame buffer is read and written ([The frame buffer](#the-frame-buffer)): an offscreen surface in video memory (DDSCAPS_OFFSCREENPLAIN, DDSCAPS_VIDEOMEMORY, 32-bit RGB), `stride_write / 4` pixels wide and as many rows as `(stride_write + stride_read) × height` bytes need, Locked once (DDLOCK_WAIT) for its address and Unlocked. It holds the **write rows** (`stride_write` bytes each, the window's height of them) then the **read rows** (`stride_read` each). The strides are a Voodoo3's frame buffer's, 2048 pixels a row: 8192 bytes for writes (32-bit pixels; 16-bit writes use 4096 of them) and 4096 for reads. It is taken only as one run of video memory at or above the block's `fb_linear`, its pitch the write stride, inside video memory. Without room, rows of the window's width alone (its width × 4 and × 2 bytes), said in a line; without that either, a line, and `grLfbLock` and the region calls fail.
7. **The mode's serial.** The shared block's `mode_serial` is kept: a later change of it (another program's mode, the desktop back) ends the session ([What ends a session](#what-ends-a-session)).
8. **The session's ID:** the process ID's low 28 bits, and in the top 4 a count 1-15 of the DLL's opens in the process.
9. **The registers** as a Voodoo3's Glide leaves them at the open (its assertDefaultState; [gpu-commands.md](gpu-commands.md), the state registers), the clip window the whole window and the origin asked for; delta0 off, mipmap dithering not allowed ([Drawing](#drawing), [Textures](#textures)).
10. **The device takes it:** OPEN (the size, the colour buffers, the aux buffer, the origin, the staging surface's offset in video memory and its bytes), every register in one STATE, and the gamma table of 1.3, as a Voodoo3's Glide loads it at the open ([Gamma](#gamma)), in one batch, waited for. A batch dropped because the GPU is held (a mode change or a DOS box, waited for 2 seconds) refuses the window, with a line, and gives the screen back. A GPU restart before the device ran the batch (the shared block's `state_epoch` moved since it went into the ring) may have lost the OPEN: it goes again, once, with a line at Glide.
11. Glide 2 empties the utility library's texture manager, as a Voodoo3's Glide does at the open ([The texture manager](#the-texture-manager)); Glide 3 sets window coordinates, the viewport the window and the depth range 0-1 ([Glide 3's vertices](#glide-3s-vertices)).

It returns FXTRUE (Glide 2) or the window's context, 1 (Glide 3); refused, FXFALSE or 0. At Glide a line says what opened: `window open: 640x480, 2 colour buffers and a depth buffer, upper-left origin; mode set, exclusive`.

### What ends a session

| Event | The DLL | The device |
|---|---|---|
| `grSstWinClose`, `grGlideShutdown`, a FreeLibrary | CLOSE (none for a session lost already), everything sent waited for, the staging surface released, RestoreDisplayMode if the mode was set, `DDSCL_NORMAL` if exclusive, DirectDraw released | The session ends; the desktop comes back |
| The process ends | CLOSE if the ring lock is free, nothing waited for ([Per-process DLLs](#per-process-dlls)) | Likewise |
| `grSstWinOpen` again | The window closes, then opens | The same ID opened again replaces the session |
| A change of the screen's mode (the shared block's `mode_serial` moved, seen when a batch is about to go, once the GPU is taken, at a batch's end, or at a frame buffer transfer) | The session is **lost**, said once with the window in front then: nothing more is sent for it, the batch about to go included (the ring let go with a FENCE alone, so the device drops nothing for a session it ended; `grSelectContext` answers FXFALSE), and `grSstWinClose` sends no CLOSE but gives the screen back | The display driver's mode change holds the GPU with RESET, which ends every session |
| The GPU held for a VGA mode (a full-screen DOS session, the blue-screen messages) | (nothing: its batches wait and drop meanwhile, as above) | The session is kept, and the screen given back (`session` id `hidden for a DOS session; shown again at its next swap`): Windows comes back in the same mode, and the session shows again at its next swap |
| The BIOS's GPU_RESET at POST, the machine's reset | (nothing) | Every session ends at that RESET ([gpu-registers.md](gpu-registers.md)), the desktop back |
| Four sessions open and a fifth opened | (nothing) | The one not on the screen with the fewest swaps ends (a process that ended without closing) |

The GPU's other resets, the drivers' own starts and recoveries (a DirectX or Glide DLL restarting a GPU after an error or after it made no progress), leave sessions as they are: the DLL sends its registers again ([Batches and the ring](#batches-and-the-ring)).

### Showing Glide

While a session shows, the display composes the screen from its front buffer instead of video memory:
- **At every swap**, and when a front buffer drawn into needs showing ([Pacing and swaps](#pacing-and-swaps)), the GPU thread copies the session's front buffer, its 16-bit 565 pixels in rows of the session's width, into the display's Glide buffer on the host GPU (2048 × 2048 pixels at most), its submission done and waited for under the display's layer lock, and hands the display the frame's size and the session's gamma table. The session is then the one the screen shows: the last to swap.
- **The display** (`glide_show.comp`, a compute shader of Glide's own) turns each pixel into 86Box's: each channel widened to 8 bits by repeating its top bits, as a Voodoo's video output does, then through the gamma table; a frame of another size than the screen is scaled to it, each screen pixel taking the frame's nearest (`x × width / screen width`, likewise for rows), so a mode Windows refused shows the session scaled to the mode it has. The whole frame is composed at each new frame, gamma table or screen geometry, and a blanked screen is black. Video memory waits meanwhile.
- **When the session ends**, the desktop comes back: every row of video memory's frame is composed again.
- **A new gamma table** for the session on the screen shows at once.
- **Without the Glide screen's objects** on the host GPU (they failed to be made: `VGPU98 display: no Glide screen on the host GPU; Glide's frames go to video memory`), or without the display on the host GPU, a frame goes into video memory where the screen shows it, when the visible mode there is 16 bpp and holds the session's size; otherwise frames are not shown, said once (`the screen is not a` WxH `16-bit mode on the host GPU: the session's frames are not shown`).

## Batches and the ring

The DLLs drive the GPU's command ring with the DirectX DLL's own code, `driver32/gpu.c`, built into each with `VGPU98_GLIDE_DLL` and `glide/gpuenv.h`: the same ring lock, packets, fences, waits, takeovers and recoveries ([directx-hal.md](directx-hal.md), The 32-bit DLL). What two instances of it must agree on is in the shared block or the GPU's registers, since a Glide DLL's statics are its process's: the lock's take reads RING_TAIL every time, as another kind of client may have rung since. Its lines follow the DLL's prefix with `ring:` ([The log](#the-log)). A Glide DLL's batches draw for no Direct3D context (the context hooks see none), and its ring counters are kept at Glide but not written: the device writes the Glide report ([The device's diagnostics](#the-devices-diagnostics)).

- **The batch.** A DLL records its commands into a buffer of its own, 64 KB (16384 dwords), each a packet header, the session's ID and the payload ([gpu-commands.md](gpu-commands.md), Command set 2.4), and copies it into the ring as one batch under the ring lock, ended by a FENCE: when the buffer fills; at a swap; at `grSstIdle`, `grFinish`, `grFlush`, `grSstIsBusy`, `grSstStatus` and GR_IS_BUSY; at a frame buffer lock, a write lock's unlock and every region transfer; and at the open and the close. A wait for a fence always follows the batch that holds it. No packet passes F000h bytes (VGPU98_GL_PACKET_BYTES).
- **The state goes first.** The registers changed since they were last recorded go as one STATE (the range from the lowest changed to the highest) before the next command that reads them: a draw, a clear, a texture download, a palette, a frame buffer transfer.
- **Draws together.** Triangles with nothing between them go into one DRAW: while the open DRAW is the batch's last command and its payload stays within F000h bytes less 4 dwords, the next triangles are added to it (up to 319 triangles a DRAW).
- **A held GPU.** A batch waits up to 2 seconds for a GPU held by a mode change or a DOS box (looking every 5 ms), then is dropped, said in a line once a hold (`a batch of` n `dwords dropped: the GPU is held (the next ones while it is are not said)`): its fence is 0, and nothing waits for it. The unload at the process's end waits for nothing.
- **A GPU that lost what went.** The DLL keeps the registers as it recorded them and as the device had them before the batch being recorded. When the shared block's `state_epoch` moved since the DLL's last batch went into the ring (a GPU restart, or a batch abandoned by a takeover), or moved while it went (a restart for room, which loses what the GPU had not run), the registers as they stood go first in the next batch, every one of them in one STATE: the session itself outlasts a restart. A dropped batch has its registers go again too.
- **The rest of a batch** is lost with a GPU that stops while the batch goes in (the ring's own recovery restarts it).
- **The front buffer.** Drawing into the front buffer shows without a swap: while the render buffer is the front one (or the window has one colour buffer), a clear, a draw or a frame buffer write into it sends the batch at least every 16 ms.

## The state registers

Glide's state is the device's registers, 72 dwords a session (VGPU98_GS_*; their layout and values: [gpu-commands.md](gpu-commands.md), Command set 2.4), not Direct3D's render states: each call checks its arguments as a Voodoo3's Glide does and stores Glide's own numbers.

| Calls | Register | What the DLL stores |
|---|---|---|
| grClipWindow | CLIP_MIN, CLIP_MAX | Its four values, each at most FFFFh: the minimums inclusive, the maximums exclusive |
| grRenderBuffer | RENDER_BUFFER | GR_BUFFER_FRONTBUFFER or BACKBUFFER; another is ignored |
| grColorMask, grDepthMask | MASKS | RGB, ALPHA, DEPTH bits. Alpha on with an aux buffer and depth buffering off asks for the aux buffer as an alpha buffer ([deferred](#what-is-deferred)) |
| grSstOrigin | ORIGIN | GR_ORIGIN_UPPER_LEFT or LOWER_LEFT; another is ignored |
| grColorCombine, grAlphaCombine, guColorCombineFunction, guAlphaSource | COLOR_COMBINE, ALPHA_COMBINE | Function (8 bits), factor (4), local and other (2 each), invert |
| grConstantColorValue | CONSTANT | The colour in the window's colour format, as ARGB |
| grAlphaBlendFunction | BLEND | The colour's source and destination factors (4 bits each); the alpha's source ONE unless ZERO or ONE was asked, its destination ZERO likewise, as a Voodoo3's Glide makes them |
| grAlphaTestFunction, grAlphaTestReferenceValue | ALPHA_TEST | The function (3 bits), the reference (8) |
| grAlphaControlsITRGBLighting | ITRGB_LIGHTING | 1 or 0 |
| grDepthBufferMode, grDepthBufferFunction, grDepthBiasLevel | DEPTH | A mode 0-4 (another is ignored), the function (3 bits), the bias (its low 16 bits) |
| grFogMode, grFogColorValue, grFogTable | FOG, FOG_COLOR, FOG_TABLE | The device's fog mode (below); the colour as RGB; the 64 entries |
| grChromakeyMode, grChromakeyValue | CHROMA, CHROMA_VALUE | On for any value but 0; the colour as RGB |
| grCullMode | CULL | 0-2; another is ignored. The DLL culls ([Drawing](#drawing)) |
| grDitherMode | DITHER | 0-2; another is ignored |
| grLfbConstantAlpha, grLfbConstantDepth | LFB_CONSTANT | The alpha (low 8 bits), the depth (bits 31:16) |
| The texture calls | TMU0's and TMU1's | [Textures](#textures) |

**Colours** a program gives (grConstantColorValue, grChromakeyValue, grFogColorValue, grBufferClear) are in the window's colour format, GR_COLORFORMAT_ARGB, ABGR, RGBA or BGRA, and are stored as ARGB.

**grDisableAllEffects**, as a Voodoo3's Glide does it: blending ONE, ZERO, ONE, ZERO; the alpha test ALWAYS; chroma key off; depth bias 0; depth buffering off; fog off.

**Fog modes.** The device's: 0 off, 1 iterated alpha, 2 the table at 1/w, 3 iterated Z, 4 the table at the fog coordinate, with GR_FOG_MULT2 (100h) and GR_FOG_ADD2 (200h). Glide 2's grFogMode values 0-3 are the device's own (GR_FOG_WITH_ITERATED_ALPHA 1, GR_FOG_WITH_TABLE 2, GR_FOG_WITH_ITERATED_Z 3); Glide 3's 1 (the table at the fog coordinate, GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT) is the device's 4, 2 (GR_FOG_WITH_TABLE_ON_Q) its 2, 3 (Z) its 3. Any other mode is off (Glide 3's iterated alpha, 4, which a Voodoo3's Glide does not take, among them).

**Glide 2's presets.** guColorCombineFunction's 17 presets, GR_COLORCOMBINE_ZERO (0) to GR_COLORCOMBINE_ONE (16), are each the grColorCombine a Voodoo3's library makes of it; the two delta0 ones (GR_COLORCOMBINE_ITRGB_DELTA0, 3, and TEXTURE_TIMES_ITRGB_DELTA0, 7) turn delta0 on, every other preset off ([Glide 2's vertices](#glide-2s-vertices)). guAlphaSource's four (GR_ALPHASOURCE_CC_ALPHA, ITERATED_ALPHA, TEXTURE_ALPHA, TEXTURE_ALPHA_TIMES_ITERATED_ALPHA) are its grAlphaCombine calls. grColorCombine leaves delta0 as it is, as a Voodoo3's Glide 2 does.

## Drawing

Every primitive reaches the device as triangles, as a Voodoo3's Glide builds them, in the device's vertex: 16 floats of Glide's own values ([gpu-commands.md](gpu-commands.md), DRAW): x and y in the origin's coordinates, 65535/z (OOZ), 1/w (OOW), the iterated colour (0-255 each), each TMU's s/w, t/w and 1/w, and Glide 3's fog coordinate.

### Glide 2's vertices

GrVertex, 60 bytes: x, y, z, r, g, b, ooz, a, oow, then for each TMU sow, tow, oow. Read into the device's vertex by grHints' **STW hint** (GR_HINT_STWHINT, 0; `grHints(GR_HINT_ALLOW_MIPMAP_DITHER, mask)`, 3, allows mipmap dithering; other hints are ignored):

| Device vertex | From |
|---|---|
| X, Y, OOZ, OOW, R, G, B, A | x, y, ooz, oow, r, g, b, a |
| SOW0, TOW0 | TMU0's sow, tow |
| OOW0 | TMU0's oow when TMU0 is active and hinted GR_STWHINT_W_DIFF_TMU0 (02h), else oow |
| SOW1, TOW1 | TMU1's sow, tow when TMU1 is active and either TMU0 is not or the program hints GR_STWHINT_ST_DIFF_TMU1 (10h), else TMU0's |
| OOW1 | TMU1's oow when TMU1 is active and hinted GR_STWHINT_W_DIFF_TMU1 (08h), else OOW0 |
| FOG | 0 |

**Active TMUs**. A Voodoo3's Glide sends a TMU its own coordinates only while the TMU is active: its own combine uses its texel. That means a factor of LOCAL or LOCAL_ALPHA (their ONE_MINUS forms too), or a colour or alpha function other than ZERO and SCALE_OTHER. At the open both TMUs combine ZERO by NONE: inactive. The setup unit receives:
1. the vertex's oow, which reaches every TMU;
2. an active TMU0's own oow, only with its hint, which reaches TMU1 too;
3. an active TMU0's s and t, to both;
4. an active TMU1's own oow, only with its hint;
5. an active TMU1's own s and t, when TMU0 is not active or with its hint.

So while TMU0 passes TMU1's texel through (SCALE_OTHER by ONE, say), TMU1 has its own s and t and the vertex's 1/w, unless the program hints TMU1's 1/w. A program may leave TMU1's oow unwritten then, and some do. A TMU that is not active has its texel reach no output. The host still samples it, and its LOD still matters to a combine factor of DETAIL or LOD_FRACTION, from values a Voodoo3 would not have sent. And while neither the colour nor the alpha combine reads the texture, a Voodoo3 sends no TMU values at all; the device's are then read by nothing.

**Delta0.** With a delta0 preset (above), the iterated colour is the flat one: grConstantColorValue4's red, green and blue (floats, 0-255) at every vertex, alpha the vertices'. It lasts until a preset without it or the next window's open.

### Triangles, points and lines

- **x and y** are taken as a Voodoo's setup unit takes them: 12.4 fixed point in 16 bits, so whole multiples of 4096 pixels fall away, into −2048 up to, not including, 2048 (x 2100 is −1996, −2049 is 2047, 2048 is −2048: in the 2048-wide and 2048-high modes a right or bottom edge at exactly 2048 wraps too, as in the 12.4 model). A program's snap bias left in its vertices, 3 << 18 = 786432 (192 × 4096: Unreal's Glide renderer adds it and never takes it away), so falls away as on a Voodoo, exactly: the multiple is taken away in double (exact at any x87 precision, the program's control word in force), and the program's fraction kept whole. A point, line or triangle with a vertex whose x or y is not a number, infinite, or at or past ±2^27 is dropped (counted); the compares are quiet, so a program that unmasked the x87's invalid-operation exception gets none from a NaN. Neither programs nor Glide 3's clip coordinates are clipped against this range.
- **A triangle**'s signed area `(bx − ax)(cy − ay) − (cx − ax)(by − ay)`, from the program's own coordinates (as the setup unit takes the area; a bias's differences are exact), culls it: zero (or not a number) draws nothing, GR_CULL_NEGATIVE (1) drops negative areas, GR_CULL_POSITIVE (2) positive ones (on an upper-left screen, a positive area is clockwise). The device does not cull. Then its corners are wrapped: corners wrapped by different multiples of 4096 (a vertex past ±2048 where the others are not) draw nothing, counted. Near the wrap's edge the Voodoo draws nothing of them either (its spans run apart, or its rows out of order); farther out it draws a wrong shape (86Box's Voodoo model): the DLL draws nothing either way, so it never drops a triangle the Voodoo draws right. A line's two triangles likewise, built from the program's own ends. A point's position is wrapped before its pixel is found.
- **A point** is a small triangle over the pixel its position falls in, as the Voodoo snaps it: x and y to 1/16 of a pixel (`floor(v × 16 + 0.5) / 16`), then the pixel's corner (floor), and legs of 1.5 pixels from it, so its centre alone is inside.
- **A line** is a one-pixel-wide parallelogram, two triangles: the end with the smaller y first; half a pixel each way across its major axis (y moved for an x-major line, x for a y-major one); a line of no length draws nothing.
- Points and lines are never culled; they are counted (GR_STATS_POINTS and _LINES), not as triangles.

### Polygons and the clipper

- **Glide 2's polygons**, grDrawPolygon (by index, nothing without an index list) and grDrawPolygonVertexList (in order), are fans of triangles (0, i, i + 1); the planar ones are drawn as the plain ones, as on a Voodoo3. The antialiased forms (grAADraw*) are drawn plain ([deferred](#what-is-deferred)).
- **The utility library's clipped draws**, guDrawTriangleWithClip, guAADrawTriangleWithClip and guDrawPolygonVertexListWithClip (3 to 100 vertices; others draw nothing): a polygon wholly inside the clip window is drawn as a fan; otherwise it is clipped against the window's right, bottom, left and top (x below the maximum, y below it, x at least the minimum, y likewise), every parameter of the vertex interpolated linearly in screen space, then x and y snapped to 1/16 of a pixel as the utility library snaps them (adding and taking 2^19), and drawn as a fan. Room for 256 vertices: a polygon that would pass it (a twisted one) is dropped.
- **The multipass helpers** (guMPInit, guMPTexSource, guMPTexCombineFunction, guMPDrawTriangle) draw in one pass with the first texture ([deferred](#what-is-deferred)): guMPDrawTriangle sets guMPTexSource's first mipmap as guTexSource does, then draws the triangle.

### Glide 3's vertices

A vertex is read through **grVertexLayout(param, offset, mode)**: each parameter's byte offset in the program's vertex, on when `mode` is not 0. The parameters, in the order a Voodoo3's Glide 3 keeps them in its 108-byte layout (each a mode and an offset, then the colour's kind and two spare dwords): GR_PARAM_XY (01h, two floats), Z (02h), W (03h), A (10h), FOG_EXT (05h), RGB (20h, three floats), PARGB (30h, a packed ARGB dword), ST0 (40h, two floats), ST1 (41h), Q (04h), Q0 (50h), Q1 (51h). ST2 and Q2, a third TMU's, are ignored. The colour is floats or packed, by the last of A, RGB and PARGB turned on. A parameter the layout leaves out keeps its value from the last vertex (at the start every value 0, the three 1/w's 1). `grReset(GR_VERTEX_PARAMETER)` empties the layout; grGlideGetVertexLayout and grGlideSetVertexLayout copy its 108 bytes.

**Window coordinates** (grCoordinateSpace GR_WINDOW_COORDS, 0, set at every open), the program's own values as a Voodoo3's Glide 3 builds its parameters:

| Device vertex | From |
|---|---|
| X, Y | x, y |
| OOZ | Z (65535/z, the Z buffer's value) |
| OOW | Q (1/w) |
| R, G, B, A | RGB and A (0-255), or PARGB's bytes |
| SOW0, TOW0 | ST0 (s/w, t/w in the 256 level's texels) |
| SOW1, TOW1 | ST1, else ST0's |
| OOW0 | Q0, else OOW |
| OOW1 | Q1; else an active TMU0's Q0 (as Glide 2's rules: what reaches TMU1 after TMU0's), except with FOG_EXT and Q both in the layout (a Voodoo3's Glide 3 then puts the fog coordinate in the vertex's W and sends each active TMU its own: Q1 or else Q); else OOW |
| FOG | FOG_EXT, else OOW: table fog on the fog coordinate without one indexes by q, as a Voodoo3's Glide 3 does |

**Clip coordinates** (GR_CLIP_COORDS, 1): divided by w (the W parameter, else 1; 1/w is 0 for a w of 0) and mapped through **grViewport(x, y, width, height)** (from the origin's corner; a negative width or height mirrors) and **grDepthRange(near, far)**: `X = x/w × width/2 + x0 + width/2`, `Y = y/w × height/2 + y0 + height/2`, OOZ `= z/w × (far − near)/2 × 65535 + (far + near)/2 × 65535`; OOW is Q/w (else 1/w); FOG the fog coordinate over w (else OOW); RGB and A are 0-1, times 255 (PARGB's bytes as they are); s and t over w times the TMU's scale from its last grTexSource (256 for the long side, `256 >> |aspect|` for the short one; TMU1's from ST0 as TMU0's, with TMU0's scale, without ST1, as the Voodoo3 sends ST0 to both); OOW0 Q0/w, else Q/w (1/w with FOG_EXT: the TMUs are then sent the vertex's 1/w, the fog coordinate taking the vertex's W); OOW1 Q1/w, else an active TMU0's Q0/w (not with FOG_EXT and W both in the layout), else as OOW0's fallback.

### Glide 3's draws

grDrawPoint, grDrawLine and grDrawTriangle read their vertices through the layout and draw as above; grAADrawTriangle draws plain. **grDrawVertexArray(mode, count, pointers)** takes an array of vertex pointers, **grDrawVertexArrayContiguous(mode, count, vertices, stride)** vertices `stride` bytes apart:

| Mode | Draws |
|---|---|
| 0 GR_POINTS | A point each |
| 1 GR_LINE_STRIP | Lines (i, i + 1) |
| 2 GR_LINES | Lines of pairs |
| 3 GR_POLYGON | A fan (0, i − 1, i) |
| 4 GR_TRIANGLE_STRIP | Triangles (0, 1, 2), (2, 1, 3), (2, 3, 4) …: every other one's first two swapped, so each winds as the first does |
| 5 GR_TRIANGLE_FAN | A fan (0, i − 1, i) |
| 6 GR_TRIANGLES | Threes |
| 7 GR_TRIANGLE_STRIP_CONTINUE | The strip just drawn going on: each vertex one more triangle from its last two (a strip of fewer than three vertices kept them for it, as a Voodoo3's setup unit does); without a strip just drawn, a new strip |
| 8 GR_TRIANGLE_FAN_CONTINUE | The fan (or polygon) just drawn going on: each vertex one more triangle around its first vertex, a fan of fewer than three kept as a strip is; without one, a new fan |

A continuing mode goes on only straight after a strip (fan) or its continuation; after anything else (another mode, grDrawPoint, grDrawLine, grDrawTriangle, the open, grGlideSetState) it starts a new one. Other modes draw nothing. With `grEnable(GR_AA_ORDERED)`, grDrawPoint, grDrawLine, grDrawTriangle and the arrays' points, lines and triangles (not their strips, fans and polygons) say antialiasing is deferred and draw plain; `grEnable(GR_ALLOW_MIPMAP_DITHER)` allows mipmap dithering; grDisable turns either off, and the other modes are ignored.

## The pixel pipeline

As the device implements it (86Box's `glide.vert` and `glide.frag`, one shader whose settings come from a uniform block filled from the session's registers at each DRAW). Pipelines differ only in what Vulkan fixes: blending's colour factors, the colour mask, the depth test, its function and mask, and whether the session has an aux buffer; they go into the pipeline cache's file with DirectX's ([pipeline-cache.md](pipeline-cache.md)), 256 kept.

1. **Position.** x and y snapped to the Voodoo's 1/16-pixel grid (`floor(v × 16 + 0.5) / 16`); pixel (0, 0)'s centre is (0.5, 0.5); with the lower-left origin y counts from the bottom, the flipped rows moved down 1/256 of a pixel so a centre on a horizontal edge falls on the side a Voodoo puts it. Pixels whose centres are inside, by Vulkan's rule; the scissor is the clip window (empty: nothing drawn). Depth comes from the fragment shader, so no plane clips a triangle.
2. **Parameters** are interpolated linearly in screen space, as the Voodoo iterates them; s/w and t/w are divided by their 1/w per pixel. An iterated colour, alpha or Z is read as the Voodoo reads it: its integer part after 1/4096 is added (the iterators' 12 fraction bits), −1 as 0, 256 as 255, others their low 8 bits.
3. **The TMUs**, TMU1 first, its output TMU0's other; each with no texture reads white.
   - **Its LOD**, in the 256 level's texels: log2 of the larger of the coordinates' derivatives across and down, kept to −16..16, plus the LOD bias; clamped to the texture's levels (only its largest without mipmapping). At an LOD below the largest level's (biased, before that clamp) the magnification filter applies, else the minification filter. GR_MIPMAP_NEAREST_DITHER, where allowed, adds 0 to 0.75 by the pixel's place in a 4x4 pattern.
   - **Its level** is the LOD's integer part; a split texture's (even or odd levels alone) the level of its parity at that LOD or the next smaller.
   - **Its texel:** point sampling, or bilinear among the four around the point, texel centres at +0.5; wrapped by the coordinate's low bits, clamped, or mirrored (Glide 3's extension).
   - **Its combine:** each channel `((other − local) × (factor + 1)) >> 8`, the subtraction and the addition of local or local alpha as the function says, clamped to 0-255, then inverted if asked; local is the TMU's texel, other the TMU upstream's output (none for TMU1). The factor is ZERO, LOCAL, OTHER_ALPHA, LOCAL_ALPHA, the **detail factor** (TEXTURE_ALPHA: `(detail bias − LOD before the bias in quarters) << scale`, clamped to 0-255 and to the detail maximum), the **LOD fraction** (TEXTURE_RGB: the clamped LOD's fraction in 256ths), or one minus any of them. With trilinear on, the LOD fraction's sense turns over at every odd LOD, so one setting blends each level with the next.
4. **The colour combine's inputs:** other is iterated, the texture (TMU0's output) or the constant colour; local the constant colour or iterated, or with ITRGB lighting the constant where the texture's alpha has its top bit set, else iterated. The alpha combine's other is iterated, the texture's or the constant's alpha; local the constant's, Z's top 8 bits (GR_COMBINE_LOCAL_DEPTH) or iterated.
5. **Chroma key:** a pixel whose colour combine's other colour (8:8:8, alpha aside) is the chroma value is not drawn.
6. **The colour and alpha combine units**, as a TMU's (above): the colour's factor LOCAL, OTHER_ALPHA, LOCAL_ALPHA, TEXTURE_ALPHA or TEXTURE_RGB (the texture's channel); the alpha's LOCAL, OTHER_ALPHA, LOCAL_ALPHA or TEXTURE_ALPHA.
7. **The alpha test:** the pixel's alpha against the reference, by GR_CMP_*.
8. **Fog:** its alpha from iterated alpha, the table at 1/w, Z's top 8 bits, or the table at the fog coordinate. The table at a value: the value as the Voodoo's 16-bit W float (below), the entry its top six bits pick, toward the next by the eight bits after them. Then each channel `c + ((fog − c) × (fog alpha + 1)) >> 8`, clamped; with MULT2 `fog × (fog alpha + 1) >> 8`; with ADD2 `c × (255 − fog alpha) >> 8`; with both 0.
9. **Depth**, with depth buffering on and an aux buffer: Z buffering (modes 1 and 3) takes Z's integer part, 0-65535; W buffering (2 and 4) 1/w as the Voodoo's 16-bit float: a 4-bit count of the leading zeros of its fraction and the 12 bits after the leading one, inverted, plus one (0 at 1/w of 1 and above, FFFFh at 2^−16 and below and for a 1/w below 0), growing with w. Modes 1 and 2 add the depth bias, clamped. The value is the aux buffer's exactly (a 16-bit depth image, value / 65535), compared by the depth function and written with the depth mask; frame buffer reads give it back unchanged. The compare-to-bias modes (3, 4) compare as usual ([deferred](#what-is-deferred)).
10. **Blending** of the colour by its source and destination factors (GR_BLEND_*; as a source factor 2 is the destination's colour and 6 one minus it, as a destination factor the source's; ALPHA_SATURATE as a source, and as a destination PREFOG_COLOR, for which the fogged colour stands in). Destination alpha reads 1: the colour buffers have no alpha. Blending is on unless the factors are ONE and ZERO.
11. **To the 5:6:5 buffer** with the colour mask (red, green, blue): unblended pixels are dithered as a Voodoo3 dithers, an ordered 4x4 or 2x2 pattern of 16 steps between the two 5- (6-) bit values around the colour (`(c × 31 + d × 16) / 255`, d 0-15), or cut to 5:6:5 without dithering; blended pixels go out as they are, rounded by the host's blend ([deferred](#what-is-deferred)).

## Textures

### Texture memory

Each TMU's memory is the device's, a session's own (8 MB each on the device, zero at the open), never video memory. The DLL computes what a Voodoo3's Glide computes, with the device's own arithmetic (`card/vid_vgpu98_glcpu.h`):
- **Each TMU's memory now:** before a window, 2 MB; with one, the board's 16 MB less the window's buffers and the command FIFO's 256 KB, halved and rounded down to 16, at most 8 MB, as a Voodoo3's Glide finds it. The buffers: each `((width + 63) & ~63) × 2 × height` bytes (rows of 64-pixel tiles) rounded up to 4 KB, times the colour and aux buffers (GR_MEMORY_FB). At 640x480 with two colour buffers and a depth buffer, 7335936 bytes a TMU.
- **grTexMinAddress** is 0 and **grTexMaxAddress** that memory less 16, for either TMU.
- **A texture** is its format (GR_TEXFMT_*), its smallest and largest levels and its aspect (Glide 3's log2 numbers in the core: the level's long side, 1 to 256 as 0-8; width over height, −3 to 3; Glide 2's numbers, levels 0 (256) to 8 and aspects 0 (8:1) to 6 (1:8), are turned into them), and the levels it holds (GR_MIPMAPLEVELMASK_EVEN 1, ODD 2, BOTH 3: a texture split between TMUs, its levels' parity the same in either numbering). A level's short side is never below 1; a texel is one byte for formats 0-7, two for 8-15.
- **Where a level lies**, as a Voodoo3's Glide places it: from the texture's start address, the levels from large to small, each after those above it down from the 256 level (of its parity alone for a split texture), counted from the start less the largest level's own such offset rounded down to 16.
- **grTexTextureMemRequired and grTexCalcMemRequired** (either order of the levels): the levels' bytes rounded up to 16, a split texture's small levels counted padded (1x1, 2x1 and 1x2 as 4 texels; 4x1, 8x1 and their tall forms as twice theirs). The programming guide's example, 128x64 down to 8x4 at 8 bits, is 10912 bytes. A description a Voodoo3's Glide would not take (a format 7 or 0Fh, levels or aspects outside their ranges, the smallest larger than the largest, another level mask) needs 0 bytes, and is neither downloaded nor set.

### Downloads and palettes

- **grTexDownloadMipMap(tmu, start, evenOdd, info)**: every level from the largest to the smallest, the data packed level after level; a level outside the mask is skipped, its data passed over. **grTexDownloadMipMapLevel** one level, **grTexDownloadMipMapLevelPartial** its rows `start` to `end` (the data at the first of them; Glide 3's answers FXFALSE for arguments it refuses, else FXTRUE). A level the texture does not hold is skipped, as a Voodoo3's Glide skips it. Each goes as TEXDATA packets of at most F000h − 64 bytes to the TMU's memory at the level's place.
- **The palette** (grTexDownloadTable and grTexDownloadTablePartial with GR_TEXTABLE_PALETTE, 2): its entries, `start` to `end` read from the whole table's pointer, go to both TMUs as a Voodoo3's Glide sends them, as RGB. Glide 3's GR_TEXTABLE_PALETTE_6666_EXT (3): each channel's top six bits, widened again by repeating their top two, alpha included; a P_8 texture set after it reads through it (the device's format 6). NCC tables (GR_TEXTABLE_NCC0, NCC1, grTexNCCTable) are not taken: YIQ textures are [deferred](#what-is-deferred).

### A texture's source and settings

| Calls | TMU register | What the DLL stores |
|---|---|---|
| grTexSource(tmu, start, evenOdd, info) | TEX_ADDRESS, TEX_INFO, TEX_SOURCE | The start, the description (format, levels, aspect, mask, the TMU's memory), 1. A YIQ format says it is deferred |
| grTexCombine; Glide 2's grTexCombineFunction and guTexCombineFunction | TEX_COMBINE, TEX_INVERT | Colour and alpha function (8 bits) and factor (4); the two inverts. The presets (GR_TEXTURECOMBINE_ZERO to ONE: ZERO, DECAL, OTHER, ADD, MULTIPLY, SUBTRACT, DETAIL, DETAIL_OTHER, TRILINEAR_ODD, TRILINEAR_EVEN, ONE) set colour and alpha alike |
| grTexFilterMode | TEX_FILTER | Minification and magnification, point or bilinear |
| grTexClampMode | TEX_CLAMP | s and t: wrap or clamp in Glide 2 (bit 0 alone); in Glide 3 also mirror (2), another value wrap |
| grTexMipMapMode(tmu, mode, lodBlend) | TEX_MIPMAP | GR_MIPMAP_DISABLE, NEAREST or NEAREST_DITHER (another is ignored), and trilinear; dithering only where allowed (grHints, grEnable) and without trilinear, else NEAREST, as a Voodoo3's Glide has it |
| grTexLodBiasValue | TEX_LOD_BIAS | The bias in quarter steps, −8.0 to 7.75, as a Voodoo3's Glide rounds it: `trunc((v + 0.125) / 0.25)` quarters |
| grTexDetailControl(tmu, bias, scale, max) | TEX_DETAIL | The bias (−32 to 31), the scale (0-7), the maximum times 255 |
| grTexMultibase, grTexMultibaseAddress | (none) | A texture's levels in more than one place: its first alone ([deferred](#what-is-deferred)) |

The TMU number must be 0 or 1. The registers' defaults at the open: clamped both ways, the detail bias 0, scale 1, maximum 1.0, the rest 0.

### On the device

At each DRAW, a TMU whose texels the combine reads (TMU0's when the colour or alpha combine, or ITRGB lighting, takes the texture; TMU1's when TMU0's combine takes its other too) finds its texture among those decoded from its memory by its address, its description and, for a palette format (P_8, AP_88, and P_8 through the 6666 palette), its TMU's palette's contents; or decodes it into a host image (8:8:8:8, its levels from the largest; a split texture's other levels black): every format but YIQ's (RGB_332, ALPHA_8, INTENSITY_8, ALPHA_INTENSITY_44, P_8, ARGB_8332, RGB_565, ARGB_1555, ARGB_4444, ALPHA_INTENSITY_88, AP_88), each channel widened by repeating its top bits as the Voodoo does, and YIQ_422 and AYIQ_8422 reading their Y index as grey. Texels past the memory's end read 0. A download over a decoded texture's bytes drops it; a palette change drops none, as each palette's textures are found by its contents. A session keeps 2048 textures and 256 MB of their images at most, the device 4096; the least lately used go first, never one the DRAW uses. Their memory comes from blocks of 32 MB taken from the host GPU (16 at most; a host GPU allows 4096 allocations at least, shared with the renderer), each texture a range of one, given back once the slot of the renderer's command buffers it was dropped in has been begun again (the host GPU done with it, its image destroyed). The texels are fetched and filtered in the shader ([The pixel pipeline](#the-pixel-pipeline)).

### The texture manager

Glide 2's utility library keeps its own records of mipmaps (1024 at most), emptied by `guTexMemReset` and at every `grSstWinOpen`:
- **guTexAllocateMemory** places a mipmap on a TMU by a bump allocation, moved to 2 MB when it would straddle it (as a Voodoo3's manager keeps that rule), refused (GR_NULL_MIPMAP_HANDLE) past the TMU's memory or the table, and returns its ID (0, 1, …). No texels are sent.
- **guTexDownloadMipMap** downloads it whole; **guTexDownloadMipMapLevel** one level, its data pointer moved past it.
- **guTexSource** sets its filter, clamp, mipmap mode, LOD bias and source on its TMU; **guTexGetCurrentMipMap** gives a TMU's last.
- **guTexChangeAttributes** changes what it is asked to (−1 keeps a value; the memory stays where it is), set again on the TMU when current; FXFALSE for an ID not allocated.
- **guTexMemQueryAvail** is the TMU's memory less what was allocated; **guTexGetMipMapInfo** the record (GrMipMapInfo, 196 bytes) or NULL.
- **guTexCreateColorMipMap** answers NULL: a Voodoo3's library allocates its mipmap from its C library's heap for the caller to free with its own, and this DLL has no C library, so the answer is a failed allocation's.

### .3df files and run-length data

- **gu3dfGetInfo(file, info)** reads a `.3df` file's header: four text lines, `3df v`version, the format's name (I8, A8, AI44, YIQ, RGB332, RGB565, ARGB8332, ARGB1555, AYIQ8422, ARGB4444, AI88, P8, AP88, in either case), `lod range:` smallest largest (powers of two 1-256) and `aspect ratio:` w h (1, 2, 4 or 8, one of them 1). Gu3dfInfo (1056 bytes) gets the width and height (the largest level's long side, and it over the aspect's number), the levels and the aspect in the program's numbers (Glide 2's, or Glide 3's log2), the format and the texels' bytes, every level counted whole. **gu3dfLoad** reads it again and, into the caller's buffer, the NCC table of a YIQ format (16 Y and 12 I and 12 Q values, big-endian words, I and Q 9-bit signed, and their packed form), a palette format's 256 big-endian entries, and the texels from the largest level, 16-bit ones turned from big-endian. Either fails (FXFALSE) for a file it cannot open or read, or a header it does not take; gu3dfLoad also without the caller's buffer (`data`). The file goes through CreateFileA (read, shared for reading), ReadFile, SetFilePointer and CloseHandle.
- **guEncodeRLE16(dst, src, width, height)** writes a word for each run of one 16-bit colour, its length in the top half and the colour in the bottom, runs going on across rows (65535 at most); with no `dst`, it only counts. It returns the bytes made. A Voodoo3's encoder stops early on pictures of many runs; this one encodes them whole.
- **ConvertAndDownloadRle**: a Voodoo3's Glide 2 on Windows decodes no run-length data there (its line decoder does nothing) and writes the level's rows from a line buffer that stays zero, so here too the rows asked for, and one more when the level is taller, become zero (a 565 level).
- **grLfbWriteRegion** with run-length pixels (GR_LFB_SRC_FMT_RLE16) is refused, said once.

## The frame buffer

The buffers live on the host GPU; a program reaches them through the staging surface ([grSstWinOpen](#grsstwinopen)), in rows laid out as a Voodoo3's frame buffer has them. The transfers are LFB_READ and LFB_WRITE ([gpu-commands.md](gpu-commands.md)); the device reads and writes its images' pixels exactly, converting only between them and the program's format.

**Buffers.** GR_BUFFER_FRONTBUFFER (0), BACKBUFFER (1) and TRIPLEBUFFER (5) are colour buffers; GR_BUFFER_AUXBUFFER (2), and Glide's DEPTHBUFFER (3) and ALPHABUFFER (4), the aux buffer. Its pixels are its 16-bit depth values (GR_LFBWRITEMODE_ZA16); a colour buffer's are 565 for reads, and for writes any of Glide's write modes: 565, 555, 1555, 888, 8888, or 565, 555 or 1555 with depth (a colour buffer's pixel and the aux buffer's together). Writes in 888 and 8888 take grLfbWriteColorFormat's colour format and grLfbWriteColorSwizzle's byte and word swaps, the DLL's flags sent with each write.

**grLfbLock(type, buffer, writeMode, origin, pixelPipeline, info)**: a read lock (GR_LFB_READ_ONLY) and a write lock (GR_LFB_WRITE_ONLY) may be held at once, one of each; a second of a kind, a lock without an open window or its staging surface, an `info` whose `size` is below its 20 bytes, a buffer not among those above or that the window has not (the third without three colour buffers, the aux buffer without one; the region calls refuse those too), a write mode the DLL does not know, or the aux buffer with another format than ZA16 (or ZA16 on a colour buffer), is refused (FXFALSE).
- A read lock gives the buffer's own pixels in the read rows (`stride_read`, 4096 bytes); a write lock, the write mode's (GR_LFBWRITEMODE_ANY: the buffer's own) in the write rows, 8192 bytes for 32-bit pixels and 4096 for 16-bit ones (the fallback's, the window's width × 4 and × 2).
- The origin is the lock's: GR_ORIGIN_UPPER_LEFT, LOWER_LEFT (row 0 the bottom), or GR_ORIGIN_ANY, grSstOrigin's.
- The device writes the whole buffer into the rows in the lock's format, after everything sent before, and the DLL waits for it: what was drawn before is there, a write lock's rows too (a GPU held meanwhile: refused).
- `info` gets the rows' address (in video memory, through the display driver's mapping), the stride, the write mode and the origin.
- A write through the pixel pipeline is taken as a plain write ([deferred](#what-is-deferred)).

**grLfbUnlock(type, buffer)**: FXFALSE without such a lock on that buffer. A write lock's unlock sends a WRITE of the whole window with CHANGED: the device takes back only the pixels the program changed since the lock's read, the others keeping the buffer's exact values (a format narrower than the buffer's would otherwise change them). Nothing waits for it.

**The region calls** count rows from the top whatever grSstOrigin says, as a Voodoo3's Glide locks for them. A rectangle must lie inside the window, checked without sums that could wrap; a region needs an open window, its staging surface and its pixels (`dst`, `src`).
- **grLfbReadRegion(buffer, x, y, w, h, stride, dst)**: the buffer's 16-bit pixels (565, or the aux buffer's depth) of the rectangle into the read rows, waited for, then copied out `stride` bytes apart.
- **grLfbWriteRegion(buffer, x, y, format, w, h, [pixelPipeline,] stride, src)**: the source rows (`stride` apart, a signed stride) into the write rows once the last transfer is done, then a WRITE of the rectangle, with no read first. Glide 2's takes no pixel pipeline.
- **Glide 2's guFbReadRegion and guFbWriteRegion** do nothing: they are obsolete, and a Voodoo3's Glide 2 does nothing for them either.

The staging surface's rows are the program's again once the last transfer is done: a region call or a new lock waits for it. **grLfbConstantAlpha and grLfbConstantDepth** are kept (LFB_CONSTANT) for writes through the pixel pipeline, which are deferred.

## Gamma

The device keeps a table a session, 256 entries a channel (red, green, blue, 0-255), through which the screen shows it ([Showing Glide](#showing-glide)); at OPEN it is the identity, and the DLL sends 1.3 with the session.
- **Glide 2's grGammaCorrectionValue(value)**, 0 < value ≤ 20 (others ignored), and **Glide 3's guGammaCorrectionRGB(red, green, blue)**, each above 0: each channel's curve `255 × (i / 255)^(1/gamma) + 0.5`, cut to 0-255, entry 0 always 0 (through the FPU's log2 and powers of two).
- **Glide 3's grLoadGammaTable(n, red, green, blue)**, n 2 to 256 entries a channel (others ignored): spread over the 256 levels, level i between entries `i × (n − 1) / 255` and the next, rounded.

## Pacing and swaps

- **grBufferSwap(interval)** sends SWAP and its batch. At most two swaps wait to be shown (Glide 3's **grSetNumPendingBuffers** sets 1 to 6): before sending one more, the DLL waits for the one that many back. A GPU held meanwhile (a DOS box) ends no wait: the program sleeps (`Sleep(1)`) between looks, for 5 seconds at most, and a change of the mode meanwhile ends the wait too. The fences of the last 8 swaps are kept.
- **On the device** the back buffer becomes the front (the one after it, with three), then the swap waits for `interval` vertical blanks since the last Glide swap, and at least one from now, as FLIP does (at most 4; 0 waits for none; Glide 2's negative intervals are 0); the front buffer then shows ([Showing Glide](#showing-glide)). A RESET gives the wait up.
- **grBufferNumPending** (Glide 2) and GR_PENDING_BUFFERSWAPS count the kept swaps not yet shown.
- **Drawing into the front buffer** shows without a swap: the DLL sends its batch at least every 16 ms ([Batches and the ring](#batches-and-the-ring)), and the device, marking a front buffer drawn into (a CLEAR or DRAW with the colour mask on, or an LFB_WRITE of colour, into the session's front buffer), shows it at a batch's end or while it is idle, once a vertical blank at most: every vertical blank wakes the GPU thread while one waits.
- **Glide 2's board status:** **grSstIdle** sends the batch and waits for everything sent; **grSstIsBusy** sends it and answers whether the device is at work on it (FXFALSE without the card or a window). **grSstStatus** gives the status register as a Voodoo3's Glide 2 does: bits 4:0 the FIFO's room (all free), bit 6 clear in vertical retrace, bits 9:7 set while busy, bits 27:12 all ones (a Voodoo's memory FIFO, empty), bits 30:28 the swaps pending (at most 7): 0FFFF05Fh idle and out of retrace. **grSstVRetraceOn** is SCANLINE's vertical blank bit ([gpu-registers.md](gpu-registers.md)), FXFALSE without the card; **grSstVideoLine** is always 1, as a Voodoo3's Glide 2 answers; **grSstScreenWidth** and **grSstScreenHeight** the window's size (0 before the first window). Glide 3's **grFlush** sends the batch; **grFinish** sends it and waits.

## Glide 3's contexts and the saved state

- **Contexts.** A process has one window, so one context: `grSstWinOpen` returns 1. `grSelectContext(1)` answers FXTRUE while the window is open and its screen kept; with the session lost (a mode change: the program then opens its window again, as after a Voodoo3 lost it), closed, or another context, FXFALSE. `grSstWinClose(context)` closes the window for context 1 and answers FXTRUE (with no window open too), FXFALSE for a lost one (the desktop given back all the same), as a Voodoo3's Glide answers; another context, FXFALSE.
- **The core's state**, 308 bytes: the 72 registers, the frame buffer writes' colour format and swaps, a dword of flags (bit 0 mipmap dithering allowed, bit 1 delta0), and delta0's colour (three floats). Set again, every register goes to the device before the next command.
- **Glide 2's GrState** (grGlideGetState, grGlideSetState) is 312 bytes: the core's state and the STW hint.
- **Glide 3's** is 448 bytes (GR_GLIDE_STATE_SIZE): the core's state, then the vertex layout (108 bytes), the coordinate space, the viewport and the depth range (floats), and GR_AA_ORDERED. Setting it ends a continuing strip or fan.
- **grReset(what)** (Glide 3): GR_STATS_POINTS (1Eh), GR_STATS_LINES (17h) and GR_STATS_TRIANGLES (21h, both triangle counts) start their counts again, GR_STATS_PIXELS (1Dh) has none to reset, GR_VERTEX_PARAMETER (29h) empties the layout: FXTRUE; anything else FXFALSE.
- **grQueryResolutions(template, output)** (Glide 3): every resolution (0-17h), refresh rate (0-8), colour buffer count (1-3) and aux buffer count (0-1) the template allows (each field a value, or GR_QUERY_ANY FFFFFFFFh; a refresh of GR_REFRESH_NONE FFh is any), whose size Windows has at 16 bpp in the display driver's list (with no list, every size) and whose buffers leave a Voodoo3 its smallest texture memory (`width × height × 2 × buffers` below 16 MB less 4 MB and 64 KB); 16 bytes each, written to `output` unless it is NULL. It returns their bytes; 0 without the card, without a template or with a field outside its range.

## Every export

Every export counts its calls ([The call record](#the-call-record)), records itself for the recorder, looks for the card when it is the process's first Glide call, and at Diagnostics Glide notes its own first call in the log. Without the card, or without a window, an export that needs one sends nothing and reaches for none of the card's registers, answering as below for that case (the host test calls every one so). Where Glide 2 and Glide 3 differ only in their numbers (levels, aspects, fog modes) or a return type, one row serves both.

**Init and the board**

| Export | DLLs | What it does |
|---|---|---|
| grGlideInit, grSstSelect | both | Accepted; nothing to do |
| grGlideShutdown | both | The window closes, as at grSstWinClose; at Diagnostics Glide, the call record |
| grGlideGetVersion, grSstQueryBoards, grSstQueryHardware | Glide 2 | [The identity](#the-identity) |
| grGet, grGetString, grGetProcAddress, grQueryResolutions, grReset | Glide 3 | [The identity](#the-identity), [Glide 3's contexts](#glide-3s-contexts-and-the-saved-state) |
| grSstWinOpen, grSstWinClose, grSelectContext | both (grSelectContext: Glide 3) | [The session and the screen](#the-session-and-the-screen), [Glide 3's contexts](#glide-3s-contexts-and-the-saved-state) |
| grGlideGetState, grGlideSetState | both | [The saved state](#glide-3s-contexts-and-the-saved-state) |
| grGlideGetVertexLayout, grGlideSetVertexLayout, grEnable, grDisable | Glide 3 | [Glide 3's vertices](#glide-3s-vertices), [Glide 3's draws](#glide-3s-draws) |
| grSstIdle, grSstIsBusy, grSstStatus, grSstVRetraceOn, grSstVideoLine, grSstScreenWidth, grSstScreenHeight | Glide 2 | [Pacing and swaps](#pacing-and-swaps) |
| grFlush, grFinish, grSetNumPendingBuffers | Glide 3 | [Pacing and swaps](#pacing-and-swaps) |
| grSstControl | Glide 2 | FXTRUE, nothing done, as a Voodoo3's Glide 2 |
| grSstPerfStats | Glide 2 | Its five pixel counts 0: the device keeps none |
| grSstResetPerfStats | Glide 2 | Nothing to reset |
| grTriStats(processed, drawn) | Glide 2 | The program's triangles asked for, and those sent to the device (a point's or a line's own triangles not among them); grResetTriStats starts both again |
| grErrorSetCallback | both | The callback is kept, never called: the device reports nothing to it |
| grCheckForRoom, grSplash, grSstConfigPipeline, grSstVidMode | both | Nothing: what a Voodoo3's Glide does for them needs nothing of the device |
| grGlideShamelessPlug | Glide 2 | Nothing |
| _grGetSipValues | Glide 2 | Does nothing (0) |
| guMovieSetName, guMovieStart, guMovieStop | Glide 2 | Do nothing: a Voodoo3's Glide 2 records no movies either |

**The screen and the frame buffer**: grBufferClear (the render buffer's clip window: its colour with the colour mask, the aux buffer to the depth's raw 16-bit value with the depth mask and depth buffering on; [gpu-commands.md](gpu-commands.md), CLEAR), grBufferSwap and grBufferNumPending ([Pacing and swaps](#pacing-and-swaps)); grRenderBuffer, grClipWindow, grSstOrigin, grColorMask, grDepthMask ([The state registers](#the-state-registers)); grGammaCorrectionValue, grLoadGammaTable, guGammaCorrectionRGB ([Gamma](#gamma)); grLfbLock, grLfbUnlock, grLfbReadRegion, grLfbWriteRegion, grLfbConstantAlpha, grLfbConstantDepth, grLfbWriteColorFormat (the four colour formats; another is ignored), grLfbWriteColorSwizzle, and Glide 2's guFbReadRegion and guFbWriteRegion ([The frame buffer](#the-frame-buffer)).

**Drawing**: grDrawPoint, grDrawLine, grDrawTriangle, grAADrawTriangle (both); Glide 2's grDrawPolygon, grDrawPolygonVertexList, grDrawPlanarPolygon, grDrawPlanarPolygonVertexList, grAADrawPoint, grAADrawLine, grAADrawPolygon, grAADrawPolygonVertexList, guDrawTriangleWithClip, guAADrawTriangleWithClip, guDrawPolygonVertexListWithClip, grHints, guMPInit, guMPTexSource, guMPTexCombineFunction, guMPDrawTriangle; Glide 3's grVertexLayout, grCoordinateSpace, grViewport, grDepthRange, grDrawVertexArray, grDrawVertexArrayContiguous ([Drawing](#drawing)).

**The pixel pipeline**: grColorCombine, grAlphaCombine, grAlphaControlsITRGBLighting, grConstantColorValue, grAlphaBlendFunction, grAlphaTestFunction, grAlphaTestReferenceValue, grDepthBufferMode, grDepthBufferFunction, grDepthBiasLevel, grFogMode, grFogColorValue, grFogTable, grChromakeyMode, grChromakeyValue, grCullMode, grDitherMode, grDisableAllEffects (both); Glide 2's grConstantColorValue4, guColorCombineFunction, guAlphaSource ([The state registers](#the-state-registers)).

**Textures**: grTexMinAddress, grTexMaxAddress, grTexCalcMemRequired, grTexTextureMemRequired, grTexSource, grTexDownloadMipMap, grTexDownloadMipMapLevel, grTexDownloadMipMapLevelPartial, grTexDownloadTable, grTexDownloadTablePartial, grTexNCCTable, grTexClampMode, grTexCombine, grTexDetailControl, grTexFilterMode, grTexLodBiasValue, grTexMipMapMode, grTexMultibase, grTexMultibaseAddress, gu3dfGetInfo, gu3dfLoad (both); Glide 2's grTexCombineFunction, guTexCombineFunction, the texture manager (guTexAllocateMemory, guTexDownloadMipMap, guTexDownloadMipMapLevel, guTexSource, guTexMemReset, guTexGetCurrentMipMap, guTexChangeAttributes, guTexMemQueryAvail, guTexGetMipMapInfo, guTexCreateColorMipMap) and ConvertAndDownloadRle ([Textures](#textures)).

**The utility library's arithmetic**, which needs no card and is exactly defined, card or not:

| Export | DLLs | Answer |
|---|---|---|
| guFogTableIndexToW(i) | both (`glide/gu.c`) | The w fog table entry i stands for: 2 to the power 3 + (i >> 2), over 8 − (i & 3), in single precision as Glide computes it. The power of two is exact; past the floats' range it is infinity, below it 0 |
| guFogGenerateExp(table, density), guFogGenerateExp2, guFogGenerateLinear(table, near, far) | both | The 64 entries as a Voodoo3's library builds them, for entry i at its w, wᵢ: `(1 − e^−(d·wᵢ)) / (1 − e^−(d·w₆₃))`; the same with (d·w) squared; `(min(wᵢ, 65535) − near) / (far − near)`; each clamped to 0-1, times 255, cut (nothing for a NULL table) |
| guEncodeRLE16 | both | [Run-length data](#3df-files-and-run-length-data) |
| guEndianSwapWords(value) | Glide 2 (`glide/glide2x/util.c`) | The 32-bit value with its two 16-bit halves exchanged |
| guEndianSwapBytes(value) | Glide 2 | The 16-bit value with its two bytes exchanged |

Only Glide 2's `_grGetSipValues`, `guMovieSetName`, `guMovieStart` and `guMovieStop` do nothing yet in the call record's sense (`(does nothing yet)`): each touches none of its arguments and returns 0 in its own type.

## What is deferred

What a Voodoo3 does and the device does not. Each is said once a process, the first time a program asks for it, as an event line `not done yet:` and what is done instead:

| What | Asked for by | Done instead (the line's words) |
|---|---|---|
| Edge antialiasing | grAADraw*, guAADrawTriangleWithClip, Glide 3's grAADrawTriangle, and its draws under grEnable(GR_AA_ORDERED) | `drawn without it` |
| A depth compare against the bias | GR_ZBUFFER_COMPARE_TO_BIAS, GR_WBUFFER_COMPARE_TO_BIAS (grDepthBufferMode 3, 4) | `compared as usual` (the pixel's own depth, without the bias) |
| YIQ (NCC) textures | grTexSource of YIQ_422 or AYIQ_8422, an NCC table | `its Y shown as grey` |
| A texture in more than one place | grTexMultibase(on), grTexMultibaseAddress | `its first place alone` |
| Frame buffer writes through the pixel pipeline | a write lock or Glide 3's grLfbWriteRegion with the pixel pipeline | `written plain` (the device says so too, once) |
| The aux buffer as an alpha buffer | grColorMask with alpha, an aux buffer and depth buffering off | `not kept`: no destination alpha; destination alpha reads 1 |
| The utility library's multipass helpers | guMPDrawTriangle | `one pass, the first texture` |

And without a line: the 3dfx extensions (`grGetProcAddress` still gives none, and GR_EXTENSION lists none); run-length pixels in grLfbWriteRegion (refused, said once); dithering of blended pixels, and 4x4 dithering's subtraction from the destination before blending (blended pixels are rounded by the host's blend); windowed Glide (the window always takes the screen); Glide 3's unified texture memory (the device drops a TEXDATA to memory 2, and the DLLs send none); and, for speed, vertices from main memory instead of the ring, and textures sampled through their palette instead of decoded again for it.

## The log

Lines go to the host's log through BAR0's DEBUG register ([registers.md](registers.md)), formatted by `driver32/logline.c` as the DirectX DLL's are:

```
VGPU98 glide2x <stamp> <PROGRAM>: <text>
```

`glide3x` for the other DLL; the build stamp as every driver's; the program's file name as found above. The DLL makes `VGPU98 glide2x <stamp> <PROGRAM>: ` once, at step 2, and `log_line` copies it as it is before the line's format (so a `%` in a program's name is only a character); a line is at most 159 characters, then a newline. Every line fits: the prefix names at most 20 characters of the program, a name a program hands `grGetProcAddress` is cut to 64, and each line puts its verdict first, so only the end of a long path, or of a line with numbers outside Glide's range, can be cut. Nothing is written without the card's register window (steps 1-5 above), after the line about another build's display driver (step 7), or at Diagnostics Off. The device's own lines start `VGPU98 Glide:` ([The device's diagnostics](#the-devices-diagnostics)).

**The levels.** DEBUG is read once, at step 6 ([registers.md](registers.md), Diagnostics). The **event lines** come with any of bit 0 (LISTENING, the DirectX levels), bit 5 (EVENTS, Minimal and Glide) and bit 7 (GLIDE): the table's "Every level but Off". Every other line comes with bit 7, at Diagnostics **Glide** alone. So at Minimal and at every DirectX level the DLLs write their event lines alone, as they do with an 86Box without the Glide level, which never sets bit 7.

| Line | When | Levels |
|---|---|---|
| `loaded; a Voodoo3 (16 MB, two TMUs) for Glide 2.61` (`Glide 3.10`) | Once a process, at the first Glide call, with the card | Every level but Off |
| `no Glide board: another build's display driver (block` v`/`n `bytes, answer` v`; this DLL's` v`/`n `bytes)` (the block's version and size, the escape's `sb_version`, then this build's) | Once a process, in its place, with another build's display driver; the process's last line | Every level but Off |
| `file (the system folder):` path, or `file (not the system folder,` folder`):` path (`not known` for a folder Windows does not give), or `file not known` | Once a process, after `loaded` ([The DLL check](#the-dll-check)) | Glide |
| `program` path, or `program not known` | After it | Glide |
| `build` stamp`, the display driver's` stamp | After it, the two stamps the same | Glide |
| `possible stale Glide DLL: built` stamp`, the display driver` stamp | In the build line's place, with another stamp | Every level but Off |
| `possible stale Glide DLL: built` stamp`; the display driver gives no stamp (an older build)` | In the build line's place, with a 16-byte answer | Every level but Off |
| `environment:` variables, then `environment, more:` variables | After the build line, with Glide variables set; six lines at most | Glide |
| `environment:` n `more lines not shown` | After them, with more than six | Glide |
| `environment: no Glide variables` | In their place, with none set | Glide |
| `environment: not readable` | In their place, when GetEnvironmentStringsA gives no block | Glide |
| `first call:` export, then ` (does nothing yet)` for one of the four exports that do nothing | Each export's first call in the process | Glide |
| `the card draws no Glide: no 3D on the host GPU, or an 86Box from before Glide drew` | The first grSstWinOpen without PROFILE.GLIDE, once a process | Every level but Off |
| `grSstWinOpen refused:` resolution`,` rate`, window` h`, colour format` n`, origin` n`, buffers` colour`+`aux | A window refused for PROFILE.GLIDE or its arguments: the first three in a process, its first 64 at Glide, counted by each DLL | Every level but Off |
| `grSstWinOpen refused again: no more of its lines from this process` | The next one; later ones write nothing (a program may retry every frame) | Every level but Off |
| `grSstWinOpen: DirectDraw is not there; the screen keeps its mode` | DirectDrawCreate failed | Every level but Off |
| `grSstWinOpen: no exclusive mode for window` h `(`hresult`); the mode is set for the desktop` | SetCooperativeLevel refused, or no window | Every level but Off |
| `grSstWinOpen: the desktop is` WxH `at 16 bpp already: 8 bpp set, as a Voodoo3 does` | The desktop already in the window's mode | Glide |
| `grSstWinOpen: Windows refused` WxH `at` n `bpp (`hresult`); the screen shows Glide scaled to its mode` | SetDisplayMode refused | Every level but Off |
| `grSstWinOpen: video memory is short: the frame buffer's rows are` n `bytes, not a Voodoo3's 4096` | The staging surface at the window's width | Every level but Off |
| `grSstWinOpen: no` n `KB of video memory for the frame buffer's transfers; grLfbLock will fail` | No staging surface (the figure the smaller one's) | Every level but Off |
| `grSstWinOpen: the GPU restarted as the window opened; its session goes again` | The session sent again | Glide |
| `grSstWinOpen refused: the GPU is held (a mode change or a DOS box)` | The open's batch dropped | Every level but Off |
| `grSstWinOpen refused: the screen's mode changed as the window opened` | The mode changed after the window's own was set and before its OPEN went (the batch kept back, after the lost-mode lines): the screen given back | Every level but Off |
| `window open:` WxH`,` n `colour buffers` (` and a depth buffer`)`,` `upper-left origin` or `lower-left origin``; mode` `set` or `not set``,` (`not `)`exclusive` | A window opened | Glide |
| `the window:` handle`, class "`class`"`, then `  at` x`,`y WxH`,` `foreground` or `not foreground``,` `topmost` or `not topmost``, "`title`"` | After `window open:`: the window taken (the class and title 40 characters at most, a control character `?` and a quote `'`; the title read only for a window of another process or, here alone, the Glide window of the calling thread (its window procedure answers inside grSstWinOpen), else `""`, so the line never waits on another thread; nothing asked of any window at the levels where the lines are not written) | Glide |
| `x and y wrapped as a Voodoo wraps them: the first x` n `y` n `as` n n | The window's first vertex wrapped (its integer parts before and after), once a window ([Drawing](#drawing)) | Glide |
| `possible bad vertices:` n `of` n `primitives dropped in this window (not a number, or past 2^27)` | 100 points, lines and triangles dropped for their vertices in a window, and one in ten of those it asked for at least; once a window | Every level but Off |
| `window closed after` n `swaps: triangles` n `asked,` n `sent,` n `culled`, then `  points` n`, lines` n`;` n `primitives dropped` and `  ` n `triangles wrapped apart,` n `vertices wrapped`; then `  TMU1's 1/w while active:` n `its own,` n `TMU0's,` n `the vertex's` and `  TMU1's s and t while active:` n `its own,` n `TMU0's` (vertices: where an active TMU1's coordinates came from, which shows a program's hints) (five lines, each short of the writer's cut with a 20-character program name) | A window's close (grSstWinClose, a new open, a FreeLibrary, the process's end with it open): its counts since its open (culled: a zero area or the cull mode; wrapped apart: triangles, a line's among them, whose corners wrapped by different multiples; vertices wrapped: the corners sent, and points) | Glide |
| `a batch of` n `dwords dropped: the GPU is held (the next ones while it is are not said)` | The first batch dropped after 2 seconds of a held GPU, once a hold | Every level but Off |
| `the screen's mode changed under the Glide window: nothing more is drawn`, then `  the foreground window then:` and the window in front as `the window:` describes it (two lines; the title of another process's window alone, so no window procedure of the program runs inside the Glide call), or `the Glide window` | The session lost, once a session (inside DllMain the first line alone) | Every level but Off |
| `not done yet:` what`:` what is done | A deferred feature's first use in the process ([What is deferred](#what-is-deferred)) | Every level but Off |
| `grLfbWriteRegion: run-length pixels are not taken yet` | The first such region, once a process | Every level but Off |
| `grGet` pname `: not a query a Voodoo3 answers` (pname in hex) | Each of a process's first 16 queries grGet does not know (with the card; a wrong length is not noted) | Glide |
| `grGetProcAddress` name`: none` (at most 64 characters of the name; `(null)` for no name) | Each of the process's first 16 calls | Glide |
| `a fault in the Glide DLL in` export`: code` c `at +`offset`, then `  its addresses on the stack:` six `+`offsets | A fault in the DLL ([The recorder and the fault catcher](#the-recorder-and-the-fault-catcher)) | Every level but Off |
| `the program faulted (code` c `at` address`), not in the Glide DLL` | A fault the program does not handle, outside the DLL | Glide |
| `the last` n `Glide calls (`why`), oldest first:`, then a line an entry: `  `export`(`four dwords, spaces between`)` (` x`n for n calls in a row) `at` ms `ms, thread` id (n counts entries: repeats folded) | After a fault in the DLL (`before the fault`); after the program's (`before the program's fault`); at an unload without grGlideShutdown (`unloaded without grGlideShutdown`) | Every level but Off after a fault in the DLL; Glide otherwise |
| `call record:` n `exports,` n `calls;` n `of the exports do nothing yet` | At grGlideShutdown and at the DLL's unload, with calls since the last record ([The call record](#the-call-record)) | Glide |
| `done:` export count`,` …, then `done, more:` … | After it: the exports that do something | Glide |
| `nothing yet:` export count`,` …, then `nothing yet, more:` … | After those: the exports that do nothing yet | Glide |
| `ring:` … | The ring's lines, as the DirectX DLL's ([directx-hal.md](directx-hal.md)): its placement (`ring 2048 KB in main memory, status in main memory`, or `ring 128 KB in video memory (`why`)`) at Glide; its events (a wait over 2 seconds in an export, with the GPU's state; a takeover of the ring lock; a process that ended holding it; a GPU error, a reset for no progress, the GPU's refusal of the ring in main memory, a GPU running another ring) at every level but Off | As said |

At the first call, with the card, the lines come as the table has them: `loaded`, the `file`, `program` and `build` lines (or a stale line in the build line's place), the environment's, then that export's `first call:` line; at Minimal and the DirectX levels, `loaded` and any stale line alone. In the grSstWinOpen line the resolution is GR_RESOLUTION_320x200 (0) to GR_RESOLUTION_2048x2048 (17h) by name (`640x480`), FFh `window size` (the window's own), anything else `?`; the refresh rate GR_REFRESH_60Hz (0) to GR_REFRESH_120Hz (8) by name (`60 Hz`; 7 is 85 Hz, after 100 Hz), FFh `no refresh`, anything else `?`; the window handle is in hex; the buffers are the colour buffers and the aux buffers: `grSstWinOpen refused: 640x480, 60 Hz, window 0, colour format 0, origin 0, buffers 2+1`.

**One line at a time.** DEBUG collects one line for every 32-bit writer: `vgpu98dx.dll` in every process and both Glide DLLs in every process that loads them. So each line is formatted first, then written whole under the shared block's `log_lock` (0 free, 1 held, taken with an atomic exchange). After a short spin (100,000 tries) the writer takes the lock over, writes its line and releases the lock, so a holder that died never leaves it held for good: that holder's line is lost, and one still writing may interleave with the new line. `vgpu98dx.dll`'s lines take the lock the same way ([directx-hal.md](directx-hal.md)). The one line about another build's block goes under a lock word of the DLL's own, the block not being trusted.

### The DLL check

At the process's first Glide call, with the card and a shared block of this build, after the `loaded` line. The file, the program and both build stamps are read at every level, their lines written at Glide; a stale stamp's line is an event. The environment is read at Glide alone.

- **The file.** The DLL's own path, from GetModuleFileNameA of the instance DllMain kept, and the system folder, from GetSystemDirectoryA (not known when Windows gives none, or one that fills its 260 bytes). The path's folder, all before its last `\`, is compared with the system folder, ASCII letters in either case: `file (the system folder): C:\WINDOWS\SYSTEM\GLIDE2X.DLL`; otherwise `file (not the system folder,` the system folder`):` the path, `not known` in the folder's place when Windows gave none. A path without a `\` is not in the system folder. Without a path, `file not known`.
- **The program.** `program` and the program's full path from step 2 (at most 259 characters), or `program not known`.
- **The build.** The DLL's build stamp against the display driver's, the answer's `stamp` (its 16 bytes, then a NUL), compared byte for byte. The same: `build 1a2b3c4, the display driver's 1a2b3c4`. Another: `possible stale Glide DLL: built` the DLL's stamp`, the display driver` the display driver's. A 16-byte answer, from a display driver of before the stamp: `possible stale Glide DLL: built` the DLL's stamp`; the display driver gives no stamp (an older build)`. The board is reported either way: the stamps tell builds apart, not interfaces.
- **The environment.** The variables of GetEnvironmentStringsA's block, given back with FreeEnvironmentStringsA, whose names start `FX_GLIDE_`, `SST_`, `SSTV2_` or `SSTH3_` (letters in either case): in the block's order, each as it stands (`NAME=value`), separated by spaces. A line takes as many as it has room for, 159 characters less the prefix and 24 (40 at least); a variable is cut to 100 characters and to that room, so one alone always fits. The first line starts `environment:`, the others `environment, more:`; six are written at most, then `environment:` n `more lines not shown`. With none, `environment: no Glide variables`; without a block, `environment: not readable`.

### The call record

Every export counts its calls in the process, at every level: a plain increment, so two threads calling at once may lose one. An export's first call puts it on the process's list of the exports called, each DLL's own. At Glide the DLL writes that list as its call record:

- **When.** At grGlideShutdown, that call counted, and at the DLL's unload (DllMain's process detach, which ExitProcess brings too): each time only with an export called and calls since the last record. The counts are totals since the DLL loaded. A program ended by TerminateProcess or a fault gets no detach, and writes none at its end. One record is written at a time: one asked for while another is being written (a second thread in grGlideShutdown) is skipped, its calls left for the next.
- **The header.** `call record:` n `exports,` n `calls;` n `of the exports do nothing yet`: the exports called, their calls in all, and how many of those exports do nothing yet.
- **Two groups.** `done`, the exports that do something, then `nothing yet`, those that do nothing yet: each export's name and count (`grSstWinOpen 5`), separated by `, `, busiest first, those with as many calls by name in byte order. A group takes as many lines as it needs, each holding as many as it has room for (as the environment's lines do); the first starts `done:` (`nothing yet:`), the others `done, more:` (`nothing yet, more:`). A group without an export has no line.

The host test's Glide 2 session ([The host test](#the-host-test)), under a program named `GAME.EXE` on a card without PROFILE.GLIDE (so every window is refused), ends with:

```
VGPU98 glide2x 1a2b3c4 GAME.EXE: call record: 8 exports, 15 calls; 1 of the exports do nothing yet
VGPU98 glide2x 1a2b3c4 GAME.EXE: done: grSstWinOpen 5, grBufferSwap 2, grGlideGetVersion 1, grGlideInit 1, grGlideShutdown 1
VGPU98 glide2x 1a2b3c4 GAME.EXE: done, more: grSstQueryBoards 1, grSstQueryHardware 1
VGPU98 glide2x 1a2b3c4 GAME.EXE: nothing yet: guMovieStart 3
```

### The recorder and the fault catcher

- **The recorder** keeps the process's last 32 Glide calls, at every level: each export's name, its first four argument dwords (a stdcall export's, from above its frame's return address: an argument of fewer dwords shows what lies past it), GetTickCount and the thread's ID. A call of the export the last entry holds, on the same thread, folds into that entry (a poll such as grBufferNumPending, which Unreal makes about 8,500 times a frame): its count, and the last call's arguments and time. It is written, oldest first, after a fault (below), and at the DLL's unload when the program never called grGlideShutdown (at Glide).
- **The fault catcher** is put in place at the process's first Glide call, with the card's log there (any level but Off), once a process: SetUnhandledExceptionFilter, the program's filter from before kept and called after it (or, with none, the search goes on: Windows' own); never a filter in the DLL's own image or in memory that cannot be read. At a FreeLibrary the program's filter goes back, if the DLL's is still the one in place. For a fault the program does not handle, one at a time: when the faulting instruction is in the DLL's image (its size from its PE header), an event line names the export the program was in (the last entered; `a Glide call` outside one), the exception code and the instruction's offset in the DLL, and a second the first six of the DLL's addresses among the 256 stack dwords (return addresses, or data in its image) from the fault's stack pointer (IsBadReadPtr ends the walk; 0 for those not found), then the recorder's lines as events; otherwise, at Glide, a line that the program faulted, not in the Glide DLL, and the recorder.

## The device's diagnostics

86Box's Glide device writes its lines to 86Box's log after the date and time, starting `VGPU98 Glide:`. Its events come at every level but Off ([registers.md](registers.md), Diagnostics); the report, two of the findings and the captures at **Glide** alone, which also has the device keep the frame's commands and count bad vertices.

**Its events:**

| Line | When |
|---|---|
| `session` id `opened:` WxH`,` n `colour buffers` (`, a depth buffer`) | An OPEN taken |
| `session` id `closed after` n `swaps` | A CLOSE |
| `session` id `ends: the GPU was reset for a mode change or a new machine` | Each session at such a RESET (two RESETs the GPU thread takes as one keep the stronger: a mode change's hold before the driver's start still ends them) |
| `session` id `hidden for a DOS session; shown again at its next swap` | The session shown at a full-screen DOS session's RESET |
| `4 sessions open: session` id `ends for session` id | A fifth session opened |
| `session` id`: no host images for` WxH `with` n `colour buffers` (` and a depth buffer`) | The host GPU could not make its images; the session ends |
| `command` nnh `dropped:` why | A command dropped: the first 16 after a window opens, the 16th ending ` (more are counted, not named)`; the rest counted in the report ([gpu-commands.md](gpu-commands.md), Command set 2.4) |
| `a frame buffer write through the pixel pipeline is taken as a plain write (not done yet)` | The first such write while 86Box runs |
| `the screen is not a` WxH `16-bit mode on the host GPU: the session's frames are not shown` | The first frame with nowhere to go ([Showing Glide](#showing-glide)) while 86Box runs |
| `256 pipelines made: the oldest are made again when needed` | The pipelines' table full, once while 86Box runs |
| `the host GPU lacks 16-bit colour or depth images; no Glide`, `setting up the host GPU failed; no Glide` | At start-up: no Glide device, so no PROFILE.GLIDE |
| `a texture could not be made on the host GPU (no memory, or 2048 textures): it draws white` | The first such texture while 86Box runs |
| `texture memory in too many pieces: some is not reused until 86Box closes` | A block's 1024 free ranges full, once while 86Box runs (a line of 86Box's own, at every level) |
| `possible` … | The findings (below) |
| `capture` n `(`why`):` n `files in` folder | A capture written (its files on a thread of their own) |
| `no capture: the captures' folder's path is too long (`folder`)` | A capture's path past 260 characters, once while 86Box runs; it does not count toward the 8 |

The GPU's start-up line names the command set: `VGPU98 GPU: profile 7FF (3D on the host's GPU; command set 2.2, DirectX 8 with shaders; Glide (command set 2.4))`.

**The report**, every 5 seconds at Glide while Glide works (a period with swaps, draws, downloads, frame buffer transfers or commands dropped), its counts starting again each time:

```
VGPU98 Glide: 5.0 s: 300 swaps (worst frame 21 ms), 4100 draws of 61000 triangles, 5200 state changes, 300 clears; frame buffer 0 reads, 2 writes; 0 commands dropped
VGPU98 Glide:   of the draws 92% textured, 31% blended, 88% depth Z, 0% depth W, 40% fogged
VGPU98 Glide:   61% of the draws: colour SCALE_OTHER by LOCAL of ITERATED, TEXTURE; alpha SCALE_OTHER by ONE of CONSTANT, CONSTANT; TMU0 LOCAL by ZERO; TMU1 LOCAL by ZERO
VGPU98 Glide:   textures: 1840 KB downloaded, 2 palettes; 96 decoded (P8 16, 565 60, 1555 20) (40 of them a dropped one's bytes again), 4004 found again, 12 dropped; 84 kept (21760 KB); TMU memory written to 6912 KB and 0 KB
VGPU98 Glide:   blends: 55% none; 30% SRC_ALPHA, ONE_MINUS_SRC_ALPHA; 15% DST_COLOR, ZERO
VGPU98 Glide:   texture units: 2000 DRAWs TMU0's texel alone, 900 TMU1's alone (TMU0 not active), 1100 both; 0 of TMU1's alone with its own 1/w
VGPU98 Glide:   texture coordinates: TMU0 12 one texel, 300 at the smallest level, 0 1/w its own, 0 1/w bad; TMU1 4 one texel, 20 at the smallest level, 0 1/w its own, 0 1/w bad
VGPU98 Glide:   depth: 88% of the draws tested (LESS 80%, LEQUAL 20%), 70% writing, 120 with a depth bias, 0 with a 1/w past the W buffer's far end
VGPU98 Glide:   e.g. TMU1 one texel: TMU 1 memory's 565 32x32 at 1a2000 (average c08040); texel s 12.00 t 3.50, its 1/w 0.0123, the vertex's 0.0123 (6 vertices); colour SCALE_OTHER by LOCAL of CONSTANT, TEXTURE; alpha SCALE_OTHER by ONE of CONSTANT, TEXTURE; TMU0 SCALE_OTHER by LOCAL; TMU1 LOCAL by ZERO; blend none
VGPU98 Glide:   busiest textures in TMU 0's memory: P8 256x256 at 0 (average 806040), 1200 draws; P8 128x128 at 15558 (average 404850), 800 draws; 565 64x64 at 1f0000 (average 302820), 300 draws
VGPU98 Glide:   textures checked against TMU memory: 31, 0 changed
```

- The first line: the period's seconds, its swaps and the longest time between two of them in the period, the DRAWs and their triangles, the STATE packets, the CLEARs, the frame buffer READs and WRITEs, the commands dropped.
- With draws, the share textured (a TMU with a texture), blended, Z- and W-buffered and fogged; then the busiest combine set-ups, three at most, each the colour and alpha combine (function `by` factor `of` local`,` other, `, inverted` when inverted) and TMU0's colour combine (function `by` factor), in Glide's names (7 is BLEND, 9 BLEND_LOCAL).
- With texture work, the kilobytes downloaded and the palettes, the textures decoded (by format, in the formats' order; at Glide, how many had the bytes of one a download dropped, by its place, description, palette and TMU memory hash: decodes a content-keyed cache would have kept. At least that many: the last 512 such drops are remembered, about half a second at Unreal's rate; the same bytes at another place do not count; room's evictions are not remembered, as any cache of this size has them; a window's open starts the memory again), those found among the decoded, those dropped; those kept and their host kilobytes; the highest byte any session's downloads wrote in each TMU's memory.
- What the draws sampled (read from the DRAWs the device already holds, nothing drawn differently). The combine lines also give TMU1's combine, and the set-ups are told apart by it. Then:
  - `blends:` the three busiest blend set-ups by share of the draws, Glide's names for the source and destination factors (the alpha's after, in brackets, when they differ), or `none`.
  - `texture units:` the textured DRAWs by the TMUs whose texel reaches the output (a texture read and the TMU active, by the DLLs' rule, **Active TMUs** above): TMU0's alone, TMU1's alone (TMU0 not active), both, neither active; and of TMU1's alone, how many gave TMU1 its own 1/w (only a program's hint, or Glide 3's Q1, should). In Glide 3's clip coordinates with FOG_EXT and Q, the TMUs' 1/w is 1/w where the vertex's is Q/w, rightly: those DRAWs count as `1/w its own` too.
  - `texture coordinates:`, for each texture unit a DRAW reads, counts of DRAWs:
    - `one texel`: every vertex samples the same texel of the texture's largest level;
    - `at the smallest level`: its size on the screen takes its smallest of several levels, from its triangles' texel and screen areas plus the LOD bias;
    - `1/w its own`: the unit's 1/w is not the vertex's;
    - `1/w bad`: at or below 0, or not a number.

    These are counts, not warnings: programs do each on purpose too.
  - `depth:` the share of the draws depth-tested, its two busiest functions (by share of the tested), the share writing depth, the DRAWs with a depth bias, and those W-buffered with a vertex's 1/w below 2^-16 (the W buffer's far end, where the Voodoo's 16-bit W is FFFFh).
  - `e.g.` lines, at most two of a kind and six a report: `one texel`, `1/w bad`, `its own 1/w, TMU0 not active`, `stale texture`. Each names the unit and its texture (TMU memory, format, size, place, the largest level's average colour as RRGGBB). For a DRAW's oddity it adds the first vertex's texel s and t, the unit's 1/w and the vertex's, and the DRAW's vertices. Then the DRAW's colour and alpha combines, both TMUs' combines and its blend.
  - `busiest textures in TMU` m`'s memory:` that memory's three most drawn textures of the period, with their DRAWs.
  - `textures checked against TMU memory:` how many textures found again were checked, and how many no longer matched. One hit in 257 is checked, 128 a report at most: its TMU memory's bytes hashed (FNV-1a 64) against the hash taken when it was decoded.

  Each texture decoded at Glide keeps the hash and the average colour; a texture decoded below Glide has neither, and is not checked.

**The findings**, past real problems' thresholds, each said once (said again after a window opens), worded `possible`, and each asking for a capture of its session at its next swap:

| Line | When |
|---|---|
| `possible missing texture: TMU` t `samples` lo`-`hi `of its memory, where no download wrote` | A texture decoded where no download wrote any of its 4 KB pages (at every level) |
| `possible texture overlap: a download to TMU` t `at` lo`-`hi `wrote over part of the texture TMU` t `samples at` lo`-`hi | A download that writes over part of a texture a TMU samples, reaching past the texture's bytes and smaller than them (at every level) |
| `possible stale texture: one found again in TMU` m`'s memory at` place `(`format WxH`) no longer matches it: the memory changed without a download dropping it` | A texture found again whose TMU memory's bytes changed since it was decoded: a download over it should have dropped it, so each is a cache defect (at Glide, by the sampled check above) |
| `possible blank screen:` n `swaps in` s `s with nothing drawn` | A report's period with 10 swaps or more and no draw, clear or frame buffer write (at Glide) |
| `possible blank screen:` n `swaps in` s `s with nothing drawn but clears` | Three report periods in a row (15 s), each with 10 swaps or more and clears but no draw or frame buffer write: a program whose triangles never reach the device still clears every frame, and a black loading screen's few seconds are not a finding (the counts the three periods'; a window's open starts the run again) (at Glide) |
| `possible unclipped geometry:` n `vertices in` s `s with 1/w at or below 0, or not a number (a Voodoo draws them wrong too)` | 100 or more such vertices (or 1/w past 10^30) in a report's period, counted in the draws that read 1/w (a TMU's texture read, W-buffering, or table fog); they are drawn (at Glide) |

**A capture** (at Glide): at the swap after a finding, and once 10 seconds into each session; 8 a session at most. An 86Box screenshot (Action > Take screenshot) at Glide asks for one too, of the frame shown at the next swap (`a screenshot (Action > Take screenshot)`), ten a session apart from those 8. The captures' folder is set at Glide as at the texture audit (before, at the texture audit alone, so captures at Glide wrote nothing). The GPU thread reads its pixels and commands; a thread of its own writes the files (while one writes, the next capture waits for a later swap). Its folder is in the captures' folder, the machine's `vgpu98-textures` ([registers.md](registers.md), Texture dumps): `<date> <time> glide capture N` (`2026-10-25 21-14-03 glide capture 2`, N counting while 86Box runs). It holds:
- `front.png`: the front buffer just shown, 565 widened to 8 bits;
- `tmu0.png`, `tmu1.png`: each TMU's texture, its largest level (of its parity) decoded from its memory;
- `commands.txt`: `VGPU98 Glide capture` N `(`why`): session` id`,` WxH`, swap` n, the 72 registers (`nnn: XXXXXXXX`, eight a line), then `The frame's commands (`n`):` a line each since the swap before (4096 at most kept): its name (OPEN … GAMMA), its length and its first six payload dwords.

## The export lists

`glide/glide2x/glide2x.exports` (133 names) and `glide/glide3x/glide3x.exports` (96 names) are the Voodoo3 1.07.00 DLLs' export tables exactly: decorated stdcall names, each with its arguments' bytes after the `@` (`_grDrawTriangle@12`), the names Glide programs import. One name per line, `;` comments (vglink and drvlint take `#` too), plain ASCII, sorted as the export table is (byte order, so `_ConvertAndDownloadRle@64` and `__grGetSipValues@12` come before the `_gr` names). `glide2x.dll`'s list has none of the 2.43 SDK's `pci*` functions, which the Voodoo3's dropped.

- **The link.** The DLL's `[exports]` has `names = decorated` and `file =` its list ([vglink.md](vglink.md)): each name is exported as written, and is the symbol it exports. The symbol's decoration comes from the function's C prototype (`glide2x.h`, `glide3x.h`), so a function whose argument bytes differ from the list's has another symbol, and the link fails, citing the list's line (`export` name`: symbol` name `is not defined`). Nothing outside the list is exported.
- **drvlint at the link.** `drvlint --strict --glide --exports` with the list ([pe-format.md](pe-format.md)): the export table must hold exactly the list's names. The DLL is linked to `<name>.unchecked.dll` and copied to its name only once drvlint passes it.
- **The retail DLLs.** With the reference library present, the tests `drvlint_windows_glide2x` and `drvlint_windows_glide3x` run `drvlint --glide --exports` on the Voodoo3's own DLLs: they pass the Glide checks, and their tables are exactly the lists.

## The host test

`tools/tests/test_glide.c` runs the DLLs on the host against a stand-in machine. It is built twice, as `test_glide2x` and `test_glide3x` (with TEST_GLIDE3), each from `glide/*.c`, its DLL's own folder's sources, `driver32/logline.c` and `driver32/gpu.c`, compiled with `VGPU98_GLIDE_DLL`, with stand-ins for what Windows 98 and the card give the DLLs. The stand-ins take names of their own, `test_` and the function's name, by compile definitions (so the host C library's own imports keep theirs), and `DLLIMPORT` is empty:

- **GDI32.** CreateDCA (the display's alone), DeleteDC (no DC may be left open), and ExtEscape: QUERYESCSUPPORT for VGPU98_ESC_INFO, and VGPU98_ESC_INFO as the display driver answers it, its buffers checked (8 and 32 bytes, `out_size` 32), with the whole answer and the scenario's stamp or with its head alone.
- **KERNEL32.** GetModuleFileNameA (the scenario's program, `C:\GAMES\TEST\TEST.EXE` unless it names another; the DLL's instance, `C:\WINDOWS\SYSTEM\GLIDE2X.DLL` or `GLIDE3X.DLL` unless it names another), GetSystemDirectoryA (`C:\WINDOWS\system`, so the folders' comparison must ignore case), GetEnvironmentStringsA (`PATH`, `FX_GLIDE_NO_SPLASH=1`, `sst_gamma=1.3`, `SSTX=1`, unless the scenario gives another block), FreeEnvironmentStringsA (the same block must come back), Sleep (counted; with a stalled or held GPU and the scenario's count, the stall and the hold end at that many sleeps), GetTickCount (a millisecond more at each call), GetCurrentProcessId (FFF12345h), GetCurrentThreadId (77h, or the scenario's other thread, 78h), the files (CreateFileA for reading alone, ReadFile, SetFilePointer, CloseHandle, over the host's files, eight handles), SetUnhandledExceptionFilter (the filter kept, for the scenario to call) and IsBadReadPtr (NULL alone is bad).
- **USER32.** GetActiveWindow: 5150h; GetForegroundWindow: 5150h unless the scenario puts another window in front (6000h, the taskbar: another process's; 7000h: another thread's of the program; 8000h: another window of the program's own thread, its frame). While the scenario calls DllMain no USER32 call may come, and the window queries are counted (none at Off). The window's description: GetClassNameA (`TestWindow`, `Shell_TrayWnd`, for 7000h a class with a quote and a control character, `Frame` for 8000h), GetWindowTextA (`Test Game` for 5150h; never called for 7000h, whose thread would be sent a message, nor for 8000h, whose window procedure would run inside a Glide call), GetWindowRect (640x480 at 0,0; the taskbar 800x28 at 0,572), GetWindowLongA (GWL_EXSTYLE alone: 5150h and the taskbar topmost) and GetWindowThreadProcessId (the program's process and thread 77h; 7000h another thread; the taskbar another process).
- **DirectDraw.** DirectDrawCreate gives an object whose methods sit at their places in the interface: QueryInterface (IDirectDraw2: itself), Release, CreateSurface (the staging surface, its description checked, at 1 MB into video memory, its pitch its width's bytes, inside video memory below the GPU's reserve), RestoreDisplayMode (counted), SetCooperativeLevel (the window and flags kept), SetDisplayMode (the mode kept, the shared block's `mode_serial` moved; or refused), and the surface's Release, Lock and Unlock. The live objects are counted: a closed window leaves none.
- **The GPU.** BAR0's writes go to `test_reg_write` (`REG_WRITE_HOOK`, which `glide/gpuenv.h`'s `reg_write` takes when it is defined): CONTROL's RESET and RUN as the GPU has them, and at each RING_TAIL every packet from RING_HEAD to the new tail taken (none may pass the ring's end, and the GPU must be running): the first 2048 kept with their first 264 dwords, a FENCE completing unless the scenario stalls the GPU, an LFB_READ in 565 writing each pixel's place there (x + 7y) into the staging rows, and, when the scenario asks, the GPU restarting (the block's `state_epoch` moved) once the first OPEN, or the next CLEAR, is in the ring, or the screen's mode changing (`mode_serial` moved) as the GPU is next started (CONTROL's RUN).
- **The log.** The real line writer, its characters handed to `test_put` instead of BAR0's DEBUG register (`LOG_PUT_HOOK`, which `driver32/logline.c`'s `LOG_PUT` takes when it is defined), so the lines are what the host would get. Each character must go to the register window under the right lock (the shared block's held, or for another build's block that one free), and a line ends at its newline; only a `file` or `program` line may reach the writer's 159-character cut.
- **The machine.** A shared block of this build (16 MB of video memory, the GPU's reserve at its top, `mode_serial` 1) and BAR0 (its ID, and DEBUG reading the scenario's bits) in memory below 4 GB, as the DLLs keep addresses in 32 bits (`test_glide_low.c`: VirtualAlloc, apart because `windows.h` and the DLLs' own KERNEL32 declarations do not mix). PROFILE is the full profile less GLIDE, the card of an 86Box before Glide drew, unless the scenario draws, which gives the full profile. DllMain gets the process attach (instance 10000000h) at the start.

Each scenario runs as a process of its own (the DLLs find the card once a process), `test_glide2x` or `test_glide3x` with the scenario's name, and is a test of the suite. A Glide 2 **session** is grGlideGetVersion, grSstQueryBoards, grGlideInit, grSstQueryHardware, five grSstWinOpen (640x480, 60 Hz, two colour buffers and an aux buffer), three guMovieStart, two grBufferSwap and grGlideShutdown, then the detach a FreeLibrary brings. The **screen calls** are every export of the screen, the frame buffer and the board's status, called without a window (none may reach for one).

| Test | DEBUG, the machine | Checked |
|---|---|---|
| `glide2x_glide` | A0h (EVENTS, GLIDE) | A Glide 2 session: the Voodoo3's board; the first six lines `loaded`, `file (the system folder): C:\WINDOWS\SYSTEM\GLIDE2X.DLL`, `program C:\GAMES\TEST\TEST.EXE`, `build`, `environment: FX_GLIDE_NO_SPLASH=1 sst_gamma=1.3` (without `SSTX=1`) and `first call: grGlideGetVersion`; `first call: guMovieStart (does nothing yet)`; 8 `first call:` lines; all five refused windows; `the card draws no Glide`, once; `call record: 8 exports, 15 calls; 1 of the exports do nothing yet` and its `done` and `nothing yet` groups in order, once (not again at the detach, with no calls since); no stale line |
| `glide2x_minimal` | 20h (EVENTS) | The session: the board; `loaded`, `the card draws no Glide`, three windows and `refused again`, six lines in all, none of the DLL check's, the environment's, `first call:` or the record's |
| `glide2x_directx` | 01h (LISTENING) | The session: the same six lines |
| `glide2x_stale` | 20h; the display driver's stamp `0000000` | The session: the board, and `possible stale Glide DLL: built` the stamp`, the display driver 0000000` |
| `glide2x_head` | A0h; a 16-byte answer | The session: the board, the `gives no stamp (an older build)` line, no `build` line |
| `glide2x_other` | A0h; the block's version and the answer's `sb_version` SB_VERSION + 1 | The session: no board; the one `no Glide board: another build's display driver` line, with both versions |
| `glide2x_none` | A0h; no escape | grGlideInit, the screen calls, grSstWinOpen, the screen calls again, the session: no board, no line |
| `glide2x_long` | A0h; the program `C:\GAMES\A FOLDER WITH A RATHER LONG NAME\A PROGRAM WITH A RATHER LONG NAME.EXE` | grGlideInit and 70 grSstWinOpen asking for the window's size, no refresh rate, window 12345678h, colour format 3, origin 1, buffers 3+1: the prefix's name cut to `A PROGRAM WITH A RAT`, the whole path in the `program` line, 64 lines `grSstWinOpen refused: window size, no refresh, window 12345678, colour format 3, origin 1, buffers 3+1`, one `refused again`, no line cut |
| `glide2x_folder` | A0h; the DLL `C:\GAMES\UNREAL TOURNAMENT GAME OF THE YEAR EDITION\UNREAL TOURNAMENT\SYSTEM\GLIDE2X.DLL`, the program `UNREALTOURNAMENT.EXE` beside it; an environment of a 120-character `FX_GLIDE_LONG` and nine 60-character `SST_VARIABLE_`n | grGlideInit, grSstQueryHardware, four grBufferClear, twelve state calls once each, grGlideShutdown and the detach: the board; `file (not the system folder, C:\WINDOWS\system): C:\GAMES\UNREAL TOURNAMENT` and the rest of the path, that line cut; the `program` line; `environment: FX_GLIDE_LONG=` (cut to its room), five `environment, more:` lines (up to `SST_VARIABLE_5`), `environment: 4 more lines not shown`, no `PATH`; `call record: 16 exports, 19 calls; 0 of the exports do nothing yet` and its `done` group, busiest first then by name, over more than one line |
| `glide2x_screen` | A0h; the card draws Glide | A window at 640x480: exclusive mode for window 5150h (flags 11h), the mode 640x480 at 16 bpp and 60 Hz; OPEN's size, flags (two colour buffers, AUX) and staging surface (its offset, (8192 + 4096) × 480 bytes); the first STATE every register from 0 (the clip window the screen, the back buffer, the colour mask alone, 4x4 dithering, the constant FFFFFFFFh, the LFB constant FFh); gamma 1.3 (entry 128 149-151); the screen's size. A clip window and a clear, none sent before the swap; at the swap the clip window's STATE, the CLEAR's colour (FF8040h), alpha and depth, SWAP's interval, the batch ending with a FENCE, no swap pending. Gamma 2.2 (entry 128 184-188, 255 at 255, 0 at 0). A 565 write lock: its rows (the staging surface, stride 4096), its READ, its unlock's WRITE with CHANGED; an 8888 lower-left write lock: stride 8192, its READ's LOWER_LEFT; a second unlock refused; a read lock: the read rows (8192 × 480 on), stride 4096; a region written with no read first, its WRITE and pixels in the rows; rectangles whose sums wrap refused; a region read copying the GPU's pixels out; grSstStatus 0FFFF05Fh, not busy. grSstWinClose: CLOSE, the mode restored once, `DDSCL_NORMAL`, no DirectDraw object left; the screen calls without a window; no second CLOSE at shutdown and the detach; the `window open:` line, and `the window: 5150, class "TestWindow"` with `  at 0,0 640x480, foreground, topmost, "Test Game"`; no `refused`, `not done yet` or `goes again` line |
| `glide2x_snap` | 20h; the card draws Glide | A triangle, a polygon of four (a fan of 6 vertices), a point and a line with the snap bias (3 << 18) on x and y: their vertices in range and exact (y 20.5625 kept); the biased triangle's cull sign the unbiased one's; x 2100, 2110 and 2120 wrapped to −1996, −1986 and −1976; x 134217712 and 134217704 (just inside 2^27) to −16 and −24; x 2^27, y not a number, an infinite x drawn not (a triangle, a line, a point); at Glide (A0h) the wrap's note once (`the first x 786442 y 786442 as 10 10`), and at the close `window closed after 0 swaps: triangles 9 asked, 6 sent, 1 culled`, `  points 2, lines 2; 4 primitives dropped` and `  0 triangles wrapped apart, 25 vertices wrapped`; no `possible bad vertices`. A second window: the range's edges (2048 to −2048, −2048 and 2047.9375 kept, −2048.0625 to 2047.9375, −6144 to −2048, −4096 to 0, 2100.123 to exactly 2100.123 − 4096); three triangles and a line whose corners wrap by different multiples drawn not, and the first culled by GR_CULL_NEGATIVE from its own values: `triangles 11 asked, 7 sent, 1 culled`, `points 0, lines 1; 0 primitives dropped`, `5 triangles wrapped apart, 15 vertices wrapped` (the corners of triangles not sent not counted) |
| `glide2x_bad` | A0h; the card draws Glide; the program `UNREALTOURNAMENT.EXE` (20 characters) | 99 triangles with a vertex not a number: no finding; the 100th: `possible bad vertices: 100 of 100 primitives dropped in this window (not a number, or past 2^27)`; 50 more: not said again. A new window: 1000 good triangles, then 110 bad: no finding (fewer than one in ten); the 111th: `111 of 1111`. A third window, five triangles and 123456 grAlphaControlsITRGBLighting, then the process's end: the recorder's ` x123456 at ` and the window's counts; no line cut |
| `glide2x_draw` | 20h; the card draws Glide | A triangle's DRAW of 3 vertices, its vertex without the STW hint (TMU0's 1/w the vertex's, TMU1's s and t TMU0's), with it and both TMUs active (each TMU's own), and with it and neither active (none applies); TMU1's own 1/w holding garbage: with TMU0 passing it through, its own s and t and the vertex's 1/w, and its own 1/w with its hint; with TMU0 combining ZERO, its own s and t; with TMU0 active by a LOCAL factor and only TMU0's 1/w hinted, TMU0's 1/w, s and t; with TMU0 passing through and only TMU0's 1/w hinted, the vertex's; two triangles one DRAW of 6, a blend change between two DRAWs and the blend's STATE; a positive triangle dropped by GR_CULL_POSITIVE, kept by GR_CULL_NEGATIVE; a vertex not a number drawn not; a point's triangle; an x-major line's two triangles; delta0's flat colour, kept after grColorCombine, gone after a preset without it; a twisted polygon of 100 vertices through the clipper; guFogGenerateExp rising to 255 and guFogGenerateLinear from 0 to 255; the fog mode and table in the registers; drawing into the front buffer sent without a swap; grGlideGetState and grGlideSetState restoring the constant colour; no `not done yet` line |
| `glide2x_textures` | 20h; the card draws Glide | grTexMaxAddress 2 MB − 16 before a window and (16 MB − 3 × 614400 − 256 KB) / 2 − 16 with one; grTexMinAddress 0; the programming guide's example's 10912 bytes (both calls); a 64 mipmap of 565 to 1x1 as 7 TEXDATA at their places (the 64 level 8192 bytes at 1000h, the 32 level 2048 after it); one row of a level on TMU1; grTexSource's three registers; a TMU's filter, clamp, mipmap mode, LOD bias (0.6 as 0.5), detail and TMU1's TRILINEAR_EVEN combine; the palette to both TMUs as RGB, and part of it; the texture manager's two allocations (IDs 0 and 1, the room left), guTexSource's address and current mipmap, guTexMemReset; a `.3df` file's header (4x2, levels 8-6, aspect 2, format 565, 22 bytes) and texels, a missing file refused, no file left open; no `not done yet` line |
| `glide2x_recorder` | A0h; the card draws Glide | The fault catcher placed at the first call, answering a call without a fault with the search going on; a window, five grBufferNumPending, two more on another thread (78h), and a clear, then the process's end: `the last 5 Glide calls (unloaded without grGlideShutdown), oldest first:`, the lines of grSstWinOpen and grBufferClear, one of grBufferNumPending with ` x5 at ` and one with ` x2 at ` and `thread 78` |
| `glide2x_nomode` | 20h; the card draws Glide; SetDisplayMode refused | A window at 512x384 opened all the same, `grSstWinOpen: Windows refused 512x384 at 16 bpp`; two colour buffers given an aux buffer; no mode restored at the close |
| `glide2x_unload` | 20h; the card draws Glide | A window and a clear left unsent, then the process's end: CLOSE sent, the CLEAR not, DirectDraw not called |
| `glide2x_free` | 20h; the card draws Glide | A window, then a FreeLibrary: CLOSE, the mode restored, no DirectDraw object left; the fault catcher taken away |
| `glide2x_toobig` | 20h; the card draws Glide | 1920x1440 with two colour buffers and an aux buffer refused; 1600x1200 with them opened, grTexMaxAddress below 8 MB |
| `glide2x_unknown` | 20h; the card draws Glide | grSstWinOpen of GR_RESOLUTION_NONE, GR_REFRESH_NONE and two colour buffers without an aux buffer: the mode 640x480 at 60 Hz, OPEN with an aux buffer |
| `glide2x_desktop` | 20h; the card draws Glide; the desktop 640x480 at 16 bpp | The window's mode set at 8 bpp |
| `glide2x_held` | 20h; the card draws Glide | Two swaps with the GPU at work: two pending, busy; then held: the third swap sleeps (4 times at least) until the hold ends, and goes: 3 SWAPs |
| `glide2x_restart` | 20h; the card draws Glide; a restart once OPEN is in the ring | Two OPENs; then a restart under the session: the next batch's first packet a STATE of every register, the clip window set before in it |
| `glide2x_lost` | 20h; the card draws Glide | A clear unsent, the taskbar in front, the mode's serial moved: at the next swap `the screen's mode changed under the Glide window: nothing more is drawn`, `  the foreground window then: 6000, class "Shell_TrayWnd"` and `  at 0,572 800x28, foreground, topmost, ""`, once; no Glide command sent after the move (that clear, the swap, later clears and swaps, a CLOSE). First a window whose mode changes as the GPU starts inside its open's batch: refused (`grSstWinOpen refused: the screen's mode changed as the window opened`), no OPEN, the screen given back. A second window, another thread's window of the program in front: its title not read, its class `Other'Thread?`; a third, the Glide window in front: `the Glide window`; a fourth, the program's own frame (8000h) in front: described, its title not read; a fifth, the process's end with the mode gone: the lost line alone, no Glide command, no USER32 call inside DllMain |
| `glide2x_quiet` | 00h (Off); the card draws Glide | A window, the taskbar in front, the mode moved, a swap and the close: no window asked about, no line |
| `glide3x_glide` | A0h | grGlideInit; grGet's GR_NUM_BOARDS, GR_NUM_TMU, the three memories (A00000h, 200000h, 1000000h), GR_REVISION_FB, GR_BITS_RGBA (16 bytes; refused at 4), GR_FOG_TABLE_ENTRIES, GR_ZDEPTH_MIN_MAX, and an unknown 99h refused; grGetString's GR_HARDWARE and GR_VERSION; grGetProcAddress NULL, for `grChromaRangeExt` and for a name of 100 characters; grSstWinOpen refused; grGlideShutdown and the detach. The lines `loaded`, `file (the system folder): C:\WINDOWS\SYSTEM\GLIDE3X.DLL`, `grGet 99: not a query a Voodoo3 answers`, 6 `first call:` lines, `grGetProcAddress grChromaRangeExt: none`, the long name's line with its first 64 characters, and `call record: 6 exports, 18 calls; 0 of the exports do nothing yet` with its `done` group |
| `glide3x_screen` | A0h; the card draws Glide | A window at 800x600, lower-left: context 1, OPEN's size and flags (LOWER_LEFT); a gamma table of 32 entries spread over 256 levels; grSetNumPendingBuffers and a swap of interval 0; grLfbWriteRegion through the pixel pipeline (its flag, rows from the top) and `not done yet: a frame buffer write through the pixel pipeline`; grFinish; grSstWinClose(1) and its CLOSE |
| `glide3x_draw` | 20h; the card draws Glide | grSelectContext(1); a window-coordinate vertex through the layout (XY, Q, PARGB, ST0: x, y, 1/w, the packed colour, TMU1's s and t TMU0's, both TMUs' 1/w q), and with the snap bias on x and y the same vertex in range; with Q0 in the layout and no Q1, TMU1's 1/w TMU0's q0 while TMU0 is active (LOCAL) and the vertex's while it is not; a strip of four (the second triangle wound as the first), its continuation, a fan of four, two points through an array of pointers; a clip-coordinate point (XY, W, RGB) through the viewport (480, 120; 1/w 0.5; colours times 255); GR_GLIDE_STATE_SIZE, the saved state restoring the constant colour; grQueryResolutions' bytes the same with an output buffer and without; grReset(GR_VERTEX_PARAMETER); grSstWinClose(1), and grSelectContext(1) FXFALSE after it |
| `glide3x_textures` | 20h; the card draws Glide | grTexTextureMemRequired in log2 numbers (10912); the 6666 palette's entry (80C0FF40h as 82C3FF41h); a P_8 texture set after it, the device's format 6, and mirroring in TMU1's clamp register |

Every test checks that no display DC is left open and that no line was cut but a `file` or `program` line. A failing one prints its lines, and with `TEST_GLIDE_SHOW` set a passing one too.

**`glide_arithmetic`** (`tools/tests/test_gltex.c`) checks the device's arithmetic (`card/vid_vgpu98_glcpu.h`) against Glide's manuals' and the Voodoo specs' numbers: level sizes across the aspects; the programming guide's example's memory (10912 bytes both, 8736 odd, 2176 even) and a split texture's padded small levels; levels' offsets (a 256 texture's 128 level at 131072, a 128 texture's 64 level at 32768, a 1x1 texture at 8, an even texture's 64 level at 65536); the decoding of 565, 1555, 4444, 332, AI44, P8 and AP88, a reserved format refused; the W float (1/w 0.5 as 1000h, 1 as 0, just under 1 as 1, 2^−16 and below as FFFFh, 0.25 as 2000h, growing with w); and the fog table at a float, between entries and at its far end.

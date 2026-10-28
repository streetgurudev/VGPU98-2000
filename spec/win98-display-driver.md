# Windows 98 display driver

`vgpu98.drv` is a display minidriver for Windows 98 SE: a 16-bit DLL (NE format, [ne-format.md](ne-format.md)) that leaves all drawing to Windows' DIB engine (DIBENG.DLL), which draws straight into the card's linear framebuffer. The driver sets modes on the display engine ([display-engine.md](display-engine.md)), describes the framebuffer to the DIB engine, keeps the DAC in step with the palette at 8 bpp, and cooperates with the VDD when full-screen DOS sessions take the screen. Source: `driver/` (NASM), linked by `vglink` with `driver/vgpu98.lnk`; tested on the host by `drvtest`.

The interfaces below are restated from the Windows 95/98 DDK documentation and headers, used as a reference for facts only. Every import ordinal was checked against Windows 98's own KRNL386.EXE, USER.EXE and DIBENG.DLL, and the resource layouts against a shipping Windows 98 display driver.

## Module

| | |
|---|---|
| Module name | `DISPLAY` (GDI loads display drivers under this name) |
| Description | `DISPLAY : 100, 96, 96 : VGPU98 2000 display driver` (aspect ratio and DPI, as Windows 3.x display drivers wrote) |
| Segments | `_TEXT` (code) and `_DATA` (the automatic data segment), both fixed and preloaded; DriverInit locks them in memory |
| Expected Windows version | 4.0 |
| Entry point | DriverInit |

Exports use the Pascal calling convention (arguments pushed left to right, the callee pops them) with far calls, and preserve SI, DI, BP and DS. The driver loads its own DS in every export.

## Exports

| Ordinal | Export | What the driver does |
|---|---|---|
| 1, 2, 6, 8, 9, 11-18, 20, 21, 27-30, 101 | BitBlt, ColorInfo, EnumDFonts, Output, Pixel, StrBlt, ScanLR, DeviceMode, ExtTextOut, GetCharWidth, DeviceBitmap, FastBorder, SetAttribute, CreateDIBitmap, DibToDevice, StretchBlt, StretchDIBits, SelectBitmap, BitmapBits, Inquire | Jumps to the DIB engine routine of the same name |
| 3 | Control | DirectDraw's escapes (QUERYESCSUPPORT for DCICOMMAND, the DCICOMMAND commands: [directx-hal.md](directx-hal.md)), the Glide DLLs' (QUERYESCSUPPORT for VGPU98_ESC_INFO, VGPU98_ESC_INFO: [below](#the-glide-dlls-escape)), and while PROFILE has OGL and RENDER3D the OpenGL ICD's (QUERYESCSUPPORT for OPENGL_GETINFO, OPENGL_GETINFO, VGPU98_ESC_ACCESS: [opengl.md](opengl.md), How Windows finds the ICD); MOUSETRAILS (39) goes to DIB_Control after the cursor notes it ([below](#the-cursor)); every other escape goes to DIB_Control untouched |
| 7, 10, 23-26 | EnumObj, RealizeObject, GetPalette, SetPaletteTranslate, GetPaletteTranslate, UpdateColors | Jumps to the DIB engine's `...Ext` routine, adding the driver's PDEVICE as the last argument |
| 102, 103 | SetCursor, MoveCursor | The card's hardware cursor, or the DIB engine's (DIB_SetCursorExt, DIB_MoveCursorExt) as [below](#the-cursor) has it; each notes first whether SetCursor gave a shape (NULL hides the cursor) and where MoveCursor put it, which VGPU98_ESC_ACCESS reads to keep the DIB engine's cursor out of a window only when it is over it ([opengl.md](opengl.md)) |
| 19 | DibBlt | Jumps to DIB_DibBltExt, adding whether the mode is palettized (word) |
| 4 | Disable | Back to VGA text mode (below) |
| 5 | Enable | GDIINFO and PDEVICE (below) |
| 22 | SetPalette | DIB_SetPaletteExt updates the colour table; the driver loads the changed entries into the DAC unless a DOS session owns the screen |
| 31 | ReEnable | Mode change without a restart (below) |
| 32 | DDIGammaRamp | The gamma ramp: Windows' `SetDeviceGammaRamp` and DirectDraw's gamma control, while the card has PROFILE.GAMMA ([below](#gamma)) |
| 104 | CheckCursor | DIB_CheckCursorExt while enabled (called at timer time) |
| 450 | GetDriverResourceID | Resource 3 (the OEM fonts) becomes 2003 (large fonts) above 96 DPI; other IDs are returned as they are |
| 500 | UserRepaintDisable | USER holds repaints off and on; a repaint asked for meanwhile is done when they are allowed again |
| 700 | ValidateMode | Windows finds it by name, so its name is a resident name (below) |

DeviceMode, GetCharWidth, DeviceBitmap and SetAttribute are exported without the "uses the data segment" flag, as the DDK's samples do.

## Imports

| Module | Ordinal | Function | Use |
|---|---|---|---|
| KERNEL | 47 | GetModuleHandle | Find USER |
| KERNEL | 50 | GetProcAddress | USER.275 (RepaintScreen), looked up when the INT 2Fh hook is installed, since USER may load after the display driver |
| KERNEL | 170 | AllocCStoDSAlias | Write the previous INT 2Fh handler into the code segment |
| KERNEL | 176 | FreeSelector | Free that alias |
| KERNEL | 230 | GlobalSmartPageLock | Lock the code and data |
| DIBENG | 300 | CreateDIBPDevice | Build the PDEVICE; returns its result in EAX, not DX:AX |
| DIBENG | 5 | DIB_Enable | Both Enable passes |
| DIBENG | 105, 106 | DIB_BeginAccess, DIB_EndAccess | The PDEVICE's access callbacks; cursor exclusion during ReEnable |
| DIBENG | 405, 407, 104 | DIB_SetPaletteExt, DIB_SetPaletteTranslateExt, DIB_CheckCursorExt | |
| DIBENG | 1-30, 101-103, 400-408 | the forwarded routines | |

## DriverInit

Called once when the module loads (CX = heap size, DI = instance handle, DS = data segment, ES:SI = command line). It locks the code and data, gets the VDD's API entry point (INT 2Fh AX=1684h, BX=000Ah) and the system VM's handle (INT 2Fh AX=1683h), and looks for the display engine: identity 98h, revision 1 or later, display engine and linear framebuffer features, at least 1 MB of video memory, and a framebuffer address (BAR1 assigned). Without them it returns 0, and Windows uses its own VGA driver. Otherwise it reads the configuration (below) and returns 1.

## The configuration

`VDD_GET_DISPLAY_CONFIG` fills a DISPLAYINFO with the registry's display settings: resolution (diXRes, diYRes), depth (diBpp), DPI (diDPI), refresh rate (diRefreshRateMax: a rate in Hz, 0 for the adapter default, FFFFh for "optimal") and the monitor's highest frame rate (diHighVert). The call fails when EAX comes back as 85h (no VDD: the function code unchanged) or -1 (the VDD could not read the registry); any other value is success, as in the DDK's samples. Without a VDD, or if the call fails, the driver takes 640x480 at 8 bpp and 96 DPI.

A configuration the card cannot run is changed: a depth other than 8, 16 or 32 becomes 8; a resolution that is not valid or does not fit in video memory becomes 640x480 at the same depth, then at 8 bpp.

A mode is valid at any width that is a multiple of 8 from 640 to 1920 and any height from 480 to 1440, not only the mode table's, so modes added to the registry work. The mode table supplies refresh rates and VBE numbers:

- **Refresh rate:** the requested rate if the mode lists it; for "optimal", the mode's fastest rate up to the monitor's highest (when known); otherwise the mode's default rate. Modes outside the table run at 60 Hz.
- **SCRATCH:** the mode's VBE number for the depth, with bits 14 (linear) and 15 (memory kept), so VBE function 03h in the system VM reports the desktop mode; C000h for modes without a number.

## Enable

`Enable(lpDevice, style, lpDeviceType, lpOutputFile, lpStuff)`, called twice by GDI.

**Style bit 0 set: GDIINFO.** The driver calls DIB_Enable, which fills the drawing capabilities (dpCurves through dpStyleLen) and the size of its part of the PDEVICE, then sets:

| Field | Value |
|---|---|
| dpVersion, dpTechnology | 0400h, DT_RASDISPLAY |
| dpHorzRes, dpVertRes, dpBitsPixel, dpPlanes | the configured mode, 1 plane |
| dpHorzSize, dpVertSize | the screen at the configured DPI, in mm |
| mapping modes (dpMLoWin ... dpTwpVpt) | from the logical inch: window extents 254, 2540, 100, 1000, 1440 (units per inch), viewport extents DPI and -DPI |
| dpLogPixelsX, dpLogPixelsY | the DPI |
| dpNumBrushes | -1 |
| dpDCManage | DC_IgnoreDFNP |
| dpCaps1 | the DIB engine's, plus C1_BYTE_PACKED, C1_COLORCURSOR, C1_REINIT_ABLE (not C1_SLOW_CARD), and C1_GAMMA_RAMP (20h) while the card has PROFILE.GAMMA ([below](#gamma)) |
| dpDEVICEsize | the DIB engine's size + 40 (bitmap header) + 1024 (colour table), whatever the depth, so ReEnable can change the depth in the same PDEVICE |
| 8 bpp | 16 pens, 20 colours, 256 palette entries of which 20 reserved, 18 bits of colour resolution; dpRaster adds RC_DIBTODEV, RC_PALETTE, RC_SAVEBITMAP |
| 16, 32 bpp | -1 pens and colours, no palette; dpRaster adds RC_DIBTODEV |

It returns the size of the GDIINFO, 110.

**Style bit 0 clear: PDEVICE.** On the first enable the driver first issues INT 2Fh AX=4000h (stop I/O trapping): the display engine's registers are sequencer registers, which the VDD traps until the driver owns the hardware. Then:

1. Sets the mode: `VDD_PRE_MODE_CHANGE`; the engine's format, size, pitch, start 0, refresh rate, bank 0 and SCRATCH, then ENABLE; on the first enable, a selector for the whole framebuffer (DPMI: allocate a descriptor, map the physical framebuffer, set base and limit); `VDD_REGISTER_DISPLAY_DRIVER_INFO` (83h: ECX = video memory size, DX = 0, no extra screen selector; the VDD passes it on to the mini-VDD's REGISTER_DISPLAY_DRIVER, [minivdd.md](minivdd.md)); `VDD_DRIVER_REGISTER` (ECX = pitch x height, EDX = -1, ES:DI = RestoreDesktopMode); `VDD_POST_MODE_CHANGE`; `VDD_SAVE_DRIVER_STATE`; clears the screen.
2. Calls DIB_Enable.
3. Writes a BITMAPINFOHEADER after the DIB engine's part of the PDEVICE: the mode's width, height (positive) and depth, one plane. At 8 bpp the colour table follows: on the first enable, and on a ReEnable from 16 or 32 bpp, the 20 static colours (entries 0-9 and 246-255, the light ones marked MAPTOWHITE, entries 8, 9, 246 and 247 also NONSTATIC) and 236 free entries; a ReEnable from 8 bpp keeps the palette Windows has built.
4. CreateDIBPDevice(header, PDEVICE, framebuffer selector:0, flags): MINIDRIVER and VRAM, plus FIVE6FIVE at 16 bpp and PALETTIZED at 8 bpp.
5. Sets the PDEVICE's access callbacks: deBeginAccess to the driver's own, which waits for the GPU's last fence ([directx-hal.md](directx-hal.md), "GDI and the GPU") and then goes on to DIB_BeginAccess; deEndAccess to DIB_EndAccess. The DIB engine's software cursor excludes itself.
6. At 8 bpp: DIB_SetPaletteTranslateExt(NULL) and the DAC loaded from the colour table (8-bit components shifted to the DAC's 6 bits).
7. On the first enable, hooks INT 2Fh.

It returns 1, or 0 if the mode cannot be set.

EDX = -1 in `VDD_DRIVER_REGISTER` tells the VDD not to run DOS programs' 16-colour VGA graphics in a window. It would do that in off-screen video memory through the A0000h window, which needs planar VGA memory beside the desktop and the mini-VDD's bank functions; this card has neither (in an extended mode A0000h is a linear bank window). Such programs run full screen, as with the DDK's generic framebuffer driver on cards it cannot virtualize.

Around every mode set (the PDEVICE pass of Enable, and so ReEnable) the driver stops the GPU: under the ring lock it lets the GPU finish, then writes HOLD_DRIVER with RESET; the hold is let go when the new mode is on the screen ([gpu-registers.md](gpu-registers.md)). The mode goes into the shared block for the DirectX DLL, and, when DirectDraw has a driver object, the driver description is rebuilt and handed to DirectDraw's SetInfo ([directx-hal.md](directx-hal.md)). Disable stops and holds the GPU likewise, and leaves it held.

Modes narrower than 640 pixels (the low-resolution game modes) are set with the display engine's double scan, so they fill the screen as on period hardware ([display-engine.md](display-engine.md)).

## ReEnable

`ReEnable(lpDevice, lpInfo)`, for a change of resolution or depth without a restart (advertised by C1_REINIT_ABLE). The driver reads the configuration again, excludes the cursor (DIB_BeginAccess with CURSOREXCLUDE over the old screen), runs the PDEVICE pass of Enable on the same PDEVICE, ends the exclusion, and on success fills the GDIINFO at lpInfo and returns 1. If the new mode cannot be set, the old configuration comes back, its mode is set again, USER repaints, and ReEnable returns 0.

## Disable

Marks the PDEVICE busy, turns the card's hardware cursor and gamma table off (what they show is kept for the next Enable), turns the display engine off while it still owns the hardware, gives the hardware back to the VDD (INT 2Fh AX=4007h), calls `VDD_DRIVER_UNREGISTER`, sets mode 03h through INT 10h, and removes the INT 2Fh hook. Returns 1.

## Full-screen DOS sessions

| Event | Driver |
|---|---|
| INT 2Fh AX=4001h (a full-screen session takes the screen) | marks the PDEVICE busy (GDI stops drawing); clears the caller's carry flag (switching is fine) |
| RestoreDesktopMode (far call from the VDD when the desktop comes back) | sets the mode again (`VDD_PRE_MODE_CHANGE`, the engine), reloads the DAC at 8 bpp, clears busy, writes the hardware cursor's pattern and the gamma table again ([below](#the-cursor), [Gamma](#gamma)), `VDD_SAVE_DRIVER_STATE`, clears the screen |
| INT 2Fh AX=4002h (the desktop has the screen again) | restores the mode as above if it is still busy, then asks USER to repaint (RepaintScreen) |
| other INT 2Fh functions | passed to the previous handler with every register untouched |

The previous handler's address is kept in the code segment, so the hook reaches it with a far jump through CS without disturbing any register.

## The cursor

`cursor.asm`. With PROFILE.CURSOR and the register window mapped (DriverInit), Windows' own shapes go to the card's hardware cursor ([gpu-registers.md](gpu-registers.md), The hardware cursor); otherwise every shape goes to the DIB engine's software cursor, as on a card without the hardware cursor. The project supplies no cursor images.

**SetCursor(lpCursorShape)** gets a CURSORSHAPE: the hot spot (x, y), width, height, the AND mask's bytes a row, planes and bits a pixel, then the AND mask (1 bit a pixel), then the image (1 bit a pixel, or with C1_COLORCURSOR the screen's bits a pixel, its rows whole words). It returns 1.
- **The card takes a shape** of 1 to 64 pixels each way, one plane, an AND mask whose rows hold its width (16 bytes at most), and 1 bit a pixel or the screen's, while pointer trails are off. The driver keeps a copy of it, writes it into the slot the card is not showing, two 32 KB slots from 128 KB into the GPU's reserve: the AND mask padded to 64x64 with ones, the image at the mode's bytes a pixel padded with zeros, a mono image's set bits as all ones. Then CURSOR_BASE names the slot, CURSOR_CONTROL enables the cursor, and CURSOR_POS gets its place. Changing over from the DIB engine's cursor first hides that one (DIB_SetCursorExt with NULL).
- **Any other shape**, and every shape while trails are on, goes to DIB_SetCursorExt. Changing over from the card's cursor first turns it off and tells the DIB engine where the cursor is (DIB_MoveCursorExt), which it was not told meanwhile.
- **NULL** hides the cursor: the card's off, and DIB_SetCursorExt with NULL.
- **While a DOS session owns the screen** (the PDEVICE busy), a shape for the card is only kept: the desktop's return writes it.
- A SetCursor while another is writing a shape (Windows may call it asynchronously) returns at once: the shape on its way stands.

**MoveCursor(x, y)**, at the mouse's interrupt time, stores the place as one dword. With the card's cursor it writes CURSOR_POS, the place less the hot spot (each half signed), unless a shape is on its way, which then writes the place itself, again if a MoveCursor came meanwhile; it counts each move for that. Otherwise DIB_MoveCursorExt.

**Neither waits** for the GPU or anything else, takes no lock, calls nothing that may block, and writes no I/O port but the debug register (whose index it puts back).

**Pointer trails** (Control's MOUSETRAILS, a count, 0 for none): on, the cursor goes to the DIB engine, which draws the trails, given the kept shape; off, back to the card when the kept shape is one it can show. The escape then goes to DIB_Control as always.

**Mode changes:** before every mode set (Enable, ReEnable) and the desktop's return from a DOS session, the card's cursor goes off, so no frame shows the old pattern read at a new depth or one the DOS session wrote over the GPU's reserve; after them the kept shape is written again, at the new depth, and the cursor comes back. A colour shape of another depth is hidden until Windows gives another. **Disable** turns the card's cursor off.

**The OpenGL ICD's escape** (VGPU98_ESC_ACCESS): while the card has the cursor, a begin answers 0, as the cursor is never in a window's pixels ([opengl.md](opengl.md)).

**Its lines** (Debug output, below), each once a session: `cursor: the card's (hardware)` at the first shape the card takes; `cursor: the DIB engine's for a shape the card cannot show (WxH, p planes of b bits)`; `cursor: the DIB engine's while pointer trails are on`; `cursor: the DIB engine's (the card has no hardware cursor)` at the first shape on a card without PROFILE.CURSOR.

## Gamma

`gamma.asm`. Windows' `SetDeviceGammaRamp` and DirectDraw's gamma control (and with it Direct3D's) come to **`DDIGammaRamp(lpDevice, fGetSet, lpGammaRamp)`**, ordinal 32, which C1_GAMMA_RAMP in GDIINFO's dpCaps1 announces. The DIB engine has no gamma, so nothing is passed on to it.

- **The ramp:** 768 words, red, green and blue, 256 each, from 0 to FFFFh; an 8-bit DAC takes each word's high byte. A nonzero `fGetSet` sets it, 0 gets it, the sense the DDK's sample code uses (its document says the opposite). It returns 1, or 0 refused.
- **A set** keeps the 768 words, for gets, and writes the card's table from them, entry *i* being the three high bytes as `R << 16 | G << 8 | B` ([gpu-registers.md](gpu-registers.md), The gamma table). The table goes into whichever of two 1 KB slots the card is not showing, at the GPU's reserve + 30000h; GAMMA_BASE then names it and GAMMA_CONTROL turns it on, so no frame shows half a table. An identity ramp turns GAMMA_CONTROL off. Nothing waits for the GPU.
- **A get** returns the words kept, or an identity ramp before any set.
- **Refused** at 8 bpp (the palette is the DAC there) and on a card without PROFILE.GAMMA (DriverInit's check, after the register window is mapped; C1_GAMMA_RAMP is then not set).
- **The ramp is global** and stays until the next set, whatever the mode: before a mode set and the desktop's return from a DOS session the card's table goes off, and after them the kept ramp is written again (off at 8 bpp until a direct-colour mode). While a DOS session has the screen only the ramp is kept. DirectDraw saves and restores a full-screen program's ramp itself, and the driver does not. **Disable** turns the table off.
- **Its lines** (Debug output, below): at start `gamma: the card's table (DDIGammaRamp, C1_GAMMA_RAMP)` or `gamma: none (the card has no gamma table: 86Box's Gamma setting is Off)`; the first four sets (`gamma: set n (red, green, blue at 128: r/g/b)`) and gets one by one, then a line that later ones are counted, and the totals at Disable; each refusal's reason once.
- **Who sets it:** programs, and the control program's VGPU98 Color tab, which also applies the saved ramp at start-up ([driver-package.md](driver-package.md), The control program).

## ValidateMode

`ValidateMode(lpDispValMode)` returns VALMODE_YES (0), VALMODE_NO_WRONGDRV (1: no VGPU98 display engine), VALMODE_NO_NOMEM (2: the frame does not fit below the GPU's reserve at the top of video memory), VALMODE_NO_NODAC (3: a depth other than 8, 16 or 32) or VALMODE_NO_UNKNOWN (4: a size the engine does not take; below 640x480, any size not in the mode table's low-resolution modes). DISPVALMODE holds dvmSize, dvmBpp, dvmXRes and dvmYRes (words). 640x480 at 4 bpp is valid: the INF hands that mode to Windows' own VGA driver. Windows may call ValidateMode while another driver is active, so it looks for the card itself when DriverInit has not.

## The Glide DLLs' escape

`glide2x.dll` and `glide3x.dll` find the card through an escape of the driver's own, VGPU98_ESC_INFO (7098h), called with GDI's ExtEscape on a display DC ([glide.md](glide.md)):

- **QUERYESCSUPPORT (8)** with VGPU98_ESC_INFO as its input word returns 1 (DX:AX = 0:1). For DCICOMMAND it returns DD_HAL_VERSION ([directx-hal.md](directx-hal.md)); for any other escape it goes to DIB_Control.
- **VGPU98_ESC_INFO** takes a question as its input, a `vgpu98_escquery_t`: `magic` (offset 0) ESC_INFO_MAGIC, 49454756h (`VGEI`), and `out_size` (offset 4), the output buffer's bytes. Control is never told the output buffer's size, so the question carries it. With an input and an output buffer (neither selector null), the question's magic ESC_INFO_MAGIC and `out_size` at least 16, the driver writes the answer, a `vgpu98_escinfo_t` (32 bytes), as far as `out_size` has room for it, and returns 1 (DX:AX = 0:1). The head, its first 16 bytes, is always written; the stamp after it only when `out_size` is 32 or more, room for the whole answer. `size` says what was written:

  | Offset | Field | Value |
  |---|---|---|
  | 0 | `magic` | ESC_INFO_MAGIC |
  | 4 | `size` | The bytes written: 32 with the stamp, 16 (the head) without |
  | 8 | `shared_linear` | The shared block's linear address, as DriverInit found it (DPMI's segment base); 0 if it could not |
  | 12 | `sb_version` | SB_VERSION, the block's version as the driver was built |
  | 16 | `stamp` | The driver's build stamp ([registers.md](registers.md)), padded with NULs to 16 bytes; not written in a 16-byte answer |

  Otherwise (no input, no output buffer, another magic, an output under 16 bytes) it is refused: it returns -1 (DX:AX = FFFFh:FFFFh) and writes nothing. The Glide DLLs ask with ESC_INFO_MAGIC and 32. A question for 16 bytes, as the first Glide DLLs asked, gets the head alone: `size` 16, nothing written past it. Control is not told the input's size either: it reads the question's 8 bytes, which only a caller sending a shorter input under this escape's number could make it read past.

  The stamp is the driver's own, BUILD_STAMP from `stamp.inc`, kept as a string in the code segment. It is at most 15 characters, so a NUL always follows it: `ddraw.asm` does not assemble with a longer one (`%error`).

The structures and the escape's number are defined in `driver32/shared.h` with the shared block (the head's size there as ESC_INFO_HEAD); `sbgen` writes them (`VGPU98_ESC_INFO`, `ESC_INFO_MAGIC`, the `EQ_` and `EI_` field offsets, `EI_stamp` among them, the head's size, and `EI_SIZE`) into the driver's `shared.inc`.

## Resources

All of type OEMBIN:

| ID | Contents |
|---|---|
| 1 | Display metrics (scroll thumb size, icon and cursor ratios, border widths) and 20 default system colours (COLORREF: red, green, blue, 0) |
| 2 | The colour dialog's 48 basic colours: a count, then COLORREFs |
| 3 and FONTS | The stock fonts at 96 DPI: three LOGFONT records (OEM fixed "Terminal", ANSI fixed "Courier", ANSI variable "MS Sans Serif"), each followed directly by its NUL-terminated face name |
| 2003 | The stock fonts at 120 DPI |

Plus version information (type 16): file version 4.10.0.1, a display driver (VFT_DRV, VFT2_DRV_DISPLAY).

## Debug output

With 86Box's debug log setting on, the driver writes lines to the card's debug register (sequencer index E0h, [registers.md](registers.md)), checking before each character that the register reads 01h: loading, the configuration, each mode set, a failed enable, the desktop restored, and Disable.

## Host tests

`drvtest` loads vgpu98.drv with its own NE loader under Unicorn and stands in for KERNEL, DIBENG, USER, the VDD, DPMI and the INT 2Fh and INT 21h services with recording stubs; the card is the display engine model from `biostest`. It checks DriverInit, both Enable passes at several modes and memory sizes (engine registers, GDIINFO fields, PDEVICE, bitmap header, colour table, DAC, the cleared screen, the framebuffer mapping, the order of the VDD calls, STOP_IO_TRAP before the first register write), SetPalette, CheckCursor, every kind of thunk, GetDriverResourceID, screen switching and RestoreDesktopMode, held repaints, ReEnable (resolution and depth), Disable, ValidateMode, the configuration fallbacks, and Control's escapes: QUERYESCSUPPORT for VGPU98_ESC_INFO (1), for DCICOMMAND (00FFh) and for another escape (handed to DIB_Control); another escape handed to DIB_Control with its arguments; VGPU98_ESC_INFO's answer to a 32-byte question (its magic, size 32 and version, its address inside the driver's data segment holding the shared block's magic, version and size, and its stamp, the build's, padded with NULs; nothing written past its 32 bytes); its answer to a 16-byte and to a 20-byte question (its magic, size 16 and address, nothing written past the head); and its four refusals (no output buffer, no input, another question, a 15-byte output), each FFFFh and none writing; and, without a register window to read PROFILE from, QUERYESCSUPPORT for OPENGL_GETINFO, OPENGL_GETINFO and VGPU98_ESC_ACCESS handed to DIB_Control (the cases with PROFILE.OGL are not reached on the host). The cursor's scenario gives the card a register window (REGS_BASE, its DPMI mapping, PROFILE in plain memory) and checks: MoveCursor before any shape (the DIB engine's); a mono shape at 8 bpp (the DIB engine's cursor hidden once, the pattern in slot 1 against the shape's own masks, CURSOR_BASE, CURSOR_CONTROL, CURSOR_POS by the hot spot); MoveCursor (CURSOR_POS alone, a negative half); a colour shape (slot 0, no DIB engine call); a 20x7 shape padded transparent; VGPU98_ESC_ACCESS answering 0; pointer trails on (the card's off, the DIB engine told the place and given the kept shape, DIB_Control reached), a move and a shape while they are on, and off (the card's again, in the next slot, placed); a 65x32 shape (the DIB engine's) and the card's again; NULL and the shape after it; a ReEnable to 16 bpp (the mono shape at 2 bytes a pixel), a 16-bit colour shape, a ReEnable to 32 bpp (hidden), a mono shape at 32 bpp; a DOS session (a shape kept, not written; written at RestoreDesktopMode); Disable (off); and a card without PROFILE.CURSOR (every shape and move to the DIB engine, the registers untouched). What it cannot check is Windows itself: the stubs implement the DIB engine and the VDD as this page describes them.

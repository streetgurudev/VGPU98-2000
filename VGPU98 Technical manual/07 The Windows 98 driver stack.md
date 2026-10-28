# 07 The Windows 98 driver stack

Everything the card needs inside Windows 98 SE comes from one CD: eight files, all built by this project. This document covers what each file is, how Windows finds and loads it, the display driver and the mini-VDD that sit under everything else, how the pieces find each other, and the control program. Each 3D interface's own DLL has its translation document, [08](08%20DirectDraw%20translation.md) to [12](12%20OpenGL%20translation.md). The reference is [`spec/driver-package.md`](../spec/driver-package.md), [`spec/win98-display-driver.md`](../spec/win98-display-driver.md), [`spec/minivdd.md`](../spec/minivdd.md) and [`spec/directx-hal.md`](../spec/directx-hal.md).

## The package

| File | Size | Format | Source | What it is |
|---|---|---|---|---|
| `vgpu98.inf` | 3,630 | INF | written by `infgen` from `card/driver.card` and the mode tables | The install: files, registry, modes |
| `vgpu98.drv` | 37,632 | NE, 16-bit | `driver/`, NASM, about 3,800 lines | The display driver: a DIB engine minidriver |
| `vgpu98.vxd` | 9,796 | LE, 32-bit ring 0 | `minivdd/`, NASM, about 1,500 lines | The mini-VDD |
| `vgpu98dx.dll` | 262,656 | PE | `driver32/`, C, about 24,200 lines | DirectDraw and Direct3D: the DirectX 6, 7 and 8 driver interfaces |
| `glide2x.dll` | 79,360 | PE | `glide/`, `glide/glide2x/`, C | Glide 2.61 |
| `glide3x.dll` | 78,336 | PE | `glide/`, `glide/glide3x/`, C (both Glide DLLs together about 8,300 lines) | Glide 3.10 |
| `vgpu98gl.dll` | 237,056 | PE | `opengl/`, C, about 18,000 lines | The OpenGL 1.1 ICD |
| `vgpu98cp.cpl` | 30,208 | PE | `cpl/`, C, about 2,200 lines | The control program: the VGPU98 Color and VGPU98 3D tabs |

**Versions.** The five VGPU98 files carry file version 4.10.0.1 (the INF's DriverVer) and product version 1.0. The Glide DLLs carry 2.61.0.658 and 3.10.0.658, as a Voodoo3's do. Every driver's log lines also carry the build stamp, the commit it was built from.

**How they are built.** The 16-bit driver and the VxD are NASM, linked by the project's `vglink` into NE and LE. The DLLs are freestanding C, compiled by the host's GCC with `-m32 -march=i686` (no MMX, no SSE, x87 floating point) and the project's own headers, with no C library: `driver32/rt.c` supplies what the compiler expects, and every DLL imports only from Windows' own system DLLs. `drvlint` checks every file's structure in the build ([15](15%20Building%2C%20tools%20and%20file%20formats.md)). The guest processor must be a Pentium Pro, Pentium II or later; Socket 7 Pentiums and K6s lack CMOV and FCOMI.

## Installing

1. Choose **VGPU98 2000 (PCI)** in 86Box's machine settings, with its ROM in `roms/video/vgpu98/`.
2. Install Windows 98 SE. It finds a standard VGA adapter.
3. Mount `vgpu98-driver.iso`, or the `vgpu98-driver` folder (Media > CD-ROM > Folder).
4. In Device Manager, update the display adapter's driver with **Have Disk**, pointing at the CD. Restart.
5. For DirectX 8 or 9 programs, install that DirectX runtime; Windows 98 SE ships DirectX 6.1. The Glide DLLs need DirectDraw too.

**What the INF writes:**
- the eight files into the system folder;
- the device key: `DevLoader=*vdd`, and under `DEFAULT` `drv=vgpu98.drv`, `minivdd=vgpu98.vxd`, `Mode=8,640,480`, `RefreshRate=-1` (Windows' optimal), `DDC=1`;
- `MODES`: the 14 desktop resolutions at 8, 16 and 32 bpp, each with its refresh rates, and 640x480 at 16 colours through Windows' `vga.drv`;
- the OpenGL ICD's name: `HKLM\Software\Microsoft\Windows\CurrentVersion\OpenGLdrivers`, value `VGPU98` = `vgpu98gl.dll`;
- the control program's CLSID and its place among the Advanced dialog's property sheet handlers;
- matching on `PCI\VEN_9898&DEV_2000&SUBSYS_20009898`, then `PCI\VEN_9898&DEV_2000`.

There is no uninstall step: to go back, choose another driver in Device Manager.

## How Windows loads each piece

```
 boot
  |
  +-- VMM loads Windows' VDD, which loads the mini-VDD (vgpu98.vxd)
  |     the mini-VDD: allocates the ring's buffer and the GPU's page
  |     table in guest RAM, names them in BAR0 (SYSBUF, GPUVM)
  |
  +-- GDI loads the display driver (vgpu98.drv) as DISPLAY
  |     it sets the mode on the display engine, describes the frame
  |     buffer to DIBENG.DLL, maps BAR0, keeps the shared block
  |
 a program starts DirectDraw or Direct3D
  |     DDRAW.DLL -> GDI's Control escape (QUERYESCSUPPORT, DCICOMMAND)
  |     -> vgpu98.drv names its 32-bit DLL (DDGET32BITDRIVERNAME)
  |     -> DDHELP loads vgpu98dx.dll into the shared arena and calls
  |        DriverInit with the shared block's address
  |
 a program loads glide2x.dll or glide3x.dll from the system folder
  |     at its first Glide call: escape VGPU98_ESC_INFO -> the shared
  |     block -> BAR0
  |
 a program calls OpenGL
  |     opengl32.dll -> escape OPENGL_GETINFO -> "VGPU98"
  |     -> OpenGLdrivers\VGPU98 -> loads vgpu98gl.dll into the program
  |
 Display Properties > Settings > Advanced, or the Control Panel icon
        the shell loads vgpu98cp.cpl as a property sheet handler; at
        start-up, a Run entry applies the saved settings
```

## `vgpu98.drv`: the display driver

A DIB engine minidriver, as most display drivers of the time were. Windows' DIB engine (`DIBENG.DLL`) draws everything GDI asks for into the linear frame buffer; this driver sets the modes, describes the frame buffer to it, and adds what is specific to the card.

```
 GDI call (lines, text, bitmaps, BitBlt)
      |
 vgpu98.drv: most exports forward straight to DIBENG.DLL
      |        (a far jump, so the DIB engine returns to the caller)
      |        before it draws on the screen: wait for the GPU
      v
 DIBENG.DLL draws into the frame buffer (BAR1 -> video memory)
      |
 the host display's mirror picks up the changed pages -> the window
```

| File | What it does |
|---|---|
| `vgpu98.asm` | DriverInit, Enable, Disable, ReEnable, the palette at 8 bpp, the exports |
| `hw.asm` | The display engine, choosing the mode and refresh rate, the VDD |
| `dibthunk.asm` | The forwarders to the DIB engine |
| `gpu.asm` | Maps BAR0 for itself and the DirectX DLL; holds the GPU around every mode change (HOLD_DRIVER); makes GDI wait for the GPU before the DIB engine draws on the screen |
| `ddraw.asm` | DirectDraw's discovery through the Control escape, the driver description it completes, and the project's own escapes |
| `cursor.asm` | The hardware cursor, with the DIB engine's cursor as fallback ([05](05%20The%20display%20path.md)) |
| `gamma.asm` | DDIGammaRamp, ordinal 32: the gamma table ([05](05%20The%20display%20path.md)) |
| `switch.asm` | Full-screen DOS sessions: stop drawing when one takes the screen, set the mode again and repaint when it gives it back |
| `drvdebug.asm` | Debug text to the card's debug register |

**GDI is not accelerated.** GDI's drawing runs in the DIB engine on the emulated CPU, as it did with most minidrivers. The GPU accelerates DirectDraw, Direct3D, Glide and OpenGL.

**GDI and the GPU.** Before the DIB engine touches the screen, the driver waits for the GPU's outstanding work, so a GDI repaint never races a blit. The wait gives up when the GPU is held, stopped or in error, or makes no progress for about a second.

## `vgpu98.vxd`: the mini-VDD

Windows' VDD virtualizes the VGA for DOS sessions. The mini-VDD adds what is specific to the card.

- **The display engine and DOS sessions.** A DOS session in a window or in the background gets its own copy of the engine's lock and mode registers, so its BIOS calls cannot turn the desktop's display off. The engine's state is saved and restored per virtual machine as full-screen sessions come and go, and the engine is turned off before a DOS session gets the screen. A full-screen session running a VBE mode cannot be switched away from, since the desktop would draw over its frame.
- **Holding the GPU.** Before a VGA mode takes the screen, the GPU finishes, is reset and held (HOLD_VDD); it is let go when the desktop's state comes back ([04](04%20Video%20memory%2C%20the%20ring%20and%20coherency.md)).
- **The ring's buffer.** At boot it allocates the ring's buffer in guest RAM and names it in BAR0's SYSBUF registers.
- **The pool.** At boot it allocates the GPU's page table; afterwards it maps chunks of locked guest RAM into it as the DirectX DLL asks through DeviceIoControl on `\\.\VGPU98`, and takes them back when the DLL gives them up.

## How the pieces find each other

**The shared block** lives in the display driver's fixed, locked data segment. It holds BAR0's linear address, the ring lock, the log's line lock, the driver description DirectDraw is given, and state the drivers share. Its layout is `driver32/shared.h`: fixed-width fields only, so the 16-bit driver gets the same offsets from a NASM include that the project's `sbgen` writes from the header.

| Who | How it finds the card |
|---|---|
| `vgpu98dx.dll` | DriverInit's argument is the shared block's address |
| `glide2x.dll`, `glide3x.dll` | The escape VGPU98_ESC_INFO (7098h) gives the shared block's address and the driver's build stamp |
| `vgpu98gl.dll` | The same escape |
| `vgpu98cp.cpl` | The same escape, then BAR0 |

**The display driver's escapes:**

| Escape | Number | For |
|---|---|---|
| QUERYESCSUPPORT, DCICOMMAND | Windows' | DirectDraw's discovery and its driver description |
| `VGPU98_ESC_INFO` | 7098h | The shared block, for the Glide DLLs, the ICD and the control program |
| `VGPU98_ESC_ACCESS` | 7099h | Keeping the software cursor out of the way while the ICD writes a window's frame |
| `OPENGL_GETINFO` | 1101h | The ICD's name, "VGPU98", for opengl32.dll |

## `vgpu98cp.cpl`: the control program

The card's own settings, in the style of a Windows 98 property page.

```
 Display Properties > Settings > Advanced    Control Panel > VGPU98 2000
              \                                  /
               +--> vgpu98cp.cpl: two pages <---+
                     |                       |
               VGPU98 Color             VGPU98 3D
               gamma, brightness,       V-sync for Direct3D,
               contrast, per channel    OpenGL, Glide: program's
                     |                  choice, always on, off
                     |                       |
        SetDeviceGammaRamp            VSYNC_CONTROL (BAR0), taken
        -> DDIGammaRamp -> the        at each program's next flip
        gamma table                   or swap
                     |                       |
        HKLM\Software\VGPU98 Project\VGPU98 2000\Gamma and \VSync
                     |
        at start-up: Run entry "VGPU98 2000"
        -> rundll32 vgpu98cp.cpl,vgpu98_apply
```

- **The pages** are built in memory (`tmpl.c`) in MS Sans Serif 8 at the large page size, 252x218 dialog units, with standard controls and system colours throughout. Moving a trackbar shows the ramp at once; Apply saves it; Cancel puts back what the page found.
- **Where they appear:** as a property sheet handler in Display Properties' Advanced dialog (the CLSID the INF registers), and from the control program's own Control Panel icon, drawn in code.
- **At start-up,** the Run entry applies the saved settings. It exists only while a page differs from its defaults.
- **The card** is found through the display driver's escape, as the Glide DLLs find it. The pages read PROFILE to know whether the card has the gamma table and the V-sync overrides ([02](02%20The%20VGPU98%202000%20card.md)).

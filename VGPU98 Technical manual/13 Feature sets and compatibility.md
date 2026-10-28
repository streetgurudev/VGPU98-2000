# 13 Feature sets and compatibility

What each part of the card offers, what is missing, and what has been seen running. Missing items get a short note; the translation documents ([08](08%20DirectDraw%20translation.md) to [12](12%20OpenGL%20translation.md)) explain how each feature works, and [`spec/d3d-caps.md`](../spec/d3d-caps.md) lists Direct3D's caps in full.

**Status words**

| Word | Meaning |
|---|---|
| **Supported** | Built, and seen working in a VM run |
| **Built** | Built and checked on the host; no VM run has reported on it yet |
| **Partial** | Works, with the gaps noted |
| **Missing** | Not built, but could be |
| **Not planned** | Left out by choice |
| **Untested** | Nothing is known either way |

"Supported" means programs using that part ran; not that every cap or format in the row was exercised one by one.

## The card and the host

| Feature | Status | Notes |
|---|---|---|
| PCI card, VGA class (9898:2000 rev 01) | Supported | 64 KB of registers, a 128 MB prefetchable frame buffer window, a 32 KB ROM |
| Video memory 8, 16, 32, 64, 128 MB | Supported | 128 MB by default |
| More than 128 MB | Missing | The frame buffer window is fixed at 128 MB |
| AGP | Not planned | PCI only |
| 3D on the host GPU through Vulkan 1.1 | Supported | Needs `shaderClipDistance` and `shaderInt64` |
| A host without Vulkan 1.1 | Untested | The card comes up without its 3D bits: 2D and DirectDraw stay, Direct3D, Glide and OpenGL go |
| The pipeline cache | Supported | Shared by every machine; On, Start empty, Off |
| 86Box on Windows | Supported | Built with MSYS2 UCRT64 |
| 86Box on Linux or macOS | Untested | The loader names their Vulkan libraries; nobody has built or run it there |
| Two VGPU98 cards in one machine | Untested | |

## BIOS and boot

| Feature | Status | Notes |
|---|---|---|
| VGA BIOS with the project's own fonts | Supported | 8x8, 8x14, 8x16 |
| Standard VGA text and graphics modes | Supported | |
| VBE 2.0, with the protected-mode interface | Built | Checked on the host by biostest |
| VBE 3.0 refresh control | Missing | A VBE mode set uses the mode's first refresh rate |
| VBE/DDC with EDID 1.3 | Built | "VGPU98 Screen", 1024x768 at 75 Hz preferred |
| Full-screen DOS sessions under Windows | Built | |
| A short blink of the 350-line text mode at boot | Partial | Cause not yet found |

## Display and desktop

| Feature | Status | Notes |
|---|---|---|
| Desktop at 8, 16 and 32 bpp | Supported | |
| 16-colour 640x480 | Supported | Windows' own `vga.drv` |
| 24 bpp (packed) | Missing | |
| 14 desktop resolutions, 640x480 to 1920x1440 | Supported | |
| Widescreen resolutions | Missing | Defined in the mode table, switched off |
| Low resolutions (320x200 to 512x384) for Windows programs | Missing | Offered through VBE only, for DOS |
| Refresh rates 50 to 160 Hz | Supported | A list per mode |
| Double-scanned modes | Built | |
| Clean mode changes (no stretched frame, a black first frame) | Built | |
| Multiple monitors | Untested | |
| TV out, DVD decoding, motion compensation | Not planned | |

## Display controls

| Feature | Status | Notes |
|---|---|---|
| Hardware cursor | Supported | Up to 64x64, mono and colour; trails and odd shapes use the software cursor |
| Gamma ramps (SetDeviceGammaRamp, DirectDraw gamma) | Supported | Direct colour modes only |
| Gamma at 8 bpp | Missing | Refused: palette modes keep their palette |
| Desktop gamma, brightness, contrast (VGPU98 Color) | Supported | |
| V-sync per interface (VGPU98 3D) | Built | Direct3D, OpenGL, Glide |
| V-sync for windowed OpenGL | Not planned | GDI would wait behind it |
| A separate gamma for full-screen 3D; colour presets; an information tab | Missing | |

## 2D: GDI and DirectDraw

| Feature | Status | Notes |
|---|---|---|
| GDI drawing | Supported | In software, through the DIB engine |
| Primary, flips, off-screen surfaces in video memory | Supported | |
| Blits: copy, colour and depth fill, stretch, mirror, source and destination keys | Supported | |
| System memory to video memory blits | Supported | |
| Raster operations other than SRCCOPY | Missing | |
| Overlays and YUV surfaces | Missing | |
| Flip intervals, no-wait flips, scan line | Supported | |
| Locks of surfaces the GPU drew, through main memory | Supported | Setting "Locks through main memory" |

## Direct3D, DirectX 6 and 7 interfaces

| Feature | Status | Notes |
|---|---|---|
| The DirectX 7 driver interface | Supported | A DirectX 6 runtime sees a rasterizer |
| DirectX 3 and 5 programs | Built | Execute buffers in the pool, translated by the runtime |
| Hardware T&L | Supported | 8 lights, 6 clip planes, 4 blend matrices, texture coordinate generation |
| 8 texture stages, 8 textures | Supported | Every operation |
| Texture formats | Supported | 16- and 32-bit, palettized, luminance, bump, DXT1-5 |
| Textures up to 2048x2048, cube maps, non-power-of-two (conditional) | Supported | |
| D16, D24X8, D24S8, W-buffer, stencil | Supported | |
| Filtering up to 16x anisotropic | Partial | MIPFILTER NONE magnifies at level 0, MAXMIPLEVEL without mipmaps and the host's depth-bias step are not exact |
| Fog: vertex, table, W, range | Supported | |
| Windowed rendering | Supported | |
| Antialiasing | Missing | |

## Direct3D 8

| Feature | Status | Notes |
|---|---|---|
| The DirectX 8 driver interface | Supported | Setting "DirectX 8 interface" |
| Vertex shaders 1.1 | Supported | 192 constants |
| Pixel shaders 1.0-1.4 | Supported | Setting "DirectX 8 shaders" |
| 16 streams; vertex and index buffers in the pool | Supported | Up to 1M vertices or indices a draw |
| Volume textures up to 256 | Built | |
| Point sprites, point sizes up to 256 | Built | |
| V16U16, G16R16, A2W10V10U10; lockable D16 | Built | |
| Tweening, the matrix palette | Built | |
| N-patches, RT patches | Missing | |
| The pure device | Missing | |
| Direct3D 8's cursor calls | Missing | |
| Multisampling | Missing | |
| Shader programs as SPIR-V of their own | Missing | They run in the general shaders |

## DirectX 9

| Feature | Status | Notes |
|---|---|---|
| DirectX 9 runtimes over the DirectX 8 interface | Supported | As on any DirectX 8 card |
| DirectX 9's driver interface, shaders 2.0 | Missing | |

## Glide

| Feature | Status | Notes |
|---|---|---|
| Glide 2 (`glide2x.dll`, 2.61) | Supported | 133 exports |
| Glide 3 (`glide3x.dll`, 3.10) | Built | 96 exports |
| A Voodoo3 with 16 MB and two TMUs | Supported | |
| 16-bit colour with dithering; the screen through DirectDraw | Supported | |
| Frame buffer locks (565, the 16-bit aux buffer) | Built | |
| Palettes | Supported | |
| NCC tables, the 6666 palette | Built | |
| Glide 3 extensions | Missing | |
| `guMovie*`, `_grGetSipValues` | Missing | Return 0, as a Voodoo3's Glide 2 does |
| A 3dfx PCI identity | Missing | For programs that look for a 3dfx card on the bus |
| DOS Glide | Not planned | |

## OpenGL

| Feature | Status | Notes |
|---|---|---|
| The OpenGL 1.1 ICD | Supported | |
| Command set 2.6's extensions | Supported | Multitexture, compiled vertex arrays, paletted textures, point parameters, swap control |
| Clamp-to-edge (2.7) | Built | |
| GL_CLAMP as period drivers, or exact | Supported | Setting "OpenGL GL_CLAMP" |
| Windowed OpenGL | Supported | |
| Full-screen swap interval | Supported | |
| Two texture units | Partial | More than two: Missing |
| OpenGL 1.2, 1.3 | Missing | |
| S3TC, anisotropic filtering, LOD bias, add and combine environments | Missing | |
| WGL_ARB_extensions_string, ARB pixel formats, pbuffers | Missing | |
| Dithering, GL_POLYGON_SMOOTH coverage | Missing | |

## The control program

| Feature | Status | Notes |
|---|---|---|
| VGPU98 Color | Supported | |
| VGPU98 3D | Built | |
| In Display Properties' Advanced dialog, and its own Control Panel icon | Supported | |
| Saved settings applied at start-up | Supported | |
| Windows 98 styling | Supported | MS Sans Serif 8, standard controls |

## Install and package

| Feature | Status | Notes |
|---|---|---|
| One package for every interface | Supported | Eight files |
| A CD image, and a folder 86Box can mount | Supported | |
| Have Disk install on Windows 98 SE | Supported | |
| Uninstall | Missing | No uninstall step; the control program's settings key and Run entry stay |
| A bundled DirectX runtime | Not planned | Install DirectX 8 or 9 separately |

## Operating systems and processors

| Feature | Status | Notes |
|---|---|---|
| Windows 98 SE | Supported | The target |
| Windows 98 first edition, Windows ME | Untested | |
| Windows 95 OSR2 and OSR2.5 | Missing | |
| Windows NT 4, 2000, XP | Not planned | A different driver model |
| DOS programs on the BIOS | Supported (VGA) | VBE is checked on the host |
| Pentium Pro, Pentium II class guest processors | Supported | |
| Socket 7 Pentiums and K6s | Missing | The DLLs use CMOV and FCOMI |

## Programs seen running

| Program | Interface | Notes |
|---|---|---|
| 3DMark99 | DirectX 6 | Some texture stretching, cause not found |
| 3DMark2000 | DirectX 7 | Runs to its score |
| Giants | DirectX 7 | Distant terrain textures stretch with the camera angle |
| Flight Simulator 2002 | DirectX 7 | Runs |
| 3DMark2001 SE | DirectX 8 | Runs to its score |
| GTA3 | DirectX 8 | Runs; a garbled first frame is unexplained |
| Morrowind | DirectX 8 | Runs; one door texture was wrong |
| Tron 2.0 | DirectX 8/9 | Runs cleanly |
| 3DMark03 | DirectX 9.0c runtime | Completes; game test 4 skipped (it needs pixel shaders 2.0); CPU test 2 is slow by design |
| Unreal Gold | Glide 2 | Draws, with a clean log; a crash after a focus loss is unexplained |
| Quake 2 | OpenGL | Runs, with gamma |
| NFS3 | Software only | Its 3D setup wants a 3dfx card on the PCI bus |

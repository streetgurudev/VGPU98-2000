# DirectX 7 and 8 capability profile

**Covers:** the DirectX 6.1 profile, the DirectX 7 profile, and DirectX 8's in fixed function ([below](#the-directx-8-profile-d3dcaps8-and-the-format-list)).

What `vgpu98dx.dll` tells DirectDraw and Direct3D the card can do. The rule: **everything listed here is implemented as Direct3D 7 specifies it, and nothing else is advertised.** "As specified" means exactly, except where Direct3D itself leaves results to the hardware (mip level choice, anisotropic footprint, subpixel precision, the dither pattern): there the host GPU decides, within its precision ([gpu-commands.md](gpu-commands.md), Tolerance). A program then takes the paths the card really supports and never a path it does not, which is what keeps the card invisible. Caps are named by their Direct3D and DirectDraw constants; their values are restated in the DLL's headers (`ddi.h`, `ddi7.h`).

The profile is a DirectX 7 class card with hardware transform and lighting, and the features of the era's best: 32-bit colour, stencil, eight texture stages, cube maps, DXT compression, bump mapping. One set of caps serves both runtimes: the stock DirectX 6.1 runtime does not know the DirectX 7 bits and sees a rasterizer (it transforms and lights in software and keeps the DirectX 6 interface); a DirectX 7 or later runtime finds DRAWPRIMITIVES2EX and HWTRANSFORMANDLIGHT and offers the T&L HAL device ([directx-hal.md](directx-hal.md), The DirectX 7 interface).

These caps apply when PROFILE has all of COMMANDS, BLIT, RENDER3D and VERTEX ([gpu-registers.md](gpu-registers.md)). With BLIT only, the Direct3D parts are absent; with nothing, the driver reports no acceleration. EXECUTEBUFFER needs the pool (PROFILE.GPUVM and the mini-VDD's page table).

## DirectDraw (DDCORECAPS)

| Field | Caps |
|---|---|
| dwCaps | DDCAPS_BLT, BLTCOLORFILL, BLTDEPTHFILL, BLTSTRETCH, COLORKEY, 3D, GDI, READSCANLINE, CANBLTSYSMEM (palettes are DirectDraw's own, through GDI) |
| dwCaps2 | DDCAPS2_WIDESURFACES, CANRENDERWINDOWED, FLIPNOVSYNC, FLIPINTERVAL, NOPAGELOCKREQUIRED |
| dwCKeyCaps | DDCKEYCAPS_SRCBLT, DESTBLT (single key values; no colour-space ranges) |
| dwFXCaps | DDFXCAPS_BLTSTRETCHX, STRETCHXN, STRETCHY, STRETCHYN, SHRINKX, SHRINKXN, SHRINKY, SHRINKYN, MIRRORLEFTRIGHT, MIRRORUPDOWN |
| dwRops | SRCCOPY only |
| dwSVBCaps, dwSVBRops | DDCAPS_BLT; SRCCOPY only. System-to-video copies of the same size go through the ring, DXT decompressed when the destination is a colour format; no SVB colour key or effect caps, so DirectDraw's emulation does keyed and stretched ones ([directx-hal.md](directx-hal.md), System-to-video blits) |
| ddsCaps | DDSCAPS_FLIP, PRIMARYSURFACE, OFFSCREENPLAIN, TEXTURE, 3DDEVICE, ZBUFFER, MIPMAP, VIDEOMEMORY, LOCALVIDMEM; EXECUTEBUFFER with the pool (the driver makes Direct3D's vertex and command buffers: [directx-hal.md](directx-hal.md)) |
| More surface caps (GUID_DDMoreSurfaceCaps) | DDSCAPS2_CUBEMAP in video memory; the heap adds no restriction |
| dwZBufferBitDepths | DDBD_16, DDBD_32 (the bit depth counts stencil: D24X8 and D24S8 are 32-bit Z buffers, as the reference rasterizer reports) |
| dwVidMemTotal, dwVidMemFree | filled by DirectDraw from the heap |
| dwNumFourCCCodes | 0 (DXT1-DXT5 are listed only as texture formats, as the DXTn documentation asks) |
| Overlays, video ports, alpha blits, video-to-system blits, FourCC copies (DDCAPS2_COPYFOURCC) | none |

## Direct3D device (D3DDEVICEDESC_V1 in the global driver data)

| Field | Value |
|---|---|
| dwFlags | every D3DDD_ flag for the fields below |
| dcmColorModel | D3DCOLOR_RGB |
| dwDevCaps | D3DDEVCAPS_FLOATTLVERTEX, EXECUTESYSTEMMEMORY, TLVERTEXSYSTEMMEMORY, TEXTUREVIDEOMEMORY, DRAWPRIMTLVERTEX, CANRENDERAFTERFLIP, DRAWPRIMITIVES2, DRAWPRIMITIVES2EX, HWTRANSFORMANDLIGHT, HWRASTERIZATION |
| dtcTransformCaps | D3DTRANSFORMCAPS_CLIP: the GPU clips what it transforms, to the view volume and the user clip planes |
| bClipping | FALSE: pre-transformed vertices are the runtime's to clip, within the guard band |
| dlcLightingCaps | D3DLIGHTCAPS_POINT, SPOT, DIRECTIONAL; D3DLIGHTINGMODEL_RGB; 8 lights |
| dwDeviceRenderBitDepth | DDBD_16, DDBD_32 |
| dwDeviceZBufferBitDepth | DDBD_16, DDBD_32 |
| dwMaxBufferSize | 0 |
| dwMaxVertexCount | 65535 |

### Triangle and line caps (D3DPRIMCAPS, the same for both)

| Field | Caps |
|---|---|
| dwMiscCaps | D3DPMISCCAPS_MASKZ, CULLNONE, CULLCW, CULLCCW |
| dwRasterCaps | D3DPRASTERCAPS_DITHER, ZTEST, SUBPIXEL, FOGVERTEX, FOGTABLE, FOGRANGE, MIPMAPLODBIAS, ZBIAS, ANISOTROPY, WBUFFER, WFOG |
| dwZCmpCaps, dwAlphaCmpCaps | all eight comparisons |
| dwSrcBlendCaps, dwDestBlendCaps | all thirteen blend factors |
| dwShadeCaps | COLORFLATRGB, COLORGOURAUDRGB, SPECULARFLATRGB, SPECULARGOURAUDRGB, ALPHAFLATBLEND, ALPHAGOURAUDBLEND, FOGFLAT, FOGGOURAUD |
| dwTextureCaps | D3DPTEXTURECAPS_PERSPECTIVE, POW2, NONPOW2CONDITIONAL, ALPHA, TRANSPARENCY, BORDER, ALPHAPALETTE, PROJECTED, CUBEMAP, COLORKEYBLEND |
| dwTextureFilterCaps | NEAREST, LINEAR, MIPNEAREST, MIPLINEAR, LINEARMIPNEAREST, LINEARMIPLINEAR, MINFPOINT, MINFLINEAR, MINFANISOTROPIC, MIPFPOINT, MIPFLINEAR, MAGFPOINT, MAGFLINEAR, MAGFANISOTROPIC |
| dwTextureBlendCaps | the legacy blend modes: DECAL, MODULATE, DECALALPHA, MODULATEALPHA, COPY, ADD |
| dwTextureAddressCaps | WRAP, MIRROR, CLAMP, BORDER, INDEPENDENTUV |
| dwStippleWidth, dwStippleHeight | 0 |

Not advertised, so never relied on: edge and scene antialiasing, stipple, line patterns, ROP2 and XOR, phong and mono shading, cubic magnification filters, unconditional non-power-of-two textures, volume textures, sort-independent translucency, PARALLELPOINT lights, the EXTENTS clip status.

## Extended caps (GUID_D3DExtendedCaps)

| Field | Value |
|---|---|
| dwMinTextureWidth, dwMinTextureHeight | 1 |
| dwMaxTextureWidth, dwMaxTextureHeight | 2048 |
| dwMinStippleWidth ... dwMaxStippleHeight | 0 |
| dwMaxTextureRepeat | 8192 |
| dwMaxTextureAspectRatio | 2048 |
| dwMaxAnisotropy | 16. The anisotropic footprint is the host GPU's (Tolerance); a host without anisotropic filtering filters linearly |
| dvGuardBandLeft, Top, Right, Bottom | −32768, −32768, 32767, 32767 |
| dvExtentsAdjust | 0 |
| dwStencilCaps | all eight operations |
| dwFVFCaps | 8 texture coordinate sets |
| dwTextureOpCaps | all 24 operations (DISABLE through DOTPRODUCT3) |
| wMaxTextureBlendStages, wMaxSimultaneousTextures | 8, 8 |
| dwMaxActiveLights | 8 |
| dvMaxVertexW | 1e10 |
| wMaxUserClipPlanes | 6 |
| wMaxVertexBlendMatrices | 4 (one to three weights) |
| dwVertexProcessingCaps | D3DVTXPCAPS_TEXGEN, MATERIALSOURCE7, VERTEXFOG, DIRECTIONALLIGHTS, POSITIONALLIGHTS, LOCALVIEWER |

The DirectX 6.1 runtime asks for the first 84 bytes only; the DirectX 7 fields after them make 116.

## Formats

**Texture formats** (the global driver data's list, one DDSURFACEDESC each, with DDSD_PIXELFORMAT and DDSCAPS_TEXTURE): R5G6B5, X1R5G5B5, A1R5G5B5, A4R4G4B4, X8R8G8B8, A8R8G8B8, P8, L8, A8L8, V8U8, L6V5U5, X8L8V8U8, DXT1-DXT5 ([gpu-formats.md](gpu-formats.md)). Any of them can be a cube map (square faces, all six present, as many levels on each).

**Render targets**: R5G6B5, X1R5G5B5, X8R8G8B8, A8R8G8B8. Direct3D renders in 16 and 32 bpp modes; at 8 bpp the runtime's software devices are used, as on any card of the period without 8-bit 3D.

**Z buffers** (GUID_ZPixelFormats): D16; D24X8 (24-bit Z in a 32-bit pixel); D24S8 (24-bit Z, 8-bit stencil). Their DDPIXELFORMATs give the total bit depth, 16, 32 and 32 ([gpu-formats.md](gpu-formats.md)).

**Vertex formats** (the T&L device): pre-transformed (XYZRHW) or untransformed (XYZ, XYZB1-XYZB5), with a normal, diffuse and specular colours and up to eight coordinate sets of one to four floats each ([directx-hal.md](directx-hal.md)).

## The DirectX 8 profile (D3DCAPS8 and the format list)

With the GPU's PROFILE.FIXED8 the DLL answers DirectX 8's GetDriverInfo2 ([directx-hal.md](directx-hal.md), The DirectX 8 interface): a DirectX 8 card in fixed function, GeForce3-shaped where DirectX 8 leaves the numbers to the card, everything listed implemented as Direct3D 8.1 specifies it. The DirectX 7 profile above stays what older runtimes and interfaces see; the DirectX 8 runtime takes D3DCAPS8 and the format list instead, for DirectX 8 programs.

| Field | Value |
|---|---|
| DeviceType, AdapterOrdinal, Caps3, PresentationIntervals | 0 (the runtime's to fill) |
| Caps | D3DCAPS_READ_SCANLINE |
| Caps2 | D3DCAPS2_CANRENDERWINDOWED |
| CursorCaps | 0 |
| DevCaps | DirectX 7's dwDevCaps but FLOATTLVERTEX (not a DirectX 8 bit); with the pool HWVERTEXBUFFER and HWINDEXBUFFER. No PUREDEVICE, patches or SUBVOLUMELOCK |
| PrimitiveMiscCaps | MASKZ, CULLNONE, CULLCW, CULLCCW, COLORWRITEENABLE, TSSARGTEMP, BLENDOP. Not CLIPPLANESCALEDPOINTS (points are clipped by their centre) or CLIPTLVERTS |
| RasterCaps | DirectX 7's but SUBPIXEL (gone from DirectX 8): DITHER, ZTEST, FOGVERTEX, FOGTABLE, MIPMAPLODBIAS, ZBIAS, ANISOTROPY, WBUFFER, WFOG, FOGRANGE |
| ZCmpCaps, AlphaCmpCaps, SrcBlendCaps, DestBlendCaps | as DirectX 7's: all |
| ShadeCaps | COLORGOURAUDRGB, SPECULARGOURAUDRGB, ALPHAGOURAUDBLEND, FOGGOURAUD |
| TextureCaps | PERSPECTIVE, POW2, ALPHA, ALPHAPALETTE, NONPOW2CONDITIONAL, PROJECTED, CUBEMAP, VOLUMEMAP, MIPMAP, MIPVOLUMEMAP, MIPCUBEMAP, CUBEMAP_POW2, VOLUMEMAP_POW2 |
| TextureFilterCaps, CubeTextureFilterCaps | MINFPOINT, MINFLINEAR, MINFANISOTROPIC, MIPFPOINT, MIPFLINEAR, MAGFPOINT, MAGFLINEAR, MAGFANISOTROPIC |
| VolumeTextureFilterCaps | MINFPOINT, MINFLINEAR, MIPFPOINT, MIPFLINEAR, MAGFPOINT, MAGFLINEAR |
| TextureAddressCaps, VolumeTextureAddressCaps | WRAP, MIRROR, CLAMP, BORDER, INDEPENDENTUV (no MIRRORONCE) |
| LineCaps | TEXTURE, ZTEST, BLEND, ALPHACMP, FOG |
| MaxTextureWidth, MaxTextureHeight | 2048 |
| MaxVolumeExtent | 256 |
| MaxTextureRepeat, MaxTextureAspectRatio, MaxAnisotropy, MaxVertexW, guard band, ExtentsAdjust, StencilCaps | as DirectX 7's |
| FVFCaps | 8 coordinate sets, PSIZE |
| TextureOpCaps | all 26 operations (DISABLE through LERP) |
| MaxTextureBlendStages, MaxSimultaneousTextures | 8, 8 |
| VertexProcessingCaps | TEXGEN, MATERIALSOURCE7, DIRECTIONALLIGHTS, POSITIONALLIGHTS, LOCALVIEWER, TWEENING (UBYTE4 is taken: no NO_VSDT_UBYTE4) |
| MaxActiveLights, MaxUserClipPlanes, MaxVertexBlendMatrices | 8, 6, 4 |
| MaxVertexBlendMatrixIndex | 255: a palette of 256 world matrices (normals blended through every one's inverse transpose) |
| MaxPointSize | 256.0 (point sprites) |
| MaxPrimitiveCount | 349525 (55555h): a triangle list of as many fits one DRAW |
| MaxVertexIndex | FFFFFh (32-bit indices) |
| MaxStreams, MaxStreamStride | 16, 256 |
| VertexShaderVersion, MaxVertexShaderConst | With PROFILE.PROGRAMS: D3DVS_VERSION(1,1) (FFFE0101h) and 192 constants, a Radeon 8500's. Without: D3DVS_VERSION(0,0) (FFFE0000h, as the DDK's sample reports no shaders) and 0 |
| PixelShaderVersion, MaxPixelShaderValue | With PROFILE.PROGRAMS: D3DPS_VERSION(1,4) (FFFF0104h) and 8.0, a Radeon 8500's (texcrd's range needs 8). Without: D3DPS_VERSION(0,0) (FFFF0000h) and 0 |

**The format list** (DDPF_D3DFORMAT entries; "texture" is a 2D texture and a cube map):

| Format | Operations |
|---|---|
| X8R8G8B8, R5G6B5 | texture, volume, render target in any mode, render target of the same format, display mode with 3D (one 16-bit display format: R5G6B5) |
| A8R8G8B8 | texture, volume, render target in any mode, render target over an X8R8G8B8 mode |
| X1R5G5B5 | texture, volume, render target in any mode, of the same format |
| A1R5G5B5 | texture, volume, render target in any mode |
| A4R4G4B4, X4R4G4B4, A8, L8, A8L8, A4L4, V8U8, L6V5U5, X8L8V8U8, Q8W8V8U8 | texture, volume |
| V16U16, G16R16, A2W10V10U10 (command set 2.2) | texture, volume |
| P8, DXT1-DXT5 | texture |
| D16_LOCKABLE, D24X8, D24S8 | depth and stencil, with any colour depth |

Not offered: multisampling (every format's sample words 0), D32, D15S1, D24X4S4, A2B10G10R10, W11V11U10, the YUV formats, A8P8, R3G3B2, A8R3G3B2, R8G8B8.

## Reconciling

Every advertised cap is meant to produce Direct3D 7's results (the DirectX 7 SDK's samples, each against Microsoft's reference device); anything that cannot be implemented exactly is left out of the profile, not approximated. The profile is the single place to change what the card claims.

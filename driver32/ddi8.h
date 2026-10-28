/*
 * What the DirectX 8 driver interface adds to ddi.h and ddi7.h, restated
 * from the DirectX 8.1 DDK (the Windows XP DDK's headers and documentation)
 * and the DirectX 8.1 SDK's headers, read for facts only: GetDriverInfo2 and
 * its answers (D3DCAPS8, the format list), DrawPrimitives2's DirectX 8
 * commands, vertex shader declarations, buffers and volumes as surfaces
 * (spec/directx-hal.md). For the DLL only.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_DDI8_H
#define VGPU98_DDI8_H

#include "ddi7.h"

/* --- GetDriverInfo2 ------------------------------------------------------- */

/* DDHALINFO.dwFlags: the driver answers GetDriverInfo2, so the runtime treats
   it as a DirectX 8 driver. */
#define DDHALINFO_GETDRIVERINFO2 0x00000008

/* GetDriverInfo2 comes through GetDriverInfo with the stereo-mode GUID, its
   data told apart by the dword at offset 4: the magic for GetDriverInfo2,
   a mode's height for a stereo query. */
#define D3DGDI2_MAGIC 0xFFFFFFFFu

/* The header of every request; the answer goes into the same buffer. The
   size to honour is dwExpectedSize here, not GetDriverInfo's own. */
typedef struct DD_GETDRIVERINFO2DATA {
    DWORD dwReserved; /* neither read nor written */
    DWORD dwMagic;
    DWORD dwType;
    DWORD dwExpectedSize;
} DD_GETDRIVERINFO2DATA;
DDI_SIZE(DD_GETDRIVERINFO2DATA, 16);

#define D3DGDI2_TYPE_GETD3DCAPS8     1
#define D3DGDI2_TYPE_GETFORMATCOUNT  2
#define D3DGDI2_TYPE_GETFORMAT       3
#define D3DGDI2_TYPE_DXVERSION       4

typedef struct DD_GETFORMATCOUNTDATA {
    DD_GETDRIVERINFO2DATA gdi2;
    DWORD                 dwFormatCount;
    DWORD                 dwReserved;
} DD_GETFORMATCOUNTDATA;
DDI_SIZE(DD_GETFORMATCOUNTDATA, 24);

typedef struct DD_GETFORMATDATA {
    DD_GETDRIVERINFO2DATA gdi2;
    DWORD                 dwFormatIndex;
    DDPIXELFORMAT         format; /* dwFlags DDPF_D3DFORMAT, dwFourCC the D3DFORMAT, dwRBitMask its operations */
} DD_GETFORMATDATA;
DDI_SIZE(DD_GETFORMATDATA, 52);

typedef struct DD_DXVERSION {
    DD_GETDRIVERINFO2DATA gdi2;
    DWORD                 dwDXVersion; /* the application runtime's DD_RUNTIME_VERSION */
    DWORD                 dwReserved;
} DD_DXVERSION;
DDI_SIZE(DD_DXVERSION, 24);

/* A DirectX 8 format-list entry's DDPIXELFORMAT. */
#define DDPF_D3DFORMAT 0x00200000

/* What a format may be (the entry's dwRBitMask). */
#define D3DFORMAT_OP_TEXTURE                          0x00000001
#define D3DFORMAT_OP_VOLUMETEXTURE                    0x00000002
#define D3DFORMAT_OP_CUBETEXTURE                      0x00000004
#define D3DFORMAT_OP_OFFSCREEN_RENDERTARGET           0x00000008
#define D3DFORMAT_OP_SAME_FORMAT_RENDERTARGET         0x00000010
#define D3DFORMAT_OP_ZSTENCIL                         0x00000040
#define D3DFORMAT_OP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH 0x00000080
#define D3DFORMAT_OP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET 0x00000100
#define D3DFORMAT_OP_DISPLAYMODE                      0x00000400
#define D3DFORMAT_OP_3DACCELERATION                   0x00000800

/* D3DFORMAT codes (DXTn are their FourCCs) */
#define D3DFMT_A8R8G8B8     21
#define D3DFMT_X8R8G8B8     22
#define D3DFMT_R5G6B5       23
#define D3DFMT_X1R5G5B5     24
#define D3DFMT_A1R5G5B5     25
#define D3DFMT_A4R4G4B4     26
#define D3DFMT_A8           28
#define D3DFMT_X4R4G4B4     30
#define D3DFMT_P8           41
#define D3DFMT_L8           50
#define D3DFMT_A8L8         51
#define D3DFMT_A4L4         52
#define D3DFMT_V8U8         60
#define D3DFMT_L6V5U5       61
#define D3DFMT_X8L8V8U8     62
#define D3DFMT_Q8W8V8U8     63
#define D3DFMT_V16U16       64
#define D3DFMT_A2W10V10U10  67
#define D3DFMT_G16R16       34
#define D3DFMT_D16_LOCKABLE 70
#define D3DFMT_D24S8        75 /* Z in bits 31:8, stencil in 7:0 */
#define D3DFMT_D24X8        77

/* --- D3DCAPS8 ------------------------------------------------------------- */

typedef struct D3DCAPS8 {
    DWORD    DeviceType; /* the runtime's */
    DWORD    AdapterOrdinal;
    DWORD    Caps;
    DWORD    Caps2;
    DWORD    Caps3;
    DWORD    PresentationIntervals;
    DWORD    CursorCaps;
    DWORD    DevCaps;
    DWORD    PrimitiveMiscCaps;
    DWORD    RasterCaps;
    DWORD    ZCmpCaps;
    DWORD    SrcBlendCaps;
    DWORD    DestBlendCaps;
    DWORD    AlphaCmpCaps;
    DWORD    ShadeCaps;
    DWORD    TextureCaps;
    DWORD    TextureFilterCaps;
    DWORD    CubeTextureFilterCaps;
    DWORD    VolumeTextureFilterCaps;
    DWORD    TextureAddressCaps;
    DWORD    VolumeTextureAddressCaps;
    DWORD    LineCaps;
    DWORD    MaxTextureWidth;
    DWORD    MaxTextureHeight;
    DWORD    MaxVolumeExtent;
    DWORD    MaxTextureRepeat;
    DWORD    MaxTextureAspectRatio;
    DWORD    MaxAnisotropy;
    D3DVALUE MaxVertexW;
    D3DVALUE GuardBandLeft;
    D3DVALUE GuardBandTop;
    D3DVALUE GuardBandRight;
    D3DVALUE GuardBandBottom;
    D3DVALUE ExtentsAdjust;
    DWORD    StencilCaps;
    DWORD    FVFCaps;
    DWORD    TextureOpCaps;
    DWORD    MaxTextureBlendStages;
    DWORD    MaxSimultaneousTextures;
    DWORD    VertexProcessingCaps;
    DWORD    MaxActiveLights;
    DWORD    MaxUserClipPlanes;
    DWORD    MaxVertexBlendMatrices;
    DWORD    MaxVertexBlendMatrixIndex;
    D3DVALUE MaxPointSize;
    DWORD    MaxPrimitiveCount;
    DWORD    MaxVertexIndex;
    DWORD    MaxStreams;
    DWORD    MaxStreamStride;
    DWORD    VertexShaderVersion;
    DWORD    MaxVertexShaderConst;
    DWORD    PixelShaderVersion;
    D3DVALUE MaxPixelShaderValue;
} D3DCAPS8;
DDI_SIZE(D3DCAPS8, 212);
DDI_CHECK(D3DCAPS8, DevCaps, 0x1C);
DDI_CHECK(D3DCAPS8, LineCaps, 0x54);
DDI_CHECK(D3DCAPS8, MaxVertexW, 0x70);
DDI_CHECK(D3DCAPS8, MaxTextureBlendStages, 0x94);
DDI_CHECK(D3DCAPS8, MaxPointSize, 0xB0);
DDI_CHECK(D3DCAPS8, VertexShaderVersion, 0xC4);

#define D3DCAPS_READ_SCANLINE       0x00020000
#define D3DCAPS2_CANRENDERWINDOWED  0x00080000

/* DevCaps beyond DirectX 7's: the driver makes vertex and index buffers
   (masked before the application sees them) */
#define D3DDEVCAPS_HWVERTEXBUFFER   0x02000000
#define D3DDEVCAPS_HWINDEXBUFFER    0x04000000

#define D3DPMISCCAPS_COLORWRITEENABLE 0x00000080
#define D3DPMISCCAPS_TSSARGTEMP       0x00000400
#define D3DPMISCCAPS_BLENDOP          0x00000800

#define D3DLINECAPS_TEXTURE  0x00000001
#define D3DLINECAPS_ZTEST    0x00000002
#define D3DLINECAPS_BLEND    0x00000004
#define D3DLINECAPS_ALPHACMP 0x00000008
#define D3DLINECAPS_FOG      0x00000010

#define D3DPTEXTURECAPS_VOLUMEMAP      0x00002000
#define D3DPTEXTURECAPS_MIPMAP         0x00004000
#define D3DPTEXTURECAPS_MIPVOLUMEMAP   0x00008000
#define D3DPTEXTURECAPS_MIPCUBEMAP     0x00010000
#define D3DPTEXTURECAPS_CUBEMAP_POW2   0x00020000
#define D3DPTEXTURECAPS_VOLUMEMAP_POW2 0x00040000

#define D3DTEXOPCAPS_ALL8    0x03FFFFFF /* disable ... lerp */
#define D3DFVFCAPS_PSIZE     0x00100000
#define D3DVTXPCAPS_TWEENING 0x00000040

/* Shader versions (D3DCAPS8's VertexShaderVersion, PixelShaderVersion). */
#define D3DVS_VERSION_0_0 0xFFFE0000u
#define D3DVS_VERSION_1_1 0xFFFE0101u
#define D3DPS_VERSION_0_0 0xFFFF0000u
#define D3DPS_VERSION_1_3 0xFFFF0103u
#define D3DPS_VERSION_1_4 0xFFFF0104u

/* --- Formats at surface creation ---------------------------------------- */

#define DDSCAPS2_VOLUME      0x00200000 /* a volume texture: depth in ddsCapsEx.dwCaps4's low word */
#define DDSCAPS2_TEXTUREMANAGE    0x00000010 /* managed by DirectDraw (the trace's "managed") */
#define DDSCAPS2_D3DTEXTUREMANAGE 0x00020000 /* managed by Direct3D's runtime */
#define DDSCAPS2_INDEXBUFFER 0x04000000 /* an execute buffer that is an index buffer */

/* --- Contexts ------------------------------------------------------------- */

/* ContextCreate's dwhContext, on the way in: the interface that makes it. */
#define D3D_IFACE_DX7 3
#define D3D_IFACE_DX8 4

/* --- DrawPrimitives2's DirectX 8 commands --------------------------------- */

#define D3DDP2OP_CREATEVERTEXSHADER    45 /* dwHandle, dwDeclSize, dwCodeSize, then the declaration and the code */
#define D3DDP2OP_DELETEVERTEXSHADER    46 /* dwHandle */
#define D3DDP2OP_SETVERTEXSHADER       47 /* dwHandle: an FVF code (bit 0 clear) or a shader */
#define D3DDP2OP_SETVERTEXSHADERCONST  48 /* dwRegister, dwCount, then dwCount float4s */
#define D3DDP2OP_SETSTREAMSOURCE       49 /* dwStream, dwVBHandle, dwStride */
#define D3DDP2OP_SETSTREAMSOURCEUM     50 /* dwStream, dwStride: the call's user memory */
#define D3DDP2OP_SETINDICES            51 /* dwVBHandle, dwStride (2 or 4) */
#define D3DDP2OP_DRAWPRIMITIVE         52 /* primType, VStart, PrimitiveCount */
#define D3DDP2OP_DRAWINDEXEDPRIMITIVE  53 /* primType, BaseVertexIndex, MinIndex, NumVertices, StartIndex, PrimitiveCount */
#define D3DDP2OP_CREATEPIXELSHADER     54 /* dwHandle, dwCodeSize, then the code */
#define D3DDP2OP_DELETEPIXELSHADER     55 /* dwHandle */
#define D3DDP2OP_SETPIXELSHADER        56 /* dwHandle */
#define D3DDP2OP_SETPIXELSHADERCONST   57 /* dwRegister, dwCount, then dwCount float4s */
#define D3DDP2OP_CLIPPEDTRIANGLEFAN    58 /* FirstVertexOffset (bytes), dwEdgeFlags, PrimitiveCount */
#define D3DDP2OP_DRAWPRIMITIVE2        59 /* primType, FirstVertexOffset (bytes), PrimitiveCount */
#define D3DDP2OP_DRAWINDEXEDPRIMITIVE2 60 /* primType, BaseVertexOffset (bytes, signed), MinIndex, NumVertices, StartIndexOffset (bytes), PrimitiveCount */
#define D3DDP2OP_DRAWRECTPATCH         61 /* Handle, Flags, then optional segments and info */
#define D3DDP2OP_DRAWTRIPATCH          62 /* Handle, Flags, then optional segments and info */
#define D3DDP2OP_VOLUMEBLT             63 /* dest, source, DestX, DestY, DestZ, D3DBOX, dwFlags: 48 bytes */
#define D3DDP2OP_BUFFERBLT             64 /* dest, source, dwOffset, D3DRANGE (offset, size), dwFlags: 24 bytes */
#define D3DDP2OP_MULTIPLYTRANSFORM     65 /* xfrmType, D3DMATRIX */
#define D3DDP2OP_ADDDIRTYRECT          66 /* dwSurface, RECTL */
#define D3DDP2OP_ADDDIRTYBOX           67 /* dwSurface, D3DBOX */

/* D3DPRIMITIVETYPE: the GPU's primitive numbers are the same */
#define D3DPT_POINTLIST     1
#define D3DPT_TRIANGLEFAN   6

/* The patch commands' flags: what follows the record */
#define RTPATCHFLAG_HASSEGS 0x00000001
#define RTPATCHFLAG_HASINFO 0x00000002

#define D3DHAL_STATESETCREATE 5

/* Transform types the DirectX 8 interface numbers anew: world matrix n is
   256 + n. */
#define D3DTS_WORLDMATRIX(n) (256 + (n))

/* --- Vertex shader declarations (D3DVSD tokens) ----------------------------- */

#define D3DVSD_TOKENTYPE(t)   ((t) >> 29)
#define D3DVSD_TOKEN_NOP        0
#define D3DVSD_TOKEN_STREAM     1 /* bits 3:0 the stream; bit 28 the tessellator's */
#define D3DVSD_TOKEN_STREAMDATA 2 /* bit 28 clear: type in bits 19:16, register in 4:0; set: skip bits 19:16 dwords */
#define D3DVSD_TOKEN_TESSELLATOR 3
#define D3DVSD_TOKEN_CONSTMEM   4 /* count in bits 28:25, then 4 dwords each */
#define D3DVSD_TOKEN_EXT        5 /* count in bits 28:24, then that many dwords */
#define D3DVSD_TOKEN_END        7 /* the whole dword is FFFFFFFFh */
#define D3DVSD_END              0xFFFFFFFFu

/* The fixed-function input registers */
#define D3DVSDE_POSITION     0
#define D3DVSDE_BLENDWEIGHT  1
#define D3DVSDE_BLENDINDICES 2
#define D3DVSDE_NORMAL       3
#define D3DVSDE_PSIZE        4
#define D3DVSDE_DIFFUSE      5
#define D3DVSDE_SPECULAR     6
#define D3DVSDE_TEXCOORD0    7
#define D3DVSDE_TEXCOORD7    14
#define D3DVSDE_POSITION2    15
#define D3DVSDE_NORMAL2      16

/* Their data types: FLOAT1-4 0-3, D3DCOLOR 4, UBYTE4 5, SHORT2 6, SHORT4 7
   (the GPU's element types are one more). */
#define D3DVSDT_D3DCOLOR 4
#define D3DVSDT_UBYTE4   5
#define D3DVSDT_SHORT4   7

/* --- The FVF codes DirectX 8 adds -------------------------------------------- */

#define D3DFVF_PSIZE          0x00000020 /* DirectX 7's RESERVED1, in the same place */
#define D3DFVF_LASTBETA_UBYTE4 0x00001000 /* the last blending weight dword holds four matrix indices */

/* --- Texture stage states ---------------------------------------------------- */

/* The filters a DirectX 8 context sends (D3DTEXF): one numbering for MAG,
   MIN and MIP. */
#define D3DTEXF_NONE          0
#define D3DTEXF_POINT         1
#define D3DTEXF_LINEAR        2
#define D3DTEXF_ANISOTROPIC   3

#endif /* VGPU98_DDI8_H */

/*
 * What the DirectX 7 driver interface adds to the DirectDraw and Direct3D
 * interface of ddi.h, restated from the DirectX 7 DDK (its headers and
 * documentation, read for facts only): the Miscellaneous2 callbacks, surface
 * handles, DrawPrimitives2's DirectX 7 commands, the transform and lighting
 * device's caps (spec/directx-hal.md). For the DLL only.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_DDI7_H
#define VGPU98_DDI7_H

#include "ddi.h"

/* GetDriverInfo's GUID_Miscellaneous2Callbacks: a DirectX 7 runtime asks for
   them, and from then on names surfaces by the handles CreateSurfaceEx
   announces. */
typedef struct DDHAL_DDMISCELLANEOUS2CALLBACKS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD Reserved;
    DWORD CreateSurfaceEx;
    DWORD GetDriverState;
    DWORD DestroyDDLocal;
} DDHAL_DDMISCELLANEOUS2CALLBACKS;
DDI_SIZE(DDHAL_DDMISCELLANEOUS2CALLBACKS, 24);

#define DDHAL_MISC2CB32_CREATESURFACEEX 0x00000002
#define DDHAL_MISC2CB32_GETDRIVERSTATE  0x00000004
#define DDHAL_MISC2CB32_DESTROYDDLOCAL  0x00000008

typedef struct DDHAL_CREATESURFACEEXDATA {
    DWORD   dwFlags;
    void   *lpDDLcl;  /* the DirectDraw local object: the handles' scope */
    LPSURF  lpDDSLcl; /* the root of what was created */
    HRESULT ddRVal;
} DDHAL_CREATESURFACEEXDATA;

typedef struct DDHAL_GETDRIVERSTATEDATA {
    DWORD   dwFlags; /* D3DDEVINFOID_* */
    DWORD   dwhContext;
    DWORD  *lpdwStates;
    DWORD   dwLength;
    HRESULT ddRVal;
} DDHAL_GETDRIVERSTATEDATA;

typedef struct DDHAL_DESTROYDDLOCALDATA {
    DWORD   dwFlags;
    void   *pDDLcl;
    HRESULT ddRVal;
} DDHAL_DESTROYDDLOCALDATA;

/* GUID_DDMoreSurfaceCaps: the DDSCAPS2 surfaces video memory takes, and per
   heap the extra restrictions (one heap here). */
typedef struct DDMORESURFACECAPS {
    DWORD     dwSize;
    DDSCAPSEX ddsCapsMore;
    DDSCAPSEX ddsCapsEx;    /* the heap's restrictions */
    DDSCAPSEX ddsCapsExAlt;
} DDMORESURFACECAPS;
DDI_SIZE(DDMORESURFACECAPS, 40);

#define DDSCAPS2_CUBEMAP          0x00000200
#define DDSCAPS2_CUBEMAP_POSITIVEX 0x00000400 /* the faces, in Direct3D's order: +X -X +Y -Y +Z -Z */
#define DDSCAPS2_CUBEMAP_ALLFACES 0x0000FC00
#define DDSCAPS2_MIPMAPSUBLEVEL   0x00010000 /* every level of a mip chain but the top */

/* DrawPrimitives2's DirectX 7 commands. */
#define D3DDP2OP_SETPALETTE      30 /* dwPaletteHandle, dwPaletteFlags, dwSurfaceHandle */
#define D3DDP2OP_UPDATEPALETTE   31 /* dwPaletteHandle, wStartIndex, wNumEntries, then the entries (once) */
#define D3DDP2OP_ZRANGE          32 /* dvMinZ, dvMaxZ */
#define D3DDP2OP_SETMATERIAL     33 /* D3DMATERIAL7 */
#define D3DDP2OP_SETLIGHT        34 /* dwIndex, dwDataType (then a D3DLIGHT7 for DATA) */
#define D3DDP2OP_CREATELIGHT     35 /* dwIndex */
#define D3DDP2OP_SETTRANSFORM    36 /* xfrmType, D3DMATRIX */
#define D3DDP2OP_EXT             37 /* dwExtToken, dwSize */
#define D3DDP2OP_TEXBLT          38 /* dest handle, source handle, POINT, RECTL, dwFlags: 36 bytes */
#define D3DDP2OP_STATESET        39 /* dwOperation, dwParam, sbType */
#define D3DDP2OP_SETPRIORITY     40 /* dwDDSurface, dwPriority */
#define D3DDP2OP_SETRENDERTARGET 41 /* hRenderTarget, hZBuffer */
#define D3DDP2OP_CLEAR           42 /* dwFlags, dwFillColor, dvFillDepth, dwFillStencil, then the RECTs */
#define D3DDP2OP_SETTEXLOD       43 /* dwDDSurface, dwLOD */
#define D3DDP2OP_SETCLIPPLANE    44 /* dwIndex, plane[4] */

#define D3DHAL_SETLIGHT_ENABLE  0
#define D3DHAL_SETLIGHT_DISABLE 1
#define D3DHAL_SETLIGHT_DATA    2

#define D3DHAL_STATESETBEGIN   0
#define D3DHAL_STATESETEND     1
#define D3DHAL_STATESETDELETE  2
#define D3DHAL_STATESETEXECUTE 3
#define D3DHAL_STATESETCAPTURE 4

/* D3DTRANSFORMSTATETYPE */
#define D3DTRANSFORMSTATE_WORLD      1
#define D3DTRANSFORMSTATE_VIEW       2
#define D3DTRANSFORMSTATE_PROJECTION 3
#define D3DTRANSFORMSTATE_WORLD1     4
#define D3DTRANSFORMSTATE_WORLD2     5
#define D3DTRANSFORMSTATE_WORLD3     6
#define D3DTRANSFORMSTATE_TEXTURE0   16

#define D3DLIGHT7_DWORDS    26 /* type, diffuse, specular, ambient, position, direction, range, falloff, three attenuations, theta, phi */
#define D3DMATERIAL7_DWORDS 17 /* diffuse, ambient, specular, emissive, power */

#define D3DDEVCAPS_DRAWPRIMITIVES2EX   0x00008000
#define D3DDEVCAPS_HWTRANSFORMANDLIGHT 0x00010000
#define D3DDEVCAPS_HWRASTERIZATION     0x00080000

#define D3DTRANSFORMCAPS_CLIP    0x00000001
#define D3DLIGHTCAPS_POINT       0x00000001
#define D3DLIGHTCAPS_SPOT        0x00000002
#define D3DLIGHTCAPS_DIRECTIONAL 0x00000004
#define D3DLIGHTINGMODEL_RGB     0x00000001

#define D3DPTEXTURECAPS_NONPOW2CONDITIONAL 0x00000100
#define D3DPTEXTURECAPS_PROJECTED          0x00000400
#define D3DPTEXTURECAPS_CUBEMAP            0x00000800
#define D3DPTEXTURECAPS_COLORKEYBLEND      0x00001000
#define D3DPRASTERCAPS_FOGRANGE            0x00010000

#define D3DVTXPCAPS_TEXGEN            0x00000001
#define D3DVTXPCAPS_MATERIALSOURCE7   0x00000002
#define D3DVTXPCAPS_VERTEXFOG         0x00000004
#define D3DVTXPCAPS_DIRECTIONALLIGHTS 0x00000008
#define D3DVTXPCAPS_POSITIONALLIGHTS  0x00000010
#define D3DVTXPCAPS_LOCALVIEWER       0x00000020

#define DDCAPS_CANBLTSYSMEM 0x80000000

#define D3DFVF_XYZB1 0x00000006 /* XYZB1-5: the position, then 1-5 blending weights */
#define D3DFVF_XYZB5 0x0000000E

#endif /* VGPU98_DDI7_H */

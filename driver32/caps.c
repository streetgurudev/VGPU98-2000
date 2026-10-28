/*
 * vgpu98dx.dll: what the driver tells DirectDraw and Direct3D the card can
 * do (spec/d3d-caps.md). Everything listed is implemented as Direct3D 7
 * specifies it; nothing else is advertised. A DirectX 6 runtime does not
 * know the DirectX 7 bits and sees a rasterizer.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "dll.h"
#include "ddi7.h"

D3DHAL_GLOBALDRIVERDATA caps_global;
D3DHAL_D3DEXTENDEDCAPS  caps_extended;
DDSURFACEDESC           caps_texture_formats[17];
DWORD                   caps_num_texture_formats;
DWORD                   caps_zformats[1 + 3 * sizeof(DDPIXELFORMAT) / 4];

static void
pf_set(DDPIXELFORMAT *pf, DWORD flags, DWORD bits, DWORD m1, DWORD m2, DWORD m3, DWORD m4)
{
    memset(pf, 0, sizeof(*pf));
    pf->dwSize            = sizeof(DDPIXELFORMAT);
    pf->dwFlags           = flags;
    pf->dwRGBBitCount     = bits;
    pf->dwRBitMask        = m1;
    pf->dwGBitMask        = m2;
    pf->dwBBitMask        = m3;
    pf->dwRGBAlphaBitMask = m4;
}

static void
texture_format(DWORD flags, DWORD bits, DWORD m1, DWORD m2, DWORD m3, DWORD m4)
{
    DDSURFACEDESC *d = &caps_texture_formats[caps_num_texture_formats++];

    memset(d, 0, sizeof(*d));
    d->dwSize         = sizeof(DDSURFACEDESC);
    d->dwFlags        = DDSD_CAPS | DDSD_PIXELFORMAT;
    d->ddsCaps.dwCaps = DDSCAPS_TEXTURE;
    pf_set(&d->ddpfPixelFormat, flags, bits, m1, m2, m3, m4);
}

static void
texture_fourcc(DWORD fourcc)
{
    DDSURFACEDESC *d = &caps_texture_formats[caps_num_texture_formats++];

    memset(d, 0, sizeof(*d));
    d->dwSize                   = sizeof(DDSURFACEDESC);
    d->dwFlags                  = DDSD_CAPS | DDSD_PIXELFORMAT;
    d->ddsCaps.dwCaps           = DDSCAPS_TEXTURE;
    d->ddpfPixelFormat.dwSize   = sizeof(DDPIXELFORMAT);
    d->ddpfPixelFormat.dwFlags  = DDPF_FOURCC;
    d->ddpfPixelFormat.dwFourCC = fourcc;
}

static void
prim_caps(D3DPRIMCAPS *c)
{
    memset(c, 0, sizeof(*c));
    c->dwSize       = sizeof(D3DPRIMCAPS);
    c->dwMiscCaps   = D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW;
    c->dwRasterCaps = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_SUBPIXEL | D3DPRASTERCAPS_FOGVERTEX |
                      D3DPRASTERCAPS_FOGTABLE | D3DPRASTERCAPS_MIPMAPLODBIAS | D3DPRASTERCAPS_ZBIAS |
                      D3DPRASTERCAPS_ANISOTROPY | D3DPRASTERCAPS_WBUFFER | D3DPRASTERCAPS_WFOG |
                      D3DPRASTERCAPS_FOGRANGE;
    c->dwZCmpCaps      = D3DPCMPCAPS_ALL;
    c->dwSrcBlendCaps  = D3DPBLENDCAPS_ALL;
    c->dwDestBlendCaps = D3DPBLENDCAPS_ALL;
    c->dwAlphaCmpCaps  = D3DPCMPCAPS_ALL;
    c->dwShadeCaps     = D3DPSHADECAPS_COLORFLATRGB | D3DPSHADECAPS_COLORGOURAUDRGB | D3DPSHADECAPS_SPECULARFLATRGB |
                     D3DPSHADECAPS_SPECULARGOURAUDRGB | D3DPSHADECAPS_ALPHAFLATBLEND | D3DPSHADECAPS_ALPHAGOURAUDBLEND |
                     D3DPSHADECAPS_FOGFLAT | D3DPSHADECAPS_FOGGOURAUD;
    c->dwTextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_POW2 | D3DPTEXTURECAPS_ALPHA |
                       D3DPTEXTURECAPS_TRANSPARENCY | D3DPTEXTURECAPS_BORDER | D3DPTEXTURECAPS_ALPHAPALETTE |
                       D3DPTEXTURECAPS_NONPOW2CONDITIONAL | D3DPTEXTURECAPS_PROJECTED | D3DPTEXTURECAPS_CUBEMAP |
                       D3DPTEXTURECAPS_COLORKEYBLEND;
    c->dwTextureFilterCaps = D3DPTFILTERCAPS_NEAREST | D3DPTFILTERCAPS_LINEAR | D3DPTFILTERCAPS_MIPNEAREST |
                             D3DPTFILTERCAPS_MIPLINEAR | D3DPTFILTERCAPS_LINEARMIPNEAREST |
                             D3DPTFILTERCAPS_LINEARMIPLINEAR | D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR |
                             D3DPTFILTERCAPS_MINFANISOTROPIC | D3DPTFILTERCAPS_MIPFPOINT | D3DPTFILTERCAPS_MIPFLINEAR |
                             D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFANISOTROPIC;
    c->dwTextureBlendCaps = D3DPTBLENDCAPS_DECAL | D3DPTBLENDCAPS_MODULATE | D3DPTBLENDCAPS_DECALALPHA |
                            D3DPTBLENDCAPS_MODULATEALPHA | D3DPTBLENDCAPS_COPY | D3DPTBLENDCAPS_ADD;
    c->dwTextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_CLAMP |
                              D3DPTADDRESSCAPS_BORDER | D3DPTADDRESSCAPS_INDEPENDENTUV;
}

/* The DirectDraw core caps go straight into the driver description. */
static void
ddraw_caps(DWORD profile)
{
    DDCORECAPS *c = &sb->halinfo.ddCaps;

    memset(c, 0, sizeof(*c));
    c->dwSize = sizeof(DDCORECAPS);
    if (!(profile & VGPU98_PROFILE_BLIT))
        return; /* no acceleration: DirectDraw emulates everything */
    c->dwCaps = DDCAPS_BLT | DDCAPS_BLTCOLORFILL | DDCAPS_BLTDEPTHFILL | DDCAPS_BLTSTRETCH | DDCAPS_COLORKEY |
                DDCAPS_GDI | DDCAPS_READSCANLINE;
    c->dwCaps2 = DDCAPS2_WIDESURFACES | DDCAPS2_FLIPNOVSYNC | DDCAPS2_FLIPINTERVAL | DDCAPS2_NOPAGELOCKREQUIRED;
    c->dwCKeyCaps = DDCKEYCAPS_SRCBLT | DDCKEYCAPS_DESTBLT;
    c->dwFXCaps = DDFXCAPS_BLTSTRETCHX | DDFXCAPS_BLTSTRETCHXN | DDFXCAPS_BLTSTRETCHY | DDFXCAPS_BLTSTRETCHYN |
                  DDFXCAPS_BLTSHRINKX | DDFXCAPS_BLTSHRINKXN | DDFXCAPS_BLTSHRINKY | DDFXCAPS_BLTSHRINKYN |
                  DDFXCAPS_BLTMIRRORLEFTRIGHT | DDFXCAPS_BLTMIRRORUPDOWN;
    /* Raster operations: SRCCOPY (CCh) only. */
    c->dwRops[(SRCCOPY >> 16) / 32] = 1u << ((SRCCOPY >> 16) % 32);
    c->ddsCaps.dwCaps = DDSCAPS_FLIP | DDSCAPS_PRIMARYSURFACE | DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY |
                        DDSCAPS_LOCALVIDMEM;
    /* System to video memory: plain copies, through the ring (UPLOAD). */
    c->dwCaps |= DDCAPS_CANBLTSYSMEM;
    c->dwSVBCaps                       = DDCAPS_BLT;
    c->dwSVBRops[(SRCCOPY >> 16) / 32] = 1u << ((SRCCOPY >> 16) % 32);
    if (profile & VGPU98_PROFILE_RENDER3D) {
        c->dwCaps |= DDCAPS_3D;
        c->dwCaps2 |= DDCAPS2_CANRENDERWINDOWED;
        c->ddsCaps.dwCaps |= DDSCAPS_TEXTURE | DDSCAPS_3DDEVICE | DDSCAPS_ZBUFFER | DDSCAPS_MIPMAP;
        c->dwZBufferBitDepths = DDBD_16 | DDBD_32;
        /* The driver makes execute buffers (in the pool): Direct3D asks for
           its vertex and command buffers with this bit. */
        if (pool_on)
            c->ddsCaps.dwCaps |= DDSCAPS_EXECUTEBUFFER;
    }
}

void
caps_init(DWORD profile)
{
    D3DDEVICEDESC_V1 *d = &caps_global.hwCaps;
    DDPIXELFORMAT    *z;

    ddraw_caps(profile);

    /* Direct3D: the global driver data. */
    memset(&caps_global, 0, sizeof(caps_global));
    caps_global.dwSize = sizeof(caps_global);
    d->dwSize          = sizeof(D3DDEVICEDESC_V1);
    d->dwFlags = D3DDD_COLORMODEL | D3DDD_DEVCAPS | D3DDD_TRANSFORMCAPS | D3DDD_LIGHTINGCAPS | D3DDD_BCLIPPING |
                 D3DDD_LINECAPS | D3DDD_TRICAPS | D3DDD_DEVICERENDERBITDEPTH | D3DDD_DEVICEZBUFFERBITDEPTH |
                 D3DDD_MAXBUFFERSIZE | D3DDD_MAXVERTEXCOUNT;
    d->dcmColorModel = D3DCOLOR_RGB;
    d->dwDevCaps     = D3DDEVCAPS_FLOATTLVERTEX | D3DDEVCAPS_EXECUTESYSTEMMEMORY | D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
                   D3DDEVCAPS_TEXTUREVIDEOMEMORY | D3DDEVCAPS_DRAWPRIMTLVERTEX | D3DDEVCAPS_CANRENDERAFTERFLIP |
                   D3DDEVCAPS_DRAWPRIMITIVES2 | D3DDEVCAPS_DRAWPRIMITIVES2EX | D3DDEVCAPS_HWTRANSFORMANDLIGHT |
                   D3DDEVCAPS_HWRASTERIZATION;
    /* The transform clips (DirectX 7); transformed vertices are the
       runtime's to clip, as before. */
    d->dtcTransformCaps.dwSize       = sizeof(D3DTRANSFORMCAPS);
    d->dtcTransformCaps.dwCaps       = D3DTRANSFORMCAPS_CLIP;
    d->bClipping                     = 0;
    d->dlcLightingCaps.dwSize        = sizeof(D3DLIGHTINGCAPS);
    d->dlcLightingCaps.dwCaps        = D3DLIGHTCAPS_POINT | D3DLIGHTCAPS_SPOT | D3DLIGHTCAPS_DIRECTIONAL;
    d->dlcLightingCaps.dwLightingModel = D3DLIGHTINGMODEL_RGB;
    d->dlcLightingCaps.dwNumLights   = VGPU98_MAX_LIGHTS;
    prim_caps(&d->dpcLineCaps);
    prim_caps(&d->dpcTriCaps);
    d->dwDeviceRenderBitDepth  = DDBD_16 | DDBD_32;
    d->dwDeviceZBufferBitDepth = DDBD_16 | DDBD_32;
    d->dwMaxBufferSize         = 0;
    d->dwMaxVertexCount        = 65535;

    caps_num_texture_formats = 0;
    texture_format(DDPF_RGB, 16, 0xf800, 0x07e0, 0x001f, 0);
    texture_format(DDPF_RGB, 16, 0x7c00, 0x03e0, 0x001f, 0);
    texture_format(DDPF_RGB | DDPF_ALPHAPIXELS, 16, 0x7c00, 0x03e0, 0x001f, 0x8000);
    texture_format(DDPF_RGB | DDPF_ALPHAPIXELS, 16, 0x0f00, 0x00f0, 0x000f, 0xf000);
    texture_format(DDPF_RGB, 32, 0xff0000, 0xff00, 0xff, 0);
    texture_format(DDPF_RGB | DDPF_ALPHAPIXELS, 32, 0xff0000, 0xff00, 0xff, 0xff000000);
    texture_format(DDPF_RGB | DDPF_PALETTEINDEXED8, 8, 0, 0, 0, 0);
    texture_format(DDPF_LUMINANCE, 8, 0xff, 0, 0, 0);
    texture_format(DDPF_LUMINANCE | DDPF_ALPHAPIXELS, 16, 0xff, 0, 0, 0xff00);
    texture_format(DDPF_BUMPDUDV, 16, 0x00ff, 0xff00, 0, 0);
    texture_format(DDPF_BUMPDUDV | DDPF_BUMPLUMINANCE, 16, 0x001f, 0x03e0, 0xfc00, 0);
    texture_format(DDPF_BUMPDUDV | DDPF_BUMPLUMINANCE, 32, 0x0000ff, 0x00ff00, 0xff0000, 0);
    texture_fourcc(MAKEFOURCC('D', 'X', 'T', '1'));
    texture_fourcc(MAKEFOURCC('D', 'X', 'T', '2'));
    texture_fourcc(MAKEFOURCC('D', 'X', 'T', '3'));
    texture_fourcc(MAKEFOURCC('D', 'X', 'T', '4'));
    texture_fourcc(MAKEFOURCC('D', 'X', 'T', '5'));
    caps_global.dwNumVertices       = 0;
    caps_global.dwNumClipVertices   = 0;
    caps_global.dwNumTextureFormats = caps_num_texture_formats;
    caps_global.lpTextureFormats    = caps_texture_formats;

    /* The extended caps (GUID_D3DExtendedCaps). */
    memset(&caps_extended, 0, sizeof(caps_extended));
    caps_extended.dwSize                   = sizeof(caps_extended);
    caps_extended.dwMinTextureWidth        = 1;
    caps_extended.dwMaxTextureWidth        = 2048;
    caps_extended.dwMinTextureHeight       = 1;
    caps_extended.dwMaxTextureHeight       = 2048;
    caps_extended.dwMaxTextureRepeat       = 8192;
    caps_extended.dwMaxTextureAspectRatio  = 2048;
    caps_extended.dwMaxAnisotropy          = 16;
    caps_extended.dvGuardBandLeft          = -32768.0f;
    caps_extended.dvGuardBandTop           = -32768.0f;
    caps_extended.dvGuardBandRight         = 32767.0f;
    caps_extended.dvGuardBandBottom        = 32767.0f;
    caps_extended.dvExtentsAdjust          = 0.0f;
    caps_extended.dwStencilCaps            = D3DSTENCILCAPS_ALL;
    caps_extended.dwFVFCaps                = 8;
    caps_extended.dwTextureOpCaps          = D3DTEXOPCAPS_ALL;
    caps_extended.wMaxTextureBlendStages   = 8;
    caps_extended.wMaxSimultaneousTextures = 8;
    caps_extended.dvMaxVertexW             = 1e10f;
    /* DirectX 7's transform and lighting */
    caps_extended.dwMaxActiveLights       = VGPU98_MAX_LIGHTS;
    caps_extended.wMaxUserClipPlanes      = VGPU98_MAX_CLIPPLANES;
    caps_extended.wMaxVertexBlendMatrices = 4;
    caps_extended.dwVertexProcessingCaps  = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 | D3DVTXPCAPS_VERTEXFOG |
                                           D3DVTXPCAPS_DIRECTIONALLIGHTS | D3DVTXPCAPS_POSITIONALLIGHTS |
                                           D3DVTXPCAPS_LOCALVIEWER;

    /* The Z buffer formats (GUID_ZPixelFormats): a count, then D16, D24X8, D24S8. */
    memset(caps_zformats, 0, sizeof(caps_zformats));
    caps_zformats[0] = 3;
    z                = (DDPIXELFORMAT *) &caps_zformats[1];
    pf_set(&z[0], DDPF_ZBUFFER, 16, 0, 0xffff, 0, 0);
    pf_set(&z[1], DDPF_ZBUFFER, 32, 0, 0xffffff00, 0, 0);
    pf_set(&z[2], DDPF_ZBUFFER | DDPF_STENCILBUFFER, 32, 8, 0xffffff00, 0xff, 0);
}

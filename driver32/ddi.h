/*
 * The DirectDraw and Direct3D driver interface of Windows 98 SE and the
 * DirectX 6.1 runtime, restated from the Windows 98 SE and DirectX 7 DDKs and
 * the DirectX 6.1 SDK headers, used as references for facts only: names,
 * layouts, values (spec/directx-hal.md). Every structure's size and the
 * offsets the driver uses are checked at compile time.
 *
 * Two kinds of structure live here:
 * - the DirectDraw driver description the display driver hands to DirectDraw
 *   (DDHALINFO and what it points to). It is built in the shared block by both
 *   halves of the driver, so it uses fixed-width fields only (its pointers are
 *   DWORDs) and has the same layout on every compiler; tools/sbgen reads it on
 *   the host to give the 16-bit driver its offsets;
 * - everything the runtime passes the 32-bit DLL (surface objects, callback
 *   data), with real pointers: only for the DLL (VGPU98_DLL).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_DDI_H
#define VGPU98_DDI_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t  BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t  LONG;
typedef int32_t  BOOL;
typedef int32_t  HRESULT;
typedef float    D3DVALUE;
typedef uint32_t D3DCOLOR;
typedef uint32_t FLATPTR;

#define DDI_CHECK(type, field, offset) \
    _Static_assert(offsetof(type, field) == (offset), #type "." #field " is not at " #offset)
#define DDI_SIZE(type, size) _Static_assert(sizeof(type) == (size), #type " is not " #size " bytes")

/* --- Results ------------------------------------------------------------ */

#define DD_OK                         0
#define DDHAL_DRIVER_NOTHANDLED       0
#define DDHAL_DRIVER_HANDLED          1
#define DDERR_GENERIC                 ((HRESULT) 0x80004005)
#define DDERR_UNSUPPORTED             ((HRESULT) 0x80004001)
#define DDERR_INVALIDPARAMS           ((HRESULT) 0x80070057)
#define DDERR_OUTOFMEMORY             ((HRESULT) 0x8007000E)
#define DDERR_CURRENTLYNOTAVAIL       ((HRESULT) 0x88760028)
#define DDERR_INVALIDOBJECT           ((HRESULT) 0x88760082)
#define DDERR_INVALIDPIXELFORMAT      ((HRESULT) 0x88760091)
#define DDERR_INVALIDRECT             ((HRESULT) 0x88760096)
#define DDERR_OUTOFVIDEOMEMORY        ((HRESULT) 0x8876017C)
#define DDERR_SURFACELOST             ((HRESULT) 0x887601C2)
#define DDERR_VERTICALBLANKINPROGRESS ((HRESULT) 0x88760219)
#define DDERR_WASSTILLDRAWING         ((HRESULT) 0x8876021C)
#define DDERR_NOTFOUND                ((HRESULT) 0x887600FF)
#define D3D_OK                        0
#define D3DERR_WRONGTEXTUREFORMAT     ((HRESULT) 0x88760818)
#define D3DERR_UNSUPPORTEDCOLOROPERATION ((HRESULT) 0x88760819)
#define D3DERR_UNSUPPORTEDCOLORARG    ((HRESULT) 0x8876081A)
#define D3DERR_UNSUPPORTEDALPHAOPERATION ((HRESULT) 0x8876081B)
#define D3DERR_UNSUPPORTEDALPHAARG    ((HRESULT) 0x8876081C)
#define D3DERR_TOOMANYOPERATIONS      ((HRESULT) 0x8876081D)
#define D3DERR_COMMAND_UNPARSED       ((HRESULT) 0x88760BB8)
#define D3DERR_DRIVERINVALIDCALL      ((HRESULT) 0x8876086D) /* MAKE_D3DHRESULT(2157) */
#define D3DHAL_CONTEXT_BAD            ((HRESULT) 0x00000200)
#define D3DHAL_OUTOFCONTEXTS          ((HRESULT) 0x00000201)

/* --- DirectDraw basics -------------------------------------------------- */

typedef struct DDSCAPS {
    DWORD dwCaps;
} DDSCAPS;

#define DDSCAPS_ALPHA           0x00000002
#define DDSCAPS_BACKBUFFER      0x00000004
#define DDSCAPS_COMPLEX         0x00000008
#define DDSCAPS_FLIP            0x00000010
#define DDSCAPS_FRONTBUFFER     0x00000020
#define DDSCAPS_OFFSCREENPLAIN  0x00000040
#define DDSCAPS_OVERLAY         0x00000080
#define DDSCAPS_PALETTE         0x00000100
#define DDSCAPS_PRIMARYSURFACE  0x00000200
#define DDSCAPS_SYSTEMMEMORY    0x00000800
#define DDSCAPS_TEXTURE         0x00001000
#define DDSCAPS_3DDEVICE        0x00002000
#define DDSCAPS_VIDEOMEMORY     0x00004000
#define DDSCAPS_VISIBLE         0x00008000
#define DDSCAPS_WRITEONLY       0x00010000
#define DDSCAPS_ZBUFFER         0x00020000
#define DDSCAPS_OWNDC           0x00040000
#define DDSCAPS_LIVEVIDEO       0x00080000
#define DDSCAPS_HWCODEC         0x00100000
#define DDSCAPS_MODEX           0x00200000
#define DDSCAPS_MIPMAP          0x00400000
#define DDSCAPS_EXECUTEBUFFER   0x00800000
#define DDSCAPS_ALLOCONLOAD     0x04000000
#define DDSCAPS_VIDEOPORT       0x08000000
#define DDSCAPS_LOCALVIDMEM     0x10000000
#define DDSCAPS_NONLOCALVIDMEM  0x20000000
#define DDSCAPS_STANDARDVGAMODE 0x40000000
#define DDSCAPS_OPTIMIZED       0x80000000

typedef struct DDCOLORKEY {
    DWORD dwColorSpaceLowValue;
    DWORD dwColorSpaceHighValue;
} DDCOLORKEY;

/* DDPIXELFORMAT: the four masks are reused by Z, luminance and bump formats. */
typedef struct DDPIXELFORMAT {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    DWORD dwRGBBitCount;     /* also dwZBufferBitDepth, dwLuminanceBitCount, dwBumpBitCount */
    DWORD dwRBitMask;        /* also dwStencilBitDepth, dwLuminanceBitMask, dwBumpDuBitMask */
    DWORD dwGBitMask;        /* also dwZBitMask, dwBumpDvBitMask */
    DWORD dwBBitMask;        /* also dwStencilBitMask, dwBumpLuminanceBitMask */
    DWORD dwRGBAlphaBitMask; /* also dwLuminanceAlphaBitMask */
} DDPIXELFORMAT;
DDI_SIZE(DDPIXELFORMAT, 32);

#define DDPF_ALPHAPIXELS      0x00000001
#define DDPF_ALPHA            0x00000002
#define DDPF_FOURCC           0x00000004
#define DDPF_PALETTEINDEXED4  0x00000008
#define DDPF_PALETTEINDEXED8  0x00000020
#define DDPF_RGB              0x00000040
#define DDPF_ZBUFFER          0x00000400
#define DDPF_ZPIXELS          0x00002000
#define DDPF_STENCILBUFFER    0x00004000
#define DDPF_ALPHAPREMULT     0x00008000
#define DDPF_LUMINANCE        0x00020000
#define DDPF_BUMPLUMINANCE    0x00040000
#define DDPF_BUMPDUDV         0x00080000

#define MAKEFOURCC(a, b, c, d) ((DWORD) (BYTE) (a) | ((DWORD) (BYTE) (b) << 8) | ((DWORD) (BYTE) (c) << 16) | ((DWORD) (BYTE) (d) << 24))

typedef struct DDSURFACEDESC {
    DWORD         dwSize;
    DWORD         dwFlags;
    DWORD         dwHeight;
    DWORD         dwWidth;
    LONG          lPitch;          /* also dwLinearSize */
    DWORD         dwBackBufferCount;
    DWORD         dwMipMapCount;   /* also dwZBufferBitDepth, dwRefreshRate */
    DWORD         dwAlphaBitDepth;
    DWORD         dwReserved;
    DWORD         lpSurface;
    DDCOLORKEY    ddckCKDestOverlay;
    DDCOLORKEY    ddckCKDestBlt;
    DDCOLORKEY    ddckCKSrcOverlay;
    DDCOLORKEY    ddckCKSrcBlt;
    DDPIXELFORMAT ddpfPixelFormat;
    DDSCAPS       ddsCaps;
} DDSURFACEDESC;
DDI_SIZE(DDSURFACEDESC, 108);
DDI_CHECK(DDSURFACEDESC, ddpfPixelFormat, 72);
DDI_CHECK(DDSURFACEDESC, ddsCaps, 104);

#define DDSD_CAPS            0x00000001
#define DDSD_HEIGHT          0x00000002
#define DDSD_WIDTH           0x00000004
#define DDSD_PITCH           0x00000008
#define DDSD_ZBUFFERBITDEPTH 0x00000040
#define DDSD_PIXELFORMAT     0x00001000
#define DDSD_CKSRCBLT        0x00010000
#define DDSD_MIPMAPCOUNT     0x00020000
#define DDSD_LINEARSIZE      0x00080000

/* --- The driver description (fixed-width: shared with the 16-bit driver) - */

#define DD_ROP_SPACE 8

typedef struct DDCORECAPS {
    DWORD   dwSize;
    DWORD   dwCaps;
    DWORD   dwCaps2;
    DWORD   dwCKeyCaps;
    DWORD   dwFXCaps;
    DWORD   dwFXAlphaCaps;
    DWORD   dwPalCaps;
    DWORD   dwSVCaps;
    DWORD   dwAlphaBltConstBitDepths;
    DWORD   dwAlphaBltPixelBitDepths;
    DWORD   dwAlphaBltSurfaceBitDepths;
    DWORD   dwAlphaOverlayConstBitDepths;
    DWORD   dwAlphaOverlayPixelBitDepths;
    DWORD   dwAlphaOverlaySurfaceBitDepths;
    DWORD   dwZBufferBitDepths;
    DWORD   dwVidMemTotal;
    DWORD   dwVidMemFree;
    DWORD   dwMaxVisibleOverlays;
    DWORD   dwCurrVisibleOverlays;
    DWORD   dwNumFourCCCodes;
    DWORD   dwAlignBoundarySrc;
    DWORD   dwAlignSizeSrc;
    DWORD   dwAlignBoundaryDest;
    DWORD   dwAlignSizeDest;
    DWORD   dwAlignStrideAlign;
    DWORD   dwRops[DD_ROP_SPACE];
    DDSCAPS ddsCaps;
    DWORD   dwMinOverlayStretch;
    DWORD   dwMaxOverlayStretch;
    DWORD   dwMinLiveVideoStretch;
    DWORD   dwMaxLiveVideoStretch;
    DWORD   dwMinHwCodecStretch;
    DWORD   dwMaxHwCodecStretch;
    DWORD   dwReserved1;
    DWORD   dwReserved2;
    DWORD   dwReserved3;
    DWORD   dwSVBCaps;
    DWORD   dwSVBCKeyCaps;
    DWORD   dwSVBFXCaps;
    DWORD   dwSVBRops[DD_ROP_SPACE];
    DWORD   dwVSBCaps;
    DWORD   dwVSBCKeyCaps;
    DWORD   dwVSBFXCaps;
    DWORD   dwVSBRops[DD_ROP_SPACE];
    DWORD   dwSSBCaps;
    DWORD   dwSSBCKeyCaps;
    DWORD   dwSSBFXCaps;
    DWORD   dwSSBRops[DD_ROP_SPACE];
    DWORD   dwMaxVideoPorts;
    DWORD   dwCurrVideoPorts;
    DWORD   dwSVBCaps2;
} DDCORECAPS;
DDI_SIZE(DDCORECAPS, 316);
DDI_CHECK(DDCORECAPS, dwZBufferBitDepths, 56);
DDI_CHECK(DDCORECAPS, dwNumFourCCCodes, 76);
DDI_CHECK(DDCORECAPS, dwRops, 100);
DDI_CHECK(DDCORECAPS, ddsCaps, 132);
DDI_CHECK(DDCORECAPS, dwSVBCaps, 172);
DDI_CHECK(DDCORECAPS, dwVSBCaps, 216);
DDI_CHECK(DDCORECAPS, dwSSBCaps, 260);
DDI_CHECK(DDCORECAPS, dwMaxVideoPorts, 304);

#define DDCAPS_3D                  0x00000001
#define DDCAPS_BLT                 0x00000040
#define DDCAPS_BLTSTRETCH          0x00000200
#define DDCAPS_GDI                 0x00000400
#define DDCAPS_PALETTE             0x00008000
#define DDCAPS_READSCANLINE        0x00020000
#define DDCAPS_COLORKEY            0x00400000
#define DDCAPS_BLTCOLORFILL        0x04000000
#define DDCAPS_BLTDEPTHFILL        0x10000000
#define DDCAPS2_NOPAGELOCKREQUIRED 0x00000800
#define DDCAPS2_WIDESURFACES       0x00001000
#define DDCAPS2_CANRENDERWINDOWED  0x00080000
#define DDCAPS2_FLIPINTERVAL       0x00200000
#define DDCAPS2_FLIPNOVSYNC        0x00400000
#define DDCKEYCAPS_DESTBLT         0x00000001
#define DDCKEYCAPS_SRCBLT          0x00000200
#define DDFXCAPS_BLTMIRRORLEFTRIGHT 0x00000040
#define DDFXCAPS_BLTMIRRORUPDOWN   0x00000080
#define DDFXCAPS_BLTSHRINKX        0x00000400
#define DDFXCAPS_BLTSHRINKXN       0x00000800
#define DDFXCAPS_BLTSHRINKY        0x00001000
#define DDFXCAPS_BLTSHRINKYN       0x00002000
#define DDFXCAPS_BLTSTRETCHX       0x00004000
#define DDFXCAPS_BLTSTRETCHXN      0x00008000
#define DDFXCAPS_BLTSTRETCHY       0x00010000
#define DDFXCAPS_BLTSTRETCHYN      0x00020000
#define DDBD_8                     0x00000800
#define DDBD_16                    0x00000400
#define DDBD_24                    0x00000200
#define DDBD_32                    0x00000100
#define SRCCOPY                    0x00CC0020

typedef struct VIDMEM {
    DWORD   dwFlags;
    FLATPTR fpStart;
    FLATPTR fpEnd;     /* also dwWidth */
    DDSCAPS ddsCaps;   /* what the heap may NOT be used for */
    DDSCAPS ddsCapsAlt;
    DWORD   lpHeap;    /* also dwHeight */
} VIDMEM;
DDI_SIZE(VIDMEM, 24);

#define VIDMEM_ISLINEAR 0x00000001

typedef struct VIDMEMINFO {
    FLATPTR       fpPrimary;
    DWORD         dwFlags;
    DWORD         dwDisplayWidth;
    DWORD         dwDisplayHeight;
    LONG          lDisplayPitch;
    DDPIXELFORMAT ddpfDisplay;
    DWORD         dwOffscreenAlign;
    DWORD         dwOverlayAlign;
    DWORD         dwTextureAlign;
    DWORD         dwZBufferAlign;
    DWORD         dwAlphaAlign;
    DWORD         dwNumHeaps;
    DWORD         pvmList; /* 16:16 */
} VIDMEMINFO;
DDI_SIZE(VIDMEMINFO, 80);
DDI_CHECK(VIDMEMINFO, dwOffscreenAlign, 52);
DDI_CHECK(VIDMEMINFO, pvmList, 76);

typedef struct DDHALMODEINFO {
    DWORD dwWidth;
    DWORD dwHeight;
    LONG  lPitch;
    DWORD dwBPP;
    WORD  wFlags;
    WORD  wRefreshRate;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwAlphaBitMask;
} DDHALMODEINFO;
DDI_SIZE(DDHALMODEINFO, 36);

#define DDMODEINFO_PALETTIZED 0x0001

typedef struct DDHAL_DDCALLBACKS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD DestroyDriver; /* 16:16 in vgpu98.drv: no CB32 flag */
    DWORD CreateSurface;
    DWORD SetColorKey;
    DWORD SetMode;
    DWORD WaitForVerticalBlank;
    DWORD CanCreateSurface;
    DWORD CreatePalette;
    DWORD GetScanLine;
    DWORD SetExclusiveMode;
    DWORD FlipToGDISurface;
} DDHAL_DDCALLBACKS;
DDI_SIZE(DDHAL_DDCALLBACKS, 48);

#define DDHAL_CB32_DESTROYDRIVER        0x00000001
#define DDHAL_CB32_CREATESURFACE        0x00000002
#define DDHAL_CB32_SETCOLORKEY          0x00000004
#define DDHAL_CB32_SETMODE              0x00000008
#define DDHAL_CB32_WAITFORVERTICALBLANK 0x00000010
#define DDHAL_CB32_CANCREATESURFACE     0x00000020
#define DDHAL_CB32_CREATEPALETTE        0x00000040
#define DDHAL_CB32_GETSCANLINE          0x00000080
#define DDHAL_CB32_SETEXCLUSIVEMODE     0x00000100
#define DDHAL_CB32_FLIPTOGDISURFACE     0x00000200

typedef struct DDHAL_DDSURFACECALLBACKS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD DestroySurface;
    DWORD Flip;
    DWORD SetClipList;
    DWORD Lock;
    DWORD Unlock;
    DWORD Blt;
    DWORD SetColorKey;
    DWORD AddAttachedSurface;
    DWORD GetBltStatus;
    DWORD GetFlipStatus;
    DWORD UpdateOverlay;
    DWORD SetOverlayPosition;
    DWORD reserved4;
    DWORD SetPalette;
} DDHAL_DDSURFACECALLBACKS;
DDI_SIZE(DDHAL_DDSURFACECALLBACKS, 64);

#define DDHAL_SURFCB32_DESTROYSURFACE 0x00000001
#define DDHAL_SURFCB32_FLIP           0x00000002
#define DDHAL_SURFCB32_LOCK           0x00000008
#define DDHAL_SURFCB32_UNLOCK         0x00000010
#define DDHAL_SURFCB32_BLT            0x00000020
#define DDHAL_SURFCB32_SETCOLORKEY    0x00000040
#define DDHAL_SURFCB32_ADDATTACHEDSURFACE 0x00000080
#define DDHAL_SURFCB32_GETBLTSTATUS   0x00000100
#define DDHAL_SURFCB32_GETFLIPSTATUS  0x00000200

typedef struct DDHAL_DDPALETTECALLBACKS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD DestroyPalette;
    DWORD SetEntries;
} DDHAL_DDPALETTECALLBACKS;
DDI_SIZE(DDHAL_DDPALETTECALLBACKS, 16);

#define DDHAL_PALCB32_SETENTRIES 0x00000002

/* DirectDraw's execute buffers (Direct3D's vertex and command buffers) when
   the driver allocates them: called instead of the surface callbacks for
   surfaces with DDSCAPS_EXECUTEBUFFER, with the same data. */
typedef struct DDHAL_DDEXEBUFCALLBACKS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD CanCreateExecuteBuffer;
    DWORD CreateExecuteBuffer;
    DWORD DestroyExecuteBuffer;
    DWORD LockExecuteBuffer;
    DWORD UnlockExecuteBuffer;
} DDHAL_DDEXEBUFCALLBACKS;
DDI_SIZE(DDHAL_DDEXEBUFCALLBACKS, 28);

#define DDHAL_EXEBUFCB32_CANCREATEEXEBUF 0x00000001
#define DDHAL_EXEBUFCB32_CREATEEXEBUF    0x00000002
#define DDHAL_EXEBUFCB32_DESTROYEXEBUF   0x00000004
#define DDHAL_EXEBUFCB32_LOCKEXEBUF      0x00000008
#define DDHAL_EXEBUFCB32_UNLOCKEXEBUF    0x00000010

typedef struct DDHALINFO {
    DWORD      dwSize;
    DWORD      lpDDCallbacks;        /* 16:16 */
    DWORD      lpDDSurfaceCallbacks; /* 16:16 */
    DWORD      lpDDPaletteCallbacks; /* 16:16 */
    VIDMEMINFO vmiData;
    DDCORECAPS ddCaps;
    DWORD      dwMonitorFrequency;
    DWORD      GetDriverInfo;        /* flat */
    DWORD      dwModeIndex;
    DWORD      lpdwFourCC;
    DWORD      dwNumModes;
    DWORD      lpModeInfo;           /* 16:16 */
    DWORD      dwFlags;
    DWORD      lpPDevice;
    DWORD      hInstance;
    DWORD      lpD3DGlobalDriverData; /* flat */
    DWORD      lpD3DHALCallbacks;     /* flat */
    DWORD      lpDDExeBufCallbacks;
} DDHALINFO;
DDI_SIZE(DDHALINFO, 460);
DDI_CHECK(DDHALINFO, vmiData, 16);
DDI_CHECK(DDHALINFO, ddCaps, 96);
DDI_CHECK(DDHALINFO, dwMonitorFrequency, 412);
DDI_CHECK(DDHALINFO, lpModeInfo, 432);
DDI_CHECK(DDHALINFO, lpD3DGlobalDriverData, 448);

#define DDHALINFO_ISPRIMARYDISPLAY 0x00000001
#define DDHALINFO_GETDRIVERINFOSET 0x00000004

#ifdef VGPU98_DLL

/* --- Runtime objects (32-bit only) -------------------------------------- */

typedef struct RECTL {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECTL;

typedef struct PALETTEENTRY {
    BYTE peRed;
    BYTE peGreen;
    BYTE peBlue;
    BYTE peFlags;
} PALETTEENTRY;

typedef struct GUID {
    DWORD Data1;
    WORD  Data2;
    WORD  Data3;
    BYTE  Data4[8];
} GUID;

struct DDRAWI_DDRAWSURFACE_LCL;
struct DDRAWI_DDRAWSURFACE_INT;
struct DDRAWI_DDRAWPALETTE_INT;

typedef struct DDRAWI_DDRAWSURFACE_GBL {
    DWORD         dwRefCnt;
    DWORD         dwGlobalFlags;
    DWORD         dwBlockSizeY;  /* also lpRectList */
    DWORD         dwBlockSizeX;  /* also lpVidMemHeap */
    void         *lpDD;
    FLATPTR       fpVidMem;
    LONG          lPitch;        /* also dwLinearSize */
    WORD          wHeight;
    WORD          wWidth;
    DWORD         dwUsageCount;
    DWORD         dwReserved1;   /* the driver's: the surface's last-use fence */
    DDPIXELFORMAT ddpfSurface;
} DDRAWI_DDRAWSURFACE_GBL;
DDI_CHECK(DDRAWI_DDRAWSURFACE_GBL, fpVidMem, 20);
DDI_CHECK(DDRAWI_DDRAWSURFACE_GBL, wHeight, 28);
DDI_CHECK(DDRAWI_DDRAWSURFACE_GBL, dwReserved1, 36);
DDI_CHECK(DDRAWI_DDRAWSURFACE_GBL, ddpfSurface, 40);

#define DDHAL_PLEASEALLOC_BLOCKSIZE 0x00000002

#define DDRAWISURFGBL_SYSMEMEXECUTEBUFFER 0x00004000 /* the driver's execute buffer is in system memory */

/* The surface's extra capabilities (DirectX 6 and later). */
typedef struct DDSCAPSEX {
    DWORD dwCaps2;
    DWORD dwCaps3;
    DWORD dwCaps4;
} DDSCAPSEX;

#define DDSCAPS2_VERTEXBUFFER  0x00000020 /* an execute buffer that is an application's vertex buffer */
#define DDSCAPS2_COMMANDBUFFER 0x00000040 /* an execute buffer that is the runtime's command buffer */

/* As Windows 98's DirectDraw lays it out (without the NT-only mode field). */
typedef struct DDRAWI_DDRAWSURFACE_MORE {
    DWORD     dwSize;
    void     *lpIUnknowns;
    void     *lpDD_lcl;
    DWORD     dwPageLockCount;
    DWORD     dwBytesAllocated;
    void     *lpDD_int;
    DWORD     dwMipMapCount;
    void     *lpDDIClipper;
    void     *lpHeapAliasInfo;
    DWORD     dwOverlayFlags;
    void     *rgjunc;
    void     *lpVideoPort;
    void     *lpddOverlayFX;
    DDSCAPSEX ddsCapsEx;
    DWORD     dwTextureStage;
    void     *lpDDRAWReserved;
    void     *lpDDRAWReserved2;
    void     *lpDDrawReserved3;
    DWORD     dwDDrawReserved4;
    void     *lpDDrawReserved5;
    DWORD    *lpGammaRamp;
    DWORD    *lpOriginalGammaRamp;
    void     *lpDDrawReserved6;
    DWORD     dwSurfaceHandle; /* DirectX 7: the handle CreateSurfaceEx names */
    DWORD     qwDDrawReserved8[2];
    void     *lpDDrawReserved9;
    DWORD     cSurfaces;
    void     *pCreatedDDSurfaceDesc2;
    void     *slist;
    DWORD     dwFVF;           /* an execute buffer's vertex format */
} DDRAWI_DDRAWSURFACE_MORE;
DDI_CHECK(DDRAWI_DDRAWSURFACE_MORE, dwMipMapCount, 24);
DDI_CHECK(DDRAWI_DDRAWSURFACE_MORE, ddsCapsEx, 52);
DDI_CHECK(DDRAWI_DDRAWSURFACE_MORE, dwSurfaceHandle, 100);
DDI_CHECK(DDRAWI_DDRAWSURFACE_MORE, dwFVF, 128);

#define DDAL_IMPLICIT 0x00000001 /* ATTACHLIST.dwFlags: attached when the chain was made */

typedef struct ATTACHLIST {
    DWORD                           dwFlags;
    struct ATTACHLIST              *lpLink;
    struct DDRAWI_DDRAWSURFACE_LCL *lpAttached;
    struct DDRAWI_DDRAWSURFACE_INT *lpIAttached;
} ATTACHLIST;

typedef struct DDRAWI_DDRAWSURFACE_LCL {
    DDRAWI_DDRAWSURFACE_MORE       *lpSurfMore;
    DDRAWI_DDRAWSURFACE_GBL        *lpGbl;
    DWORD                           hDDSurface;
    ATTACHLIST                     *lpAttachList;
    ATTACHLIST                     *lpAttachListFrom;
    DWORD                           dwLocalRefCnt;
    DWORD                           dwProcessId;
    DWORD                           dwFlags;
    DDSCAPS                         ddsCaps;
    struct DDRAWI_DDRAWPALETTE_INT *lpDDPalette;
    void                           *lpDDClipper;
    DWORD                           dwModeCreatedIn;
    DWORD                           dwBackBufferCount;
    DDCOLORKEY                      ddckCKDestBlt;
    DDCOLORKEY                      ddckCKSrcBlt;
    DWORD                           hDC;
    DWORD                           dwReserved1;
} DDRAWI_DDRAWSURFACE_LCL;
DDI_CHECK(DDRAWI_DDRAWSURFACE_LCL, dwFlags, 28);
DDI_CHECK(DDRAWI_DDRAWSURFACE_LCL, ddsCaps, 32);
DDI_CHECK(DDRAWI_DDRAWSURFACE_LCL, lpDDPalette, 36);
DDI_CHECK(DDRAWI_DDRAWSURFACE_LCL, ddckCKSrcBlt, 60);
DDI_CHECK(DDRAWI_DDRAWSURFACE_LCL, dwReserved1, 72);

#define DDRAWISURF_HASCKEYSRCBLT  0x00000800
#define DDRAWISURF_HASPIXELFORMAT 0x00002000
#define DDRAWISURF_INVALID        0x10000000

typedef struct DDRAWI_DDRAWSURFACE_INT {
    void                    *lpVtbl;
    DDRAWI_DDRAWSURFACE_LCL *lpLcl;
    void                    *lpLink;
    DWORD                    dwIntRefCnt;
} DDRAWI_DDRAWSURFACE_INT;

typedef struct DDRAWI_DDRAWPALETTE_GBL {
    DWORD         dwRefCnt;
    DWORD         dwFlags;
    void         *lpDD_lcl;
    DWORD         dwProcessId;
    PALETTEENTRY *lpColorTable;
} DDRAWI_DDRAWPALETTE_GBL;
DDI_CHECK(DDRAWI_DDRAWPALETTE_GBL, lpColorTable, 16);

typedef struct DDRAWI_DDRAWPALETTE_LCL {
    void                    *lpPalMore;
    DDRAWI_DDRAWPALETTE_GBL *lpGbl;
} DDRAWI_DDRAWPALETTE_LCL;

typedef struct DDRAWI_DDRAWPALETTE_INT {
    void                    *lpVtbl;
    DDRAWI_DDRAWPALETTE_LCL *lpLcl;
} DDRAWI_DDRAWPALETTE_INT;

#define DDRAWIPAL_256   0x00000001
#define DDRAWIPAL_ALPHA 0x00002000
#define DDPCAPS_ALPHA   0x00000400

typedef DDRAWI_DDRAWSURFACE_LCL *LPSURF;

/* --- DirectDraw callback data ------------------------------------------- */

typedef struct DDHAL_CREATESURFACEDATA {
    void           *lpDD;
    DDSURFACEDESC  *lpDDSurfaceDesc;
    LPSURF         *lplpSList;
    DWORD           dwSCnt;
    HRESULT         ddRVal;
    void           *CreateSurface;
} DDHAL_CREATESURFACEDATA;
DDI_SIZE(DDHAL_CREATESURFACEDATA, 24);

typedef struct DDHAL_CANCREATESURFACEDATA {
    void          *lpDD;
    DDSURFACEDESC *lpDDSurfaceDesc;
    DWORD          bIsDifferentPixelFormat;
    HRESULT        ddRVal;
    void          *CanCreateSurface;
} DDHAL_CANCREATESURFACEDATA;

typedef struct DDHAL_DESTROYSURFACEDATA {
    void   *lpDD;
    LPSURF  lpDDSurface;
    HRESULT ddRVal;
    void   *DestroySurface;
} DDHAL_DESTROYSURFACEDATA;

typedef struct DDHAL_FLIPDATA {
    void   *lpDD;
    LPSURF  lpSurfCurr;
    LPSURF  lpSurfTarg;
    DWORD   dwFlags;
    HRESULT ddRVal;
    void   *Flip;
} DDHAL_FLIPDATA;

#define DDCKEY_SRCBLT 0x00000008

typedef struct DDHAL_SETCOLORKEYDATA {
    void      *lpDD;
    LPSURF     lpDDSurface;
    DWORD      dwFlags;
    DDCOLORKEY ckNew;
    HRESULT    ddRVal;
    void      *SetColorKey;
} DDHAL_SETCOLORKEYDATA;
DDI_SIZE(DDHAL_SETCOLORKEYDATA, 28);

typedef struct DDHAL_ADDATTACHEDSURFACEDATA {
    void   *lpDD;
    LPSURF  lpDDSurface;
    LPSURF  lpSurfAttached;
    HRESULT ddRVal;
    void   *AddAttachedSurface;
} DDHAL_ADDATTACHEDSURFACEDATA;
DDI_SIZE(DDHAL_ADDATTACHEDSURFACEDATA, 20);

#define DDFLIP_WAIT      0x00000001
#define DDFLIP_NOVSYNC   0x00000008
#define DDFLIP_INTERVAL2 0x02000000
#define DDFLIP_INTERVAL3 0x03000000
#define DDFLIP_INTERVAL4 0x04000000

typedef struct DDHAL_LOCKDATA {
    void   *lpDD;
    LPSURF  lpDDSurface;
    DWORD   bHasRect;
    RECTL   rArea;
    void   *lpSurfData;
    HRESULT ddRVal;
    void   *Lock;
    DWORD   dwFlags;
} DDHAL_LOCKDATA;
DDI_CHECK(DDHAL_LOCKDATA, lpSurfData, 28);
DDI_CHECK(DDHAL_LOCKDATA, dwFlags, 40);

#define DDLOCK_WAIT            0x00000001
#define DDLOCK_READONLY        0x00000010
#define DDLOCK_WRITEONLY       0x00000020
#define DDLOCK_NOOVERWRITE     0x00001000 /* DirectX 7: nothing a queued draw reads is written */
#define DDLOCK_DISCARDCONTENTS 0x00002000 /* DirectX 7: the old contents are not needed */

typedef struct DDHAL_UNLOCKDATA {
    void   *lpDD;
    LPSURF  lpDDSurface;
    HRESULT ddRVal;
    void   *Unlock;
} DDHAL_UNLOCKDATA;

typedef struct DDBLTFX {
    DWORD      dwSize;
    DWORD      dwDDFX;
    DWORD      dwROP;
    DWORD      dwDDROP;
    DWORD      dwRotationAngle;
    DWORD      dwZBufferOpCode;
    DWORD      dwZBufferLow;
    DWORD      dwZBufferHigh;
    DWORD      dwZBufferBaseDest;
    DWORD      dwZDestConstBitDepth;
    DWORD      dwZDestConst;
    DWORD      dwZSrcConstBitDepth;
    DWORD      dwZSrcConst;
    DWORD      dwAlphaEdgeBlendBitDepth;
    DWORD      dwAlphaEdgeBlend;
    DWORD      dwReserved;
    DWORD      dwAlphaDestConstBitDepth;
    DWORD      dwAlphaDestConst;
    DWORD      dwAlphaSrcConstBitDepth;
    DWORD      dwAlphaSrcConst;
    DWORD      dwFillColor; /* also dwFillDepth, dwFillPixel */
    DDCOLORKEY ddckDestColorkey;
    DDCOLORKEY ddckSrcColorkey;
} DDBLTFX;
DDI_SIZE(DDBLTFX, 100);
DDI_CHECK(DDBLTFX, dwFillColor, 80);

#define DDBLTFX_MIRRORLEFTRIGHT 0x00000002
#define DDBLTFX_MIRRORUPDOWN    0x00000004

typedef struct DDHAL_BLTDATA {
    void   *lpDD;
    LPSURF  lpDDDestSurface;
    RECTL   rDest;
    LPSURF  lpDDSrcSurface;
    RECTL   rSrc;
    DWORD   dwFlags;
    DWORD   dwROPFlags;
    DDBLTFX bltFX;
    HRESULT ddRVal;
    void   *Blt;
    DWORD   IsClipped;
    RECTL   rOrigDest;
    RECTL   rOrigSrc;
    DWORD   dwRectCnt;
    RECTL  *prDestRects;
} DDHAL_BLTDATA;
DDI_CHECK(DDHAL_BLTDATA, lpDDSrcSurface, 24);
DDI_CHECK(DDHAL_BLTDATA, dwFlags, 44);
DDI_CHECK(DDHAL_BLTDATA, bltFX, 52);
DDI_CHECK(DDHAL_BLTDATA, ddRVal, 152);
DDI_CHECK(DDHAL_BLTDATA, IsClipped, 160);
DDI_CHECK(DDHAL_BLTDATA, dwRectCnt, 196);
DDI_CHECK(DDHAL_BLTDATA, prDestRects, 200);

#define DDBLT_ASYNC            0x00000200
#define DDBLT_COLORFILL        0x00000400
#define DDBLT_DDFX             0x00000800
#define DDBLT_KEYDEST          0x00002000
#define DDBLT_KEYDESTOVERRIDE  0x00004000
#define DDBLT_KEYSRC           0x00008000
#define DDBLT_KEYSRCOVERRIDE   0x00010000
#define DDBLT_ROP              0x00020000
#define DDBLT_WAIT             0x01000000
#define DDBLT_DEPTHFILL        0x02000000
#define DDBLT_DONOTWAIT        0x08000000
/* DirectX 8's presentation blits (set by the runtime, for DirectX 8 drivers):
   every blit of a windowed Present, and its last. */
#define DDBLT_PRESENTATION      0x10000000
#define DDBLT_LAST_PRESENTATION 0x20000000

typedef struct DDHAL_GETBLTSTATUSDATA {
    void   *lpDD;
    LPSURF  lpDDSurface;
    DWORD   dwFlags;
    HRESULT ddRVal;
    void   *GetBltStatus;
} DDHAL_GETBLTSTATUSDATA;

#define DDGBS_CANBLT    0x00000001
#define DDGBS_ISBLTDONE 0x00000002

typedef struct DDHAL_GETFLIPSTATUSDATA {
    void   *lpDD;
    LPSURF  lpDDSurface;
    DWORD   dwFlags;
    HRESULT ddRVal;
    void   *GetFlipStatus;
} DDHAL_GETFLIPSTATUSDATA;

#define DDGFS_CANFLIP    0x00000001
#define DDGFS_ISFLIPDONE 0x00000002

typedef struct DDHAL_WAITFORVERTICALBLANKDATA {
    void   *lpDD;
    DWORD   dwFlags;
    DWORD   bIsInVB;
    DWORD   hEvent;
    HRESULT ddRVal;
    void   *WaitForVerticalBlank;
} DDHAL_WAITFORVERTICALBLANKDATA;

#define DDWAITVB_BLOCKBEGIN 0x00000001
#define DDWAITVB_BLOCKEND   0x00000004
#define DDWAITVB_I_TESTVB   0x80000006

typedef struct DDHAL_GETSCANLINEDATA {
    void   *lpDD;
    DWORD   dwScanLine;
    HRESULT ddRVal;
    void   *GetScanLine;
} DDHAL_GETSCANLINEDATA;

typedef struct DDHAL_FLIPTOGDISURFACEDATA {
    void   *lpDD;
    DWORD   dwToGDI;
    DWORD   dwReserved;
    HRESULT ddRVal;
    void   *FlipToGDISurface;
} DDHAL_FLIPTOGDISURFACEDATA;

typedef struct DDHAL_SETENTRIESDATA {
    void                    *lpDD;
    DDRAWI_DDRAWPALETTE_GBL *lpDDPalette;
    DWORD                    dwBase;
    DWORD                    dwNumEntries;
    PALETTEENTRY            *lpEntries;
    HRESULT                  ddRVal;
    void                    *SetEntries;
} DDHAL_SETENTRIESDATA;
DDI_CHECK(DDHAL_SETENTRIESDATA, lpEntries, 16);

typedef struct DDHAL_GETDRIVERINFODATA {
    DWORD   dwSize;
    DWORD   dwFlags;
    GUID    guidInfo;
    DWORD   dwExpectedSize;
    void   *lpvData;
    DWORD   dwActualSize;
    HRESULT ddRVal;
} DDHAL_GETDRIVERINFODATA;
DDI_CHECK(DDHAL_GETDRIVERINFODATA, dwExpectedSize, 24);
DDI_CHECK(DDHAL_GETDRIVERINFODATA, ddRVal, 36);

/* --- Direct3D ----------------------------------------------------------- */

typedef struct D3DTRANSFORMCAPS {
    DWORD dwSize;
    DWORD dwCaps;
} D3DTRANSFORMCAPS;

typedef struct D3DLIGHTINGCAPS {
    DWORD dwSize;
    DWORD dwCaps;
    DWORD dwLightingModel;
    DWORD dwNumLights;
} D3DLIGHTINGCAPS;

typedef struct D3DPRIMCAPS {
    DWORD dwSize;
    DWORD dwMiscCaps;
    DWORD dwRasterCaps;
    DWORD dwZCmpCaps;
    DWORD dwSrcBlendCaps;
    DWORD dwDestBlendCaps;
    DWORD dwAlphaCmpCaps;
    DWORD dwShadeCaps;
    DWORD dwTextureCaps;
    DWORD dwTextureFilterCaps;
    DWORD dwTextureBlendCaps;
    DWORD dwTextureAddressCaps;
    DWORD dwStippleWidth;
    DWORD dwStippleHeight;
} D3DPRIMCAPS;
DDI_SIZE(D3DPRIMCAPS, 56);

typedef struct D3DDEVICEDESC_V1 {
    DWORD            dwSize;
    DWORD            dwFlags;
    DWORD            dcmColorModel;
    DWORD            dwDevCaps;
    D3DTRANSFORMCAPS dtcTransformCaps;
    BOOL             bClipping;
    D3DLIGHTINGCAPS  dlcLightingCaps;
    D3DPRIMCAPS      dpcLineCaps;
    D3DPRIMCAPS      dpcTriCaps;
    DWORD            dwDeviceRenderBitDepth;
    DWORD            dwDeviceZBufferBitDepth;
    DWORD            dwMaxBufferSize;
    DWORD            dwMaxVertexCount;
} D3DDEVICEDESC_V1;
DDI_SIZE(D3DDEVICEDESC_V1, 172);
DDI_CHECK(D3DDEVICEDESC_V1, dpcLineCaps, 44);
DDI_CHECK(D3DDEVICEDESC_V1, dpcTriCaps, 100);
DDI_CHECK(D3DDEVICEDESC_V1, dwDeviceRenderBitDepth, 156);

#define D3DDD_COLORMODEL            0x00000001
#define D3DDD_DEVCAPS               0x00000002
#define D3DDD_TRANSFORMCAPS         0x00000004
#define D3DDD_LIGHTINGCAPS          0x00000008
#define D3DDD_BCLIPPING             0x00000010
#define D3DDD_LINECAPS              0x00000020
#define D3DDD_TRICAPS               0x00000040
#define D3DDD_DEVICERENDERBITDEPTH  0x00000080
#define D3DDD_DEVICEZBUFFERBITDEPTH 0x00000100
#define D3DDD_MAXBUFFERSIZE         0x00000200
#define D3DDD_MAXVERTEXCOUNT        0x00000400
#define D3DCOLOR_RGB                2

#define D3DDEVCAPS_FLOATTLVERTEX       0x00000001
#define D3DDEVCAPS_EXECUTESYSTEMMEMORY 0x00000010
#define D3DDEVCAPS_TLVERTEXSYSTEMMEMORY 0x00000040
#define D3DDEVCAPS_TEXTUREVIDEOMEMORY  0x00000200
#define D3DDEVCAPS_DRAWPRIMTLVERTEX    0x00000400
#define D3DDEVCAPS_CANRENDERAFTERFLIP  0x00000800
#define D3DDEVCAPS_DRAWPRIMITIVES2     0x00002000

#define D3DPMISCCAPS_MASKZ    0x00000002
#define D3DPMISCCAPS_CULLNONE 0x00000010
#define D3DPMISCCAPS_CULLCW   0x00000020
#define D3DPMISCCAPS_CULLCCW  0x00000040

#define D3DPRASTERCAPS_DITHER        0x00000001
#define D3DPRASTERCAPS_ZTEST         0x00000010
#define D3DPRASTERCAPS_SUBPIXEL      0x00000020
#define D3DPRASTERCAPS_FOGVERTEX     0x00000080
#define D3DPRASTERCAPS_FOGTABLE      0x00000100
#define D3DPRASTERCAPS_MIPMAPLODBIAS 0x00002000
#define D3DPRASTERCAPS_ZBIAS         0x00004000
#define D3DPRASTERCAPS_ANISOTROPY    0x00020000
#define D3DPRASTERCAPS_WBUFFER       0x00040000
#define D3DPRASTERCAPS_WFOG          0x00100000

#define D3DPCMPCAPS_ALL      0x000000FF /* never ... always */
#define D3DPBLENDCAPS_ALL    0x00001FFF /* zero ... both inverse source alpha */

#define D3DPSHADECAPS_COLORFLATRGB       0x00000002
#define D3DPSHADECAPS_COLORGOURAUDRGB    0x00000008
#define D3DPSHADECAPS_SPECULARFLATRGB    0x00000080
#define D3DPSHADECAPS_SPECULARGOURAUDRGB 0x00000200
#define D3DPSHADECAPS_ALPHAFLATBLEND     0x00001000
#define D3DPSHADECAPS_ALPHAGOURAUDBLEND  0x00004000
#define D3DPSHADECAPS_FOGFLAT            0x00040000
#define D3DPSHADECAPS_FOGGOURAUD         0x00080000

#define D3DPTEXTURECAPS_PERSPECTIVE  0x00000001
#define D3DPTEXTURECAPS_POW2         0x00000002
#define D3DPTEXTURECAPS_ALPHA        0x00000004
#define D3DPTEXTURECAPS_TRANSPARENCY 0x00000008
#define D3DPTEXTURECAPS_BORDER       0x00000010
#define D3DPTEXTURECAPS_ALPHAPALETTE 0x00000080

#define D3DPTFILTERCAPS_NEAREST          0x00000001
#define D3DPTFILTERCAPS_LINEAR           0x00000002
#define D3DPTFILTERCAPS_MIPNEAREST       0x00000004
#define D3DPTFILTERCAPS_MIPLINEAR        0x00000008
#define D3DPTFILTERCAPS_LINEARMIPNEAREST 0x00000010
#define D3DPTFILTERCAPS_LINEARMIPLINEAR  0x00000020
#define D3DPTFILTERCAPS_MINFPOINT        0x00000100
#define D3DPTFILTERCAPS_MINFLINEAR       0x00000200
#define D3DPTFILTERCAPS_MINFANISOTROPIC  0x00000400
#define D3DPTFILTERCAPS_MIPFPOINT        0x00010000
#define D3DPTFILTERCAPS_MIPFLINEAR       0x00020000
#define D3DPTFILTERCAPS_MAGFPOINT        0x01000000
#define D3DPTFILTERCAPS_MAGFLINEAR       0x02000000
#define D3DPTFILTERCAPS_MAGFANISOTROPIC  0x04000000

#define D3DPTBLENDCAPS_DECAL         0x00000001
#define D3DPTBLENDCAPS_MODULATE      0x00000002
#define D3DPTBLENDCAPS_DECALALPHA    0x00000004
#define D3DPTBLENDCAPS_MODULATEALPHA 0x00000008
#define D3DPTBLENDCAPS_COPY          0x00000040
#define D3DPTBLENDCAPS_ADD           0x00000080

#define D3DPTADDRESSCAPS_WRAP          0x00000001
#define D3DPTADDRESSCAPS_MIRROR        0x00000002
#define D3DPTADDRESSCAPS_CLAMP         0x00000004
#define D3DPTADDRESSCAPS_BORDER        0x00000008
#define D3DPTADDRESSCAPS_INDEPENDENTUV 0x00000010

#define D3DSTENCILCAPS_ALL   0x000000FF /* keep ... decrement with wrap */
#define D3DTEXOPCAPS_ALL     0x00FFFFFF /* disable ... dot product 3 */

typedef struct D3DHAL_GLOBALDRIVERDATA {
    DWORD            dwSize;
    D3DDEVICEDESC_V1 hwCaps;
    DWORD            dwNumVertices;
    DWORD            dwNumClipVertices;
    DWORD            dwNumTextureFormats;
    DDSURFACEDESC   *lpTextureFormats;
} D3DHAL_GLOBALDRIVERDATA;
DDI_SIZE(D3DHAL_GLOBALDRIVERDATA, 192);
DDI_CHECK(D3DHAL_GLOBALDRIVERDATA, dwNumVertices, 176);
DDI_CHECK(D3DHAL_GLOBALDRIVERDATA, lpTextureFormats, 188);

typedef struct D3DHAL_D3DEXTENDEDCAPS {
    DWORD    dwSize;
    DWORD    dwMinTextureWidth;
    DWORD    dwMaxTextureWidth;
    DWORD    dwMinTextureHeight;
    DWORD    dwMaxTextureHeight;
    DWORD    dwMinStippleWidth;
    DWORD    dwMaxStippleWidth;
    DWORD    dwMinStippleHeight;
    DWORD    dwMaxStippleHeight;
    DWORD    dwMaxTextureRepeat;
    DWORD    dwMaxTextureAspectRatio;
    DWORD    dwMaxAnisotropy;
    D3DVALUE dvGuardBandLeft;
    D3DVALUE dvGuardBandTop;
    D3DVALUE dvGuardBandRight;
    D3DVALUE dvGuardBandBottom;
    D3DVALUE dvExtentsAdjust;
    DWORD    dwStencilCaps;
    DWORD    dwFVFCaps;
    DWORD    dwTextureOpCaps;
    WORD     wMaxTextureBlendStages;
    WORD     wMaxSimultaneousTextures;
    /* DirectX 7 */
    DWORD    dwMaxActiveLights;
    D3DVALUE dvMaxVertexW;
    WORD     wMaxUserClipPlanes;
    WORD     wMaxVertexBlendMatrices;
    DWORD    dwVertexProcessingCaps;
    DWORD    dwReserved[4];
} D3DHAL_D3DEXTENDEDCAPS;
DDI_SIZE(D3DHAL_D3DEXTENDEDCAPS, 116);
DDI_CHECK(D3DHAL_D3DEXTENDEDCAPS, dwMaxTextureRepeat, 36);
DDI_CHECK(D3DHAL_D3DEXTENDEDCAPS, dvGuardBandLeft, 48);
DDI_CHECK(D3DHAL_D3DEXTENDEDCAPS, dwStencilCaps, 68);
DDI_CHECK(D3DHAL_D3DEXTENDEDCAPS, wMaxTextureBlendStages, 80);
DDI_CHECK(D3DHAL_D3DEXTENDEDCAPS, dwMaxActiveLights, 84);
DDI_CHECK(D3DHAL_D3DEXTENDEDCAPS, dvMaxVertexW, 88);

#define D3DHAL_D3DEXTENDEDCAPS_DX6_SIZE 84

typedef struct D3DHAL_CALLBACKS {
    DWORD dwSize;
    void *ContextCreate;
    void *ContextDestroy;
    void *ContextDestroyAll;
    void *SceneCapture;
    void *lpReserved10;
    void *lpReserved11;
    void *RenderState;
    void *RenderPrimitive;
    DWORD dwReserved;
    void *TextureCreate;
    void *TextureDestroy;
    void *TextureSwap;
    void *TextureGetSurf;
    void *lpReserved12[10];
    void *GetState;
    DWORD dwReserved0[10];
} D3DHAL_CALLBACKS;
DDI_SIZE(D3DHAL_CALLBACKS, 140);
DDI_CHECK(D3DHAL_CALLBACKS, RenderState, 28);
DDI_CHECK(D3DHAL_CALLBACKS, TextureCreate, 40);
DDI_CHECK(D3DHAL_CALLBACKS, TextureGetSurf, 52);
DDI_CHECK(D3DHAL_CALLBACKS, GetState, 96);

typedef struct D3DHAL_CALLBACKS2 {
    DWORD dwSize;
    DWORD dwFlags;
    void *SetRenderTarget;
    void *Clear;
    void *DrawOnePrimitive;
    void *DrawOneIndexedPrimitive;
    void *DrawPrimitives;
} D3DHAL_CALLBACKS2;
DDI_SIZE(D3DHAL_CALLBACKS2, 28);

#define D3DHAL2_CB32_SETRENDERTARGET 0x00000001
#define D3DHAL2_CB32_CLEAR           0x00000002

typedef struct D3DHAL_CALLBACKS3 {
    DWORD dwSize;
    DWORD dwFlags;
    void *Clear2;
    void *lpvReserved;
    void *ValidateTextureStageState;
    void *DrawPrimitives2;
} D3DHAL_CALLBACKS3;
DDI_SIZE(D3DHAL_CALLBACKS3, 24);

#define D3DHAL3_CB32_CLEAR2                    0x00000001
#define D3DHAL3_CB32_VALIDATETEXTURESTAGESTATE 0x00000004
#define D3DHAL3_CB32_DRAWPRIMITIVES2           0x00000008

typedef struct D3DRECT {
    LONG x1;
    LONG y1;
    LONG x2;
    LONG y2;
} D3DRECT;

typedef struct D3DHAL_CONTEXTCREATEDATA {
    void                    *lpDDGbl;
    DDRAWI_DDRAWSURFACE_INT *lpDDS;
    DDRAWI_DDRAWSURFACE_INT *lpDDSZ;
    DWORD                    dwPID;
    DWORD                    dwhContext;
    HRESULT                  ddrval;
} D3DHAL_CONTEXTCREATEDATA;
DDI_SIZE(D3DHAL_CONTEXTCREATEDATA, 24);

typedef struct D3DHAL_CONTEXTDESTROYDATA {
    DWORD   dwhContext;
    HRESULT ddrval;
} D3DHAL_CONTEXTDESTROYDATA;

typedef struct D3DHAL_CONTEXTDESTROYALLDATA {
    DWORD   dwPID;
    HRESULT ddrval;
} D3DHAL_CONTEXTDESTROYALLDATA;

typedef struct D3DHAL_SCENECAPTUREDATA {
    DWORD   dwhContext;
    DWORD   dwFlag;
    HRESULT ddrval;
} D3DHAL_SCENECAPTUREDATA;

typedef struct D3DHAL_TEXTURECREATEDATA {
    DWORD                    dwhContext;
    DDRAWI_DDRAWSURFACE_INT *lpDDS;
    DWORD                    dwHandle;
    HRESULT                  ddrval;
} D3DHAL_TEXTURECREATEDATA;
DDI_CHECK(D3DHAL_TEXTURECREATEDATA, dwHandle, 8);

typedef struct D3DHAL_TEXTUREDESTROYDATA {
    DWORD   dwhContext;
    DWORD   dwHandle;
    HRESULT ddrval;
} D3DHAL_TEXTUREDESTROYDATA;

typedef struct D3DHAL_TEXTURESWAPDATA {
    DWORD   dwhContext;
    DWORD   dwHandle1;
    DWORD   dwHandle2;
    HRESULT ddrval;
} D3DHAL_TEXTURESWAPDATA;

typedef struct D3DHAL_TEXTUREGETSURFDATA {
    DWORD   dwhContext;
    DWORD   lpDDS;
    DWORD   dwHandle;
    HRESULT ddrval;
} D3DHAL_TEXTUREGETSURFDATA;

typedef struct D3DHAL_SETRENDERTARGETDATA {
    DWORD                    dwhContext;
    DDRAWI_DDRAWSURFACE_INT *lpDDS;
    DDRAWI_DDRAWSURFACE_INT *lpDDSZ;
    HRESULT                  ddrval;
} D3DHAL_SETRENDERTARGETDATA;
DDI_SIZE(D3DHAL_SETRENDERTARGETDATA, 16);

typedef struct D3DHAL_CLEARDATA {
    DWORD    dwhContext;
    DWORD    dwFlags;
    DWORD    dwFillColor;
    DWORD    dwFillDepth;
    D3DRECT *lpRects;
    DWORD    dwNumRects;
    HRESULT  ddrval;
} D3DHAL_CLEARDATA;

typedef struct D3DHAL_CLEAR2DATA {
    DWORD    dwhContext;
    DWORD    dwFlags;
    DWORD    dwFillColor;
    D3DVALUE dvFillDepth;
    DWORD    dwFillStencil;
    D3DRECT *lpRects;
    DWORD    dwNumRects;
    HRESULT  ddrval;
} D3DHAL_CLEAR2DATA;
DDI_CHECK(D3DHAL_CLEAR2DATA, ddrval, 28);

#define D3DCLEAR_TARGET  0x00000001
#define D3DCLEAR_ZBUFFER 0x00000002
#define D3DCLEAR_STENCIL 0x00000004

typedef struct D3DHAL_VALIDATETEXTURESTAGESTATEDATA {
    DWORD   dwhContext;
    DWORD   dwFlags;
    DWORD   dwReserved;
    DWORD   dwNumPasses;
    HRESULT ddrval;
} D3DHAL_VALIDATETEXTURESTAGESTATEDATA;

typedef struct D3DHAL_DRAWPRIMITIVES2DATA {
    DWORD   dwhContext;
    DWORD   dwFlags;
    DWORD   dwVertexType;
    LPSURF  lpDDCommands;
    DWORD   dwCommandOffset;
    DWORD   dwCommandLength;
    union {
        LPSURF lpDDVertex;
        void  *lpVertices;
    };
    DWORD   dwVertexOffset;
    DWORD   dwVertexLength;
    DWORD   dwReqVertexBufSize;
    DWORD   dwReqCommandBufSize;
    DWORD  *lpdwRStates;
    union {
        DWORD   dwVertexSize;
        HRESULT ddrval;
    };
    DWORD   dwErrorOffset;
} D3DHAL_DRAWPRIMITIVES2DATA;
DDI_SIZE(D3DHAL_DRAWPRIMITIVES2DATA, 56);
DDI_CHECK(D3DHAL_DRAWPRIMITIVES2DATA, lpdwRStates, 44);
DDI_CHECK(D3DHAL_DRAWPRIMITIVES2DATA, dwErrorOffset, 52);

#define D3DHALDP2_USERMEMVERTICES   0x00000001
#define D3DHALDP2_EXECUTEBUFFER     0x00000002
#define D3DHALDP2_SWAPVERTEXBUFFER  0x00000004 /* the driver may give the vertex buffer new memory */
#define D3DHALDP2_SWAPCOMMANDBUFFER 0x00000008
#define D3DHALDP2_REQVERTEXBUFSIZE  0x00000010 /* the new memory holds at least dwReqVertexBufSize bytes */
#define D3DHALDP2_REQCOMMANDBUFSIZE 0x00000020
#define D3DHALDP2_VIDMEMVERTEXBUF   0x00000040 /* out: the new vertex memory is video memory */
#define D3DHALDP2_VIDMEMCOMMANDBUF  0x00000080

typedef struct D3DHAL_DP2COMMAND {
    BYTE bCommand;
    BYTE bReserved;
    WORD wCount; /* wPrimitiveCount or wStateCount */
} D3DHAL_DP2COMMAND;

#define D3DDP2OP_POINTS               1
#define D3DDP2OP_INDEXEDLINELIST      2
#define D3DDP2OP_INDEXEDTRIANGLELIST  3
#define D3DDP2OP_RENDERSTATE          8
#define D3DDP2OP_LINELIST             15
#define D3DDP2OP_LINESTRIP            16
#define D3DDP2OP_INDEXEDLINESTRIP     17
#define D3DDP2OP_TRIANGLELIST         18
#define D3DDP2OP_TRIANGLESTRIP        19
#define D3DDP2OP_INDEXEDTRIANGLESTRIP 20
#define D3DDP2OP_TRIANGLEFAN          21
#define D3DDP2OP_INDEXEDTRIANGLEFAN   22
#define D3DDP2OP_TRIANGLEFAN_IMM      23
#define D3DDP2OP_LINELIST_IMM         24
#define D3DDP2OP_TEXTURESTAGESTATE    25
#define D3DDP2OP_INDEXEDTRIANGLELIST2 26
#define D3DDP2OP_INDEXEDLINELIST2     27
#define D3DDP2OP_VIEWPORTINFO         28
#define D3DDP2OP_WINFO                29

#define D3DTRIFLAG_EDGEENABLE1 0x00000100
#define D3DTRIFLAG_EDGEENABLE2 0x00000200
#define D3DTRIFLAG_EDGEENABLE3 0x00000400

/* The runtime's parser for commands the driver does not know
   (GUID_D3DParseUnknownCommandCallback). */
typedef HRESULT (__attribute__((stdcall)) *PFND3DPARSEUNKNOWNCOMMAND)(void *lpvCommands, void **lplpvReturnedCommand);

/* Flexible vertex format */
#define D3DFVF_RESERVED0        0x00000001
#define D3DFVF_POSITION_MASK    0x0000000E
#define D3DFVF_XYZ              0x00000002
#define D3DFVF_XYZRHW           0x00000004
#define D3DFVF_NORMAL           0x00000010
#define D3DFVF_RESERVED1        0x00000020
#define D3DFVF_DIFFUSE          0x00000040
#define D3DFVF_SPECULAR         0x00000080
#define D3DFVF_TEXCOUNT_MASK    0x00000F00
#define D3DFVF_TEXCOUNT_SHIFT   8

/* Direct3D render states (D3DRENDERSTATETYPE) the driver handles itself */
#define D3DRS_TEXTUREHANDLE      1
#define D3DRS_ANTIALIAS          2
#define D3DRS_TEXTUREADDRESS     3
#define D3DRS_TEXTUREPERSPECTIVE 4
#define D3DRS_WRAPU              5
#define D3DRS_WRAPV              6
#define D3DRS_ZENABLE            7
#define D3DRS_FILLMODE           8
#define D3DRS_SHADEMODE          9
#define D3DRS_LINEPATTERN        10
#define D3DRS_MONOENABLE         11
#define D3DRS_ROP2               12
#define D3DRS_PLANEMASK          13
#define D3DRS_ZWRITEENABLE       14
#define D3DRS_ALPHATESTENABLE    15
#define D3DRS_LASTPIXEL          16
#define D3DRS_TEXTUREMAG         17
#define D3DRS_TEXTUREMIN         18
#define D3DRS_SRCBLEND           19
#define D3DRS_DESTBLEND          20
#define D3DRS_TEXTUREMAPBLEND    21
#define D3DRS_CULLMODE           22
#define D3DRS_ZFUNC              23
#define D3DRS_ALPHAREF           24
#define D3DRS_ALPHAFUNC          25
#define D3DRS_DITHERENABLE       26
#define D3DRS_ALPHABLENDENABLE   27
#define D3DRS_FOGENABLE          28
#define D3DRS_SPECULARENABLE     29
#define D3DRS_ZVISIBLE           30
#define D3DRS_SUBPIXEL           31
#define D3DRS_SUBPIXELX          32
#define D3DRS_STIPPLEDALPHA      33
#define D3DRS_FOGCOLOR           34
#define D3DRS_FOGTABLEMODE       35
#define D3DRS_FOGTABLESTART      36
#define D3DRS_FOGTABLEEND        37
#define D3DRS_FOGTABLEDENSITY    38
#define D3DRS_STIPPLEENABLE      39
#define D3DRS_EDGEANTIALIAS      40
#define D3DRS_COLORKEYENABLE     41
#define D3DRS_BORDERCOLOR        43
#define D3DRS_TEXTUREADDRESSU    44
#define D3DRS_TEXTUREADDRESSV    45
#define D3DRS_MIPMAPLODBIAS      46
#define D3DRS_ZBIAS              47
#define D3DRS_RANGEFOGENABLE     48
#define D3DRS_ANISOTROPY         49
#define D3DRS_FLUSHBATCH         50
#define D3DRS_TRANSLUCENTSORTINDEPENDENT 51
#define D3DRS_STENCILENABLE      52
#define D3DRS_STENCILFAIL        53
#define D3DRS_STENCILZFAIL       54
#define D3DRS_STENCILPASS        55
#define D3DRS_STENCILFUNC        56
#define D3DRS_STENCILREF         57
#define D3DRS_STENCILMASK        58
#define D3DRS_STENCILWRITEMASK   59
#define D3DRS_TEXTUREFACTOR      60
#define D3DRS_STIPPLEPATTERN00   64
#define D3DRS_STIPPLEPATTERN31   95
#define D3DRS_WRAP0              128
#define D3DRS_WRAP7              135
#define D3DRS_MAX                256 /* the driver's table covers 0-255 */

/* Texture stage state 0 in DrawPrimitives2: the texture handle */
#define D3DTSS_TEXTUREMAP        0
#define D3DTSS_ADDRESS           12

/* Legacy texture filters (TEXTUREMAG, TEXTUREMIN) */
#define D3DFILTER_NEAREST          1
#define D3DFILTER_LINEAR           2
#define D3DFILTER_MIPNEAREST       3
#define D3DFILTER_MIPLINEAR        4
#define D3DFILTER_LINEARMIPNEAREST 5
#define D3DFILTER_LINEARMIPLINEAR  6

/* Legacy texture blend modes (TEXTUREMAPBLEND) */
#define D3DTBLEND_DECAL         1
#define D3DTBLEND_MODULATE      2
#define D3DTBLEND_DECALALPHA    3
#define D3DTBLEND_MODULATEALPHA 4
#define D3DTBLEND_DECALMASK     5
#define D3DTBLEND_MODULATEMASK  6
#define D3DTBLEND_COPY          7
#define D3DTBLEND_ADD           8

#endif /* VGPU98_DLL */

#endif /* VGPU98_DDI_H */

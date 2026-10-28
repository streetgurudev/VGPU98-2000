/*
 * vgpu98dx.dll: the DirectDraw callbacks (spec/directx-hal.md). Each turns
 * a DirectDraw request into GPU packets in the ring, or tells DirectDraw to
 * do it itself (DDHAL_DRIVER_NOTHANDLED).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3d.h"

/* --- Surfaces ---------------------------------------------------------------- */

static int
is_texture_format(DWORD f)
{
    return f && !((f >= VGPU98_FMT_D16) && (f <= VGPU98_FMT_D24S8));
}

static int
is_target_format(DWORD f)
{
    return (f == VGPU98_FMT_R5G6B5) || (f == VGPU98_FMT_X1R5G5B5) || (f == VGPU98_FMT_X8R8G8B8) ||
           (f == VGPU98_FMT_A8R8G8B8) || (d3d8_on && (f == VGPU98_FMT_A1R5G5B5)); /* command set 2.1 */
}

/* A volume texture's level (DirectX 8): its depth, the low word of
   ddsCapsEx.dwCaps4. */
static DWORD
volume_depth(LPSURF s)
{
    const DWORD d = (s->lpSurfMore && (s->lpSurfMore->ddsCapsEx.dwCaps2 & DDSCAPS2_VOLUME))
                        ? (s->lpSurfMore->ddsCapsEx.dwCaps4 & 0xffff)
                        : 0;

    return d ? d : 1;
}

static int
is_volume(LPSURF s)
{
    return s->lpSurfMore && (s->lpSurfMore->ddsCapsEx.dwCaps2 & DDSCAPS2_VOLUME);
}

GUARDED(static, CanCreateSurface, DDHAL_CANCREATESURFACEDATA, ddRVal)
{
    const DDSURFACEDESC *desc = d->lpDDSurfaceDesc;
    const DWORD          caps = desc->ddsCaps.dwCaps;
    DWORD                f    = 0;
    int                  ok   = 1;

    /* The flight recorder's figures: the caps, and the format's FourCC or
       bits. */
    flight_figures(caps, !(desc->dwFlags & DDSD_PIXELFORMAT) ? 0
                         : desc->ddpfPixelFormat.dwFourCC ? desc->ddpfPixelFormat.dwFourCC
                                                          : desc->ddpfPixelFormat.dwRGBBitCount);
    if (caps & DDSCAPS_SYSTEMMEMORY) {
        d->ddRVal = DD_OK;
        return DDHAL_DRIVER_HANDLED;
    }
    if (caps & DDSCAPS_ZBUFFER) {
        if (desc->dwFlags & DDSD_PIXELFORMAT)
            f = fmt_from_ddpf(&desc->ddpfPixelFormat);
        else if (desc->dwFlags & DDSD_ZBUFFERBITDEPTH)
            f = (desc->dwMipMapCount == 16) ? VGPU98_FMT_D16 : (desc->dwMipMapCount == 32) ? VGPU98_FMT_D24X8 : 0;
        ok = (f >= VGPU98_FMT_D16) && (f <= VGPU98_FMT_D24S8);
    } else if (d->bIsDifferentPixelFormat) {
        f = fmt_from_ddpf(&desc->ddpfPixelFormat);
        if (caps & DDSCAPS_TEXTURE)
            ok = is_texture_format(f);
        else if (caps & DDSCAPS_3DDEVICE)
            ok = is_target_format(f);
        else
            ok = f && !fmt_is_dxt(f) && !((f >= VGPU98_FMT_D16) && (f <= VGPU98_FMT_D24S8));
    }
    d->ddRVal = ok ? DD_OK : DDERR_INVALIDPIXELFORMAT;
    /* A refusal with the pixel format as the runtime described it (DirectX
       8.1's formats come by masks the DDK does not name): with the trace
       every one, else each of the first 16 distinct formats a session at
       any Diagnostics level but Minimal (every process's). */
    if (!ok && diag_on) {
        const DDPIXELFORMAT *pf  = &desc->ddpfPixelFormat;
        const DWORD          sig = ((pf->dwFlags * 0x9E3779B1u) ^ pf->dwFourCC ^ (pf->dwRGBBitCount << 24) ^ pf->dwRBitMask ^
                                    (pf->dwGBitMask * 3u) ^ (pf->dwBBitMask * 5u) ^ (pf->dwRGBAlphaBitMask * 7u)) | 1u;
        static DWORD         seen[16];
        static DWORD         n_seen;
        int                  tell = TRACE(TRACE_SURFACE);
        DWORD                i;

        if (!tell) {
            for (i = 0; (i < n_seen) && (i < ARRAY_SIZE(seen)) && (seen[i] != sig); i++)
                ;
            if ((i == n_seen) && ((i = __atomic_fetch_add(&n_seen, 1, __ATOMIC_RELAXED)) < ARRAY_SIZE(seen))) {
                seen[i] = sig;
                tell    = 1;
            }
        }
        if (tell)
            dbg(DLL_TAG ": CanCreateSurface refused caps %x, format %x (flags %x, FourCC %x, %u bits, masks %x %x %x %x)", caps, f,
                pf->dwFlags, pf->dwFourCC, pf->dwRGBBitCount, pf->dwRBitMask, pf->dwGBitMask, pf->dwBBitMask, pf->dwRGBAlphaBitMask);
    }
    return DDHAL_DRIVER_HANDLED;
}

/* DXT surfaces have no pitch DirectDraw can multiply by their height: the
   driver asks for a linear block of their compressed size. So do formats
   DirectX 8 describes by a FourCC (Q8W8V8U8, V16U16), whose pitch DirectDraw does
   not know, and volume textures (DirectX 8), whose slices stack below their
   first: rows a dword apart, slices the rows' height apart (the slice pitch
   goes into dwBlockSizeY once DirectDraw has allocated them: CreateSurfaceEx,
   d3d7.c), in their own format or, with none (DDRAWISURF_HASPIXELFORMAT is
   set only when it differs), the display's. Everything else DirectDraw
   allocates itself. */
GUARDED(static, CreateSurface, DDHAL_CREATESURFACEDATA, ddRVal)
{
    DWORD i;

    if (d->dwSCnt)
        flight_figures(d->lplpSList[0]->ddsCaps.dwCaps, d->dwSCnt);
    surf_epoch++;
    world_moved();
    for (i = 0; i < d->dwSCnt; i++) {
        LPSURF                   s   = d->lplpSList[i];
        DDRAWI_DDRAWSURFACE_GBL *gbl = s->lpGbl;
        const int                vol = is_volume(s) || is_volume(d->lplpSList[0]);
        DWORD                    f;

        gbl->dwReserved1 = 0;
        s->dwReserved1   = 0;
        if (!(s->dwFlags & DDRAWISURF_HASPIXELFORMAT) && !vol)
            continue;
        f = surf_format(s);
        if (fmt_is_dxt(f)) {
            DWORD size = ((gbl->wWidth + 3) / 4) * ((gbl->wHeight + 3) / 4) * ((f == VGPU98_FMT_DXT1) ? 8 : 16);

            gbl->fpVidMem     = DDHAL_PLEASEALLOC_BLOCKSIZE;
            gbl->dwBlockSizeX = size;
            gbl->dwBlockSizeY = 1;
            gbl->lPitch       = (LONG) size; /* dwLinearSize */
        } else if (f && (((s->dwFlags & DDRAWISURF_HASPIXELFORMAT) && (gbl->ddpfSurface.dwFlags & DDPF_FOURCC) && d3d8_on) ||
                         vol)) {
            /* A level's depth: its own, or the top level's halved, the
               larger (the list holds the levels from the top). */
            const DWORD top   = volume_depth(d->lplpSList[0]) >> i;
            const DWORD depth = (volume_depth(s) > top) ? volume_depth(s) : (top ? top : 1);
            const DWORD pitch = (gbl->wWidth * fmt_bytes(f) + 3) & ~3u;

            gbl->fpVidMem     = DDHAL_PLEASEALLOC_BLOCKSIZE;
            gbl->dwBlockSizeX = pitch * gbl->wHeight * depth;
            gbl->dwBlockSizeY = 1;
            gbl->lPitch       = (LONG) pitch;
            if (vol)
                s->dwReserved1 = SURF_SLICES;
        }
    }
    if (d->dwSCnt && TRACE(TRACE_SURFACE)) {
        LPSURF s0 = d->lplpSList[0];

        dbg(DLL_TAG " trace: CreateSurface %s, %s memory, format %x/%x, %ux%ux%u, %u surfaces, %s layout",
            trace_kind(s0), trace_where(s0), (s0->dwFlags & DDRAWISURF_HASPIXELFORMAT) ? fmt_from_ddpf(&s0->lpGbl->ddpfSurface) : 0,
            (s0->dwFlags & DDRAWISURF_HASPIXELFORMAT) ? s0->lpGbl->ddpfSurface.dwFlags : 0, s0->lpGbl->wWidth, s0->lpGbl->wHeight,
            volume_depth(s0), d->dwSCnt, (s0->lpGbl->fpVidMem == DDHAL_PLEASEALLOC_BLOCKSIZE) ? "the DLL's" : "DirectDraw's");
    }
    d->ddRVal = DD_OK;
    return DDHAL_DRIVER_NOTHANDLED;
}

/* The surface's bytes are about to belong to something else: the GPU drops
   its copies of them, and nothing still using the surface remains. */
GUARDED_FIG(static, DestroySurface, DDHAL_DESTROYSURFACEDATA, ddRVal, d->lpDDSurface, 0)
{
    gsurf_t g;
    DWORD   last;

    surf_epoch++;
    world_moved();
    if (shadow_on)
        shadow_forget(d->lpDDSurface);
    /* Its last use, from its record as the record goes. */
    last = d3d7_surface_gone(d->lpDDSurface);
    if (surf_describe(d->lpDDSurface, &g) && (gpu_begin() == DD_OK)) {
        volatile DWORD *p = gpu_packet(VGPU98_OP_DISCARD, VGPU98_LEN_DISCARD);

        if (p) {
            pkt_put(&p, g.offset);
            pkt_put(&p, surf_bytes(&g));
        }
        gpu_end();
        gpu_wait(last);
    }
    d->ddRVal = DD_OK;
    return DDHAL_DRIVER_NOTHANDLED;
}

/* A surface on the screen or flipping is busy until the last flip took effect. */
static int
flip_busy(LPSURF s)
{
    return (s->ddsCaps.dwCaps & (DDSCAPS_FLIP | DDSCAPS_PRIMARYSURFACE | DDSCAPS_FRONTBUFFER | DDSCAPS_BACKBUFFER)) &&
           !gpu_fence_done(sb->fence_flip);
}

/* Lock: the CPU must not write while a packet that uses the surface is
   pending, and sees the GPU's results once SYNC and a FENCE complete. A
   surface the GPU never drew into (most textures), outside the flipping
   chain, needs only its last use done: the host copies a texture when it
   processes a draw that reads it, and 2D blits write video memory itself.
   Then DirectDraw computes the pointer itself. The fence and the flag are
   the surface's record's (a DirectX 8 runtime's surface: the DLL's copy). */
static DWORD
lock(DDHAL_LOCKDATA *d)
{
    LPSURF       s   = d->lpDDSurface;
    const LPSURF own = surf_own(s);
    gsurf_t      g;
    DWORD        fence;
    HRESULT      hr;

    if (!surf_describe(s, &g))
        return DDHAL_DRIVER_NOTHANDLED;
    if (!(own->dwReserved1 & SURF_GPU_WRITES) &&
        !(s->ddsCaps.dwCaps & (DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_FRONTBUFFER | DDSCAPS_BACKBUFFER))) {
        const DWORD last = own->lpGbl->dwReserved1;

        if (gpu_held()) {
            d->ddRVal = DDERR_SURFACELOST;
            return DDHAL_DRIVER_HANDLED;
        }
        /* A texture DISCARDCONTENTS-Locked frame after frame: written
           through a copy in main memory, sent at the Unlock after the
           queued draws that read it, so nothing waits for them. */
        if (shadow_on && (s->ddsCaps.dwCaps & DDSCAPS_TEXTURE) && (d->dwFlags & DDLOCK_DISCARDCONTENTS) &&
            !(d->dwFlags & DDLOCK_READONLY) && shadow_hot(own) && shadow_lock(d, own, &g, SHADOW_TEXTURE)) {
            DIAG({
                if (d->ddRVal == DD_OK) {
                    diag.locks++;
                    diag.locks_tex++;
                    busy_ok(BUSY_LOCK);
                }
            });
            if (diag_textures && (d->ddRVal == DD_OK))
                tt_lock(s, d, 0);
            return DDHAL_DRIVER_HANDLED;
        }
        if (!gpu_fence_done(last)) {
            if (!(d->dwFlags & DDLOCK_WAIT)) {
                gpu_kick();
                if (!gpu_fence_done(last)) {
                    DIAG(busy_answer(BUSY_LOCK));
                    d->ddRVal = DDERR_WASSTILLDRAWING;
                    return DDHAL_DRIVER_HANDLED;
                }
            } else if ((hr = gpu_wait(last)) != DD_OK) {
                d->ddRVal = hr;
                return DDHAL_DRIVER_HANDLED;
            }
        }
        DIAG({
            /* A Lock given (still-drawing answers are not counted). */
            diag.locks++;
            diag.locks_tex += (s->ddsCaps.dwCaps & DDSCAPS_TEXTURE) != 0;
            diag.locks_direct++;
            busy_ok(BUSY_LOCK);
        });
        if (diag_textures)
            tt_lock(s, d, 1);
        return DDHAL_DRIVER_NOTHANDLED;
    }
    if (!(d->dwFlags & DDLOCK_WAIT)) {
        /* Still in use: say so without queuing anything; the caller asks
           again. The SYNC below is short once the surface is idle. */
        gpu_kick();
        if (!gpu_held() && (!gpu_fence_done(own->lpGbl->dwReserved1) || flip_busy(s))) {
            DIAG(busy_answer(BUSY_LOCK));
            d->ddRVal = DDERR_WASSTILLDRAWING;
            return DDHAL_DRIVER_HANDLED;
        }
    }
    /* A surface the GPU drew into: read and written through a copy in main
       memory, which DOWNLOAD fills and the Unlock sends back. */
    if (shadow_on && (own->dwReserved1 & SURF_GPU_WRITES)) {
        if (flip_busy(s) && ((hr = gpu_wait(sb->fence_flip)) != DD_OK)) {
            d->ddRVal = hr;
            return DDHAL_DRIVER_HANDLED;
        }
        if (shadow_lock(d, own, &g, SHADOW_GPU)) {
            DIAG({
                if (d->ddRVal == DD_OK) {
                    diag.locks++;
                    diag.locks_tex += (s->ddsCaps.dwCaps & DDSCAPS_TEXTURE) != 0;
                    diag.locks_sync++;
                    busy_ok(BUSY_LOCK);
                }
            });
            if (diag_textures && (d->ddRVal == DD_OK))
                tt_lock(s, d, 0);
            return DDHAL_DRIVER_HANDLED;
        }
    }
    hr = gpu_begin();
    if (hr != DD_OK) {
        d->ddRVal = hr;
        return DDHAL_DRIVER_HANDLED;
    }
    {
        volatile DWORD *p = gpu_packet(VGPU98_OP_SYNC, VGPU98_LEN_SYNC);

        if (p) {
            pkt_put(&p, g.offset);
            pkt_put(&p, surf_bytes(&g));
        }
    }
    fence = gpu_end();
    hr    = gpu_wait(fence);
    if ((hr == DD_OK) && flip_busy(s))
        hr = gpu_wait(sb->fence_flip);
    if (hr != DD_OK) {
        d->ddRVal = hr;
        return DDHAL_DRIVER_HANDLED;
    }
    DIAG({
        diag.locks++;
        diag.locks_tex += (s->ddsCaps.dwCaps & DDSCAPS_TEXTURE) != 0;
        diag.locks_sync++;
        busy_ok(BUSY_LOCK);
    });
    if (diag_textures)
        tt_lock(s, d, 0);
    return DDHAL_DRIVER_NOTHANDLED;
}

GUARDED_FIG(static, Lock, DDHAL_LOCKDATA, ddRVal, d->lpDDSurface, d->lpSurfData)
{
    const uint64_t t0 = diag_on ? rdtsc() : 0;
    const DWORD    r  = lock(d);

    DIAG(diag.cyc_lock += rdtsc() - t0);
    return r;
}

/* Unlock: a Lock through a copy in main memory sends the copy back
   (shadow.c); DirectDraw does the rest of the Unlock itself. */
GUARDED(static, Unlock, DDHAL_UNLOCKDATA, ddRVal)
{
    (void) shadow_unlock(d);
    d->ddRVal = DD_OK;
    return DDHAL_DRIVER_NOTHANDLED;
}

/* Frames shown so far, by FLIP or presenting blit (shadow_hot's frames). */
static DWORD frames_shown;

DWORD
ddraw_frames(void)
{
    return frames_shown;
}

/* --- Blt --------------------------------------------------------------------- */

static void
put_rect(volatile DWORD **p, const RECTL *r)
{
    pkt_put(p, VGPU98_XY(r->left, r->top));
    pkt_put(p, VGPU98_XY(r->right, r->bottom));
}

static int
rect_ok(const RECTL *r, const gsurf_t *g)
{
    return (r->left >= 0) && (r->top >= 0) && (r->left <= r->right) && (r->top <= r->bottom) &&
           ((DWORD) r->right <= g->width) && ((DWORD) r->bottom <= g->height);
}

/* The source rectangle for one clipped piece of a (possibly stretched,
   possibly mirrored) blit. */
static void
clip_source(const RECTL *od, const RECTL *os, const RECTL *cd, int mx, int my, RECTL *cs)
{
    const LONG dw = od->right - od->left, dh = od->bottom - od->top;
    const LONG sw = os->right - os->left, sh = os->bottom - os->top;
    LONG       x0 = (LONG) ((int64_t) (cd->left - od->left) * sw / dw);
    LONG       x1 = (LONG) ((int64_t) (cd->right - od->left) * sw / dw);
    LONG       y0 = (LONG) ((int64_t) (cd->top - od->top) * sh / dh);
    LONG       y1 = (LONG) ((int64_t) (cd->bottom - od->top) * sh / dh);

    cs->left   = mx ? os->right - x1 : os->left + x0;
    cs->right  = mx ? os->right - x0 : os->left + x1;
    cs->top    = my ? os->bottom - y1 : os->top + y0;
    cs->bottom = my ? os->bottom - y0 : os->top + y1;
}

/* System memory to video memory, as DDCAPS_CANBLTSYSMEM offers: a plain
   copy of the same size goes through the ring (UPLOAD) instead of the CPU
   writing the card's memory, in the same format or decompressed from DXT.
   DirectDraw sends every system-to-video blit the core caps' SVB fields
   allow here (colour keys and stretching they do not, so its emulation does
   those); one answered NOTHANDLED fails with DDERR_UNSUPPORTED. */
static DWORD
blt_sysmem(DDHAL_BLTDATA *d, const gsurf_t *gd)
{
    LPSURF         src = d->lpDDSrcSurface;
    const RECTL   *rects;
    DWORD          nrects;
    DWORD          i;
    DWORD          fence;
    DWORD          sent = 0;
    const uint8_t *mem;
    gsurf_t        gs;
    HRESULT        hr;
    int            ok = 1;
    int            dxt;

    if (d->dwFlags & ~(DDBLT_ROP | DDBLT_WAIT | DDBLT_ASYNC | DDBLT_DONOTWAIT))
        return DDHAL_DRIVER_NOTHANDLED;
    if ((d->dwFlags & DDBLT_ROP) && (d->bltFX.dwROP != SRCCOPY))
        return DDHAL_DRIVER_NOTHANDLED;
    if (!surf_describe_sys(src, &gs, &mem))
        return DDHAL_DRIVER_NOTHANDLED;
    /* The same format, or DXT to a 16- or 32-bit colour format
       (decompressed, in strips at most DXT_STRIP texels wide). */
    dxt = fmt_is_dxt(gs.format) && !fmt_is_dxt(gd->format);
    if ((gs.format != gd->format) && !dxt)
        return DDHAL_DRIVER_NOTHANDLED;
    if (dxt && (!argb_bytes(gd->format) || (d->rDest.right - d->rDest.left > DXT_STRIP)))
        return DDHAL_DRIVER_NOTHANDLED;
    if (!rect_ok(&d->rSrc, &gs) || !rect_ok(&d->rDest, gd) || (d->rSrc.right - d->rSrc.left != d->rDest.right - d->rDest.left) ||
        (d->rSrc.bottom - d->rSrc.top != d->rDest.bottom - d->rDest.top))
        return DDHAL_DRIVER_NOTHANDLED;
    if (d->IsClipped) {
        rects  = d->prDestRects;
        nrects = d->dwRectCnt;
    } else {
        rects  = &d->rDest;
        nrects = 1;
    }
    hr = gpu_begin();
    if (hr != DD_OK) {
        d->ddRVal = hr;
        return DDHAL_DRIVER_HANDLED;
    }
    for (i = 0; ok && (i < nrects); i++) {
        const RECTL *od = d->IsClipped ? &d->rOrigDest : &d->rDest;
        const RECTL *os = d->IsClipped ? &d->rOrigSrc : &d->rSrc;
        const RECTL *cd = &rects[i];
        RECTL        cs;

        if (!rect_ok(cd, gd) || (cd->left == cd->right) || (cd->top == cd->bottom))
            continue;
        cs.left   = os->left + (cd->left - od->left);
        cs.top    = os->top + (cd->top - od->top);
        cs.right  = cs.left + (cd->right - cd->left);
        cs.bottom = cs.top + (cd->bottom - cd->top);
        if (!rect_ok(&cs, &gs))
            continue;
        sent += dxt ? upload_rect_dxt(gd, cd, &gs, mem, &cs, &ok) : upload_rect(gd, cd, &gs, mem, &cs, &ok);
    }
    fence = gpu_end();
    surf_used(surf_own(d->lpDDDestSurface), fence);
    DIAG({
        diag.sys_blts++;
        diag.sys_blt_dxt += dxt;
        diag.sys_blt_bytes += sent;
    });
    d->ddRVal = DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

/* DirectX 8's presentation blits (a windowed Present: each of its blits
   flagged DDBLT_PRESENTATION, the last DDBLT_LAST_PRESENTATION too): at most
   three frames queued per source surface, the runtime's rule; another waits
   (DDBLT_WAIT) or answers busy until the oldest is done. */
#define PRESENT_SOURCES 8
#define PRESENT_FRAMES  3

typedef struct present {
    const void *src;    /* the source's DirectDraw object (with its handle), else its DDRAWI_DDRAWSURFACE_GBL */
    DWORD       handle;
    DWORD       fence[PRESENT_FRAMES];
    DWORD       next;
    DWORD       used; /* present_clock at its last frame */
} present_t;

static present_t presents[PRESENT_SOURCES];
static DWORD     present_clock;

static present_t *
present_of(LPSURF src)
{
    /* By its handle where it has one: a DirectX 8 runtime's record of it may
       last one call. */
    const DWORD handle = src->lpSurfMore ? src->lpSurfMore->dwSurfaceHandle : 0;
    const void *key    = handle ? src->lpSurfMore->lpDD_lcl : (const void *) src->lpGbl;
    present_t  *p      = &presents[0];
    DWORD       i;

    for (i = 0; i < PRESENT_SOURCES; i++) {
        if ((presents[i].src == key) && (presents[i].handle == handle))
            return &presents[i];
        if (presents[i].used < p->used)
            p = &presents[i];
    }
    memset(p, 0, sizeof(*p));
    p->src    = key;
    p->handle = handle;
    p->used   = present_clock;
    return p;
}

static DWORD
blt(DDHAL_BLTDATA *d)
{
    LPSURF        dst   = d->lpDDDestSurface;
    LPSURF        src   = d->lpDDSrcSurface;
    const DWORD   flags = d->dwFlags;
    present_t    *pr    = NULL;
    gsurf_t       gd;
    gsurf_t       gs;
    const RECTL  *rects;
    DWORD         nrects;
    DWORD         i;
    DWORD         fence;
    HRESULT       hr;
    int           fill = (flags & (DDBLT_COLORFILL | DDBLT_DEPTHFILL)) != 0;

    if (!surf_describe(dst, &gd))
        return DDHAL_DRIVER_NOTHANDLED;
    if (fmt_is_dxt(gd.format))
        return DDHAL_DRIVER_NOTHANDLED;
    if (!fill && src && (src->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY))
        return blt_sysmem(d, &gd);
    if (!fill) {
        if (!src || !surf_describe(src, &gs))
            return DDHAL_DRIVER_NOTHANDLED;
        /* A back buffer with alpha onto the display's format without it (a
           DirectX 8 present with D3DSWAPEFFECT_COPY, so with the DirectX 8
           interface only): the same bytes, read as the destination's
           format; colour keys only between the same format. */
        if ((gs.format != gd.format) && (!d3d8_on || (flags & (DDBLT_KEYSRC | DDBLT_KEYSRCOVERRIDE | DDBLT_KEYDEST | DDBLT_KEYDESTOVERRIDE)) ||
             !(((gs.format == VGPU98_FMT_A8R8G8B8) && (gd.format == VGPU98_FMT_X8R8G8B8)) ||
               ((gs.format == VGPU98_FMT_A1R5G5B5) && (gd.format == VGPU98_FMT_X1R5G5B5)))))
            return DDHAL_DRIVER_NOTHANDLED;
        gs.format = gd.format;
        if ((flags & DDBLT_ROP) && (d->bltFX.dwROP != SRCCOPY))
            return DDHAL_DRIVER_NOTHANDLED;
        if (!rect_ok(&d->rSrc, &gs))
            return DDHAL_DRIVER_NOTHANDLED;
    }
    if (!rect_ok(&d->rDest, &gd))
        return DDHAL_DRIVER_NOTHANDLED;
    if (d->IsClipped) {
        rects  = d->prDestRects;
        nrects = d->dwRectCnt;
    } else {
        rects  = &d->rDest;
        nrects = 1;
    }
    if (!fill && (flags & DDBLT_PRESENTATION)) {
        DWORD oldest;

        pr     = present_of(src);
        oldest = pr->fence[pr->next];
        if (oldest && !gpu_fence_done(oldest)) {
            gpu_kick();
            if (!gpu_fence_done(oldest)) {
                DIAG(diag.present_waits++);
                if (!(flags & DDBLT_WAIT)) {
                    DIAG(busy_answer(BUSY_BLT));
                    d->ddRVal = DDERR_WASSTILLDRAWING;
                    return DDHAL_DRIVER_HANDLED;
                }
                if ((hr = gpu_wait(oldest)) != DD_OK) {
                    d->ddRVal = hr;
                    return DDHAL_DRIVER_HANDLED;
                }
            }
        }
        DIAG(busy_ok(BUSY_BLT));
    }

    hr = gpu_begin();
    if (hr != DD_OK) {
        d->ddRVal = hr;
        return DDHAL_DRIVER_HANDLED;
    }
    for (i = 0; i < nrects; i++) {
        const RECTL    *cd = &rects[i];
        volatile DWORD *p;

        if (!rect_ok(cd, &gd) || (cd->left == cd->right) || (cd->top == cd->bottom))
            continue;
        if (fill) {
            p = gpu_packet(VGPU98_OP_FILL, VGPU98_LEN_FILL);
            if (!p)
                break;
            pkt_surf(&p, &gd);
            put_rect(&p, cd);
            pkt_put(&p, d->bltFX.dwFillColor);
        } else {
            const int mx = (flags & DDBLT_DDFX) && (d->bltFX.dwDDFX & DDBLTFX_MIRRORLEFTRIGHT);
            const int my = (flags & DDBLT_DDFX) && (d->bltFX.dwDDFX & DDBLTFX_MIRRORUPDOWN);
            const RECTL *od = d->IsClipped ? &d->rOrigDest : &d->rDest;
            const RECTL *os = d->IsClipped ? &d->rOrigSrc : &d->rSrc;
            RECTL        cs;
            DWORD        bflags = 0;
            DWORD        skey   = 0;
            DWORD        dkey   = 0;

            if ((od->right == od->left) || (od->bottom == od->top))
                continue;
            if (d->IsClipped)
                clip_source(od, os, cd, mx, my, &cs);
            else
                cs = *os;
            if (!rect_ok(&cs, &gs) || (cs.left == cs.right) || (cs.top == cs.bottom))
                continue;
            if (flags & DDBLT_KEYSRCOVERRIDE) {
                bflags |= VGPU98_BLT_SRCKEY;
                skey = d->bltFX.ddckSrcColorkey.dwColorSpaceLowValue;
            } else if (flags & DDBLT_KEYSRC) {
                bflags |= VGPU98_BLT_SRCKEY;
                skey = src->ddckCKSrcBlt.dwColorSpaceLowValue;
            }
            if (flags & DDBLT_KEYDESTOVERRIDE) {
                bflags |= VGPU98_BLT_DSTKEY;
                dkey = d->bltFX.ddckDestColorkey.dwColorSpaceLowValue;
            } else if (flags & DDBLT_KEYDEST) {
                bflags |= VGPU98_BLT_DSTKEY;
                dkey = dst->ddckCKDestBlt.dwColorSpaceLowValue;
            }
            if (mx)
                bflags |= VGPU98_BLT_MIRROR_X;
            if (my)
                bflags |= VGPU98_BLT_MIRROR_Y;
            p = gpu_packet(VGPU98_OP_BLT, VGPU98_LEN_BLT);
            if (!p)
                break;
            pkt_surf(&p, &gs);
            put_rect(&p, &cs);
            pkt_surf(&p, &gd);
            put_rect(&p, cd);
            pkt_put(&p, bflags);
            pkt_put(&p, skey);
            pkt_put(&p, dkey);
        }
    }
    fence = gpu_end();
    surf_used(surf_own(dst), fence);
    if (!fill)
        surf_used(surf_own(src), fence);
    if (pr && (flags & DDBLT_LAST_PRESENTATION)) {
        DIAG(diag.frames++);
        pr->fence[pr->next] = fence;
        pr->next            = (pr->next + 1) % PRESENT_FRAMES;
        frames_shown++;
        pr->used            = ++present_clock;
    }
    d->ddRVal = DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

GUARDED_FIG(static, Blt, DDHAL_BLTDATA, ddRVal, d->lpDDDestSurface, d->dwFlags)
{
    const uint64_t t0 = diag_on ? rdtsc() : 0;
    const DWORD    r  = blt(d);

    DIAG(diag.cyc_blt += rdtsc() - t0);
    return r;
}

/* --- Flipping ------------------------------------------------------------------ */

static HRESULT
flip_to(DWORD offset, DWORD flags)
{
    volatile DWORD *p;
    HRESULT         hr = gpu_begin();

    if (hr != DD_OK)
        return hr;
    p = gpu_packet(VGPU98_OP_FLIP, VGPU98_LEN_FLIP);
    if (p) {
        pkt_put(&p, offset);
        pkt_put(&p, flags);
    }
    /* Under the ring lock, with the FLIP: a flush (ctx_target_follow) sees
       both or neither. */
    sb->fence_flip = gpu_finish();
    sb->flip_start = offset;
    gpu_release();
    frames_shown++;
    return DD_OK;
}

GUARDED_FIG(static, Flip, DDHAL_FLIPDATA, ddRVal, d->lpSurfCurr, d->lpSurfTarg)
{
    gsurf_t g;
    DWORD   flags = 0;
    DWORD   interval;

    gpu_kick();
    if (!gpu_fence_done(sb->fence_flip)) {
        DIAG({
            busy_answer(BUSY_FLIP);
            if (!diag.flip_since)
                diag.flip_since = rdtsc();
        });
        d->ddRVal = DDERR_WASSTILLDRAWING;
        return DDHAL_DRIVER_HANDLED;
    }
    DIAG(busy_ok(BUSY_FLIP));
    if (!surf_describe(d->lpSurfTarg, &g) || (g.pitch != sb->mode_pitch)) {
        d->ddRVal = DDERR_INVALIDPARAMS;
        return DDHAL_DRIVER_HANDLED;
    }
    if (d->dwFlags & DDFLIP_NOVSYNC)
        flags |= VGPU98_FLIP_IMMEDIATE;
    interval = (d->dwFlags >> 24) & 0x0f;
    if ((interval >= 2) && (interval <= 4))
        flags |= interval << VGPU98_FLIP_INTERVAL_SHIFT;
    d->ddRVal = flip_to(g.offset, flags);
    DIAG({
        if (diag.flip_since) {
            diag.flip_cycles += rdtsc() - diag.flip_since;
            diag.flip_since = 0;
        }
    });
    if (d->ddRVal == DD_OK) {
        DIAG(diag.frames++);
        surf_used(surf_own(d->lpSurfTarg), sb->fence_flip);
        if (d->lpSurfCurr)
            surf_used(surf_own(d->lpSurfCurr), sb->fence_flip);
        /* DirectDraw moves the chain's memory once this returns. */
        world_moved();
    }
    return DDHAL_DRIVER_HANDLED;
}

/* DirectDraw sets colour keys and attaches surfaces itself (NOTHANDLED);
   the DLL is told only so that contexts check their state again
   (world_moved): of the keys, a texture's source key counts. */
GUARDED(static, SetColorKey, DDHAL_SETCOLORKEYDATA, ddRVal)
{
    if (d->dwFlags & DDCKEY_SRCBLT)
        world_moved();
    d->ddRVal = DD_OK;
    return DDHAL_DRIVER_NOTHANDLED;
}

GUARDED(static, AddAttachedSurface, DDHAL_ADDATTACHEDSURFACEDATA, ddRVal)
{
    world_moved();
    d->ddRVal = DD_OK;
    return DDHAL_DRIVER_NOTHANDLED;
}

GUARDED(static, GetFlipStatus, DDHAL_GETFLIPSTATUSDATA, ddRVal)
{
    gpu_kick();
    d->ddRVal = gpu_fence_done(sb->fence_flip) ? DD_OK : DDERR_WASSTILLDRAWING;
    DIAG({
        if (d->ddRVal != DD_OK)
            busy_answer(BUSY_FLIPSTATUS);
        else
            busy_ok(BUSY_FLIPSTATUS);
    });
    return DDHAL_DRIVER_HANDLED;
}

GUARDED(static, GetBltStatus, DDHAL_GETBLTSTATUSDATA, ddRVal)
{
    gpu_kick();
    if (d->dwFlags & DDGBS_CANBLT)
        d->ddRVal = (gpu_ring_free() >= 4096) ? DD_OK : DDERR_WASSTILLDRAWING;
    else
        d->ddRVal = gpu_fence_done(surf_own(d->lpDDSurface)->lpGbl->dwReserved1) ? DD_OK : DDERR_WASSTILLDRAWING;
    DIAG({
        if (d->ddRVal != DD_OK)
            busy_answer(BUSY_BLTSTATUS);
        else
            busy_ok(BUSY_BLTSTATUS);
    });
    return DDHAL_DRIVER_HANDLED;
}

GUARDED(static, FlipToGDISurface, DDHAL_FLIPTOGDISURFACEDATA, ddRVal)
{
    d->ddRVal = DD_OK;
    if (sb->flip_start != 0) {
        if (flip_to(0, 0) == DD_OK)
            gpu_wait(sb->fence_flip);
    }
    world_moved();
    return DDHAL_DRIVER_HANDLED;
}

/* --- Display timing -------------------------------------------------------- */

GUARDED(static, WaitForVerticalBlank, DDHAL_WAITFORVERTICALBLANKDATA, ddRVal)
{
    DWORD start = GetTickCount();

    d->ddRVal = DD_OK;
    if (d->dwFlags == DDWAITVB_I_TESTVB) {
        d->bIsInVB = (reg_read(VGPU98_REG_SCANLINE) & VGPU98_SCANLINE_VBLANK) != 0;
        return DDHAL_DRIVER_HANDLED;
    }
    if (d->dwFlags & DDWAITVB_BLOCKBEGIN) {
        DWORD v = gpu_vblank_count();

        while ((gpu_vblank_count() == v) && (GetTickCount() - start < 200))
            Sleep(0);
    } else if (d->dwFlags & DDWAITVB_BLOCKEND) {
        while (!(reg_read(VGPU98_REG_SCANLINE) & VGPU98_SCANLINE_VBLANK) && (GetTickCount() - start < 200))
            Sleep(0);
        while ((reg_read(VGPU98_REG_SCANLINE) & VGPU98_SCANLINE_VBLANK) && (GetTickCount() - start < 200))
            Sleep(0);
    }
    return DDHAL_DRIVER_HANDLED;
}

GUARDED(static, GetScanLine, DDHAL_GETSCANLINEDATA, ddRVal)
{
    DWORD s = reg_read(VGPU98_REG_SCANLINE);

    if (s & VGPU98_SCANLINE_VBLANK)
        d->ddRVal = DDERR_VERTICALBLANKINPROGRESS;
    else {
        d->dwScanLine = s & 0xffff;
        d->ddRVal     = DD_OK;
    }
    return DDHAL_DRIVER_HANDLED;
}

/* --- GetDriverInfo ------------------------------------------------------------- */

static const GUID guid_callbacks2  = { 0x0ba584e1, 0x70b6, 0x11d0, { 0x88, 0x9d, 0x00, 0xaa, 0x00, 0xbb, 0xb7, 0x6a } };
static const GUID guid_callbacks3  = { 0xddf41230, 0xec0a, 0x11d0, { 0xa9, 0xb6, 0x00, 0xaa, 0x00, 0xc0, 0x99, 0x3e } };
static const GUID guid_extcaps     = { 0x7de41f80, 0x9d93, 0x11d0, { 0x89, 0xab, 0x00, 0xa0, 0xc9, 0x05, 0x41, 0x29 } };
static const GUID guid_zformats    = { 0x93869880, 0x36cf, 0x11d1, { 0x9b, 0x1b, 0x00, 0xaa, 0x00, 0xbb, 0xb8, 0xae } };
static const GUID guid_parse       = { 0x2e04ffa0, 0x98e4, 0x11d1, { 0x8c, 0xe1, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8 } };
static const GUID guid_misc2       = { 0x406b2f00, 0x3e5a, 0x11d1, { 0xb6, 0x40, 0x00, 0xaa, 0x00, 0xa1, 0xf9, 0x6a } };
static const GUID guid_more_caps   = { 0x3b8a0466, 0xf269, 0x11d1, { 0x88, 0x0b, 0x00, 0xc0, 0x4f, 0xd9, 0x30, 0xc5 } };
/* GUID_DDStereoMode, which GetDriverInfo2 shares (DirectX 8) */
static const GUID guid_stereo      = { 0xf828169c, 0xa8e8, 0x11d2, { 0xa1, 0xf2, 0x00, 0xa0, 0xc9, 0x83, 0xea, 0xf6 } };

static int
guid_eq(const GUID *a, const GUID *b)
{
    return memcmp(a, b, sizeof(GUID)) == 0;
}

static void
answer(DDHAL_GETDRIVERINFODATA *d, void *table, DWORD size)
{
    DWORD n = (d->dwExpectedSize < size) ? d->dwExpectedSize : size;

    *(DWORD *) table = n; /* the table's own dwSize */
    memcpy(d->lpvData, table, n);
    *(DWORD *) table = size;
    d->dwActualSize = n;
    d->ddRVal       = DD_OK;
}

GUARDED_FIG(, GetDriverInfo, DDHAL_GETDRIVERINFODATA, ddRVal, d->guidInfo.Data1, d->dwActualSize)
{
    d->ddRVal = DDERR_CURRENTLYNOTAVAIL;
    if (!(sb->profile & VGPU98_PROFILE_RENDER3D))
        return DDHAL_DRIVER_HANDLED;
    if (guid_eq(&d->guidInfo, &guid_callbacks2))
        answer(d, &d3d_callbacks2, sizeof(d3d_callbacks2));
    else if (guid_eq(&d->guidInfo, &guid_callbacks3))
        answer(d, &d3d_callbacks3, sizeof(d3d_callbacks3));
    else if (guid_eq(&d->guidInfo, &guid_extcaps))
        answer(d, &caps_extended, sizeof(caps_extended));
    else if (guid_eq(&d->guidInfo, &guid_zformats)) {
        DWORD n = (d->dwExpectedSize < sizeof(caps_zformats)) ? d->dwExpectedSize : sizeof(caps_zformats);

        memcpy(d->lpvData, caps_zformats, n);
        d->dwActualSize = n;
        d->ddRVal       = DD_OK;
    } else if (guid_eq(&d->guidInfo, &guid_parse)) {
        /* An input: the runtime's parser for commands the driver does not know. */
        d3d_parse_unknown = (PFND3DPARSEUNKNOWNCOMMAND) d->lpvData;
        d->ddRVal         = DD_OK;
    } else if (guid_eq(&d->guidInfo, &guid_misc2)) {
        /* A DirectX 7 runtime: from now on surfaces have handles, and
           contexts are made with the local objects. */
        if (!d3d7_on)
            dbg(DLL_TAG ": the DirectX 7 driver interface");
        d3d7_on = 1;
        answer(d, &d3d7_misc2, sizeof(d3d7_misc2));
    } else if (guid_eq(&d->guidInfo, &guid_more_caps)) {
        /* Cube maps in video memory, and volumes with the DirectX 8
           interface; the heap (if the mode leaves one) adds no
           restrictions. */
        DDMORESURFACECAPS more;
        const DWORD       size = 16 + 24 * (sb->halinfo.vmiData.dwNumHeaps ? 1 : 0);

        memset(&more, 0, sizeof(more));
        more.ddsCapsMore.dwCaps2 = DDSCAPS2_CUBEMAP | (d3d8_on ? DDSCAPS2_VOLUME : 0);
        answer(d, &more, size);
    } else if (guid_eq(&d->guidInfo, &guid_stereo)) {
        /* DirectX 8's GetDriverInfo2 shares the stereo-mode GUID; a stereo
           query stays unanswered. */
        d3d8_get_driver_info2(d);
        return DDHAL_DRIVER_HANDLED; /* traced there */
    }
    if (TRACE(TRACE_QUERY)) {
        static const struct {
            const GUID *g;
            const char *name;
        } names[] = { { &guid_callbacks2, "D3DCallbacks2" },       { &guid_callbacks3, "D3DCallbacks3" },
                      { &guid_extcaps, "D3DExtendedCaps" },        { &guid_zformats, "ZPixelFormats" },
                      { &guid_parse, "D3DParseUnknownCommand" },   { &guid_misc2, "Miscellaneous2Callbacks" },
                      { &guid_more_caps, "DDMoreSurfaceCaps" } };
        const char *name = NULL;
        DWORD       i;

        for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            if (guid_eq(&d->guidInfo, names[i].g))
                name = names[i].name;
        }
        if (name)
            dbg(DLL_TAG " trace: GetDriverInfo %s: %s, %u bytes", name, (d->ddRVal == DD_OK) ? "answered" : "not available",
                d->dwActualSize);
        else
            dbg(DLL_TAG " trace: GetDriverInfo GUID %x-%x: not available", ((const DWORD *) &d->guidInfo)[0],
                ((const DWORD *) &d->guidInfo)[1]);
    }
    return DDHAL_DRIVER_HANDLED;
}

/* --- The tables ----------------------------------------------------------------- */

void
ddraw_fill_callbacks(DWORD profile)
{
    DDHAL_DDCALLBACKS        *dd   = &sb->cb_dd;
    DDHAL_DDSURFACECALLBACKS *surf = &sb->cb_surf;
    DDHAL_DDPALETTECALLBACKS *pal  = &sb->cb_pal;
    const DWORD               destroy_driver = dd->DestroyDriver; /* the display driver's own */

    memset(dd, 0, sizeof(*dd));
    memset(surf, 0, sizeof(*surf));
    memset(pal, 0, sizeof(*pal));
    dd->dwSize            = sizeof(*dd);
    dd->DestroyDriver     = destroy_driver;
    surf->dwSize          = sizeof(*surf);
    pal->dwSize           = sizeof(*pal);
    sb->halinfo.GetDriverInfo         = 0;
    sb->halinfo.lpD3DGlobalDriverData = 0;
    sb->halinfo.lpD3DHALCallbacks     = 0;
    /* Execute buffers in the pool, when there is one (only with 3D). */
    pool_fill_callbacks();
    if (!regs)
        return;

    /* Display timing needs only the registers. */
    dd->dwFlags |= DDHAL_CB32_WAITFORVERTICALBLANK | DDHAL_CB32_GETSCANLINE;
    dd->WaitForVerticalBlank = (DWORD) WaitForVerticalBlank;
    dd->GetScanLine          = (DWORD) GetScanLine;
    if (!(profile & VGPU98_PROFILE_BLIT))
        return;

    dd->dwFlags |= DDHAL_CB32_CREATESURFACE | DDHAL_CB32_CANCREATESURFACE | DDHAL_CB32_FLIPTOGDISURFACE;
    dd->CreateSurface    = (DWORD) CreateSurface;
    dd->CanCreateSurface = (DWORD) CanCreateSurface;
    dd->FlipToGDISurface = (DWORD) FlipToGDISurface;

    surf->dwFlags = DDHAL_SURFCB32_DESTROYSURFACE | DDHAL_SURFCB32_FLIP | DDHAL_SURFCB32_LOCK | DDHAL_SURFCB32_BLT |
                    DDHAL_SURFCB32_SETCOLORKEY | DDHAL_SURFCB32_ADDATTACHEDSURFACE | DDHAL_SURFCB32_GETBLTSTATUS |
                    DDHAL_SURFCB32_GETFLIPSTATUS;
    surf->DestroySurface     = (DWORD) DestroySurface;
    surf->Flip               = (DWORD) Flip;
    surf->Lock               = (DWORD) Lock;
    surf->Blt                = (DWORD) Blt;
    surf->SetColorKey        = (DWORD) SetColorKey;
    surf->AddAttachedSurface = (DWORD) AddAttachedSurface;
    surf->GetBltStatus       = (DWORD) GetBltStatus;
    surf->GetFlipStatus      = (DWORD) GetFlipStatus;
    /* Unlock only for the Locks through copies in main memory (shadow.c). */
    if (shadow_on) {
        surf->dwFlags |= DDHAL_SURFCB32_UNLOCK;
        surf->Unlock = (DWORD) Unlock;
    }

    sb->halinfo.GetDriverInfo = (DWORD) GetDriverInfo;
    if (profile & VGPU98_PROFILE_RENDER3D) {
        sb->halinfo.lpD3DGlobalDriverData = (DWORD) &caps_global;
        sb->halinfo.lpD3DHALCallbacks     = (DWORD) &d3d_callbacks;
    }
}

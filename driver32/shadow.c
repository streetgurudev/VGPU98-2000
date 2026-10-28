/*
 * vgpu98dx.dll: surfaces Locked through copies in main memory.
 * A surface the GPU drew into,
 * and a texture DISCARDCONTENTS-Locked frame after frame, are Locked through
 * a copy of them in the pool: DOWNLOAD fills it (command set 2.3), the
 * game's CPU reads and writes main memory rather than the video memory
 * window, and the last Unlock sends the copy back with UPLOAD, which reads
 * it through the page table.
 *
 * The copies' table is shared by every process, behind its own lock, which
 * is held for the table's records alone: never across the ring's lock, a
 * wait or a pool call (a block is made and freed outside it).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3d.h"

#define SHADOWS      16          /* copies at most ... */
#define SHADOW_MAX   (4u << 20)  /* ... each at most this large ... */
#define SHADOW_TOTAL (16u << 20) /* ... and together */
#define HOT_TEXTURES 32          /* textures followed ... */
#define HOT_FRAMES   3           /* ... DISCARDCONTENTS-Locked in this many frames running */

typedef struct shadow {
    LPSURF surf;   /* the surface's record (surf_own's); NULL: a free entry */
    DWORD  pid;    /* the process that made it */
    DWORD  block;  /* its copy in the pool (POOL_NO_BLOCK while being made) ... */
    DWORD  bytes;  /* ... of these bytes (reserved in shadow_bytes), laid out as the surface from its offset */
    int    filled; /* the copy was filled once (a new one holds another owner's bytes) */
    DWORD  fence;  /* the last packet that reads or writes the copy */
    DWORD  used;   /* shadow_clock at its last Lock: the least lately used goes first */
    int    locked; /* a Lock through it is open: its flags and rectangle, and ... */
    DWORD  flags;
    RECTL  rect;
    DWORD  others; /* ... the surface's other Locks open meanwhile (the copy goes back at the last Unlock) */
} shadow_t;

typedef struct hot {
    LPSURF surf;  /* a texture DISCARDCONTENTS-Locked lately ... */
    DWORD  frame; /* ... in this frame (ddraw_frames) ... */
    DWORD  run;   /* ... and as many frames running */
} hot_t;

/* Blocks to free, gathered under the table's lock and freed after it. */
typedef struct gone {
    DWORD n;
    DWORD block[SHADOWS];
    DWORD fence[SHADOWS];
} gone_t;

static shadow_t shadows[SHADOWS];
static hot_t    hots[HOT_TEXTURES];
static DWORD    shadow_bytes; /* the copies' bytes together, those being made included */
static DWORD    shadow_clock;
static dlock_t  shadow_dl;
int             shadow_on;

void
shadow_init(DWORD profile)
{
    shadow_on = pool_on && (profile & VGPU98_PROFILE_DOWNLOAD) && (profile & VGPU98_PROFILE_GPUVM);
}

/* The bytes of a rectangle's rows, as memory holds them. */
static DWORD
rect_bytes(const gsurf_t *g, const RECTL *r)
{
    DWORD rows;
    DWORD row_bytes;

    rect_rows(g->format, r, &rows, &row_bytes);
    return rows * row_bytes;
}

/* An entry goes, its block gathered for freeing once the packets that read
   or write it are done. Under shadow_dl. */
static void
shadow_drop(shadow_t *e, gone_t *gone)
{
    if (e->bytes && (e->block != POOL_NO_BLOCK)) {
        gone->block[gone->n] = e->block;
        gone->fence[gone->n] = e->fence;
        gone->n++;
    }
    shadow_bytes -= e->bytes;
    e->surf   = NULL;
    e->block  = POOL_NO_BLOCK;
    e->bytes  = 0;
    e->locked = 0;
    e->others = 0;
}

static void
gone_free(const gone_t *gone)
{
    DWORD i;

    for (i = 0; i < gone->n; i++)
        pool_block_free(gone->block[i], gone->fence[i]);
}

/* Surface s's entry, Locked: its own (made again when its copy was
   smaller), else a free one or the least lately used one no Lock holds,
   the copies' total kept to SHADOW_TOTAL; *make when its block is still to
   be made. NULL if none can be had, or (counted in `others`) a Lock
   through s's copy is open already. Under shadow_dl; the blocks of the
   entries it let go are gathered in `gone`. */
static shadow_t *
shadow_get(LPSURF s, DWORD bytes, const DDHAL_LOCKDATA *d, int *make, gone_t *gone)
{
    shadow_t *e = NULL;
    DWORD     i;

    *make = 0;
    for (i = 0; (i < SHADOWS) && !e; i++) {
        if (shadows[i].surf == s)
            e = &shadows[i];
    }
    if (e && (e->locked || (e->block == POOL_NO_BLOCK))) {
        e->others += e->locked != 0;
        return NULL;
    }
    if (e && (e->bytes < bytes))
        shadow_drop(e, gone);
    else if (e)
        goto take;
    e = NULL;
    for (;;) {
        shadow_t *lru = NULL;

        for (i = 0; (i < SHADOWS) && !e; i++) {
            if (!shadows[i].surf)
                e = &shadows[i];
        }
        if (e && (shadow_bytes + bytes <= SHADOW_TOTAL))
            break;
        for (i = 0; i < SHADOWS; i++) {
            shadow_t *c = &shadows[i];

            if (c->surf && !c->locked && (c->block != POOL_NO_BLOCK) && (!lru || ((LONG) (c->used - lru->used) < 0)))
                lru = c;
        }
        if (!lru)
            return NULL;
        shadow_drop(lru, gone);
    }
    e->surf   = s;
    e->pid    = GetCurrentProcessId();
    e->block  = POOL_NO_BLOCK;
    e->bytes  = bytes;
    e->filled = 0;
    e->fence  = 0;
    shadow_bytes += bytes;
    *make = 1;
take:
    e->locked = 1;
    e->others = 0;
    e->flags  = d->dwFlags;
    e->used   = ++shadow_clock;
    return e;
}

/* A Lock through e ends without its copy sent back: e free for the next. */
static void
shadow_release(shadow_t *e)
{
    dlock_take(&shadow_dl, "surface copies");
    e->locked = 0;
    dlock_give(&shadow_dl);
}

/* A new entry whose block could not be made goes. */
static void
shadow_unmake(shadow_t *e)
{
    gone_t gone;

    gone.n = 0;
    dlock_take(&shadow_dl, "surface copies");
    shadow_drop(e, &gone);
    dlock_give(&shadow_dl);
}

int
shadow_hot(LPSURF s)
{
    const DWORD frame = ddraw_frames();
    hot_t      *e     = NULL;
    hot_t      *old   = NULL;
    DWORD       i;
    int         hot;

    dlock_take(&shadow_dl, "surface copies");
    /* Its entry, else a free one, else the one Locked longest ago. */
    for (i = 0; (i < HOT_TEXTURES) && !e; i++) {
        if (hots[i].surf == s)
            e = &hots[i];
        else if (!old || (old->surf && (!hots[i].surf || ((LONG) (hots[i].frame - old->frame) < 0))))
            old = &hots[i];
    }
    if (!e) {
        e        = old;
        e->surf  = s;
        e->frame = frame;
        e->run   = 1;
    } else if (e->frame != frame) {
        e->run   = (e->frame + 1 == frame) ? e->run + 1 : 1;
        e->frame = frame;
    }
    hot = e->run >= HOT_FRAMES;
    dlock_give(&shadow_dl);
    return hot;
}

int
shadow_lock(DDHAL_LOCKDATA *d, LPSURF own, const gsurf_t *g, int kind)
{
    const DWORD bytes = surf_bytes(g);
    RECTL       r;
    shadow_t   *e;
    gone_t      gone;
    DWORD       at;
    HRESULT     hr;
    int         make;

    /* A volume level's slices lie past surf_bytes' one: not through a copy. */
    if (!shadow_on || !bytes || (bytes > SHADOW_MAX) || (own->dwReserved1 & SURF_SLICES))
        return 0;
    r.left   = 0;
    r.top    = 0;
    r.right  = (LONG) g->width;
    r.bottom = (LONG) g->height;
    if (d->bHasRect) {
        if ((d->rArea.left < 0) || (d->rArea.top < 0) || (d->rArea.left >= d->rArea.right) ||
            (d->rArea.top >= d->rArea.bottom) || (d->rArea.right > (LONG) g->width) || (d->rArea.bottom > (LONG) g->height))
            return 0;
        r = d->rArea;
        /* A DXT surface's Lock starts at a block. */
        if (fmt_is_dxt(g->format) && ((r.left | r.top) & 3))
            return 0;
        surf_blocks(g, &r);
    }
    gone.n = 0;
    dlock_take(&shadow_dl, "surface copies");
    e = shadow_get(own, bytes, d, &make, &gone);
    if (e)
        e->rect = r;
    dlock_give(&shadow_dl);
    gone_free(&gone);
    if (!e) {
        DIAG(diag.sh_refused++);
        return 0;
    }
    if (make) {
        const DWORD k = pool_block_alloc(bytes);

        if (k == POOL_NO_BLOCK) {
            shadow_unmake(e);
            DIAG(diag.sh_refused++);
            return 0;
        }
        dlock_take(&shadow_dl, "surface copies");
        e->block = k;
        dlock_give(&shadow_dl);
    }
    at = rect_start(g, &r);
    /* DISCARDCONTENTS reads nothing back, once the copy holds the surface's
       own bytes (a new one is filled first, as every copy is).
       Only the copy's last packet (an earlier Unlock's UPLOAD reading it)
       is waited for then. */
    if ((d->dwFlags & DDLOCK_DISCARDCONTENTS) && e->filled) {
        if (!gpu_fence_done(e->fence)) {
            if (!(d->dwFlags & DDLOCK_WAIT)) {
                gpu_kick();
                if (!gpu_fence_done(e->fence)) {
                    shadow_release(e);
                    d->ddRVal = DDERR_WASSTILLDRAWING;
                    return 1;
                }
            } else if ((hr = gpu_wait(e->fence)) != DD_OK) {
                shadow_release(e);
                d->ddRVal = hr;
                return 1;
            }
        }
    } else {
        DWORD           epoch;
        volatile DWORD *p;
        DWORD           fence;

        if ((hr = gpu_begin()) != DD_OK) {
            shadow_release(e);
            d->ddRVal = hr;
            return 1;
        }
        epoch = sb->state_epoch; /* after gpu_begin, which may start the GPU again itself */
        p     = gpu_packet(VGPU98_OP_DOWNLOAD, VGPU98_LEN_DOWNLOAD);
        if (p) {
            pkt_surf(&p, g);
            pkt_put(&p, VGPU98_XY(r.left, r.top));
            pkt_put(&p, VGPU98_XY(r.right, r.bottom));
            pkt_put(&p, g->pitch);
            pkt_put(&p, pool_block_gpu(e->block) + at);
        }
        fence = gpu_end();
        hr    = p ? gpu_wait(fence) : DDERR_SURFACELOST;
        /* A GPU reset meanwhile (a recovery waits it out and says DD_OK)
           left the copy unfilled: the Lock fails rather than hand it out. */
        if ((hr == DD_OK) && (sb->state_epoch != epoch))
            hr = DDERR_SURFACELOST;
        if (hr != DD_OK) {
            shadow_release(e);
            d->ddRVal = hr;
            return 1;
        }
        e->fence  = fence;
        e->filled = 1;
        DIAG(diag.sh_read_bytes += rect_bytes(g, &r));
    }
    DIAG(diag.sh_locks[kind != SHADOW_GPU]++);
    d->lpSurfData = (void *) (pool_block_mem(e->block) + at);
    d->ddRVal     = DD_OK;
    return 1;
}

int
shadow_unlock(DDHAL_UNLOCKDATA *d)
{
    static int   told;
    const LPSURF own = surf_own(d->lpDDSurface);
    shadow_t    *e   = NULL;
    gsurf_t      g;
    DWORD        i;
    int          sent = 0;

    dlock_take(&shadow_dl, "surface copies");
    for (i = 0; (i < SHADOWS) && !e; i++) {
        if (shadows[i].locked && (shadows[i].surf == own))
            e = &shadows[i];
    }
    /* Another of the surface's Locks open meanwhile: the copy goes back at
       the last Unlock (DirectDraw's Unlock names no rectangle). */
    if (e && e->others) {
        e->others--;
        e = NULL;
    }
    dlock_give(&shadow_dl);
    if (!e)
        return 0;
    if (e->flags & DDLOCK_READONLY)
        sent = 1;
    /* The copy back, queued before any later packet: a draw that samples
       the surface, or a DOWNLOAD of it, comes after. */
    else if (surf_describe(d->lpDDSurface, &g) && (gpu_begin() == DD_OK)) {
        volatile DWORD *p = gpu_packet(VGPU98_OP_UPLOAD, VGPU98_LEN_UPLOAD);
        DWORD           fence;

        if (p) {
            pkt_surf(&p, &g);
            pkt_put(&p, VGPU98_XY(e->rect.left, e->rect.top));
            pkt_put(&p, VGPU98_XY(e->rect.right, e->rect.bottom));
            pkt_put(&p, VGPU98_UPLOAD_GPUVM);
            pkt_put(&p, g.pitch);
            pkt_put(&p, pool_block_gpu(e->block) + rect_start(&g, &e->rect));
            DIAG(diag.sh_write_bytes += rect_bytes(&g, &e->rect));
            sent = 1;
        }
        fence    = gpu_end();
        e->fence = fence;
        surf_used(own, fence);
    }
    if (!sent) {
        DIAG(diag.sh_lost++);
        if (!told) {
            told = 1;
            dbg_event(DLL_TAG ": a Lock's writes through a copy in main memory were lost: the GPU took no UPLOAD (held, or the "
                              "surface gone)");
        }
    }
    shadow_release(e);
    return 1;
}

void
shadow_forget(LPSURF s)
{
    const LPSURF own = surf_own(s);
    gone_t       gone;
    DWORD        i;

    gone.n = 0;
    dlock_take(&shadow_dl, "surface copies");
    for (i = 0; i < SHADOWS; i++) {
        if (shadows[i].surf && ((shadows[i].surf == s) || (shadows[i].surf == own)))
            shadow_drop(&shadows[i], &gone);
    }
    for (i = 0; i < HOT_TEXTURES; i++) {
        if ((hots[i].surf == s) || (hots[i].surf == own))
            hots[i].surf = NULL;
    }
    dlock_give(&shadow_dl);
    gone_free(&gone);
}

void
shadow_process_gone(DWORD pid)
{
    gone_t gone;
    DWORD  i;

    gone.n = 0;
    dlock_take(&shadow_dl, "surface copies");
    /* Those it was still making too: no block to free. */
    for (i = 0; i < SHADOWS; i++) {
        if (shadows[i].surf && (shadows[i].pid == pid))
            shadow_drop(&shadows[i], &gone);
    }
    dlock_give(&shadow_dl);
    gone_free(&gone);
}

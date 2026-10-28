/*
 * The ICD and the card's OpenGL device:
 * the batches, the state registers, the device's objects (share groups,
 * contexts, drawables), the windows and their drawables, swaps into the
 * window on the desktop, and the process's end.
 *
 * Each context records into a batch of its own, copied into the GPU's ring
 * under the ring lock as one, ended by a FENCE. A GL command
 * is never lost: while the display driver or the mini-VDD holds the GPU (a
 * mode change, a full-screen DOS session), a batch waits instead of being
 * dropped. After a GPU restart (the shared block's state_epoch moved) the
 * context's objects and registers go again before its next batch: the
 * device keeps its objects across restarts, but a batch cut short may have
 * lost some of what it carried.
 *
 * A window's frames reach the screen through video memory:
 * SwapBuffers has the device copy the back image into the front image and
 * write the front image into the window's visible rectangles, the clip list
 * a DirectDraw clipper on the window gives.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "gpuenv.h"
#include "lists.h"
#include "state.h"
#include "texobj.h"

#define FRONT_SHOW_MS 16 /* drawing into the front buffer goes to the screen this often at least */

/* DirectDraw, as much as the clip lists need (its interfaces' method order
   is the SDK's). */
#define DDSCL_NORMAL           0x00000008
#define DD_RELEASE             2
#define DD_CREATECLIPPER       4
#define DD_SETCOOPERATIVELEVEL 20
#define DDC_GETCLIPLIST        3
#define DDC_SETHWND            8

#define VTBL(obj, i) ((*(void ***) (obj))[i])

typedef DWORD(STDCALL *dd_release_t)(void *self);
typedef HRESULT(STDCALL *dd_coop_t)(void *self, DWORD hwnd, DWORD flags);
typedef HRESULT(STDCALL *dd_clipper_t)(void *self, DWORD flags, void **clipper, void *outer);
typedef HRESULT(STDCALL *ddc_list_t)(void *self, const ogl_rect_t *rect, void *rgn, DWORD *size);
typedef HRESULT(STDCALL *ddc_hwnd_t)(void *self, DWORD flags, DWORD hwnd);

/* A clip list as GetClipList gives it: RGNDATAHEADER, then the rectangles. */
typedef struct rgn_head {
    DWORD      size;
    DWORD      type;
    DWORD      count;
    DWORD      rgn_size;
    ogl_rect_t bound;
} rgn_head_t;

static ogl_window_t windows[OGL_WINDOWS];
static void        *dd;          /* the process's DirectDraw object, for the clippers */
static void        *clipper[OGL_WINDOWS];
static DWORD        id_count;
static int          detaching;   /* inside DllMain's detach: nothing waited for, no USER32 */

/* --- IDs ------------------------------------------------------------------------- */

/* The process's ID folded into the high half, a count in the low half
   (never 0): unique among the device's live objects as long as two live
   processes do not fold to the same high half. */
DWORD
ogl_new_id(void)
{
    const DWORD pid = GetCurrentProcessId();
    DWORD       hi  = ((pid >> 16) ^ (pid & 0xffff) ^ (pid >> 7)) & 0xffff;
    DWORD       lo;

    do
        lo = __atomic_add_fetch(&id_count, 1, __ATOMIC_RELAXED) & 0xffff;
    while (!lo);
    if (!hi)
        hi = 0x9898;
    return (hi << 16) | lo;
}

/* --- Windows --------------------------------------------------------------------- */

ogl_window_t *
ogl_window_find(DWORD hwnd)
{
    int i;

    for (i = 0; hwnd && (i < OGL_WINDOWS); i++) {
        if (windows[i].hwnd == hwnd)
            return &windows[i];
    }
    return NULL;
}

ogl_window_t *
ogl_window_of(DWORD dc)
{
    const DWORD   hwnd = dc ? WindowFromDC(dc) : 0;
    ogl_window_t *w    = ogl_window_find(hwnd);

    return (w && w->pixel_format) ? w : NULL;
}

static void window_drop(ogl_window_t *w);

int
ogl_window_set_format(DWORD dc, int pixel_format, DWORD format)
{
    const DWORD   hwnd = dc ? WindowFromDC(dc) : 0;
    ogl_window_t *w    = ogl_window_find(hwnd);
    int           i;

    if (!hwnd)
        return 0;
    if (w) {
        if ((w->pixel_format == pixel_format) && (w->format == format))
            return 1;
        /* Windows 9x soon gives a gone window's HWND to a new window: an
           entry no context draws into is taken as the new window's (a
           window's format is set once, so the old one has gone). */
        for (i = 0; i < OGL_CONTEXTS; i++) {
            if (ogl_ctx_table[i] && (ogl_ctx_table[i]->win == w))
                return 0;
        }
        window_drop(w);
    }
    /* A free entry, or one whose window has gone (its drawable closed). */
    for (i = 0; (i < OGL_WINDOWS) && !w; i++) {
        if (!windows[i].hwnd)
            w = &windows[i];
    }
    for (i = 0; (i < OGL_WINDOWS) && !w; i++) {
        if (!IsWindow(windows[i].hwnd)) {
            window_drop(&windows[i]);
            w = &windows[i];
        }
    }
    if (!w)
        return 0;
    memset(w, 0, sizeof(*w));
    w->hwnd          = hwnd;
    w->pixel_format  = pixel_format;
    w->format        = format;
    w->swap_interval = 1;
    w->swap_told     = 0xffffffffu; /* no decision said yet */
    return 1;
}

int
ogl_client_size(DWORD hwnd, DWORD *w, DWORD *h)
{
    ogl_rect_t r;

    if (!GetClientRect(hwnd, &r) || (r.right <= r.left) || (r.bottom <= r.top)) {
        *w = *h = 0;
        return 0;
    }
    *w = (DWORD) (r.right - r.left);
    *h = (DWORD) (r.bottom - r.top);
    return 1;
}

/* --- Sending ------------------------------------------------------------------------ */

/* Writes one packet into the ring; 0 when the GPU stopped. */
static int
put(DWORD op, DWORD len, const DWORD *payload)
{
    volatile DWORD *p = gpu_packet(op, len);
    DWORD           k;

    if (!p)
        return 0;
    for (k = 0; k < len; k++)
        p[k] = payload[k];
    return 1;
}

/* The ring lock, the GPU running: waits while the GPU is held, unless the
   process is ending. 0 if the batch cannot go. */
static int
begin(void)
{
    HRESULT hr;

    if (!regs || !sb)
        return 0;
    while ((hr = gpu_begin()) == DDERR_SURFACELOST) {
        if (detaching)
            return 0;
        Sleep(5);
    }
    return hr == DD_OK;
}

/* A few packets at once, apart from any context's batch (the process's end,
   a context's close). */
static DWORD
send_now(const DWORD *packets, DWORD n)
{
    DWORD i;

    if (!begin())
        return 0;
    for (i = 0; i < n;) {
        const DWORD op  = VGPU98_PKT_OP(packets[i]);
        const DWORD len = VGPU98_PKT_LEN(packets[i]);

        if (!put(op, len, &packets[i + 1]))
            break;
        i += 1 + len;
    }
    return gpu_end();
}

/* The packets a context's objects need again after a GPU restart: its
   group, itself, its drawable and binding, its registers as they stood. */
static void
put_objects_again(ogl_ctx_t *c)
{
    DWORD               p[4];
    const ogl_window_t *w = NULL;
    int                 i;

    for (i = 0; c->bound && (i < OGL_WINDOWS); i++) {
        if (windows[i].drawable == c->bound)
            w = &windows[i];
    }
    if (c->group->opened) {
        p[0] = c->group->id;
        p[1] = VGPU98_OGL_OPEN;
        put(VGPU98_OGL_GROUP, VGPU98_OGL_LEN_GROUP, p);
    }
    if (c->opened) {
        p[0] = c->id;
        p[1] = VGPU98_OGL_OPEN;
        p[2] = c->group->id;
        put(VGPU98_OGL_CONTEXT, VGPU98_OGL_LEN_CONTEXT, p);
    }
    if (w) {
        p[0] = w->drawable;
        p[1] = VGPU98_OGL_OPEN;
        p[2] = w->w | (w->h << 16);
        p[3] = w->format;
        put(VGPU98_OGL_DRAWABLE, VGPU98_OGL_LEN_DRAWABLE, p);
        p[0] = c->id;
        p[1] = c->bound;
        put(VGPU98_OGL_BIND, VGPU98_OGL_LEN_BIND, p);
    }
    if (c->opened) {
        static DWORD again[2 + VGPU98_OGL_R_COUNT];

        again[0] = c->id;
        again[1] = 0;
        memcpy(&again[2], c->base, ogl_reg_count() * 4);
        put(VGPU98_OGL_STATE, 2 + ogl_reg_count(), again);
    }
    /* The shared palette is the context's state too: a PALETTE lost with a
       batch, or a context the device made again, gets it back. */
    if (c->opened && ogl_ext_on(OGL_EXT_SHARED_TEXTURE_PALETTE) && !ogl_palette_default(&c->gl->shared_pal)) {
        static DWORD pal[VGPU98_OGL_LEN_PALETTE + VGPU98_OGL_PALETTE_MAX];
        const ogl_palette_t *sp = &c->gl->shared_pal;

        pal[0] = c->id;
        pal[1] = 0;
        pal[2] = (DWORD) sp->base;
        pal[3] = (DWORD) sp->n;
        memcpy(&pal[VGPU98_OGL_LEN_PALETTE], sp->e, (size_t) sp->n * 4);
        put(VGPU98_OGL_PALETTE, VGPU98_OGL_LEN_PALETTE + (DWORD) sp->n, pal);
    }
}

DWORD
ogl_flush(ogl_ctx_t *c)
{
    DWORD i;
    DWORD epoch;
    int   cut;

    ogl_draw_flush(c);
    if (!c->batch_n)
        return c->last_fence;
    if (!begin()) {
        /* The process is ending, or there is no card: dropped. */
        c->batch_n = 0;
        return 0;
    }
    epoch = sb->state_epoch; /* after any restart gpu_begin made */
    if (c->synced && (epoch != c->epoch))
        put_objects_again(c);
    for (i = 0; i < c->batch_n;) {
        const DWORD op  = VGPU98_PKT_OP(c->batch[i]);
        const DWORD len = VGPU98_PKT_LEN(c->batch[i]);

        if (!put(op, len, &c->batch[i + 1]))
            break; /* the GPU stopped: the rest of the batch is lost with it */
        i += 1 + len;
    }
    cut           = i < c->batch_n;
    c->batch_n    = 0;
    c->last_fence = gpu_end();
    /* Cut short, or the GPU restarted while it went: what it carried goes
       again with the next batch. */
    c->epoch  = (cut || (sb->state_epoch != epoch)) ? sb->state_epoch - 1 : sb->state_epoch;
    c->synced = c->opened;
    memcpy(c->base, c->sent, sizeof(c->base));
    return c->last_fence;
}

int
ogl_wait(DWORD fence)
{
    if (!regs || !sb || !fence)
        return 1;
    return gpu_wait(fence) == DD_OK;
}

void
ogl_finish(ogl_ctx_t *c)
{
    ogl_sync(c);
    ogl_wait(ogl_flush(c));
}

DWORD *
ogl_cmd(ogl_ctx_t *c, DWORD op, DWORD len)
{
    DWORD *p;

    if (len + 1 > OGL_BATCH_DWORDS)
        return NULL;
    /* A DRAW being built goes first: the commands keep their order. */
    if (op != VGPU98_OGL_DRAW)
        ogl_draw_flush(c);
    if (c->batch_n + 1 + len > OGL_BATCH_DWORDS)
        ogl_flush(c);
    p           = &c->batch[c->batch_n];
    p[0]        = VGPU98_PKT(op, len);
    c->batch_n += 1 + len;
    return p + 1;
}

/* --- Registers ----------------------------------------------------------------------- */

static void
dirty(ogl_ctx_t *c, DWORD lo, DWORD hi)
{
    if (c->dirty_lo >= c->dirty_hi) {
        c->dirty_lo = lo;
        c->dirty_hi = hi;
        return;
    }
    if (lo < c->dirty_lo)
        c->dirty_lo = lo;
    if (hi > c->dirty_hi)
        c->dirty_hi = hi;
}

void
ogl_reg(ogl_ctx_t *c, DWORD index, DWORD value)
{
    if ((index < ogl_reg_count()) && (c->reg[index] != value)) {
        c->reg[index] = value;
        dirty(c, index, index + 1);
    }
}

void
ogl_regf(ogl_ctx_t *c, DWORD index, float value)
{
    DWORD v;

    memcpy(&v, &value, 4);
    ogl_reg(c, index, v);
}

void
ogl_regs(ogl_ctx_t *c, DWORD index, const DWORD *values, DWORD n)
{
    DWORD k;

    for (k = 0; (k < n) && (index + k < VGPU98_OGL_R_COUNT); k++)
        ogl_reg(c, index + k, values[k]);
}

/* Unchanged registers that end a STATE run:
   a run of fewer goes inside the STATE. */
#define RUN_GAP 16

void
ogl_sync(ogl_ctx_t *c)
{
    DWORD lo = c->dirty_lo, hi = c->dirty_hi;

    if (lo >= hi)
        return;
    c->dirty_lo = c->dirty_hi = 0;
    /* Only what differs from what was sent, a STATE a run of changes. */
    while (lo < hi) {
        DWORD  end, last, *p;

        while ((lo < hi) && (c->reg[lo] == c->sent[lo]))
            lo++;
        if (lo >= hi)
            return;
        for (last = end = lo; (end < hi) && (end - last < RUN_GAP); end++) {
            if (c->reg[end] != c->sent[end])
                last = end;
        }
        p = ogl_cmd(c, VGPU98_OGL_STATE, 2 + (last + 1 - lo));
        if (!p)
            return;
        p[0] = c->id;
        p[1] = lo;
        memcpy(&p[2], &c->reg[lo], (last + 1 - lo) * 4);
        memcpy(&c->sent[lo], &c->reg[lo], (last + 1 - lo) * 4);
        lo = last + 1;
    }
}

/* --- Objects --------------------------------------------------------------------------- */

int
ogl_objects(ogl_ctx_t *c)
{
    ogl_window_t *w = c->win;
    DWORD        *p;
    DWORD         cw, ch;

    if (!ogl_card())
        return 0;
    if (!c->group->opened) {
        p = ogl_cmd(c, VGPU98_OGL_GROUP, VGPU98_OGL_LEN_GROUP);
        p[0]             = c->group->id;
        p[1]             = VGPU98_OGL_OPEN;
        c->group->opened = 1;
    }
    if (!c->opened) {
        p = ogl_cmd(c, VGPU98_OGL_CONTEXT, VGPU98_OGL_LEN_CONTEXT);
        p[0]      = c->id;
        p[1]      = VGPU98_OGL_OPEN;
        p[2]      = c->group->id;
        c->opened = 1;
        /* Its registers go whole: the device starts them at GL's defaults,
           but the ICD's are the truth. */
        memset(c->sent, 0xff, sizeof(c->sent));
        memset(c->base, 0xff, sizeof(c->base));
        dirty(c, 0, ogl_reg_count());
        c->epoch = sb->state_epoch;
    }
    if (!w)
        return 0;
    /* The window's client size now; a window with none (minimized) keeps
       the drawable it has, or gets one pixel. */
    if (!ogl_client_size(w->hwnd, &cw, &ch)) {
        cw = w->w ? w->w : 1;
        ch = w->h ? w->h : 1;
    }
    if (cw > VGPU98_OGL_MAX_SIZE)
        cw = VGPU98_OGL_MAX_SIZE;
    if (ch > VGPU98_OGL_MAX_SIZE)
        ch = VGPU98_OGL_MAX_SIZE;
    if (!w->drawable || (cw != w->w) || (ch != w->h)) {
        p = ogl_cmd(c, VGPU98_OGL_DRAWABLE, VGPU98_OGL_LEN_DRAWABLE);
        if (!w->drawable)
            w->drawable = ogl_new_id();
        p[0] = w->drawable;
        p[1] = (w->w || w->h) ? VGPU98_OGL_RESIZE : VGPU98_OGL_OPEN;
        p[2] = cw | (ch << 16);
        p[3] = w->format;
        w->w = cw;
        w->h = ch;
        w->front_drawn  = 0;
        w->front_loaded = 0;
    }
    if (c->bound != w->drawable) {
        p         = ogl_cmd(c, VGPU98_OGL_BIND, VGPU98_OGL_LEN_BIND);
        p[0]      = c->id;
        p[1]      = w->drawable;
        c->bound  = w->drawable;
    }
    return 1;
}

/* A window gone, or its last context: its drawable closed on the device
   (the window keeps its pixel format while it lives). */
static void
drawable_close(ogl_window_t *w)
{
    DWORD pk[1 + VGPU98_OGL_LEN_DRAWABLE];
    int   i;

    if (!w->drawable)
        return;
    pk[0] = VGPU98_PKT(VGPU98_OGL_DRAWABLE, VGPU98_OGL_LEN_DRAWABLE);
    pk[1] = w->drawable;
    pk[2] = VGPU98_OGL_CLOSE;
    pk[3] = 0;
    pk[4] = 0;
    send_now(pk, 1 + VGPU98_OGL_LEN_DRAWABLE);
    for (i = 0; i < OGL_CONTEXTS; i++) {
        if (ogl_ctx_table[i] && (ogl_ctx_table[i]->bound == w->drawable))
            ogl_ctx_table[i]->bound = 0;
    }
    w->drawable = 0;
    w->w = w->h = 0;
}

static void
window_drop(ogl_window_t *w)
{
    const int i = (int) (w - windows);

    drawable_close(w);
    if (clipper[i] && !detaching)
        ((dd_release_t) VTBL(clipper[i], DD_RELEASE))(clipper[i]);
    clipper[i] = NULL;
    memset(w, 0, sizeof(*w));
}

void
ogl_close(ogl_ctx_t *c, int ending)
{
    DWORD pk[2 * (1 + VGPU98_OGL_LEN_CONTEXT)];
    DWORD n = 0;
    int   i, others = 0;

    /* The context's default textures leave the group (which may outlive
       it, shared): their TEXDELETEs go with the last batch. */
    ogl_tex_ctx_release(c, !ending);
    if (!ending)
        ogl_flush(c);
    else
        c->batch_n = 0;
    if (c->opened) {
        pk[n++] = VGPU98_PKT(VGPU98_OGL_CONTEXT, VGPU98_OGL_LEN_CONTEXT);
        pk[n++] = c->id;
        pk[n++] = VGPU98_OGL_CLOSE;
        pk[n++] = 0;
    }
    if (--c->group->refs <= 0) {
        if (c->group->opened) {
            pk[n++] = VGPU98_PKT(VGPU98_OGL_GROUP, VGPU98_OGL_LEN_GROUP);
            pk[n++] = c->group->id;
            pk[n++] = VGPU98_OGL_CLOSE;
        }
        ogl_dl_group_free(c->group);
        ogl_tex_group_free(c->group);
        ogl_free(c->group);
    }
    c->group  = NULL;
    c->opened = 0;
    if (n)
        send_now(pk, n);
    /* The process's last context: its windows' drawables go too. */
    for (i = 0; i < OGL_CONTEXTS; i++) {
        if (ogl_ctx_table[i] && (ogl_ctx_table[i] != c))
            others = 1;
    }
    if (!others) {
        for (i = 0; i < OGL_WINDOWS; i++)
            drawable_close(&windows[i]);
    }
}

void
ogl_detach(int ending)
{
    int i;

    detaching = 1;
    if (!sb || !regs)
        return;
    /* A killed thread may hold the ring lock: let go of it at the process's
       end, as the Glide DLLs do (not at a FreeLibrary: another DLL of the
       process may hold it rightly). */
    if (ending)
        gpu_release_process(GetCurrentProcessId());
    for (i = 0; i < OGL_CONTEXTS; i++) {
        if (ogl_ctx_table[i]) {
            ogl_close(ogl_ctx_table[i], 1);
            ogl_ctx_table[i] = NULL;
        }
    }
    for (i = 0; i < OGL_WINDOWS; i++)
        drawable_close(&windows[i]);
}

/* --- DirectDraw: the clippers -------------------------------------------------------- */

/* The objects made when first needed (DirectDraw's, its clippers and
   surface, the display's DC) are made one thread at a time: a lock of
   their own, never the exports' (a thread's detach takes that one). */
static volatile LONG make_lock_word;

static void
make_lock(void)
{
    int spins = 0;

    while (__atomic_exchange_n(&make_lock_word, 1, __ATOMIC_ACQUIRE))
        Sleep((++spins < 16) ? 0 : 1);
}

static void
make_unlock(void)
{
    __atomic_store_n(&make_lock_word, 0, __ATOMIC_RELEASE);
}

/* DirectDraw's object, made once (normal cooperation). NULL without it. */
static void *
dd_object(void)
{
    if (dd)
        return dd;
    make_lock();
    if (!dd) {
        void *d;

        if (DirectDrawCreate(NULL, &d, NULL) == DD_OK) {
            ((dd_coop_t) VTBL(d, DD_SETCOOPERATIVELEVEL))(d, 0, DDSCL_NORMAL);
            dd = d;
        }
    }
    make_unlock();
    return dd;
}

/* The window's clipper, made once. NULL without DirectDraw: the client
   area alone then. */
static void *
window_clipper(const ogl_window_t *w)
{
    const int i = (int) (w - windows);

    if (clipper[i])
        return clipper[i];
    if (!dd_object())
        return NULL;
    make_lock();
    if (!clipper[i]) {
        void *cl;

        if (((dd_clipper_t) VTBL(dd, DD_CREATECLIPPER))(dd, 0, &cl, NULL) == DD_OK) {
            if (((ddc_hwnd_t) VTBL(cl, DDC_SETHWND))(cl, 0, w->hwnd) == DD_OK)
                clipper[i] = cl;
            else
                ((dd_release_t) VTBL(cl, DD_RELEASE))(cl);
        }
    }
    make_unlock();
    return clipper[i];
}

/* --- The staging surface (read-backs) ----------------------------------------------------- */

#define DD_CREATESURFACE 6
#define DDS_LOCK         25
#define DDS_RESTORE      27
#define DDS_UNLOCK       32
#define DD_LOCK_WAIT     0x00000001 /* DDLOCK_WAIT */

typedef HRESULT(STDCALL *dd_create_t)(void *self, DDSURFACEDESC *desc, void **surface, void *outer);
typedef HRESULT(STDCALL *dds_lock_t)(void *self, void *rect, DDSURFACEDESC *desc, DWORD flags, DWORD event);
typedef HRESULT(STDCALL *dds_unlock_t)(void *self, void *ptr);
typedef HRESULT(STDCALL *dds_restore_t)(void *self);

static void *staging;

/* Where the surface is now: a Lock finds it (restored first if a mode
   change lost it). NULL when it is not one run of video memory. */
static uint8_t *
staging_at(DWORD *offset)
{
    DDSURFACEDESC desc;
    HRESULT       hr;
    int           tries;

    for (tries = 0; tries < 2; tries++) {
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        hr          = ((dds_lock_t) VTBL(staging, DDS_LOCK))(staging, NULL, &desc, DD_LOCK_WAIT, 0);
        if (hr == DD_OK) {
            const DWORD at = (DWORD) desc.lpSurface - sb->fb_linear;

            ((dds_unlock_t) VTBL(staging, DDS_UNLOCK))(staging, (void *) desc.lpSurface);
            if (((DWORD) desc.lpSurface < sb->fb_linear) || ((DWORD) desc.lPitch != OGL_STAGING_STRIDE) ||
                (at > sb->vram_size) || (sb->vram_size - at < OGL_STAGING_STRIDE * OGL_STAGING_ROWS))
                return NULL;
            *offset = at;
            return vram + at;
        }
        ((dds_restore_t) VTBL(staging, DDS_RESTORE))(staging);
    }
    return NULL;
}

uint8_t *
ogl_staging(DWORD *offset)
{
    DDSURFACEDESC desc;

    if (staging)
        return staging_at(offset);
    if (!dd_object() || !vram)
        return NULL;
    make_lock();
    if (staging) {
        make_unlock();
        return staging_at(offset);
    }
    memset(&desc, 0, sizeof(desc));
    desc.dwSize                        = sizeof(desc);
    desc.dwFlags                       = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth                       = OGL_STAGING_STRIDE / 4;
    desc.dwHeight                      = OGL_STAGING_ROWS;
    desc.ddsCaps.dwCaps                = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    desc.ddpfPixelFormat.dwSize        = sizeof(DDPIXELFORMAT);
    desc.ddpfPixelFormat.dwFlags       = DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = 32;
    desc.ddpfPixelFormat.dwRBitMask    = 0xff0000;
    desc.ddpfPixelFormat.dwGBitMask    = 0xff00;
    desc.ddpfPixelFormat.dwBBitMask    = 0xff;
    {
        void *s;

        if (((dd_create_t) VTBL(dd, DD_CREATESURFACE))(dd, &desc, &s, NULL) == DD_OK)
            staging = s;
    }
    make_unlock();
    return staging ? staging_at(offset) : NULL;
}

#define CLIP_MAX 256

/* Each window's clip list and rectangles (no two threads swap one window) */
static uint8_t rgn_buf[OGL_WINDOWS][sizeof(rgn_head_t) + CLIP_MAX * sizeof(ogl_rect_t)];
static DWORD   rect_buf[OGL_WINDOWS][2 * CLIP_MAX];

/* The window's visible rectangles on the screen, inside its client area
   (at ox, oy, w by h) and the screen, each two dwords as SWAP takes them
   (x0 | y0 << 16, x1 | y1 << 16). Returns how many. */
static DWORD
visible_rects(const ogl_window_t *w, LONG ox, LONG oy, DWORD *out)
{
    uint8_t     *buf = rgn_buf[w - windows];
    const LONG   sw = (LONG) sb->mode_width, sh = (LONG) sb->mode_height;
    void        *clip = window_clipper(w);
    rgn_head_t  *head = (rgn_head_t *) buf;
    ogl_rect_t   whole;
    ogl_rect_t  *list = &whole;
    DWORD        n    = 1, k, out_n = 0;

    whole.left   = ox;
    whole.top    = oy;
    whole.right  = ox + (LONG) w->w;
    whole.bottom = oy + (LONG) w->h;
    if (clip) {
        DWORD size = sizeof(rgn_buf[0]);

        /* Without its clip list the window's visible part is unknown:
           nothing is written (better than over the windows covering it). */
        if (((ddc_list_t) VTBL(clip, DDC_GETCLIPLIST))(clip, NULL, buf, &size) == DD_OK) {
            n    = (head->count < CLIP_MAX) ? head->count : CLIP_MAX;
            list = (ogl_rect_t *) (buf + sizeof(rgn_head_t));
        } else
            n = 0;
    }
    for (k = 0; k < n; k++) {
        LONG x0 = list[k].left, y0 = list[k].top, x1 = list[k].right, y1 = list[k].bottom;

        if (x0 < whole.left)
            x0 = whole.left;
        if (y0 < whole.top)
            y0 = whole.top;
        if (x1 > whole.right)
            x1 = whole.right;
        if (y1 > whole.bottom)
            y1 = whole.bottom;
        if (x0 < 0)
            x0 = 0;
        if (y0 < 0)
            y0 = 0;
        if (x1 > sw)
            x1 = sw;
        if (y1 > sh)
            y1 = sh;
        if ((x1 <= x0) || (y1 <= y0))
            continue;
        out[2 * out_n]     = (DWORD) x0 | ((DWORD) y0 << 16);
        out[2 * out_n + 1] = (DWORD) x1 | ((DWORD) y1 << 16);
        out_n++;
    }
    return out_n;
}

/* --- The cursor ------------------------------------------------------------------------ */

static DWORD screen_dc; /* the display's DC, for the driver's escape (made once) */

static WORD
screen_word(LONG v)
{
    return (WORD) ((v < 0) ? 0 : (v > 0x7fff) ? 0x7fff : v);
}

/* The DIB engine's software cursor kept out of the window's client area
   while the GPU writes it (VGPU98_ESC_ACCESS): else the cursor's saved
   background, an old frame, comes back where the cursor moves away.
   1 when it is out (cursor_back lets it back once the SHOW is done). */
static int
cursor_out(const ogl_window_t *w, LONG ox, LONG oy)
{
    vgpu98_escaccess_t a;

    if (detaching)
        return 0;
    if (!screen_dc) {
        make_lock();
        if (!screen_dc)
            screen_dc = CreateDCA("DISPLAY", NULL, NULL, NULL);
        make_unlock();
    }
    if (!screen_dc)
        return 0;
    a.begin  = 1;
    a.left   = screen_word(ox);
    a.top    = screen_word(oy);
    a.right  = screen_word(ox + (LONG) w->w - 1);
    a.bottom = screen_word(oy + (LONG) w->h - 1);
    return ExtEscape(screen_dc, VGPU98_ESC_ACCESS, sizeof(a), (const char *) &a, 0, NULL) > 0;
}

static void
cursor_back(void)
{
    vgpu98_escaccess_t a;

    memset(&a, 0, sizeof(a));
    ExtEscape(screen_dc, VGPU98_ESC_ACCESS, sizeof(a), (const char *) &a, 0, NULL);
}

/* --- Swaps and the front image ------------------------------------------------------------ */

/* A SWAP for the window into the batch: its flags, the screen's layout
   (the primary surface is at video memory's start, driver/ddraw.asm), the
   client area's place and the visible rectangles. With SHOW the batch is
   sent, the cursor kept out of the window until the GPU has written it;
   the batch's fence (0 without SHOW).

   `interval`: SwapBuffers' vertical blanks (WGL_EXT_swap_control), sent
   only for a window whose client area is the whole screen, all of it
   visible. The device waits for them in the command processor, which
   every later packet waits behind, GDI's drawing among them (driver/gpu.asm
   waits for the last fence before the DIB engine draws): a window on the
   desktop, or one with a dialog or pop-up over it, swaps at once, as a
   windowed Direct3D Blt does. Nor while the cursor is kept out of the
   window, which would hide it for the wait. The same SwapBuffers, and only
   they, carry OVERRIDE to a card with the V-sync overrides,
   whatever the interval: the card's "Always on" may make
   them wait, never the others. */
static DWORD
put_swap(ogl_ctx_t *c, ogl_window_t *w, DWORD flags, DWORD interval)
{
    DWORD      *rects = rect_buf[w - windows];
    ogl_point_t o     = { 0, 0 };
    DWORD       n = 0, fence;
    DWORD      *p;
    int         out = 0;

    if (!detaching && ClientToScreen(w->hwnd, &o))
        n = visible_rects(w, o.x, o.y, rects);
    /* SHOW writes the window's pixels, LOAD reads them: either with the
       cursor out of them when it is over them */
    if ((flags & (VGPU98_OGL_SWAP_SHOW | VGPU98_OGL_SWAP_LOAD)) && n)
        out = cursor_out(w, o.x, o.y);
    {
        const int screen = !o.x && !o.y && (w->w == sb->mode_width) && (w->h == sb->mode_height);
        const int whole  = (n == 1) && !rects[0] && (rects[1] == (sb->mode_width | (sb->mode_height << 16)));

        if (interval && !out && screen && whole)
            flags |= interval << VGPU98_OGL_SWAP_INTERVAL_SHIFT;
        if (ogl_vsync_overrides && (flags & VGPU98_OGL_SWAP_COPY) && (flags & VGPU98_OGL_SWAP_SHOW) && !out && screen &&
            whole)
            flags |= VGPU98_OGL_SWAP_OVERRIDE;
        /* Diagnostics "OpenGL": what became of a SwapBuffers' interval, said
           when it changes */
        if (diag_on && (flags & VGPU98_OGL_SWAP_COPY) && ogl_ext_on(OGL_EXT_SWAP_CONTROL))
            ogl_diag_swap(w, interval,
                          !interval ? OGL_SWAP_AT_ONCE
                          : !screen ? OGL_SWAP_DESKTOP
                          : !whole  ? OGL_SWAP_COVERED
                          : out     ? OGL_SWAP_CURSOR
                                    : OGL_SWAP_SENT,
                          n);
    }
    p = ogl_cmd(c, VGPU98_OGL_SWAP, VGPU98_OGL_LEN_SWAP + 2 * n);
    if (p) {
        p[0] = w->drawable;
        p[1] = flags;
        p[2] = 0;
        p[3] = sb->mode_pitch;
        p[4] = sb->mode_bpp;
        p[5] = ((DWORD) o.x & 0xffff) | ((DWORD) o.y << 16);
        p[6] = n;
        memcpy(&p[VGPU98_OGL_LEN_SWAP], rects, n * 8);
    }
    if (!(flags & VGPU98_OGL_SWAP_SHOW) && !out)
        return 0;
    fence = ogl_flush(c);
    if (out) {
        ogl_wait(fence);
        cursor_back();
    }
    return fence;
}

/* The window's SwapBuffers interval (WGL_EXT_swap_control; none on a
   device without it), at most the device's 4. */
static DWORD
swap_interval(const ogl_window_t *w)
{
    DWORD n = (w->swap_interval > 0) ? (DWORD) w->swap_interval : 0;

    if (!ogl_ext_on(OGL_EXT_SWAP_CONTROL))
        return 0;
    return (n > VGPU98_OGL_SWAP_INTERVAL_MAX) ? VGPU98_OGL_SWAP_INTERVAL_MAX : n;
}

int
ogl_swap(ogl_window_t *w)
{
    ogl_ctx_t *c = ogl_thread_current();
    DWORD      fence;

    if (!c || (c->win != w)) {
        /* The window is not this thread's current drawable: a swap of its
           buffers with whatever context drew into them, from another thread
           (its batch goes when that thread flushes): nothing to do here. */
        return 0;
    }
    if (!(w->format & VGPU98_OGL_DOUBLE)) {
        /* No back buffer: SwapBuffers flushes, and shows what was drawn
           into the front buffer. */
        ogl_front_show(c, 1);
        ogl_flush(c);
        ogl_diag_swapped();
        return 1;
    }
    /* The frame goes at the size it was drawn at: a new client size is
       taken at the next glClear or wglMakeCurrent (resizing first would
       show a cleared frame). */
    if (!c->opened || !w->drawable || (c->bound != w->drawable)) {
        if (!ogl_objects(c))
            return 0;
    }
    /* At most two swaps in flight: this one waits for the one before last
       (with a swap interval, the device's waits for the vertical blank hold
       the program to the refresh rate through it). */
    if (w->swap_fence[0] && !gpu_fence_done(w->swap_fence[0]))
        ogl_wait(w->swap_fence[0]);
    ogl_sync(c);
    fence              = put_swap(c, w, VGPU98_OGL_SWAP_COPY | VGPU98_OGL_SWAP_SHOW, swap_interval(w));
    w->swap_fence[0]   = w->swap_fence[1];
    w->swap_fence[1]   = fence;
    w->front_drawn     = 0;
    w->front_shown_ms  = GetTickCount();
    w->front_loaded    = 1; /* the front image is the frame swapped */
    ogl_diag_swapped();
    return 1;
}

void
ogl_front_show(ogl_ctx_t *c, int now)
{
    ogl_window_t *w = c->win;

    /* A DRAW being built is drawing too (glFlush, glFinish, the swap of a
       single-buffered window). */
    if (now)
        ogl_draw_flush(c);
    if (!w || !w->drawable || !w->front_drawn)
        return;
    if (!now && (GetTickCount() - w->front_shown_ms < FRONT_SHOW_MS))
        return;
    ogl_sync(c);
    w->front_drawn    = 0;
    w->front_shown_ms = GetTickCount();
    put_swap(c, w, VGPU98_OGL_SWAP_SHOW, 0);
}

void
ogl_front_load(ogl_ctx_t *c)
{
    ogl_window_t *w = c->win;

    if (!w || !w->drawable || w->front_loaded)
        return;
    w->front_loaded = 1;
    ogl_sync(c);
    put_swap(c, w, VGPU98_OGL_SWAP_LOAD, 0);
}

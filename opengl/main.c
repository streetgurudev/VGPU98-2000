/*
 * vgpu98gl.dll: the ICD's entry points.
 * DllMain; finding the card; the process's heap and contexts;
 * the pixel formats; the 18 Drv* exports Windows' opengl32.dll calls.
 *
 * opengl32.dll finds the ICD through the display driver's OPENGL_GETINFO
 * escape and the "VGPU98" value under OpenGLdrivers, loads this DLL into
 * the program, and calls:
 * - DrvDescribePixelFormat and DrvSetPixelFormat for GDI's pixel format
 *   calls on a window of the card's desktop (our formats come first, then
 *   Windows' own software ones);
 * - DrvCreateContext, DrvDeleteContext, DrvSetContext (wglMakeCurrent: it
 *   returns the table of GL's 336 functions), DrvReleaseContext,
 *   DrvShareLists, DrvCopyContext;
 * - DrvSwapBuffers;
 * - the layer-plane calls, which find no planes, and DrvGetProcAddress,
 *   which finds the offered extensions' functions (ext.c).
 *
 * The card is found once a process, at the first call that needs it, not
 * in DllMain: GDI's calls are best kept out of the loader's lock.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "gpuenv.h"
#include "state.h"
#include "texobj.h"

/* The card, for the ring too (gpuenv.h). */
vgpu98_shared_t *sb;
volatile DWORD  *regs;
uint8_t         *vram; /* video memory, through the display driver's mapping */
int              ogl_vsync_overrides;
icd_ring_diag_t  diag;

static DWORD heap;
static DWORD tls = 0xffffffffu; /* TLS_OUT_OF_INDEXES until DllMain's attach */
static int   found;

/* 0 not looked for, 1 being looked for (by another thread), 2 done. */
static volatile DWORD look_state;

ogl_ctx_t *ogl_ctx_table[OGL_CONTEXTS];

/* The current context: TLS, and while only one thread has made a context
   current, that thread's in a plain variable as well (a GL call reads it
   without a call into KERNEL32). */
static ogl_ctx_t *volatile single_ctx;
static volatile DWORD      single_thread;
static volatile int        multi; /* a second thread made a context current: TLS alone from then */

BOOL STDCALL DllMain(DWORD dll_instance, DWORD reason, void *reserved);
static void set_current(ogl_ctx_t *c);
static void drv_lock(void);
static int  drv_trylock(void);
static void drv_unlock(void);
static volatile LONG drv_lock_word;

/* --- DllMain -------------------------------------------------------------------- */

BOOL STDCALL
DllMain(DWORD dll_instance, DWORD reason, void *reserved)
{
    if (reason == 1) { /* DLL_PROCESS_ATTACH */
        ogl_diag_instance(dll_instance);
        heap = GetProcessHeap();
        tls  = TlsAlloc();
        if (tls == 0xffffffffu)
            return 0;
    } else if (reason == 0) { /* DLL_PROCESS_DETACH */
        /* At the process's end a killed thread may hold the exports' lock:
           let go of it (final audit, item 2). */
        if (reserved)
            __atomic_store_n(&drv_lock_word, 0, __ATOMIC_RELEASE);
        ogl_detach(reserved != NULL);
        if (tls != 0xffffffffu)
            TlsFree(tls);
        tls = 0xffffffffu;
        /* Unloaded by the program: its filter no longer the DLL's, whose
           code goes. */
        if (!reserved)
            ogl_fault_catcher_end();
    } else if (reason == 3) { /* DLL_THREAD_DETACH */
        /* A thread ending with a context current lets it go, so another
           thread can make it current or delete it. */
        ogl_ctx_t *c = ogl_thread_current();

        if (c) {
            /* Under the loader's lock: the exports' lock only if it is free
               (a swap holding it may be loading DirectDraw's DLL, which
               waits for the loader's lock: final audit, item 3). Without
               it the batch is not sent now; it goes with the context's
               next flush, on whichever thread makes it current. */
            const int locked = drv_trylock();

            if (locked)
                ogl_flush(c);
            c->thread = 0;
            c->win    = NULL;
            set_current(NULL);
            if (locked)
                drv_unlock();
        }
    }
    return 1;
}

/* --- The heap --------------------------------------------------------------------- */

void *
ogl_alloc(DWORD bytes)
{
    return HeapAlloc(heap, HEAP_ZERO_MEMORY, bytes ? bytes : 1);
}

void *
ogl_realloc(void *p, DWORD bytes)
{
    if (!p)
        return ogl_alloc(bytes);
    return HeapReAlloc(heap, HEAP_ZERO_MEMORY, p, bytes ? bytes : 1);
}

void
ogl_free(void *p)
{
    if (p)
        HeapFree(heap, 0, p);
}

/* --- Finding the card ------------------------------------------------------------- */

/* The display driver's escape (VGPU98_ESC_INFO, as the Glide DLLs ask it),
   the shared block of this build, BAR0, and PROFILE.OGL with 3D. */
static void
look_for_card(void)
{
    vgpu98_escquery_t query;
    vgpu98_escinfo_t  info;
    vgpu98_shared_t  *s   = NULL;
    const int         esc = VGPU98_ESC_INFO;
    DWORD             dc  = CreateDCA("DISPLAY", NULL, NULL, NULL);
    volatile DWORD   *r;
    DWORD             profile;

    if (!dc)
        return;
    query.magic    = ESC_INFO_MAGIC;
    query.out_size = sizeof(info);
    memset(&info, 0, sizeof(info));
    if ((ExtEscape(dc, QUERYESCSUPPORT, sizeof(esc), (const char *) &esc, 0, NULL) > 0) &&
        (ExtEscape(dc, VGPU98_ESC_INFO, sizeof(query), (const char *) &query, sizeof(info), (char *) &info) > 0) &&
        (info.magic == ESC_INFO_MAGIC) && ((info.size == sizeof(info)) || (info.size == ESC_INFO_HEAD)))
        s = (vgpu98_shared_t *) (uintptr_t) info.shared_linear;
    DeleteDC(dc);
    if (!s || (s->magic != SB_MAGIC))
        return; /* not the VGPU98 2000's display driver: nobody to tell */
    /* The block's first fields keep their offsets in every version, so a
       block from another build still leads to the log. */
    r = (volatile DWORD *) (uintptr_t) s->regs_linear;
    if (!r || (r[VGPU98_REG_ID / 4] != VGPU98_ID_MAGIC))
        return;
    if ((s->version != SB_VERSION) || (s->size != sizeof(vgpu98_shared_t)) || (info.sb_version != SB_VERSION)) {
        ogl_diag_start(r, NULL);
        ogl_event("no OpenGL: another build's display driver (block %u/%u bytes, answer %u; this DLL's %u/%u)", s->version,
                  s->size, info.sb_version, SB_VERSION, (DWORD) sizeof(vgpu98_shared_t));
        ogl_diag_stop(); /* nothing more from this process under a lock of its own */
        return;
    }
    ogl_diag_start(r, s);
    profile = r[VGPU98_REG_PROFILE / 4];
    if ((profile & (VGPU98_PROFILE_OGL | VGPU98_PROFILE_RENDER3D)) != (VGPU98_PROFILE_OGL | VGPU98_PROFILE_RENDER3D)) {
        ogl_event("no OpenGL: the GPU's profile %x has %s", profile,
                  !(profile & VGPU98_PROFILE_RENDER3D) ? "no 3D (the host GPU unavailable)" : "no OpenGL device (an older 86Box)");
        return;
    }
    sb    = s;
    regs  = r;
    vram  = (uint8_t *) (uintptr_t) s->fb_linear;
    found = 1;
    ogl_vsync_overrides = (profile & VGPU98_PROFILE_VSYNC) != 0;
    gpu_ring_choose(profile);
    /* The extensions its command set draws. */
    ogl_ext_init(r[VGPU98_REG_COMMAND_SET / 4]);
    ogl_diag_loaded(r[VGPU98_REG_COMMAND_SET / 4]);
    ogl_diag_check(&info);
    ogl_fault_catcher();
}

int
ogl_card(void)
{
    DWORD expect = 0;

    if (__atomic_load_n(&look_state, __ATOMIC_ACQUIRE) == 2)
        return found;
    if (__atomic_compare_exchange_n(&look_state, &expect, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        look_for_card();
        __atomic_store_n(&look_state, 2, __ATOMIC_RELEASE);
    } else {
        while (__atomic_load_n(&look_state, __ATOMIC_ACQUIRE) != 2)
            Sleep(1);
    }
    return found;
}

/* --- The current context -------------------------------------------------------------- */

ogl_ctx_t *
ogl_thread_current(void)
{
    return (tls != 0xffffffffu) ? (ogl_ctx_t *) TlsGetValue(tls) : NULL;
}

ogl_ctx_t *
ogl_current(void)
{
    if (!multi)
        return single_ctx;
    return ogl_thread_current();
}

static void
set_current(ogl_ctx_t *c)
{
    const DWORD me = GetCurrentThreadId();

    TlsSetValue(tls, c);
    if (multi)
        return;
    if (!single_ctx || (single_thread == me)) {
        single_thread = c ? me : 0;
        single_ctx    = c;
    } else if (c)
        multi = 1; /* another thread has one current: TLS alone from now */
}

/* --- Pixel formats ------------------------------------------------------------------- */

#define F(color, depth, dbl) \
    (VGPU98_OGL_COLOR_##color | ((DWORD) VGPU98_OGL_DEPTH_##depth << VGPU98_OGL_DEPTH_SHIFT) | ((dbl) ? VGPU98_OGL_DOUBLE : 0))

static const DWORD formats16[] = {
    F(565, 16, 1), F(565, 24S8, 1), F(565, 16, 0), F(565, 24S8, 0),
};
static const DWORD formats32[] = {
    F(XRGB8888, 24S8, 1), F(XRGB8888, 16, 1), F(ARGB8888, 24S8, 1),
    F(XRGB8888, 24S8, 0), F(XRGB8888, 16, 0), F(ARGB8888, 24S8, 0),
};

#undef F

int
ogl_formats(DWORD bpp, const DWORD **list)
{
    if (bpp == 16) {
        *list = formats16;
        return (int) ARRAY_SIZE(formats16);
    }
    if (bpp == 32) {
        *list = formats32;
        return (int) ARRAY_SIZE(formats32);
    }
    *list = NULL;
    return 0;
}

int
ogl_format_bits(DWORD format, int which)
{
    const DWORD color = format & VGPU98_OGL_COLOR_MASK;
    const DWORD depth = (format & VGPU98_OGL_DEPTH_MASK) >> VGPU98_OGL_DEPTH_SHIFT;

    switch (which) {
        case OGL_BITS_RED:
        case OGL_BITS_BLUE:
            return (color == VGPU98_OGL_COLOR_565) ? 5 : 8;
        case OGL_BITS_GREEN:
            return (color == VGPU98_OGL_COLOR_565) ? 6 : 8;
        case OGL_BITS_ALPHA:
            return (color == VGPU98_OGL_COLOR_ARGB8888) ? 8 : 0;
        case OGL_BITS_DEPTH:
            return (depth == VGPU98_OGL_DEPTH_16) ? 16 : (depth == VGPU98_OGL_DEPTH_24S8) ? 24 : 0;
        case OGL_BITS_STENCIL:
            return (depth == VGPU98_OGL_DEPTH_24S8) ? 8 : 0;
        default:
            return 0;
    }
}

void
ogl_describe(DWORD format, ogl_pfd_t *pfd)
{
    const int is565 = (format & VGPU98_OGL_COLOR_MASK) == VGPU98_OGL_COLOR_565;

    memset(pfd, 0, sizeof(*pfd));
    pfd->nSize      = sizeof(*pfd);
    pfd->nVersion   = 1;
    pfd->dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
    if (format & VGPU98_OGL_DOUBLE)
        pfd->dwFlags |= PFD_DOUBLEBUFFER | PFD_SWAP_COPY;
    pfd->iPixelType = PFD_TYPE_RGBA;
    /* cColorBits is the desktop's depth, as Windows' own formats on a
       32-bit desktop give it (a request for 24 still finds ours first). */
    pfd->cColorBits   = is565 ? 16 : 32;
    pfd->cRedBits     = (uint8_t) ogl_format_bits(format, OGL_BITS_RED);
    pfd->cGreenBits   = (uint8_t) ogl_format_bits(format, OGL_BITS_GREEN);
    pfd->cBlueBits    = (uint8_t) ogl_format_bits(format, OGL_BITS_BLUE);
    pfd->cAlphaBits   = (uint8_t) ogl_format_bits(format, OGL_BITS_ALPHA);
    pfd->cRedShift    = is565 ? 11 : 16;
    pfd->cGreenShift  = is565 ? 5 : 8;
    pfd->cBlueShift   = 0;
    pfd->cAlphaShift  = pfd->cAlphaBits ? 24 : 0;
    pfd->cDepthBits   = (uint8_t) ogl_format_bits(format, OGL_BITS_DEPTH);
    pfd->cStencilBits = (uint8_t) ogl_format_bits(format, OGL_BITS_STENCIL);
    pfd->iLayerType   = PFD_MAIN_PLANE;
}

/* --- The Drv* exports ------------------------------------------------------------------ */

typedef DWORD DHGLRC;
typedef void(STDCALL *ogl_set_table_t)(const ogl_proc_table_t *table);

BOOL STDCALL   DrvValidateVersion(DWORD version);
void STDCALL   DrvSetCallbackProcs(int n, void **procs);
LONG STDCALL   DrvDescribePixelFormat(DWORD hdc, int pixel_format, DWORD bytes, ogl_pfd_t *pfd);
BOOL STDCALL   DrvSetPixelFormat(DWORD hdc, LONG pixel_format);
DHGLRC STDCALL DrvCreateContext(DWORD hdc);
DHGLRC STDCALL DrvCreateLayerContext(DWORD hdc, int layer);
BOOL STDCALL   DrvDeleteContext(DHGLRC rc);
const ogl_proc_table_t *STDCALL DrvSetContext(DWORD hdc, DHGLRC rc, ogl_set_table_t set_table);
BOOL STDCALL   DrvReleaseContext(DHGLRC rc);
BOOL STDCALL   DrvCopyContext(DHGLRC src, DHGLRC dst, DWORD mask);
BOOL STDCALL   DrvShareLists(DHGLRC a, DHGLRC b);
BOOL STDCALL   DrvSwapBuffers(DWORD hdc);
BOOL STDCALL   DrvSwapLayerBuffers(DWORD hdc, DWORD planes);
void *STDCALL  DrvGetProcAddress(const char *name);
BOOL STDCALL   DrvDescribeLayerPlane(DWORD hdc, int pixel_format, int layer, DWORD bytes, void *lpd);
int STDCALL    DrvSetLayerPaletteEntries(DWORD hdc, int layer, int start, int n, const DWORD *colors);
int STDCALL    DrvGetLayerPaletteEntries(DWORD hdc, int layer, int start, int n, DWORD *colors);
BOOL STDCALL   DrvRealizeLayerPalette(DWORD hdc, int layer, BOOL realize);

/* The version the display driver's escape gives: any (Windows 98's
   opengl32.dll passes it, or nothing). */
BOOL STDCALL
DrvValidateVersion(DWORD version)
{
    (void) version;
    return 1;
}

/* opengl32.dll's callbacks: not needed, the ICD keeps its own current
   context per thread. */
void STDCALL
DrvSetCallbackProcs(int n, void **procs)
{
    (void) n;
    (void) procs;
}

/* GDI's DescribePixelFormat semantics: the count, and the format's
   description when it is one of ours and there is room. */
LONG STDCALL
DrvDescribePixelFormat(DWORD hdc, int pixel_format, DWORD bytes, ogl_pfd_t *pfd)
{
    const DWORD *list;
    int          n;

    (void) hdc;
    if (!ogl_card())
        return 0;
    n = ogl_formats(sb->mode_bpp, &list);
    if (pfd && (pixel_format >= 1) && (pixel_format <= n) && (bytes >= sizeof(ogl_pfd_t)))
        ogl_describe(list[pixel_format - 1], pfd);
    else if (pfd && ((pixel_format < 1) || (pixel_format > n)))
        return 0;
    return n;
}

/* --- The exports' lock ----------------------------------------------------------------- */

/* The Drv* exports that change the ICD's tables (windows, contexts, the
   DirectDraw objects) take turns: any thread of the program may call them
   (drv_lock_word, declared above for DllMain). */
static int
drv_trylock(void)
{
    int spins;

    for (spins = 0; spins < 16; spins++) {
        if (!__atomic_exchange_n(&drv_lock_word, 1, __ATOMIC_ACQUIRE))
            return 1;
    }
    return 0;
}

static void
drv_lock(void)
{
    int spins = 0;

    while (__atomic_exchange_n(&drv_lock_word, 1, __ATOMIC_ACQUIRE))
        Sleep((++spins < 16) ? 0 : 1);
}

static void
drv_unlock(void)
{
    __atomic_store_n(&drv_lock_word, 0, __ATOMIC_RELEASE);
}

static BOOL
set_pixel_format(DWORD hdc, LONG pixel_format)
{
    const DWORD *list;
    int          n;

    if (!ogl_card())
        return 0;
    n = ogl_formats(sb->mode_bpp, &list);
    if ((pixel_format < 1) || (pixel_format > n) || !ogl_window_set_format(hdc, (int) pixel_format, list[pixel_format - 1])) {
        if (diag_on)
            ogl_diag_format(hdc, pixel_format, n, sb->mode_bpp);
        return 0;
    }
    if (diag_on) {
        const ogl_window_t *w = ogl_window_of(hdc);

        if (w)
            ogl_diag_pixel_format(w->hwnd, (int) pixel_format, w->format);
    }
    return 1;
}

BOOL STDCALL
DrvSetPixelFormat(DWORD hdc, LONG pixel_format)
{
    BOOL r;

    drv_lock();
    r = set_pixel_format(hdc, pixel_format);
    drv_unlock();
    return r;
}

static DHGLRC
create_context(DWORD hdc, int layer)
{
    ogl_window_t *w;
    ogl_ctx_t    *c;
    int           i;

    if ((layer != 0) || !ogl_card())
        return 0;
    w = ogl_window_of(hdc);
    if (!w)
        return 0; /* no pixel format set on the window */
    for (i = 0; i < OGL_CONTEXTS; i++) {
        if (!ogl_ctx_table[i])
            break;
    }
    if (i == OGL_CONTEXTS)
        return 0;
    c = ogl_alloc(sizeof(ogl_ctx_t));
    if (!c)
        return 0;
    c->batch = ogl_alloc(OGL_BATCH_DWORDS * 4);
    c->group = ogl_alloc(sizeof(ogl_group_t));
    if (!c->batch || !c->group) {
        ogl_free(c->batch);
        ogl_free(c->group);
        ogl_free(c);
        return 0;
    }
    c->id           = ogl_new_id();
    c->slot         = i;
    c->pixel_format = w->pixel_format;
    c->format       = w->format;
    c->group->id    = ogl_new_id();
    c->group->refs  = 1;
    if (!ogl_state_init(c)) {
        ogl_state_free(c);
        ogl_free(c->batch);
        ogl_free(c->group);
        ogl_free(c);
        return 0;
    }
    ogl_ctx_table[i] = c;
    return (DHGLRC) (i + 1);
}

DHGLRC STDCALL
DrvCreateLayerContext(DWORD hdc, int layer)
{
    DHGLRC rc;

    drv_lock();
    rc = create_context(hdc, layer);
    drv_unlock();
    return rc;
}

DHGLRC STDCALL
DrvCreateContext(DWORD hdc)
{
    return DrvCreateLayerContext(hdc, 0);
}

static ogl_ctx_t *
ctx_of(DHGLRC rc)
{
    return ((rc >= 1) && (rc <= OGL_CONTEXTS)) ? ogl_ctx_table[rc - 1] : NULL;
}

static BOOL
delete_context(DHGLRC rc)
{
    ogl_ctx_t *c = ctx_of(rc);

    if (!c)
        return 0;
    if (c->thread && (c->thread != GetCurrentThreadId()))
        return 0; /* current to another thread (opengl32.dll refuses that first) */
    if (c->thread)
        set_current(NULL);
    ogl_close(c, 0);
    ogl_ctx_table[c->slot] = NULL;
    ogl_state_free(c);
    ogl_free(c->batch);
    ogl_free(c);
    return 1;
}

BOOL STDCALL
DrvDeleteContext(DHGLRC rc)
{
    BOOL r;

    drv_lock();
    r = delete_context(rc);
    drv_unlock();
    return r;
}

/* wglMakeCurrent: the context current on this thread, on the DC's window
   (its drawable made or resized on the device, bound to the context); the
   table of GL's functions. NULL if the window is not one the context can
   draw into. */
static const ogl_proc_table_t *
set_context(DWORD hdc, DHGLRC rc, ogl_set_table_t set_table)
{
    ogl_ctx_t    *c = ctx_of(rc);
    ogl_window_t *w = ogl_window_of(hdc);
    ogl_ctx_t    *old;

    (void) set_table;
    if (!c || !w || (w->format != c->format) || (c->thread && (c->thread != GetCurrentThreadId())))
        return NULL;
    old = ogl_thread_current();
    if (old && (old != c)) {
        ogl_flush(old);
        old->thread = 0;
        old->win    = NULL;
    }
    /* Another context or window: the window's pixels may have changed since
       its front image last had them (the same context on the same window
       keeps its front drawing not shown yet). */
    if ((old != c) || (c->win != w))
        w->front_loaded = 0;
    c->thread = GetCurrentThreadId();
    c->win    = w;
    set_current(c);
    ogl_objects(c);
    /* The first time a context is made current, the viewport and the
       scissor box take the window's size (GL 1.1). */
    if (!c->made_current) {
        c->made_current          = 1;
        c->gl->viewport.x        = 0;
        c->gl->viewport.y        = 0;
        c->gl->viewport.w        = (GLsizei) w->w;
        c->gl->viewport.h        = (GLsizei) w->h;
        c->gl->scissor.x         = 0;
        c->gl->scissor.y         = 0;
        c->gl->scissor.w         = (GLsizei) w->w;
        c->gl->scissor.h         = (GLsizei) w->h;
        {
            const DWORD vp[4] = { 0, 0, w->w, w->h };

            ogl_regs(c, VGPU98_OGL_R_VIEWPORT, vp, 4);
            ogl_regs(c, VGPU98_OGL_R_SCISSOR, vp, 4);
        }
    }
    c->gl->win_w = (GLsizei) w->w;
    c->gl->win_h = (GLsizei) w->h;
    return &ogl_table;
}

const ogl_proc_table_t *STDCALL
DrvSetContext(DWORD hdc, DHGLRC rc, ogl_set_table_t set_table)
{
    const ogl_proc_table_t *t;

    drv_lock();
    t = set_context(hdc, rc, set_table);
    drv_unlock();
    return t;
}

BOOL STDCALL
DrvReleaseContext(DHGLRC rc)
{
    ogl_ctx_t *c;
    BOOL       r = 0;

    drv_lock();
    c = ctx_of(rc);
    if (c && (c->thread == GetCurrentThreadId())) {
        ogl_flush(c);
        c->thread = 0;
        c->win    = NULL;
        set_current(NULL);
        r = 1;
    }
    drv_unlock();
    return r;
}

/* wglCopyContext: the mask's attribute groups from one context into
   another, which no thread has current (as Windows asks). */
BOOL STDCALL
DrvCopyContext(DHGLRC src, DHGLRC dst, DWORD mask)
{
    ogl_ctx_t *a, *b;
    BOOL       r = 0;

    drv_lock();
    a = ctx_of(src);
    b = ctx_of(dst);
    if (a && b && (a != b) && !b->thread && !b->gl->in_begin)
        r = ogl_attrib_copy(a, b, mask) ? 1 : 0;
    drv_unlock();
    return r;
}

/* wglShareLists: the second context shares the first's display lists and
   textures (its share group), which GL allows only while it
   has none of its own: its group, alone and empty, is closed, and the
   context opened again on the device in the first's. */
static BOOL
share_lists(DHGLRC a, DHGLRC b)
{
    ogl_ctx_t   *src = ctx_of(a), *dst = ctx_of(b);
    ogl_group_t *old;
    int          i;

    if (!src || !dst)
        return 0;
    if (src->group == dst->group)
        return 1;
    old = dst->group;
    if ((old->refs > 1) || old->lists || old->textures)
        return 0;
    for (i = 0; i < 2; i++) {
        if (dst->gl->default_tex[i] && dst->gl->default_tex[i]->dev)
            return 0; /* its default textures have images on the device */
    }
    ogl_flush(dst);
    if (old->opened) {
        /* Closing the group ends the context with it on the device. */
        DWORD *p = ogl_cmd(dst, VGPU98_OGL_GROUP, VGPU98_OGL_LEN_GROUP);

        p[0] = old->id;
        p[1] = VGPU98_OGL_CLOSE;
        ogl_flush(dst);
    }
    ogl_free(old);
    dst->group = src->group;
    src->group->refs++;
    dst->opened = 0; /* opened again in the group, its registers sent whole */
    dst->bound  = 0;
    return 1;
}

BOOL STDCALL
DrvShareLists(DHGLRC a, DHGLRC b)
{
    BOOL r;

    drv_lock();
    r = share_lists(a, b);
    drv_unlock();
    return r;
}

BOOL STDCALL
DrvSwapBuffers(DWORD hdc)
{
    ogl_window_t *w;
    BOOL          r = 0;

    drv_lock();
    w = ogl_window_of(hdc);
    if (w)
        r = ogl_swap(w);
    drv_unlock();
    return r;
}

/* Only the main plane exists: a swap of it (WGL_SWAP_MAIN_PLANE). */
BOOL STDCALL
DrvSwapLayerBuffers(DWORD hdc, DWORD planes)
{
    if (!(planes & 1))
        return 0;
    return DrvSwapBuffers(hdc);
}

/* The offered extensions' functions (ext.c); opengl32.dll answers GL 1.1's
   own names. */
void *STDCALL
DrvGetProcAddress(const char *name)
{
    void *p;

    if (!ogl_card())
        return NULL;
    p = ogl_ext_proc(name);
    if (!p && diag_on)
        ogl_diag_miss(name);
    return p;
}

/* No overlay or underlay planes. */
BOOL STDCALL
DrvDescribeLayerPlane(DWORD hdc, int pixel_format, int layer, DWORD bytes, void *lpd)
{
    (void) hdc;
    (void) pixel_format;
    (void) layer;
    (void) bytes;
    (void) lpd;
    return 0;
}

int STDCALL
DrvSetLayerPaletteEntries(DWORD hdc, int layer, int start, int n, const DWORD *colors)
{
    (void) hdc;
    (void) layer;
    (void) start;
    (void) n;
    (void) colors;
    return 0;
}

int STDCALL
DrvGetLayerPaletteEntries(DWORD hdc, int layer, int start, int n, DWORD *colors)
{
    (void) hdc;
    (void) layer;
    (void) start;
    (void) n;
    (void) colors;
    return 0;
}

BOOL STDCALL
DrvRealizeLayerPalette(DWORD hdc, int layer, BOOL realize)
{
    (void) hdc;
    (void) layer;
    (void) realize;
    return 0;
}

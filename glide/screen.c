/*
 * The Glide core: the session on the card's Glide device, the batches, the
 * state registers, the screen.
 *
 * grSstWinOpen takes the screen as a Voodoo3's Glide does: DirectDraw's
 * exclusive mode for the program's window and the resolution at 16 bpp
 * (DirectDraw gives the desktop back when the program closes or ends),
 * then a session on the device with its buffers, and an offscreen surface
 * in video memory through which the frame buffer is read and written.
 * Commands go into a batch of the DLL's own, copied into the ring under
 * its lock as one, ended by a FENCE.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcore.h"

#define BATCH_DWORDS  16384 /* 64 KB */
#define SWAPS_KEPT    8
#define HELD_WAIT_MS  2000  /* a batch waits this long for the GPU held by a mode change */
#define FRONT_SHOW_MS 16    /* drawing into the front buffer goes to the device this often at least */

/* The staging surface's rows, as a Voodoo3's frame buffer has them: 2048
   pixels a row, so 4096 bytes for 16-bit pixels (reads, and 16-bit
   writes) and 8192 for 32-bit writes. */
#define STRIDE_WRITE 8192
#define STRIDE_READ  4096

/* DirectDraw, as much of it as the screen needs (its interfaces' method
   order is the SDK's). */
#define DDSCL_FULLSCREEN 0x00000001
#define DDSCL_NORMAL     0x00000008
#define DDSCL_EXCLUSIVE  0x00000010
#define DD_RELEASE             2
#define DD_QUERY               0
#define DD_CREATESURFACE       6
#define DD_RESTOREDISPLAYMODE  19
#define DD_SETCOOPERATIVELEVEL 20
#define DD_SETDISPLAYMODE      21
#define DDS_LOCK               25
#define DDS_UNLOCK             32
#define DD_LOCK_WAIT           0x00000001 /* DDLOCK_WAIT */

typedef struct dd_guid {
    DWORD    data1;
    uint16_t data2, data3;
    uint8_t  data4[8];
} dd_guid_t;

static const dd_guid_t iid_directdraw2 = { 0xB3A6F3E0, 0x2B43, 0x11CF, { 0xA2, 0xDE, 0x00, 0xAA, 0x00, 0xB9, 0x33, 0x56 } };

#define VTBL(obj, i) ((*(void ***) (obj))[i])

typedef HRESULT(STDCALL *dd_query_t)(void *self, const dd_guid_t *iid, void **out);
typedef DWORD(STDCALL *dd_release_t)(void *self);
typedef HRESULT(STDCALL *dd_coop_t)(void *self, DWORD hwnd, DWORD flags);
typedef HRESULT(STDCALL *dd_mode1_t)(void *self, DWORD w, DWORD h, DWORD bpp);
typedef HRESULT(STDCALL *dd_mode2_t)(void *self, DWORD w, DWORD h, DWORD bpp, DWORD refresh, DWORD flags);
typedef HRESULT(STDCALL *dd_restore_t)(void *self);
typedef HRESULT(STDCALL *dd_create_t)(void *self, DDSURFACEDESC *desc, void **surface, void *outer);
typedef HRESULT(STDCALL *dds_lock_t)(void *self, void *rect, DDSURFACEDESC *desc, DWORD flags, DWORD event);
typedef HRESULT(STDCALL *dds_unlock_t)(void *self, void *rect);

/* The session (one a process). */
static struct {
    int      open;
    int      lost; /* the screen's mode changed under it: nothing more drawn */
    DWORD    id;
    FxU32    w, h;
    int      n_color, aux;
    FxU32    color_format;
    DWORD    mode_serial;
    DWORD    regs[VGPU98_GS_COUNT];
    DWORD    dirty_lo, dirty_hi; /* registers [lo, hi) changed since sent */
    /* The registers as sent (recorded into a batch), and as the device has
       them before the batch being recorded: what goes again when it may
       have lost its state. */
    DWORD    sent[VGPU98_GS_COUNT];
    DWORD    base[VGPU98_GS_COUNT];
    int      synced; /* the device has the session (its OPEN's batch went) */
    DWORD    epoch;  /* sb->state_epoch at the last batch: a change may have lost what went */
    /* The screen */
    void    *dd;     /* IDirectDraw2, or IDirectDraw */
    int      dd2;
    DWORD    hwnd;
    int      exclusive, mode_set;
    void    *surface; /* the staging surface */
    uint8_t *staging;
    DWORD    staging_offset, staging_bytes;
    DWORD    stride_write, stride_read;
    /* Swaps: the fences of the last ones, oldest first */
    DWORD    swap_fence[SWAPS_KEPT];
    int      n_swaps;
    int      pending_max;
    DWORD    lfb_fence;
    DWORD    front_ms; /* GetTickCount at the last batch with drawing into the front buffer */
} gs;

#define NO_DRAW       0xffffffffu
#define DRAW_MAX      (VGPU98_GL_PACKET_BYTES / 4 - 4) /* a DRAW's payload dwords at most */

static DWORD batch[BATCH_DWORDS];
static DWORD batch_n;
static DWORD draw_at = NO_DRAW; /* the open DRAW's header in the batch: the batch's last command */
static DWORD last_fence;
static DWORD opens;
static int   dropped;     /* the last batch was dropped (the GPU held) */
static DWORD batch_epoch; /* sb->state_epoch as the last batch went into the ring */
static DWORD held_told;   /* batches dropped since the GPU was last free */
static int   detaching;   /* inside DllMain: no USER32 calls */

static void mode_lost(void);

/* --- Batches ---------------------------------------------------------------- */

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

/* The batch to the ring. `patient`: a GPU held by a mode change is waited
   for a while (the DLL's unload waits for nothing). */
static DWORD
flush(int patient)
{
    DWORD   i;
    DWORD   start;
    HRESULT hr;

    dropped = 0;
    draw_at = NO_DRAW;
    if (!batch_n || !regs || !sb)
        return last_fence;
    start = GetTickCount();
    while ((hr = gpu_begin()) == DDERR_SURFACELOST) {
        if (!patient || (GetTickCount() - start >= HELD_WAIT_MS))
            break;
        Sleep(5);
    }
    if (hr != DD_OK) {
        if (!held_told++)
            glide_event("a batch of %u dwords dropped: the GPU is held (the next ones while it is are not said)", batch_n);
        batch_n = 0;
        dropped = 1;
        /* What it set goes again with the next batch. */
        memcpy(gs.base, gs.sent, sizeof(gs.base));
        gs.epoch = sb->state_epoch - 1;
        return 0;
    }
    batch_epoch = sb->state_epoch; /* after any restart gpu_begin made */
    held_told   = 0;
    /* The screen's mode changed since the window opened (another program,
       or the desktop back; a mode change can end while the GPU is waited
       for): the session went with it on the device, so nothing of it goes
       (the lock let go with a FENCE alone). */
    if (gs.open && (sb->mode_serial != gs.mode_serial)) {
        batch_n    = 0;
        last_fence = gpu_end();
        if (!gs.lost)
            mode_lost();
        return 0;
    }
    /* The GPU restarted, or a batch was abandoned, since the last batch
       went: the device may have lost what went then. Its registers as they
       stood go first (the session itself outlasts a restart). */
    if (gs.synced && (sb->state_epoch != gs.epoch)) {
        static DWORD again[2 + VGPU98_GS_COUNT];

        again[0] = gs.id;
        again[1] = 0;
        memcpy(&again[2], gs.base, sizeof(gs.base));
        put(VGPU98_GL_STATE, 2 + VGPU98_GS_COUNT, again);
    }
    for (i = 0; i < batch_n;) {
        const DWORD op  = VGPU98_PKT_OP(batch[i]);
        const DWORD len = VGPU98_PKT_LEN(batch[i]);

        if (!put(op, len, &batch[i + 1]))
            break; /* the GPU stopped: the rest of the batch is lost with it */
        i += 1 + len;
    }
    {
        const int cut = i < batch_n;

        batch_n    = 0;
        last_fence = gpu_end();
        /* Cut short, or the GPU restarted while it went (a restart for room
           loses what it had not run): its registers go again with the next
           batch. */
        gs.epoch = (cut || (sb->state_epoch != batch_epoch)) ? sb->state_epoch - 1 : sb->state_epoch;
    }
    memcpy(gs.base, gs.sent, sizeof(gs.base));
    /* The mode changed while the batch went: the session went with it. */
    if (gs.open && !gs.lost && (sb->mode_serial != gs.mode_serial))
        mode_lost();
    return last_fence;
}

DWORD
glc_flush(void)
{
    return flush(1);
}

DWORD *
glc_cmd(DWORD op, DWORD len)
{
    DWORD *p;

    if (!gs.open || gs.lost || (len + 1 > BATCH_DWORDS))
        return NULL;
    if (batch_n + 1 + len > BATCH_DWORDS)
        glc_flush();
    draw_at  = NO_DRAW;
    p        = &batch[batch_n];
    p[0]     = VGPU98_PKT(op, len);
    p[1]     = gs.id;
    batch_n += 1 + len;
    return p + 2;
}

float *
glc_vertices(DWORD n)
{
    const DWORD dw = n * VGPU98_GL_VERTEX_DWORDS;
    DWORD      *p;

    if (!gs.open || gs.lost || !n || (dw > DRAW_MAX - 3))
        return NULL;
    glc_sync();
    /* The open DRAW goes on: nothing came after it, and it has room. */
    if ((draw_at != NO_DRAW) && (batch_n + dw <= BATCH_DWORDS)) {
        const DWORD len = VGPU98_PKT_LEN(batch[draw_at]);

        if (len + dw <= DRAW_MAX) {
            float *o = (float *) &batch[batch_n];

            batch[draw_at]     = VGPU98_PKT(VGPU98_GL_DRAW, len + dw);
            batch[draw_at + 3] += n;
            batch_n += dw;
            return o;
        }
    }
    p = glc_cmd(VGPU98_GL_DRAW, VGPU98_GL_LEN_DRAW + dw);
    if (!p)
        return NULL;
    draw_at = batch_n - (1 + VGPU98_GL_LEN_DRAW + dw);
    p[0]    = VGPU98_GL_TRIANGLES;
    p[1]    = n;
    glc_count(GLC_DRAWS);
    return (float *) &p[2];
}

int
glc_wait(DWORD v)
{
    if (!regs || !sb || !v)
        return 1;
    return gpu_wait(v) == DD_OK;
}

/* Everything done, the registers set since included. */
void
glc_finish(void)
{
    glc_sync();
    glc_wait(glc_flush());
}

/* --- Counters, and what is deferred -------------------------------------- */

static DWORD counters[GLC_COUNTERS];
static DWORD win_counters[GLC_COUNTERS]; /* the window's: since its open */
static DWORD deferred_told;

void
glc_count(int which)
{
    if ((which >= 0) && (which < GLC_COUNTERS)) {
        counters[which]++;
        win_counters[which]++;
    }
}

DWORD
glc_window_counter(int which)
{
    return ((which >= 0) && (which < GLC_COUNTERS)) ? win_counters[which] : 0;
}

void
glc_counter_reset(int which)
{
    if ((which >= 0) && (which < GLC_COUNTERS))
        counters[which] = 0;
}

void
glc_count_bytes(int which, DWORD n)
{
    if ((which >= 0) && (which < GLC_COUNTERS)) {
        counters[which] += n;
        win_counters[which] += n;
    }
}

DWORD
glc_counter(int which)
{
    return ((which >= 0) && (which < GLC_COUNTERS)) ? counters[which] : 0;
}

void
glc_deferred(DWORD what)
{
    static const struct {
        DWORD       bit;
        const char *text;
    } names[] = {
        { GLC_DEFER_AA, "edge antialiasing (grAADraw*, grEnable(GR_AA_ORDERED)): drawn without it" },
        { GLC_DEFER_COMPARE_TO_BIAS, "a depth compare against the bias (GR_*BUFFER_COMPARE_TO_BIAS): compared as usual" },
        { GLC_DEFER_YIQ, "a YIQ (NCC) texture: its Y shown as grey" },
        { GLC_DEFER_MULTIBASE, "a texture in more than one place (grTexMultibase): its first place alone" },
        { GLC_DEFER_LFB_PIPELINE, "a frame buffer write through the pixel pipeline: written plain" },
        { GLC_DEFER_ALPHA_BUFFER, "the aux buffer as an alpha buffer: not kept" },
        { GLC_DEFER_MULTIPASS, "the utility library's multipass helpers (guMP*): one pass, the first texture" },
    };
    size_t i;

    if (!(what & ~deferred_told))
        return;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if ((what & names[i].bit) && !(deferred_told & names[i].bit))
            glide_event("not done yet: %s", names[i].text);
    }
    deferred_told |= what;
}

int
glc_busy(void)
{
    if (!regs || !sb)
        return 0;
    return !gpu_fence_done(glc_flush());
}

/* Drawing went into the front buffer: on the screen without a swap, the
   batch going to the device at least every FRONT_SHOW_MS. */
void
glc_front_written(void)
{
    const DWORD now = GetTickCount();

    if (now - gs.front_ms >= FRONT_SHOW_MS) {
        gs.front_ms = now;
        glc_flush();
    }
}

/* Is drawing going into the front buffer: the render buffer is the front,
   or the only, buffer. */
int
glc_drawing_front(void)
{
    return (gs.n_color == 1) || (gs.regs[VGPU98_GS_RENDER_BUFFER] == GR_BUFFER_FRONTBUFFER);
}

/* --- State --------------------------------------------------------------- */

void
glc_reg(DWORD reg, DWORD value)
{
    if (reg >= VGPU98_GS_COUNT)
        return;
    if (gs.regs[reg] == value)
        return;
    gs.regs[reg] = value;
    if (gs.dirty_lo == gs.dirty_hi) {
        gs.dirty_lo = reg;
        gs.dirty_hi = reg + 1;
    } else {
        if (reg < gs.dirty_lo)
            gs.dirty_lo = reg;
        if (reg + 1 > gs.dirty_hi)
            gs.dirty_hi = reg + 1;
    }
}

DWORD
glc_reg_get(DWORD reg)
{
    return (reg < VGPU98_GS_COUNT) ? gs.regs[reg] : 0;
}

const DWORD *
glc_regs(void)
{
    return gs.regs;
}

void
glc_regs_load(const DWORD *r)
{
    memcpy(gs.regs, r, sizeof(gs.regs));
    gs.dirty_lo = 0;
    gs.dirty_hi = VGPU98_GS_COUNT;
}

void
glc_sync(void)
{
    const DWORD n = gs.dirty_hi - gs.dirty_lo;
    DWORD      *p;

    if (!n)
        return;
    p = glc_cmd(VGPU98_GL_STATE, 2 + n);
    if (p) {
        p[0] = gs.dirty_lo;
        memcpy(&p[1], &gs.regs[gs.dirty_lo], n * sizeof(DWORD));
        memcpy(&gs.sent[gs.dirty_lo], &gs.regs[gs.dirty_lo], n * sizeof(DWORD));
    }
    gs.dirty_lo = gs.dirty_hi = 0;
}

DWORD
glc_argb(FxU32 c)
{
    switch (gs.color_format) {
        case GR_COLORFORMAT_ABGR:
            return (c & 0xff00ff00u) | ((c & 0xff) << 16) | ((c >> 16) & 0xff);
        case GR_COLORFORMAT_RGBA:
            return (c >> 8) | (c << 24);
        case GR_COLORFORMAT_BGRA:
            return ((c & 0xff) << 24) | (((c >> 8) & 0xff) << 16) | (((c >> 16) & 0xff) << 8) | (c >> 24);
        default:
            return c;
    }
}

/* --- The screen ---------------------------------------------------------- */

/* GR_RESOLUTION_320x200 (0) to GR_RESOLUTION_2048x2048 (17h). */
static const uint16_t resolutions[][2] = {
    { 320, 200 },   { 320, 240 },   { 400, 256 },   { 512, 384 },   { 640, 200 },   { 640, 350 },
    { 640, 400 },   { 640, 480 },   { 800, 600 },   { 960, 720 },   { 856, 480 },   { 512, 256 },
    { 1024, 768 },  { 1280, 1024 }, { 1600, 1200 }, { 400, 300 },   { 1152, 864 },  { 1280, 960 },
    { 1600, 1024 }, { 1792, 1344 }, { 1856, 1392 }, { 1920, 1440 }, { 2048, 1536 }, { 2048, 2048 },
};

#define RES_640x480 7

/* GR_REFRESH_60Hz (0) to GR_REFRESH_120Hz (8). */
static const uint8_t refreshes[] = { 60, 70, 72, 75, 80, 90, 100, 85, 120 };

static void
dd_release(void **obj)
{
    if (*obj)
        ((dd_release_t) VTBL(*obj, DD_RELEASE))(*obj);
    *obj = NULL;
}

/* The staging surface: the write rows, then the read rows. 0 if video
   memory has no room for it. */
static int
staging_make(DWORD stride_write, DWORD stride_read)
{
    DDSURFACEDESC desc;
    const DWORD   total  = (stride_write + stride_read) * gs.h;
    const DWORD   width  = stride_write / 4;
    const DWORD   height = (total + stride_write - 1) / stride_write;

    memset(&desc, 0, sizeof(desc));
    desc.dwSize                        = sizeof(desc);
    desc.dwFlags                       = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth                       = width;
    desc.dwHeight                      = height;
    desc.ddsCaps.dwCaps                = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    desc.ddpfPixelFormat.dwSize        = sizeof(DDPIXELFORMAT);
    desc.ddpfPixelFormat.dwFlags       = DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = 32;
    desc.ddpfPixelFormat.dwRBitMask    = 0xff0000;
    desc.ddpfPixelFormat.dwGBitMask    = 0xff00;
    desc.ddpfPixelFormat.dwBBitMask    = 0xff;
    if (((dd_create_t) VTBL(gs.dd, DD_CREATESURFACE))(gs.dd, &desc, &gs.surface, NULL) != DD_OK) {
        gs.surface = NULL;
        return 0;
    }
    memset(&desc, 0, sizeof(desc));
    desc.dwSize = sizeof(desc);
    if (((dds_lock_t) VTBL(gs.surface, DDS_LOCK))(gs.surface, NULL, &desc, DD_LOCK_WAIT, 0) == DD_OK) {
        const DWORD at = (DWORD) desc.lpSurface - sb->fb_linear;

        ((dds_unlock_t) VTBL(gs.surface, DDS_UNLOCK))(gs.surface, (void *) desc.lpSurface);
        /* One run of video memory, rows of the stride asked for. */
        if (((DWORD) desc.lpSurface >= sb->fb_linear) && (at < sb->vram_size) && ((DWORD) desc.lPitch == stride_write) &&
            (total <= sb->vram_size - at)) {
            gs.staging        = (uint8_t *) desc.lpSurface;
            gs.staging_offset = at;
            gs.staging_bytes  = total;
            gs.stride_write   = stride_write;
            gs.stride_read    = stride_read;
            return 1;
        }
    }
    dd_release(&gs.surface);
    return 0;
}

/* DirectDraw: exclusive mode, the resolution at 16 bpp, the staging
   surface. What fails is said, and the session opens all the same: the
   screen then shows it scaled to the mode Windows has. */
static void
screen_take(FxU32 hwnd, FxU32 hz)
{
    DWORD   bpp = 16;
    HRESULT hr;
    void   *dd1 = NULL;

    gs.hwnd = hwnd ? hwnd : GetActiveWindow();
    if (!gs.hwnd)
        gs.hwnd = GetForegroundWindow();
    if (DirectDrawCreate(NULL, &dd1, NULL) != DD_OK) {
        glide_event("grSstWinOpen: DirectDraw is not there; the screen keeps its mode");
        return;
    }
    if ((((dd_query_t) VTBL(dd1, DD_QUERY))(dd1, &iid_directdraw2, &gs.dd) == DD_OK) && gs.dd) {
        gs.dd2 = 1;
        dd_release(&dd1);
    } else {
        gs.dd  = dd1;
        gs.dd2 = 0;
    }
    hr = gs.hwnd ? ((dd_coop_t) VTBL(gs.dd, DD_SETCOOPERATIVELEVEL))(gs.dd, gs.hwnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN)
                 : DDERR_GENERIC;
    gs.exclusive = hr == DD_OK;
    if (!gs.exclusive) {
        glide_event("grSstWinOpen: no exclusive mode for window %x (%x); the mode is set for the desktop", gs.hwnd, hr);
        ((dd_coop_t) VTBL(gs.dd, DD_SETCOOPERATIVELEVEL))(gs.dd, gs.hwnd, DDSCL_NORMAL);
    }
    /* The desktop in the window's mode already: 8 bpp instead, as a
       Voodoo3's Glide sets it, so the desktop's mode comes back as a change
       of mode when the program ends, however it ends (the screen shows
       Glide's own buffers meanwhile). */
    if ((sb->mode_width == gs.w) && (sb->mode_height == gs.h) && (sb->mode_bpp == 16)) {
        bpp = 8;
        glide_dbg("grSstWinOpen: the desktop is %ux%u at 16 bpp already: 8 bpp set, as a Voodoo3 does", gs.w, gs.h);
    }
    /* The refresh rate asked for, else the mode's default, as a Voodoo3's
       Glide tries them. */
    if (gs.dd2) {
        hr = ((dd_mode2_t) VTBL(gs.dd, DD_SETDISPLAYMODE))(gs.dd, gs.w, gs.h, bpp, hz, 0);
        if (hr != DD_OK)
            hr = ((dd_mode2_t) VTBL(gs.dd, DD_SETDISPLAYMODE))(gs.dd, gs.w, gs.h, bpp, 0, 0);
    } else
        hr = ((dd_mode1_t) VTBL(gs.dd, DD_SETDISPLAYMODE))(gs.dd, gs.w, gs.h, bpp);
    gs.mode_set = hr == DD_OK;
    if (!gs.mode_set)
        glide_event("grSstWinOpen: Windows refused %ux%u at %u bpp (%x); the screen shows Glide scaled to its mode", gs.w,
                    gs.h, bpp, hr);

    /* The staging surface as a Voodoo3's rows, else (video memory short)
       rows of the window's width alone. */
    if (!staging_make(STRIDE_WRITE, STRIDE_READ) && !staging_make(gs.w * 4, gs.w * 2)) {
        glide_event("grSstWinOpen: no %u KB of video memory for the frame buffer's transfers; grLfbLock will fail",
                    (gs.w * 6 * gs.h) >> 10);
    } else if (gs.stride_write != STRIDE_WRITE)
        glide_event("grSstWinOpen: video memory is short: the frame buffer's rows are %u bytes, not a Voodoo3's %u",
                    gs.stride_read, STRIDE_READ);
}

/* A window in two lines: its
   handle and class; its place and size, whether it is in front and
   topmost, and its title. The title only where reading it sends no message
   to another thread: a window of another process; or, with `own`, one of
   this thread (its window procedure runs, inside the call: the Glide
   window at the open alone). At Glide, or with `event` at every level. */
static void
printable(char *s)
{
    for (; *s; s++) {
        if (((unsigned char) *s < 0x20) || (*s == 0x7f))
            *s = '?';
        else if (*s == '"')
            *s = '\'';
    }
}

static void
window_lines(const char *lead, DWORD hwnd, int event, int own)
{
    char         cls[41], title[41];
    glide_rect_t r;
    DWORD        pid = 0, tid;
    DWORD        ex;
    const char  *front;

    if (event ? !diag_events : !diag_on)
        return; /* not logged at this level: nothing asked of the window */
    if (!hwnd) {
        if (event)
            glide_event("%s none", lead);
        else
            glide_dbg("%s none", lead);
        return;
    }
    cls[0] = title[0] = 0;
    if (GetClassNameA(hwnd, cls, (int) sizeof(cls)) <= 0)
        cls[0] = 0;
    cls[sizeof(cls) - 1] = 0;
    tid                  = GetWindowThreadProcessId(hwnd, &pid);
    if ((pid != GetCurrentProcessId()) || (own && (tid == GetCurrentThreadId()))) {
        if (GetWindowTextA(hwnd, title, (int) sizeof(title)) <= 0)
            title[0] = 0;
        title[sizeof(title) - 1] = 0;
    }
    printable(cls);
    printable(title);
    if (!GetWindowRect(hwnd, &r))
        r.left = r.top = r.right = r.bottom = 0;
    ex    = (DWORD) GetWindowLongA(hwnd, GWL_EXSTYLE);
    front = (GetForegroundWindow() == hwnd) ? "foreground" : "not foreground";
    if (event) {
        glide_event("%s %x, class \"%s\"", lead, hwnd, cls);
        glide_event("  at %d,%d %dx%d, %s, %s, \"%s\"", r.left, r.top, r.right - r.left, r.bottom - r.top, front,
                    (ex & WS_EX_TOPMOST) ? "topmost" : "not topmost", title);
    } else {
        glide_dbg("%s %x, class \"%s\"", lead, hwnd, cls);
        glide_dbg("  at %d,%d %dx%d, %s, %s, \"%s\"", r.left, r.top, r.right - r.left, r.bottom - r.top, front,
                  (ex & WS_EX_TOPMOST) ? "topmost" : "not topmost", title);
    }
}

/* The mode changed under the window: nothing more drawn, said once with
   the window in front then (not inside DllMain). */
static void
mode_lost(void)
{
    DWORD fg;

    gs.lost = 1;
    glide_event("the screen's mode changed under the Glide window: nothing more is drawn");
    if (detaching)
        return;
    fg = GetForegroundWindow();
    if (fg && (fg == gs.hwnd))
        glide_event("  the foreground window then: the Glide window");
    else
        window_lines("  the foreground window then:", fg, 1, 0);
}

/* The window's counts at its close (at Glide). */
static void
window_summary(void)
{
    glide_dbg("window closed after %u swaps: triangles %u asked, %u sent, %u culled", win_counters[GLC_SWAPS],
              win_counters[GLC_TRIANGLES_IN], win_counters[GLC_TRIANGLES], win_counters[GLC_CULLED]);
    glide_dbg("  points %u, lines %u; %u primitives dropped", win_counters[GLC_POINTS], win_counters[GLC_LINES],
              win_counters[GLC_DROPPED]);
    glide_dbg("  %u triangles wrapped apart, %u vertices wrapped", win_counters[GLC_APART], win_counters[GLC_WRAPPED]);
    glide_dbg("  TMU1's 1/w while active: %u its own, %u TMU0's, %u the vertex's", win_counters[GLC_T1W_OWN],
              win_counters[GLC_T1W_TMU0], win_counters[GLC_T1W_VERTEX]);
    glide_dbg("  TMU1's s and t while active: %u its own, %u TMU0's", win_counters[GLC_T1ST_OWN], win_counters[GLC_T1ST_TMU0]);
}

static void
screen_give_back(void)
{
    gs.staging = NULL;
    dd_release(&gs.surface);
    if (gs.dd) {
        if (gs.mode_set)
            ((dd_restore_t) VTBL(gs.dd, DD_RESTOREDISPLAYMODE))(gs.dd);
        if (gs.exclusive && gs.hwnd)
            ((dd_coop_t) VTBL(gs.dd, DD_SETCOOPERATIVELEVEL))(gs.dd, gs.hwnd, DDSCL_NORMAL);
        dd_release(&gs.dd);
    }
    gs.mode_set = gs.exclusive = 0;
}

/* OPEN, then every register: the session on the device as it starts. 0
   if the batch was dropped; 2 if the GPU restarted before it was done
   (it may be lost); 3 if the screen's mode changed before it went (the
   batch kept back: the device never had it); else 1. */
static int
session_send(FxU32 origin)
{
    DWORD *p = glc_cmd(VGPU98_GL_OPEN, VGPU98_GL_LEN_OPEN);

    if (!p)
        return 0;
    p[0]        = gs.w | (gs.h << 16);
    p[1]        = (DWORD) gs.n_color | (gs.aux ? VGPU98_GL_OPEN_AUX : 0) |
                  ((origin == GR_ORIGIN_LOWER_LEFT) ? VGPU98_GL_OPEN_LOWER_LEFT : 0);
    p[2]        = gs.staging_offset;
    p[3]        = gs.staging_bytes;
    gs.synced   = 0;
    gs.dirty_lo = 0;
    gs.dirty_hi = VGPU98_GS_COUNT;
    glc_sync();
    /* Gamma 1.3, as a Voodoo3's Glide loads it at the open. */
    glc_gamma(1.3f, 1.3f, 1.3f);
    glc_finish();
    if (gs.lost)
        return 3;
    if (dropped)
        return 0;
    gs.synced = 1;
    return (sb->state_epoch != batch_epoch) ? 2 : 1;
}

int
glc_open(FxU32 hwnd, FxU32 res, FxU32 refresh, FxU32 color_format, FxU32 origin, int color_buffers, int aux_buffers)
{
    int ok;

    if (!glide_card() || !sb)
        return 0;
    if (!(reg_read(VGPU98_REG_PROFILE) & VGPU98_PROFILE_GLIDE)) {
        static int told;

        /* Why, once; then each refusal as the DLL's first step had them. */
        if (!told++)
            glide_event("the card draws no Glide: no 3D on the host GPU, or an 86Box from before Glide drew");
        glide_refuse_window(hwnd, res, refresh, color_format, origin, (FxU32) color_buffers, (FxU32) aux_buffers);
        return 0;
    }
    if ((color_buffers < 1) || (color_buffers > 3) || (aux_buffers < 0) || (aux_buffers > 1) ||
        (color_format > GR_COLORFORMAT_BGRA) || (origin > GR_ORIGIN_LOWER_LEFT)) {
        glide_refuse_window(hwnd, res, refresh, color_format, origin, (FxU32) color_buffers, (FxU32) aux_buffers);
        return 0;
    }
    if (gs.open)
        glc_close();
    memset(&gs, 0, sizeof(gs));
    glc_lfb_reset();
    memset(win_counters, 0, sizeof(win_counters));
    glc_draw_open();
    /* As a Voodoo3's Glide: a resolution it does not know (the window's
       size, GR_RESOLUTION_NONE) is 640x480; two colour buffers come with a
       depth buffer. */
    if (res >= sizeof(resolutions) / sizeof(resolutions[0]))
        res = RES_640x480;
    if ((color_buffers == 2) && !aux_buffers)
        aux_buffers = 1;
    /* A Voodoo3's Glide refuses a window whose buffers leave its 16 MB less
       than 2 MB of textures and the command FIFO's 64 KB. */
    {
        const DWORD row = ((resolutions[res][0] + 63u) & ~63u) * 2;
        const DWORD buf = (row * resolutions[res][1] + 4095u) & ~4095u;

        if (buf * (DWORD) (color_buffers + aux_buffers) + (2u << 20) + (64u << 10) > (16u << 20)) {
            glide_dbg("grSstWinOpen: %ux%u with %u colour and %u aux buffers leaves a Voodoo3 no room for textures",
                      resolutions[res][0], resolutions[res][1], (DWORD) color_buffers, (DWORD) aux_buffers);
            glide_refuse_window(hwnd, res, refresh, color_format, origin, (FxU32) color_buffers, (FxU32) aux_buffers);
            return 0;
        }
    }
    gs.w            = resolutions[res][0];
    gs.h            = resolutions[res][1];
    gs.n_color      = color_buffers;
    gs.aux          = aux_buffers;
    gs.color_format = color_format;
    gs.pending_max  = 2;
    gpu_ring_choose(reg_read(VGPU98_REG_PROFILE));
    /* A refresh rate it does not know is 60 Hz. */
    screen_take(hwnd, refreshes[(refresh < sizeof(refreshes)) ? refresh : 0]);
    /* The mode is set: a later change of it ends the session. */
    gs.mode_serial = sb->mode_serial;
    opens          = (opens % 15) + 1;
    gs.id          = (GetCurrentProcessId() & 0x0fffffffu) | (opens << 28);
    gs.open        = 1;
    /* The registers as a Voodoo3's Glide leaves them at the open; delta0
       off, mipmap dithering not allowed. */
    glc_state_defaults(gs.regs, gs.n_color);
    glc_delta0(0, NULL);
    glc_mipmap_dither(0);
    glc_palette_6666_set(0);
    gs.regs[VGPU98_GS_CLIP_MAX] = gs.w | (gs.h << 16);
    gs.regs[VGPU98_GS_ORIGIN]   = origin;
    /* The device takes it; a GPU restart before it ran it loses it, so
       once more then. */
    ok = session_send(origin);
    if (ok == 2) {
        glide_dbg("grSstWinOpen: the GPU restarted as the window opened; its session goes again");
        ok = session_send(origin);
    }
    if (!ok || (ok == 3)) {
        glide_event(ok ? "grSstWinOpen refused: the screen's mode changed as the window opened"
                       : "grSstWinOpen refused: the GPU is held (a mode change or a DOS box)");
        gs.open = 0;
        gs.lost = 0;
        screen_give_back();
        return 0;
    }
    glide_dbg("window open: %ux%u, %u colour buffers%s, %s; mode %s, %sexclusive", gs.w, gs.h, (DWORD) gs.n_color,
              gs.aux ? " and a depth buffer" : "", (origin == GR_ORIGIN_LOWER_LEFT) ? "lower-left origin" : "upper-left origin",
              gs.mode_set ? "set" : "not set", gs.exclusive ? "" : "not ");
    window_lines("the window:", gs.hwnd, 0, 1);
    return 1;
}

void
glc_close(void)
{
    if (!gs.open)
        return;
    window_summary();
    /* A session lost with the mode is gone from the device already. The
       registers changed go before the CLOSE, not after it. */
    if (!gs.lost) {
        glc_sync();
        glc_cmd(VGPU98_GL_CLOSE, VGPU98_GL_LEN_CLOSE);
    }
    glc_finish();
    gs.open = 0;
    gs.lost = 0;
    glc_lfb_reset();
    screen_give_back();
}

/* The DLL's unload (DllMain). Unloaded by the program (FreeLibrary): the
   window closes whole, the desktop given back, as a Voodoo3's Glide does.
   At the process's end: the session closes on the device if the ring is
   free now (a thread killed holding its lock lets it go first), waiting
   for nothing inside the loader's lock, and DirectDraw, which gives the
   screen back itself as the process ends, is left alone. */
void
glc_detach(int process_ending)
{
    if (!gs.open)
        return;
    detaching = 1;
    if (!process_ending) {
        glc_close();
        detaching = 0;
        return;
    }
    window_summary();
    if (sb)
        gpu_release_process(GetCurrentProcessId());
    if (!gs.lost && sb && !sb->lock) {
        batch_n = 0; /* what the program left unsent goes with it */
        if (glc_cmd(VGPU98_GL_CLOSE, VGPU98_GL_LEN_CLOSE))
            flush(0);
    }
    gs.open    = 0;
    gs.staging = NULL;
    detaching  = 0;
}

int
glc_is_open(void)
{
    return gs.open && !gs.lost;
}

FxU32
glc_width(void)
{
    return gs.w;
}

FxU32
glc_height(void)
{
    return gs.h;
}

int
glc_color_buffers(void)
{
    return gs.n_color;
}

int
glc_aux_buffers(void)
{
    return gs.aux;
}

/* A buffer the window has (GR_BUFFER_*: the third with three colour
   buffers, the aux buffer with one). */
int
glc_has_buffer(FxI32 buffer)
{
    switch (buffer) {
        case GR_BUFFER_FRONTBUFFER:
        case GR_BUFFER_BACKBUFFER:
            return 1;
        case GR_BUFFER_TRIPLEBUFFER:
            return gs.n_color == 3;
        case GR_BUFFER_AUXBUFFER:
            return gs.aux != 0;
        default:
            return 0;
    }
}

void
glc_clear(FxU32 color, FxU32 alpha, FxU32 depth)
{
    DWORD *p;

    glc_sync();
    p = glc_cmd(VGPU98_GL_CLEAR, VGPU98_GL_LEN_CLEAR);
    if (!p)
        return;
    p[0] = glc_argb(color) & 0xffffff;
    p[1] = alpha & 0xff;
    p[2] = depth & 0xffff;
    if (glc_drawing_front())
        glc_front_written();
}

int
glc_pending(void)
{
    int n = 0, i;

    if (!regs || !sb)
        return 0;
    for (i = 0; i < gs.n_swaps; i++)
        n += !gpu_fence_done(gs.swap_fence[i]);
    return n;
}

void
glc_set_pending_max(int n)
{
    gs.pending_max = (n < 1) ? 1 : (n > 6) ? 6 : n;
}

void
glc_swap(FxU32 interval)
{
    DWORD *p;
    DWORD  fence;
    int    i;

    if (!gs.open || gs.lost)
        return;
    /* No more than pending_max swaps waiting: the oldest of
       them shown first. A GPU held meanwhile (a DOS box) ends no wait:
       the program sleeps between looks, for 5 seconds at most (a hold that
       lasts, Windows shutting down, or the mode gone, ends the pacing). */
    {
        const DWORD start = GetTickCount();

        while ((gs.n_swaps >= gs.pending_max) && !gpu_fence_done(gs.swap_fence[gs.n_swaps - gs.pending_max])) {
            if (glc_wait(gs.swap_fence[gs.n_swaps - gs.pending_max]))
                continue;
            if ((sb->mode_serial != gs.mode_serial) || (GetTickCount() - start >= 5000))
                break;
            Sleep(1);
        }
    }
    p = glc_cmd(VGPU98_GL_SWAP, VGPU98_GL_LEN_SWAP);
    if (!p)
        return;
    p[0]  = interval;
    fence = glc_flush();
    if (!fence)
        return; /* dropped: nothing to wait for */
    glc_count(GLC_SWAPS);
    if (gs.n_swaps == SWAPS_KEPT) {
        for (i = 1; i < SWAPS_KEPT; i++)
            gs.swap_fence[i - 1] = gs.swap_fence[i];
        gs.n_swaps--;
    }
    gs.swap_fence[gs.n_swaps++] = fence;
}

void
glc_gamma_table(const uint8_t *r, const uint8_t *g, const uint8_t *b)
{
    DWORD *p = glc_cmd(VGPU98_GL_GAMMA, VGPU98_GL_LEN_GAMMA);
    int    i;

    if (!p)
        return;
    for (i = 0; i < 256; i++)
        p[i] = (DWORD) r[i] | ((DWORD) g[i] << 8) | ((DWORD) b[i] << 16);
}

/* x to the power y, x in (0, 1], y > 0: through the FPU's log2 and 2^x. */
static double
power(double x, double y)
{
    double t, r;

    if (x <= 0.0)
        return 0.0;
    __asm__("fyl2x" : "=t"(t) : "0"(x), "u"(y) : "st(1)");
    /* 2^t = 2^int(t) * 2^frac(t), the fraction through f2xm1. */
    __asm__("fld %%st(0)\n\t"
            "frndint\n\t"
            "fxch\n\t"
            "fsub %%st(1), %%st\n\t"
            "f2xm1\n\t"
            "fld1\n\t"
            "faddp\n\t"
            "fscale\n\t"
            "fstp %%st(1)"
            : "=t"(r)
            : "0"(t));
    return r;
}

static void
curve(float gamma, uint8_t *out)
{
    int i;

    for (i = 0; i < 256; i++) {
        double v = (gamma > 0.0f) ? 255.0 * power((double) i / 255.0, 1.0 / gamma) + 0.5 : (double) i;

        out[i] = (v >= 255.0) ? 255 : (v <= 0.0) ? 0 : (uint8_t) v;
    }
    out[0] = 0;
}

void
glc_gamma(float r, float g, float b)
{
    uint8_t tr[256], tg[256], tb[256];

    curve(r, tr);
    curve(g, tg);
    curve(b, tb);
    glc_gamma_table(tr, tg, tb);
}

DWORD
glc_vretrace(void)
{
    return regs ? reg_read(VGPU98_REG_SCANLINE) : 0;
}

/* --- The staging surface ------------------------------------------------ */

uint8_t *
glc_staging(int for_read, DWORD *offset, DWORD *stride, DWORD *bytes)
{
    if (!gs.open || gs.lost || !gs.staging)
        return NULL;
    /* Gone with the mode: DirectDraw may have given its memory away. */
    if (sb->mode_serial != gs.mode_serial) {
        mode_lost();
        return NULL;
    }
    if (for_read) {
        *offset = gs.staging_offset + gs.stride_write * gs.h;
        *stride = gs.stride_read;
        *bytes  = gs.stride_read * gs.h;
        return gs.staging + gs.stride_write * gs.h;
    }
    *offset = gs.staging_offset;
    *stride = gs.stride_write;
    *bytes  = gs.stride_write * gs.h;
    return gs.staging;
}

void
glc_lfb_fence(DWORD fence)
{
    gs.lfb_fence = fence;
}

int
glc_lfb_settle(void)
{
    if (gs.lfb_fence && !gpu_fence_done(gs.lfb_fence))
        return glc_wait(gs.lfb_fence);
    return 1;
}

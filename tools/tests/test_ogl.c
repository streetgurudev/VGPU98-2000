/*
 * The OpenGL ICD on the host: opengl/'s sources,
 * compiled here, with stand-ins for what Windows 98 gives them: KERNEL32's
 * TLS, heap and clock, GDI's escape answering as the display driver would,
 * USER32's windows, DirectDraw's clippers, and a GPU that takes the ring's
 * packets at each doorbell. The shared block, BAR0 and video memory are
 * memory below 4 GB (test_glide_low.c), as the ICD holds their addresses in
 * 32 bits. Each scenario runs in its own process (the ICD finds the card
 * once a process).
 *
 *   test_ogl SCENARIO
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "icd.h"
#include "glcalls.h"
#include "state.h"

void        *test_low_alloc(size_t size);
BOOL STDCALL DllMain(DWORD dll_instance, DWORD reason, void *reserved);

/* The ICD's exports (opengl/main.c). */
typedef DWORD DHGLRC;
typedef void(STDCALL *ogl_set_table_t)(const ogl_proc_table_t *table);
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
void *STDCALL  DrvGetProcAddress(const char *name);
BOOL STDCALL   DrvDescribeLayerPlane(DWORD hdc, int pixel_format, int layer, DWORD bytes, void *lpd);

static int failures;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            fprintf(stderr, "test_ogl:%d: ", __LINE__);   \
            fprintf(stderr, __VA_ARGS__);                 \
            fputc('\n', stderr);                          \
            failures++;                                   \
        }                                                 \
    } while (0)

/* --- The machine ------------------------------------------------------------ */

#define TEST_VRAM_BYTES 0x1000000 /* 16 MB */
#define TEST_DC         0x4444u   /* the program's window's DC */
#define TEST_DC2        0x4445u   /* a second window's */
#define TEST_MEMDC      0x4446u   /* a memory DC: no window */
#define TEST_DISPLAY_DC 0x5555u   /* CreateDCA("DISPLAY") */
#define TEST_HWND       0x5150u
#define TEST_HWND2      0x5160u
#define TEST_DD         ((void *) &dd_obj)

typedef struct pkt {
    DWORD op, len;
    DWORD p[16384];
} pkt_t;

typedef struct test_rect {
    LONG l, t, r, b;
} test_rect_t;

static struct {
    int              escape;  /* the display driver answers VGPU98_ESC_INFO */
    vgpu98_shared_t *sb;
    volatile DWORD  *regs;
    uint8_t         *vram;
    int              dc_open;
    DWORD            thread;  /* GetCurrentThreadId's answer */
    DWORD            tick;
    int              sleeps;
    int              release_after; /* a hold ends at this many sleeps (0: never) */
    int              restart_at;    /* the GPU restarts as it takes the n-th packet (0: never) */
    int              taken;
    /* Windows */
    LONG             client_w[2], client_h[2]; /* each window's client area */
    LONG             origin_x[2], origin_y[2]; /* its top-left on the screen */
    int              alive[2];
    test_rect_t      clip[8];   /* the clip list the clippers give */
    int              n_clip;
    int              dd_objects, clippers;
    /* The display driver's cursor exclusion (cursor_far: the cursor is not
       near the window, so it is not kept out) */
    int                cursor_far;
    int                access_begins, access_ends;
    vgpu98_escaccess_t access_rect;
    /* The GPU */
    DWORD            texread_w, texread_value; /* TEXREAD's level width, and the texel it gives */
    int              texread_bytes; /* an index level's: bytes, texread_value + the column */
    pkt_t            pkts[512];
    int              n_pkts;
    int              resets;
    DWORD            last_error; /* SetLastError's */
    /* Diagnostics */
    char             stamp[16];   /* the display driver's build stamp in the escape's answer */
    int              escape_head; /* the escape answers its head alone (an older display driver) */
    ogl_filter_t     filter;      /* SetUnhandledExceptionFilter's last */
    int              filters_set;
    /* A slow GPU: a fence lands
       fence_hold_ms after its packet */
    DWORD            fence_hold_ms, fence_pending, fence_due;
    int              fence_waiting;
} m;

static void *tls_value[64];

DWORD STDCALL
test_TlsAlloc(void)
{
    return 3;
}

BOOL STDCALL
test_TlsFree(DWORD i)
{
    (void) i;
    return 1;
}

void *STDCALL
test_TlsGetValue(DWORD i)
{
    (void) i;
    return tls_value[m.thread & 63];
}

BOOL STDCALL
test_TlsSetValue(DWORD i, void *v)
{
    (void) i;
    tls_value[m.thread & 63] = v;
    return 1;
}

DWORD STDCALL
test_GetProcessHeap(void)
{
    return 1;
}

/* Each block remembers its size, so a growth can zero what it adds. */
void *STDCALL
test_HeapAlloc(DWORD heap, DWORD flags, DWORD bytes)
{
    size_t *p = calloc(1, bytes + sizeof(size_t) * 2);

    (void) heap;
    (void) flags;
    if (!p)
        return NULL;
    p[0] = bytes;
    return p + 2;
}

void *STDCALL
test_HeapReAlloc(DWORD heap, DWORD flags, void *q, DWORD bytes)
{
    size_t *p   = (size_t *) q - 2;
    size_t  old = p[0];

    (void) heap;
    (void) flags;
    p = realloc(p, bytes + sizeof(size_t) * 2);
    if (!p)
        return NULL;
    if (bytes > old)
        memset((uint8_t *) (p + 2) + old, 0, bytes - old);
    p[0] = bytes;
    return p + 2;
}

BOOL STDCALL
test_HeapFree(DWORD heap, DWORD flags, void *q)
{
    (void) heap;
    (void) flags;
    free((size_t *) q - 2);
    return 1;
}

void STDCALL
test_Sleep(DWORD ms)
{
    (void) ms;
    m.sleeps++;
    m.tick += 5;
    if (m.release_after && (m.sleeps >= m.release_after))
        m.regs[VGPU98_REG_STATUS / 4] &= ~(DWORD) VGPU98_STATUS_HELD;
    if (m.fence_waiting && ((LONG) (m.tick - m.fence_due) >= 0)) {
        m.regs[VGPU98_REG_FENCE / 4] = m.fence_pending;
        m.fence_waiting              = 0;
    }
}

void STDCALL
test_SetLastError(DWORD error)
{
    m.last_error = error;
}

/* --- Diagnostics: the ICD's lines and the files - */

#define TEST_INSTANCE 0x11800000u /* DllMain's, as machine() gives it */
#define TEST_PROGRAM  "C:\\GAMES\\QUAKE2\\QUAKE2.EXE"
#define TEST_DLL      "C:\\WINDOWS\\SYSTEM\\VGPU98GL.DLL"

/* The lines written, as the host gets them: characters through BAR0's
   DEBUG register (driver32/logline.c's LOG_PUT_HOOK), a line to each
   newline. */
static char lines[512][200];
static int  cut[512]; /* the writer cut the line at 159 characters */
static int  n_lines;
static int  line_at;

void test_log_put(volatile uint32_t *regs, char c);

void
test_log_put(volatile uint32_t *regs, char c)
{
    CHECK(regs == m.regs, "a line written to another register window");
    /* Under the block's lock while it is this build's, else the DLL's own */
    CHECK((m.sb->version == SB_VERSION) ? (m.sb->log_lock == 1) : (m.sb->log_lock == 0), "a line under the wrong lock");
    if (n_lines >= (int) (sizeof(lines) / sizeof(lines[0])))
        return;
    if (c == 10) {
        cut[n_lines]              = line_at >= 159;
        lines[n_lines++][line_at] = 0;
        line_at                   = 0;
    } else if (line_at < (int) sizeof(lines[0]) - 1)
        lines[n_lines][line_at++] = c;
}

DWORD STDCALL
test_GetModuleFileNameA(DWORD module, char *name, DWORD size)
{
    const char *path = (module == 0) ? TEST_PROGRAM : (module == TEST_INSTANCE) ? TEST_DLL : "";
    DWORD       n    = (DWORD) strlen(path);

    if (!size)
        return 0;
    if (n >= size)
        n = size - 1;
    memcpy(name, path, n);
    name[n] = 0;
    return n;
}

DWORD STDCALL
test_GetSystemDirectoryA(char *name, DWORD size)
{
    static const char sys[] = "C:\\WINDOWS\\SYSTEM";

    if (size < sizeof(sys))
        return sizeof(sys);
    memcpy(name, sys, sizeof(sys));
    return sizeof(sys) - 1;
}

ogl_filter_t STDCALL
test_SetUnhandledExceptionFilter(ogl_filter_t filter)
{
    ogl_filter_t prev = m.filter;

    m.filter = filter;
    m.filters_set++;
    return prev;
}

BOOL STDCALL
test_IsBadReadPtr(const void *p, DWORD bytes)
{
    (void) p;
    (void) bytes;
    return 0;
}

DWORD STDCALL
test_GetTickCount(void)
{
    return m.tick;
}

DWORD STDCALL
test_GetCurrentProcessId(void)
{
    return 0x0a5c3e11u;
}

DWORD STDCALL
test_GetCurrentThreadId(void)
{
    return m.thread;
}

DWORD STDCALL
test_CreateDCA(const char *driver, const char *device, const char *output, const void *devmode)
{
    CHECK(!strcmp(driver, "DISPLAY") && !device && !output && !devmode, "CreateDCA not for the display");
    m.dc_open++;
    return TEST_DISPLAY_DC;
}

BOOL STDCALL
test_DeleteDC(DWORD dc)
{
    CHECK(dc == TEST_DISPLAY_DC, "DeleteDC of another DC");
    m.dc_open--;
    return 1;
}

/* As the display driver answers (driver/ddraw.asm, Control). */
int STDCALL
test_ExtEscape(DWORD dc, int escape, int in_bytes, const char *in, int out_bytes, char *out)
{
    CHECK(dc == TEST_DISPLAY_DC, "ExtEscape on another DC");
    if (escape == QUERYESCSUPPORT)
        return (m.escape && (in_bytes >= 2) && (*(const uint16_t *) in == VGPU98_ESC_INFO)) ? 1 : 0;
    if ((escape == VGPU98_ESC_ACCESS) && m.escape) {
        const vgpu98_escaccess_t *a = (const vgpu98_escaccess_t *) in;

        CHECK(in_bytes == (int) sizeof(*a), "VGPU98_ESC_ACCESS's input");
        if (a->begin && m.cursor_far)
            return 0; /* the cursor not near the window: not kept out */
        if (a->begin) {
            CHECK(m.access_begins == m.access_ends, "the cursor kept out twice");
            m.access_begins++;
            m.access_rect = *a;
        } else
            m.access_ends++;
        return 1;
    }
    if ((escape == VGPU98_ESC_INFO) && m.escape) {
        const vgpu98_escquery_t *q = (const vgpu98_escquery_t *) in;
        vgpu98_escinfo_t        *a = (vgpu98_escinfo_t *) out;

        CHECK((in_bytes == (int) sizeof(*q)) && (out_bytes == (int) sizeof(*a)), "VGPU98_ESC_INFO's buffers");
        a->magic         = ESC_INFO_MAGIC;
        a->size          = m.escape_head ? ESC_INFO_HEAD : sizeof(*a);
        a->shared_linear = (DWORD) (uintptr_t) m.sb;
        a->sb_version    = SB_VERSION;
        memset(a->stamp, 0, sizeof(a->stamp));
        if (!m.escape_head)
            memcpy(a->stamp, m.stamp, sizeof(a->stamp));
        return 1;
    }
    return 0;
}

static int
window_index(DWORD hwnd)
{
    return (hwnd == TEST_HWND) ? 0 : (hwnd == TEST_HWND2) ? 1 : -1;
}

DWORD STDCALL
test_WindowFromDC(DWORD dc)
{
    return (dc == TEST_DC) ? TEST_HWND : (dc == TEST_DC2) ? TEST_HWND2 : 0;
}

BOOL STDCALL
test_GetClientRect(DWORD hwnd, ogl_rect_t *r)
{
    const int i = window_index(hwnd);

    if (i < 0)
        return 0;
    r->left   = 0;
    r->top    = 0;
    r->right  = m.client_w[i];
    r->bottom = m.client_h[i];
    return 1;
}

BOOL STDCALL
test_ClientToScreen(DWORD hwnd, ogl_point_t *pt)
{
    const int i = window_index(hwnd);

    if (i < 0)
        return 0;
    pt->x += m.origin_x[i];
    pt->y += m.origin_y[i];
    return 1;
}

BOOL STDCALL
test_IsWindow(DWORD hwnd)
{
    const int i = window_index(hwnd);

    return (i >= 0) && m.alive[i];
}

/* --- DirectDraw: an object that makes clippers, which give m.clip -------------- */

static void *dd_vtbl[24];
static void *ddc_vtbl[12];
static struct {
    void **vtbl;
} dd_obj = { dd_vtbl };
typedef struct test_clipper {
    void **vtbl;
    DWORD  hwnd;
} test_clipper_t;
static test_clipper_t clip_obj[8];

static DWORD STDCALL
dd_release(void *self)
{
    if (self == TEST_DD)
        m.dd_objects--;
    else
        m.clippers--;
    return 0;
}

static HRESULT STDCALL
dd_coop(void *self, DWORD hwnd, DWORD flags)
{
    (void) self;
    (void) hwnd;
    CHECK(flags == 8, "a cooperative level other than DDSCL_NORMAL: %x", flags);
    return DD_OK;
}

static HRESULT STDCALL
dd_create_clipper(void *self, DWORD flags, void **out, void *outer)
{
    (void) self;
    (void) flags;
    (void) outer;
    CHECK(m.clippers < 8, "too many clippers");
    clip_obj[m.clippers].vtbl = ddc_vtbl;
    *out                      = &clip_obj[m.clippers++];
    return DD_OK;
}

static HRESULT STDCALL
ddc_set_hwnd(void *self, DWORD flags, DWORD hwnd)
{
    (void) flags;
    ((test_clipper_t *) self)->hwnd = hwnd;
    return DD_OK;
}

static HRESULT STDCALL
ddc_clip_list(void *self, const ogl_rect_t *rect, void *rgn, DWORD *size)
{
    const DWORD need = 32 + (DWORD) m.n_clip * 16;
    DWORD      *h    = rgn;
    int         i;

    (void) self;
    CHECK(!rect, "a clip list for a rectangle");
    if (!rgn) {
        *size = need;
        return DD_OK;
    }
    if (*size < need)
        return (HRESULT) 0x8876023A; /* DDERR_REGIONTOOSMALL */
    h[0] = 32;
    h[1] = 1;
    h[2] = (DWORD) m.n_clip;
    h[3] = (DWORD) m.n_clip * 16;
    for (i = 0; i < m.n_clip; i++) {
        LONG *r = (LONG *) ((uint8_t *) rgn + 32 + i * 16);

        r[0] = m.clip[i].l;
        r[1] = m.clip[i].t;
        r[2] = m.clip[i].r;
        r[3] = m.clip[i].b;
    }
    return DD_OK;
}

/* The staging surface: an offscreen surface at TEST_STAGING_AT in video
   memory, rows of the ICD's stride. */
#define TEST_STAGING_AT 0x400000u
static void *dds_vtbl[40];
static struct {
    void **vtbl;
} dds_obj = { dds_vtbl };
static int surfaces;

static HRESULT STDCALL
dd_create_surface(void *self, DDSURFACEDESC *desc, void **out, void *outer)
{
    (void) self;
    (void) outer;
    CHECK((desc->ddsCaps.dwCaps & DDSCAPS_OFFSCREENPLAIN) && (desc->dwWidth * 4 == OGL_STAGING_STRIDE) &&
              (desc->dwHeight == OGL_STAGING_ROWS),
          "the staging surface asked for: %u by %u", desc->dwWidth, desc->dwHeight);
    surfaces++;
    *out = &dds_obj;
    return DD_OK;
}

static HRESULT STDCALL
dds_lock(void *self, void *rect, DDSURFACEDESC *desc, DWORD flags, DWORD event)
{
    (void) self;
    (void) rect;
    (void) flags;
    (void) event;
    desc->lpSurface = (DWORD) (uintptr_t) (m.vram + TEST_STAGING_AT);
    desc->lPitch    = OGL_STAGING_STRIDE;
    return DD_OK;
}

static HRESULT STDCALL
dds_unlock(void *self, void *ptr)
{
    (void) self;
    (void) ptr;
    return DD_OK;
}

HRESULT STDCALL
test_DirectDrawCreate(void *guid, void **out, void *outer)
{
    (void) guid;
    (void) outer;
    dd_vtbl[2]   = (void *) dd_release;
    dd_vtbl[4]   = (void *) dd_create_clipper;
    dd_vtbl[6]   = (void *) dd_create_surface;
    dds_vtbl[25] = (void *) dds_lock;
    dds_vtbl[32] = (void *) dds_unlock;
    dd_vtbl[20]  = (void *) dd_coop;
    ddc_vtbl[2]  = (void *) dd_release;
    ddc_vtbl[3]  = (void *) ddc_clip_list;
    ddc_vtbl[8]  = (void *) ddc_set_hwnd;
    *out         = TEST_DD;
    m.dd_objects++;
    return DD_OK;
}

/* --- The GPU: the ring's packets, taken at each doorbell -------------------------- */

void test_reg_write(DWORD off, DWORD v);

static DWORD
ring_word(DWORD at)
{
    DWORD v;

    memcpy(&v, m.vram + m.regs[VGPU98_REG_RING_BASE / 4] + at, 4);
    return v;
}

void
test_reg_write(DWORD off, DWORD v)
{
    m.regs[off / 4] = v;
    if (off == VGPU98_REG_CONTROL) {
        if (v & VGPU98_CTRL_RESET) {
            m.regs[VGPU98_REG_RING_HEAD / 4] = 0;
            m.regs[VGPU98_REG_RING_TAIL / 4] = 0;
            m.regs[VGPU98_REG_STATUS / 4] &= ~(DWORD) (VGPU98_STATUS_RUNNING | VGPU98_STATUS_ERROR);
            m.resets++;
        }
        if (v & VGPU98_CTRL_RUN)
            m.regs[VGPU98_REG_STATUS / 4] |= VGPU98_STATUS_RUNNING;
        else if (!(v & VGPU98_CTRL_RESET))
            m.regs[VGPU98_REG_STATUS / 4] &= ~(DWORD) VGPU98_STATUS_RUNNING;
        return;
    }
    if (off != VGPU98_REG_RING_TAIL)
        return;
    {
        const DWORD size = m.regs[VGPU98_REG_RING_SIZE / 4];
        DWORD       at   = m.regs[VGPU98_REG_RING_HEAD / 4];

        CHECK(m.regs[VGPU98_REG_STATUS / 4] & VGPU98_STATUS_RUNNING, "a doorbell with the GPU stopped");
        while (size && (at != v)) {
            const DWORD hdr = ring_word(at);
            const DWORD op  = VGPU98_PKT_OP(hdr);
            const DWORD len = VGPU98_PKT_LEN(hdr);
            DWORD       k;

            CHECK(at + 4 + len * 4 <= size, "a packet past the ring's end");
            if (op != VGPU98_OP_NOP) {
                m.taken++;
                if (m.n_pkts < (int) (sizeof(m.pkts) / sizeof(m.pkts[0]))) {
                    pkt_t *p = &m.pkts[m.n_pkts++];

                    p->op  = op;
                    p->len = len;
                    for (k = 0; (k < len) && (k < 16384); k++)
                        p->p[k] = ring_word(at + 4 + k * 4);
                }
                if ((op == VGPU98_OP_FENCE) && m.fence_hold_ms) {
                    /* A slow GPU: the fence lands that much later (Sleep) */
                    m.fence_pending = ring_word(at + 4);
                    m.fence_due     = m.tick + m.fence_hold_ms;
                    m.fence_waiting = 1;
                } else if (op == VGPU98_OP_FENCE)
                    m.regs[VGPU98_REG_FENCE / 4] = ring_word(at + 4);
                /* Read-backs: a pattern into video memory, as the device
                   would write the buffer's pixels or the level's texels */
                if ((op == VGPU98_OGL_READ) || (op == VGPU98_OGL_TEXREAD)) {
                    const int   tex    = op == VGPU98_OGL_TEXREAD;
                    const DWORD what   = ring_word(at + 8);
                    const DWORD offset = ring_word(at + 4 + (tex ? 3 : 4) * 4);
                    const DWORD stride = ring_word(at + 4 + (tex ? 4 : 5) * 4);
                    const DWORD wh     = ring_word(at + 4 + (tex ? 5 : 3) * 4);
                    const DWORD w = tex ? m.texread_w : (wh & 0xffff), h = wh >> 16;
                    DWORD       i, j;

                    for (j = 0; j < h; j++) {
                        uint8_t *row = m.vram + offset + j * stride;

                        for (i = 0; i < w; i++) {
                            if (tex && m.texread_bytes)
                                row[i] = (uint8_t) (m.texread_value + i);
                            else if (tex)
                                memcpy(row + 4 * i, &m.texread_value, 4);
                            else if (what == VGPU98_OGL_READ_DEPTH) {
                                const DWORD v = 0x8000u + i;

                                memcpy(row + 4 * i, &v, 4);
                            } else if (what == VGPU98_OGL_READ_STENCIL)
                                row[i] = (uint8_t) (i + 2 * j);
                            else if (m.sb->mode_bpp == 16) {
                                /* 565: red the column, green the row */
                                const uint16_t v = (uint16_t) (((i & 31u) << 11) | ((j & 63u) << 5) | 31u);

                                memcpy(row + 2 * i, &v, 2);
                            } else {
                                row[4 * i]     = 0xff;            /* blue */
                                row[4 * i + 1] = (uint8_t) j;     /* green */
                                row[4 * i + 2] = (uint8_t) i;     /* red */
                                row[4 * i + 3] = 0x80;
                            }
                        }
                    }
                }
                if (m.restart_at && (m.taken == m.restart_at)) {
                    m.restart_at = 0;
                    m.sb->state_epoch++;
                }
            }
            at = (at + 4 + len * 4) % size;
        }
        m.regs[VGPU98_REG_RING_HEAD / 4] = v;
    }
}

static int
count_pkts(DWORD op)
{
    int i, n = 0;

    for (i = 0; i < m.n_pkts; i++)
        n += m.pkts[i].op == op;
    return n;
}

static const pkt_t *
nth_pkt(DWORD op, int n)
{
    int i;

    for (i = 0; i < m.n_pkts; i++) {
        if ((m.pkts[i].op == op) && (n-- == 0))
            return &m.pkts[i];
    }
    return NULL;
}

static const pkt_t *
last_pkt(DWORD op)
{
    int i;

    for (i = m.n_pkts - 1; i >= 0; i--) {
        if (m.pkts[i].op == op)
            return &m.pkts[i];
    }
    return NULL;
}

/* The STATE packets' last value of register reg, or `none`. */
static DWORD
reg_sent(DWORD reg, DWORD none)
{
    DWORD v = none;
    int   i;

    for (i = 0; i < m.n_pkts; i++) {
        const pkt_t *q = &m.pkts[i];

        if ((q->op == VGPU98_OGL_STATE) && (q->p[1] <= reg) && (reg < q->p[1] + q->len - 2) && (2 + reg - q->p[1] < 16384))
            v = q->p[2 + reg - q->p[1]];
    }
    return v;
}

static float
fl(DWORD v)
{
    float f;

    memcpy(&f, &v, 4);
    return f;
}

static int
near_f(float a, float b)
{
    return (a - b < 0.0001f) && (b - a < 0.0001f);
}

/* --- Scenarios --------------------------------------------------------------------- */

static void
machine(DWORD bpp)
{
    uint8_t *low = test_low_alloc(0x100000 + TEST_VRAM_BYTES);

    if (!low) {
        fprintf(stderr, "test_ogl: no memory below 4 GB\n");
        exit(1);
    }
    memset(&m, 0, sizeof(m));
    m.escape            = 1;
    m.thread            = 1;
    m.tick              = 1000;
    m.sb                = (vgpu98_shared_t *) low;
    m.regs              = (volatile DWORD *) (low + 0x10000);
    m.vram              = low + 0x100000;
    m.sb->magic         = SB_MAGIC;
    m.sb->version       = SB_VERSION;
    m.sb->size          = sizeof(vgpu98_shared_t);
    m.sb->regs_linear   = (DWORD) (uintptr_t) m.regs;
    m.sb->fb_linear     = (DWORD) (uintptr_t) m.vram;
    m.sb->vram_size     = TEST_VRAM_BYTES;
    m.sb->reserve_base  = TEST_VRAM_BYTES - VGPU98_GPU_RESERVE;
    m.sb->mode_serial   = 1;
    m.sb->mode_width    = 1024;
    m.sb->mode_height   = 768;
    m.sb->mode_bpp      = bpp;
    m.sb->mode_pitch    = 1024 * (bpp / 8);
    m.regs[VGPU98_REG_ID / 4]      = VGPU98_ID_MAGIC;
    m.regs[VGPU98_REG_PROFILE / 4] = VGPU98_PROFILE & ~(DWORD) (VGPU98_PROFILE_SYSRING | VGPU98_PROFILE_STATUSWB);
    m.client_w[0] = 640;
    m.client_h[0] = 480;
    m.origin_x[0] = 100;
    m.origin_y[0] = 50;
    m.alive[0]    = 1;
    m.client_w[1] = 320;
    m.client_h[1] = 200;
    m.alive[1]    = 1;
    strncpy(m.stamp, VGPU98_BUILD_STAMP, sizeof(m.stamp)); /* the display driver of this build */
    n_lines = line_at = 0;
    /* The window wholly visible, unless a scenario covers it. */
    m.clip[0].l = 100;
    m.clip[0].t = 50;
    m.clip[0].r = 740;
    m.clip[0].b = 530;
    m.n_clip    = 1;
    DllMain(0x11800000u, 1, NULL);
}

/* A window with the first pixel format, a context made current on it. */
static DHGLRC
current_context(DWORD dc)
{
    DHGLRC rc;

    CHECK(DrvSetPixelFormat(dc, 1), "DrvSetPixelFormat refused");
    rc = DrvCreateContext(dc);
    CHECK(rc != 0, "DrvCreateContext refused");
    CHECK(DrvSetContext(dc, rc, NULL) == &ogl_table, "DrvSetContext refused");
    return rc;
}

static void
expect_pfd(int n, int color_bits, int r, int g, int b, int a, int depth, int stencil, int dbl)
{
    ogl_pfd_t pfd;

    memset(&pfd, 0xcc, sizeof(pfd));
    CHECK(DrvDescribePixelFormat(TEST_DC, n, sizeof(pfd), &pfd) > 0, "format %d not described", n);
    CHECK((pfd.nSize == 40) && (pfd.nVersion == 1), "format %d: size %u, version %u", n, pfd.nSize, pfd.nVersion);
    CHECK(pfd.dwFlags == (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | (dbl ? (PFD_DOUBLEBUFFER | PFD_SWAP_COPY) : 0)),
          "format %d: flags %x", n, pfd.dwFlags);
    CHECK((pfd.iPixelType == PFD_TYPE_RGBA) && (pfd.cColorBits == color_bits) && (pfd.cRedBits == r) &&
              (pfd.cGreenBits == g) && (pfd.cBlueBits == b) && (pfd.cAlphaBits == a),
          "format %d: colour %u (%u %u %u %u)", n, pfd.cColorBits, pfd.cRedBits, pfd.cGreenBits, pfd.cBlueBits, pfd.cAlphaBits);
    CHECK((pfd.cDepthBits == depth) && (pfd.cStencilBits == stencil) && !pfd.cAccumBits && !pfd.cAuxBuffers,
          "format %d: depth %u stencil %u accum %u aux %u", n, pfd.cDepthBits, pfd.cStencilBits, pfd.cAccumBits,
          pfd.cAuxBuffers);
    CHECK((r != 5) || ((pfd.cRedShift == 11) && (pfd.cGreenShift == 5) && !pfd.cBlueShift), "format %d: 565's shifts", n);
    CHECK((r != 8) || ((pfd.cRedShift == 16) && (pfd.cGreenShift == 8) && !pfd.cBlueShift && (pfd.cAlphaShift == (a ? 24 : 0))),
          "format %d: 8888's shifts", n);
}

/* --- GL's state --------------------------------------------------------------------- */

static GLint
geti(GLenum pname)
{
    GLint v[16] = { 0 };

    ogl_GetIntegerv(pname, v);
    return v[0];
}

static float
getf(GLenum pname, int i)
{
    GLfloat v[16] = { 0 };

    ogl_GetFloatv(pname, v);
    return v[i];
}

static GLboolean
getb(GLenum pname)
{
    GLboolean v[16] = { 0 };

    ogl_GetBooleanv(pname, v);
    return v[0];
}

static GLenum
error_now(void)
{
    return ogl_GetError();
}

/* Window coordinates of the viewport 640x480 at 0,0, depth range 0-1. */
#define WX(x) (320.0f + (x) * 320.0f)
#define WY(y) (240.0f + (y) * 240.0f)

/* A second thread makes its own context current: the first's stays as it
   was (audit 1, item 2); a thread that ends lets its context go (item 4). */
static void
scenario_threads(void)
{
    DHGLRC a, b;
    DWORD  a_id;

    machine(16);
    a = current_context(TEST_DC);
    ogl_Flush();
    a_id   = nth_pkt(VGPU98_OGL_CONTEXT, 0)->p[0];
    m.thread = 2;
    CHECK(DrvSetPixelFormat(TEST_DC2, 1), "the second window's format");
    b = DrvCreateContext(TEST_DC2);
    CHECK(b && (DrvSetContext(TEST_DC2, b, NULL) == &ogl_table), "the second thread's context");
    CHECK(!DrvSetContext(TEST_DC, a, NULL), "the first thread's context made current on the second");
    m.thread = 1;
    CHECK(ogl_thread_current() && (ogl_thread_current()->id == a_id), "the first thread lost its context");
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    CHECK(last_pkt(VGPU98_OGL_CLEAR) && (last_pkt(VGPU98_OGL_CLEAR)->p[0] == a_id), "the first thread's clear");
    CHECK(DrvSwapBuffers(TEST_DC), "the first thread's swap");
    /* The second thread ends with its context current. */
    m.thread = 2;
    DllMain(0x11800000u, 3, NULL);
    CHECK(!ogl_thread_current(), "the ended thread's context is still current");
    m.thread = 3;
    CHECK(DrvSetContext(TEST_DC2, b, NULL) == &ogl_table, "the ended thread's context, on another thread");
    CHECK(DrvReleaseContext(b) && DrvDeleteContext(b), "the ended thread's context deleted");
    m.thread = 1;
    CHECK(DrvReleaseContext(a) && DrvDeleteContext(a), "the first context deleted");
}

/* A new window with a gone window's HWND sets another format (audit 1,
   item 3); a window being drawn into keeps its own. */
static void
scenario_rehwnd(void)
{
    DHGLRC rc;

    machine(16);
    rc = current_context(TEST_DC);
    CHECK(!DrvSetPixelFormat(TEST_DC, 2), "another format for a window being drawn into");
    CHECK(DrvReleaseContext(rc) && DrvDeleteContext(rc), "the dummy window's context deleted");
    CHECK(DrvSetPixelFormat(TEST_DC, 2), "the HWND's new window refused format 2");
    rc = DrvCreateContext(TEST_DC);
    CHECK(rc && (DrvSetContext(TEST_DC, rc, NULL) == &ogl_table), "a context on the new window");
    ogl_Flush();
    CHECK(last_pkt(VGPU98_OGL_DRAWABLE) &&
              (last_pkt(VGPU98_OGL_DRAWABLE)->p[3] ==
               (VGPU98_OGL_COLOR_565 | (VGPU98_OGL_DEPTH_24S8 << VGPU98_OGL_DEPTH_SHIFT) | VGPU98_OGL_DOUBLE)),
          "the new window's drawable is not format 2's");
}

/* Every query's initial value and its setter's (GL 1.1, section 6.2). */
static void
scenario_queries(void)
{
    GLint   iv[16];
    GLfloat fv[16];
    const GLfloat pos[4]  = { 1.0f, 2.0f, 3.0f, 1.0f };
    const GLfloat diff[4] = { 0.25f, 0.5f, 0.75f, 1.0f };

    machine(16);
    current_context(TEST_DC);
    /* Initial values */
    CHECK(geti(GL_MATRIX_MODE) == GL_MODELVIEW, "GL_MATRIX_MODE");
    ogl_GetIntegerv(GL_VIEWPORT, iv);
    CHECK((iv[0] == 0) && (iv[1] == 0) && (iv[2] == 640) && (iv[3] == 480), "GL_VIEWPORT %d %d %d %d", iv[0], iv[1], iv[2], iv[3]);
    CHECK(geti(GL_MAX_TEXTURE_SIZE) == 2048 && geti(GL_MAX_LIGHTS) == 8 && geti(GL_MAX_CLIP_PLANES) == 6, "the limits");
    CHECK(geti(GL_MAX_MODELVIEW_STACK_DEPTH) == 32 && geti(GL_MAX_PROJECTION_STACK_DEPTH) == 2 &&
              geti(GL_MAX_ATTRIB_STACK_DEPTH) == 16 && geti(GL_MAX_NAME_STACK_DEPTH) == 64 && geti(GL_MAX_EVAL_ORDER) == 30,
          "the stack limits");
    CHECK(geti(GL_RED_BITS) == 5 && geti(GL_GREEN_BITS) == 6 && geti(GL_BLUE_BITS) == 5 && geti(GL_ALPHA_BITS) == 0 &&
              geti(GL_DEPTH_BITS) == 16 && geti(GL_STENCIL_BITS) == 0 && geti(GL_ACCUM_RED_BITS) == 0,
          "the format's bits");
    CHECK(getb(GL_DOUBLEBUFFER) && getb(GL_RGBA_MODE) && !getb(GL_INDEX_MODE) && !getb(GL_STEREO), "the format's modes");
    CHECK(getb(GL_DITHER) && !getb(GL_DEPTH_TEST) && !getb(GL_LIGHTING), "the initial enables");
    CHECK(geti(GL_DEPTH_FUNC) == GL_LESS && getb(GL_DEPTH_WRITEMASK), "the depth state");
    CHECK(geti(GL_STENCIL_WRITEMASK) == -1 && geti(GL_STENCIL_VALUE_MASK) == -1, "the stencil masks: all ones");
    CHECK(geti(GL_UNPACK_ALIGNMENT) == 4 && geti(GL_PACK_ALIGNMENT) == 4, "the alignments");
    CHECK(geti(GL_SHADE_MODEL) == GL_SMOOTH && geti(GL_FRONT_FACE) == GL_CCW && geti(GL_CULL_FACE_MODE) == GL_BACK,
          "the polygon state");
    CHECK(near_f(getf(GL_CURRENT_COLOR, 0), 1.0f) && near_f(getf(GL_CURRENT_COLOR, 3), 1.0f), "GL_CURRENT_COLOR");
    CHECK(near_f(getf(GL_CURRENT_NORMAL, 2), 1.0f) && near_f(getf(GL_CURRENT_TEXTURE_COORDS, 3), 1.0f), "the current normal and coordinates");
    CHECK(geti(GL_CURRENT_NORMAL) == 0, "GL_CURRENT_NORMAL's x as an integer");
    ogl_GetIntegerv(GL_CURRENT_NORMAL, iv);
    CHECK(iv[2] == 0x7fffffff, "GL_CURRENT_NORMAL's z mapped: %x", iv[2]);
    CHECK(near_f(getf(GL_POINT_SIZE_RANGE, 1), 64.0f) && near_f(getf(GL_LINE_WIDTH_GRANULARITY, 0), 0.125f), "the point and line ranges");
    ogl_GetFloatv(GL_MODELVIEW_MATRIX, fv);
    CHECK(near_f(fv[0], 1.0f) && near_f(fv[5], 1.0f) && near_f(fv[1], 0.0f) && near_f(fv[15], 1.0f), "the identity");
    CHECK(geti(GL_TEXTURE_BINDING_2D) == 0 && geti(GL_RENDER_MODE) == GL_RENDER && geti(GL_LIST_MODE) == 0, "bindings and modes");
    CHECK(!strcmp((const char *) ogl_GetString(GL_VENDOR), "VGPU98") &&
              !strcmp((const char *) ogl_GetString(GL_RENDERER), "VGPU98 2000/PCI") &&
              !strcmp((const char *) ogl_GetString(GL_VERSION), "1.1.0") && !strcmp((const char *) ogl_GetString(GL_EXTENSIONS), ""),
          "the strings");
    CHECK(!ogl_GetString(GL_RGBA) && error_now() == GL_INVALID_ENUM, "glGetString of another name");
    /* Setters, then their queries */
    ogl_ClearColor(1.0f, 0.0f, -1.0f, 0.5f);
    ogl_GetIntegerv(GL_COLOR_CLEAR_VALUE, iv);
    CHECK(iv[0] == 0x7fffffff && iv[1] == 0 && iv[2] == 0, "GL_COLOR_CLEAR_VALUE mapped, clamped: %x %x %x", iv[0], iv[1], iv[2]);
    CHECK(near_f(getf(GL_COLOR_CLEAR_VALUE, 3), 0.5f), "GL_COLOR_CLEAR_VALUE's alpha");
    ogl_LineWidth(2.25f);
    CHECK(near_f(getf(GL_LINE_WIDTH, 0), 2.25f) && geti(GL_LINE_WIDTH) == 2, "GL_LINE_WIDTH");
    ogl_Enable(GL_FOG);
    CHECK(getb(GL_FOG) && ogl_IsEnabled(GL_FOG), "GL_FOG");
    ogl_Fogf(GL_FOG_DENSITY, 0.5f);
    ogl_Fogi(GL_FOG_MODE, GL_LINEAR);
    CHECK(near_f(getf(GL_FOG_DENSITY, 0), 0.5f) && geti(GL_FOG_MODE) == GL_LINEAR, "the fog");
    ogl_DepthFunc(GL_ALWAYS);
    ogl_DepthRange(0.25, 0.75);
    CHECK(geti(GL_DEPTH_FUNC) == GL_ALWAYS && near_f(getf(GL_DEPTH_RANGE, 1), 0.75f), "GL_DEPTH_FUNC, GL_DEPTH_RANGE");
    ogl_StencilFunc(GL_EQUAL, 3, 0xf0);
    ogl_StencilOp(GL_KEEP, GL_INCR, GL_INVERT);
    CHECK(geti(GL_STENCIL_FUNC) == GL_EQUAL && geti(GL_STENCIL_REF) == 3 && geti(GL_STENCIL_VALUE_MASK) == 0xf0 &&
              geti(GL_STENCIL_PASS_DEPTH_FAIL) == GL_INCR && geti(GL_STENCIL_PASS_DEPTH_PASS) == GL_INVERT,
          "the stencil state");
    ogl_BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    CHECK(geti(GL_BLEND_SRC) == GL_SRC_ALPHA && geti(GL_BLEND_DST) == GL_ONE_MINUS_SRC_ALPHA, "the blend factors");
    ogl_ColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
    {
        GLboolean bv[4];

        ogl_GetBooleanv(GL_COLOR_WRITEMASK, bv);
        CHECK(bv[0] && !bv[1] && bv[2] && !bv[3], "GL_COLOR_WRITEMASK");
    }
    ogl_PixelStorei(GL_UNPACK_ALIGNMENT, 2);
    ogl_PixelStorei(GL_PACK_ROW_LENGTH, 100);
    CHECK(geti(GL_UNPACK_ALIGNMENT) == 2 && geti(GL_PACK_ROW_LENGTH) == 100, "the pixel store");
    ogl_PixelStorei(GL_UNPACK_ALIGNMENT, 3);
    CHECK(error_now() == GL_INVALID_VALUE && geti(GL_UNPACK_ALIGNMENT) == 2, "an alignment of 3");
    ogl_PixelTransferf(GL_RED_SCALE, 2.0f);
    ogl_PixelZoom(2.0f, -1.0f);
    CHECK(near_f(getf(GL_RED_SCALE, 0), 2.0f) && near_f(getf(GL_ZOOM_Y, 0), -1.0f), "the pixel transfer and zoom");
    {
        const GLfloat map[4] = { 0.0f, 0.25f, 0.5f, 1.0f };
        GLfloat       back[4];

        ogl_PixelMapfv(GL_PIXEL_MAP_R_TO_R, 4, map);
        ogl_GetPixelMapfv(GL_PIXEL_MAP_R_TO_R, back);
        CHECK(geti(GL_PIXEL_MAP_R_TO_R_SIZE) == 4 && near_f(back[1], 0.25f), "a pixel map");
        ogl_PixelMapfv(GL_PIXEL_MAP_I_TO_R, 3, map);
        CHECK(error_now() == GL_INVALID_VALUE, "an index map of 3 entries");
    }
    /* Matrices */
    ogl_Translatef(1.0f, 2.0f, 3.0f);
    ogl_GetFloatv(GL_MODELVIEW_MATRIX, fv);
    CHECK(near_f(fv[12], 1.0f) && near_f(fv[13], 2.0f) && near_f(fv[14], 3.0f), "glTranslate");
    ogl_PushMatrix();
    CHECK(geti(GL_MODELVIEW_STACK_DEPTH) == 2, "the modelview stack's depth");
    ogl_LoadIdentity();
    ogl_PopMatrix();
    CHECK(near_f(getf(GL_MODELVIEW_MATRIX, 12), 1.0f), "glPopMatrix");
    ogl_MatrixMode(GL_PROJECTION);
    ogl_PushMatrix();
    ogl_PushMatrix();
    CHECK(error_now() == GL_STACK_OVERFLOW && geti(GL_PROJECTION_STACK_DEPTH) == 2, "the projection stack's overflow");
    ogl_PopMatrix();
    ogl_PopMatrix();
    CHECK(error_now() == GL_STACK_UNDERFLOW, "the projection stack's underflow");
    ogl_MatrixMode(GL_MODELVIEW);
    /* Lights and materials: positions in eye coordinates */
    ogl_LoadIdentity();
    ogl_Translatef(1.0f, 0.0f, 0.0f);
    ogl_Lightfv(GL_LIGHT0, GL_POSITION, pos);
    ogl_GetLightfv(GL_LIGHT0, GL_POSITION, fv);
    CHECK(near_f(fv[0], 2.0f) && near_f(fv[1], 2.0f) && near_f(fv[2], 3.0f) && near_f(fv[3], 1.0f), "a light's eye position");
    ogl_LoadIdentity();
    ogl_Materialfv(GL_FRONT, GL_DIFFUSE, diff);
    ogl_GetMaterialfv(GL_FRONT, GL_DIFFUSE, fv);
    CHECK(near_f(fv[1], 0.5f), "a material");
    ogl_GetMaterialfv(GL_BACK, GL_DIFFUSE, fv);
    CHECK(near_f(fv[1], 0.8f), "the back material untouched");
    ogl_LightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1);
    CHECK(getb(GL_LIGHT_MODEL_TWO_SIDE), "two-sided lighting");
    /* Texture environment and generation */
    ogl_TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    ogl_GetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, iv);
    CHECK(iv[0] == GL_DECAL, "GL_TEXTURE_ENV_MODE");
    ogl_TexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    ogl_GetTexGeniv(GL_S, GL_TEXTURE_GEN_MODE, iv);
    CHECK(iv[0] == GL_SPHERE_MAP, "GL_TEXTURE_GEN_MODE");
    ogl_TexGeni(GL_R, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    CHECK(error_now() == GL_INVALID_ENUM, "sphere mapping for r");
    /* Clip planes: in eye coordinates */
    {
        const GLdouble eq[4] = { 1.0, 0.0, 0.0, -1.0 };
        GLdouble       back[4];

        ogl_Translatef(2.0f, 0.0f, 0.0f);
        ogl_ClipPlane(GL_CLIP_PLANE0, eq);
        ogl_GetClipPlane(GL_CLIP_PLANE0, back);
        CHECK(back[0] > 0.99 && back[0] < 1.01 && back[3] > -3.01 && back[3] < -2.99, "a clip plane in eye coordinates: %f %f",
              back[0], back[3]);
        ogl_LoadIdentity();
    }
    /* The polygon stipple, as given */
    {
        GLubyte pat[128], back[128];
        int     i;

        for (i = 0; i < 128; i++)
            pat[i] = (GLubyte) (i * 7);
        ogl_PixelStorei(GL_PACK_ROW_LENGTH, 0);
        ogl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        ogl_PolygonStipple(pat);
        ogl_GetPolygonStipple(back);
        CHECK(!memcmp(pat, back, 128), "the polygon stipple");
    }
    /* Hints */
    ogl_Hint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
    CHECK(geti(GL_PERSPECTIVE_CORRECTION_HINT) == GL_NICEST, "a hint");
    /* Errors in queries */
    ogl_GetIntegerv(GL_TEXTURE_ENV_MODE, iv);
    CHECK(error_now() == GL_INVALID_ENUM, "glGet of a glGetTexEnv name");
    ogl_Begin(GL_POINTS);
    ogl_GetIntegerv(GL_MATRIX_MODE, iv);
    ogl_End();
    CHECK(error_now() == GL_INVALID_OPERATION, "glGet between glBegin and glEnd");
    CHECK(error_now() == GL_NO_ERROR, "an error left");
}

/* glPushAttrib with every group, glPopAttrib, and the client's. */
static void
scenario_attrib(void)
{
    GLint   iv[4];
    GLubyte pat[128];
    int     i;

    machine(16);
    current_context(TEST_DC);
    memset(pat, 0x55, sizeof(pat));
    ogl_PushAttrib(GL_ALL_ATTRIB_BITS);
    CHECK(geti(GL_ATTRIB_STACK_DEPTH) == 1, "GL_ATTRIB_STACK_DEPTH");
    ogl_Enable(GL_BLEND);
    ogl_Enable(GL_LIGHT3);
    ogl_Enable(GL_MAP1_VERTEX_3);
    ogl_BlendFunc(GL_ONE, GL_ONE);
    ogl_Color3f(0.5f, 0.25f, 0.0f);
    ogl_LineWidth(3.0f);
    ogl_PointSize(4.0f);
    ogl_PolygonMode(GL_FRONT, GL_LINE);
    ogl_PolygonStipple(pat);
    ogl_ShadeModel(GL_FLAT);
    ogl_Fogf(GL_FOG_DENSITY, 0.5f);
    ogl_DepthFunc(GL_ALWAYS);
    ogl_ClearStencil(9);
    ogl_Viewport(1, 2, 3, 4);
    ogl_MatrixMode(GL_TEXTURE);
    ogl_ClearColor(0.5f, 0.5f, 0.5f, 0.5f);
    ogl_Hint(GL_FOG_HINT, GL_NICEST);
    ogl_MapGrid1f(4, 0.0f, 2.0f);
    ogl_ListBase(5);
    ogl_TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
    ogl_Scissor(5, 6, 7, 8);
    ogl_PixelTransferf(GL_DEPTH_SCALE, 3.0f);
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ogl_PopAttrib();
    CHECK(geti(GL_ATTRIB_STACK_DEPTH) == 0, "GL_ATTRIB_STACK_DEPTH after the pop");
    CHECK(!ogl_IsEnabled(GL_BLEND) && !ogl_IsEnabled(GL_LIGHT3) && !ogl_IsEnabled(GL_MAP1_VERTEX_3), "the enables");
    CHECK(geti(GL_BLEND_DST) == GL_ZERO && near_f(getf(GL_CURRENT_COLOR, 1), 1.0f), "colour buffer, current");
    CHECK(near_f(getf(GL_LINE_WIDTH, 0), 1.0f) && near_f(getf(GL_POINT_SIZE, 0), 1.0f), "line, point");
    ogl_GetIntegerv(GL_POLYGON_MODE, iv);
    CHECK(iv[0] == GL_FILL && geti(GL_SHADE_MODEL) == GL_SMOOTH, "polygon, lighting");
    ogl_GetPolygonStipple(pat);
    for (i = 0; (i < 128) && (pat[i] == 0xff); i++)
        ;
    CHECK(i == 128, "the polygon stipple");
    CHECK(near_f(getf(GL_FOG_DENSITY, 0), 1.0f) && geti(GL_DEPTH_FUNC) == GL_LESS && geti(GL_STENCIL_CLEAR_VALUE) == 0,
          "fog, depth, stencil");
    ogl_GetIntegerv(GL_VIEWPORT, iv);
    CHECK(iv[2] == 640 && geti(GL_MATRIX_MODE) == GL_MODELVIEW, "viewport, transform");
    CHECK(near_f(getf(GL_COLOR_CLEAR_VALUE, 0), 0.0f) && geti(GL_FOG_HINT) == GL_DONT_CARE, "colour clear, hints");
    CHECK(geti(GL_MAP1_GRID_SEGMENTS) == 1 && geti(GL_LIST_BASE) == 0, "eval, list");
    ogl_GetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, iv);
    CHECK(iv[0] == GL_MODULATE, "texture environment");
    ogl_GetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iv);
    CHECK(iv[0] == GL_NEAREST_MIPMAP_LINEAR, "the bound texture's parameters");
    ogl_GetIntegerv(GL_SCISSOR_BOX, iv);
    CHECK(iv[2] == 640 && near_f(getf(GL_DEPTH_SCALE, 0), 1.0f), "scissor, pixel mode");
    /* One group alone: the others keep their changes */
    ogl_PushAttrib(GL_LINE_BIT);
    ogl_LineWidth(5.0f);
    ogl_PointSize(6.0f);
    ogl_PopAttrib();
    CHECK(near_f(getf(GL_LINE_WIDTH, 0), 1.0f) && near_f(getf(GL_POINT_SIZE, 0), 6.0f), "GL_LINE_BIT alone");
    /* The stack's limit */
    for (i = 0; i < 17; i++)
        ogl_PushAttrib(GL_CURRENT_BIT);
    CHECK(error_now() == GL_STACK_OVERFLOW && geti(GL_ATTRIB_STACK_DEPTH) == 16, "the attribute stack's overflow");
    for (i = 0; i < 17; i++)
        ogl_PopAttrib();
    CHECK(error_now() == GL_STACK_UNDERFLOW, "the attribute stack's underflow");
    /* The client's */
    {
        static const GLfloat verts[9] = { 0 };
        GLvoid              *ptr      = NULL;

        ogl_PushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
        CHECK(geti(GL_CLIENT_ATTRIB_STACK_DEPTH) == 1, "GL_CLIENT_ATTRIB_STACK_DEPTH");
        ogl_PixelStorei(GL_PACK_ALIGNMENT, 1);
        ogl_EnableClientState(GL_VERTEX_ARRAY);
        ogl_VertexPointer(3, GL_FLOAT, 0, verts);
        ogl_PopClientAttrib();
        ogl_GetPointerv(GL_VERTEX_ARRAY_POINTER, &ptr);
        CHECK(geti(GL_PACK_ALIGNMENT) == 4 && !ogl_IsEnabled(GL_VERTEX_ARRAY) && !ptr, "the client attributes");
    }
}

/* Display lists: compiled, run, nested, by every glCallLists type, and the
   commands GL runs at once instead. */
static void
scenario_lists(void)
{
    GLuint  base;
    GLfloat fv[16];

    machine(16);
    current_context(TEST_DC);
    base = ogl_GenLists(4);
    CHECK(base && ogl_IsList(base) && ogl_IsList(base + 3), "glGenLists");
    ogl_NewList(base, GL_COMPILE);
    CHECK(geti(GL_LIST_INDEX) == (GLint) base && geti(GL_LIST_MODE) == GL_COMPILE, "GL_LIST_INDEX and GL_LIST_MODE");
    ogl_Color3f(1.0f, 0.0f, 0.0f);
    ogl_Translatef(1.0f, 0.0f, 0.0f);
    ogl_LineWidth(4.0f);
    ogl_PixelStorei(GL_UNPACK_ALIGNMENT, 1); /* run at once, not compiled */
    ogl_NewList(base + 1, GL_COMPILE);
    CHECK(error_now() == GL_INVALID_OPERATION, "glNewList within glNewList");
    ogl_EndList();
    CHECK(near_f(getf(GL_LINE_WIDTH, 0), 1.0f) && near_f(getf(GL_CURRENT_COLOR, 1), 1.0f) &&
              near_f(getf(GL_MODELVIEW_MATRIX, 12), 0.0f),
          "GL_COMPILE ran the commands");
    CHECK(geti(GL_UNPACK_ALIGNMENT) == 1, "glPixelStore did not run at once");
    ogl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    ogl_CallList(base);
    CHECK(near_f(getf(GL_LINE_WIDTH, 0), 4.0f) && near_f(getf(GL_CURRENT_COLOR, 1), 0.0f) &&
              near_f(getf(GL_MODELVIEW_MATRIX, 12), 1.0f) && geti(GL_UNPACK_ALIGNMENT) == 4,
          "glCallList");
    /* GL_COMPILE_AND_EXECUTE */
    ogl_NewList(base + 1, GL_COMPILE_AND_EXECUTE);
    ogl_PointSize(3.0f);
    ogl_CallList(base); /* nested */
    ogl_EndList();
    CHECK(near_f(getf(GL_POINT_SIZE, 0), 3.0f) && near_f(getf(GL_MODELVIEW_MATRIX, 12), 2.0f), "GL_COMPILE_AND_EXECUTE");
    ogl_LoadIdentity();
    /* glCallLists by type, from the list base */
    {
        const GLubyte  ub[2] = { 0, 0 };
        const GLushort us[1] = { 1 };
        const GLfloat  fl1[1] = { 0.0f };
        const GLubyte  two[2] = { 0, 0 };     /* GL_2_BYTES: 0 */
        const GLubyte  four[4] = { 0, 0, 0, 1 }; /* GL_4_BYTES: 1 */

        ogl_ListBase(base);
        ogl_CallLists(2, GL_UNSIGNED_BYTE, ub);
        CHECK(near_f(getf(GL_MODELVIEW_MATRIX, 12), 2.0f), "glCallLists by GL_UNSIGNED_BYTE");
        ogl_LoadIdentity();
        ogl_CallLists(1, GL_UNSIGNED_SHORT, us); /* base + 1: base's translation, nested */
        CHECK(near_f(getf(GL_MODELVIEW_MATRIX, 12), 1.0f), "glCallLists by GL_UNSIGNED_SHORT (nested)");
        ogl_LoadIdentity();
        ogl_CallLists(1, GL_FLOAT, fl1);
        ogl_CallLists(1, GL_2_BYTES, two);
        ogl_CallLists(1, GL_4_BYTES, four);
        CHECK(near_f(getf(GL_MODELVIEW_MATRIX, 12), 3.0f), "glCallLists by GL_FLOAT, GL_2_BYTES, GL_4_BYTES: %f",
              getf(GL_MODELVIEW_MATRIX, 12));
        ogl_CallLists(1, GL_RGBA, ub);
        CHECK(error_now() == GL_INVALID_ENUM, "glCallLists of another type");
        ogl_ListBase(0);
        ogl_LoadIdentity();
    }
    /* A list that calls itself stops at the nesting limit */
    ogl_NewList(base + 2, GL_COMPILE);
    ogl_Translatef(1.0f, 0.0f, 0.0f);
    ogl_CallList(base + 2);
    ogl_EndList();
    ogl_CallList(base + 2);
    CHECK(near_f(getf(GL_MODELVIEW_MATRIX, 12), 64.0f), "the nesting limit: %f", getf(GL_MODELVIEW_MATRIX, 12));
    ogl_LoadIdentity();
    /* An evaluator's map compiled */
    {
        const GLfloat pts[6] = { 0.0f, 0.0f, 0.0f, 4.0f, 0.0f, 0.0f };

        ogl_NewList(base + 3, GL_COMPILE);
        ogl_Map1f(GL_MAP1_VERTEX_3, 0.0f, 1.0f, 3, 2, pts);
        ogl_EndList();
        ogl_GetMapfv(GL_MAP1_VERTEX_3, GL_COEFF, fv);
        CHECK(near_f(fv[3], 0.0f), "glMap1 ran when compiled");
        ogl_CallList(base + 3);
        ogl_GetMapfv(GL_MAP1_VERTEX_3, GL_COEFF, fv);
        CHECK(near_f(fv[3], 4.0f), "glMap1 from a list: %f", fv[3]);
    }
    /* Deleting */
    ogl_DeleteLists(base, 4);
    CHECK(!ogl_IsList(base) && !ogl_IsList(base + 3), "glDeleteLists");
    ogl_EndList();
    CHECK(error_now() == GL_INVALID_OPERATION, "glEndList without glNewList");
    ogl_NewList(0, GL_COMPILE);
    CHECK(error_now() == GL_INVALID_VALUE, "glNewList of list 0");
}

/* Selection: hit records by GL's rules, on known geometry. */
static void
scenario_select(void)
{
    GLuint buf[64];

    machine(16);
    current_context(TEST_DC);
    memset(buf, 0xee, sizeof(buf));
    ogl_SelectBuffer(64, buf);
    ogl_PushName(1); /* ignored in GL_RENDER */
    CHECK(error_now() == GL_NO_ERROR && geti(GL_NAME_STACK_DEPTH) == 0, "glPushName in GL_RENDER");
    CHECK(ogl_RenderMode(GL_SELECT) == 0, "entering GL_SELECT");
    ogl_InitNames();
    ogl_PushName(7);
    ogl_Begin(GL_TRIANGLES);
    ogl_Vertex3f(0.0f, 0.0f, 0.5f);
    ogl_Vertex3f(0.5f, 0.0f, 0.0f);
    ogl_Vertex3f(0.0f, 0.5f, -0.5f);
    ogl_End();
    ogl_LoadName(8);
    ogl_Begin(GL_TRIANGLES); /* wholly outside: no hit */
    ogl_Vertex3f(5.0f, 0.0f, 0.0f);
    ogl_Vertex3f(6.0f, 0.0f, 0.0f);
    ogl_Vertex3f(5.0f, 1.0f, 0.0f);
    ogl_End();
    ogl_PushName(9);
    ogl_RasterPos2f(0.0f, 0.0f); /* a valid raster position is a hit (GL 1.1, 5.2) */
    ogl_PopName();
    ogl_PopName();
    ogl_PopName();
    CHECK(error_now() == GL_STACK_UNDERFLOW, "the name stack's underflow");
    CHECK(ogl_RenderMode(GL_RENDER) == 2, "the hits");
    CHECK(buf[0] == 1 && buf[3] == 7, "the first hit record's names: %u %u", buf[0], buf[3]);
    /* Window z 0.25 to 0.75, scaled to 2^32 - 1 */
    CHECK(buf[1] > 0x3fff0000u && buf[1] < 0x40010000u && buf[2] > 0xbfff0000u && buf[2] < 0xc0010000u,
          "the hit's depths: %x %x", buf[1], buf[2]);
    CHECK(buf[4] == 2 && buf[7] == 8 && buf[8] == 9, "the raster position's record: %u %u %u", buf[4], buf[7], buf[8]);
    CHECK(buf[5] > 0x7fff0000u && buf[5] < 0x80010000u, "the raster position's depth: %x", buf[5]);
    CHECK(buf[9] == 0xeeeeeeeeu, "past the records");
    /* An overflowing buffer */
    ogl_SelectBuffer(2, buf);
    ogl_RenderMode(GL_SELECT);
    ogl_PushName(3);
    ogl_Begin(GL_POINTS);
    ogl_Vertex2f(0.0f, 0.0f);
    ogl_End();
    CHECK(ogl_RenderMode(GL_RENDER) == -1, "an overflowing selection buffer");
}

/* Feedback: every token, by GL's rules, on known geometry; arrays drawn. */
static void
scenario_feedback(void)
{
    GLfloat fb[128];
    GLint   n;
    static const GLfloat  verts[9] = { 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f };
    static const GLushort idx[3]   = { 2, 1, 0 };

    machine(16);
    current_context(TEST_DC);
    ogl_FeedbackBuffer(128, GL_3D_COLOR, fb);
    ogl_RenderMode(GL_FEEDBACK);
    ogl_PassThrough(5.0f);
    ogl_Color4f(1.0f, 0.0f, 0.0f, 1.0f);
    ogl_Begin(GL_POINTS);
    ogl_Vertex3f(0.0f, 0.0f, 0.0f);
    ogl_End();
    ogl_Begin(GL_LINES);
    ogl_Vertex3f(0.0f, 0.0f, 0.0f);
    ogl_Vertex3f(0.5f, 0.0f, 0.0f);
    ogl_End();
    n = ogl_RenderMode(GL_RENDER);
    CHECK(n == 25, "feedback values: %d", n);
    CHECK(fb[0] == GL_PASS_THROUGH_TOKEN && fb[1] == 5.0f, "the pass-through");
    CHECK(fb[2] == GL_POINT_TOKEN && near_f(fb[3], WX(0)) && near_f(fb[4], WY(0)) && near_f(fb[5], 0.5f) && near_f(fb[6], 1.0f) &&
              near_f(fb[7], 0.0f) && near_f(fb[9], 1.0f),
          "the point: %f %f %f", fb[3], fb[4], fb[5]);
    CHECK(fb[10] == GL_LINE_RESET_TOKEN && near_f(fb[11], WX(0)) && near_f(fb[18], WX(0.5f)), "the line: %f %f %f", fb[10],
          fb[11], fb[18]);
    /* Arrays, in GL_3D */
    ogl_FeedbackBuffer(128, GL_3D, fb);
    ogl_RenderMode(GL_FEEDBACK);
    ogl_VertexPointer(3, GL_FLOAT, 0, verts);
    ogl_EnableClientState(GL_VERTEX_ARRAY);
    ogl_DrawArrays(GL_TRIANGLES, 0, 3);
    ogl_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
    ogl_Begin(GL_POINTS);
    ogl_ArrayElement(1);
    ogl_End();
    n = ogl_RenderMode(GL_RENDER);
    CHECK(n == 11 + 11 + 4, "array feedback values: %d", n);
    CHECK(fb[0] == GL_POLYGON_TOKEN && fb[1] == 3.0f && near_f(fb[2], WX(0)) && near_f(fb[5], WX(0.5f)) && near_f(fb[9], WY(0.5f)),
          "glDrawArrays' triangle");
    CHECK(fb[11] == GL_POLYGON_TOKEN && near_f(fb[14], WY(0.5f)) && near_f(fb[19], WX(0)), "glDrawElements' triangle");
    CHECK(fb[22] == GL_POINT_TOKEN && near_f(fb[23], WX(0.5f)), "glArrayElement's point");
    ogl_DisableClientState(GL_VERTEX_ARRAY);
    /* glInterleavedArrays */
    {
        static const GLfloat cv[12] = { 1, 0, 0, 0, 0, 0, 0, 1, 0, 0.5f, 0, 0 };
        GLvoid              *ptr    = NULL;

        ogl_InterleavedArrays(GL_C3F_V3F, 0, cv);
        ogl_GetPointerv(GL_VERTEX_ARRAY_POINTER, &ptr);
        CHECK(ogl_IsEnabled(GL_COLOR_ARRAY) && ogl_IsEnabled(GL_VERTEX_ARRAY) && !ogl_IsEnabled(GL_NORMAL_ARRAY) &&
                  geti(GL_COLOR_ARRAY_STRIDE) == 24 && ptr == (const GLvoid *) (cv + 3),
              "glInterleavedArrays' arrays");
    }
    /* The buffer's overflow, and the errors */
    ogl_FeedbackBuffer(4, GL_2D, fb);
    ogl_RenderMode(GL_FEEDBACK);
    ogl_FeedbackBuffer(4, GL_2D, fb);
    CHECK(error_now() == GL_INVALID_OPERATION, "glFeedbackBuffer in GL_FEEDBACK");
    ogl_Begin(GL_POINTS);
    ogl_Vertex2f(0.0f, 0.0f);
    ogl_Vertex2f(0.1f, 0.0f);
    ogl_End();
    CHECK(ogl_RenderMode(GL_RENDER) == -1, "an overflowing feedback buffer");
}

/* Evaluators: curves, surfaces, grids and meshes, through feedback. */
static void
scenario_eval(void)
{
    GLfloat fb[128];
    GLfloat fv[16];
    GLint   n;
    static const GLfloat line[6]  = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
    static const GLfloat color[8] = { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f };
    /* A bilinear patch: P(u, v) = (u, v, 0); ustride 3, vstride 6 */
    static const GLfloat patch[12] = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0 };

    machine(16);
    current_context(TEST_DC);
    ogl_Map1f(GL_MAP1_VERTEX_3, 0.0f, 1.0f, 3, 2, line);
    ogl_Map1f(GL_MAP1_COLOR_4, 0.0f, 1.0f, 4, 2, color);
    ogl_Enable(GL_MAP1_VERTEX_3);
    ogl_Enable(GL_MAP1_COLOR_4);
    ogl_GetMapfv(GL_MAP1_VERTEX_3, GL_ORDER, fv);
    CHECK(near_f(fv[0], 2.0f), "GL_ORDER");
    ogl_GetMapfv(GL_MAP1_VERTEX_3, GL_DOMAIN, fv);
    CHECK(near_f(fv[0], 0.0f) && near_f(fv[1], 1.0f), "GL_DOMAIN");
    ogl_FeedbackBuffer(128, GL_3D_COLOR, fb);
    ogl_RenderMode(GL_FEEDBACK);
    ogl_Begin(GL_POINTS);
    ogl_EvalCoord1f(0.5f);
    ogl_End();
    ogl_MapGrid1f(2, 0.0f, 1.0f);
    ogl_EvalMesh1(GL_POINT, 0, 2);
    n = ogl_RenderMode(GL_RENDER);
    CHECK(n == 4 * 8, "evaluated points: %d", n);
    CHECK(fb[0] == GL_POINT_TOKEN && near_f(fb[1], WX(0.5f)) && near_f(fb[2], WY(0)) && near_f(fb[4], 0.5f),
          "glEvalCoord1's point and colour: %f %f", fb[1], fb[4]);
    CHECK(near_f(fb[9], WX(0)) && near_f(fb[17], WX(0.5f)) && near_f(fb[25], WX(1.0f)), "glEvalMesh1's points");
    CHECK(near_f(getf(GL_CURRENT_COLOR, 0), 1.0f) && near_f(getf(GL_CURRENT_COLOR, 1), 1.0f),
          "an evaluated colour changed the current one");
    ogl_Disable(GL_MAP1_COLOR_4);
    /* Surfaces */
    ogl_Map2f(GL_MAP2_VERTEX_3, 0.0f, 1.0f, 3, 2, 0.0f, 1.0f, 6, 2, patch);
    ogl_Enable(GL_MAP2_VERTEX_3);
    ogl_Enable(GL_AUTO_NORMAL);
    ogl_GetMapfv(GL_MAP2_VERTEX_3, GL_COEFF, fv);
    CHECK(near_f(fv[6], 0.0f) && near_f(fv[7], 1.0f), "GL_COEFF in GL's order (u-major): %f %f", fv[6], fv[7]);
    ogl_FeedbackBuffer(128, GL_3D, fb);
    ogl_RenderMode(GL_FEEDBACK);
    ogl_Begin(GL_POINTS);
    ogl_EvalCoord2f(0.5f, 0.25f);
    ogl_End();
    ogl_MapGrid2f(1, 0.0f, 1.0f, 1, 0.0f, 1.0f);
    ogl_EvalMesh2(GL_FILL, 0, 1, 0, 1);
    n = ogl_RenderMode(GL_RENDER);
    CHECK(fb[0] == GL_POINT_TOKEN && near_f(fb[1], WX(0.5f)) && near_f(fb[2], WY(0.25f)), "glEvalCoord2's point: %f %f", fb[1], fb[2]);
    CHECK(n == 4 + 14, "glEvalMesh2's quadrilateral: %d", n);
    CHECK(near_f(getf(GL_CURRENT_NORMAL, 2), 1.0f) && near_f(getf(GL_CURRENT_NORMAL, 0), 0.0f), "the current normal changed");
    /* Errors */
    ogl_Map1f(GL_MAP1_VERTEX_3, 0.0f, 1.0f, 3, 0, line);
    CHECK(error_now() == GL_INVALID_VALUE, "an order of 0");
    ogl_Map1f(GL_MAP2_VERTEX_3, 0.0f, 1.0f, 3, 2, line);
    CHECK(error_now() == GL_INVALID_ENUM, "glMap1 of a GL_MAP2 target");
    ogl_EvalMesh1(GL_FILL, 0, 1);
    CHECK(error_now() == GL_INVALID_ENUM, "glEvalMesh1 with GL_FILL");
}

/* The raster position: transformed, clipped, its colour and distance. */
static void
scenario_rasterpos(void)
{
    GLfloat fv[4];

    machine(16);
    current_context(TEST_DC);
    ogl_Color3f(0.5f, 0.25f, 1.0f);
    ogl_RasterPos3f(0.5f, -0.5f, 0.0f);
    ogl_GetFloatv(GL_CURRENT_RASTER_POSITION, fv);
    CHECK(near_f(fv[0], WX(0.5f)) && near_f(fv[1], WY(-0.5f)) && near_f(fv[2], 0.5f) && near_f(fv[3], 1.0f),
          "GL_CURRENT_RASTER_POSITION: %f %f %f %f", fv[0], fv[1], fv[2], fv[3]);
    CHECK(getb(GL_CURRENT_RASTER_POSITION_VALID), "the raster position's valid bit");
    CHECK(near_f(getf(GL_CURRENT_RASTER_COLOR, 1), 0.25f), "GL_CURRENT_RASTER_COLOR");
    ogl_Translatef(0.0f, 0.0f, -0.5f);
    ogl_RasterPos2f(0.0f, 0.0f);
    CHECK(near_f(getf(GL_CURRENT_RASTER_DISTANCE, 0), 0.5f), "GL_CURRENT_RASTER_DISTANCE: %f", getf(GL_CURRENT_RASTER_DISTANCE, 0));
    ogl_RasterPos2f(3.0f, 0.0f);
    CHECK(!getb(GL_CURRENT_RASTER_POSITION_VALID), "a clipped raster position is valid");
}

/* The error rules. */
static void
scenario_errors(void)
{
    GLint iv[4];

    machine(16);
    current_context(TEST_DC);
    CHECK(error_now() == GL_NO_ERROR, "an error at the start");
    ogl_Enable(GL_RGBA);
    CHECK(error_now() == GL_INVALID_ENUM, "glEnable of another name");
    ogl_Viewport(0, 0, -1, 4);
    CHECK(error_now() == GL_INVALID_VALUE, "a negative viewport");
    ogl_PopMatrix();
    CHECK(error_now() == GL_STACK_UNDERFLOW, "the modelview stack's underflow");
    ogl_Begin(GL_POLYGON + 1);
    CHECK(error_now() == GL_INVALID_ENUM, "glBegin of another mode");
    ogl_Begin(GL_TRIANGLES);
    ogl_Begin(GL_TRIANGLES);
    ogl_Enable(GL_BLEND);
    ogl_LoadIdentity();
    ogl_Vertex2f(0.0f, 0.0f);
    ogl_Color3f(1.0f, 1.0f, 1.0f); /* allowed */
    CHECK(ogl_GetError() == GL_NO_ERROR, "glGetError between glBegin and glEnd");
    ogl_End();
    CHECK(error_now() == GL_INVALID_OPERATION && !ogl_IsEnabled(GL_BLEND), "commands between glBegin and glEnd");
    CHECK(error_now() == GL_NO_ERROR, "the flag was not cleared");
    ogl_End();
    CHECK(error_now() == GL_INVALID_OPERATION, "glEnd without glBegin");
    /* Two different errors: both reported, then none */
    ogl_Enable(GL_RGBA);
    ogl_Viewport(0, 0, -1, 4);
    iv[0] = (GLint) error_now();
    iv[1] = (GLint) error_now();
    CHECK(((iv[0] == GL_INVALID_ENUM) && (iv[1] == GL_INVALID_VALUE)) || ((iv[0] == GL_INVALID_VALUE) && (iv[1] == GL_INVALID_ENUM)),
          "two errors: %x %x", iv[0], iv[1]);
    CHECK(error_now() == GL_NO_ERROR, "a third error");
}

/* --- Drawing -------------------------------------------------------------------------- */

/* A DRAW's index k: the vertex it names, and that vertex's dword j. */
static DWORD
draw_index(const pkt_t *k, DWORD i)
{
    const DWORD w = k->p[VGPU98_OGL_LEN_DRAW + k->p[2] * VGPU98_OGL_VERTEX_DWORDS + i / 2];

    return (i & 1) ? (w >> 16) : (w & 0xffff);
}

static float
draw_vertex(const pkt_t *k, DWORD i, DWORD j)
{
    return fl(k->p[VGPU98_OGL_LEN_DRAW + draw_index(k, i) * VGPU98_OGL_VERTEX_DWORDS + j]);
}

/* A primitive of n vertices at x = 0, 1, ..., n - 1 (y alternating), then
   the DRAW it made: its kind, and each index's vertex as its x. */
static void
expect_prim(GLenum mode, int n, DWORD kind, const char *xs)
{
    const pkt_t *k;
    int          i, ni = (int) strlen(xs), nv = 0;
    int          seen[10] = { 0 };

    /* The vertices sent: those the indices name (a primitive's leftover
       vertices are not) */
    for (i = 0; i < ni; i++) {
        nv += !seen[xs[i] - '0'];
        seen[xs[i] - '0'] = 1;
    }
    m.n_pkts = 0;
    ogl_Begin(mode);
    for (i = 0; i < n; i++) {
        ogl_Color3f((float) i / 8.0f, 0.0f, 0.0f);
        ogl_Vertex2f((float) i, (float) (i & 1));
    }
    ogl_End();
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_DRAW);
    CHECK(k && (count_pkts(VGPU98_OGL_DRAW) == 1), "mode %x: one DRAW", mode);
    if (!k)
        return;
    CHECK(k->p[1] == kind && (int) k->p[2] == nv && (int) k->p[3] == ni, "mode %x: kind %u, %u vertices, %u indices", mode, k->p[1],
          k->p[2], k->p[3]);
    for (i = 0; (i < ni) && (i < (int) k->p[3]); i++)
        CHECK(near_f(draw_vertex(k, (DWORD) i, VGPU98_OGLV_X), (float) (xs[i] - '0')), "mode %x: index %d is vertex %.0f, not %c",
              mode, i, draw_vertex(k, (DWORD) i, VGPU98_OGLV_X), xs[i]);
}

static void
scenario_draw(void)
{
    const pkt_t *k;
    int          i, at_draw, at_state;
    DWORD        id;

    machine(16);
    current_context(TEST_DC);
    ogl_Flush();
    id = nth_pkt(VGPU98_OGL_CONTEXT, 0)->p[0];
    /* Every primitive kind: GL's provoking vertex first in each triangle
       (GL 1.1, table 2.2), each a rotation of GL's order */
    expect_prim(GL_POINTS, 2, VGPU98_OGL_POINTS, "01");
    expect_prim(GL_LINES, 5, VGPU98_OGL_LINES, "0123");
    expect_prim(GL_LINE_STRIP, 3, VGPU98_OGL_LINES, "0112");
    expect_prim(GL_LINE_LOOP, 3, VGPU98_OGL_LINES, "011220");
    expect_prim(GL_TRIANGLES, 4, VGPU98_OGL_TRIANGLES, "201");
    expect_prim(GL_TRIANGLE_STRIP, 5, VGPU98_OGL_TRIANGLES, "201321423");
    expect_prim(GL_TRIANGLE_FAN, 4, VGPU98_OGL_TRIANGLES, "201302");
    expect_prim(GL_QUADS, 4, VGPU98_OGL_TRIANGLES, "301312");
    expect_prim(GL_QUAD_STRIP, 6, VGPU98_OGL_TRIANGLES, "320301542523");
    expect_prim(GL_POLYGON, 5, VGPU98_OGL_TRIANGLES, "012023034");
    /* The vertex as given: object coordinates, the colour, the normal, the
       texture coordinates */
    m.n_pkts = 0;
    ogl_Normal3f(0.0f, 1.0f, 0.0f);
    ogl_TexCoord2f(0.25f, 0.75f);
    ogl_Begin(GL_POINTS);
    ogl_Color4f(0.5f, 2.0f, -1.0f, 0.25f);
    ogl_Vertex4f(1.0f, 2.0f, 3.0f, 4.0f);
    ogl_End();
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_DRAW);
    CHECK(k && near_f(draw_vertex(k, 0, VGPU98_OGLV_W), 4.0f) && near_f(draw_vertex(k, 0, VGPU98_OGLV_G), 2.0f) &&
              near_f(draw_vertex(k, 0, VGPU98_OGLV_B), -1.0f) && near_f(draw_vertex(k, 0, VGPU98_OGLV_NY), 1.0f) &&
              near_f(draw_vertex(k, 0, VGPU98_OGLV_T), 0.75f) && near_f(draw_vertex(k, 0, VGPU98_OGLV_Q), 1.0f) &&
              (k->p[0] == id),
          "the device path's vertex");
    /* Primitives of one kind share a DRAW; a register's change goes
       between them */
    m.n_pkts = 0;
    for (i = 0; i < 3; i++) {
        ogl_Begin(GL_TRIANGLES);
        ogl_Vertex2f(0.0f, 0.0f);
        ogl_Vertex2f(1.0f, 0.0f);
        ogl_Vertex2f(0.0f, 1.0f);
        ogl_End();
    }
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_DRAW);
    CHECK(count_pkts(VGPU98_OGL_DRAW) == 1 && k && k->p[2] == 9 && k->p[3] == 9, "three primitives in one DRAW: %d DRAWs",
          count_pkts(VGPU98_OGL_DRAW));
    m.n_pkts = 0;
    ogl_Begin(GL_TRIANGLES);
    ogl_Vertex2f(0.0f, 0.0f);
    ogl_Vertex2f(1.0f, 0.0f);
    ogl_Vertex2f(0.0f, 1.0f);
    ogl_End();
    ogl_Enable(GL_FOG);
    ogl_Begin(GL_TRIANGLES);
    ogl_Vertex2f(0.0f, 0.0f);
    ogl_Vertex2f(1.0f, 0.0f);
    ogl_Vertex2f(0.0f, 1.0f);
    ogl_End();
    ogl_Flush();
    at_draw = at_state = -1;
    for (i = 0; i < m.n_pkts; i++) {
        if ((m.pkts[i].op == VGPU98_OGL_DRAW) && (at_draw < 0))
            at_draw = i;
        if (m.pkts[i].op == VGPU98_OGL_STATE)
            at_state = i;
    }
    CHECK(count_pkts(VGPU98_OGL_DRAW) == 2 && at_draw >= 0 && at_state > at_draw, "a STATE between the DRAWs: %d %d", at_draw,
          at_state);
    CHECK(reg_sent(VGPU98_OGL_R_ENABLES, 0) & VGPU98_OGL_EN_FOG, "the fog's enable sent");
    /* A glClear after a primitive: the DRAW goes first */
    m.n_pkts = 0;
    ogl_Begin(GL_POINTS);
    ogl_Vertex2f(0.0f, 0.0f);
    ogl_End();
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    CHECK(m.n_pkts >= 2 && count_pkts(VGPU98_OGL_DRAW) == 1 && count_pkts(VGPU98_OGL_CLEAR) == 1, "a DRAW and a CLEAR");
    for (i = 0; (i < m.n_pkts) && (m.pkts[i].op != VGPU98_OGL_DRAW) && (m.pkts[i].op != VGPU98_OGL_CLEAR); i++)
        ;
    CHECK((i < m.n_pkts) && (m.pkts[i].op == VGPU98_OGL_DRAW), "the CLEAR went before the DRAW before it");
    /* A primitive larger than one DRAW: split, every triangle kept */
    m.n_pkts = 0;
    ogl_Begin(GL_TRIANGLES);
    for (i = 0; i < 3 * 1400; i++)
        ogl_Vertex2f((float) (i % 3) * 0.1f, 0.0f);
    ogl_End();
    ogl_Flush();
    {
        DWORD tris = 0;

        for (i = 0; i < m.n_pkts; i++) {
            if (m.pkts[i].op == VGPU98_OGL_DRAW) {
                CHECK(m.pkts[i].p[2] <= 768 && m.pkts[i].p[3] <= 4096, "a DRAW too large: %u %u", m.pkts[i].p[2], m.pkts[i].p[3]);
                tris += m.pkts[i].p[3] / 3;
            }
        }
        CHECK(tris == 1400 && count_pkts(VGPU98_OGL_DRAW) > 1, "a long primitive's triangles: %u in %d DRAWs", tris,
              count_pkts(VGPU98_OGL_DRAW));
    }
}

/* The ICD's own path in GL_RENDER: polygon modes, materials
   between glBegin and glEnd, stippled strips; clip coordinates, lit. */
static void
scenario_cpudraw(void)
{
    const pkt_t  *k;
    const GLfloat red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };

    machine(16);
    current_context(TEST_DC);
    ogl_Flush();
    /* GL_LINE: the triangle's edges, as a polygon's */
    m.n_pkts = 0;
    ogl_PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    ogl_Begin(GL_TRIANGLES);
    ogl_Vertex2f(0.0f, 0.0f);
    ogl_Vertex2f(0.5f, 0.0f);
    ogl_Vertex2f(0.0f, 0.5f);
    ogl_End();
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_DRAW);
    CHECK(k && (k->p[1] == (VGPU98_OGL_LINES | VGPU98_OGL_DRAW_CLIP | VGPU98_OGL_DRAW_STIPPLE_ON | VGPU98_OGL_DRAW_POLYGON)) &&
              (k->p[3] == 6),
          "GL_LINE's DRAW: %x, %u indices", k ? k->p[1] : 0, k ? k->p[3] : 0);
    CHECK(k && near_f(draw_vertex(k, 2, VGPU98_OGLV_X), 0.5f) && near_f(draw_vertex(k, 2, VGPU98_OGLV_W), 1.0f),
          "the second edge's start in clip coordinates");
    /* The stipple counter: from the polygon's first edge, along the rest
       (160 pixels the first) */
    CHECK(k && near_f(draw_vertex(k, 0, VGPU98_OGLV_STIPPLE), 0.0f) && near_f(draw_vertex(k, 2, VGPU98_OGLV_STIPPLE), 160.0f),
          "the edges' stipple counters: %f", k ? draw_vertex(k, 2, VGPU98_OGLV_STIPPLE) : 0.0f);
    ogl_PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    /* glMaterial inside glBegin: lit here. Light 0 (white, along z), the
       normal (0, 0, 1), diffuse red: 0.2 * 0.2 + 1 (red), 0.04 (green) */
    m.n_pkts = 0;
    ogl_Enable(GL_LIGHTING);
    ogl_Enable(GL_LIGHT0);
    ogl_Begin(GL_TRIANGLES);
    ogl_Materialfv(GL_FRONT, GL_DIFFUSE, red);
    ogl_Vertex2f(0.0f, 0.0f);
    ogl_Vertex2f(0.5f, 0.0f);
    ogl_Vertex2f(0.0f, 0.5f);
    ogl_End();
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_DRAW);
    CHECK(k && (k->p[1] == (VGPU98_OGL_TRIANGLES | VGPU98_OGL_DRAW_CLIP)) && (k->p[3] == 3), "the lit triangle's DRAW: %x",
          k ? k->p[1] : 0);
    CHECK(k && near_f(draw_vertex(k, 0, VGPU98_OGLV_R), 1.0f) && near_f(draw_vertex(k, 0, VGPU98_OGLV_G), 0.04f) &&
              near_f(draw_vertex(k, 0, VGPU98_OGLV_A), 1.0f),
          "the lit colour: %f %f", k ? draw_vertex(k, 0, VGPU98_OGLV_R) : 0.0f, k ? draw_vertex(k, 0, VGPU98_OGLV_G) : 0.0f);
    ogl_Disable(GL_LIGHTING);
    /* A stippled strip: the counter goes on along it */
    m.n_pkts = 0;
    ogl_Enable(GL_LINE_STIPPLE);
    ogl_Begin(GL_LINE_STRIP);
    ogl_Vertex2f(0.0f, 0.0f);
    ogl_Vertex2f(0.5f, 0.0f);
    ogl_Vertex2f(1.0f, 0.0f);
    ogl_End();
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_DRAW);
    CHECK(k && (k->p[1] == (VGPU98_OGL_LINES | VGPU98_OGL_DRAW_CLIP | VGPU98_OGL_DRAW_STIPPLE_ON)) && (k->p[3] == 4),
          "the strip's DRAW: %x", k ? k->p[1] : 0);
    CHECK(k && near_f(draw_vertex(k, 2, VGPU98_OGLV_STIPPLE), 160.0f) && near_f(draw_vertex(k, 3, VGPU98_OGLV_STIPPLE), 320.0f),
          "the strip's counters");
    /* Independent lines stay on the device's path */
    m.n_pkts = 0;
    ogl_Begin(GL_LINES);
    ogl_Vertex2f(0.0f, 0.0f);
    ogl_Vertex2f(0.5f, 0.0f);
    ogl_End();
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_DRAW);
    CHECK(k && (k->p[1] == VGPU98_OGL_LINES), "GL_LINES on the device's path: %x", k ? k->p[1] : 0);
    ogl_Disable(GL_LINE_STIPPLE);
    /* A concave polygon that clipping grows past its buffers (audit 2,
       item 1): drawn without a fault, in GL_FEEDBACK and through GL_LINE */
    {
        static GLfloat fb[8192];
        int            i;

        ogl_FeedbackBuffer(8192, GL_2D, fb);
        ogl_RenderMode(GL_FEEDBACK);
        ogl_Begin(GL_POLYGON);
        for (i = 0; i < 480; i++)
            ogl_Vertex2f((i & 1) ? 1.5f : 0.5f, -0.9f + 1.8f * (float) i / 480.0f);
        ogl_End();
        CHECK(ogl_RenderMode(GL_RENDER) > 0, "the concave polygon's feedback");
        ogl_PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        ogl_Begin(GL_POLYGON);
        for (i = 0; i < 480; i++)
            ogl_Vertex2f((i & 1) ? 1.5f : 0.5f, -0.9f + 1.8f * (float) i / 480.0f);
        ogl_End();
        ogl_Flush();
        ogl_PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
}

/* Colour material's tracking at its enable, and the device's material
   after its disable (audit 2, item 2); a long list operation (item 4). */
static void
scenario_colormat(void)
{
    GLfloat fv[4];

    machine(16);
    current_context(TEST_DC);
    ogl_Color3f(1.0f, 0.0f, 0.0f);
    ogl_Enable(GL_COLOR_MATERIAL);
    ogl_GetMaterialfv(GL_FRONT, GL_DIFFUSE, fv);
    CHECK(near_f(fv[0], 1.0f) && near_f(fv[1], 0.0f), "the material at the enable: %f %f", fv[0], fv[1]);
    ogl_Color3f(0.0f, 1.0f, 0.0f);
    ogl_Disable(GL_COLOR_MATERIAL);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    CHECK(near_f(fl(reg_sent(VGPU98_OGL_R_MATERIAL(0) + VGPU98_OGL_M_DIFFUSE + 1, 0)), 1.0f) &&
              near_f(fl(reg_sent(VGPU98_OGL_R_MATERIAL(0) + VGPU98_OGL_M_DIFFUSE, 0)), 0.0f),
          "the device's material after the disable");
    {
        enum { N = 33000 };
        static GLuint   names[N];
        static GLclampf pri[N];
        GLuint          list;
        GLint           iv;
        int             i;

        ogl_GenTextures(N, names);
        for (i = 0; i < N; i++)
            pri[i] = 0.5f;
        list = ogl_GenLists(1);
        ogl_NewList(list, GL_COMPILE);
        ogl_PrioritizeTextures(N, names, pri);
        ogl_EndList();
        ogl_CallList(list);
        ogl_BindTexture(GL_TEXTURE_2D, names[N - 1]);
        ogl_GetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, &iv);
        CHECK(iv > 0x3fff0000 && iv < 0x40010000, "the last priority of a long list operation: %x", iv);
        CHECK(names[1] == names[0] + 1, "glGenTextures' names in a row");
    }
}

/* --- Textures and pixels --------------------------------------------------------------------- */

static DWORD
rgba8(int r, int g, int b, int a)
{
    return (DWORD) r | ((DWORD) g << 8) | ((DWORD) b << 16) | ((DWORD) a << 24);
}

/* glTexImage, glTexSubImage, proxies, completeness, the texels sent. */
static void
scenario_teximage(void)
{
    static GLubyte img[4 * 4 * 4], big[256 * 256 * 4];
    const GLubyte  white[2 * 2 * 4] = { 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255 };
    const pkt_t   *k;
    GLuint         tex[2], list;
    GLint          iv;
    DWORD          dev;
    int            i, n;

    machine(16);
    current_context(TEST_DC);
    ogl_Flush();
    for (i = 0; i < 64; i++)
        img[i] = (GLubyte) (i * 4);
    ogl_GenTextures(2, tex);
    ogl_BindTexture(GL_TEXTURE_2D, tex[0]);
    m.n_pkts = 0;
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_TEXIMAGE);
    CHECK(k && (k->p[2] == (2u | VGPU98_OGL_TEX_DEFINE | VGPU98_OGL_TEX_TEXELS)) && (k->p[3] == (4u | (4u << 16))) &&
              (k->p[5] == VGPU98_OGL_BASE_RGBA) && (k->len == VGPU98_OGL_LEN_TEXIMAGE + 16),
          "TEXIMAGE's level: %x %x %u", k ? k->p[2] : 0, k ? k->p[3] : 0, k ? k->len : 0);
    CHECK(k && (k->p[6] == rgba8(0, 4, 8, 12)) && (k->p[7] == rgba8(16, 20, 24, 28)), "TEXIMAGE's texels: %x", k ? k->p[6] : 0);
    dev = k ? k->p[1] : 0;
    CHECK(count_pkts(VGPU98_OGL_TEXPARAM) == 1 && last_pkt(VGPU98_OGL_TEXPARAM)->p[1] == dev, "the texture's parameters first");
    /* Completeness: the default filter needs mipmaps */
    ogl_Enable(GL_TEXTURE_2D);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    CHECK(reg_sent(VGPU98_OGL_R_TEXTURE_ID, 0) == 0, "an incomplete texture drawn with");
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    CHECK(reg_sent(VGPU98_OGL_R_TEXTURE_ID, 0) == dev, "the complete texture not drawn with");
    /* Luminance, through the transfer path */
    m.n_pkts = 0;
    ogl_PixelTransferf(GL_RED_SCALE, 0.5f);
    ogl_TexImage2D(GL_TEXTURE_2D, 1, GL_LUMINANCE, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    ogl_PixelTransferf(GL_RED_SCALE, 1.0f);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_TEXIMAGE);
    CHECK(k && (k->p[2] == (2u | (1u << VGPU98_OGL_TEX_LEVEL_SHIFT) | VGPU98_OGL_TEX_DEFINE | VGPU98_OGL_TEX_TEXELS)) &&
              (k->p[5] == VGPU98_OGL_BASE_LUMINANCE) && (k->p[6] == rgba8(128, 128, 128, 255)),
          "a luminance level, red scaled: %x", k ? k->p[6] : 0);
    ogl_GetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_LUMINANCE_SIZE, &iv);
    CHECK(iv == 8, "GL_TEXTURE_LUMINANCE_SIZE");
    /* A part replaced */
    m.n_pkts = 0;
    ogl_TexSubImage2D(GL_TEXTURE_2D, 0, 1, 1, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, img);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_TEXIMAGE);
    CHECK(k && (k->p[2] == 2u) && (k->p[3] == (2u | (2u << 16))) && (k->p[4] == (1u | (1u << 16))) &&
              (k->len == VGPU98_OGL_LEN_TEXIMAGE + 4) && (k->p[6] == rgba8(0, 4, 8, 12)),
          "TEXIMAGE's rectangle: %x %x", k ? k->p[2] : 0, k ? k->p[4] : 0);
    /* The errors */
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 3, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    CHECK(error_now() == GL_INVALID_VALUE, "a width of 3");
    ogl_TexImage2D(GL_TEXTURE_2D, 0, 5, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    CHECK(error_now() == GL_INVALID_VALUE, "an internal format of 5");
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, img);
    CHECK(error_now() == GL_INVALID_ENUM, "a depth image for a texture");
    ogl_TexSubImage2D(GL_TEXTURE_2D, 5, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, img);
    CHECK(error_now() == GL_INVALID_OPERATION, "glTexSubImage of an undefined level");
    ogl_TexSubImage2D(GL_TEXTURE_2D, 0, 3, 3, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, img);
    CHECK(error_now() == GL_INVALID_VALUE, "glTexSubImage past the level");
    /* Proxies: answered, never sent, no error */
    m.n_pkts = 0;
    ogl_TexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_RGBA, 4096, 4096, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    ogl_GetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &iv);
    CHECK(iv == 0 && error_now() == GL_NO_ERROR, "a proxy too large: %d", iv);
    ogl_TexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_RGB, 256, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    ogl_GetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &iv);
    ogl_Flush();
    CHECK(iv == 128 && count_pkts(VGPU98_OGL_TEXIMAGE) == 0, "a proxy that fits: %d", iv);
    /* A level larger than a packet: its size, then rows */
    m.n_pkts = 0;
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, big);
    ogl_Flush();
    k = nth_pkt(VGPU98_OGL_TEXIMAGE, 0);
    CHECK(k && (k->p[2] == (2u | VGPU98_OGL_TEX_DEFINE)) && (k->len == VGPU98_OGL_LEN_TEXIMAGE), "the large level's size first");
    for (i = 1, n = 0; (k = nth_pkt(VGPU98_OGL_TEXIMAGE, i)) != NULL; i++)
        n += (int) (k->p[3] >> 16);
    CHECK(n == 256 && i > 2, "the large level's rows: %d in %d packets", n, i - 1);
    /* A border: GL's -1 is the level's 0 */
    m.n_pkts = 0;
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 6, 6, 1, GL_RGBA, GL_UNSIGNED_BYTE, big);
    ogl_TexSubImage2D(GL_TEXTURE_2D, 0, -1, -1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, img);
    ogl_Flush();
    CHECK((nth_pkt(VGPU98_OGL_TEXIMAGE, 0)->p[2] & VGPU98_OGL_TEX_BORDER) && (last_pkt(VGPU98_OGL_TEXIMAGE)->p[4] == 0),
          "a border's texel at the level's corner");
    /* 1D: one row high, its border at the ends; complete with GL_LINEAR */
    ogl_Disable(GL_TEXTURE_2D);
    ogl_Enable(GL_TEXTURE_1D);
    ogl_BindTexture(GL_TEXTURE_1D, tex[1]);
    ogl_TexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m.n_pkts = 0;
    ogl_TexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 10, 1, GL_RGBA, GL_UNSIGNED_BYTE, big);
    ogl_GetTexLevelParameteriv(GL_TEXTURE_1D, 0, GL_TEXTURE_HEIGHT, &iv);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_TEXIMAGE);
    CHECK(k && ((k->p[2] & 15u) == 1u) && (k->p[3] == (10u | (1u << 16))) && iv == 1, "a 1D level: %x, height %d", k ? k->p[3] : 0, iv);
    CHECK(reg_sent(VGPU98_OGL_R_TEXTURE_ID, 0) == k->p[1] && (reg_sent(VGPU98_OGL_R_ENABLES, 0) & VGPU98_OGL_EN_TEXTURE_1D),
          "the 1D texture drawn with");
    /* Compiled: unpacked then, sent when run */
    list = ogl_GenLists(1);
    ogl_NewList(list, GL_COMPILE);
    ogl_TexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    ogl_EndList();
    m.n_pkts = 0;
    ogl_CallList(list);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_TEXIMAGE);
    CHECK(k && (k->p[3] == (2u | (1u << 16))) && (k->p[6] == rgba8(0, 4, 8, 12)), "a compiled glTexImage1D run");
}

/* glDrawPixels, glBitmap, glCopyPixels: the PIXELS sent. */
static void
scenario_pixels(void)
{
    static GLubyte big[300 * 300 * 4];
    const GLubyte  px[16] = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 0 };
    const GLubyte  bm[8]  = { 0x80, 0x40, 0, 0, 0xff, 0xc0, 0, 0 }; /* 10 by 2, rows of 4 bytes (alignment 4) */
    GLfloat        fb[64];
    const pkt_t   *k;
    GLuint         list;
    int            i, n;

    machine(16);
    current_context(TEST_DC);
    ogl_Flush();
    ogl_RasterPos2f(0.0f, 0.0f);
    m.n_pkts = 0;
    ogl_DrawPixels(2, 2, GL_RGBA, GL_UNSIGNED_BYTE, px);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_PIXELS);
    CHECK(k && (k->len == VGPU98_OGL_LEN_PIXELS + 4) && (k->p[1] == VGPU98_OGL_PIXELS_COLOR) && (k->p[2] == (2u | (2u << 16))),
          "PIXELS' image: %u %u %x", k ? k->len : 0, k ? k->p[1] : 0, k ? k->p[2] : 0);
    CHECK(k && near_f(fl(k->p[3]), WX(0)) && near_f(fl(k->p[4]), WY(0)) && near_f(fl(k->p[5]), 0.5f) && near_f(fl(k->p[6]), 1.0f) &&
              near_f(fl(k->p[7]), 1.0f),
          "PIXELS' place and zoom");
    CHECK(k && (k->p[17] == rgba8(255, 0, 0, 255)) && (k->p[20] == rgba8(255, 255, 255, 0)), "PIXELS' colours: %x", k ? k->p[17] : 0);
    ogl_PixelZoom(2.0f, -1.0f);
    m.n_pkts = 0;
    ogl_DrawPixels(2, 2, GL_RGBA, GL_UNSIGNED_BYTE, px);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_PIXELS);
    CHECK(k && near_f(fl(k->p[6]), 2.0f) && near_f(fl(k->p[7]), -1.0f), "PIXELS' zoom");
    ogl_PixelZoom(1.0f, 1.0f);
    /* A bitmap: its fragments' bytes, its origin taken off, the raster
       position moved */
    m.n_pkts = 0;
    ogl_Bitmap(10, 2, 1.0f, 0.0f, 12.0f, 0.0f, bm);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_PIXELS);
    CHECK(k && (k->p[1] == VGPU98_OGL_PIXELS_BITMAP) && (k->p[2] == (10u | (2u << 16))) && (k->len == VGPU98_OGL_LEN_PIXELS + 6) &&
              near_f(fl(k->p[3]), WX(0) - 1.0f),
          "the bitmap's PIXELS: %x %u", k ? k->p[2] : 0, k ? k->len : 0);
    /* Row 0: pixels 0 and 9; row 1: all ten (three dwords a row) */
    CHECK(k && (k->p[17] == 0x00000001u) && (k->p[18] == 0x00000000u) && (k->p[19] == 0x00000100u) && (k->p[20] == 0x01010101u) &&
              (k->p[21] == 0x01010101u) && (k->p[22] == 0x00000101u),
          "the bitmap's bytes: %x %x %x", k ? k->p[17] : 0, k ? k->p[19] : 0, k ? k->p[22] : 0);
    CHECK(near_f(getf(GL_CURRENT_RASTER_POSITION, 0), WX(0) + 12.0f), "the raster position after the bitmap");
    /* An invalid raster position: nothing drawn, the bitmap not moving it */
    ogl_RasterPos2f(5.0f, 0.0f);
    m.n_pkts = 0;
    ogl_DrawPixels(2, 2, GL_RGBA, GL_UNSIGNED_BYTE, px);
    ogl_Bitmap(10, 2, 0.0f, 0.0f, 12.0f, 0.0f, bm);
    ogl_Flush();
    CHECK(count_pkts(VGPU98_OGL_PIXELS) == 0, "pixels at an invalid raster position");
    /* The render modes' tokens */
    ogl_FeedbackBuffer(64, GL_2D, fb);
    ogl_RenderMode(GL_FEEDBACK);
    ogl_RasterPos2f(0.0f, 0.0f);
    ogl_DrawPixels(2, 2, GL_RGBA, GL_UNSIGNED_BYTE, px);
    ogl_Bitmap(10, 2, 0.0f, 0.0f, 0.0f, 0.0f, bm);
    ogl_CopyPixels(0, 0, 2, 2, GL_COLOR);
    n = ogl_RenderMode(GL_RENDER);
    CHECK(n == 9 && fb[0] == GL_DRAW_PIXEL_TOKEN && fb[3] == GL_BITMAP_TOKEN && fb[6] == GL_COPY_PIXEL_TOKEN, "the pixel tokens: %d", n);
    /* The errors */
    ogl_DrawPixels(2, 2, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, px);
    CHECK(error_now() == GL_INVALID_OPERATION, "a stencil image without a stencil buffer");
    ogl_DrawPixels(2, 2, GL_RGBA, GL_BITMAP, px);
    CHECK(error_now() == GL_INVALID_ENUM, "a colour image of GL_BITMAP");
    ogl_CopyPixels(0, 0, 2, 2, GL_RGBA);
    CHECK(error_now() == GL_INVALID_ENUM, "glCopyPixels of GL_RGBA");
    /* A large image: several packets, every pixel sent */
    m.n_pkts = 0;
    ogl_DrawPixels(300, 300, GL_RGBA, GL_UNSIGNED_BYTE, big);
    ogl_Flush();
    for (i = n = 0; i < m.n_pkts; i++) {
        if (m.pkts[i].op == VGPU98_OGL_PIXELS) {
            CHECK(m.pkts[i].len + 1 <= VGPU98_OGL_PACKET_BYTES / 4, "a PIXELS packet too long");
            n += (int) ((m.pkts[i].p[2] & 0xffff) * (m.pkts[i].p[2] >> 16));
        }
    }
    CHECK(n == 90000 && count_pkts(VGPU98_OGL_PIXELS) > 1, "the large image's pixels: %d", n);
    /* glCopyPixels: read, then drawn */
    m.n_pkts = 0;
    ogl_CopyPixels(1, 1, 2, 2, GL_COLOR);
    ogl_Flush();
    CHECK(count_pkts(VGPU98_OGL_READ) == 1 && last_pkt(VGPU98_OGL_PIXELS) && (last_pkt(VGPU98_OGL_PIXELS)->p[2] == (2u | (2u << 16))),
          "glCopyPixels' READ and PIXELS");
    /* Compiled */
    list = ogl_GenLists(1);
    ogl_NewList(list, GL_COMPILE);
    ogl_DrawPixels(2, 2, GL_RGBA, GL_UNSIGNED_BYTE, px);
    ogl_EndList();
    m.n_pkts = 0;
    ogl_CallList(list);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_PIXELS);
    CHECK(k && (k->p[17] == rgba8(255, 0, 0, 255)), "a compiled glDrawPixels run");
}

/* glReadPixels and glGetTexImage: READ and TEXREAD, converted and packed. */
static void
scenario_readpixels(void)
{
    GLubyte      out[64];
    GLfloat      fv[8];
    GLuint       tex;
    const pkt_t *k;
    const GLubyte img[16] = { 9, 9, 9, 9 };

    machine(16);
    current_context(TEST_DC);
    ogl_Flush();
    m.n_pkts = 0;
    memset(out, 0xee, sizeof(out));
    ogl_ReadPixels(3, 5, 4, 2, GL_RGBA, GL_UNSIGNED_BYTE, out);
    k = last_pkt(VGPU98_OGL_READ);
    CHECK(k && (k->p[1] == VGPU98_OGL_READ_BACK) && (k->p[2] == (3u | (5u << 16))) && (k->p[3] == (4u | (2u << 16))) &&
              (k->p[4] == TEST_STAGING_AT) && (k->p[5] == OGL_STAGING_STRIDE),
          "READ's rectangle: %x %x %x", k ? k->p[1] : 0, k ? k->p[2] : 0, k ? k->p[4] : 0);
    /* The pattern's 565: red the column (of 31), green the row (of 63) */
    CHECK(out[4] == 8 && out[16 + 1] == 4 && out[2] == 255 && out[3] == 255, "the pixels read: %u %u %u %u", out[4], out[17], out[2],
          out[3]);
    CHECK(out[32] == 0xee, "past the image");
    CHECK(surfaces == 1, "the staging surface made %d times", surfaces);
    /* Luminance is red, green and blue's sum */
    ogl_ReadPixels(0, 0, 1, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, out);
    CHECK(out[0] == 255, "luminance read: %u", out[0]);
    /* Depth, by the format's bits */
    ogl_ReadPixels(0, 0, 2, 1, GL_DEPTH_COMPONENT, GL_FLOAT, fv);
    CHECK(near_f(fv[1], (float) 0x8001 / 65535.0f), "depth read: %f", fv[1]);
    /* The errors */
    ogl_ReadPixels(0, 0, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, out);
    CHECK(error_now() == GL_INVALID_OPERATION, "stencil read without a stencil buffer");
    ogl_ReadPixels(0, 0, 1, 1, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, out);
    CHECK(error_now() == GL_INVALID_OPERATION, "colour indices read in RGBA");
    ogl_ReadPixels(0, 0, -1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
    CHECK(error_now() == GL_INVALID_VALUE, "a negative width read");
    /* glGetTexImage: the level's texels, through TEXREAD */
    ogl_GenTextures(1, &tex);
    ogl_BindTexture(GL_TEXTURE_2D, tex);
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    m.texread_w     = 2;
    m.texread_value = rgba8(0x10, 0x20, 0x40, 0x80);
    m.n_pkts        = 0;
    ogl_GetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);
    k = last_pkt(VGPU98_OGL_TEXREAD);
    CHECK(k && (k->p[2] == 0) && (k->p[5] == (0u | (2u << 16))), "TEXREAD's rows: %x", k ? k->p[5] : 0);
    CHECK(out[0] == 0x10 && out[1] == 0x20 && out[2] == 0x40 && out[3] == 0x80 && out[12 + 3] == 0x80, "the texels read");
    /* A luminance level gives its luminance as red */
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    m.texread_value = rgba8(0x40, 0x40, 0x40, 0xff);
    ogl_GetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);
    CHECK(out[0] == 0x40 && out[1] == 0 && out[2] == 0 && out[3] == 255, "a luminance level read: %u %u", out[0], out[1]);
}

/* --- Windows integration ------------------------------------------------------------------------- */

/* wglShareLists and wglCopyContext. */
static void
scenario_share(void)
{
    DHGLRC a, b, c3;
    GLuint list;

    machine(16);
    a = current_context(TEST_DC);
    list = ogl_GenLists(1);
    ogl_NewList(list, GL_COMPILE);
    ogl_LineWidth(3.0f);
    ogl_EndList();
    ogl_LineWidth(5.0f);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    b = DrvCreateContext(TEST_DC);
    CHECK(b && DrvShareLists(a, b), "wglShareLists");
    /* The second context, made current, runs the first's list */
    CHECK(DrvSetContext(TEST_DC, b, NULL) == &ogl_table, "the sharing context made current");
    CHECK(ogl_IsList(list), "the shared list not seen");
    ogl_CallList(list);
    CHECK(near_f(getf(GL_LINE_WIDTH, 0), 3.0f), "the shared list run: %f", getf(GL_LINE_WIDTH, 0));
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    CHECK(last_pkt(VGPU98_OGL_CONTEXT) && (last_pkt(VGPU98_OGL_CONTEXT)->p[2] == nth_pkt(VGPU98_OGL_GROUP, 0)->p[0]),
          "the second context opened in the first's group");
    /* A context with lists of its own does not share */
    c3 = DrvCreateContext(TEST_DC);
    CHECK(DrvSetContext(TEST_DC, c3, NULL) == &ogl_table, "a third context");
    ogl_NewList(ogl_GenLists(1), GL_COMPILE);
    ogl_EndList();
    CHECK(!DrvShareLists(a, c3), "a context with a list of its own shared");
    /* wglCopyContext: the mask's groups, into a context no thread has */
    CHECK(DrvReleaseContext(c3), "the third released");
    CHECK(DrvCopyContext(a, c3, GL_LINE_BIT), "wglCopyContext");
    CHECK(DrvSetContext(TEST_DC, c3, NULL) == &ogl_table, "the third made current again");
    CHECK(near_f(getf(GL_LINE_WIDTH, 0), 5.0f), "the line width copied: %f", getf(GL_LINE_WIDTH, 0));
    CHECK(!DrvCopyContext(a, c3, GL_LINE_BIT), "wglCopyContext into a current context");
}

/* The front buffer: loaded before it is drawn into (single-buffered
   formats, glDrawBuffer(GL_FRONT)), not after a swap; swaps at the size
   drawn; the cursor kept out of the window while the GPU writes it. */
static void
scenario_front(void)
{
    const pkt_t *k;
    DHGLRC       rc;
    int          i, first_load, first_draw;
    GLuint       t[3];

    machine(16);
    /* Format 3: 565, D16, single-buffered */
    CHECK(DrvSetPixelFormat(TEST_DC, 3), "a single-buffered format");
    rc = DrvCreateContext(TEST_DC);
    CHECK(rc && (DrvSetContext(TEST_DC, rc, NULL) == &ogl_table), "its context");
    m.n_pkts = 0;
    ogl_Begin(GL_POINTS);
    ogl_Vertex2f(0.0f, 0.0f);
    ogl_End();
    ogl_Flush();
    first_load = first_draw = -1;
    for (i = 0; i < m.n_pkts; i++) {
        if ((m.pkts[i].op == VGPU98_OGL_SWAP) && (m.pkts[i].p[1] == VGPU98_OGL_SWAP_LOAD) && (first_load < 0))
            first_load = i;
        if ((m.pkts[i].op == VGPU98_OGL_DRAW) && (first_draw < 0))
            first_draw = i;
    }
    CHECK(first_load >= 0 && first_draw > first_load, "the front loaded before the draw: %d %d", first_load, first_draw);
    k = last_pkt(VGPU98_OGL_SWAP);
    CHECK(k && (k->p[1] == VGPU98_OGL_SWAP_SHOW), "glFlush shows the front");
    /* The stand-in driver's cursor is over the window: kept out for the
       LOAD and for the SHOW */
    CHECK(m.access_begins == 2 && m.access_ends == 2 && m.access_rect.left == 100 && m.access_rect.top == 50 &&
              m.access_rect.right == 739 && m.access_rect.bottom == 529,
          "the cursor kept out of the window: %d %d (%u,%u)-(%u,%u)", m.access_begins, m.access_ends, m.access_rect.left,
          m.access_rect.top, m.access_rect.right, m.access_rect.bottom);
    /* Drawn again: no second load */
    m.n_pkts = 0;
    ogl_Begin(GL_POINTS);
    ogl_Vertex2f(0.0f, 0.0f);
    ogl_End();
    ogl_Flush();
    CHECK(count_pkts(VGPU98_OGL_SWAP) == 1 && last_pkt(VGPU98_OGL_SWAP)->p[1] == VGPU98_OGL_SWAP_SHOW, "a second load");
    DrvReleaseContext(rc);
    DrvDeleteContext(rc);

    /* A double-buffered window */
    rc = current_context(TEST_DC2);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    m.n_pkts = 0;
    ogl_DrawBuffer(GL_FRONT);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    CHECK(nth_pkt(VGPU98_OGL_SWAP, 0) && (nth_pkt(VGPU98_OGL_SWAP, 0)->p[1] == VGPU98_OGL_SWAP_LOAD),
          "glDrawBuffer(GL_FRONT) loads the front first");
    ogl_DrawBuffer(GL_BACK);
    /* A swap at the size drawn, the new size taken at the next glClear */
    m.client_w[1] = 400;
    m.n_pkts      = 0;
    CHECK(DrvSwapBuffers(TEST_DC2), "the swap");
    CHECK(count_pkts(VGPU98_OGL_DRAWABLE) == 0, "a resize before the swap");
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    CHECK(last_pkt(VGPU98_OGL_DRAWABLE) && (last_pkt(VGPU98_OGL_DRAWABLE)->p[2] == (400u | (200u << 16))), "the resize at glClear");
    /* After a swap the front image is the frame: drawing into it loads
       nothing */
    DrvSwapBuffers(TEST_DC2);
    m.n_pkts = 0;
    ogl_DrawBuffer(GL_FRONT);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    for (i = 0; i < m.n_pkts; i++)
        CHECK(!((m.pkts[i].op == VGPU98_OGL_SWAP) && (m.pkts[i].p[1] & VGPU98_OGL_SWAP_LOAD)), "a load after a swap");
    ogl_DrawBuffer(GL_BACK);
    /* The group's texture IDs: the lowest free, given back at a delete */
    ogl_GenTextures(3, t);
    for (i = 0; i < 3; i++) {
        ogl_BindTexture(GL_TEXTURE_2D, t[i]);
        ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, t);
    }
    ogl_Flush();
    CHECK(nth_pkt(VGPU98_OGL_TEXPARAM, 0)->p[1] == 1 && nth_pkt(VGPU98_OGL_TEXPARAM, 2)->p[1] == 3, "the texture IDs");
    ogl_DeleteTextures(1, &t[1]);
    ogl_GenTextures(1, &t[1]);
    ogl_BindTexture(GL_TEXTURE_2D, t[1]);
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, t);
    ogl_Flush();
    CHECK(last_pkt(VGPU98_OGL_TEXDELETE) && last_pkt(VGPU98_OGL_TEXDELETE)->p[1] == 2 && last_pkt(VGPU98_OGL_TEXPARAM)->p[1] == 2,
          "a deleted texture's ID used again");
}

/* Texture objects: names, binding, parameters, priorities, residency. */
static void
scenario_texobj(void)
{
    GLuint    t[2];
    GLint     iv[4];
    GLboolean res[2];
    const GLclampf pri[2] = { 0.25f, 2.0f };

    machine(16);
    current_context(TEST_DC);
    ogl_GenTextures(2, t);
    CHECK(t[0] && t[1] && (t[0] != t[1]), "glGenTextures");
    CHECK(!ogl_IsTexture(t[0]), "a name not yet bound is a texture");
    ogl_BindTexture(GL_TEXTURE_2D, t[0]);
    CHECK(ogl_IsTexture(t[0]) && geti(GL_TEXTURE_BINDING_2D) == (GLint) t[0], "glBindTexture");
    ogl_BindTexture(GL_TEXTURE_1D, t[0]);
    CHECK(error_now() == GL_INVALID_OPERATION, "a 2D texture bound as 1D");
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    ogl_GetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, iv);
    CHECK(iv[0] == GL_LINEAR, "GL_TEXTURE_MIN_FILTER");
    ogl_GetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, iv);
    CHECK(iv[0] == GL_CLAMP, "GL_TEXTURE_WRAP_S");
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    CHECK(error_now() == GL_INVALID_ENUM, "a mipmap filter for magnification");
    ogl_PrioritizeTextures(2, t, pri);
    ogl_GetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, iv);
    CHECK(iv[0] == 0x20000000 || iv[0] == 0x1fffffff, "GL_TEXTURE_PRIORITY mapped: %x", iv[0]);
    CHECK(ogl_AreTexturesResident(1, t, res) == GL_TRUE, "glAreTexturesResident");
    ogl_GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, iv);
    CHECK(iv[0] == 0, "an undefined level's width");
    ogl_DeleteTextures(2, t);
    CHECK(geti(GL_TEXTURE_BINDING_2D) == 0 && !ogl_IsTexture(t[0]), "glDeleteTextures unbinds");
}

/* --- The extensions ---------------------------------------------- */

/* A machine whose device reads command set `cs` (the card definition's
   2.6 for the extensions; machine() leaves it 0, as GL 1.1's tests had it). */
static void
machine_cs(DWORD bpp, DWORD cs)
{
    machine(bpp);
    m.regs[VGPU98_REG_COMMAND_SET / 4] = cs;
}

typedef BOOL(STDCALL *swap_interval_t)(int interval);
typedef int(STDCALL *get_swap_interval_t)(void);

/* Every name in GL_EXTENSIONS followed by a space, the whole under 1 KB;
   the string holds `name ` once. */
static int
ext_has(const char *list, const char *name)
{
    const size_t n = strlen(name);
    const char  *p = list;
    int          found = 0;

    while ((p = strstr(p, name)) != NULL) {
        if (((p == list) || (p[-1] == ' ')) && (p[n] == ' '))
            found++;
        p += n;
    }
    return found == 1;
}

/* GL_EXTENSIONS as the finished passes make it, exactly: a 2.7 device's,
   and a 2.6 device's. */
#define EXT_STRING                                                                                       \
    "GL_ARB_multitexture GL_EXT_compiled_vertex_array GL_EXT_paletted_texture GL_EXT_point_parameters " \
    "GL_EXT_shared_texture_palette GL_EXT_texture_edge_clamp GL_SGIS_multitexture "                     \
    "GL_SGIS_texture_edge_clamp WGL_EXT_swap_control "
#define EXT_STRING_26                                                                                    \
    "GL_ARB_multitexture GL_EXT_compiled_vertex_array GL_EXT_paletted_texture GL_EXT_point_parameters " \
    "GL_EXT_shared_texture_palette GL_SGIS_multitexture WGL_EXT_swap_control "

/* Every function of each extension (Wine's generated GL header's lists):
   each offered extension must give all of its functions (Quake 3 stops at
   a NULL one), and no other. */
#define MTC_NAMES(n, sfx, T) "glMultiTexCoord" #n #sfx "ARB", "glMultiTexCoord" #n #sfx "vARB",
#define MTC_SGIS_NAMES(n, sfx, T) "glMultiTexCoord" #n #sfx "SGIS", "glMultiTexCoord" #n #sfx "vSGIS",
static const char *const ext_swap[]  = { "wglSwapIntervalEXT", "wglGetSwapIntervalEXT", NULL };
static const char *const ext_cva[]   = { "glLockArraysEXT", "glUnlockArraysEXT", NULL };
static const char *const ext_arb[]   = { "glActiveTextureARB", "glClientActiveTextureARB", OGL_MTC_LIST(MTC_NAMES) NULL };
static const char *const ext_sgis[]  = { "glSelectTextureSGIS", "glSelectTextureCoordSetSGIS", "glMultiTexCoordPointerSGIS",
                                         "glMTexCoord2fSGIS", "glMTexCoord2fvSGIS", OGL_MTC_LIST(MTC_SGIS_NAMES) NULL };
static const char *const ext_point[] = { "glPointParameterfEXT", "glPointParameterfvEXT", NULL };
static const char *const ext_pal[]   = { "glColorTableEXT", "glColorSubTableEXT", "glGetColorTableEXT",
                                         "glGetColorTableParameterivEXT", "glGetColorTableParameterfvEXT", NULL };
static const struct {
    const char        *name;
    const char *const *procs;
} ext_table[] = {
    { "WGL_EXT_swap_control", ext_swap },        { "GL_EXT_compiled_vertex_array", ext_cva },
    { "GL_ARB_multitexture", ext_arb },          { "GL_SGIS_multitexture", ext_sgis },
    { "GL_EXT_point_parameters", ext_point },    { "GL_EXT_paletted_texture", ext_pal },
    { "GL_EXT_shared_texture_palette", NULL }, { "GL_EXT_texture_edge_clamp", NULL }, { "GL_SGIS_texture_edge_clamp", NULL },
};

static void
scenario_ext(void)
{
    const char *s;
    unsigned    i;

    machine_cs(16, VGPU98_COMMAND_SET);
    current_context(TEST_DC);
    s = (const char *) ogl_GetString(GL_EXTENSIONS);
    CHECK(s && !strcmp(s, EXT_STRING), "GL_EXTENSIONS: \"%s\"", s ? s : "(null)");
    CHECK(s && (strlen(s) < 1024), "GL_EXTENSIONS' length");
    for (i = 0; s && (i < ARRAY_SIZE(ext_table)); i++) {
        const int on = ext_has(s, ext_table[i].name);
        int       k;

        for (k = 0; ext_table[i].procs && ext_table[i].procs[k]; k++)
            CHECK(!DrvGetProcAddress(ext_table[i].procs[k]) == !on, "%s %s %s", ext_table[i].name,
                  on ? "offered without" : "not offered, with", ext_table[i].procs[k]);
    }
    CHECK(!DrvGetProcAddress("wglswapintervalext"), "a name matched without its case");
    CHECK(!DrvGetProcAddress("wglSwapIntervalEXTX") && !DrvGetProcAddress("wglSwapInterval"), "a name matched in part");
    CHECK(!DrvGetProcAddress(NULL), "a NULL name");
    CHECK(!DrvGetProcAddress("glBindTexture"), "GL 1.1's own name (opengl32.dll answers those)");
}

/* A device of command set 2.5: GL 1.1 alone, as before the extensions. */
static void
scenario_ext11(void)
{
    const pkt_t *k;
    const char  *s;
    unsigned     i;
    int          j;

    machine_cs(16, 0x00020005u);
    current_context(TEST_DC);
    s = (const char *) ogl_GetString(GL_EXTENSIONS);
    CHECK(s && !*s, "GL_EXTENSIONS on a 2.5 device: \"%s\"", s ? s : "(null)");
    for (i = 0; i < ARRAY_SIZE(ext_table); i++) {
        for (j = 0; ext_table[i].procs && ext_table[i].procs[j]; j++)
            CHECK(!DrvGetProcAddress(ext_table[i].procs[j]), "%s on a 2.5 device", ext_table[i].procs[j]);
    }
    {
        static const GLenum names[] = { GL_ARRAY_ELEMENT_LOCK_COUNT_EXT, GL_ACTIVE_TEXTURE_ARB, GL_MAX_TEXTURE_UNITS_ARB };
        GLint               v = 5;

        for (i = 0; i < ARRAY_SIZE(names); i++) {
            ogl_GetIntegerv(names[i], &v);
            CHECK((ogl_GetError() == GL_INVALID_ENUM) && (v == 5), "query %x on a 2.5 device", names[i]);
        }
    }
    /* No index levels, no shared palette */
    {
        static const GLubyte idx[2] = { 0, 1 };
        GLuint               tex;
        GLint                iv;

        ogl_GenTextures(1, &tex);
        ogl_BindTexture(GL_TEXTURE_2D, tex);
        ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, 2, 1, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, idx);
        CHECK(ogl_GetError() == GL_INVALID_VALUE, "an index level on a 2.5 device");
        ogl_Enable(GL_SHARED_TEXTURE_PALETTE_EXT);
        CHECK(ogl_GetError() == GL_INVALID_ENUM, "the shared palette's enable on a 2.5 device");
        ogl_GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INDEX_SIZE_EXT, &iv);
        CHECK(ogl_GetError() == GL_INVALID_ENUM, "GL_TEXTURE_INDEX_SIZE_EXT on a 2.5 device");
        ogl_GetTexImage(GL_TEXTURE_2D, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, &iv);
        CHECK(ogl_GetError() == GL_INVALID_ENUM, "glGetTexImage of indices on a 2.5 device");
    }
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    DrvSwapBuffers(TEST_DC);
    k = last_pkt(VGPU98_OGL_SWAP);
    CHECK(k && (k->p[1] == (VGPU98_OGL_SWAP_COPY | VGPU98_OGL_SWAP_SHOW)), "SWAP's flags on a 2.5 device: %x", k ? k->p[1] : 0);
    CHECK(count_pkts(VGPU98_OGL_PALETTE) == 0, "a PALETTE to a 2.5 device");
    /* No register past 2.5's is sent. */
    for (j = 0; j < m.n_pkts; j++) {
        if (m.pkts[j].op == VGPU98_OGL_STATE)
            CHECK(m.pkts[j].p[1] + m.pkts[j].len - 2 <= VGPU98_OGL_R_COUNT_25, "a STATE past 2.5's registers: %u + %u",
                  m.pkts[j].p[1], m.pkts[j].len - 2);
    }
}

static DWORD
swap_flags(void)
{
    const pkt_t *k = last_pkt(VGPU98_OGL_SWAP);

    return k ? k->p[1] : 0xffffffffu;
}

/* The first window covers the screen (1024 by 768 at 0, 0). */
static void
full_screen_window(void)
{
    m.client_w[0] = 1024;
    m.client_h[0] = 768;
    m.origin_x[0] = 0;
    m.origin_y[0] = 0;
    m.clip[0].l   = 0;
    m.clip[0].t   = 0;
    m.clip[0].r   = 1024;
    m.clip[0].b   = 768;
}

static void
scenario_swapint(void)
{
    swap_interval_t     set;
    get_swap_interval_t get;
    DHGLRC              rc1, rc2;
    const DWORD         swap = VGPU98_OGL_SWAP_COPY | VGPU98_OGL_SWAP_SHOW;
    /* The full-screen SwapBuffers, which the V-sync overrides may change:
       the card has PROFILE.VSYNC. */
    const DWORD         over = swap | VGPU98_OGL_SWAP_OVERRIDE;

    machine_cs(16, VGPU98_COMMAND_SET);
    full_screen_window();
    m.cursor_far = 1;
    set = (swap_interval_t) DrvGetProcAddress("wglSwapIntervalEXT");
    get = (get_swap_interval_t) DrvGetProcAddress("wglGetSwapIntervalEXT");
    CHECK(set && get, "no swap control");
    if (!set || !get)
        return;
    CHECK(!set(1) && (get() == 0), "an interval with no context current");
    rc1 = current_context(TEST_DC);
    CHECK(get() == 1, "the default interval: %d", get());
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    DrvSwapBuffers(TEST_DC);
    CHECK(swap_flags() == (over | (1u << VGPU98_OGL_SWAP_INTERVAL_SHIFT)), "SWAP's flags by default: %x", swap_flags());
    CHECK(set(0) && (get() == 0), "interval 0");
    DrvSwapBuffers(TEST_DC);
    CHECK(swap_flags() == over, "SWAP's flags with interval 0: %x", swap_flags());
    CHECK(set(7) && (get() == 7), "interval 7");
    DrvSwapBuffers(TEST_DC);
    CHECK(swap_flags() == (over | (VGPU98_OGL_SWAP_INTERVAL_MAX << VGPU98_OGL_SWAP_INTERVAL_SHIFT)),
          "SWAP's flags with interval 7 (the device's 4 at most): %x", swap_flags());
    m.last_error = 0;
    CHECK(!set(-1) && (m.last_error == ERROR_INVALID_DATA) && (get() == 7), "a negative interval: error %u, interval %d",
          m.last_error, get());
    /* A dialog over part of it: what is visible is not the whole screen, so
       it swaps at once */
    m.clip[0].b = 300;
    m.clip[1].l = 0;
    m.clip[1].t = 300;
    m.clip[1].r = 400;
    m.clip[1].b = 768;
    m.n_clip    = 2;
    DrvSwapBuffers(TEST_DC);
    CHECK(swap_flags() == swap, "a swap with a window over part of it waited: %x", swap_flags());
    full_screen_window();
    m.n_clip = 1;
    DrvSwapBuffers(TEST_DC);
    CHECK(swap_flags() == (over | (VGPU98_OGL_SWAP_INTERVAL_MAX << VGPU98_OGL_SWAP_INTERVAL_SHIFT)),
          "the interval after the dialog went: %x", swap_flags());
    /* The cursor near the window: kept out for the SHOW, which then does
       not wait. */
    m.cursor_far = 0;
    DrvSwapBuffers(TEST_DC);
    CHECK((swap_flags() == swap) && (m.access_begins == 1) && (m.access_ends == 1),
          "a swap with the cursor kept out: flags %x, %d %d", swap_flags(), m.access_begins, m.access_ends);
    m.cursor_far = 1;
    /* The interval is the window's: a second window starts at 1, the first
       keeps its own. The second, not covering the screen, swaps at once. */
    CHECK(DrvSetPixelFormat(TEST_DC2, 1), "the second window's pixel format");
    rc2 = DrvCreateContext(TEST_DC2);
    CHECK(rc2 && (DrvSetContext(TEST_DC2, rc2, NULL) == &ogl_table), "the second window's context");
    CHECK(get() == 1, "the second window's interval: %d", get());
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    DrvSwapBuffers(TEST_DC2);
    CHECK(swap_flags() == swap, "a window on the desktop waited: %x", swap_flags());
    CHECK(DrvSetContext(TEST_DC, rc1, NULL) == &ogl_table, "the first context again");
    CHECK(get() == 7, "the first window's interval: %d", get());
    /* Drawing into the front buffer is shown without a wait. */
    ogl_DrawBuffer(GL_FRONT);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    CHECK(swap_flags() == VGPU98_OGL_SWAP_SHOW, "the front buffer's SHOW: %x", swap_flags());
}

/* A single-buffered window: SwapBuffers shows the front buffer, no wait. */
static void
scenario_swapint1(void)
{
    DHGLRC rc;

    machine_cs(16, VGPU98_COMMAND_SET);
    full_screen_window();
    m.cursor_far = 1;
    CHECK(DrvSetPixelFormat(TEST_DC, 3), "the single-buffered format");
    rc = DrvCreateContext(TEST_DC);
    CHECK(rc && (DrvSetContext(TEST_DC, rc, NULL) == &ogl_table), "the context");
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    DrvSwapBuffers(TEST_DC);
    CHECK(swap_flags() == VGPU98_OGL_SWAP_SHOW, "a single-buffered window's SwapBuffers: %x", swap_flags());
}

/* A card without PROFILE.VSYNC (86Box's "V-sync overrides" Off, or an
   older 86Box): no SWAP carries OVERRIDE, which such a card does not take. */
static void
scenario_swapint_no_vsync(void)
{
    machine_cs(16, VGPU98_COMMAND_SET);
    m.regs[VGPU98_REG_PROFILE / 4] &= ~(DWORD) VGPU98_PROFILE_VSYNC;
    full_screen_window();
    m.cursor_far = 1;
    current_context(TEST_DC);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    DrvSwapBuffers(TEST_DC);
    CHECK(swap_flags() == (VGPU98_OGL_SWAP_COPY | VGPU98_OGL_SWAP_SHOW | (1u << VGPU98_OGL_SWAP_INTERVAL_SHIFT)),
          "a full-screen swap without PROFILE.VSYNC: %x", swap_flags());
}

/* The DRAWs' vertices of packets `from` to `to`, one an index in order (16
   floats each); their count. *vertices: the DRAWs' vertices sent, *ctl the
   last one's control. */
static int
draws_resolved(int from, int to, float (*out)[16], int max, int *vertices, DWORD *ctl)
{
    int i, n = 0;

    if (vertices)
        *vertices = 0;
    for (i = from; i < to; i++) {
        const pkt_t *k = &m.pkts[i];
        DWORD        nv, ni, j;

        if (k->op != VGPU98_OGL_DRAW)
            continue;
        nv = k->p[2];
        ni = k->p[3];
        if (vertices)
            *vertices += (int) nv;
        if (ctl)
            *ctl = k->p[1];
        for (j = 0; (j < ni) && (n < max); j++) {
            const DWORD w  = k->p[VGPU98_OGL_LEN_DRAW + nv * VGPU98_OGL_VERTEX_DWORDS + j / 2];
            const DWORD ix = (j & 1) ? (w >> 16) : (w & 0xffff);

            memcpy(out[n++], &k->p[VGPU98_OGL_LEN_DRAW + ix * VGPU98_OGL_VERTEX_DWORDS], VGPU98_OGL_VERTEX_DWORDS * 4);
        }
    }
    return n;
}

static float cva_a[64][16], cva_b[64][16];

/* The same vertices, index by index, from the two runs of packets. */
static int
same_draws(int a, int a_end, int b, int *va, int *vb)
{
    const int na = draws_resolved(a, a_end, cva_a, 64, va, NULL);
    const int nb = draws_resolved(b, m.n_pkts, cva_b, 64, vb, NULL);

    return (na == nb) && na && !memcmp(cva_a, cva_b, (size_t) na * sizeof(cva_a[0]));
}

static void
scenario_cva(void)
{
    static const GLenum modes[] = { GL_POINTS,         GL_LINES,        GL_LINE_STRIP, GL_LINE_LOOP,  GL_TRIANGLES,
                                    GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_QUADS,      GL_QUAD_STRIP, GL_POLYGON };
    static const float   pos[8][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 },
                                       { 0, 0, 1 }, { 1, 0, 1 }, { 1, 1, 1 }, { 0, 1, 1 } };
    static const GLubyte col[8][4] = { { 255, 0, 0, 255 }, { 0, 255, 0, 255 }, { 0, 0, 255, 255 }, { 255, 255, 0, 128 },
                                       { 0, 255, 255, 64 }, { 255, 0, 255, 32 }, { 10, 20, 30, 40 }, { 50, 60, 70, 80 } };
    static const GLubyte col2[8][4] = { { 1, 2, 3, 4 } };
    static const float   tc[8][2]  = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0.5f, 0 }, { 1, 0.5f }, { 0.5f, 1 }, { 0, 0.5f } };
    static const float   nrm[8][3] = { { 0, 0, 1 }, { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, -1, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } };
    static const GLushort seq[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    static const GLushort shared[6] = { 0, 1, 2, 2, 1, 3 };
    static float          posm[8][3], pos2[8][3], big[3000][3];
    const char           *s;
    GLint                 v;
    GLfloat               cur[4];
    unsigned              i;
    int                   a, ae, b, va, vb, k;
    DWORD                 ctl = 0;

    machine_cs(16, VGPU98_COMMAND_SET);
    current_context(TEST_DC);
    s = (const char *) ogl_GetString(GL_EXTENSIONS);
    CHECK(s && ext_has(s, "GL_EXT_compiled_vertex_array"), "GL_EXTENSIONS lacks compiled vertex arrays: \"%s\"", s ? s : "");
    CHECK((DrvGetProcAddress("glLockArraysEXT") == (void *) ogl_LockArraysEXT) &&
              (DrvGetProcAddress("glUnlockArraysEXT") == (void *) ogl_UnlockArraysEXT),
          "the lock's functions");

    /* --- The lock's errors, queries and push and pop --- */
    ogl_LockArraysEXT(-1, 4);
    CHECK(ogl_GetError() == GL_INVALID_VALUE, "a negative first");
    ogl_LockArraysEXT(0, 0);
    CHECK(ogl_GetError() == GL_INVALID_VALUE, "a count of 0");
    ogl_UnlockArraysEXT();
    CHECK(ogl_GetError() == GL_INVALID_OPERATION, "an unlock while not locked");
    ogl_LockArraysEXT(2, 5);
    CHECK(ogl_GetError() == GL_NO_ERROR, "a lock refused");
    ogl_GetIntegerv(GL_ARRAY_ELEMENT_LOCK_FIRST_EXT, &v);
    CHECK(v == 2, "the lock's first: %d", v);
    ogl_GetIntegerv(GL_ARRAY_ELEMENT_LOCK_COUNT_EXT, &v);
    CHECK(v == 5, "the lock's count: %d", v);
    ogl_LockArraysEXT(0, 1);
    CHECK(ogl_GetError() == GL_INVALID_OPERATION, "a lock while locked");
    ogl_Begin(GL_POINTS);
    ogl_UnlockArraysEXT();
    ogl_End();
    CHECK(ogl_GetError() == GL_INVALID_OPERATION, "an unlock between glBegin and glEnd");
    ogl_PushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    ogl_UnlockArraysEXT();
    ogl_GetIntegerv(GL_ARRAY_ELEMENT_LOCK_COUNT_EXT, &v);
    CHECK(v == 0, "the unlock inside the push: %d", v);
    ogl_PopClientAttrib();
    ogl_GetIntegerv(GL_ARRAY_ELEMENT_LOCK_COUNT_EXT, &v);
    CHECK(v == 5, "the pop's lock: %d", v);
    ogl_UnlockArraysEXT();
    CHECK(ogl_GetError() == GL_NO_ERROR, "the last unlock");

    /* --- Every mode: the direct path's DRAWs are glBegin's --- */
    ogl_VertexPointer(3, GL_FLOAT, 0, pos);
    ogl_ColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
    ogl_TexCoordPointer(2, GL_FLOAT, 0, tc);
    ogl_NormalPointer(GL_FLOAT, 0, nrm);
    ogl_EnableClientState(GL_VERTEX_ARRAY);
    ogl_EnableClientState(GL_COLOR_ARRAY);
    ogl_EnableClientState(GL_TEXTURE_COORD_ARRAY);
    ogl_EnableClientState(GL_NORMAL_ARRAY);
    for (i = 0; i < ARRAY_SIZE(modes); i++) {
        a = m.n_pkts;
        ogl_Begin(modes[i]);
        for (k = 0; k < 8; k++)
            ogl_ArrayElement(k);
        ogl_End();
        ogl_Flush();
        ae = b = m.n_pkts;
        ogl_DrawArrays(modes[i], 0, 8);
        ogl_Flush();
        CHECK(same_draws(a, ae, b, &va, &vb) && (va == vb), "mode %u: glDrawArrays' DRAW differs from glBegin's", modes[i]);
        b = m.n_pkts;
        ogl_DrawElements(modes[i], 8, GL_UNSIGNED_SHORT, seq);
        ogl_Flush();
        CHECK(same_draws(a, ae, b, &va, &vb) && (va == vb), "mode %u: glDrawElements' DRAW differs from glBegin's", modes[i]);
    }

    /* --- An element given twice is one vertex --- */
    a = m.n_pkts;
    ogl_Begin(GL_TRIANGLES);
    for (k = 0; k < 6; k++)
        ogl_ArrayElement(shared[k]);
    ogl_End();
    ogl_Flush();
    ae = b = m.n_pkts;
    ogl_DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, shared);
    ogl_Flush();
    CHECK(same_draws(a, ae, b, &va, &vb), "shared elements: the DRAWs differ");
    CHECK((va == 6) && (vb == 4), "shared elements: %d vertices through glBegin, %d directly (6 and 4)", va, vb);
    ogl_GetFloatv(GL_CURRENT_COLOR, cur);
    CHECK(near_f(cur[0], 1.0f) && near_f(cur[1], 1.0f) && near_f(cur[2], 0.0f) && near_f(cur[3], 128.0f / 255.0f),
          "the current colour after the draw (the last element's): %f %f %f %f", cur[0], cur[1], cur[2], cur[3]);

    /* --- Quake 3's lock: positions alone, the colours changed between draws --- */
    memcpy(posm, pos, sizeof(posm));
    memcpy(pos2, pos, sizeof(pos2));
    pos2[0][0] = 7.0f;
    ogl_DisableClientState(GL_COLOR_ARRAY);
    ogl_DisableClientState(GL_TEXTURE_COORD_ARRAY);
    ogl_DisableClientState(GL_NORMAL_ARRAY);
    ogl_VertexPointer(3, GL_FLOAT, 0, posm);
    ogl_LockArraysEXT(0, 8);
    ogl_EnableClientState(GL_COLOR_ARRAY);
    a = m.n_pkts;
    ogl_DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, shared);
    ogl_Flush();
    draws_resolved(a, m.n_pkts, cva_a, 64, NULL, NULL);
    CHECK(near_f(cva_a[0][VGPU98_OGLV_R], col[2][0] / 255.0f) && near_f(cva_a[0][VGPU98_OGLV_X], 1.0f),
          "the first locked draw's vertex");
    /* The locked positions changed in memory (undefined for the program):
       the conversion made at the lock's first read is what is drawn. The
       colours, enabled after the lock, are read at each call. */
    posm[2][0] = 99.0f;
    ogl_ColorPointer(4, GL_UNSIGNED_BYTE, 0, col2);
    a = m.n_pkts;
    ogl_DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, shared);
    ogl_Flush();
    draws_resolved(a, m.n_pkts, cva_a, 64, NULL, NULL);
    CHECK(near_f(cva_a[0][VGPU98_OGLV_X], 1.0f), "the locked position was read again: %f", cva_a[0][VGPU98_OGLV_X]);
    CHECK(near_f(cva_a[1][VGPU98_OGLV_R], 1.0f / 255.0f), "the colour enabled after the lock was not read at the call");
    a = m.n_pkts;
    ogl_Begin(GL_POINTS);
    ogl_ArrayElement(2);
    ogl_End();
    ogl_Flush();
    draws_resolved(a, m.n_pkts, cva_a, 64, NULL, NULL);
    CHECK(near_f(cva_a[0][VGPU98_OGLV_X], 1.0f), "glArrayElement did not read the lock's conversion");
    /* Another array in its place: read as it is. */
    ogl_VertexPointer(3, GL_FLOAT, 0, pos2);
    a = m.n_pkts;
    ogl_DrawArrays(GL_POINTS, 0, 1);
    ogl_Flush();
    draws_resolved(a, m.n_pkts, cva_a, 64, NULL, NULL);
    CHECK(near_f(cva_a[0][VGPU98_OGLV_X], 7.0f), "a new vertex array under the lock was not read");
    ogl_VertexPointer(3, GL_FLOAT, 0, posm);
    ogl_UnlockArraysEXT();
    a = m.n_pkts;
    ogl_DrawArrays(GL_POINTS, 2, 1);
    ogl_Flush();
    draws_resolved(a, m.n_pkts, cva_a, 64, NULL, NULL);
    CHECK(near_f(cva_a[0][VGPU98_OGLV_X], 99.0f), "after the unlock the array was not read as it is");

    /* --- A DRAW that fills: split, a whole triangle in each --- */
    for (k = 0; k < 3000; k++) {
        big[k][0] = (float) k;
        big[k][1] = (float) (k % 7);
    }
    ogl_DisableClientState(GL_COLOR_ARRAY);
    ogl_VertexPointer(3, GL_FLOAT, 0, big);
    a = m.n_pkts;
    ogl_DrawArrays(GL_TRIANGLES, 0, 3000);
    ogl_Flush();
    {
        int draws = 0, idx = 0, ok = 1;

        for (k = a; k < m.n_pkts; k++) {
            if (m.pkts[k].op == VGPU98_OGL_DRAW) {
                draws++;
                idx += (int) m.pkts[k].p[3];
                ok &= (m.pkts[k].p[2] <= 768) && !(m.pkts[k].p[3] % 3);
            }
        }
        CHECK((draws >= 4) && (idx == 3000) && ok, "3000 vertices: %d DRAWs, %d indices", draws, idx);
    }

    /* --- A list compiles the old way; the CPU path takes polygon modes --- */
    ogl_VertexPointer(3, GL_FLOAT, 0, pos);
    ogl_EnableClientState(GL_COLOR_ARRAY);
    ogl_ColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
    ogl_NewList(1, GL_COMPILE);
    a = m.n_pkts;
    ogl_DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, shared);
    ogl_Flush();
    CHECK(!draws_resolved(a, m.n_pkts, cva_a, 64, NULL, NULL),
          "a draw while compiling a list");
    ogl_EndList();
    a = m.n_pkts;
    ogl_CallList(1);
    ogl_Flush();
    ae = b = m.n_pkts;
    ogl_DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, shared);
    ogl_Flush();
    CHECK(same_draws(a, ae, b, &va, &vb) && (va == 6) && (vb == 4), "the list's draw: %d and %d vertices", va, vb);
    ogl_PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    a = m.n_pkts;
    ogl_DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, shared);
    ogl_Flush();
    draws_resolved(a, m.n_pkts, cva_a, 64, NULL, &ctl);
    CHECK(ctl & VGPU98_OGL_DRAW_CLIP, "a polygon mode's draw did not take the ICD's path: %x", ctl);
    ogl_PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

/* Further cases: Quake 3's fog under a lock, degenerate counts,
   the conversions' values, the current values after a direct draw, lists
   compiled and run at once, the ICD's own path, a disabled vertex array. */
static void
scenario_cva2(void)
{
    static const GLenum modes[] = { GL_POINTS,         GL_LINES,        GL_LINE_STRIP, GL_LINE_LOOP,  GL_TRIANGLES,
                                    GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_QUADS,      GL_QUAD_STRIP, GL_POLYGON };
    static const int    counts[] = { 1, 2, 3, 4, 5, 7 };
    static float        pos[8][3];
    static GLubyte      col[8][4];
    static const GLbyte bcol[8][3]  = { { 127, -128, 0 } };
    static const GLshort snrm[8][3] = { { 32767, 0, -32768 } };
    static const float  tc1[8]      = { 0.5f, 0.25f, 0.125f };
    static const GLushort shared[6] = { 0, 1, 2, 2, 1, 3 };
    GLfloat             f[4];
    unsigned            i;
    int                 a, ae, b, va, vb, k, n;
    DWORD               ctl = 0;

    machine_cs(16, VGPU98_COMMAND_SET);
    current_context(TEST_DC);
    for (k = 0; k < 8; k++) {
        pos[k][0] = (float) k;
        pos[k][1] = (float) (k * k);
        col[k][0] = (GLubyte) (10 * k);
        col[k][3] = 255;
    }
    ogl_VertexPointer(3, GL_FLOAT, 0, pos);
    ogl_ColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
    ogl_EnableClientState(GL_VERTEX_ARRAY);
    ogl_EnableClientState(GL_COLOR_ARRAY);

    /* --- Quake 3's fog: a locked colour array rewritten and pointed at again --- */
    ogl_LockArraysEXT(0, 8);
    a = m.n_pkts;
    ogl_DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, shared);
    ogl_Flush();
    for (k = 0; k < 8; k++)
        col[k][0] = 200;
    ogl_ColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
    a = m.n_pkts;
    ogl_DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, shared);
    ogl_Flush();
    draws_resolved(a, m.n_pkts, cva_a, 64, NULL, NULL);
    CHECK(near_f(cva_a[0][VGPU98_OGLV_R], 200.0f / 255.0f), "the fog pass's colour: %f", cva_a[0][VGPU98_OGLV_R] * 255.0f);
    /* The vertex array pointed at again under the lock: converted anew */
    pos[2][0] = 50.0f;
    ogl_VertexPointer(3, GL_FLOAT, 0, pos);
    a = m.n_pkts;
    ogl_DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, shared);
    ogl_Flush();
    draws_resolved(a, m.n_pkts, cva_a, 64, NULL, NULL);
    CHECK(near_f(cva_a[0][VGPU98_OGLV_X], 50.0f), "the vertex array pointed at again: %f", cva_a[0][VGPU98_OGLV_X]);
    /* A draw partly outside the locked range */
    ogl_UnlockArraysEXT();
    ogl_LockArraysEXT(0, 2);
    a = m.n_pkts;
    ogl_Begin(GL_POINTS);
    for (k = 0; k < 4; k++)
        ogl_ArrayElement(k);
    ogl_End();
    ogl_Flush();
    ae = b = m.n_pkts;
    ogl_DrawArrays(GL_POINTS, 0, 4);
    ogl_Flush();
    CHECK(same_draws(a, ae, b, &va, &vb), "a draw partly outside the lock");
    ogl_UnlockArraysEXT();

    /* --- Degenerate counts, both entry points, against glBegin --- */
    for (i = 0; i < ARRAY_SIZE(modes); i++) {
        for (n = 0; n < (int) ARRAY_SIZE(counts); n++) {
            static const GLuint idx[7] = { 6, 0, 6, 1, 2, 6, 3 }; /* element 6 given three times */

            a = m.n_pkts;
            ogl_Begin(modes[i]);
            for (k = 0; k < counts[n]; k++)
                ogl_ArrayElement((GLint) idx[k]);
            ogl_End();
            ogl_Flush();
            ae = b = m.n_pkts;
            ogl_DrawElements(modes[i], counts[n], GL_UNSIGNED_INT, idx);
            ogl_Flush();
            va = draws_resolved(a, ae, cva_a, 64, NULL, NULL);
            vb = draws_resolved(b, m.n_pkts, cva_b, 64, NULL, NULL);
            CHECK((va == vb) && !memcmp(cva_a, cva_b, (size_t) va * sizeof(cva_a[0])), "mode %u, %d elements: %d and %d indices",
                  modes[i], counts[n], va, vb);
        }
    }

    /* --- The conversions' values --- */
    ogl_ColorPointer(3, GL_BYTE, 0, bcol);
    ogl_NormalPointer(GL_SHORT, 0, snrm);
    ogl_TexCoordPointer(1, GL_FLOAT, 0, tc1);
    ogl_EnableClientState(GL_NORMAL_ARRAY);
    ogl_EnableClientState(GL_TEXTURE_COORD_ARRAY);
    a = m.n_pkts;
    ogl_DrawArrays(GL_POINTS, 0, 1);
    ogl_Flush();
    draws_resolved(a, m.n_pkts, cva_a, 64, NULL, NULL);
    CHECK(near_f(cva_a[0][VGPU98_OGLV_R], 1.0f) && near_f(cva_a[0][VGPU98_OGLV_G], -1.0f) &&
              near_f(cva_a[0][VGPU98_OGLV_B], 1.0f / 255.0f) && near_f(cva_a[0][VGPU98_OGLV_A], 1.0f),
          "a byte colour of 3: %f %f %f %f", cva_a[0][VGPU98_OGLV_R], cva_a[0][VGPU98_OGLV_G], cva_a[0][VGPU98_OGLV_B],
          cva_a[0][VGPU98_OGLV_A]);
    CHECK(near_f(cva_a[0][VGPU98_OGLV_NX], 1.0f) && near_f(cva_a[0][VGPU98_OGLV_NY], 1.0f / 65535.0f) &&
              near_f(cva_a[0][VGPU98_OGLV_NZ], -1.0f),
          "a short normal: %f %f %f", cva_a[0][VGPU98_OGLV_NX], cva_a[0][VGPU98_OGLV_NY], cva_a[0][VGPU98_OGLV_NZ]);
    CHECK(near_f(cva_a[0][VGPU98_OGLV_S], 0.5f) && near_f(cva_a[0][VGPU98_OGLV_T], 0.0f) && near_f(cva_a[0][VGPU98_OGLV_Q], 1.0f),
          "a texture coordinate of 1");
    /* --- The current values after the draw: its last element's --- */
    ogl_DrawArrays(GL_POINTS, 2, 1);
    ogl_GetFloatv(GL_CURRENT_TEXTURE_COORDS, f);
    CHECK(near_f(f[0], 0.125f) && near_f(f[1], 0.0f) && near_f(f[3], 1.0f), "the current coordinates: %f", f[0]);
    ogl_GetFloatv(GL_CURRENT_NORMAL, f);
    CHECK(near_f(f[0], 0.0f) && near_f(f[2], 0.0f) && near_f(f[1], 1.0f / 65535.0f), "the current normal");
    ogl_DisableClientState(GL_NORMAL_ARRAY);
    ogl_DisableClientState(GL_TEXTURE_COORD_ARRAY);
    ogl_ColorPointer(4, GL_UNSIGNED_BYTE, 0, col);

    /* --- A list compiled and run at once: through glBegin, and drawn --- */
    ogl_Flush();
    ogl_NewList(4, GL_COMPILE_AND_EXECUTE);
    a = m.n_pkts;
    ogl_DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, shared);
    ogl_Flush();
    ogl_EndList();
    CHECK(draws_resolved(a, m.n_pkts, cva_a, 64, &va, NULL) == 6 && (va == 6), "GL_COMPILE_AND_EXECUTE: %d vertices", va);

    /* --- Stippled strips stay on the ICD's path --- */
    ogl_Enable(GL_LINE_STIPPLE);
    a = m.n_pkts;
    ogl_DrawArrays(GL_LINE_STRIP, 0, 4);
    ogl_Flush();
    draws_resolved(a, m.n_pkts, cva_a, 64, NULL, &ctl);
    CHECK(ctl & VGPU98_OGL_DRAW_CLIP, "a stippled strip's draw: %x", ctl);
    ogl_Disable(GL_LINE_STIPPLE);

    /* --- No vertex array: no vertex, the current values still set --- */
    ogl_DisableClientState(GL_VERTEX_ARRAY);
    a = m.n_pkts;
    ogl_DrawArrays(GL_POINTS, 5, 1);
    ogl_Flush();
    CHECK(!draws_resolved(a, m.n_pkts, cva_a, 64, NULL, NULL), "a draw without the vertex array");
    ogl_GetFloatv(GL_CURRENT_COLOR, f);
    CHECK(near_f(f[0], 200.0f / 255.0f), "the current colour without the vertex array: %f", f[0] * 255.0f);
}

/* --- Point parameters --------------------------------------------------------- */

static void
scenario_pointparam(void)
{
    static const GLfloat q2atten[3] = { 0.01f, 0.0f, 0.01f };
    GLfloat              f[4];
    const pkt_t         *k;
    int                  a;

    machine_cs(16, VGPU98_COMMAND_SET);
    current_context(TEST_DC);
    /* --- The defaults, errors and queries --- */
    ogl_GetFloatv(GL_POINT_SIZE_MIN_EXT, f);
    CHECK(near_f(f[0], 0.0f), "the minimum: %f", f[0]);
    ogl_GetFloatv(GL_POINT_SIZE_MAX_EXT, f);
    CHECK(near_f(f[0], 64.0f), "the maximum: %f", f[0]);
    ogl_GetFloatv(GL_POINT_FADE_THRESHOLD_SIZE_EXT, f);
    CHECK(near_f(f[0], 1.0f), "the threshold: %f", f[0]);
    ogl_GetFloatv(GL_DISTANCE_ATTENUATION_EXT, f);
    CHECK(near_f(f[0], 1.0f) && near_f(f[1], 0.0f) && near_f(f[2], 0.0f), "the attenuation");
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    CHECK(reg_sent(VGPU98_OGL_R_POINT_PARAMS, 9) == 0, "the defaults change sizes");
    ogl_PointParameterfEXT(GL_DISTANCE_ATTENUATION_EXT, 1.0f);
    CHECK(ogl_GetError() == GL_INVALID_ENUM, "the attenuation by the scalar call");
    ogl_PointParameterfEXT(GL_POINT_SIZE, 1.0f);
    CHECK(ogl_GetError() == GL_INVALID_ENUM, "another name");
    ogl_PointParameterfEXT(GL_POINT_SIZE_MIN_EXT, -1.0f);
    CHECK(ogl_GetError() == GL_INVALID_VALUE, "a negative minimum");
    ogl_PointParameterfEXT(GL_POINT_FADE_THRESHOLD_SIZE_EXT, -0.5f);
    CHECK(ogl_GetError() == GL_INVALID_VALUE, "a negative threshold");

    /* --- Quake 2's: size 40, min 2, max 40, attenuation (0.01, 0, 0.01) --- */
    ogl_PointSize(40.0f);
    ogl_PointParameterfEXT(GL_POINT_SIZE_MIN_EXT, 2.0f);
    ogl_PointParameterfEXT(GL_POINT_SIZE_MAX_EXT, 40.0f);
    ogl_PointParameterfvEXT(GL_DISTANCE_ATTENUATION_EXT, q2atten);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    CHECK(reg_sent(VGPU98_OGL_R_POINT_PARAMS, 0) == 1, "the sizes by distance on");
    CHECK(near_f(fl(reg_sent(VGPU98_OGL_R_POINT_MIN, 0)), 2.0f) && near_f(fl(reg_sent(VGPU98_OGL_R_POINT_MAX, 0)), 40.0f) &&
              near_f(fl(reg_sent(VGPU98_OGL_R_POINT_ATTEN, 0)), 0.01f) && near_f(fl(reg_sent(VGPU98_OGL_R_POINT_ATTEN + 2, 0)), 0.01f),
          "the point registers");
    {
        ogl_raster_t r;
        float        fade, s;

        /* ogl_point_size against hand-worked values: at eye distance 10,
           40 * sqrt(1 / (0.01 + 0.01 * 100)) = 39.80; at 100,
           40 * sqrt(1 / 100.01) = 4.0 */
        memset(&r, 0, sizeof(r));
        r.point_size     = 40.0f;
        r.point_min      = 2.0f;
        r.point_max      = 40.0f;
        r.point_fade     = 1.0f;
        r.point_atten[0] = 0.01f;
        r.point_atten[2] = 0.01f;
        s = ogl_point_size(&r, 10.0f, &fade);
        CHECK((s > 39.79f) && (s < 39.81f) && near_f(fade, 1.0f), "the size at 10: %f", s);
        s = ogl_point_size(&r, 100.0f, &fade);
        CHECK((s > 3.99f) && (s < 4.01f), "the size at 100: %f", s);
        s = ogl_point_size(&r, 10000.0f, &fade);
        CHECK(near_f(s, 2.0f) && near_f(fade, 1.0f), "the minimum far away: %f", s);
        r.point_size = 400.0f;
        s            = ogl_point_size(&r, 0.0f, &fade);
        CHECK(near_f(s, 40.0f), "the maximum close up: %f", s);
        /* Below the threshold: its size, alpha faded */
        r.point_size = 1.0f;
        r.point_min  = 0.0f;
        r.point_fade = 4.0f;
        s            = ogl_point_size(&r, 10.0f, &fade);
        CHECK(near_f(s, 4.0f) && (fade > 0.0f) && (fade < 1.0f), "the fade: %f %f", s, fade);
    }
    /* The ICD's own path inside the view volume: a point's size and fade */
    {
        ogl_PointSize(10.0f);
        ogl_PointParameterfEXT(GL_POINT_SIZE_MIN_EXT, 0.0f);
        ogl_PointParameterfEXT(GL_POINT_FADE_THRESHOLD_SIZE_EXT, 8.0f);
        f[0] = 1.0f;
        f[1] = 0.0f;
        f[2] = 3.0f; /* d 0.5 (z -0.5): 10 * sqrt(1 / (1 + 0.75)) = 7.56, faded at 8 */
        ogl_PointParameterfvEXT(GL_DISTANCE_ATTENUATION_EXT, f);
        ogl_PolygonMode(GL_FRONT_AND_BACK, GL_POINT);
        ogl_Color4f(1.0f, 1.0f, 1.0f, 1.0f);
        ogl_Begin(GL_TRIANGLES);
        ogl_Vertex3f(0.0f, 0.0f, -0.5f);
        ogl_Vertex3f(0.5f, 0.0f, -0.5f);
        ogl_Vertex3f(0.0f, 0.5f, -0.5f);
        ogl_End();
        ogl_Flush();
        k = last_pkt(VGPU98_OGL_DRAW);
        CHECK(k && (k->p[1] & VGPU98_OGL_DRAW_CLIP) && near_f(fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_STIPPLE]), 8.0f),
              "the ICD's point's size: %f", k ? fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_STIPPLE]) : 0.0f);
        CHECK(k && (fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_A]) < 0.95f) && (fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_A]) > 0.8f),
              "the ICD's point's fade: %f", k ? fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_A]) : 0.0f);
        ogl_PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    /* --- Push and pop with GL_POINT_BIT; lists --- */
    ogl_PushAttrib(GL_POINT_BIT);
    ogl_PointParameterfEXT(GL_POINT_SIZE_MAX_EXT, 5.0f);
    ogl_PopAttrib();
    ogl_GetFloatv(GL_POINT_SIZE_MAX_EXT, f);
    CHECK(near_f(f[0], 40.0f), "the maximum after the pop: %f", f[0]);
    ogl_NewList(7, GL_COMPILE);
    ogl_PointParameterfEXT(GL_POINT_SIZE_MAX_EXT, 6.0f);
    ogl_PointParameterfvEXT(GL_DISTANCE_ATTENUATION_EXT, q2atten);
    ogl_EndList();
    ogl_GetFloatv(GL_POINT_SIZE_MAX_EXT, f);
    CHECK(near_f(f[0], 40.0f), "compiled, run: %f", f[0]);
    ogl_CallList(7);
    ogl_GetFloatv(GL_POINT_SIZE_MAX_EXT, f);
    CHECK(near_f(f[0], 6.0f), "the list's maximum: %f", f[0]);
    ogl_GetFloatv(GL_DISTANCE_ATTENUATION_EXT, f);
    CHECK(near_f(f[0], 0.01f) && near_f(f[2], 0.01f), "the list's attenuation");
    (void) a;
}

/* --- Multitexture ------------------------------------------------------------ */

static GLint
get_int(GLenum pname)
{
    GLint v = -12345;

    ogl_GetIntegerv(pname, &v);
    return v;
}

/* A complete 2x2 RGBA texture bound to the active unit's 2D target. */
static void
small_texture(GLuint name)
{
    static const GLubyte px[16] = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 };

    ogl_BindTexture(GL_TEXTURE_2D, name);
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
}

static void
scenario_multitex(void)
{
    static const float scale2[16] = { 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1 };
    static const float tc1[3][2]  = { { 0.5f, 0.25f }, { 1, 0 }, { 0, 1 } };
    static const float pos[3][3]  = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 } };
    GLfloat            f[16];
    GLdouble           dv[4];
    const pkt_t       *k;
    int                a, i, runs;
    DWORD              ctl = 0;

    machine_cs(16, VGPU98_COMMAND_SET);
    current_context(TEST_DC);

    /* --- The units and their tokens --- */
    CHECK(get_int(GL_MAX_TEXTURE_UNITS_ARB) == 2, "the units: %d", get_int(GL_MAX_TEXTURE_UNITS_ARB));
    CHECK(get_int(GL_ACTIVE_TEXTURE_ARB) == (GLint) GL_TEXTURE0_ARB, "the first active unit");
    CHECK(get_int(GL_CLIENT_ACTIVE_TEXTURE_ARB) == (GLint) GL_TEXTURE0_ARB, "the first client unit");
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB + 2);
    CHECK(ogl_GetError() == GL_INVALID_ENUM, "a third unit");
    ogl_SelectTextureSGIS(GL_TEXTURE1_SGIS);
    CHECK(get_int(GL_ACTIVE_TEXTURE_ARB) == (GLint) GL_TEXTURE1_ARB, "SGIS's unit 1 is ARB's");
    ogl_SelectTextureSGIS(GL_TEXTURE0_ARB);
    CHECK(ogl_GetError() == GL_INVALID_ENUM, "an ARB token to SGIS");
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);

    /* --- Unit 1's state apart from unit 0's --- */
    small_texture(5);
    ogl_Enable(GL_TEXTURE_2D);
    ogl_TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    ogl_TexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    ogl_MatrixMode(GL_TEXTURE);
    ogl_LoadMatrixf(scale2);
    ogl_MatrixMode(GL_MODELVIEW);
    CHECK(ogl_IsEnabled(GL_TEXTURE_2D) && (get_int(GL_TEXTURE_BINDING_2D) == 5), "unit 1's texture");
    ogl_GetFloatv(GL_TEXTURE_MATRIX, f);
    CHECK(near_f(f[0], 2.0f), "unit 1's matrix: %f", f[0]);
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB);
    CHECK(!ogl_IsEnabled(GL_TEXTURE_2D) && (get_int(GL_TEXTURE_BINDING_2D) == 0), "unit 0 took unit 1's texture");
    ogl_GetFloatv(GL_TEXTURE_MATRIX, f);
    CHECK(near_f(f[0], 1.0f), "unit 0's matrix: %f", f[0]);
    {
        GLint env = 0;

        ogl_GetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &env);
        CHECK(env == GL_MODULATE, "unit 0's environment: %x", env);
        ogl_GetTexGendv(GL_S, GL_TEXTURE_GEN_MODE, dv);
        CHECK(dv[0] == (double) GL_EYE_LINEAR, "unit 0's generation: %f", dv[0]);
    }
    ogl_Clear(GL_COLOR_BUFFER_BIT); /* the registers go */
    ogl_Flush();
    CHECK(reg_sent(VGPU98_OGL_R_UNIT1 + VGPU98_OGL_U_TEXTURE_ID, 0) && !reg_sent(VGPU98_OGL_R_TEXTURE_ID, 9),
          "the units' textures: %u %u", reg_sent(VGPU98_OGL_R_UNIT1 + VGPU98_OGL_U_TEXTURE_ID, 0),
          reg_sent(VGPU98_OGL_R_TEXTURE_ID, 9));
    CHECK(reg_sent(VGPU98_OGL_R_UNIT1 + VGPU98_OGL_U_TEX_ENV_MODE, 9) == VGPU98_OGL_ENV_REPLACE &&
              reg_sent(VGPU98_OGL_R_TEX_ENV_MODE, 9) == VGPU98_OGL_ENV_MODULATE,
          "the units' environments");
    CHECK(near_f(fl(reg_sent(VGPU98_OGL_R_UNIT1 + VGPU98_OGL_U_TEXTURE, 0)), 2.0f) &&
              near_f(fl(reg_sent(VGPU98_OGL_R_TEXTURE, 0)), 1.0f),
          "the units' matrices");
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_Enable(GL_TEXTURE_GEN_S);
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    CHECK(reg_sent(VGPU98_OGL_R_UNIT1 + VGPU98_OGL_U_TEXGEN_MODE, 9) == VGPU98_OGL_TG_OBJECT &&
              reg_sent(VGPU98_OGL_R_TEXGEN_MODE, 9) == VGPU98_OGL_TG_OFF,
          "the units' generation");
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_Disable(GL_TEXTURE_GEN_S);
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB);

    /* --- Current coordinates: each unit's --- */
    ogl_MultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.5f, 0.25f);
    ogl_MTexCoord2fSGIS(GL_TEXTURE0_SGIS, 0.75f, 0.125f);
    ogl_GetFloatv(GL_CURRENT_TEXTURE_COORDS, f);
    CHECK(near_f(f[0], 0.75f) && near_f(f[1], 0.125f) && near_f(f[3], 1.0f), "unit 0's coordinates: %f %f", f[0], f[1]);
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_GetFloatv(GL_CURRENT_TEXTURE_COORDS, f);
    CHECK(near_f(f[0], 0.5f) && near_f(f[1], 0.25f), "unit 1's coordinates: %f %f", f[0], f[1]);
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB);
    ogl_MultiTexCoord4fARB(GL_TEXTURE0_ARB + 5, 0, 0, 0, 1);
    CHECK(ogl_GetError() == GL_INVALID_ENUM, "coordinates of a third unit");
    /* glTexCoord: unit 0, or SGIS's coordinate set */
    ogl_SelectTextureCoordSetSGIS(GL_TEXTURE1_SGIS);
    ogl_TexCoord2f(0.375f, 0.0f);
    ogl_SelectTextureCoordSetSGIS(GL_TEXTURE0_SGIS);
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_GetFloatv(GL_CURRENT_TEXTURE_COORDS, f);
    CHECK(near_f(f[0], 0.375f), "glTexCoord in SGIS's coordinate set 1: %f", f[0]);
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB);

    /* --- A DRAW with unit 1's coordinates (20 floats a vertex) --- */
    a = m.n_pkts;
    ogl_Begin(GL_TRIANGLES);
    for (i = 0; i < 3; i++) {
        ogl_MultiTexCoord2fvARB(GL_TEXTURE1_ARB, tc1[i]);
        ogl_TexCoord2f((float) i, 0.0f);
        ogl_Vertex3fv(pos[i]);
    }
    ogl_End();
    ogl_Flush();
    k = NULL;
    for (i = a; i < m.n_pkts; i++) {
        if (m.pkts[i].op == VGPU98_OGL_DRAW)
            k = &m.pkts[i];
    }
    CHECK(k && (k->p[1] & VGPU98_OGL_DRAW_TEX1) && (k->len == VGPU98_OGL_LEN_DRAW + 3 * VGPU98_OGL_VERTEX1_DWORDS + 2),
          "the DRAW with unit 1: control %x, length %u", k ? k->p[1] : 0, k ? k->len : 0);
    if (k && (k->p[1] & VGPU98_OGL_DRAW_TEX1)) {
        /* Triangles start at the provoking vertex: GL's third first */
        const DWORD *v0 = &k->p[VGPU98_OGL_LEN_DRAW];

        CHECK(near_f(fl(v0[VGPU98_OGLV_S1]), tc1[2][0]) && near_f(fl(v0[VGPU98_OGLV_T1]), tc1[2][1]) &&
                  near_f(fl(v0[VGPU98_OGLV_Q1]), 1.0f) && near_f(fl(v0[VGPU98_OGLV_S]), 2.0f),
              "the first vertex's coordinates");
    }
    /* The direct path: unit 1's array through the client unit */
    ogl_ClientActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_TexCoordPointer(2, GL_FLOAT, 0, tc1);
    ogl_EnableClientState(GL_TEXTURE_COORD_ARRAY);
    CHECK((get_int(GL_TEXTURE_COORD_ARRAY_SIZE) == 2) && ogl_IsEnabled(GL_TEXTURE_COORD_ARRAY), "unit 1's array");
    ogl_ClientActiveTextureARB(GL_TEXTURE0_ARB);
    CHECK((get_int(GL_TEXTURE_COORD_ARRAY_SIZE) == 4) && !ogl_IsEnabled(GL_TEXTURE_COORD_ARRAY), "unit 0's array");
    ogl_VertexPointer(3, GL_FLOAT, 0, pos);
    ogl_EnableClientState(GL_VERTEX_ARRAY);
    a = m.n_pkts;
    ogl_DrawArrays(GL_TRIANGLES, 0, 3);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_DRAW);
    CHECK(k && (k->p[1] & VGPU98_OGL_DRAW_TEX1) && near_f(fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_S1]), tc1[2][0]),
          "the direct path's unit 1");
    ogl_DisableClientState(GL_VERTEX_ARRAY);
    /* Unit 1 off: 16 floats again */
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_Disable(GL_TEXTURE_2D);
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB);
    ogl_Begin(GL_POINTS);
    ogl_Vertex2f(0, 0);
    ogl_End();
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_DRAW);
    CHECK(k && !(k->p[1] & VGPU98_OGL_DRAW_TEX1) && (k->len == VGPU98_OGL_LEN_DRAW + VGPU98_OGL_VERTEX_DWORDS + 1),
          "a DRAW without unit 1: %x", k ? k->p[1] : 0);
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_Enable(GL_TEXTURE_2D);
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB);

    /* --- The ICD's own path: unit 1's coordinates through its matrix --- */
    ogl_PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    ogl_Begin(GL_TRIANGLES);
    for (i = 0; i < 3; i++) {
        ogl_MultiTexCoord2fvARB(GL_TEXTURE1_ARB, tc1[i]);
        ogl_Vertex3fv(pos[i]);
    }
    ogl_End();
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_DRAW);
    CHECK(k && ((k->p[1] & (VGPU98_OGL_DRAW_CLIP | VGPU98_OGL_DRAW_TEX1)) == (VGPU98_OGL_DRAW_CLIP | VGPU98_OGL_DRAW_TEX1)) &&
              near_f(fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_S1]), 2.0f * tc1[0][0]),
          "the ICD's path's unit 1: %x %f", k ? k->p[1] : 0, k ? fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_S1]) : 0.0f);
    ogl_PolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    /* --- Pixels: unit 1's raster coordinates --- */
    {
        static const GLubyte px[4] = { 1, 2, 3, 4 };

        ogl_MultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.25f, 0.0f);
        ogl_RasterPos2f(0.0f, 0.0f);
        ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
        ogl_GetFloatv(GL_CURRENT_RASTER_TEXTURE_COORDS, f);
        CHECK(near_f(f[0], 0.5f), "unit 1's raster coordinates: %f", f[0]);
        ogl_ActiveTextureARB(GL_TEXTURE0_ARB);
        ogl_DrawPixels(1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        ogl_Flush();
        k = last_pkt(VGPU98_OGL_PIXELS);
        CHECK(k && (k->p[1] & VGPU98_OGL_PIXELS_TEX1) && (k->len == VGPU98_OGL_LEN_PIXELS + 4 + 1) && near_f(fl(k->p[17]), 0.5f),
              "PIXELS with unit 1: kind %x, length %u", k ? k->p[1] : 0, k ? k->len : 0);
    }

    /* --- Push and pop --- */
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_PushAttrib(GL_TEXTURE_BIT | GL_ENABLE_BIT | GL_CURRENT_BIT);
    ogl_TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    ogl_Disable(GL_TEXTURE_2D);
    ogl_MultiTexCoord2fARB(GL_TEXTURE1_ARB, 9.0f, 9.0f);
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB);
    ogl_PopAttrib();
    CHECK(get_int(GL_ACTIVE_TEXTURE_ARB) == (GLint) GL_TEXTURE1_ARB, "the active unit after the pop");
    {
        GLint env = 0;

        ogl_GetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &env);
        CHECK((env == GL_REPLACE) && ogl_IsEnabled(GL_TEXTURE_2D), "unit 1 after the pop: %x", env);
        ogl_GetFloatv(GL_CURRENT_TEXTURE_COORDS, f);
        CHECK(near_f(f[0], 0.25f), "unit 1's coordinates after the pop: %f", f[0]);
    }
    ogl_PushClientAttrib(GL_CLIENT_VERTEX_ARRAY_BIT);
    ogl_ClientActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_DisableClientState(GL_TEXTURE_COORD_ARRAY);
    ogl_PopClientAttrib();
    CHECK(get_int(GL_CLIENT_ACTIVE_TEXTURE_ARB) == (GLint) GL_TEXTURE0_ARB, "the client unit after the pop");
    ogl_ClientActiveTextureARB(GL_TEXTURE1_ARB);
    CHECK(ogl_IsEnabled(GL_TEXTURE_COORD_ARRAY), "unit 1's array after the pop");
    ogl_ClientActiveTextureARB(GL_TEXTURE0_ARB);

    /* --- Lists: the active unit and unit coordinates compiled; the client
       unit at once --- */
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB);
    ogl_NewList(3, GL_COMPILE);
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_MultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.0625f, 0.0f);
    ogl_ClientActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_EndList();
    CHECK(get_int(GL_ACTIVE_TEXTURE_ARB) == (GLint) GL_TEXTURE0_ARB, "glActiveTextureARB ran while compiled");
    CHECK(get_int(GL_CLIENT_ACTIVE_TEXTURE_ARB) == (GLint) GL_TEXTURE1_ARB, "glClientActiveTextureARB was compiled");
    ogl_ClientActiveTextureARB(GL_TEXTURE0_ARB);
    ogl_CallList(3);
    CHECK(get_int(GL_ACTIVE_TEXTURE_ARB) == (GLint) GL_TEXTURE1_ARB, "the list's active unit");
    ogl_GetFloatv(GL_CURRENT_TEXTURE_COORDS, f);
    CHECK(near_f(f[0], 0.0625f), "the list's coordinates: %f", f[0]);

    /* --- Register runs: changes far apart go in STATEs of their own --- */
    ogl_TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE); /* unit 1's */
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB);
    ogl_LoadIdentity();
    ogl_Translatef(1, 0, 0); /* the modelview, register 12 */
    a = m.n_pkts;
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    for (runs = 0, i = a; i < m.n_pkts; i++) {
        if (m.pkts[i].op == VGPU98_OGL_STATE) {
            runs++;
            CHECK(m.pkts[i].len - 2 < 64, "a STATE run of %u registers from %u", m.pkts[i].len - 2, m.pkts[i].p[1]);
        }
    }
    CHECK(runs >= 2, "the STATE runs: %d", runs);

    /* --- A texture deleted while bound to unit 1 --- */
    {
        const GLuint name = 5;

        ogl_DeleteTextures(1, &name);
        ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
        CHECK(get_int(GL_TEXTURE_BINDING_2D) == 0, "unit 1's binding after the delete");
    }
    (void) ctl;
}

/* The device's copy of the registers, as the STATE packets taken build it */
static DWORD mirror[VGPU98_OGL_R_COUNT];

static void
mirror_take(void)
{
    int i;

    for (i = 0; i < m.n_pkts; i++) {
        const pkt_t *k = &m.pkts[i];

        if ((k->op == VGPU98_OGL_STATE) && (k->len >= 2)) {
            DWORD j;

            CHECK(k->p[1] + k->len - 2 <= VGPU98_OGL_R_COUNT, "a STATE past the count: %u + %u", k->p[1], k->len - 2);
            for (j = 0; (j + 2 < k->len) && (k->p[1] + j < VGPU98_OGL_R_COUNT); j++)
                mirror[k->p[1] + j] = k->p[2 + j];
        }
    }
    m.n_pkts = 0;
}

/* Multitexture's further cases: the register runs send every change
   (changes scattered over both units, the matrices, a light, fog, the
   viewport, the point and palette registers); glActiveTextureARB refused
   inside glBegin and
   glEnd; a compiled glTexCoord run into SGIS's coordinate set;
   glMultiTexCoordPointerSGIS; GL_TEXTURE_BIT and unit 1's texture. */
static void
scenario_multitex2(void)
{
    ogl_ctx_t *c;
    GLfloat    f[4];
    GLint      v;
    int        round, i, bad;

    machine_cs(16, VGPU98_COMMAND_SET);
    current_context(TEST_DC);
    memset(mirror, 0xff, sizeof(mirror));
    small_texture(7);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    mirror_take();
    c = ogl_current();
    for (round = 0; round < 40; round++) {
        const float x = (float) round;

        ogl_ActiveTextureARB((round & 1) ? GL_TEXTURE1_ARB : GL_TEXTURE0_ARB);
        if (round % 3 == 0) {
            ogl_Enable(GL_TEXTURE_2D);
            ogl_BindTexture(GL_TEXTURE_2D, 7);
        }
        if (round % 5 == 0)
            ogl_Disable(GL_TEXTURE_2D);
        f[0] = x / 64.0f;
        f[1] = 0.5f;
        f[2] = 0.25f;
        f[3] = 1.0f;
        ogl_TexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, f);
        ogl_TexGeni(GL_T, GL_TEXTURE_GEN_MODE, (round & 2) ? GL_OBJECT_LINEAR : GL_EYE_LINEAR);
        f[0] = x;
        f[1] = 1.0f;
        f[2] = 2.0f;
        f[3] = 3.0f;
        ogl_TexGenfv(GL_R, GL_OBJECT_PLANE, f);
        ogl_MatrixMode(GL_TEXTURE);
        ogl_Translatef(x, 0, 0);
        ogl_MatrixMode(GL_MODELVIEW);
        if (round % 4 == 1)
            ogl_Translatef(0, x, 0);
        if (round % 7 == 2) {
            f[0] = x;
            f[1] = 0;
            f[2] = 0;
            f[3] = 1;
            ogl_Lightfv(GL_LIGHT3, GL_POSITION, f);
        }
        if (round % 6 == 3)
            ogl_Fogf(GL_FOG_DENSITY, x / 10.0f);
        if (round % 9 == 4)
            ogl_Viewport(round, 0, 100, 100);
        if (round % 8 == 5)
            ogl_PointParameterfEXT(GL_POINT_SIZE_MIN_EXT, x / 8.0f);
        if (round % 10 == 6)
            ((round & 4) ? ogl_Enable : ogl_Disable)(GL_SHARED_TEXTURE_PALETTE_EXT);
        ogl_Clear(GL_COLOR_BUFFER_BIT);
        ogl_Flush();
        mirror_take();
        for (bad = -1, i = 0; i < (int) VGPU98_OGL_R_COUNT; i++) {
            if ((mirror[i] != c->reg[i]) && (bad < 0))
                bad = i;
        }
        CHECK(bad < 0, "round %d: register %d sent as %08X, the ICD's %08X", round, bad, (bad >= 0) ? mirror[bad] : 0,
              (bad >= 0) ? c->reg[bad] : 0);
    }
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB);

    /* glActiveTextureARB refused inside glBegin and glEnd; glMultiTexCoord allowed */
    ogl_GetError();
    ogl_Begin(GL_POINTS);
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
    v = (GLint) ogl_GetError(); /* inside glBegin: itself an error, the first one kept */
    ogl_MultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.125f, 0.0f);
    ogl_End();
    CHECK(ogl_GetError() == GL_INVALID_OPERATION, "glActiveTextureARB inside glBegin not refused");
    CHECK(get_int(GL_ACTIVE_TEXTURE_ARB) == (GLint) GL_TEXTURE0_ARB, "glActiveTextureARB inside glBegin changed the unit");
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_GetFloatv(GL_CURRENT_TEXTURE_COORDS, f);
    CHECK(near_f(f[0], 0.125f), "glMultiTexCoord inside glBegin: %f", f[0]);
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB);
    (void) v;

    /* glTexCoord compiled, run into SGIS's coordinate set at the call */
    ogl_NewList(9, GL_COMPILE);
    ogl_TexCoord2f(0.3125f, 0.0f);
    ogl_EndList();
    ogl_SelectTextureCoordSetSGIS(GL_TEXTURE1_SGIS);
    ogl_CallList(9);
    ogl_SelectTextureCoordSetSGIS(GL_TEXTURE0_SGIS);
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_GetFloatv(GL_CURRENT_TEXTURE_COORDS, f);
    CHECK(near_f(f[0], 0.3125f), "the list's glTexCoord into coordinate set 1: %f", f[0]);
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB);

    /* glMultiTexCoordPointerSGIS: unit 1's array, the client unit kept */
    {
        static const float tc[4][2] = { { 0, 0 } };

        ogl_MultiTexCoordPointerSGIS(GL_TEXTURE1_SGIS, 2, GL_FLOAT, 8, (GLvoid *) tc);
        CHECK(get_int(GL_CLIENT_ACTIVE_TEXTURE_ARB) == (GLint) GL_TEXTURE0_ARB, "the client unit after the SGIS pointer");
        ogl_ClientActiveTextureARB(GL_TEXTURE1_ARB);
        CHECK(get_int(GL_TEXTURE_COORD_ARRAY_STRIDE) == 8, "unit 1's array stride: %d", get_int(GL_TEXTURE_COORD_ARRAY_STRIDE));
        ogl_ClientActiveTextureARB(GL_TEXTURE0_ARB);
    }

    /* GL_TEXTURE_BIT: unit 1's bound texture's parameters restored */
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_BindTexture(GL_TEXTURE_2D, 7);
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB);
    ogl_PushAttrib(GL_TEXTURE_BIT);
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ogl_PopAttrib();
    CHECK(get_int(GL_ACTIVE_TEXTURE_ARB) == (GLint) GL_TEXTURE0_ARB, "the active unit after the texture pop");
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
    v = 0;
    ogl_GetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &v);
    CHECK(v == GL_NEAREST, "unit 1's texture's filter after the pop: %x", v);
}

/* EXT_paletted_texture and EXT_shared_texture_palette: index
   levels sent as bytes, palettes whole, the queries, glGetTexImage through
   the palette, the errors, display lists and a GPU restart (ext11 has the
   2.5 device's). */
static void
scenario_palette(void)
{
    static const GLubyte idx4[2 * 4]  = { 0, 1, 9, 9, 2, 3, 9, 9 }; /* rows of 2, each 4 bytes apart (GL_UNPACK_ALIGNMENT) */
    static const GLubyte idx8[8 * 2]  = { 0, 1, 2, 3, 4, 5, 6, 7, 7, 6, 5, 4, 3, 2, 1, 0 };
    static const GLubyte table[4 * 4] = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 10, 20, 30, 40 };
    static const GLubyte two[2 * 4]   = { 1, 2, 3, 4, 5, 6, 7, 8 };
    static GLubyte       wide[256 * 4];
    GLubyte              out[256 * 4];
    GLuint               tex, list;
    GLint                iv;
    GLfloat              fv;
    DWORD                dev, ctx;
    const pkt_t         *k;
    int                  i;

    machine_cs(16, VGPU98_COMMAND_SET);
    current_context(TEST_DC);
    ogl_Flush();
    ctx = last_pkt(VGPU98_OGL_CONTEXT)->p[0];
    ogl_GenTextures(1, &tex);
    ogl_BindTexture(GL_TEXTURE_2D, tex);
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    /* --- A palette given before the first image goes with the texture's first packets --- */
    m.n_pkts = 0;
    ogl_ColorTableEXT(GL_TEXTURE_2D, GL_RGB, 4, GL_RGBA, GL_UNSIGNED_BYTE, table);
    ogl_Flush();
    CHECK(error_now() == GL_NO_ERROR, "glColorTableEXT refused");
    CHECK(count_pkts(VGPU98_OGL_PALETTE) == 0, "a palette sent before its texture was on the device");
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, 2, 2, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, idx4);
    ogl_Flush();
    CHECK(error_now() == GL_NO_ERROR, "an index level refused");
    k   = last_pkt(VGPU98_OGL_TEXIMAGE);
    dev = k ? k->p[1] : 0;
    /* Rows shorter than a dword: each padded to one */
    CHECK(k && (k->p[2] == (2u | VGPU98_OGL_TEX_DEFINE | VGPU98_OGL_TEX_TEXELS)) && (k->p[3] == (2u | (2u << 16))) &&
              (k->p[5] == VGPU98_OGL_BASE_INDEX) && (k->len == VGPU98_OGL_LEN_TEXIMAGE + 2),
          "the index level's TEXIMAGE: %x %x %u", k ? k->p[3] : 0, k ? k->p[5] : 0, k ? k->len : 0);
    CHECK(k && ((k->p[6] & 0xffffu) == 0x0100u) && ((k->p[7] & 0xffffu) == 0x0302u), "the index bytes: %x %x",
          k ? k->p[6] : 0, k ? k->p[7] : 0);
    k = last_pkt(VGPU98_OGL_PALETTE);
    CHECK(k && (k->p[0] == ctx) && (k->p[1] == dev) && (k->p[2] == VGPU98_OGL_BASE_RGB) && (k->p[3] == 4) &&
              (k->len == VGPU98_OGL_LEN_PALETTE + 4),
          "the texture's PALETTE: %x %x %u", k ? k->p[1] : 0, k ? k->p[2] : 0, k ? k->p[3] : 0);
    CHECK(k && (k->p[4] == rgba8(255, 0, 0, 255)) && (k->p[7] == rgba8(10, 20, 30, 255)), "RGB entries: %x %x",
          k ? k->p[4] : 0, k ? k->p[7] : 0);
    {
        int pal = -1, tim = -1;

        for (i = 0; i < m.n_pkts; i++) {
            if ((m.pkts[i].op == VGPU98_OGL_PALETTE) && (pal < 0))
                pal = i;
            if ((m.pkts[i].op == VGPU98_OGL_TEXIMAGE) && (tim < 0))
                tim = i;
        }
        CHECK((pal >= 0) && (tim >= 0) && (pal < tim), "the palette before the level: %d %d", pal, tim);
    }
    ogl_GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INDEX_SIZE_EXT, &iv);
    CHECK(iv == 8, "GL_TEXTURE_INDEX_SIZE_EXT: %d", iv);
    ogl_GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &iv);
    CHECK(iv == GL_COLOR_INDEX8_EXT, "the internal format: %x", iv);
    /* Four bytes a dword for a row of eight */
    m.n_pkts = 0;
    ogl_TexImage2D(GL_TEXTURE_2D, 1, GL_COLOR_INDEX4_EXT, 8, 2, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, idx8);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_TEXIMAGE);
    CHECK(k && (k->len == VGPU98_OGL_LEN_TEXIMAGE + 4) && (k->p[6] == 0x03020100u) && (k->p[7] == 0x07060504u) &&
              (k->p[8] == 0x04050607u) && (k->p[9] == 0x00010203u),
          "a row of eight: %u %x %x", k ? k->len : 0, k ? k->p[6] : 0, k ? k->p[8] : 0);
    ogl_GetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_INDEX_SIZE_EXT, &iv);
    CHECK(iv == 8, "4-bit indices kept as 8: %d", iv);

    /* --- Index images: the transfer path, sub-images, what is refused --- */
    m.n_pkts = 0;
    ogl_PixelTransferi(GL_INDEX_OFFSET, 1);
    ogl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 1, 2, 1, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, idx4);
    ogl_PixelTransferi(GL_INDEX_OFFSET, 0);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_TEXIMAGE);
    CHECK(k && (k->p[2] == 2u) && (k->p[3] == (2u | (1u << 16))) && (k->p[4] == (0u | (1u << 16))) &&
              (k->p[5] == VGPU98_OGL_BASE_INDEX) && (k->len == VGPU98_OGL_LEN_TEXIMAGE + 1) && ((k->p[6] & 0xffffu) == 0x0201u),
          "an index sub-image, offset by 1: %x %x", k ? k->p[3] : 0, k ? k->p[6] : 0);
    ogl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, table);
    CHECK(error_now() == GL_INVALID_OPERATION, "colours into an index level");
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, table);
    CHECK(error_now() == GL_INVALID_OPERATION, "an index level from colours");
    ogl_CopyTexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, 0, 0, 2, 2, 0);
    CHECK(error_now() == GL_INVALID_OPERATION, "an index level from the colour buffer");

    /* --- Palette errors and proxies --- */
    ogl_ColorTableEXT(GL_TEXTURE_2D, GL_RGB, 3, GL_RGBA, GL_UNSIGNED_BYTE, table);
    CHECK(error_now() == GL_INVALID_VALUE, "a width not a power of two");
    ogl_ColorTableEXT(GL_TEXTURE_2D, GL_RGB, 512, GL_RGBA, GL_UNSIGNED_BYTE, table);
    CHECK(error_now() == GL_INVALID_VALUE, "512 entries");
    ogl_ColorTableEXT(GL_TEXTURE_2D, GL_COLOR_INDEX8_EXT, 4, GL_RGBA, GL_UNSIGNED_BYTE, table);
    CHECK(error_now() == GL_INVALID_ENUM, "an index internal format for a table");
    ogl_ColorTableEXT(GL_TEXTURE_2D, GL_RGB, 4, GL_DEPTH_COMPONENT, GL_FLOAT, table);
    CHECK(error_now() == GL_INVALID_ENUM, "a table of depths");
    ogl_ColorTableEXT(GL_TEXTURE_ENV, GL_RGB, 4, GL_RGBA, GL_UNSIGNED_BYTE, table);
    CHECK(error_now() == GL_INVALID_ENUM, "a target that names no palette");
    ogl_ColorTableEXT(GL_PROXY_TEXTURE_2D, GL_RGBA, 512, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    CHECK(error_now() == GL_NO_ERROR, "a proxy too wide gives an error");
    ogl_GetColorTableParameterivEXT(GL_PROXY_TEXTURE_2D, GL_COLOR_TABLE_WIDTH_EXT, &iv);
    CHECK(iv == 0, "a proxy too wide: width %d", iv);
    m.n_pkts = 0;
    ogl_ColorTableEXT(GL_PROXY_TEXTURE_2D, GL_LUMINANCE_ALPHA, 128, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    ogl_GetColorTableParameterivEXT(GL_PROXY_TEXTURE_2D, GL_COLOR_TABLE_WIDTH_EXT, &iv);
    CHECK(iv == 128, "a proxy that fits: width %d", iv);
    ogl_GetColorTableParameterivEXT(GL_PROXY_TEXTURE_2D, GL_COLOR_TABLE_LUMINANCE_SIZE_EXT, &iv);
    CHECK(iv == 8, "a proxy's luminance size: %d", iv);
    ogl_Flush();
    CHECK(count_pkts(VGPU98_OGL_PALETTE) == 0, "a proxy sent a palette");

    /* --- Queries --- */
    ogl_GetColorTableParameterivEXT(GL_TEXTURE_2D, GL_COLOR_TABLE_WIDTH_EXT, &iv);
    CHECK(iv == 4, "the width: %d", iv);
    ogl_GetColorTableParameterivEXT(GL_TEXTURE_2D, GL_COLOR_TABLE_FORMAT_EXT, &iv);
    CHECK(iv == GL_RGB, "the format: %x", iv);
    ogl_GetColorTableParameterivEXT(GL_TEXTURE_2D, GL_COLOR_TABLE_RED_SIZE_EXT, &iv);
    CHECK(iv == 8, "the red size: %d", iv);
    ogl_GetColorTableParameterivEXT(GL_TEXTURE_2D, GL_COLOR_TABLE_ALPHA_SIZE_EXT, &iv);
    CHECK(iv == 0, "an RGB table's alpha size: %d", iv);
    ogl_GetColorTableParameterfvEXT(GL_TEXTURE_2D, GL_COLOR_TABLE_GREEN_SIZE_EXT, &fv);
    CHECK(near_f(fv, 8.0f), "the green size as a float: %f", fv);
    ogl_GetColorTableParameterivEXT(GL_TEXTURE_2D, GL_TEXTURE_WIDTH, &iv);
    CHECK(error_now() == GL_INVALID_ENUM, "another name");
    memset(out, 0xcc, sizeof(out));
    ogl_GetColorTableEXT(GL_TEXTURE_2D, GL_RGBA, GL_UNSIGNED_BYTE, out);
    CHECK((out[0] == 255) && (out[1] == 0) && (out[3] == 255) && (out[12] == 10) && (out[13] == 20) && (out[14] == 30) &&
              (out[15] == 255) && (out[16] == 0xcc),
          "the entries read: %u %u %u %u", out[12], out[13], out[14], out[15]);
    ogl_GetColorTableEXT(GL_TEXTURE_2D, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, out);
    CHECK(error_now() == GL_INVALID_ENUM, "a table read as indices");

    /* --- glColorSubTableEXT: the palette goes whole --- */
    m.n_pkts = 0;
    ogl_ColorSubTableEXT(GL_TEXTURE_2D, 1, 2, GL_RGBA, GL_UNSIGNED_BYTE, two);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_PALETTE);
    CHECK(k && (k->p[3] == 4) && (k->p[4] == rgba8(255, 0, 0, 255)) && (k->p[5] == rgba8(1, 2, 3, 255)) &&
              (k->p[6] == rgba8(5, 6, 7, 255)) && (k->p[7] == rgba8(10, 20, 30, 255)),
          "the sub-table: %x %x", k ? k->p[5] : 0, k ? k->p[6] : 0);
    CHECK(count_pkts(VGPU98_OGL_TEXIMAGE) == 0, "a palette change sent texels");
    ogl_ColorSubTableEXT(GL_TEXTURE_2D, 3, 2, GL_RGBA, GL_UNSIGNED_BYTE, two);
    CHECK(error_now() == GL_INVALID_VALUE, "entries past the width");
    ogl_ColorSubTableEXT(GL_PROXY_TEXTURE_2D, 0, 1, GL_RGBA, GL_UNSIGNED_BYTE, two);
    CHECK(error_now() == GL_INVALID_ENUM, "a proxy's sub-table");

    /* --- glGetTexImage through the palette drawn with --- */
    m.texread_w     = 2;
    m.texread_bytes = 1;
    m.texread_value = 1; /* indices 1 and 2 across */
    memset(out, 0, sizeof(out));
    ogl_GetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);
    CHECK(error_now() == GL_NO_ERROR, "glGetTexImage of an index level");
    CHECK((out[0] == 1) && (out[1] == 2) && (out[2] == 3) && (out[3] == 255) && (out[4] == 5) && (out[7] == 255),
          "the level read through its palette: %u %u %u", out[0], out[4], out[7]);

    /* --- The shared palette --- */
    for (i = 0; i < 256; i++) {
        wide[4 * i]     = (GLubyte) i;
        wide[4 * i + 1] = (GLubyte) (255 - i);
        wide[4 * i + 2] = 7;
        wide[4 * i + 3] = (GLubyte) (i / 2);
    }
    m.n_pkts = 0;
    ogl_ColorTableEXT(GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGBA, 256, GL_RGBA, GL_UNSIGNED_BYTE, wide);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_PALETTE);
    CHECK(k && (k->p[0] == ctx) && (k->p[1] == 0) && (k->p[2] == VGPU98_OGL_BASE_RGBA) && (k->p[3] == 256) &&
              (k->len == VGPU98_OGL_LEN_PALETTE + 256) && (k->p[4 + 200] == rgba8(200, 55, 7, 100)),
          "the shared PALETTE: %x %u %x", k ? k->p[1] : 0, k ? k->p[3] : 0, k ? k->p[204] : 0);
    CHECK(!ogl_IsEnabled(GL_SHARED_TEXTURE_PALETTE_EXT), "the shared palette on at first");
    ogl_Enable(GL_SHARED_TEXTURE_PALETTE_EXT);
    CHECK(ogl_IsEnabled(GL_SHARED_TEXTURE_PALETTE_EXT) && (error_now() == GL_NO_ERROR), "the shared palette not on");
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    CHECK(reg_sent(VGPU98_OGL_R_ENABLES, 0) & VGPU98_OGL_EN_SHARED_PALETTE, "EN_SHARED_PALETTE not sent");
    ogl_GetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);
    CHECK((out[0] == 1) && (out[1] == 254) && (out[2] == 7) && (out[3] == 0) && (out[4] == 2) && (out[7] == 1),
          "the level read through the shared palette: %u %u %u %u", out[0], out[1], out[2], out[3]);
    ogl_GetColorTableParameterivEXT(GL_SHARED_TEXTURE_PALETTE_EXT, GL_COLOR_TABLE_WIDTH_EXT, &iv);
    CHECK(iv == 256, "the shared palette's width: %d", iv);
    ogl_PushAttrib(GL_ENABLE_BIT);
    ogl_Disable(GL_SHARED_TEXTURE_PALETTE_EXT);
    ogl_PopAttrib();
    CHECK(ogl_IsEnabled(GL_SHARED_TEXTURE_PALETTE_EXT), "GL_ENABLE_BIT's pop lost the shared palette's enable");
    m.texread_bytes = 0;

    /* --- Display lists: compiled, sent when run --- */
    list     = ogl_GenLists(1);
    m.n_pkts = 0;
    ogl_NewList(list, GL_COMPILE);
    ogl_ColorTableEXT(GL_TEXTURE_2D, GL_LUMINANCE, 2, GL_LUMINANCE, GL_UNSIGNED_BYTE, two);
    ogl_ColorSubTableEXT(GL_TEXTURE_2D, 1, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, &two[4]);
    ogl_EndList();
    ogl_Flush();
    CHECK(count_pkts(VGPU98_OGL_PALETTE) == 0, "a compiled table sent");
    ogl_GetColorTableParameterivEXT(GL_TEXTURE_2D, GL_COLOR_TABLE_WIDTH_EXT, &iv);
    CHECK(iv == 4, "a compiled table changed the palette: %d", iv);
    ogl_CallList(list);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_PALETTE);
    CHECK((count_pkts(VGPU98_OGL_PALETTE) == 2) && k && (k->p[1] == dev) && (k->p[2] == VGPU98_OGL_BASE_LUMINANCE) &&
              (k->p[3] == 2) && (k->p[4] == rgba8(1, 1, 1, 255)) && (k->p[5] == rgba8(5, 5, 5, 255)),
          "the list's palettes: %d %x %x", count_pkts(VGPU98_OGL_PALETTE), k ? k->p[4] : 0, k ? k->p[5] : 0);

    /* --- A GPU restart sends the shared palette again with the registers --- */
    m.restart_at = m.taken + 1;
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    m.n_pkts = 0;
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_PALETTE);
    CHECK(k && (k->p[1] == 0) && (k->p[3] == 256), "the shared palette not sent again after a restart");

}

/* The last TEXIMAGE's texel dwords into out (at most max); their count, or
   -1 for none. */
static int
last_texels(DWORD *out, int max)
{
    const pkt_t *k = last_pkt(VGPU98_OGL_TEXIMAGE);
    int          n, i;

    if (!k)
        return -1;
    n = (int) k->len - VGPU98_OGL_LEN_TEXIMAGE;
    for (i = 0; (i < n) && (i < max); i++)
        out[i] = k->p[VGPU98_OGL_LEN_TEXIMAGE + i];
    return n;
}

/* Point parameters' and palettes' further cases: Quake 2's start-up (the shared palette from RGB
   bytes, a mip chain of index levels); the direct index path against the
   unpacking path; the index offset; a compiled glPointParameterfEXT with
   the vector name; the ICD's own points sized by the eye distance with w
   taken out; glGetTexImage of indices; an index sub-image in a list; index
   and colour levels mixed; two contexts' shared palettes. */
static void
scenario_palette2(void)
{
    static GLubyte rgb[768], img[64 * 64];
    static DWORD   def[2][4096];
    GLubyte        out[64];
    GLuint         tex, tex2, list;
    const pkt_t   *k;
    int            i, na, nb;
    GLfloat        f[4];
    DHGLRC         rc1, rc2;
    DWORD          ctx1, ctx2;

    machine_cs(16, VGPU98_COMMAND_SET);
    rc1 = current_context(TEST_DC);
    for (i = 0; i < 256; i++) {
        rgb[i * 3]     = (GLubyte) i;
        rgb[i * 3 + 1] = (GLubyte) (255 - i);
        rgb[i * 3 + 2] = 7;
    }
    for (i = 0; i < 64 * 64; i++)
        img[i] = (GLubyte) (i * 7 + 3);

    /* --- Quake 2: the shared palette from RGB bytes, on, then index levels --- */
    m.n_pkts = 0;
    ogl_ColorTableEXT(GL_SHARED_TEXTURE_PALETTE_EXT, GL_RGB, 256, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    ogl_Enable(GL_SHARED_TEXTURE_PALETTE_EXT);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_PALETTE);
    CHECK(k && !k->p[1] && (k->p[2] == VGPU98_OGL_BASE_RGB) && (k->p[3] == 256) && (k->p[4 + 5] == rgba8(5, 250, 7, 255)),
          "Quake 2's shared palette: %x", k ? k->p[4 + 5] : 0);
    ctx1 = k ? k->p[0] : 0;
    ogl_GenTextures(1, &tex);
    ogl_BindTexture(GL_TEXTURE_2D, tex);
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    for (i = 0; i <= 3; i++) {
        const int s = 8 >> i;

        ogl_TexImage2D(GL_TEXTURE_2D, i, GL_COLOR_INDEX8_EXT, s, s, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, img);
        CHECK(error_now() == GL_NO_ERROR, "Quake 2's level %d refused", i);
    }
    ogl_Enable(GL_TEXTURE_2D);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    na = last_texels(def[0], 4096); /* the 1x1 */
    CHECK((na == 1) && ((def[0][0] & 0xff) == img[0]), "the 1x1 level: %d %x", na, def[0][0]);
    CHECK(reg_sent(VGPU98_OGL_R_TEXTURE_ID, 0) == last_pkt(VGPU98_OGL_TEXIMAGE)->p[1], "the complete index texture not drawn with");

    /* --- Index and colour levels mixed: incomplete, not drawn with --- */
    ogl_TexImage2D(GL_TEXTURE_2D, 3, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    CHECK(reg_sent(VGPU98_OGL_R_TEXTURE_ID, 1) == 0, "a texture of index and colour levels drawn with");
    ogl_TexImage2D(GL_TEXTURE_2D, 3, GL_COLOR_INDEX8_EXT, 1, 1, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, img);
    ogl_Disable(GL_TEXTURE_2D);

    /* --- The direct index path against the unpacking path, by the unpack state --- */
    ogl_PixelStorei(GL_UNPACK_ROW_LENGTH, 9);
    ogl_PixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
    ogl_PixelStorei(GL_UNPACK_SKIP_ROWS, 1);
    ogl_PixelStorei(GL_UNPACK_ALIGNMENT, 2);
    m.n_pkts = 0;
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, 8, 4, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, img);
    ogl_Flush();
    na = last_texels(def[0], 4096);
    ogl_NewList(77, GL_COMPILE_AND_EXECUTE);
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, 8, 4, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, img);
    ogl_EndList();
    ogl_Flush();
    nb = last_texels(def[1], 4096);
    CHECK((na == 8) && (na == nb) && !memcmp(def[0], def[1], (size_t) na * 4), "direct and unpacked indices differ: %d %d, %08x %08x",
          na, nb, def[0][0], def[1][0]);
    CHECK(error_now() == GL_NO_ERROR, "an error in the comparison");
    {
        /* Row 0 of the image is row 1 of the data (9 a row, aligned to 2:
           10 bytes), from pixel 1 */
        const DWORD want = (DWORD) img[10 + 1] | ((DWORD) img[10 + 2] << 8) | ((DWORD) img[10 + 3] << 16) | ((DWORD) img[10 + 4] << 24);

        CHECK(def[0][0] == want, "the first dword unpacked: %08x, %08x wanted", def[0][0], want);
    }
    /* The index offset, through the transfer path */
    ogl_PixelTransferi(GL_INDEX_OFFSET, 3);
    m.n_pkts = 0;
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, 8, 4, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, img);
    ogl_Flush();
    na = last_texels(def[1], 4096);
    CHECK((na == 8) && ((def[1][0] & 0xff) == ((def[0][0] + 3) & 0xff)), "the index offset: %08x %08x", def[1][0], def[0][0]);
    ogl_PixelTransferi(GL_INDEX_OFFSET, 0);
    ogl_PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    ogl_PixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    ogl_PixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    ogl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);

    /* --- An index sub-image compiled, sent when the list runs --- */
    list     = ogl_GenLists(1);
    m.n_pkts = 0;
    ogl_NewList(list, GL_COMPILE);
    ogl_TexSubImage2D(GL_TEXTURE_2D, 0, 4, 2, 4, 1, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, img);
    ogl_EndList();
    ogl_Flush();
    CHECK(count_pkts(VGPU98_OGL_TEXIMAGE) == 0, "a compiled index sub-image sent");
    ogl_CallList(list);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_TEXIMAGE);
    CHECK(k && (k->p[3] == (4u | (1u << 16))) && (k->p[4] == (4u | (2u << 16))) && (k->p[5] == VGPU98_OGL_BASE_INDEX) &&
              (k->p[6] == ((DWORD) img[0] | ((DWORD) img[1] << 8) | ((DWORD) img[2] << 16) | ((DWORD) img[3] << 24))),
          "the list's index sub-image: %x %x", k ? k->p[4] : 0, k ? k->p[6] : 0);

    /* --- glGetTexImage of the indices themselves --- */
    m.texread_w     = 8;
    m.texread_bytes = 1;
    m.texread_value = 10;
    memset(out, 0, sizeof(out));
    ogl_GetTexImage(GL_TEXTURE_2D, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, out);
    CHECK((error_now() == GL_NO_ERROR) && (out[0] == 10) && (out[7] == 17) && (out[8] == 10) && (out[31] == 17),
          "an index level's indices read: %u %u %u", out[0], out[7], out[8]);
    m.texread_bytes = 0;
    ogl_GenTextures(1, &tex2);
    small_texture(tex2);
    ogl_GetTexImage(GL_TEXTURE_2D, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, out);
    CHECK(error_now() == GL_INVALID_OPERATION, "indices read from a colour level");
    ogl_BindTexture(GL_TEXTURE_2D, tex);

    /* --- glPointParameterfEXT with the vector name, compiled: an error when
       run, as when called at once --- */
    ogl_PointParameterfEXT(GL_DISTANCE_ATTENUATION_EXT, 2.0f);
    CHECK(error_now() == GL_INVALID_ENUM, "the scalar call with the vector name");
    ogl_NewList(78, GL_COMPILE);
    ogl_PointParameterfEXT(GL_DISTANCE_ATTENUATION_EXT, 2.0f);
    ogl_EndList();
    error_now();
    ogl_CallList(78);
    ogl_GetFloatv(GL_DISTANCE_ATTENUATION_EXT, f);
    CHECK(near_f(f[0], 1.0f) && (error_now() == GL_INVALID_ENUM), "the compiled scalar call set the attenuation: %f", f[0]);

    /* --- The ICD's own points: the size by the eye distance, w taken out --- */
    {
        const GLfloat att[3] = { 0.0f, 0.0f, 1.0f };
        const GLfloat one[3] = { 1.0f, 0.0f, 0.0f };

        ogl_PointParameterfvEXT(GL_DISTANCE_ATTENUATION_EXT, att);
        ogl_PointSize(10.0f);
        ogl_PolygonMode(GL_FRONT_AND_BACK, GL_POINT);
        m.n_pkts = 0;
        ogl_Begin(GL_TRIANGLES);
        ogl_Vertex4f(1.2f, 0.0f, 0.0f, 2.0f);
        ogl_Vertex4f(0.0f, 1.2f, 0.0f, 2.0f);
        ogl_Vertex4f(-1.2f, 0.0f, 0.0f, 2.0f);
        ogl_End();
        ogl_Flush();
        k = last_pkt(VGPU98_OGL_DRAW);
        /* d = 0.6: 10 * sqrt(1 / 0.36) */
        CHECK(k && (k->p[1] & VGPU98_OGL_DRAW_CLIP) && near_f(fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_STIPPLE]), 16.6667f),
              "the CPU path's point size: %f", k ? fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_STIPPLE]) : -1.0f);
        ogl_PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        ogl_PointParameterfvEXT(GL_DISTANCE_ATTENUATION_EXT, one);
    }

    /* --- Two contexts, each its own shared palette --- */
    CHECK(DrvSetPixelFormat(TEST_DC2, 1), "the second window's pixel format");
    rc2 = DrvCreateContext(TEST_DC2);
    CHECK(rc2 && (DrvSetContext(TEST_DC2, rc2, NULL) == &ogl_table), "the second window's context");
    ogl_GetColorTableParameterivEXT(GL_SHARED_TEXTURE_PALETTE_EXT, GL_COLOR_TABLE_WIDTH_EXT, (GLint *) &na);
    CHECK(na == 1, "the second context's shared palette at first: width %d", na);
    m.n_pkts = 0;
    ogl_ColorTableEXT(GL_SHARED_TEXTURE_PALETTE_EXT, GL_LUMINANCE, 16, GL_LUMINANCE, GL_UNSIGNED_BYTE, img);
    ogl_Flush();
    k    = last_pkt(VGPU98_OGL_PALETTE);
    ctx2 = k ? k->p[0] : 0;
    CHECK(k && (ctx2 != ctx1) && !k->p[1] && (k->p[2] == VGPU98_OGL_BASE_LUMINANCE) && (k->p[3] == 16),
          "the second context's shared PALETTE: %x %x", ctx2, k ? k->p[3] : 0);
    CHECK(DrvSetContext(TEST_DC, rc1, NULL) == &ogl_table, "the first context again");
    ogl_GetColorTableParameterivEXT(GL_SHARED_TEXTURE_PALETTE_EXT, GL_COLOR_TABLE_WIDTH_EXT, (GLint *) &na);
    CHECK(na == 256, "the first context's shared palette changed: width %d", na);
}

/* The extensions together. Unit 0 RGBA with an index
   texture and its own palette on unit 1, then the shared palette; a locked
   glDrawElements with unit 1's array (DRAW_TEX1), Quake 3's array pointed
   at again; the ICD's sized points with unit 1's coordinates, and in
   feedback. */
static void
scenario_cross(void)
{
    static const GLubyte idx[8]  = { 0, 1, 9, 9, 2, 3, 9, 9 }; /* rows 4 bytes apart (GL_UNPACK_ALIGNMENT) */
    static const GLubyte tab[12] = { 10, 0, 0, 0, 20, 0, 0, 0, 30, 40, 50, 60 };
    static const float   pos[4][3] = { { 0.1f, 0.1f, 0 }, { 0.5f, 0.1f, 0 }, { 0.1f, 0.5f, 0 }, { 0.5f, 0.5f, 0 } };
    static float         tcA[4][2] = { { 0.25f, 0 }, { 0.25f, 0 }, { 0.25f, 0 }, { 0.25f, 0 } };
    static const GLushort tri[3] = { 0, 1, 2 };
    const pkt_t         *k;
    GLuint               tex[2];
    GLfloat              fb[64];
    int                  i, n;

    machine_cs(16, VGPU98_COMMAND_SET);
    current_context(TEST_DC);
    ogl_GenTextures(2, tex);
    /* Unit 0 RGBA, unit 1 an index texture with its own palette */
    small_texture(tex[0]);
    ogl_Enable(GL_TEXTURE_2D);
    ogl_ActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_BindTexture(GL_TEXTURE_2D, tex[1]);
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, 2, 2, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, idx);
    ogl_ColorTableEXT(GL_TEXTURE_2D, GL_RGB, 4, GL_RGB, GL_UNSIGNED_BYTE, tab);
    ogl_Enable(GL_TEXTURE_2D);
    ogl_ActiveTextureARB(GL_TEXTURE0_ARB);
    CHECK(error_now() == GL_NO_ERROR, "setting up the two units");
    m.n_pkts = 0;
    ogl_Begin(GL_TRIANGLES);
    for (i = 0; i < 3; i++) {
        ogl_MultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.5f, 0.5f);
        ogl_Vertex3fv(pos[i]);
    }
    ogl_End();
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_DRAW);
    CHECK(k && (k->p[1] & VGPU98_OGL_DRAW_TEX1), "an index texture on unit 1: no DRAW_TEX1");
    CHECK(reg_sent(VGPU98_OGL_R_UNIT1 + VGPU98_OGL_U_TEXTURE_ID, 0) && reg_sent(VGPU98_OGL_R_TEXTURE_ID, 0),
          "the two units' textures: %u %u", reg_sent(VGPU98_OGL_R_TEXTURE_ID, 0),
          reg_sent(VGPU98_OGL_R_UNIT1 + VGPU98_OGL_U_TEXTURE_ID, 0));
    /* The shared palette on: both units' index textures would take it */
    ogl_Enable(GL_SHARED_TEXTURE_PALETTE_EXT);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Flush();
    CHECK(reg_sent(VGPU98_OGL_R_ENABLES, 0) & VGPU98_OGL_EN_SHARED_PALETTE, "the shared enable");
    ogl_Disable(GL_SHARED_TEXTURE_PALETTE_EXT);

    /* A lock with unit 1's array; Quake 3's two passes, the arrays rewritten */
    ogl_VertexPointer(3, GL_FLOAT, 0, pos);
    ogl_EnableClientState(GL_VERTEX_ARRAY);
    ogl_ClientActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_TexCoordPointer(2, GL_FLOAT, 0, tcA);
    ogl_EnableClientState(GL_TEXTURE_COORD_ARRAY);
    ogl_ClientActiveTextureARB(GL_TEXTURE0_ARB);
    ogl_LockArraysEXT(0, 4);
    m.n_pkts = 0;
    ogl_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, tri);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_DRAW);
    CHECK(k && (k->p[1] & VGPU98_OGL_DRAW_TEX1) && (k->len == VGPU98_OGL_LEN_DRAW + 3 * VGPU98_OGL_VERTEX1_DWORDS + 2) &&
              near_f(fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_S1]), 0.25f),
          "the locked direct draw with unit 1: %x %u", k ? k->p[1] : 0, k ? k->len : 0);
    for (i = 0; i < 4; i++)
        tcA[i][0] = 0.75f;
    ogl_ClientActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_TexCoordPointer(2, GL_FLOAT, 0, tcA);
    ogl_ClientActiveTextureARB(GL_TEXTURE0_ARB);
    m.n_pkts = 0;
    ogl_DrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, tri);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_DRAW);
    CHECK(k && near_f(fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_S1]), 0.75f), "the second pass's unit 1 coordinates: %f",
          k ? fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_S1]) : -1.0f);
    ogl_UnlockArraysEXT();
    ogl_DisableClientState(GL_VERTEX_ARRAY);
    ogl_ClientActiveTextureARB(GL_TEXTURE1_ARB);
    ogl_DisableClientState(GL_TEXTURE_COORD_ARRAY);
    ogl_ClientActiveTextureARB(GL_TEXTURE0_ARB);

    /* The ICD's points: sized, with unit 1's coordinates */
    {
        const GLfloat att[3] = { 0.0f, 0.0f, 1.0f };

        ogl_PointParameterfvEXT(GL_DISTANCE_ATTENUATION_EXT, att);
        ogl_PointSize(10.0f);
        ogl_PolygonMode(GL_FRONT_AND_BACK, GL_POINT);
        m.n_pkts = 0;
        ogl_Begin(GL_TRIANGLES);
        for (i = 0; i < 3; i++) {
            ogl_MultiTexCoord2fARB(GL_TEXTURE1_ARB, 0.125f, 0.5f);
            ogl_Vertex3fv(pos[i]);
        }
        ogl_End();
        ogl_Flush();
        k = last_pkt(VGPU98_OGL_DRAW);
        CHECK(k && ((k->p[1] & (VGPU98_OGL_DRAW_CLIP | VGPU98_OGL_DRAW_TEX1)) == (VGPU98_OGL_DRAW_CLIP | VGPU98_OGL_DRAW_TEX1)) &&
                  (fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_STIPPLE]) > 10.0f) &&
                  near_f(fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_S1]), 0.125f),
              "the ICD's sized points with unit 1: %x %f %f", k ? k->p[1] : 0,
              k ? fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_STIPPLE]) : 0.0f, k ? fl(k->p[VGPU98_OGL_LEN_DRAW + VGPU98_OGLV_S1]) : 0.0f);
        /* In feedback: the points as tokens, nothing drawn */
        ogl_FeedbackBuffer(64, GL_3D, fb);
        ogl_RenderMode(GL_FEEDBACK);
        m.n_pkts = 0;
        ogl_Begin(GL_TRIANGLES);
        for (i = 0; i < 3; i++)
            ogl_Vertex3fv(pos[i]);
        ogl_End();
        n = ogl_RenderMode(GL_RENDER);
        CHECK((n == 12) && (fb[0] == GL_POINT_TOKEN) && !count_pkts(VGPU98_OGL_DRAW), "points in feedback: %d values", n);
        ogl_PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
    CHECK(error_now() == GL_NO_ERROR, "an error at the end");
}

/* The final audit: a 2.5 device's gate for all seven extensions in one
   place, beyond ext11's */
static void
scenario_gate25(void)
{
    static const GLubyte px[4] = { 1, 2, 3, 4 };
    static const GLenum  qs[]  = { GL_POINT_SIZE_MIN_EXT, GL_POINT_SIZE_MAX_EXT, GL_POINT_FADE_THRESHOLD_SIZE_EXT,
                                   GL_DISTANCE_ATTENUATION_EXT, GL_CLIENT_ACTIVE_TEXTURE_ARB, GL_ARRAY_ELEMENT_LOCK_FIRST_EXT };
    GLfloat              f[4];
    GLubyte              out[16];
    const pkt_t         *k;
    unsigned             i;
    int                  j;

    machine_cs(16, 0x00020005u);
    current_context(TEST_DC);
    for (i = 0; i < ARRAY_SIZE(qs); i++) {
        ogl_GetFloatv(qs[i], f);
        CHECK(error_now() == GL_INVALID_ENUM, "query %x on a 2.5 device", qs[i]);
    }
    small_texture(3);
    ogl_GetTexImage(GL_TEXTURE_2D, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, out);
    CHECK(error_now() == GL_INVALID_ENUM, "glGetTexImage(GL_COLOR_INDEX) on a 2.5 device");
    ogl_IsEnabled(GL_SHARED_TEXTURE_PALETTE_EXT);
    CHECK(error_now() == GL_INVALID_ENUM, "IsEnabled(shared palette) on a 2.5 device");
    ogl_Enable(GL_TEXTURE_2D);
    m.n_pkts = 0;
    ogl_Begin(GL_POINTS);
    ogl_Vertex2f(0, 0);
    ogl_End();
    ogl_RasterPos2f(0, 0);
    ogl_DrawPixels(1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    ogl_Flush();
    k = last_pkt(VGPU98_OGL_DRAW);
    CHECK(k && !(k->p[1] & VGPU98_OGL_DRAW_TEX1) && (k->len == VGPU98_OGL_LEN_DRAW + VGPU98_OGL_VERTEX_DWORDS + 1),
          "a 2.5 device's DRAW: %x", k ? k->p[1] : 0);
    k = last_pkt(VGPU98_OGL_PIXELS);
    CHECK(k && !(k->p[1] & VGPU98_OGL_PIXELS_TEX1) && (k->len == VGPU98_OGL_LEN_PIXELS + 1), "a 2.5 device's PIXELS: %x %u",
          k ? k->p[1] : 0, k ? k->len : 0);
    for (j = 0; j < m.n_pkts; j++) {
        CHECK(m.pkts[j].op != VGPU98_OGL_PALETTE, "a PALETTE to a 2.5 device");
        if (m.pkts[j].op == VGPU98_OGL_TEXIMAGE)
            CHECK(m.pkts[j].p[5] < VGPU98_OGL_BASE_INDEX, "an INDEX base to a 2.5 device");
    }
    CHECK(!(reg_sent(VGPU98_OGL_R_ENABLES, 0) & (VGPU98_OGL_EN_SHARED_PALETTE | VGPU98_OGL_EN_TEXTURE1_1D)), "2.6 enables sent");
}


/* --- Diagnostics ----------------------------------- */

static int
has_line(const char *text)
{
    int i;

    for (i = 0; i < n_lines; i++) {
        if (strstr(lines[i], text))
            return 1;
    }
    return 0;
}

static int
count_lines(const char *text)
{
    int i, n = 0;

    for (i = 0; i < n_lines; i++)
        n += strstr(lines[i], text) != NULL;
    return n;
}

static void
show_lines(void)
{
    int i;

    for (i = 0; i < n_lines; i++)
        fprintf(stderr, "  | %s\n", lines[i]);
}

#define EXPECT(text)    CHECK(has_line(text), "no line with \"%s\"", text)
#define EXPECT_NO(text) CHECK(!has_line(text), "a line with \"%s\"", text)

/* A machine at a Diagnostics level: DEBUG as 86Box reads it there. */
static void
machine_level(DWORD debug)
{
    machine_cs(16, VGPU98_COMMAND_SET);
    m.regs[VGPU98_REG_DEBUG / 4] = debug;
}

/* Every line fits the writer's 159 characters, but a long path's, and
   would with a program name of the prefix's 20 characters, 10 more than
   QUAKE2.EXE's. */
static void
lines_fit(void)
{
    int i;

    for (i = 0; i < n_lines; i++)
        CHECK((strlen(lines[i]) + 10 < 159) || strstr(lines[i], ": file (") || strstr(lines[i], ": program "),
              "a line too long: %s", lines[i]);
}

static void
triangles(int n)
{
    int i;

    for (i = 0; i < n; i++) {
        ogl_Begin(GL_TRIANGLES);
        ogl_Vertex2f(0.0f, 0.0f);
        ogl_Vertex2f(0.5f, 0.0f);
        ogl_Vertex2f(0.0f, 0.5f);
        ogl_End();
    }
}

/* Diagnostics "OpenGL": the check, GL errors once each, the misses, a
   refused pixel format, the swap decisions, the report, the findings. */
static void
scenario_diag(void)
{
    static const GLubyte px[4 * 4 * 4] = { 0 };
    char                 prefix[96], want[200];
    swap_interval_t      set;
    GLuint               tex;
    int                  i;

    machine_level(VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_OPENGL);
    current_context(TEST_DC);
    snprintf(prefix, sizeof(prefix), "VGPU98 opengl %s QUAKE2.EXE: ", VGPU98_BUILD_STAMP);
    CHECK((n_lines >= 4) && !strncmp(lines[0], prefix, strlen(prefix)), "the prefix: %s", n_lines ? lines[0] : "(none)");
    EXPECT(": loaded; OpenGL 1.1 with 9 extensions (the device's command set 2.7)");
    EXPECT(": build " VGPU98_BUILD_STAMP ", the display driver's " VGPU98_BUILD_STAMP);
    EXPECT(": file (the system folder): " TEST_DLL);
    EXPECT(": program " TEST_PROGRAM);
    EXPECT_NO("stale");
    EXPECT(": extensions offered: GL_ARB_multitexture GL_EXT_compiled_vertex_array GL_EXT_paletted_texture");
    EXPECT(": extensions offered: GL_EXT_point_parameters GL_EXT_shared_texture_palette GL_EXT_texture_edge_clamp");
    EXPECT(": extensions offered: GL_SGIS_multitexture GL_SGIS_texture_edge_clamp WGL_EXT_swap_control");
    EXPECT(": window 5150: pixel format 1: colour 5-6-5, depth 16, stencil 0, double-buffered");
    CHECK((m.filters_set == 1) && m.filter, "the fault catcher not in place: %d", m.filters_set);

    /* GL errors: each function's each code once, named */
    ogl_Enable(0x1234);
    ogl_Enable(0x1234);
    CHECK(count_lines(": GL error GL_INVALID_ENUM in set_cap") == 1, "glEnable's INVALID_ENUM said %d times",
          count_lines(": GL error GL_INVALID_ENUM in set_cap"));
    ogl_Begin(GL_POINTS);
    ogl_BindTexture(GL_TEXTURE_2D, 1);
    ogl_End();
    EXPECT(": GL error GL_INVALID_OPERATION in BindTexture");
    CHECK(error_now() != GL_NO_ERROR, "the errors still recorded");

    /* A name not there, said once; a pixel format refused */
    CHECK(!DrvGetProcAddress("glFooEXT") && !DrvGetProcAddress("glFooEXT"), "glFooEXT found");
    CHECK(count_lines(": no function glFooEXT") == 1, "the miss said %d times", count_lines(": no function glFooEXT"));
    CHECK(DrvGetProcAddress("glLockArraysEXT") && !has_line("glLockArraysEXT"), "a function there said missing");
    CHECK(!DrvSetPixelFormat(TEST_DC2, 99), "pixel format 99 set");
    EXPECT(": pixel format 99 refused for DC 4445: 4 formats at 16 bits a pixel");

    /* The swap interval's decisions, said when they change */
    set = (swap_interval_t) DrvGetProcAddress("wglSwapIntervalEXT");
    CHECK(set != NULL, "no wglSwapIntervalEXT");
    if (!set)
        return;
    m.cursor_far = 1;
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    DrvSwapBuffers(TEST_DC);
    DrvSwapBuffers(TEST_DC);
    CHECK(count_lines(": window 5150: swap interval 1 not sent (a window on the desktop: it swaps at once)") == 1,
          "the desktop window's decision said %d times",
          count_lines(": window 5150: swap interval 1 not sent (a window on the desktop: it swaps at once)"));
    set(0);
    DrvSwapBuffers(TEST_DC);
    EXPECT(": window 5150: swap interval 0: swaps at once");
    set(1);
    full_screen_window();
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    DrvSwapBuffers(TEST_DC);
    EXPECT(": window 5150: swap interval 1 sent (the window covers the screen)");
    m.clip[0].b = 300;
    m.clip[1].l = 0;
    m.clip[1].t = 300;
    m.clip[1].r = 400;
    m.clip[1].b = 768;
    m.n_clip    = 2;
    DrvSwapBuffers(TEST_DC);
    EXPECT(": window 5150: swap interval 1 not sent (covered in part, 2 rectangles visible: it swaps at once)");
    full_screen_window();
    m.n_clip     = 1;
    m.cursor_far = 0;
    DrvSwapBuffers(TEST_DC);
    EXPECT(": window 5150: swap interval 1 not sent (the cursor is kept out of it: it swaps at once)");
    m.cursor_far = 1;

    /* The report, 5 seconds on: primitives by path */
    m.tick += 1000;
    DrvSwapBuffers(TEST_DC);
    triangles(10);
    ogl_PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    triangles(1);
    ogl_PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    ogl_Enable(0x1234);
    m.tick += 5000;
    DrvSwapBuffers(TEST_DC);
    /* (The first report counts from the first SwapBuffers, 6 s before.) */
    EXPECT(": report, 6.0 s: 7 swaps; primitives: 10 on the device, 1 on the ICD's path");
    EXPECT_NO(":   arrays drawn direct"); /* a line of zeros is left out */
    EXPECT(":   the ICD's path: materials in glBegin 0, polygon modes 1, stippled strips 0");
    EXPECT(":   GL errors 1: enum 1, value 0, operation 0, stack 0, out of memory 0");

    /* An incomplete texture drawn with (the default minification wants
       mipmaps), told once after 50 draws */
    ogl_GenTextures(1, &tex);
    ogl_BindTexture(GL_TEXTURE_2D, tex);
    ogl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    ogl_Enable(GL_TEXTURE_2D);
    triangles(49);
    EXPECT_NO("possible incomplete texture");
    triangles(1);
    snprintf(want, sizeof(want), ": possible incomplete texture: unit 0's texture %u drawn untextured (50 draws)", tex);
    EXPECT(want);
    EXPECT(":   its level 0 4x4, format 1908; 1 levels defined; minification NEAREST_MIPMAP_LINEAR");
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    triangles(100);
    CHECK(count_lines("possible incomplete texture") == 1, "the incomplete texture said again");
    ogl_Disable(GL_TEXTURE_2D);

    /* The slow path: the ICD's own path drawing most of 1000 or more
       primitives, three reports in a row (a display list called in the
       first, which brings the arrays-and-lists line) */
    ogl_NewList(31, GL_COMPILE);
    ogl_Color3f(1.0f, 1.0f, 1.0f);
    ogl_EndList();
    ogl_PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    for (i = 0; i < 3; i++) {
        triangles(1000);
        if (!i)
            ogl_CallList(31);
        m.tick += 5000;
        DrvSwapBuffers(TEST_DC);
        if (i < 2)
            EXPECT_NO("possible slow path");
    }
    ogl_PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    EXPECT(": possible slow path: the ICD's own path drew 1000 of 1000 primitives, three reports in a row");
    CHECK(count_lines(":   arrays drawn direct 0, display lists called 1") == 1, "the arrays-and-lists line with a list");
    lines_fit();
    if (failures)
        show_lines();
}

/* Minimal: the event lines alone, the fault catcher in place. */
static void
scenario_diagmin(void)
{
    machine_level(VGPU98_DEBUG_EVENTS);
    current_context(TEST_DC);
    ogl_Enable(0x1234);
    CHECK(!DrvGetProcAddress("glFooEXT"), "glFooEXT found");
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    DrvSwapBuffers(TEST_DC);
    m.tick += 6000;
    DrvSwapBuffers(TEST_DC);
    EXPECT(": loaded; OpenGL 1.1 with 9 extensions (the device's command set 2.7)");
    CHECK(n_lines == 1, "%d lines at Minimal", n_lines);
    CHECK(m.filters_set == 1, "the fault catcher at Minimal: %d", m.filters_set);
    if (failures)
        show_lines();
}

/* Off: nothing, no fault catcher. */
static void
scenario_diagoff(void)
{
    machine_level(0);
    current_context(TEST_DC);
    ogl_Enable(0x1234);
    CHECK(!DrvGetProcAddress("glFooEXT"), "glFooEXT found");
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    DrvSwapBuffers(TEST_DC);
    m.tick += 6000;
    DrvSwapBuffers(TEST_DC);
    CHECK(n_lines == 0, "%d lines at Off", n_lines);
    CHECK(m.filters_set == 0, "the fault catcher at Off");
}

/* Another build's display driver: a possible stale ICD, at every level. */
static void
scenario_diagstale(void)
{
    machine_level(VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_OPENGL);
    strncpy(m.stamp, "0000000", sizeof(m.stamp));
    current_context(TEST_DC);
    EXPECT(": possible stale OpenGL driver: built " VGPU98_BUILD_STAMP ", the display driver 0000000");
    EXPECT_NO(": build ");
    EXPECT(": loaded;");
    if (failures)
        show_lines();
}

/* A display driver of before the stamp: its answer's head alone. */
static void
scenario_diaghead(void)
{
    machine_level(VGPU98_DEBUG_EVENTS);
    m.escape_head = 1;
    current_context(TEST_DC);
    EXPECT(": possible stale OpenGL driver: built " VGPU98_BUILD_STAMP "; the display driver gives no stamp (an older build)");
    if (failures)
        show_lines();
}

/* Another build's shared block: one event under the DLL's own lock, no
   card, nothing more. */
static void
scenario_diagother(void)
{
    machine_level(VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_OPENGL);
    m.sb->version = SB_VERSION + 1;
    CHECK(DrvDescribePixelFormat(TEST_DC, 0, 0, NULL) == 0, "formats with another build's block");
    CHECK(!DrvGetProcAddress("glFooEXT"), "glFooEXT found");
    EXPECT(": no OpenGL: another build's display driver (block ");
    CHECK(n_lines == 1, "%d lines with another build's block", n_lines);
    CHECK(m.filters_set == 0, "the fault catcher without the card");
    if (failures)
        show_lines();
}

/* An 86Box without the OpenGL device. */
static void
scenario_diagnoogl(void)
{
    machine_level(VGPU98_DEBUG_EVENTS);
    m.regs[VGPU98_REG_PROFILE / 4] &= ~(DWORD) VGPU98_PROFILE_OGL;
    CHECK(DrvDescribePixelFormat(TEST_DC, 0, 0, NULL) == 0, "formats without PROFILE.OGL");
    EXPECT(": no OpenGL: the GPU's profile ");
    EXPECT("has no OpenGL device (an older 86Box)");
    if (failures)
        show_lines();
}

/* The last TEXPARAM's wraps (s | t << 4), or FFFFFFFFh for none. */
static DWORD
texparam_wraps(void)
{
    const pkt_t *k = last_pkt(VGPU98_OGL_TEXPARAM);

    return k ? k->p[2] : 0xffffffffu;
}

/* Clamp-to-edge on a 2.7 device: the token
   taken for both wraps, sent as TEXPARAM's value 2, answered by the
   queries, kept by GL_TEXTURE_BIT and display lists; GL_CLAMP still sent
   as itself (the device decides how it is sampled). */
static void
scenario_edgeclamp(void)
{
    GLuint  tex, list;
    GLint   iv;
    GLfloat fv;

    machine_cs(16, VGPU98_COMMAND_SET);
    current_context(TEST_DC);
    ogl_GenTextures(1, &tex);
    small_texture(tex);
    m.n_pkts = 0;
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    ogl_TexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLfloat) GL_CLAMP_TO_EDGE);
    ogl_Flush();
    CHECK(error_now() == GL_NO_ERROR, "GL_CLAMP_TO_EDGE refused on a 2.7 device");
    CHECK(texparam_wraps() == (VGPU98_OGL_WRAP_CLAMP_EDGE | (VGPU98_OGL_WRAP_CLAMP_EDGE << 4)), "TEXPARAM's wraps: %x",
          texparam_wraps());
    ogl_GetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &iv);
    ogl_GetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &fv);
    CHECK((iv == GL_CLAMP_TO_EDGE) && near_f(fv, (GLfloat) GL_CLAMP_TO_EDGE), "the wraps answered: %x %f", iv, fv);
    /* GL_CLAMP is sent as itself */
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    ogl_Flush();
    CHECK(texparam_wraps() == (VGPU98_OGL_WRAP_CLAMP | (VGPU98_OGL_WRAP_CLAMP_EDGE << 4)), "GL_CLAMP's wrap: %x",
          texparam_wraps());
    /* GL_TEXTURE_BIT keeps it */
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    ogl_PushAttrib(GL_TEXTURE_BIT);
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    ogl_PopAttrib();
    ogl_GetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &iv);
    CHECK(iv == GL_CLAMP_TO_EDGE, "the wrap after GL_TEXTURE_BIT's pop: %x", iv);
    /* A display list: set when it runs */
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    list = ogl_GenLists(1);
    ogl_NewList(list, GL_COMPILE);
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    ogl_EndList();
    ogl_GetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &iv);
    CHECK(iv == GL_REPEAT, "a compiled wrap set at once: %x", iv);
    ogl_CallList(list);
    ogl_GetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, &iv);
    CHECK(iv == GL_CLAMP_TO_EDGE, "the list's wrap: %x", iv);
    /* Not a filter */
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_CLAMP_TO_EDGE);
    CHECK(error_now() == GL_INVALID_ENUM, "GL_CLAMP_TO_EDGE as a filter");
}

/* A 2.6 device: command set 2.6's extensions alone; the token
   refused, and TEXPARAM never given the value 2. */
static void
scenario_ext26(void)
{
    const char *s;
    GLuint      tex;
    GLint       iv;
    int         i;

    machine_cs(16, 0x00020006u);
    current_context(TEST_DC);
    s = (const char *) ogl_GetString(GL_EXTENSIONS);
    CHECK(s && !strcmp(s, EXT_STRING_26), "GL_EXTENSIONS on a 2.6 device: \"%s\"", s ? s : "(null)");
    ogl_GenTextures(1, &tex);
    small_texture(tex);
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    CHECK(error_now() == GL_INVALID_ENUM, "GL_CLAMP_TO_EDGE on a 2.6 device");
    ogl_GetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &iv);
    CHECK(iv == GL_REPEAT, "the wrap after the refusal: %x", iv);
    ogl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    ogl_Flush();
    for (i = 0; i < m.n_pkts; i++) {
        if (m.pkts[i].op == VGPU98_OGL_TEXPARAM)
            CHECK(((m.pkts[i].p[2] & 15u) <= VGPU98_OGL_WRAP_CLAMP) && (((m.pkts[i].p[2] >> 4) & 15u) <= VGPU98_OGL_WRAP_CLAMP),
                  "a 2.6 device sent wraps %x", m.pkts[i].p[2]);
    }
    CHECK(texparam_wraps() == VGPU98_OGL_WRAP_CLAMP, "GL_CLAMP on a 2.6 device: %x", texparam_wraps());
}

/* The ICD gives the GPU 5 seconds without progress before restarting it:
   a fence 2 s late, as a first
   pipeline made on a slow host GPU, is waited for; one 6 s late restarts
   the GPU. */
static void
scenario_patience(void)
{
    int before;

    machine(16);
    current_context(TEST_DC);
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Finish();
    before          = m.resets;
    m.fence_hold_ms = 2000;
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Finish();
    CHECK(m.resets == before, "a fence 2 s late restarted the GPU");
    CHECK(!m.fence_waiting, "the late fence not waited for");
    m.fence_hold_ms = 6000;
    ogl_Clear(GL_COLOR_BUFFER_BIT);
    ogl_Finish();
    CHECK(m.resets == before + 1, "a fence 6 s late: %d restarts", m.resets - before);
    m.fence_hold_ms = 0;
}

static void
run(const char *scenario)
{
    if (!strcmp(scenario, "none")) {
        /* Another display driver: no formats, no contexts. */
        machine(16);
        m.escape = 0;
        CHECK(DrvDescribePixelFormat(TEST_DC, 0, 0, NULL) == 0, "formats without the card");
        CHECK(!DrvSetPixelFormat(TEST_DC, 1), "a pixel format without the card");
        CHECK(!DrvCreateContext(TEST_DC), "a context without the card");
        CHECK(m.dc_open == 0, "a DC left open");
    } else if (!strcmp(scenario, "noogl")) {
        /* The card of an 86Box without the OpenGL device (no Vulkan). */
        machine(16);
        m.regs[VGPU98_REG_PROFILE / 4] &= ~(DWORD) VGPU98_PROFILE_OGL;
        CHECK(DrvDescribePixelFormat(TEST_DC, 0, 0, NULL) == 0, "formats without PROFILE.OGL");
    } else if (!strcmp(scenario, "formats")) {
        ogl_pfd_t pfd;

        machine(16);
        CHECK(DrvDescribePixelFormat(TEST_DC, 0, 0, NULL) == 4, "16 bpp: %ld formats",
              (long) DrvDescribePixelFormat(TEST_DC, 0, 0, NULL));
        expect_pfd(1, 16, 5, 6, 5, 0, 16, 0, 1);
        expect_pfd(2, 16, 5, 6, 5, 0, 24, 8, 1);
        expect_pfd(3, 16, 5, 6, 5, 0, 16, 0, 0);
        expect_pfd(4, 16, 5, 6, 5, 0, 24, 8, 0);
        CHECK(DrvDescribePixelFormat(TEST_DC, 5, sizeof(pfd), &pfd) == 0, "format 5 at 16 bpp");
        m.sb->mode_bpp = 32;
        CHECK(DrvDescribePixelFormat(TEST_DC, 0, 0, NULL) == 6, "32 bpp: formats");
        expect_pfd(1, 32, 8, 8, 8, 0, 24, 8, 1);
        expect_pfd(2, 32, 8, 8, 8, 0, 16, 0, 1);
        expect_pfd(3, 32, 8, 8, 8, 8, 24, 8, 1);
        expect_pfd(4, 32, 8, 8, 8, 0, 24, 8, 0);
        expect_pfd(5, 32, 8, 8, 8, 0, 16, 0, 0);
        expect_pfd(6, 32, 8, 8, 8, 8, 24, 8, 0);
        m.sb->mode_bpp = 8;
        CHECK(DrvDescribePixelFormat(TEST_DC, 0, 0, NULL) == 0, "8 bpp: formats");
        CHECK(!DrvDescribeLayerPlane(TEST_DC, 1, 1, 32, &pfd), "a layer plane");
        CHECK(!DrvGetProcAddress("glLockArraysEXT"), "an extension's function");
    } else if (!strcmp(scenario, "context")) {
        DHGLRC rc;
        int    i, nulls = 0;
        const pkt_t *k;

        machine(16);
        CHECK(!DrvCreateContext(TEST_DC), "a context before a pixel format");
        CHECK(!DrvSetPixelFormat(TEST_MEMDC, 1), "a pixel format for a memory DC");
        CHECK(!DrvSetPixelFormat(TEST_DC, 9), "pixel format 9");
        CHECK(!DrvCreateLayerContext(TEST_DC, 1), "an overlay plane's context");
        rc = current_context(TEST_DC);
        CHECK(ogl_table.count == 336, "the table's count: %d", ogl_table.count);
        for (i = 0; i < 336; i++)
            nulls += !ogl_table.entry[i];
        CHECK(!nulls, "%d entries of the table are NULL", nulls);
        ogl_Flush();
        k = nth_pkt(VGPU98_OGL_GROUP, 0);
        CHECK(k && (k->p[1] == VGPU98_OGL_OPEN) && k->p[0], "no GROUP OPEN");
        k = nth_pkt(VGPU98_OGL_CONTEXT, 0);
        CHECK(k && (k->p[1] == VGPU98_OGL_OPEN) && (k->p[2] == nth_pkt(VGPU98_OGL_GROUP, 0)->p[0]), "no CONTEXT OPEN in the group");
        k = nth_pkt(VGPU98_OGL_DRAWABLE, 0);
        CHECK(k && (k->p[1] == VGPU98_OGL_OPEN) && (k->p[2] == (640u | (480u << 16))) &&
                  (k->p[3] == (VGPU98_OGL_COLOR_565 | (VGPU98_OGL_DEPTH_16 << VGPU98_OGL_DEPTH_SHIFT) | VGPU98_OGL_DOUBLE)),
              "no DRAWABLE OPEN at the window's size and format");
        k = nth_pkt(VGPU98_OGL_BIND, 0);
        CHECK(k && (k->p[1] == nth_pkt(VGPU98_OGL_DRAWABLE, 0)->p[0]), "no BIND to the drawable");
        /* The first MakeCurrent sets the viewport and scissor box to the
           window; the registers go whole at the first draw (a clear). */
        ogl_Clear(GL_COLOR_BUFFER_BIT);
        ogl_Flush();
        CHECK(reg_sent(VGPU98_OGL_R_VIEWPORT + 2, 0) == 640 && reg_sent(VGPU98_OGL_R_VIEWPORT + 3, 0) == 480, "the viewport");
        CHECK(reg_sent(VGPU98_OGL_R_SCISSOR + 2, 0) == 640, "the scissor box");
        CHECK(reg_sent(VGPU98_OGL_R_DRAW_BUFFERS, 9) == 2, "the draw buffer: %u", reg_sent(VGPU98_OGL_R_DRAW_BUFFERS, 9));
        CHECK(near_f(fl(reg_sent(VGPU98_OGL_R_MODELVIEW, 0)), 1.0f), "the modelview's identity");
        CHECK(reg_sent(VGPU98_OGL_R_ENABLES, 0) == VGPU98_OGL_EN_DITHER, "the enables: %x", reg_sent(VGPU98_OGL_R_ENABLES, 0));
        CHECK(DrvReleaseContext(rc), "DrvReleaseContext");
        CHECK(DrvDeleteContext(rc), "DrvDeleteContext");
        CHECK(last_pkt(VGPU98_OGL_CONTEXT)->p[1] == VGPU98_OGL_CLOSE, "no CONTEXT CLOSE");
        CHECK(last_pkt(VGPU98_OGL_GROUP)->p[1] == VGPU98_OGL_CLOSE, "no GROUP CLOSE with its last context");
        CHECK(last_pkt(VGPU98_OGL_DRAWABLE)->p[1] == VGPU98_OGL_CLOSE, "no DRAWABLE CLOSE with the process's last context");
    } else if (!strcmp(scenario, "clear")) {
        const pkt_t *k;

        machine(16);
        current_context(TEST_DC);
        ogl_ClearColor(0.25f, 2.0f, -1.0f, 0.5f);
        ogl_ClearDepth(0.75);
        ogl_ClearStencil(3);
        ogl_Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        ogl_Flush();
        k = last_pkt(VGPU98_OGL_CLEAR);
        /* The 565 + D16 format has no stencil: the stencil bit clears nothing. */
        CHECK(k && (k->len == VGPU98_OGL_LEN_CLEAR) && (VGPU98_OGL_LEN_CLEAR == 8), "CLEAR's length: %u", k ? k->len : 0);
        CHECK(k && (k->p[1] == (VGPU98_OGL_CLEAR_COLOR | VGPU98_OGL_CLEAR_DEPTH)), "CLEAR's mask: %x", k ? k->p[1] : 0);
        CHECK(k && near_f(fl(k->p[2]), 0.25f) && near_f(fl(k->p[3]), 1.0f) && near_f(fl(k->p[4]), 0.0f) &&
                  near_f(fl(k->p[5]), 0.5f) && near_f(fl(k->p[6]), 0.75f),
              "CLEAR's values, clamped");
        ogl_Clear(0x80000000u);
        CHECK(ogl_GetError() == GL_INVALID_VALUE, "glClear of another bit");
        CHECK(ogl_GetError() == GL_NO_ERROR, "a second error");
        ogl_Scissor(10, 20, -1, 5);
        CHECK(ogl_GetError() == GL_INVALID_VALUE, "a negative scissor box");
    } else if (!strcmp(scenario, "swap")) {
        const pkt_t *k;

        machine(16);
        current_context(TEST_DC);
        /* Covered on its right half and its top by another window. */
        m.n_clip    = 2;
        m.clip[0].l = 100;
        m.clip[0].t = 50;
        m.clip[0].r = 420;
        m.clip[0].b = 530;
        m.clip[1].l = 420;
        m.clip[1].t = 300;
        m.clip[1].r = 2000; /* past the client area and the screen: cut */
        m.clip[1].b = 900;
        ogl_Clear(GL_COLOR_BUFFER_BIT);
        CHECK(DrvSwapBuffers(TEST_DC), "DrvSwapBuffers");
        k = last_pkt(VGPU98_OGL_SWAP);
        CHECK(k && (k->p[1] == (VGPU98_OGL_SWAP_COPY | VGPU98_OGL_SWAP_SHOW)), "SWAP's flags");
        CHECK(k && (k->p[2] == 0) && (k->p[3] == 2048) && (k->p[4] == 16) && (k->p[5] == (100u | (50u << 16))),
              "SWAP's screen and place");
        CHECK(k && (k->p[6] == 2), "SWAP's rectangles: %u", k ? k->p[6] : 0);
        CHECK(k && (k->p[7] == (100u | (50u << 16))) && (k->p[8] == (420u | (530u << 16))), "the first rectangle");
        CHECK(k && (k->p[9] == (420u | (300u << 16))) && (k->p[10] == (740u | (530u << 16))), "the second rectangle, cut");
        /* Three swaps: the third waits for the first's fence (done here). */
        DrvSwapBuffers(TEST_DC);
        DrvSwapBuffers(TEST_DC);
        CHECK(count_pkts(VGPU98_OGL_SWAP) == 3, "swaps: %d", count_pkts(VGPU98_OGL_SWAP));
    } else if (!strcmp(scenario, "resize")) {
        const pkt_t *k;

        machine(32);
        current_context(TEST_DC);
        ogl_Clear(GL_COLOR_BUFFER_BIT);
        m.client_w[0] = 800;
        m.client_h[0] = 600;
        ogl_Clear(GL_COLOR_BUFFER_BIT);
        ogl_Flush();
        k = last_pkt(VGPU98_OGL_DRAWABLE);
        CHECK(k && (k->p[1] == VGPU98_OGL_RESIZE) && (k->p[2] == (800u | (600u << 16))) &&
                  (k->p[3] == (VGPU98_OGL_COLOR_XRGB8888 | (VGPU98_OGL_DEPTH_24S8 << VGPU98_OGL_DEPTH_SHIFT) | VGPU98_OGL_DOUBLE)),
              "no RESIZE to the new client size");
        CHECK(count_pkts(VGPU98_OGL_DRAWABLE) == 2, "drawable packets: %d", count_pkts(VGPU98_OGL_DRAWABLE));
        /* The 8888 + D24S8 format has stencil: its value goes in CLEAR's last dword. */
        ogl_ClearStencil(0x1203);
        ogl_Clear(GL_STENCIL_BUFFER_BIT);
        ogl_Flush();
        k = last_pkt(VGPU98_OGL_CLEAR);
        CHECK(k && (k->p[1] == VGPU98_OGL_CLEAR_STENCIL) && (k->p[7] == 0x1203), "CLEAR's stencil: %x", k ? k->p[7] : 0);
    } else if (!strcmp(scenario, "held")) {
        /* The GPU held by a mode change: the batch waits, then goes whole. */
        machine(16);
        current_context(TEST_DC);
        m.regs[VGPU98_REG_STATUS / 4] |= VGPU98_STATUS_HELD;
        m.release_after = 10;
        ogl_Clear(GL_COLOR_BUFFER_BIT);
        ogl_Flush();
        CHECK(m.sleeps >= 10, "the batch did not wait: %d sleeps", m.sleeps);
        CHECK(count_pkts(VGPU98_OGL_CLEAR) == 1, "the CLEAR was dropped");
        CHECK(count_pkts(VGPU98_OGL_CONTEXT) == 1, "the CONTEXT OPEN was dropped");
    } else if (!strcmp(scenario, "restart")) {
        /* The GPU restarts as it takes the first batch's third packet: the
           next batch sends the objects and registers again first. */
        int before;

        machine(16);
        current_context(TEST_DC);
        m.restart_at = 3;
        ogl_Clear(GL_COLOR_BUFFER_BIT);
        ogl_Flush();
        before = count_pkts(VGPU98_OGL_CONTEXT);
        ogl_Clear(GL_COLOR_BUFFER_BIT);
        ogl_Flush();
        CHECK(count_pkts(VGPU98_OGL_CONTEXT) == before + 1, "the context was not sent again");
        CHECK(count_pkts(VGPU98_OGL_GROUP) == 2 && count_pkts(VGPU98_OGL_BIND) == 2, "the group and binding were not sent again");
        CHECK(last_pkt(VGPU98_OGL_DRAWABLE)->p[1] == VGPU98_OGL_OPEN, "the drawable was not sent again");
        CHECK(last_pkt(VGPU98_OGL_STATE)->len == 2 + VGPU98_OGL_R_COUNT || count_pkts(VGPU98_OGL_STATE) >= 2,
              "the registers were not sent again");
    } else if (!strcmp(scenario, "detach")) {
        /* The process ends with a context current: everything closed, nothing waited for. */
        machine(16);
        current_context(TEST_DC);
        ogl_Clear(GL_COLOR_BUFFER_BIT);
        ogl_Flush();
        m.sleeps = 0;
        DllMain(0x11800000u, 0, (void *) 1);
        CHECK(last_pkt(VGPU98_OGL_CONTEXT)->p[1] == VGPU98_OGL_CLOSE, "no CONTEXT CLOSE at the detach");
        CHECK(last_pkt(VGPU98_OGL_DRAWABLE)->p[1] == VGPU98_OGL_CLOSE, "no DRAWABLE CLOSE at the detach");
        CHECK(m.sleeps == 0, "the detach waited");
    } else if (!strcmp(scenario, "threads")) {
        scenario_threads();
    } else if (!strcmp(scenario, "rehwnd")) {
        scenario_rehwnd();
    } else if (!strcmp(scenario, "queries")) {
        scenario_queries();
    } else if (!strcmp(scenario, "attrib")) {
        scenario_attrib();
    } else if (!strcmp(scenario, "lists")) {
        scenario_lists();
    } else if (!strcmp(scenario, "select")) {
        scenario_select();
    } else if (!strcmp(scenario, "feedback")) {
        scenario_feedback();
    } else if (!strcmp(scenario, "eval")) {
        scenario_eval();
    } else if (!strcmp(scenario, "rasterpos")) {
        scenario_rasterpos();
    } else if (!strcmp(scenario, "errors")) {
        scenario_errors();
    } else if (!strcmp(scenario, "texobj")) {
        scenario_texobj();
    } else if (!strcmp(scenario, "draw")) {
        scenario_draw();
    } else if (!strcmp(scenario, "cpudraw")) {
        scenario_cpudraw();
    } else if (!strcmp(scenario, "colormat")) {
        scenario_colormat();
    } else if (!strcmp(scenario, "teximage")) {
        scenario_teximage();
    } else if (!strcmp(scenario, "pixels")) {
        scenario_pixels();
    } else if (!strcmp(scenario, "readpixels")) {
        scenario_readpixels();
    } else if (!strcmp(scenario, "share")) {
        scenario_share();
    } else if (!strcmp(scenario, "front")) {
        scenario_front();
    } else if (!strcmp(scenario, "ext")) {
        scenario_ext();
    } else if (!strcmp(scenario, "ext11")) {
        scenario_ext11();
    } else if (!strcmp(scenario, "swapint")) {
        scenario_swapint();
    } else if (!strcmp(scenario, "swapint1")) {
        scenario_swapint1();
    } else if (!strcmp(scenario, "swapint0")) {
        scenario_swapint_no_vsync();
    } else if (!strcmp(scenario, "cva")) {
        scenario_cva();
    } else if (!strcmp(scenario, "cva2")) {
        scenario_cva2();
    } else if (!strcmp(scenario, "multitex")) {
        scenario_multitex();
    } else if (!strcmp(scenario, "multitex2")) {
        scenario_multitex2();
    } else if (!strcmp(scenario, "palette")) {
        scenario_palette();
    } else if (!strcmp(scenario, "palette2")) {
        scenario_palette2();
    } else if (!strcmp(scenario, "cross")) {
        scenario_cross();
    } else if (!strcmp(scenario, "gate25")) {
        scenario_gate25();
    } else if (!strcmp(scenario, "diag")) {
        scenario_diag();
    } else if (!strcmp(scenario, "diagmin")) {
        scenario_diagmin();
    } else if (!strcmp(scenario, "diagoff")) {
        scenario_diagoff();
    } else if (!strcmp(scenario, "diagstale")) {
        scenario_diagstale();
    } else if (!strcmp(scenario, "diaghead")) {
        scenario_diaghead();
    } else if (!strcmp(scenario, "diagother")) {
        scenario_diagother();
    } else if (!strcmp(scenario, "diagnoogl")) {
        scenario_diagnoogl();
    } else if (!strcmp(scenario, "edgeclamp")) {
        scenario_edgeclamp();
    } else if (!strcmp(scenario, "ext26")) {
        scenario_ext26();
    } else if (!strcmp(scenario, "patience")) {
        scenario_patience();
    } else if (!strcmp(scenario, "pointparam")) {
        scenario_pointparam();
    } else {
        fprintf(stderr, "test_ogl: unknown scenario %s\n", scenario);
        exit(2);
    }
}

int
main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: test_ogl SCENARIO\n");
        return 2;
    }
    run(argv[1]);
    if (failures)
        fprintf(stderr, "test_ogl %s: %d failures\n", argv[1], failures);
    return failures ? 1 : 0;
}

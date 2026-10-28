/*
 * The Glide DLLs' card-found path on the host:
 * glide/card.c and one DLL's own sources, compiled here, with stand-ins
 * for what Windows 98 gives them. GDI's escape answers as the display
 * driver would; the shared block and BAR0 are memory below 4 GB; the real
 * line writer (driver32/logline.c) hands its characters to test_put
 * (LOG_PUT_HOOK) instead of BAR0's DEBUG register, so the lines are what
 * the host would get, cut at 159 characters as it would cut them. Each
 * scenario runs in its own process (the DLLs find the card once a process).
 *
 *   test_glide2x SCENARIO, test_glide3x SCENARIO
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef TEST_GLIDE3
#    include "glide3x.h"
#else
#    include "glide2x.h"
#endif
#include "logline.h"
#include "glcore.h"

void        *test_low_alloc(size_t size);
BOOL STDCALL DllMain(DWORD dll_instance, DWORD reason, void *reserved); /* glide/card.c */

#define TEST_INSTANCE 0x10000000u
#define TEST_DC       0x4444u

static int failures;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            fprintf(stderr, "test_glide:%d: ", __LINE__);   \
            fprintf(stderr, __VA_ARGS__);                   \
            fputc('\n', stderr);                            \
            failures++;                                     \
        }                                                   \
    } while (0)

/* --- The machine ------------------------------------------------------------ */

#define TEST_VRAM_BYTES 0x1000000 /* 16 MB of video memory */
#define TEST_HWND       0x5150   /* the program's active window */
#define TEST_TRAY_HWND  0x6000   /* another program's window (the taskbar) */
#define TEST_OTHER_HWND 0x7000   /* a window of another thread of the program */
#define TEST_FRAME_HWND 0x8000   /* another window of the program's own thread */

/* A packet the stand-in GPU took from the ring: its opcode, length and
   first dwords. */
#define PKT_KEPT 264 /* GAMMA's whole payload */
typedef struct pkt {
    DWORD op, len;
    DWORD p[PKT_KEPT];
} pkt_t;

static struct {
    const char      *program;      /* the program's full path */
    const char      *dll;          /* the DLL's full path */
    char            *env;          /* the environment's block */
    int              escape;       /* the display driver answers VGPU98_ESC_INFO */
    DWORD            answer_size;  /* the bytes of the answer it gives (16 or 32) */
    const char      *stamp;        /* its build stamp */
    DWORD            sb_version;   /* the block's version */
    DWORD            debug;        /* BAR0 DEBUG's read bits */
    vgpu98_shared_t *sb;
    volatile DWORD  *regs;
    uint8_t         *vram;
    int              dc_open;
    /* The GPU */
    pkt_t            pkts[2048];
    int              n_pkts;
    int              resets;
    int              stall;          /* FENCE packets are not done (the GPU at work) */
    DWORD            stalled_fence;  /* the last FENCE taken meanwhile */
    int              sleeps;         /* Sleep calls */
    int              release_after;  /* the stall and a hold end at this many sleeps (0: never) */
    int              restart_at_open; /* the GPU restarts after the first OPEN it takes */
    int              restart_at_clear; /* the GPU restarts as it takes the next CLEAR (the batch's earlier work lost) */
    int              mode_change_at_start; /* the screen's mode changes as the GPU is next started */
    DWORD            thread;         /* GetCurrentThreadId's answer, when set */
    int              in_dllmain;     /* the scenario is calling DllMain: no USER32 call may come */
    int              window_queries; /* the window's description asked for */
    /* DirectDraw */
    int              dd_objects;   /* live: created less released */
    DWORD            coop_hwnd, coop_flags;
    DWORD            mode_w, mode_h, mode_bpp, mode_hz;
    int              mode_refuse;  /* SetDisplayMode fails */
    int              modes_set, restores;
    DWORD            surface_offset; /* the staging surface's place in video memory */
    DWORD            surface_pitch;
} m;

/* The lines written, as the host gets them: characters through BAR0's
   DEBUG register, a line to each newline. */
static char lines[512][200];
static int  cut[512]; /* the writer cut the line at 159 characters */
static int  n_lines;
static int  line_at;

void test_put(volatile uint32_t *regs, char c);

void
test_put(volatile uint32_t *regs, char c)
{
    CHECK(regs == m.regs, "a line written to another register window");
    /* Under the block's lock while it is this build's, else the DLL's own. */
    CHECK((m.sb_version == SB_VERSION) ? (m.sb->log_lock == 1) : (m.sb->log_lock == 0), "a line under the wrong lock");
    if (n_lines >= (int) (sizeof(lines) / sizeof(lines[0])))
        return;
    if (c == 10) {
        /* The writer cuts a line at 159 characters: only a long path's
           should need it (run checks). */
        cut[n_lines] = line_at >= 159;
        lines[n_lines++][line_at] = 0;
        line_at = 0;
    } else if (line_at < (int) sizeof(lines[0]) - 1)
        lines[n_lines][line_at++] = c;
}

DWORD STDCALL
GetModuleFileNameA(DWORD module, char *name, DWORD size)
{
    const char *path = (module == 0) ? m.program : (module == TEST_INSTANCE) ? m.dll : "";

    snprintf(name, size, "%s", path);
    return (DWORD) strlen(name);
}

void STDCALL
Sleep(DWORD ms)
{
    (void) ms;
    m.sleeps++;
    if ((m.stall || (m.regs && (m.regs[VGPU98_REG_STATUS / 4] & VGPU98_STATUS_HELD))) && m.release_after &&
        (m.sleeps >= m.release_after)) {
        m.stall = 0;
        m.regs[VGPU98_REG_FENCE / 4] = m.stalled_fence;
        m.regs[VGPU98_REG_STATUS / 4] &= ~(DWORD) VGPU98_STATUS_HELD;
    }
}

DWORD STDCALL
GetSystemDirectoryA(char *path, DWORD size)
{
    snprintf(path, size, "C:\\WINDOWS\\system");
    return (DWORD) strlen(path);
}

static char environment[] = "PATH=C:\\WINDOWS\0FX_GLIDE_NO_SPLASH=1\0sst_gamma=1.3\0SSTX=1\0\0";

char *STDCALL
GetEnvironmentStringsA(void)
{
    return m.env;
}

BOOL STDCALL
FreeEnvironmentStringsA(char *block)
{
    CHECK(block == m.env, "FreeEnvironmentStringsA of another block");
    return 1;
}

DWORD STDCALL
CreateDCA(const char *driver, const char *device, const char *output, const void *devmode)
{
    CHECK(!strcmp(driver, "DISPLAY") && !device && !output && !devmode, "CreateDCA not for the display");
    m.dc_open++;
    return TEST_DC;
}

BOOL STDCALL
DeleteDC(DWORD dc)
{
    CHECK(dc == TEST_DC, "DeleteDC of another DC");
    m.dc_open--;
    return 1;
}

/* As the display driver answers (driver/ddraw.asm, Control). */
int STDCALL
ExtEscape(DWORD dc, int escape, int in_bytes, const char *in, int out_bytes, char *out)
{
    CHECK(dc == TEST_DC, "ExtEscape on another DC");
    if (escape == QUERYESCSUPPORT) {
        CHECK((in_bytes >= 2) && in, "QUERYESCSUPPORT without its input");
        return (m.escape && (*(const uint16_t *) in == VGPU98_ESC_INFO)) ? 1 : 0;
    }
    if ((escape == VGPU98_ESC_INFO) && m.escape) {
        const vgpu98_escquery_t *q = (const vgpu98_escquery_t *) in;
        vgpu98_escinfo_t        *a = (vgpu98_escinfo_t *) out;

        CHECK((in_bytes == (int) sizeof(*q)) && (out_bytes == (int) sizeof(*a)), "VGPU98_ESC_INFO's buffers");
        if (!q || !a || (q->magic != ESC_INFO_MAGIC) || (q->out_size < ESC_INFO_HEAD))
            return -1;
        CHECK(q->out_size == sizeof(*a), "the question's out_size %u", q->out_size);
        a->magic         = ESC_INFO_MAGIC;
        a->size          = m.answer_size;
        a->shared_linear = (DWORD) (uintptr_t) m.sb;
        a->sb_version    = m.sb_version;
        if (m.answer_size == sizeof(*a)) {
            memset(a->stamp, 0, sizeof(a->stamp));
            memcpy(a->stamp, m.stamp, strlen(m.stamp));
        }
        return 1;
    }
    return 0;
}

/* --- The GPU: the ring's packets, taken at each doorbell ----------------------- */

void test_reg_write(DWORD off, DWORD v);

static DWORD
ring_word(DWORD at)
{
    DWORD v;

    memcpy(&v, m.vram + m.regs[VGPU98_REG_RING_BASE / 4] + at, 4);
    return v;
}

/* A read of the frame buffer writes each pixel's place there (x + 7 y), so
   a read region's copy can be checked. */
static void
gpu_lfb_read(const pkt_t *k)
{
    const DWORD x = k->p[2] & 0xffff, y = k->p[2] >> 16, w = k->p[3] & 0xffff, h = k->p[3] >> 16;
    DWORD       r, i;

    if ((k->p[4] & VGPU98_GL_LFB_FORMAT_MASK) != VGPU98_GL_LFB_565)
        return;
    for (r = 0; r < h; r++) {
        for (i = 0; i < w; i++) {
            const uint16_t v = (uint16_t) ((x + i) + 7 * (y + r));

            memcpy(m.vram + k->p[5] + (y + r) * k->p[6] + (x + i) * 2, &v, 2);
        }
    }
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
        if (v & VGPU98_CTRL_RUN) {
            m.regs[VGPU98_REG_STATUS / 4] |= VGPU98_STATUS_RUNNING;
            if (m.mode_change_at_start) {
                m.mode_change_at_start = 0;
                m.sb->mode_serial++;
            }
        }
        else if (!(v & VGPU98_CTRL_RESET))
            m.regs[VGPU98_REG_STATUS / 4] &= ~(DWORD) VGPU98_STATUS_RUNNING;
        return;
    }
    if (off != VGPU98_REG_RING_TAIL)
        return;
    /* Every packet from RING_HEAD to the new tail, the GPU done with each. */
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
                if (m.n_pkts < (int) (sizeof(m.pkts) / sizeof(m.pkts[0]))) {
                    pkt_t *p = &m.pkts[m.n_pkts++];

                    p->op  = op;
                    p->len = len;
                    for (k = 0; (k < len) && (k < PKT_KEPT); k++)
                        p->p[k] = ring_word(at + 4 + k * 4);
                    if (op == VGPU98_GL_LFB_READ)
                        gpu_lfb_read(p);
                }
                if ((op == VGPU98_OP_FENCE) && m.stall)
                    m.stalled_fence = ring_word(at + 4);
                else if (op == VGPU98_OP_FENCE)
                    m.regs[VGPU98_REG_FENCE / 4] = ring_word(at + 4);
                /* A restart once the OPEN is in the ring: it may be lost. */
                if ((op == VGPU98_GL_OPEN) && m.restart_at_open) {
                    m.restart_at_open = 0;
                    m.sb->state_epoch++;
                }
                if ((op == VGPU98_GL_CLEAR) && m.restart_at_clear) {
                    m.restart_at_clear = 0;
                    m.sb->state_epoch++;
                }
            }
            at = (at + 4 + len * 4) % size;
        }
        m.regs[VGPU98_REG_RING_HEAD / 4] = v;
    }
}

/* The packets of one opcode taken so far, and the n-th of them. */
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
        if ((m.pkts[i].op == op) && !n--)
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

/* The fault catcher's filter, as Windows would keep it: the scenario
   calls it with a fault of its own. */
static glide_filter_t filter_set;

glide_filter_t STDCALL
SetUnhandledExceptionFilter(glide_filter_t filter)
{
    glide_filter_t old = filter_set;

    filter_set = filter;
    return old;
}

#ifndef TEST_GLIDE3
/* The program's own filter, from before the DLL's. */
static int program_faults;

static LONG STDCALL
program_filter(void *pointers)
{
    (void) pointers;
    program_faults++;
    return 0;
}
#endif

BOOL STDCALL
IsBadReadPtr(const void *p, DWORD bytes)
{
    (void) bytes;
    return p == NULL;
}

/* KERNEL32's files, over the host's: handles 1-8. */
static FILE *files[8];

DWORD STDCALL
CreateFileA(const char *name, DWORD access, DWORD share, void *security, DWORD how, DWORD flags, DWORD template_file)
{
    int i;

    (void) share;
    (void) security;
    (void) flags;
    (void) template_file;
    CHECK((access == 0x80000000u) && (how == 3), "a file opened other than for reading");
    for (i = 0; i < 8; i++) {
        if (!files[i]) {
            files[i] = fopen(name, "rb");
            return files[i] ? (DWORD) (i + 1) : 0xffffffffu;
        }
    }
    return 0xffffffffu;
}

BOOL STDCALL
ReadFile(DWORD file, void *buffer, DWORD bytes, DWORD *got, void *overlapped)
{
    (void) overlapped;
    if (!file || (file > 8) || !files[file - 1])
        return 0;
    *got = (DWORD) fread(buffer, 1, bytes, files[file - 1]);
    return 1;
}

DWORD STDCALL
SetFilePointer(DWORD file, LONG distance, LONG *high, DWORD method)
{
    (void) high;
    if (!file || (file > 8) || !files[file - 1] || (method != 0) || fseek(files[file - 1], distance, SEEK_SET))
        return 0xffffffffu;
    return (DWORD) ftell(files[file - 1]);
}

BOOL STDCALL
CloseHandle(DWORD handle)
{
    if (!handle || (handle > 8) || !files[handle - 1])
        return 0;
    fclose(files[handle - 1]);
    files[handle - 1] = NULL;
    return 1;
}

#ifndef TEST_GLIDE3
static int
files_open(void)
{
    int i, n = 0;

    for (i = 0; i < 8; i++)
        n += files[i] != NULL;
    return n;
}
#endif

DWORD STDCALL
GetTickCount(void)
{
    static DWORD ms;

    return ms += 1;
}

DWORD STDCALL
GetCurrentProcessId(void)
{
    return 0xfff12345;
}

DWORD STDCALL
GetCurrentThreadId(void)
{
    return m.thread ? m.thread : 0x77;
}

DWORD STDCALL
GetActiveWindow(void)
{
    return TEST_HWND;
}

static DWORD fg_window; /* GetForegroundWindow's answer, when set */

DWORD STDCALL
GetForegroundWindow(void)
{
    CHECK(!m.in_dllmain, "GetForegroundWindow inside DllMain");
    return fg_window ? fg_window : TEST_HWND;
}

int STDCALL
GetClassNameA(DWORD hwnd, char *name, int size)
{
    const char *c = (hwnd == TEST_HWND)        ? "TestWindow"
                    : (hwnd == TEST_TRAY_HWND)  ? "Shell_TrayWnd"
                    : (hwnd == TEST_OTHER_HWND) ? "Other\"Thread\x01"
                    : (hwnd == TEST_FRAME_HWND) ? "Frame"
                                                : NULL;

    CHECK(!m.in_dllmain, "GetClassNameA inside DllMain");
    m.window_queries++;
    if (!c || (size < 1))
        return 0;
    snprintf(name, (size_t) size, "%s", c);
    return (int) strlen(name);
}

/* Another thread's window of the program would be sent WM_GETTEXT: never;
   nor the program's own frame, whose window procedure would run inside a
   Glide call (only the Glide window's, at the open). */
int STDCALL
GetWindowTextA(DWORD hwnd, char *text, int size)
{
    CHECK(hwnd != TEST_OTHER_HWND, "a title read from another thread of the program");
    CHECK(hwnd != TEST_FRAME_HWND, "a title read from the program's own frame when the mode was lost");
    CHECK(!m.in_dllmain, "GetWindowTextA inside DllMain");
    m.window_queries++;
    if (size < 1)
        return 0;
    snprintf(text, (size_t) size, "%s", (hwnd == TEST_HWND) ? "Test Game" : "");
    return (int) strlen(text);
}

BOOL STDCALL
GetWindowRect(DWORD hwnd, glide_rect_t *r)
{
    CHECK(!m.in_dllmain, "GetWindowRect inside DllMain");
    m.window_queries++;
    r->left   = 0;
    r->top    = (hwnd == TEST_TRAY_HWND) ? 572 : 0;
    r->right  = (hwnd == TEST_TRAY_HWND) ? 800 : 640;
    r->bottom = (hwnd == TEST_TRAY_HWND) ? 600 : 480;
    return 1;
}

LONG STDCALL
GetWindowLongA(DWORD hwnd, int index)
{
    CHECK(index == GWL_EXSTYLE, "GetWindowLongA(%d)", index);
    CHECK(!m.in_dllmain, "GetWindowLongA inside DllMain");
    m.window_queries++;
    return (hwnd == TEST_HWND) ? WS_EX_TOPMOST : (hwnd == TEST_TRAY_HWND) ? 0x88 : 0;
}

DWORD STDCALL
GetWindowThreadProcessId(DWORD hwnd, DWORD *process)
{
    CHECK(!m.in_dllmain, "GetWindowThreadProcessId inside DllMain");
    m.window_queries++;
    if (process)
        *process = (hwnd == TEST_TRAY_HWND) ? 0x12340 : 0xfff12345;
    return (hwnd == TEST_OTHER_HWND) ? 0x99 : (hwnd == TEST_TRAY_HWND) ? 0x55 : 0x77;
}

/* --- DirectDraw: the methods the DLL calls, by their place ------------------- */

typedef struct fake_com {
    void **vtbl;
    int    surface;
} fake_com_t;

static void      *dd_vtbl[24];
static void      *dds_vtbl[36];
static fake_com_t dd_object  = { dd_vtbl, 0 };
static fake_com_t dds_object = { dds_vtbl, 1 };

static HRESULT STDCALL
dd_query(void *self, const void *iid, void **out)
{
    (void) iid;
    m.dd_objects++;
    *out = self; /* IDirectDraw2 */
    return DD_OK;
}

static DWORD STDCALL
dd_release(void *self)
{
    (void) self;
    m.dd_objects--;
    return 0;
}

static HRESULT STDCALL
dd_coop(void *self, DWORD hwnd, DWORD flags)
{
    (void) self;
    m.coop_hwnd  = hwnd;
    m.coop_flags = flags;
    return DD_OK;
}

static HRESULT STDCALL
dd_mode(void *self, DWORD w, DWORD h, DWORD bpp, DWORD hz, DWORD flags)
{
    (void) self;
    (void) flags;
    if (m.mode_refuse)
        return DDERR_GENERIC;
    m.mode_w   = w;
    m.mode_h   = h;
    m.mode_bpp = bpp;
    m.mode_hz  = hz;
    m.modes_set++;
    m.sb->mode_serial++;
    return DD_OK;
}

static HRESULT STDCALL
dd_restore(void *self)
{
    (void) self;
    m.restores++;
    return DD_OK;
}

static HRESULT STDCALL
dd_create(void *self, DDSURFACEDESC *desc, void **surface, void *outer)
{
    (void) self;
    (void) outer;
    CHECK((desc->dwSize == sizeof(*desc)) && (desc->ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY) &&
              (desc->ddpfPixelFormat.dwRGBBitCount == 32),
          "the staging surface's description");
    /* A pitch of the width's bytes, after the first megabyte. */
    m.surface_offset = 0x100000;
    m.surface_pitch  = desc->dwWidth * 4;
    CHECK(m.surface_offset + m.surface_pitch * desc->dwHeight <= TEST_VRAM_BYTES - VGPU98_GPU_RESERVE,
          "the staging surface does not fit video memory");
    m.dd_objects++;
    *surface = &dds_object;
    return DD_OK;
}

static HRESULT STDCALL
dds_lock(void *self, void *rect, DDSURFACEDESC *desc, DWORD flags, DWORD event)
{
    (void) self;
    (void) rect;
    (void) flags;
    (void) event;
    desc->lpSurface = (DWORD) (uintptr_t) (m.vram + m.surface_offset);
    desc->lPitch    = (LONG) m.surface_pitch;
    return DD_OK;
}

static HRESULT STDCALL
dds_unlock(void *self, void *data)
{
    (void) self;
    (void) data;
    return DD_OK;
}

HRESULT STDCALL
DirectDrawCreate(void *guid, void **dd, void *outer)
{
    (void) guid;
    (void) outer;
    dd_vtbl[0]   = (void *) dd_query;
    dd_vtbl[2]   = (void *) dd_release;
    dd_vtbl[6]   = (void *) dd_create;
    dd_vtbl[19]  = (void *) dd_restore;
    dd_vtbl[20]  = (void *) dd_coop;
    dd_vtbl[21]  = (void *) dd_mode;
    dds_vtbl[2]  = (void *) dd_release;
    dds_vtbl[25] = (void *) dds_lock;
    dds_vtbl[32] = (void *) dds_unlock;
    m.dd_objects++;
    *dd = &dd_object;
    return DD_OK;
}

/* --- Checks ------------------------------------------------------------------- */

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

#ifndef TEST_GLIDE3
static int
line_cut(const char *text)
{
    int i;

    for (i = 0; i < n_lines; i++) {
        if (strstr(lines[i], text))
            return cut[i];
    }
    return 0;
}
#endif

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

#define EXPECT(text)   CHECK(has_line(text), "no line with \"%s\"", text)
#define EXPECT_NO(text) CHECK(!has_line(text), "a line with \"%s\"", text)

/* The record's names and counts, in order, across its lines (heading
   "done:" or "nothing yet:", then ", more:" lines). */
static void
expect_group(const char *heading, const char *items)
{
    char all[1024] = "";
    int  i;

    for (i = 0; i < n_lines; i++) {
        const char *at = strstr(lines[i], heading);

        if (!at)
            continue;
        at += strlen(heading);
        if (!strncmp(at, ", more:", 7))
            at += 7;
        else if (*at == ':')
            at++;
        else
            continue;
        if (*all)
            strcat(all, ",");
        strcat(all, at);
    }
    CHECK(!strcmp(all, items), "the record's \"%s\": \"%s\", expected \"%s\"", heading, all, items);
}

/* --- Scenarios ------------------------------------------------------------- */

static void
machine(DWORD debug)
{
    uint8_t *low = test_low_alloc(0x100000 + TEST_VRAM_BYTES);

    if (!low) {
        fprintf(stderr, "test_glide: no memory below 4 GB\n");
        exit(1);
    }
    memset(&m, 0, sizeof(m));
    m.program     = "C:\\GAMES\\TEST\\TEST.EXE";
#ifdef TEST_GLIDE3
    m.dll         = "C:\\WINDOWS\\SYSTEM\\GLIDE3X.DLL";
#else
    m.dll         = "C:\\WINDOWS\\SYSTEM\\GLIDE2X.DLL";
#endif
    m.env         = environment;
    m.escape      = 1;
    m.answer_size = sizeof(vgpu98_escinfo_t);
    m.stamp       = VGPU98_BUILD_STAMP;
    m.sb_version  = SB_VERSION;
    m.debug       = debug;
    m.sb          = (vgpu98_shared_t *) low;
    m.regs        = (volatile DWORD *) (low + 0x10000);
    m.vram        = low + 0x100000;
    m.sb->magic   = SB_MAGIC;
    m.sb->version = SB_VERSION;
    m.sb->size    = sizeof(vgpu98_shared_t);
    m.sb->regs_linear  = (DWORD) (uintptr_t) m.regs;
    m.sb->fb_linear    = (DWORD) (uintptr_t) m.vram;
    m.sb->vram_size    = TEST_VRAM_BYTES;
    m.sb->reserve_base = TEST_VRAM_BYTES - VGPU98_GPU_RESERVE;
    m.sb->mode_serial  = 1;
    m.regs[VGPU98_REG_ID / 4]    = VGPU98_ID_MAGIC;
    m.regs[VGPU98_REG_DEBUG / 4] = debug;
    /* The card of an 86Box before Glide drew, unless a scenario draws:
       every window refused. */
    m.regs[VGPU98_REG_PROFILE / 4] = VGPU98_PROFILE & ~(DWORD) VGPU98_PROFILE_GLIDE;
    DllMain(TEST_INSTANCE, 1, NULL);
}

/* A card that draws Glide. */
static void
machine_glide(DWORD debug)
{
    machine(debug);
    m.regs[VGPU98_REG_PROFILE / 4] = VGPU98_PROFILE;
}

/* The n-th vertex of a DRAW, as floats. */
static const float *
draw_vertex(const pkt_t *k, int n)
{
    return (const float *) &k->p[VGPU98_GL_LEN_DRAW + n * VGPU98_GL_VERTEX_DWORDS];
}

static int
near_f(float a, float b)
{
    return (a - b < 0.001f) && (b - a < 0.001f);
}

/* The STATE packets' last value of register reg, or `none`. */
static DWORD
reg_sent(DWORD reg, DWORD none)
{
    DWORD v = none;
    int   i;

    for (i = 0; i < m.n_pkts; i++) {
        const pkt_t *q = &m.pkts[i];

        if ((q->op == VGPU98_GL_STATE) && (q->p[1] <= reg) && (reg < q->p[1] + q->len - 2) && (2 + reg - q->p[1] < PKT_KEPT))
            v = q->p[2 + reg - q->p[1]];
    }
    return v;
}

#ifndef TEST_GLIDE3
/* Every export that needs no window, called without one, or
   without the card: none reaches for one. */
static void
screen_calls(void)
{
    glc_lfb_info_t info;
    uint16_t       px[4] = { 0 };

    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    CHECK(grSstIsBusy() == FXFALSE, "busy without a window");
    (void) grSstStatus();
    (void) grSstVRetraceOn();
    CHECK(grSstVideoLine() == 1, "grSstVideoLine");
    (void) grSstScreenWidth();
    (void) grSstScreenHeight();
    CHECK(grBufferNumPending() == 0, "swaps pending without a window");
    grBufferClear(0, 0, 0);
    grBufferSwap(1);
    grRenderBuffer(GR_BUFFER_FRONTBUFFER);
    grClipWindow(0, 0, 640, 480);
    grSstOrigin(GR_ORIGIN_LOWER_LEFT);
    grColorMask(FXTRUE, FXTRUE);
    grDepthMask(FXTRUE);
    grGammaCorrectionValue(1.7f);
    CHECK(grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_ANY, GR_ORIGIN_ANY, FXFALSE,
                    (GrLfbInfo_t *) &info) == FXFALSE,
          "a lock without a window");
    CHECK(grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXFALSE, "an unlock without a window");
    CHECK(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 0, 0, 2, 2, 4, px) == FXFALSE, "a region read without a window");
    CHECK(grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 0, 0, 0, 2, 2, 4, px) == FXFALSE, "a region write without a window");
    grLfbConstantAlpha(0x80);
    grLfbConstantDepth(0x1234);
    grLfbWriteColorFormat(GR_COLORFORMAT_ABGR);
    grLfbWriteColorSwizzle(FXTRUE, FXFALSE);
    guFbReadRegion(0, 0, 2, 2, px, 4);
    guFbWriteRegion(0, 0, 2, 2, px, 4);
    grSstIdle();
    grSstWinClose();
}

/* A short Glide 2 session: the board, a window asked for five times, a
   few calls of one that does nothing, then shutdown and unload. */
static void
session(GrHwConfiguration *hwc)
{
    char version[80];
    int  i;

    grGlideGetVersion(version);
    CHECK(!strcmp(version, "2.61.00.0658"), "grGlideGetVersion: %s", version);
    grSstQueryBoards(hwc);
    grGlideInit();
    grSstQueryHardware(hwc);
    for (i = 0; i < 5; i++)
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXFALSE, "grSstWinOpen was not refused");
    for (i = 0; i < 3; i++)
        guMovieStart(); /* does nothing, as a Voodoo3's does */
    for (i = 0; i < 2; i++)
        grBufferSwap(1);
    grGlideShutdown();
    DllMain(TEST_INSTANCE, 0, NULL);
}

/* Glide 2's numbers the draws use (the SDK's). */
#define GR_BLEND_ZERO                 0x0
#define GR_BLEND_SRC_ALPHA            0x1
#define GR_BLEND_ONE                  0x4
#define GR_BLEND_ONE_MINUS_SRC_ALPHA  0x5
#define GR_CULL_DISABLE               0
#define GR_CULL_NEGATIVE              1
#define GR_CULL_POSITIVE              2
#define GR_COLORCOMBINE_ITRGB         2
#define GR_COLORCOMBINE_ITRGB_DELTA0  3
#define GR_COMBINE_FUNCTION_LOCAL     1
#define GR_COMBINE_FACTOR_NONE        0
#define TEST_FN_ZERO                  0 /* GR_COMBINE_FUNCTION_ZERO */
#define TEST_FN_SCALE_OTHER           3 /* GR_COMBINE_FUNCTION_SCALE_OTHER */
#define TEST_FAC_LOCAL                1 /* GR_COMBINE_FACTOR_LOCAL */
#define TEST_FAC_ONE                  8 /* GR_COMBINE_FACTOR_ONE */
#define GR_COMBINE_LOCAL_ITERATED     0
#define GR_COMBINE_OTHER_NONE         2
#define GR_FOG_WITH_TABLE             2

/* GrVertex as a Glide 2 program lays it out (60 bytes). */
typedef struct test_vertex {
    float x, y, z;
    float r, g, b;
    float ooz;
    float a;
    float oow;
    struct {
        float sow, tow, oow;
    } tmu[2];
} test_vertex_t;

/* Glide 2's draws. */
static void
draws(void)
{
    test_vertex_t v[3];
    const pkt_t  *k;
    const float  *f;
    FxU8          fog[64];
    int           draws0, i;

    memset(v, 0, sizeof(v));
    v[0].x = 10.0f;
    v[0].y = 10.0f;
    v[1].x = 20.0f;
    v[1].y = 10.0f;
    v[2].x = 10.0f;
    v[2].y = 20.0f;
    for (i = 0; i < 3; i++) {
        v[i].r          = 255.0f;
        v[i].g          = 128.0f;
        v[i].a          = 64.0f;
        v[i].ooz        = 100.0f;
        v[i].oow        = 0.5f;
        v[i].tmu[0].sow = 1.0f;
        v[i].tmu[0].tow = 2.0f;
        v[i].tmu[0].oow = 0.25f;
        v[i].tmu[1].sow = 7.0f;
        v[i].tmu[1].tow = 8.0f;
        v[i].tmu[1].oow = 0.125f;
    }
    /* One triangle: the device's vertex; TMU0's 1/w the vertex's, TMU1's s
       and t TMU0's, without the hint. */
    grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
    grSstIdle();
    k = last_pkt(VGPU98_GL_DRAW);
    CHECK(k && (k->p[1] == VGPU98_GL_TRIANGLES) && (k->p[2] == 3) && (k->len == VGPU98_GL_LEN_DRAW + 3 * 16), "a DRAW of 3");
    f = k ? draw_vertex(k, 1) : NULL;
    CHECK(f && near_f(f[VGPU98_GLV_X], 20.0f) && near_f(f[VGPU98_GLV_Y], 10.0f) && near_f(f[VGPU98_GLV_OOZ], 100.0f) &&
              near_f(f[VGPU98_GLV_OOW], 0.5f) && near_f(f[VGPU98_GLV_R], 255.0f) && near_f(f[VGPU98_GLV_G], 128.0f) &&
              near_f(f[VGPU98_GLV_A], 64.0f) && near_f(f[VGPU98_GLV_SOW0], 1.0f) && near_f(f[VGPU98_GLV_TOW0], 2.0f) &&
              near_f(f[VGPU98_GLV_OOW0], 0.5f) && near_f(f[VGPU98_GLV_SOW1], 1.0f) && near_f(f[VGPU98_GLV_TOW1], 2.0f) &&
              near_f(f[VGPU98_GLV_OOW1], 0.5f),
          "the vertex without the STW hint");
    /* With the hint and both TMUs active: each TMU's own (a Voodoo3 sends an
       inactive TMU none of its own). */
    grTexCombine(0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
                 FXFALSE);
    grTexCombine(1, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
                 FXFALSE);
    grHints(0, 0x02 | 0x08 | 0x10);
    grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
    grSstIdle();
    f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
    CHECK(f && near_f(f[VGPU98_GLV_OOW0], 0.25f) && near_f(f[VGPU98_GLV_SOW1], 7.0f) && near_f(f[VGPU98_GLV_TOW1], 8.0f) &&
              near_f(f[VGPU98_GLV_OOW1], 0.125f),
          "the vertex with the STW hint");
    /* The hints for TMUs that are not active: none applies. */
    grTexCombine(0, TEST_FN_ZERO, GR_COMBINE_FACTOR_NONE, TEST_FN_ZERO, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    grTexCombine(1, TEST_FN_ZERO, GR_COMBINE_FACTOR_NONE, TEST_FN_ZERO, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
    grSstIdle();
    f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
    CHECK(f && near_f(f[VGPU98_GLV_OOW0], 0.5f) && near_f(f[VGPU98_GLV_SOW1], 1.0f) && near_f(f[VGPU98_GLV_OOW1], 0.5f),
          "an inactive TMU's hints applied");
    grHints(0, 0);
    /* Each TMU's coordinates as a Voodoo3's setup unit receives them.
       TMU1's own 1/w holds garbage, as a
       program leaves it unwritten when it does not hint it. */
    {
        test_vertex_t q[3];
        const float   junk = 3.0e-37f;

        memcpy(q, v, sizeof(q));
        for (i = 0; i < 3; i++)
            q[i].tmu[1].oow = junk;
        /* TMU0 passing TMU1 through, no hint: TMU1's own s and t, the
           vertex's 1/w. */
        grTexCombine(0, TEST_FN_SCALE_OTHER, TEST_FAC_ONE, TEST_FN_SCALE_OTHER, TEST_FAC_ONE, FXFALSE, FXFALSE);
        grTexCombine(1, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE,
                     FXFALSE);
        grDrawTriangle((GrVertex *) &q[0], (GrVertex *) &q[1], (GrVertex *) &q[2]);
        grSstIdle();
        f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
        CHECK(f && (f[VGPU98_GLV_SOW1] == 7.0f) && (f[VGPU98_GLV_TOW1] == 8.0f) && (f[VGPU98_GLV_OOW1] == 0.5f),
              "TMU1 passed through without the hint: s %f, 1/w %g", f ? (double) f[VGPU98_GLV_SOW1] : 0.0,
              f ? (double) f[VGPU98_GLV_OOW1] : 0.0);
        /* With TMU1's 1/w hinted: its own. */
        grHints(0, 0x08);
        grDrawTriangle((GrVertex *) &q[0], (GrVertex *) &q[1], (GrVertex *) &q[2]);
        grSstIdle();
        f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
        CHECK(f && (f[VGPU98_GLV_OOW1] == junk), "TMU1's hinted 1/w not its own");
        /* TMU0 inactive by ZERO: TMU1's own s and t. */
        grHints(0, 0);
        grTexCombine(0, TEST_FN_ZERO, GR_COMBINE_FACTOR_NONE, TEST_FN_ZERO, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
        grDrawTriangle((GrVertex *) &q[0], (GrVertex *) &q[1], (GrVertex *) &q[2]);
        grSstIdle();
        f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
        CHECK(f && (f[VGPU98_GLV_SOW1] == 7.0f) && (f[VGPU98_GLV_OOW1] == 0.5f), "TMU1 with TMU0 combining ZERO");
        /* Both active (TMU0 by its LOCAL factor), TMU0's 1/w hinted and
           TMU1's not: TMU1 takes TMU0's, and TMU0's s and t. */
        grTexCombine(0, TEST_FN_SCALE_OTHER, TEST_FAC_LOCAL, TEST_FN_SCALE_OTHER, TEST_FAC_LOCAL, FXFALSE, FXFALSE);
        grHints(0, 0x02);
        grDrawTriangle((GrVertex *) &q[0], (GrVertex *) &q[1], (GrVertex *) &q[2]);
        grSstIdle();
        f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
        CHECK(f && (f[VGPU98_GLV_OOW0] == 0.25f) && (f[VGPU98_GLV_OOW1] == 0.25f) && (f[VGPU98_GLV_SOW1] == 1.0f),
              "TMU1 after an active TMU0 with its 1/w hinted");
        /* TMU0 passing through with its 1/w hinted: not sent, so TMU1 takes
           the vertex's. */
        grTexCombine(0, TEST_FN_SCALE_OTHER, TEST_FAC_ONE, TEST_FN_SCALE_OTHER, TEST_FAC_ONE, FXFALSE, FXFALSE);
        grDrawTriangle((GrVertex *) &q[0], (GrVertex *) &q[1], (GrVertex *) &q[2]);
        grSstIdle();
        f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
        CHECK(f && (f[VGPU98_GLV_OOW1] == 0.5f) && (f[VGPU98_GLV_OOW0] == 0.5f), "TMU1 after a passing TMU0 with its 1/w hinted");
        grHints(0, 0);
        /* The active rule's details: TMU0's combine alone decides, seen
           through TMU1's s (TMU1 active, no hint): its own (7) while TMU0 is
           not active, TMU0's (1) while it is. */
        {
            static const struct {
                int rgb_fn, rgb_fac, a_fn, a_fac, active;
            } c[] = {
                { 0, 9, 0, 0, 1 },    /* colour ZERO by ONE_MINUS_LOCAL */
                { 0, 0, 3, 0xb, 1 },  /* alpha SCALE_OTHER by ONE_MINUS_LOCAL_ALPHA */
                { 0, 0, 0x10, 0, 1 }, /* alpha SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA */
                { 0, 0, 9, 0, 1 },    /* alpha SCALE_MINUS_LOCAL_ADD_LOCAL */
                { 3, 3, 0, 0, 1 },    /* colour SCALE_OTHER by LOCAL_ALPHA */
                { 3, 4, 3, 4, 0 },    /* SCALE_OTHER by DETAIL */
                { 3, 5, 3, 5, 0 },    /* by LOD_FRACTION */
                { 3, 2, 3, 2, 0 },    /* by OTHER_ALPHA */
                { 0xa, 0, 0, 0, 0 },  /* a function Glide has not */
            };
            size_t n;

            for (n = 0; n < sizeof(c) / sizeof(c[0]); n++) {
                grTexCombine(0, c[n].rgb_fn, c[n].rgb_fac, c[n].a_fn, c[n].a_fac, FXFALSE, FXFALSE);
                grDrawTriangle((GrVertex *) &q[0], (GrVertex *) &q[1], (GrVertex *) &q[2]);
                grSstIdle();
                f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
                CHECK(f && (f[VGPU98_GLV_SOW1] == (c[n].active ? 1.0f : 7.0f)), "TMU0's combine %d %d %d %d taken as %sactive",
                      c[n].rgb_fn, c[n].rgb_fac, c[n].a_fn, c[n].a_fac, c[n].active ? "not " : "");
            }
        }
        grTexCombine(0, TEST_FN_ZERO, GR_COMBINE_FACTOR_NONE, TEST_FN_ZERO, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
        grTexCombine(1, TEST_FN_ZERO, GR_COMBINE_FACTOR_NONE, TEST_FN_ZERO, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    }

    /* Two triangles with nothing between: one DRAW. A state change between:
       two, the STATE between them. */
    draws0 = count_pkts(VGPU98_GL_DRAW);
    grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
    grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
    grSstIdle();
    CHECK((count_pkts(VGPU98_GL_DRAW) == draws0 + 1) && (last_pkt(VGPU98_GL_DRAW)->p[2] == 6),
          "two triangles not one DRAW: %d DRAWs", count_pkts(VGPU98_GL_DRAW) - draws0);
    draws0 = count_pkts(VGPU98_GL_DRAW);
    grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
    grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
    grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
    grSstIdle();
    CHECK(count_pkts(VGPU98_GL_DRAW) == draws0 + 2, "a state change inside one DRAW");
    k = last_pkt(VGPU98_GL_STATE);
    CHECK(k && (k->p[1] == VGPU98_GS_BLEND) && (k->p[2] == (1 | (5 << 8) | (4 << 16))), "the blend's register");

    /* Culling: positive areas (clockwise on an upper-left screen) dropped
       by GR_CULL_POSITIVE, kept by GR_CULL_NEGATIVE. */
    draws0 = count_pkts(VGPU98_GL_DRAW);
    grCullMode(GR_CULL_POSITIVE);
    grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
    grSstIdle();
    CHECK(count_pkts(VGPU98_GL_DRAW) == draws0, "a positive triangle drawn with GR_CULL_POSITIVE");
    grCullMode(GR_CULL_NEGATIVE);
    grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
    grSstIdle();
    CHECK(count_pkts(VGPU98_GL_DRAW) == draws0 + 1, "a positive triangle dropped with GR_CULL_NEGATIVE");
    grCullMode(GR_CULL_DISABLE);
    /* Not a number: nothing drawn. */
    draws0 = count_pkts(VGPU98_GL_DRAW);
    v[1].x = 0.0f / 0.0f;
    grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
    v[1].x = 20.0f;
    grSstIdle();
    CHECK(count_pkts(VGPU98_GL_DRAW) == draws0, "a vertex not a number drawn");

    /* A point: a small triangle over the pixel its position is in. */
    v[0].x = 5.7f;
    v[0].y = 6.2f;
    grDrawPoint((GrVertex *) &v[0]);
    grSstIdle();
    k = last_pkt(VGPU98_GL_DRAW);
    CHECK(k && (k->p[2] == 3) && near_f(draw_vertex(k, 0)[0], 5.0f) && near_f(draw_vertex(k, 0)[1], 6.0f) &&
              near_f(draw_vertex(k, 1)[0], 6.5f) && near_f(draw_vertex(k, 2)[1], 7.5f),
          "the point's triangle");
    /* An x-major line: a parallelogram half a pixel each way in y. */
    v[0].x = 0.0f;
    v[0].y = 0.0f;
    v[1].x = 10.0f;
    v[1].y = 2.0f;
    grDrawLine((GrVertex *) &v[0], (GrVertex *) &v[1]);
    grSstIdle();
    k = last_pkt(VGPU98_GL_DRAW);
    CHECK(k && (k->p[2] == 6) && near_f(draw_vertex(k, 0)[1], -0.5f) && near_f(draw_vertex(k, 1)[1], 1.5f) &&
              near_f(draw_vertex(k, 2)[1], 2.5f) && near_f(draw_vertex(k, 5)[1], 0.5f),
          "the line's triangles");
    v[0].x = 10.0f;
    v[0].y = 10.0f;
    v[1].x = 20.0f;
    v[1].y = 10.0f;

    /* Delta0: the flat colour of grConstantColorValue4 instead of the
       vertices'. */
    grConstantColorValue4(9.0f, 1.0f, 2.0f, 3.0f);
    guColorCombineFunction(GR_COLORCOMBINE_ITRGB_DELTA0);
    grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
    grSstIdle();
    f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 2) : NULL;
    CHECK(f && near_f(f[VGPU98_GLV_R], 1.0f) && near_f(f[VGPU98_GLV_G], 2.0f) && near_f(f[VGPU98_GLV_B], 3.0f) &&
              near_f(f[VGPU98_GLV_A], 64.0f),
          "delta0's flat colour");
    /* grColorCombine keeps delta0, as a Voodoo3's Glide 2 does; a preset
       turns it off. */
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE,
                   FXFALSE);
    grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
    grSstIdle();
    f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 2) : NULL;
    CHECK(f && near_f(f[VGPU98_GLV_R], 1.0f), "delta0 not kept after grColorCombine");
    guColorCombineFunction(GR_COLORCOMBINE_ITRGB);
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE,
                   FXFALSE);
    grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
    grSstIdle();
    f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 2) : NULL;
    CHECK(f && near_f(f[VGPU98_GLV_R], 255.0f), "delta0 kept after a preset without it");

    /* The utility library's clipper: a twisted polygon of 100 vertices
       crossing the clip window draws without running past its room. */
    {
        static test_vertex_t poly[100];

        for (i = 0; i < 100; i++) {
            poly[i]     = v[0];
            poly[i].x   = (i & 1) ? 700.0f : -50.0f;
            poly[i].y   = (float) (i * 4);
            poly[i].oow = 1.0f;
        }
        grClipWindow(0, 0, 640, 480);
        guDrawPolygonVertexListWithClip(100, (GrVertex *) poly);
        grSstIdle();
        CHECK(m.n_pkts > 0, "nothing after a twisted polygon");
    }

    /* The fog table: the utility library's, into the 16 registers. */
    guFogGenerateExp(fog, 0.01f);
    CHECK((fog[0] < fog[32]) && (fog[63] == 255), "guFogGenerateExp: %u %u %u", fog[0], fog[32], fog[63]);
    for (i = 1; i < 64; i++)
        CHECK(fog[i] >= fog[i - 1], "the exponential table falls at %d", i);
    guFogGenerateLinear(fog, 10.0f, 1000.0f);
    CHECK((fog[0] == 0) && (fog[63] == 255), "guFogGenerateLinear: %u %u", fog[0], fog[63]);
    grFogTable(fog);
    grFogMode(GR_FOG_WITH_TABLE);
    grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
    grSstIdle();
    {
        int found = 0;

        for (i = 0; i < m.n_pkts; i++) {
            const pkt_t *q = &m.pkts[i];

            if ((q->op == VGPU98_GL_STATE) && (q->p[1] <= VGPU98_GS_FOG) &&
                (q->p[1] + q->len - 2 > VGPU98_GS_FOG_TABLE + 15)) {
                found = (q->p[2 + VGPU98_GS_FOG - q->p[1]] == 2) &&
                        (q->p[2 + VGPU98_GS_FOG_TABLE + 15 - q->p[1]] ==
                         ((DWORD) fog[60] | ((DWORD) fog[61] << 8) | ((DWORD) fog[62] << 16) | ((DWORD) fog[63] << 24)));
            }
        }
        CHECK(found, "the fog mode and table's registers");
    }

    /* Drawing into the front buffer goes to the device without a swap. */
    grRenderBuffer(GR_BUFFER_FRONTBUFFER);
    draws0 = count_pkts(VGPU98_GL_DRAW);
    grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
    CHECK(count_pkts(VGPU98_GL_DRAW) == draws0 + 1, "drawing into the front buffer not sent");
    grRenderBuffer(GR_BUFFER_BACKBUFFER);
}

/* GrTexInfo, Glide 2's numbers. */
typedef struct test_texinfo {
    FxI32 small_lod, large_lod, aspect, format;
    void *data;
} test_texinfo_t;

/* Gu3dfInfo (1056 bytes). */
typedef struct test_3df {
    FxU32  width, height;
    FxI32  small_lod, large_lod, aspect, format;
    FxU32  table[256];
    void  *data;
    FxU32  mem_required;
} test_3df_t;

/* Glide 2's textures, a window open at
   640x480 with two colour buffers and a depth buffer. */
static void
textures(void)
{
    static uint16_t texels[5461];
    static DWORD    pal[256];
    test_texinfo_t  info;
    test_3df_t      d3;
    uint16_t        loaded[11];
    const pkt_t    *k;
    FILE           *f;
    int             i, n;

    /* Each TMU: the board's 16 MB less the three buffers (rows of 640
       pixels, 4 KB pages) and the FIFO's 256 KB, halved. */
    CHECK(grTexMinAddress(0) == 0, "grTexMinAddress");
    CHECK(grTexMaxAddress(1) == (16777216u - 3 * 614400u - 262144u) / 2 - 16, "grTexMaxAddress %u", grTexMaxAddress(1));
    /* The Programming Guide's example: 128x64 down to 8x4, 8-bit. */
    info.small_lod = 5;
    info.large_lod = 1;
    info.aspect    = 2;
    info.format    = 0x3;
    CHECK(grTexTextureMemRequired(3, (GrTexInfo *) &info) == 10912, "grTexTextureMemRequired %u",
          grTexTextureMemRequired(3, (GrTexInfo *) &info));
    CHECK(grTexCalcMemRequired(1, 5, 2, 0x3) == 10912, "grTexCalcMemRequired");

    /* A 64 mipmap of 565 down to 1x1: each level at its place. */
    for (i = 0; i < 5461; i++)
        texels[i] = (uint16_t) (i * 7);
    info.small_lod = 8;
    info.large_lod = 2;
    info.aspect    = 3;
    info.format    = 0xa;
    info.data      = texels;
    n              = count_pkts(VGPU98_GL_TEXDATA);
    grTexDownloadMipMap(0, 0x1000, 3, (GrTexInfo *) &info);
    grSstIdle();
    k = nth_pkt(VGPU98_GL_TEXDATA, n);
    CHECK(k && (k->p[1] == 0) && (k->p[2] == 0x1000) && (k->p[3] == 8192) && (k->p[4] == ((DWORD) texels[0] | ((DWORD) texels[1] << 16))),
          "the 64 level's TEXDATA");
    k = nth_pkt(VGPU98_GL_TEXDATA, n + 1);
    CHECK(k && (k->p[2] == 0x1000 + 8192) && (k->p[3] == 2048) && (k->p[4] == ((DWORD) texels[4096] | ((DWORD) texels[4097] << 16))),
          "the 32 level's TEXDATA: at %x, %u bytes", k ? k->p[2] : 0, k ? k->p[3] : 0);
    CHECK(count_pkts(VGPU98_GL_TEXDATA) == n + 7, "%d levels sent, expected 7", count_pkts(VGPU98_GL_TEXDATA) - n);
    /* One row of a level: at its row, its data from the first row given. */
    n = count_pkts(VGPU98_GL_TEXDATA);
    grTexDownloadMipMapLevelPartial(1, 0x1000, 3, 2, 3, 0xa, 3, &texels[100], 5, 5);
    grSstIdle();
    k = nth_pkt(VGPU98_GL_TEXDATA, n);
    CHECK(k && (k->p[1] == 1) && (k->p[2] == 0x1000 + 8192 + 5 * 64) && (k->p[3] == 64) && ((k->p[4] & 0xffff) == texels[100]),
          "a partial level's TEXDATA");
    /* The source: the TMU's registers. */
    grTexSource(0, 0x1000, 3, (GrTexInfo *) &info);
    grSstIdle();
    CHECK((reg_sent(VGPU98_GS_TMU(0) + VGPU98_GS_TEX_ADDRESS, 0) == 0x1000) &&
              (reg_sent(VGPU98_GS_TMU(0) + VGPU98_GS_TEX_INFO, 0) == VGPU98_GS_TEXINFO(0xa, 0, 6, 0, 3, 0)) &&
              (reg_sent(VGPU98_GS_TMU(0) + VGPU98_GS_TEX_SOURCE, 0) == 1),
          "grTexSource's registers: %x", reg_sent(VGPU98_GS_TMU(0) + VGPU98_GS_TEX_INFO, 0));
    grTexFilterMode(0, 1, 1);
    grTexClampMode(0, 0, 1);
    grTexMipMapMode(0, 1, FXFALSE);
    grTexLodBiasValue(0, 0.6f);
    grTexDetailControl(0, -4, 3, 0.5f);
    grTexCombineFunction(1, 9); /* TRILINEAR_EVEN */
    grSstIdle();
    {
        union {
            DWORD d;
            float f;
        } bias;

        bias.d = reg_sent(VGPU98_GS_TMU(0) + VGPU98_GS_TEX_LOD_BIAS, 0);
        CHECK((reg_sent(VGPU98_GS_TMU(0) + VGPU98_GS_TEX_FILTER, 9) == (1 | (1 << 8))) &&
                  (reg_sent(VGPU98_GS_TMU(0) + VGPU98_GS_TEX_CLAMP, 9) == (0 | (1 << 8))) &&
                  (reg_sent(VGPU98_GS_TMU(0) + VGPU98_GS_TEX_MIPMAP, 9) == 1) && near_f(bias.f, 0.5f) &&
                  (reg_sent(VGPU98_GS_TMU(0) + VGPU98_GS_TEX_DETAIL, 0) == (0xfc | (3 << 8) | (127 << 16))) &&
                  (reg_sent(VGPU98_GS_TMU(1) + VGPU98_GS_TEX_COMBINE, 0) == (7 | (5 << 8) | (7 << 16) | (5 << 24))),
              "a TMU's settings");
    }

    /* The palette to both TMUs, RGB alone; part of it, from the whole
       table's pointer. */
    for (i = 0; i < 256; i++)
        pal[i] = 0xff000000u | (DWORD) i * 0x010203u;
    grTexDownloadTable(0, 2, pal);
    grTexDownloadTablePartial(1, 2, pal, 10, 12);
    grSstIdle();
    k = nth_pkt(VGPU98_GL_PALETTE, 0);
    CHECK(k && (k->p[1] == 2) && (k->p[2] == (256u << 16)) && (k->p[3 + 200] == (pal[200] & 0xffffff)), "the palette");
    k = nth_pkt(VGPU98_GL_PALETTE, 1);
    CHECK(k && (k->p[2] == (10 | (3u << 16))) && (k->len == VGPU98_GL_LEN_PALETTE + 3) && (k->p[3] == (pal[10] & 0xffffff)),
          "part of the palette");

    /* The utility library's manager: bump allocations, the settings at
       guTexSource. */
    CHECK(guTexAllocateMemory(0, 3, 64, 64, 0xa, 1, 8, 2, 3, 1, 1, 1, 1, 0.0f, FXFALSE) == 0, "the first mipmap's id");
    CHECK(guTexMemQueryAvail(0) == grTexMaxAddress(0) + 16 - 10928, "guTexMemQueryAvail %u", guTexMemQueryAvail(0));
    CHECK(guTexAllocateMemory(0, 3, 64, 64, 0xa, 1, 8, 2, 3, 1, 1, 1, 1, 0.4f, FXFALSE) == 1, "the second mipmap's id");
    guTexDownloadMipMap(1, texels, NULL);
    guTexSource(1);
    grSstIdle();
    CHECK((reg_sent(VGPU98_GS_TMU(0) + VGPU98_GS_TEX_ADDRESS, 0) == 10928) && (guTexGetCurrentMipMap(0) == 1),
          "guTexSource: at %u", reg_sent(VGPU98_GS_TMU(0) + VGPU98_GS_TEX_ADDRESS, 0));
    {
        union {
            DWORD d;
            float f;
        } bias;

        bias.d = reg_sent(VGPU98_GS_TMU(0) + VGPU98_GS_TEX_LOD_BIAS, 0);
        CHECK(near_f(bias.f, 0.5f), "the manager's LOD bias 0.4 as %f, not 0.5", bias.f);
    }
    guTexMemReset();
    CHECK(guTexGetCurrentMipMap(0) == (GrMipMapId_t) -1, "guTexMemReset kept the current mipmap");

    /* A .3df file: 565 from 4x2 down to 1x1, 2:1, its texels big-endian. */
    f = fopen("test_glide.3df", "wb");
    CHECK(f != NULL, "no .3df file made");
    if (f) {
        fputs("3df v1.0\nrgb565\nlod range: 1 4\naspect ratio: 2 1\n", f);
        for (i = 0; i < 11; i++) {
            fputc(0x12, f);
            fputc(i, f);
        }
        fclose(f);
    }
    memset(&d3, 0, sizeof(d3));
    CHECK((sizeof(void *) != 4) || (sizeof(d3) == 1056), "Gu3dfInfo is %u bytes here", (unsigned) sizeof(d3));
    CHECK(sizeof(d3) == sizeof(glc_3df_info_t), "the stand-in's Gu3dfInfo is not the DLL's");
    CHECK(gu3dfGetInfo("test_glide.3df", (Gu3dfInfo *) &d3) == FXTRUE, "gu3dfGetInfo");
    CHECK((d3.width == 4) && (d3.height == 2) && (d3.small_lod == 8) && (d3.large_lod == 6) && (d3.aspect == 2) &&
              (d3.format == 0xa) && (d3.mem_required == 22),
          "the .3df header: %ux%u, levels %d-%d, aspect %d, format %x, %u bytes", d3.width, d3.height, d3.small_lod,
          d3.large_lod, d3.aspect, d3.format, d3.mem_required);
    d3.data = loaded;
    CHECK(gu3dfLoad("test_glide.3df", (Gu3dfInfo *) &d3) == FXTRUE, "gu3dfLoad");
    CHECK((loaded[0] == 0x1200) && (loaded[10] == 0x120a), "the .3df texels: %x %x", loaded[0], loaded[10]);
    CHECK(gu3dfGetInfo("no such file.3df", (Gu3dfInfo *) &d3) == FXFALSE, "a missing file's header read");
    CHECK(files_open() == 0, "a .3df file left open");
    remove("test_glide.3df");
}

static void
expect_board(const GrHwConfiguration *hwc)
{
    const GrVoodooConfig_t *v = &hwc->SSTs[0].sstBoard.VoodooConfig;

    CHECK((hwc->num_sst == 1) && (hwc->SSTs[0].type == GR_SSTTYPE_VOODOO) && (v->fbRam == 12) &&
              (v->fbiRev == 0x11002) && (v->nTexelfx == 2) && !v->sliDetect && (v->tmuConfig[0].tmuRev == 0x11002) &&
              (v->tmuConfig[0].tmuRam == 2) && (v->tmuConfig[1].tmuRev == 0x11002) && (v->tmuConfig[1].tmuRam == 2),
          "the board is not the Voodoo3: %d boards, type %d, fbRam %d, fbiRev %X, %d TMUs", hwc->num_sst,
          hwc->SSTs[0].type, v->fbRam, v->fbiRev, v->nTexelfx);
}
#endif

static int
run(const char *scenario)
{
#ifndef TEST_GLIDE3
    GrHwConfiguration hwc;

    if (!strcmp(scenario, "glide")) {
        /* Diagnostics "Glide": every line. */
        machine(VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_GLIDE);
        session(&hwc);
        expect_board(&hwc);
        EXPECT("VGPU98 glide2x " VGPU98_BUILD_STAMP " TEST.EXE: loaded; a Voodoo3 (16 MB, two TMUs) for Glide 2.61");
        EXPECT("TEST.EXE: file (the system folder): C:\\WINDOWS\\SYSTEM\\GLIDE2X.DLL");
        EXPECT("TEST.EXE: program C:\\GAMES\\TEST\\TEST.EXE");
        EXPECT("TEST.EXE: build " VGPU98_BUILD_STAMP ", the display driver's " VGPU98_BUILD_STAMP);
        EXPECT("TEST.EXE: environment: FX_GLIDE_NO_SPLASH=1 sst_gamma=1.3");
        EXPECT_NO("SSTX=1");
        EXPECT("first call: grGlideGetVersion");
        EXPECT("first call: guMovieStart (does nothing yet)");
        CHECK(count_lines("first call:") == 8, "%d first-call lines, expected 8", count_lines("first call:"));
        CHECK(count_lines("grSstWinOpen refused: 640x480, 60 Hz, window 0, colour format 0, origin 0, buffers 2+1") == 5,
              "not every refused window was written");
        EXPECT("the card draws no Glide: no 3D on the host GPU, or an 86Box from before Glide drew");
        CHECK(count_lines("the card draws no Glide") == 1, "the reason for refusing said more than once");
        EXPECT("call record: 8 exports, 15 calls; 1 of the exports do nothing yet");
        expect_group("done", " grSstWinOpen 5, grBufferSwap 2, grGlideGetVersion 1, grGlideInit 1, grGlideShutdown 1, "
                             "grSstQueryBoards 1, grSstQueryHardware 1");
        expect_group("nothing yet", " guMovieStart 3");
        CHECK(count_lines("call record:") == 1, "the record was written again at unload with no new calls");
        EXPECT_NO("possible stale");
        /* The lines before the first call's: loaded, the check, the environment. */
        CHECK(strstr(lines[0], "loaded;") && strstr(lines[1], "file ") && strstr(lines[2], "program ") &&
                  strstr(lines[3], "build ") && strstr(lines[4], "environment:") &&
                  strstr(lines[5], "first call: grGlideGetVersion"),
              "the first lines are out of order");
    } else if (!strcmp(scenario, "minimal")) {
        /* Minimal: the event lines alone, three refused windows. */
        machine(VGPU98_DEBUG_EVENTS);
        session(&hwc);
        expect_board(&hwc);
        EXPECT("loaded; a Voodoo3");
        EXPECT_NO("file ");
        EXPECT_NO("program ");
        EXPECT_NO("build ");
        EXPECT_NO("environment");
        EXPECT_NO("first call");
        EXPECT_NO("call record");
        CHECK(count_lines("grSstWinOpen refused: 640x480") == 3, "%d refused windows written, expected 3",
              count_lines("grSstWinOpen refused: 640x480"));
        EXPECT("grSstWinOpen refused again: no more of its lines from this process");
        CHECK(n_lines == 6, "%d lines at Minimal, expected 6", n_lines);
    } else if (!strcmp(scenario, "directx")) {
        /* A DirectX level: the Glide DLLs keep to their event lines. */
        machine(VGPU98_DEBUG_LISTENING);
        session(&hwc);
        EXPECT("loaded; a Voodoo3");
        EXPECT_NO("first call");
        EXPECT_NO("call record");
        CHECK(n_lines == 6, "%d lines at a DirectX level, expected 6", n_lines);
    } else if (!strcmp(scenario, "stale")) {
        /* Another stamp from the display driver: an event, even at Minimal; the board still reported. */
        machine(VGPU98_DEBUG_EVENTS);
        m.stamp = "0000000";
        session(&hwc);
        expect_board(&hwc);
        EXPECT("possible stale Glide DLL: built " VGPU98_BUILD_STAMP ", the display driver 0000000");
    } else if (!strcmp(scenario, "head")) {
        /* A display driver of before the stamp: its answer's head alone. */
        machine(VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_GLIDE);
        m.answer_size = ESC_INFO_HEAD;
        session(&hwc);
        expect_board(&hwc);
        EXPECT("possible stale Glide DLL: built " VGPU98_BUILD_STAMP "; the display driver gives no stamp (an older build)");
        EXPECT_NO("TEST.EXE: build ");
    } else if (!strcmp(scenario, "other")) {
        /* A shared block from another build: one line, no board, then nothing. */
        machine(VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_GLIDE);
        m.sb_version = SB_VERSION + 1;
        m.sb->version = SB_VERSION + 1;
        session(&hwc);
        CHECK(hwc.num_sst == 0, "a board reported with another build's block");
        EXPECT("no Glide board: another build's display driver (block 3/");
        EXPECT(", answer 3; this DLL's 2/");
        CHECK(n_lines == 1, "%d lines with another build's block, expected 1", n_lines);
    } else if (!strcmp(scenario, "long")) {
        /* A long program name and the most a window line can say: no
           line cut, the name in each line's start cut at 20 characters;
           at Glide 64 refused windows, then one line saying no more. */
        int i;

        machine(VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_GLIDE);
        m.program = "C:\\GAMES\\A FOLDER WITH A RATHER LONG NAME\\A PROGRAM WITH A RATHER LONG NAME.EXE";
        grGlideInit();
        for (i = 0; i < 70; i++)
            grSstWinOpen(0x12345678, 0xff, 0xff, 3, 1, 3, 1);
        EXPECT(" A PROGRAM WITH A RAT: loaded;");
        EXPECT("program C:\\GAMES\\A FOLDER WITH A RATHER LONG NAME\\A PROGRAM WITH A RATHER LONG NAME.EXE");
        CHECK(count_lines("grSstWinOpen refused: window size, no refresh, window 12345678, colour format 3, origin 1, "
                          "buffers 3+1") == 64,
              "%d refused windows written at Glide, expected 64", count_lines("grSstWinOpen refused: window size"));
        CHECK(count_lines("grSstWinOpen refused again: no more of its lines from this process") == 1,
              "no line saying no more refused windows come");
    } else if (!strcmp(scenario, "folder")) {
        /* A copy in a game's folder, as a game's own installer leaves one:
           the file line says so first, its long path cut at the end; ten
           lines of Glide variables, six shown; a record of many exports. */
        static char env[2048];
        size_t      at = 0;
        int         i;

        machine(VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_GLIDE);
        m.program = "C:\\GAMES\\UNREAL TOURNAMENT GAME OF THE YEAR EDITION\\UNREAL TOURNAMENT\\SYSTEM\\"
                    "UNREALTOURNAMENT.EXE";
        m.dll = "C:\\GAMES\\UNREAL TOURNAMENT GAME OF THE YEAR EDITION\\UNREAL TOURNAMENT\\SYSTEM\\GLIDE2X.DLL";
        /* A variable of 120 characters, then nine of 60: one to a line. */
        at += (size_t) sprintf(env + at, "PATH=C:\\WINDOWS") + 1;
        at += (size_t) sprintf(env + at, "FX_GLIDE_LONG=%0106d", 0) + 1;
        for (i = 1; i <= 9; i++)
            at += (size_t) sprintf(env + at, "SST_VARIABLE_%d=%045d", i, i) + 1;
        env[at] = 0;
        m.env   = env;
        grGlideInit();
        grSstQueryHardware(&hwc);
        for (i = 0; i < 4; i++)
            grBufferClear(0, 0, 0);
        grAlphaBlendFunction(0, 0, 0, 0);
        grAlphaCombine(0, 0, 0, 0, 0);
        grAlphaTestFunction(0);
        grChromakeyMode(0);
        grClipWindow(0, 0, 0, 0);
        grColorCombine(0, 0, 0, 0, 0);
        grConstantColorValue(0);
        grCullMode(0);
        grDepthBufferFunction(0);
        grDepthBufferMode(0);
        grDepthMask(0);
        grFogMode(0);
        grGlideShutdown();
        DllMain(TEST_INSTANCE, 0, NULL);
        expect_board(&hwc);
        EXPECT(" UNREALTOURNAMENT.EXE: loaded;");
        EXPECT("UNREALTOURNAMENT.EXE: file (not the system folder, C:\\WINDOWS\\system): C:\\GAMES\\UNREAL TOURNAMENT");
        CHECK(line_cut("file (not the system folder"), "the file line was not cut: the scenario's path is too short");
        EXPECT("UNREALTOURNAMENT.EXE: program C:\\GAMES\\UNREAL TOURNAMENT GAME OF THE YEAR EDITION\\");
        EXPECT("environment: FX_GLIDE_LONG=000");
        EXPECT("environment, more: SST_VARIABLE_5=");
        EXPECT_NO("SST_VARIABLE_6=");
        EXPECT_NO("PATH=");
        EXPECT("environment: 4 more lines not shown");
        CHECK(count_lines("environment, more:") == 5, "%d \"environment, more:\" lines, expected 5",
              count_lines("environment, more:"));
        EXPECT("call record: 16 exports, 19 calls; 0 of the exports do nothing yet");
        expect_group("done", " grBufferClear 4, grAlphaBlendFunction 1, grAlphaCombine 1, grAlphaTestFunction 1, "
                             "grChromakeyMode 1, grClipWindow 1, grColorCombine 1, grConstantColorValue 1, grCullMode 1, "
                             "grDepthBufferFunction 1, grDepthBufferMode 1, grDepthMask 1, grFogMode 1, grGlideInit 1, "
                             "grGlideShutdown 1, grSstQueryHardware 1");
        CHECK(count_lines("done, more:") >= 1, "the record's \"done\" took one line");
    } else if (!strcmp(scenario, "screen")) {
        /* A card that draws Glide: the
           window, the screen's mode, clears, swaps, gamma, the frame buffer. */
        const pkt_t   *k;
        glc_lfb_info_t info;
        uint16_t       px[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        uint16_t       got[6];
        DWORD          staging;

        machine_glide(VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_GLIDE);
        grGlideInit();
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        CHECK((m.coop_hwnd == TEST_HWND) && (m.coop_flags == 0x11), "exclusive mode: window %x, flags %x", m.coop_hwnd,
              m.coop_flags);
        CHECK((m.mode_w == 640) && (m.mode_h == 480) && (m.mode_bpp == 16) && (m.mode_hz == 60), "the mode %ux%ux%u at %u Hz",
              m.mode_w, m.mode_h, m.mode_bpp, m.mode_hz);
        staging = m.surface_offset;
        k       = nth_pkt(VGPU98_GL_OPEN, 0);
        CHECK(k && (k->len == VGPU98_GL_LEN_OPEN) && (k->p[1] == (640 | (480 << 16))) &&
                  (k->p[2] == (2 | VGPU98_GL_OPEN_AUX)) && (k->p[3] == staging) && (k->p[4] == (8192 + 4096) * 480),
              "OPEN: %x %x %x %x", k ? k->p[1] : 0, k ? k->p[2] : 0, k ? k->p[3] : 0, k ? k->p[4] : 0);
        k = nth_pkt(VGPU98_GL_STATE, 0);
        CHECK(k && (k->len == 2 + VGPU98_GS_COUNT) && (k->p[1] == 0) && (k->p[2 + VGPU98_GS_CLIP_MAX] == (640 | (480 << 16))) &&
                  (k->p[2 + VGPU98_GS_RENDER_BUFFER] == GR_BUFFER_BACKBUFFER) &&
                  (k->p[2 + VGPU98_GS_MASKS] == VGPU98_GS_MASK_RGB) && (k->p[2 + VGPU98_GS_DITHER] == 2) &&
                  (k->p[2 + VGPU98_GS_CONSTANT] == 0xffffffffu) && (k->p[2 + VGPU98_GS_LFB_CONSTANT] == 0xff),
              "the state at the open");
        /* Gamma 1.3, as a Voodoo3's Glide loads it at the open. */
        k = nth_pkt(VGPU98_GL_GAMMA, 0);
        CHECK(k && ((k->p[1 + 128] & 0xff) >= 149) && ((k->p[1 + 128] & 0xff) <= 151), "gamma 1.3 at the open: %x at 128",
              k ? k->p[1 + 128] : 0);
        CHECK(grSstScreenWidth() == 640 && grSstScreenHeight() == 480, "the screen's size");

        grClipWindow(10, 20, 630, 470);
        grBufferClear(0x00ff8040, 0x80, 0xffff);
        k = last_pkt(VGPU98_GL_CLEAR);
        CHECK(!k, "a CLEAR sent before a flush");
        grBufferSwap(1);
        k = last_pkt(VGPU98_GL_STATE);
        CHECK(k && (k->p[1] == VGPU98_GS_CLIP_MIN) && (k->len == 4) && (k->p[2] == (10 | (20 << 16))) &&
                  (k->p[3] == (630 | (470 << 16))),
              "the clip window's registers");
        k = last_pkt(VGPU98_GL_CLEAR);
        CHECK(k && (k->p[1] == 0xff8040) && (k->p[2] == 0x80) && (k->p[3] == 0xffff), "CLEAR's colour, alpha and depth");
        k = last_pkt(VGPU98_GL_SWAP);
        CHECK(k && (k->p[1] == 1), "SWAP's interval");
        CHECK(m.pkts[m.n_pkts - 1].op == VGPU98_OP_FENCE, "the swap's batch ends with a FENCE");
        CHECK(grBufferNumPending() == 0, "swaps pending after the GPU took them");

        grGammaCorrectionValue(2.2f);
        grSstIdle();
        k = last_pkt(VGPU98_GL_GAMMA);
        CHECK(k && ((k->p[1 + 128] & 0xff) >= 184) && ((k->p[1 + 128] & 0xff) <= 188) && ((k->p[1 + 255] & 0xff) == 255) &&
                  ((k->p[1 + 0] & 0xffffff) == 0),
              "gamma 2.2's table: %x at 128", k ? k->p[1 + 128] : 0);

        /* A write lock: filled first (READ in the lock's format), the
           changed pixels taken back at the unlock. */
        memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        CHECK(grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, VGPU98_GL_LFB_565, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                        (GrLfbInfo_t *) &info) == FXTRUE,
              "grLfbLock for writing");
        CHECK((info.lfbPtr == m.vram + staging) && (info.strideInBytes == 4096) && (info.writeMode == VGPU98_GL_LFB_565),
              "the write lock's pointer, stride %u and format", info.strideInBytes);
        k = last_pkt(VGPU98_GL_LFB_READ);
        CHECK(k && (k->p[1] == GR_BUFFER_BACKBUFFER) && (k->p[3] == (640 | (480 << 16))) && (k->p[4] == VGPU98_GL_LFB_565) &&
                  (k->p[5] == staging) && (k->p[6] == 4096),
              "the write lock's READ");
        CHECK(grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE, "grLfbUnlock");
        k = last_pkt(VGPU98_GL_LFB_WRITE);
        CHECK(k && (k->p[4] == (VGPU98_GL_LFB_565 | VGPU98_GL_LFB_CHANGED)) && (k->p[5] == staging),
              "the unlock's WRITE of changed pixels: %x", k ? k->p[4] : 0);
        /* 32-bit pixels: rows of 8192 bytes. */
        CHECK(grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, VGPU98_GL_LFB_8888, GR_ORIGIN_LOWER_LEFT, FXFALSE,
                        (GrLfbInfo_t *) &info) == FXTRUE &&
                  (info.strideInBytes == 8192) && (info.origin == GR_ORIGIN_LOWER_LEFT),
              "a 32-bit write lock: stride %u", info.strideInBytes);
        k = last_pkt(VGPU98_GL_LFB_READ);
        CHECK(k && (k->p[4] == (VGPU98_GL_LFB_8888 | VGPU98_GL_LFB_LOWER_LEFT)) && (k->p[6] == 8192), "its READ");
        CHECK(grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXTRUE, "its unlock");
        CHECK(grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER) == FXFALSE, "a second unlock taken");
        /* A read lock: the read rows, after the write rows. */
        CHECK(grLfbLock(GR_LFB_READ_ONLY, GR_BUFFER_FRONTBUFFER, GR_LFBWRITEMODE_ANY, GR_ORIGIN_UPPER_LEFT, FXFALSE,
                        (GrLfbInfo_t *) &info) == FXTRUE &&
                  (info.lfbPtr == m.vram + staging + 8192 * 480) && (info.strideInBytes == 4096),
              "a read lock");
        CHECK(grLfbUnlock(GR_LFB_READ_ONLY, GR_BUFFER_FRONTBUFFER) == FXTRUE, "the read lock's unlock");
        /* A region written: its pixels in the write rows, no read first. */
        {
            const int reads = count_pkts(VGPU98_GL_LFB_READ);

            CHECK(grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 5, 6, VGPU98_GL_LFB_565, 4, 2, 8, px) == FXTRUE, "grLfbWriteRegion");
            CHECK(count_pkts(VGPU98_GL_LFB_READ) == reads, "a region's write read the buffer");
        }
        k = last_pkt(VGPU98_GL_LFB_WRITE);
        CHECK(k && (k->p[2] == (5 | (6 << 16))) && (k->p[3] == (4 | (2 << 16))) && (k->p[4] == VGPU98_GL_LFB_565) &&
                  !memcmp(m.vram + staging + 6 * 4096 + 10, px, 8) && !memcmp(m.vram + staging + 7 * 4096 + 10, px + 4, 8),
              "the region's WRITE and pixels");
        /* A rectangle past the window, its sums wrapping: refused. */
        CHECK(grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 0xfffffff0u, 0, VGPU98_GL_LFB_565, 0x20, 1, 8, px) == FXFALSE,
              "a wrapping rectangle written");
        CHECK(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 0, 0xffffff00u, 1, 0x200, 2, got) == FXFALSE, "a wrapping rectangle read");
        /* A region read: the GPU's pixels (x + 7 y) copied out. */
        CHECK(grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 3, 4, 3, 2, 6, got) == FXTRUE, "grLfbReadRegion");
        CHECK((got[0] == 3 + 28) && (got[2] == 5 + 28) && (got[3] == 3 + 35) && (got[5] == 5 + 35),
              "the region read: %u %u %u %u", got[0], got[2], got[3], got[5]);

        /* The status register as a Voodoo3's Glide 2 gives it, idle and out of
           retrace. */
        CHECK(grSstStatus() == 0x0ffff05fu, "grSstStatus %x", grSstStatus());
        CHECK(grSstIsBusy() == FXFALSE, "busy with the GPU idle");

        grClipWindow(0, 0, 320, 240); /* changed just before the close */
        grSstWinClose();
        CHECK(count_pkts(VGPU98_GL_CLOSE) == 1, "CLOSE at grSstWinClose");
        CHECK(m.pkts[m.n_pkts - 1].op == VGPU98_OP_FENCE && m.pkts[m.n_pkts - 2].op == VGPU98_GL_CLOSE,
              "a command after the CLOSE: %x", m.pkts[m.n_pkts - 2].op);
        CHECK((m.restores == 1) && (m.coop_flags == 0x8) && (m.dd_objects == 0), "the desktop given back: %d restores, "
              "flags %x, %d objects left", m.restores, m.coop_flags, m.dd_objects);
        screen_calls();
        grGlideShutdown();
        DllMain(TEST_INSTANCE, 0, NULL);
        CHECK(count_pkts(VGPU98_GL_CLOSE) == 1, "a second CLOSE after the window closed");
        EXPECT("window open: 640x480, 2 colour buffers and a depth buffer, upper-left origin; mode set, exclusive");
        EXPECT("the window: 5150, class \"TestWindow\"");
        EXPECT("  at 0,0 640x480, foreground, topmost, \"Test Game\"");
        EXPECT_NO("refused");
        EXPECT_NO("not done yet");
        EXPECT_NO("goes again");
    } else if (!strcmp(scenario, "textures")) {
        machine_glide(VGPU98_DEBUG_EVENTS);
        CHECK(grTexMaxAddress(0) == 2 * 1024 * 1024 - 16, "grTexMaxAddress before a window: %u", grTexMaxAddress(0));
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        textures();
        grSstWinClose();
        EXPECT_NO("not done yet");
    } else if (!strcmp(scenario, "draw")) {
        machine_glide(VGPU98_DEBUG_EVENTS);
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        draws();
        {
            static DWORD saved[78];

            grConstantColorValue(0x11223344u);
            grGlideGetState((GrState *) saved);
            grConstantColorValue(0x55667788u);
            grHints(0, 0x10);
            grGlideSetState((GrState *) saved);
            grSstIdle();
            CHECK(reg_sent(VGPU98_GS_CONSTANT, 0) == 0x11223344u, "the constant colour not restored");
        }
        grSstWinClose();
        EXPECT_NO("not done yet");
    } else if (!strcmp(scenario, "snap")) {
        /* x and y as a Voodoo's setup unit takes them:
           a program's snap bias (3 << 18) falls away exactly, its 1/16
           fractions kept; x 2100 wraps to -1996, -2049 to 2047; not a
           number, and 2^27, draw nothing. */
        const float   bias = (float) (3 << 18);
        test_vertex_t v[4];
        const pkt_t  *k;
        const float  *f;
        int           i, draws0;

        machine_glide(VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_GLIDE);
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        memset(v, 0, sizeof(v));
        v[0].x = 10.0f;
        v[0].y = 10.0f;
        v[1].x = 20.0f;
        v[1].y = 10.0f;
        v[2].x = 10.0f;
        v[2].y = 20.5625f;
        v[3].x = 20.0f;
        v[3].y = 20.0f;
        for (i = 0; i < 4; i++) {
            v[i].x += bias;
            v[i].y += bias;
            v[i].oow = 1.0f;
        }
        grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
        grSstIdle();
        k = last_pkt(VGPU98_GL_DRAW);
        CHECK(k && (k->p[2] == 3) && (draw_vertex(k, 0)[VGPU98_GLV_X] == 10.0f) && (draw_vertex(k, 0)[VGPU98_GLV_Y] == 10.0f) &&
                  (draw_vertex(k, 1)[VGPU98_GLV_X] == 20.0f) && (draw_vertex(k, 2)[VGPU98_GLV_Y] == 20.5625f),
              "a biased triangle not in range, exact: %f %f",
              k ? (double) draw_vertex(k, 0)[VGPU98_GLV_X] : 0.0, k ? (double) draw_vertex(k, 2)[VGPU98_GLV_Y] : 0.0);
        /* The cull sign the unbiased triangle's (a positive area). */
        draws0 = count_pkts(VGPU98_GL_DRAW);
        grCullMode(GR_CULL_POSITIVE);
        grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
        grSstIdle();
        CHECK(count_pkts(VGPU98_GL_DRAW) == draws0, "a biased positive triangle drawn with GR_CULL_POSITIVE");
        grCullMode(GR_CULL_NEGATIVE);
        grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
        grSstIdle();
        CHECK(count_pkts(VGPU98_GL_DRAW) == draws0 + 1, "a biased positive triangle dropped with GR_CULL_NEGATIVE");
        grCullMode(GR_CULL_DISABLE);
        /* Unreal's call: a polygon of four, a fan. */
        grDrawPolygonVertexList(4, (GrVertex *) v);
        grSstIdle();
        k = last_pkt(VGPU98_GL_DRAW);
        CHECK(k && (k->p[2] == 6) && (draw_vertex(k, 5)[VGPU98_GLV_X] == 20.0f) && (draw_vertex(k, 5)[VGPU98_GLV_Y] == 20.0f),
              "a biased polygon not in range");
        /* A point: the pixel its unbiased position is in. */
        v[0].x = bias + 5.75f;
        v[0].y = bias + 6.25f;
        grDrawPoint((GrVertex *) &v[0]);
        grSstIdle();
        k = last_pkt(VGPU98_GL_DRAW);
        CHECK(k && (k->p[2] == 3) && (draw_vertex(k, 0)[0] == 5.0f) && (draw_vertex(k, 0)[1] == 6.0f) &&
                  (draw_vertex(k, 1)[0] == 6.5f) && (draw_vertex(k, 2)[1] == 7.5f),
              "a biased point's triangle");
        /* A line: the unbiased one's parallelogram. */
        v[0].x = bias;
        v[0].y = bias;
        v[1].x = bias + 10.0f;
        v[1].y = bias + 2.0f;
        grDrawLine((GrVertex *) &v[0], (GrVertex *) &v[1]);
        grSstIdle();
        k = last_pkt(VGPU98_GL_DRAW);
        CHECK(k && (k->p[2] == 6) && (draw_vertex(k, 0)[1] == -0.5f) && (draw_vertex(k, 1)[1] == 1.5f) &&
                  (draw_vertex(k, 2)[1] == 2.5f) && (draw_vertex(k, 5)[1] == 0.5f) && (draw_vertex(k, 1)[0] == 10.0f),
              "a biased line's triangles");
        /* Without a bias past 2048: wrapped, as a Voodoo wraps them. */
        v[0].x = 2100.0f;
        v[0].y = 10.0f;
        v[1].x = 2110.0f;
        v[1].y = 10.0f;
        v[2].x = 2120.0f;
        v[2].y = 20.0f;
        grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
        grSstIdle();
        f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
        CHECK(f && (f[VGPU98_GLV_X] == -1996.0f) && (f[VGPU98_GLV_X + VGPU98_GL_VERTEX_DWORDS] == -1986.0f) &&
                  (f[VGPU98_GLV_X + 2 * VGPU98_GL_VERTEX_DWORDS] == -1976.0f),
              "x past 2048 not wrapped: %f", f ? (double) f[VGPU98_GLV_X] : 0.0);
        /* Just inside 2^27: wrapped; 2^27 and not a number: nothing. */
        v[0].x = 134217712.0f;
        v[1].x = 134217704.0f;
        v[2].x = 134217712.0f;
        grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
        grSstIdle();
        f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
        CHECK(f && (f[VGPU98_GLV_X] == -16.0f) && (f[VGPU98_GLV_X + VGPU98_GL_VERTEX_DWORDS] == -24.0f),
              "x just inside 2^27 not wrapped: %f", f ? (double) f[VGPU98_GLV_X] : 0.0);
        draws0 = count_pkts(VGPU98_GL_DRAW);
        v[0].x = 134217728.0f;
        grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
        v[0].x = 10.0f;
        v[0].y = 0.0f / 0.0f;
        grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
        v[0].y = 10.0f;
        v[1].x = 1.0f / 0.0f;
        grDrawLine((GrVertex *) &v[0], (GrVertex *) &v[1]);
        grDrawPoint((GrVertex *) &v[1]);
        grSstIdle();
        CHECK(count_pkts(VGPU98_GL_DRAW) == draws0, "a vertex without a 12.4 value drawn");
        grSstWinClose();
        /* The wrap's note once; the window's counts at its close (a culled
           triangle is culled before it wraps). */
        CHECK(count_lines("x and y wrapped as a Voodoo wraps them: the first x 786442 y 786442 as 10 10") == 1,
              "the wrap's note not said once");
        EXPECT("window closed after 0 swaps: triangles 9 asked, 6 sent, 1 culled");
        EXPECT("  points 2, lines 2; 4 primitives dropped");
        EXPECT("  0 triangles wrapped apart, 25 vertices wrapped");
        EXPECT("  TMU1's 1/w while active: 0 its own, 0 TMU0's, 0 the vertex's");
        EXPECT("  TMU1's s and t while active: 0 its own, 0 TMU0's");
        EXPECT_NO("possible bad vertices");

        /* The edges of the range, and a fraction off the 1/16 grid kept
           exactly: each a triangle (x, 10) (x, 20) (other, 15), its corners
           in one multiple of 4096. */
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        {
            static const struct {
                float x, other, want;
            } edge[] = {
                { 2048.0f, 2050.0f, -2048.0f },       { -2048.0f, -2000.0f, -2048.0f }, { 2047.9375f, 2000.0f, 2047.9375f },
                { -2048.0625f, -2100.0f, 2047.9375f }, { -6144.0f, -6000.0f, -2048.0f }, { -4096.0f, -4000.0f, 0.0f },
                { 2100.123f, 2200.0f, 0.0f },
            };
            size_t e;

            for (e = 0; e < sizeof(edge) / sizeof(edge[0]); e++) {
                const float want = (e == 6) ? (float) ((double) 2100.123f - 4096.0) : edge[e].want;

                memset(v, 0, sizeof(v));
                v[0].x = v[1].x = edge[e].x;
                v[2].x          = edge[e].other;
                v[0].y          = 10.0f;
                v[1].y          = 20.0f;
                v[2].y          = 15.0f;
                draws0          = count_pkts(VGPU98_GL_DRAW);
                grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
                grSstIdle();
                f = (count_pkts(VGPU98_GL_DRAW) == draws0 + 1) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
                CHECK(f && (f[VGPU98_GLV_X] == want) && (f[VGPU98_GLV_X + VGPU98_GL_VERTEX_DWORDS] == want),
                      "x %f wrapped to %f, not %f", (double) edge[e].x, f ? (double) f[VGPU98_GLV_X] : -1.0, (double) want);
            }
        }
        /* Corners wrapped by different multiples: the Voodoo draws nothing
           of them, nor does the DLL; the cull from the program's own
           values. */
        {
            static const float tri[3][6] = {
                { 100.0f, 100.0f, 200.0f, 300.0f, 2200.0f, 200.0f },
                { 100.0f, 100.0f, 200.0f, 100.0f, 150.0f, 2100.0f },
                { 100.0f, 100.0f, 2100.0f, 150.0f, 300.0f, 300.0f },
            };

            draws0 = count_pkts(VGPU98_GL_DRAW);
            for (i = 0; i < 3; i++) {
                memset(v, 0, sizeof(v));
                v[0].x = tri[i][0];
                v[0].y = tri[i][1];
                v[1].x = tri[i][2];
                v[1].y = tri[i][3];
                v[2].x = tri[i][4];
                v[2].y = tri[i][5];
                grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
            }
            v[0].x = 1000.0f;
            v[0].y = 100.0f;
            v[1].x = 3000.0f;
            v[1].y = 100.0f;
            grDrawLine((GrVertex *) &v[0], (GrVertex *) &v[1]);
            grSstIdle();
            CHECK(count_pkts(VGPU98_GL_DRAW) == draws0, "a triangle wrapped apart drawn");
            /* The first, a negative area by the program's values: culled
               by GR_CULL_NEGATIVE before it wraps. */
            grCullMode(GR_CULL_NEGATIVE);
            v[0].x = tri[0][0];
            v[0].y = tri[0][1];
            v[1].x = tri[0][2];
            v[1].y = tri[0][3];
            v[2].x = tri[0][4];
            v[2].y = tri[0][5];
            grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
            grCullMode(GR_CULL_DISABLE);
        }
        grSstWinClose();
        EXPECT("window closed after 0 swaps: triangles 11 asked, 7 sent, 1 culled");
        /* The summary's TMU1 lines: both TMUs active, a triangle with no
           hint, TMU0's 1/w hinted, TMU1's 1/w hinted, TMU1's s and t
           hinted. */
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        {
            static const FxU32 hints[4] = { 0, 0x02, 0x08, 0x10 };

            memset(v, 0, sizeof(v));
            v[1].x = 20.0f;
            v[2].y = 20.0f;
            for (i = 0; i < 3; i++) {
                v[i].oow        = 0.5f;
                v[i].tmu[0].oow = 0.25f;
                v[i].tmu[1].oow = 0.125f;
            }
            grTexCombine(0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                         FXFALSE, FXFALSE);
            grTexCombine(1, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                         FXFALSE, FXFALSE);
            for (i = 0; i < 4; i++) {
                grHints(0, hints[i]);
                grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
            }
            grHints(0, 0);
        }
        grSstWinClose();
        EXPECT("  TMU1's 1/w while active: 3 its own, 3 TMU0's, 6 the vertex's");
        EXPECT("  TMU1's s and t while active: 3 its own, 9 TMU0's");
        EXPECT("  points 0, lines 1; 0 primitives dropped");
        EXPECT("  5 triangles wrapped apart, 15 vertices wrapped");
    } else if (!strcmp(scenario, "bad")) {
        /* "possible bad vertices": 100 primitives dropped in a window and
           one in ten of those asked at least; once a window. */
        test_vertex_t v[3];
        int           i;

        /* A program name of 20 characters (the most a line's start holds),
           at Glide: no line cut, whatever its counts. */
        machine_glide(VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_GLIDE);
        m.program = "C:\\GAMES\\UT\\UNREALTOURNAMENT.EXE";
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        memset(v, 0, sizeof(v));
        v[1].x = 20.0f;
        v[2].y = 20.0f;
        v[0].x = 0.0f / 0.0f;
        for (i = 0; i < 99; i++)
            grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
        EXPECT_NO("possible bad vertices");
        grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
        EXPECT("UNREALTOURNAMENT.EXE: possible bad vertices: 100 of 100 primitives dropped in this window (not a number, or past 2^27)");
        for (i = 0; i < 50; i++)
            grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
        CHECK(count_lines("possible bad vertices") == 1, "the finding said again in the same window");
        grSstWinClose();
        /* A new window: said again, but only once one in ten is bad. */
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        v[0].x = 0.0f;
        for (i = 0; i < 1000; i++)
            grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
        v[0].x = 0.0f / 0.0f;
        for (i = 0; i < 110; i++)
            grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
        CHECK(count_lines("possible bad vertices") == 1, "the finding said with fewer than one in ten dropped");
        grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
        CHECK(count_lines("possible bad vertices") == 2, "the finding not said again in a new window");
        EXPECT("possible bad vertices: 111 of 1111 primitives dropped in this window");
        grSstWinClose();
        /* The process ending with a window open, after a long-named call
           repeated 123456 times: the recorder's line and the window's counts. */
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        v[0].x = 0.0f;
        for (i = 0; i < 5; i++)
            grDrawTriangle((GrVertex *) &v[0], (GrVertex *) &v[1], (GrVertex *) &v[2]);
        for (i = 0; i < 123456; i++)
            grAlphaControlsITRGBLighting(FXFALSE);
        DllMain(TEST_INSTANCE, 0, (void *) 1);
        EXPECT("  grAlphaControlsITRGBLighting(");
        EXPECT(") x123456 at ");
        EXPECT("window closed after 0 swaps: triangles 5 asked, 5 sent, 0 culled");
    } else if (!strcmp(scenario, "recorder")) {
        /* The fault catcher placed at the first call; the last calls written
           at an unload without grGlideShutdown. */
        machine_glide(VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_GLIDE);
        grGlideInit();
        CHECK(filter_set != NULL, "no fault catcher placed");
        CHECK(filter_set(NULL) == 0, "the catcher answered for no fault");
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        {
            int i;

            /* A poll: its repeats fold into one entry; another thread's
               calls of it into one of their own. */
            for (i = 0; i < 5; i++)
                grBufferNumPending();
            m.thread = 0x78;
            grBufferNumPending();
            grBufferNumPending();
            m.thread = 0;
        }
        grBufferClear(0x123456, 0, 0);
        DllMain(TEST_INSTANCE, 0, (void *) 1);
        EXPECT("the last 5 Glide calls (unloaded without grGlideShutdown), oldest first:");
        /* (The arguments are a 32-bit stdcall frame's: not the host's.) */
        EXPECT("  grSstWinOpen(");
        EXPECT(") x5 at ");
        EXPECT("  grBufferNumPending(");
        EXPECT("  grBufferClear(");
        {
            int i, found = 0;

            for (i = 0; i < n_lines; i++)
                found += strstr(lines[i], ") x2 at ") && strstr(lines[i], ", thread 78");
            CHECK(found == 1, "the other thread's calls not their own entry");
        }
        CHECK(filter_set != NULL, "the catcher taken away at the process's end");
    } else if (!strcmp(scenario, "fault")) {
        /* A fault in the DLL (an image of 0x60000 bytes standing in for its
           own, that size in its PE header): the fault and its stack's
           addresses in two whole lines, then the program's own filter. */
        uint8_t *image = test_low_alloc(0x60000);
        struct {
            DWORD code, flags;
            void *next;
            DWORD address;
        } rec;
        struct {
            void          *rec;
            const uint8_t *ctx;
        } ep;
        static uint8_t ctx[0x2cc];
        DWORD         *stack = test_low_alloc(4096), v;
        int            i, found = 0;

        machine_glide(VGPU98_DEBUG_EVENTS);
        filter_set  = program_filter;
        image[0x3c] = 0x80;
        v           = 0x60000;
        memcpy(image + 0x80 + 80, &v, 4);
        DllMain((DWORD) (uintptr_t) image, 1, NULL);
        grGlideInit();
        CHECK(filter_set && (filter_set != program_filter), "no fault catcher placed");
        grTexDownloadMipMapLevelPartial(0, 0, 0, 0, 0, 0, 0, NULL, 0, 0);
        memset(&rec, 0, sizeof(rec));
        rec.code = 0xc0000005u;
        memset(stack, 0, 4096);
        stack[2]  = (DWORD) (uintptr_t) image + 0x1de0;
        stack[5]  = (DWORD) (uintptr_t) image + 0x1cb0;
        stack[9]  = (DWORD) (uintptr_t) image + 0x2000;
        stack[12] = (DWORD) (uintptr_t) image + 0x3000;
        stack[20] = (DWORD) (uintptr_t) image + 0x4000;
        stack[30] = (DWORD) (uintptr_t) image + 0x5000;
        v         = (DWORD) (uintptr_t) image + 0x5420;
        memcpy(ctx + 0xb8, &v, 4);
        v = (DWORD) (uintptr_t) stack;
        memcpy(ctx + 0xc4, &v, 4);
        ep.rec = &rec;
        ep.ctx = ctx;
        filter_set(&ep);
        EXPECT("a fault in the Glide DLL in grTexDownloadMipMapLevelPartial: code ");
        EXPECT("  its addresses on the stack: +");
        for (i = 0; i < n_lines; i++) {
            if (strstr(lines[i], "a fault in the Glide DLL") || strstr(lines[i], "its addresses on the stack")) {
                CHECK(!cut[i], "the fault's line cut: %s", lines[i]);
                found++;
            }
            if (strstr(lines[i], "its addresses on the stack"))
                CHECK(strstr(lines[i], "1de0") || strstr(lines[i], "1DE0"), "the stack's first address not said: %s", lines[i]);
            if (strstr(lines[i], "its addresses on the stack"))
                CHECK(strstr(lines[i], "5000"), "the stack's sixth address not said: %s", lines[i]);
        }
        CHECK(found == 2, "%d fault lines, expected 2", found);
        CHECK(program_faults == 1, "the program's own filter called %d times", program_faults);
        /* At a FreeLibrary the program's filter back. */
        DllMain((DWORD) (uintptr_t) image, 0, NULL);
        CHECK(filter_set == program_filter, "the program's filter not put back at FreeLibrary");
    } else if (!strcmp(scenario, "nomode")) {
        /* Windows refuses the mode: the window opens all the same. */
        machine_glide(VGPU98_DEBUG_EVENTS);
        m.mode_refuse = 1;
        CHECK(grSstWinOpen(0, 3, 0, 0, 0, 2, 0) == FXTRUE, "grSstWinOpen refused without the mode");
        EXPECT("grSstWinOpen: Windows refused 512x384 at 16 bpp");
        CHECK(nth_pkt(VGPU98_GL_OPEN, 0) && (nth_pkt(VGPU98_GL_OPEN, 0)->p[2] == (2 | VGPU98_GL_OPEN_AUX)),
              "OPEN without the depth buffer two colour buffers come with");
        grSstWinClose();
        CHECK(m.restores == 0, "a mode restored that was never set");
    } else if (!strcmp(scenario, "unload")) {
        /* A program ending with its window open: CLOSE at the unload,
           DirectDraw left alone (it gives the screen back itself). */
        machine_glide(VGPU98_DEBUG_EVENTS);
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        grBufferClear(0, 0, 0); /* left unsent */
        DllMain(TEST_INSTANCE, 0, (void *) 1);
        CHECK(count_pkts(VGPU98_GL_CLOSE) == 1, "no CLOSE at the unload");
        CHECK(count_pkts(VGPU98_GL_CLEAR) == 0, "the program's unsent commands sent at its end");
        CHECK(m.restores == 0, "DirectDraw called as the process ends");
    } else if (!strcmp(scenario, "free")) {
        /* FreeLibrary with the window open: closed whole, as a Voodoo3's
           Glide closes it. */
        machine_glide(VGPU98_DEBUG_EVENTS);
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        CHECK(filter_set != NULL, "no fault catcher placed");
        DllMain(TEST_INSTANCE, 0, NULL);
        CHECK(count_pkts(VGPU98_GL_CLOSE) == 1, "no CLOSE at the unload");
        CHECK((m.restores == 1) && (m.dd_objects == 0), "the desktop not given back at FreeLibrary: %d restores, %d objects",
              m.restores, m.dd_objects);
        CHECK(filter_set == NULL, "the unloaded DLL's fault catcher left in place");
    } else if (!strcmp(scenario, "toobig")) {
        /* Buffers that would leave a Voodoo3 less than 2 MB of textures:
           refused, as a Voodoo3's Glide refuses them. */
        machine_glide(VGPU98_DEBUG_EVENTS);
        CHECK(grSstWinOpen(0, 0x15, 0, 0, 0, 2, 1) == FXFALSE, "1920x1440 with 2+1 buffers opened");
        CHECK(grSstWinOpen(0, 0x0e, 0, 0, 0, 2, 1) == FXTRUE, "1600x1200 with 2+1 buffers refused");
        CHECK(grTexMaxAddress(0) < 0x800000, "grTexMaxAddress %x", grTexMaxAddress(0));
        grSstWinClose();
    } else if (!strcmp(scenario, "unknown")) {
        /* What a Voodoo3's Glide makes of what it does not know: the
           window's size is 640x480, a refresh rate 60 Hz; two colour
           buffers come with a depth buffer. */
        machine_glide(VGPU98_DEBUG_EVENTS);
        CHECK(grSstWinOpen(0, 0xff, 0xff, 0, 0, 2, 0) == FXTRUE, "grSstWinOpen refused");
        CHECK((m.mode_w == 640) && (m.mode_h == 480) && (m.mode_hz == 60), "the mode %ux%u at %u Hz", m.mode_w, m.mode_h,
              m.mode_hz);
        CHECK(nth_pkt(VGPU98_GL_OPEN, 0) && (nth_pkt(VGPU98_GL_OPEN, 0)->p[2] == (2 | VGPU98_GL_OPEN_AUX)),
              "two colour buffers without a depth buffer");
        grSstWinClose();
    } else if (!strcmp(scenario, "desktop")) {
        /* The desktop in the window's mode already: 8 bpp, so its mode comes
           back as a change when the program ends. */
        machine_glide(VGPU98_DEBUG_EVENTS);
        m.sb->mode_width  = 640;
        m.sb->mode_height = 480;
        m.sb->mode_bpp    = 16;
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        CHECK((m.mode_w == 640) && (m.mode_h == 480) && (m.mode_bpp == 8), "the mode %ux%ux%u", m.mode_w, m.mode_h,
              m.mode_bpp);
        grSstWinClose();
    } else if (!strcmp(scenario, "held")) {
        /* Swaps waiting while a DOS box holds the GPU: the program sleeps
           between looks, and goes on when the hold ends. */
        machine_glide(VGPU98_DEBUG_EVENTS);
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        m.stall = 1;
        grBufferSwap(1);
        grBufferSwap(1);
        CHECK(grBufferNumPending() == 2, "%d swaps pending, expected 2", grBufferNumPending());
        CHECK(grSstIsBusy() == FXTRUE, "not busy with swaps pending");
        m.regs[VGPU98_REG_STATUS / 4] |= VGPU98_STATUS_HELD;
        m.sleeps        = 0;
        m.release_after = 4;
        grBufferSwap(1);
        CHECK(m.sleeps >= 4, "%d sleeps while held, expected 4", m.sleeps);
        CHECK(count_pkts(VGPU98_GL_SWAP) == 3, "%d swaps sent, expected 3", count_pkts(VGPU98_GL_SWAP));
        grSstWinClose();
    } else if (!strcmp(scenario, "restart")) {
        /* The GPU restarts as the window opens: the session goes again.
           Then a restart under a session: its registers go again first. */
        const pkt_t *k;
        int          i, at = -1;

        machine_glide(VGPU98_DEBUG_EVENTS);
        m.restart_at_open = 1;
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        CHECK(count_pkts(VGPU98_GL_OPEN) == 2, "%d OPENs, expected 2", count_pkts(VGPU98_GL_OPEN));
        grClipWindow(1, 2, 3, 4);
        grBufferClear(0, 0, 0);
        grSstIdle();
        m.sb->state_epoch++;
        i = m.n_pkts;
        grBufferClear(0, 0, 0);
        grSstIdle();
        for (; i < m.n_pkts; i++) {
            if (m.pkts[i].op != VGPU98_OP_NOP) {
                at = i;
                break;
            }
        }
        k = (at >= 0) ? &m.pkts[at] : NULL;
        CHECK(k && (k->op == VGPU98_GL_STATE) && (k->len == 2 + VGPU98_GS_COUNT) && (k->p[2 + VGPU98_GS_CLIP_MIN] == (1 | (2 << 16))),
              "the registers not sent again first after a restart: %x", k ? k->op : 0);
        /* A restart while a batch goes (its earlier registers lost with the
           ring): they go again with the next. */
        grClipWindow(5, 6, 7, 8);
        m.restart_at_clear = 1;
        grBufferClear(0, 0, 0);
        grSstIdle();
        i  = m.n_pkts;
        at = -1;
        grBufferClear(0, 0, 0);
        grSstIdle();
        for (; i < m.n_pkts; i++) {
            if (m.pkts[i].op != VGPU98_OP_NOP) {
                at = i;
                break;
            }
        }
        k = (at >= 0) ? &m.pkts[at] : NULL;
        CHECK(k && (k->op == VGPU98_GL_STATE) && (k->len == 2 + VGPU98_GS_COUNT) && (k->p[2 + VGPU98_GS_CLIP_MIN] == (5 | (6 << 16))),
              "the registers not sent again after a restart inside a batch: %x", k ? k->op : 0);
        grSstWinClose();
    } else if (!strcmp(scenario, "lost")) {
        /* The screen's mode changed under the window: nothing more is sent. */
        int i, at, lost0, fg0;

        machine_glide(VGPU98_DEBUG_EVENTS);
        /* The mode changed as the window opened (as the GPU starts, inside
           the open's batch): refused, the screen given back, nothing sent. */
        m.mode_change_at_start = 1;
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXFALSE, "a window opened over a change of mode");
        EXPECT("grSstWinOpen refused: the screen's mode changed as the window opened");
        CHECK(count_pkts(VGPU98_GL_OPEN) == 0, "OPEN sent for a window whose mode went");
        CHECK((m.restores == 1) && (m.dd_objects == 0), "the screen not given back: %d restores, %d objects", m.restores,
              m.dd_objects);
        lost0 = count_lines("the screen's mode changed under");
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        grSstIdle();
        at = m.n_pkts;
        /* The taskbar in front, the mode changed with a clear unsent: the
           batch going is the lost window's, so none of it goes. */
        fg_window = TEST_TRAY_HWND;
        grBufferClear(0, 0, 0);
        m.sb->mode_serial++;
        grBufferSwap(1);
        EXPECT("the screen's mode changed under the Glide window: nothing more is drawn");
        EXPECT("  the foreground window then: 6000, class \"Shell_TrayWnd\"");
        EXPECT("  at 0,572 800x28, foreground, topmost, \"\"");
        grBufferClear(0, 0, 0);
        grBufferSwap(1);
        grSstWinClose();
        for (i = at; i < m.n_pkts; i++)
            CHECK(m.pkts[i].op < VGPU98_GL_FIRST, "a Glide command (%x) sent after the mode changed", m.pkts[i].op);
        CHECK(count_lines("the screen's mode changed under") == lost0 + 1, "the lost window said more than once");
        /* Another thread's window of the program in front: its title not
           read (it would be sent a message); its class made printable. */
        fg_window = 0;
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        fg_window = TEST_OTHER_HWND;
        m.sb->mode_serial++;
        grBufferClear(0, 0, 0);
        grBufferSwap(1);
        EXPECT("  the foreground window then: 7000, class \"Other'Thread?\"");
        EXPECT("  at 0,0 640x480, foreground, not topmost, \"\"");
        /* The Glide window itself in front. */
        grSstWinClose();
        fg_window = 0;
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        m.sb->mode_serial++;
        grBufferSwap(1);
        EXPECT("  the foreground window then: the Glide window");
        grSstWinClose();
        /* The program's own frame in front: described, its title not read
           (its window procedure would run inside the Glide call). */
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        fg_window = TEST_FRAME_HWND;
        m.sb->mode_serial++;
        grBufferSwap(1);
        EXPECT("  the foreground window then: 8000, class \"Frame\"");
        grSstWinClose();
        /* The process ending with the window open and its mode gone: the
           first line alone, nothing sent, no USER32 call inside DllMain. */
        fg_window = TEST_TRAY_HWND;
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        grSstIdle();
        at    = m.n_pkts;
        lost0 = count_lines("the screen's mode changed under");
        fg0   = count_lines("  the foreground window then");
        grBufferClear(0, 0, 0);
        m.sb->mode_serial++;
        m.in_dllmain = 1;
        DllMain(TEST_INSTANCE, 0, (void *) 1);
        m.in_dllmain = 0;
        CHECK(count_lines("the screen's mode changed under") == lost0 + 1, "the loss not said at the process's end");
        CHECK(count_lines("  the foreground window then") == fg0, "a window described inside DllMain");
        for (i = at; i < m.n_pkts; i++)
            CHECK(m.pkts[i].op < VGPU98_GL_FIRST, "a Glide command (%x) sent at the process's end", m.pkts[i].op);
    } else if (!strcmp(scenario, "quiet")) {
        /* Diagnostics Off: the windows never asked about, at the open or
           when the mode is lost; no line. */
        machine_glide(0);
        fg_window = TEST_TRAY_HWND;
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == FXTRUE, "grSstWinOpen refused");
        m.sb->mode_serial++;
        grBufferSwap(1);
        grSstWinClose();
        CHECK(m.window_queries == 0, "%d window queries at Off", m.window_queries);
        CHECK(n_lines == 0, "%d lines at Off", n_lines);
    } else if (!strcmp(scenario, "none")) {
        /* Another display driver: no board, no lines; every export called
           all the same. */
        machine(VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_GLIDE);
        m.escape = 0;
        grGlideInit();
        screen_calls();
        grSstWinOpen(0, 7, 0, 0, 0, 2, 1);
        screen_calls();
        session(&hwc);
        CHECK(hwc.num_sst == 0, "a board reported without the card");
        CHECK(n_lines == 0, "%d lines without the card", n_lines);
    } else {
        fprintf(stderr, "test_glide2x: unknown scenario %s\n", scenario);
        return 2;
    }
#else
    if (!strcmp(scenario, "glide")) {
        FxI32       p[4];
        const char *s;

        machine(VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_GLIDE);
        grGlideInit();
        CHECK((grGet(GR_NUM_BOARDS, 4, p) == 4) && (p[0] == 1), "GR_NUM_BOARDS");
        CHECK((grGet(GR_NUM_TMU, 4, p) == 4) && (p[0] == 2), "GR_NUM_TMU");
        CHECK((grGet(GR_MEMORY_FB, 4, p) == 4) && (p[0] == (10 << 20)), "GR_MEMORY_FB %d", p[0]);
        CHECK((grGet(GR_MEMORY_TMU, 4, p) == 4) && (p[0] == (2 << 20)), "GR_MEMORY_TMU");
        CHECK((grGet(GR_MEMORY_UMA, 4, p) == 4) && (p[0] == (16 << 20)), "GR_MEMORY_UMA");
        CHECK((grGet(GR_REVISION_FB, 4, p) == 4) && (p[0] == 0x11100), "GR_REVISION_FB");
        CHECK((grGet(GR_BITS_RGBA, 16, p) == 16) && (p[0] == 5) && (p[1] == 6) && (p[2] == 5) && (p[3] == 0),
              "GR_BITS_RGBA");
        CHECK(grGet(GR_BITS_RGBA, 4, p) == 0, "GR_BITS_RGBA answered with the wrong length");
        CHECK((grGet(0x04, 4, p) == 4) && (p[0] == 64), "GR_FOG_TABLE_ENTRIES");
        CHECK((grGet(0x28, 8, p) == 8) && (p[0] == 0xffff) && (p[1] == 0), "GR_ZDEPTH_MIN_MAX");
        CHECK(grGet(0x99, 4, p) == 0, "a query a Voodoo3 does not know answered");
        s = grGetString(GR_HARDWARE);
        CHECK(!strcmp(s, "Voodoo3 (tm)"), "GR_HARDWARE: %s", s);
        CHECK(!strcmp(grGetString(GR_VERSION), "3.10.00.0658"), "GR_VERSION");
        CHECK(grGetProcAddress("grChromaRangeExt") == NULL, "an extension's function offered");
        /* A name of 100 characters: its first 64 in the line. */
        {
            char name[101];

            memset(name, 'n', 100);
            name[100] = 0;
            CHECK(grGetProcAddress(name) == NULL, "a long name's function offered");
            name[64] = 0;
            CHECK(count_lines(name) == 1, "the long name's line is not there");
            name[64] = 'n';
            name[65] = 0;
            EXPECT_NO(name);
        }
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) == 0, "grSstWinOpen was not refused");
        grGlideShutdown();
        DllMain(TEST_INSTANCE, 0, NULL);
        EXPECT("VGPU98 glide3x " VGPU98_BUILD_STAMP " TEST.EXE: loaded; a Voodoo3 (16 MB, two TMUs) for Glide 3.10");
        EXPECT("TEST.EXE: file (the system folder): C:\\WINDOWS\\SYSTEM\\GLIDE3X.DLL");
        EXPECT("grGet 99: not a query a Voodoo3 answers");
        CHECK(count_lines("first call:") == 6, "%d first-call lines, expected 6", count_lines("first call:"));
        EXPECT("grGetProcAddress grChromaRangeExt: none");
        EXPECT("call record: 6 exports, 18 calls; 0 of the exports do nothing yet");
        expect_group("done", " grGet 11, grGetProcAddress 2, grGetString 2, grGlideInit 1, grGlideShutdown 1, "
                             "grSstWinOpen 1");
    } else if (!strcmp(scenario, "screen")) {
        /* Glide 3's window, swap, gamma table and region write. */
        const pkt_t *k;
        FxU32        ramp[32];
        uint16_t     px[4] = { 9, 8, 7, 6 };
        int          i;

        machine_glide(VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_GLIDE);
        grGlideInit();
        CHECK(grSstWinOpen(0, 8, 0, 0, 1, 2, 1) == 1, "grSstWinOpen gave no context");
        k = nth_pkt(VGPU98_GL_OPEN, 0);
        CHECK(k && (k->p[1] == (800 | (600 << 16))) && (k->p[2] == (2 | VGPU98_GL_OPEN_AUX | VGPU98_GL_OPEN_LOWER_LEFT)),
              "OPEN for 800x600, lower-left");
        for (i = 0; i < 32; i++)
            ramp[i] = (FxU32) (255 - i * 255 / 31); /* an inverse ramp */
        grLoadGammaTable(32, ramp, ramp, ramp);
        grSetNumPendingBuffers(1);
        grBufferSwap(0);
        k = last_pkt(VGPU98_GL_GAMMA);
        CHECK(k && ((k->p[1] & 0xff) == 255) && ((k->p[256] & 0xff) == 0), "the gamma table spread over 256 levels");
        CHECK(last_pkt(VGPU98_GL_SWAP) && (last_pkt(VGPU98_GL_SWAP)->p[1] == 0), "SWAP with no wait");
        CHECK(grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 1, 2, VGPU98_GL_LFB_565, 2, 2, FXTRUE, 4, px) == FXTRUE,
              "grLfbWriteRegion through the pipeline");
        k = last_pkt(VGPU98_GL_LFB_WRITE);
        CHECK(k && (k->p[4] == (VGPU98_GL_LFB_565 | VGPU98_GL_LFB_PIPELINE)),
              "the pipeline's flag, rows from the top: %x", k ? k->p[4] : 0);
        EXPECT("not done yet: a frame buffer write through the pixel pipeline");
        /* The summary's TMU1 lines: both TMUs
           active; Q1 and no ST1 (its own 1/w, TMU0's s and t); Q0 alone
           (TMU0's 1/w); Q0 with FOG_EXT and Q and ST1 (the vertex's 1/w, its
           own s and t). */
        {
            struct {
                float x, y, q, s0, t0, q1, q0, fog, s1, t1;
            } tv[3];

            memset(tv, 0, sizeof(tv));
            for (i = 0; i < 3; i++) {
                tv[i].x  = (float) (10 + 10 * (i & 1));
                tv[i].y  = (float) (10 + 10 * (i >> 1));
                tv[i].q  = 0.5f;
                tv[i].q1 = 0.125f;
                tv[i].q0 = 0.25f;
            }
            grVertexLayout(0x01, 0, 1);  /* XY */
            grVertexLayout(0x04, 8, 1);  /* Q */
            grVertexLayout(0x40, 12, 1); /* ST0 */
            grVertexLayout(0x51, 20, 1); /* Q1 */
            grTexCombine(0, 1, 0, 1, 0, FXFALSE, FXFALSE);
            grTexCombine(1, 1, 0, 1, 0, FXFALSE, FXFALSE);
            grDrawTriangle(&tv[0], &tv[1], &tv[2]);
            grVertexLayout(0x51, 0, 0);  /* no Q1 */
            grVertexLayout(0x50, 24, 1); /* Q0 */
            grDrawTriangle(&tv[0], &tv[1], &tv[2]);
            grVertexLayout(0x05, 28, 1); /* FOG_EXT */
            grVertexLayout(0x41, 32, 1); /* ST1 */
            grDrawTriangle(&tv[0], &tv[1], &tv[2]);
        }
        grFinish();
        CHECK(grSstWinClose(1) == FXTRUE, "grSstWinClose");
        EXPECT("  TMU1's 1/w while active: 3 its own, 3 TMU0's, 3 the vertex's");
        EXPECT("  TMU1's s and t while active: 3 its own, 6 TMU0's");
        CHECK(count_pkts(VGPU98_GL_CLOSE) == 1, "CLOSE");
    } else if (!strcmp(scenario, "draw")) {
        /* Glide 3's vertices through the layout, in window and clip
           coordinates; its vertex arrays; the saved state; the queries. */
        struct {
            float x, y, q;
            FxU32 argb;
            float s, t;
        } wv[5];
        struct {
            float x, y, w, r, g, b;
        } cv;
        const pkt_t *k;
        const float *f;
        void        *ptrs[2];
        FxI32        n, sz;
        static DWORD saved[512];
        int          i;

        machine_glide(VGPU98_DEBUG_EVENTS);
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) != 0, "grSstWinOpen refused");
        CHECK(grSelectContext(1) == FXTRUE, "the window's context not selected");
        grVertexLayout(0x01, 0, 1);  /* XY */
        grVertexLayout(0x04, 8, 1);  /* Q */
        grVertexLayout(0x30, 12, 1); /* PARGB */
        grVertexLayout(0x40, 16, 1); /* ST0 */
        for (i = 0; i < 5; i++) {
            wv[i].x    = (float) (10 + 10 * (i & 1));
            wv[i].y    = (float) (10 + 10 * (i >> 1));
            wv[i].q    = 0.5f;
            wv[i].argb = 0x80102030u;
            wv[i].s    = (float) i;
            wv[i].t    = 2.0f;
        }
        grDrawTriangle(&wv[0], &wv[1], &wv[2]);
        grFinish();
        k = last_pkt(VGPU98_GL_DRAW);
        f = k ? draw_vertex(k, 1) : NULL;
        CHECK(f && near_f(f[VGPU98_GLV_X], 20.0f) && near_f(f[VGPU98_GLV_Y], 10.0f) && near_f(f[VGPU98_GLV_OOW], 0.5f) &&
                  near_f(f[VGPU98_GLV_A], 128.0f) && near_f(f[VGPU98_GLV_R], 16.0f) && near_f(f[VGPU98_GLV_B], 48.0f) &&
                  near_f(f[VGPU98_GLV_SOW0], 1.0f) && near_f(f[VGPU98_GLV_SOW1], 1.0f) && near_f(f[VGPU98_GLV_TOW1], 2.0f) &&
                  near_f(f[VGPU98_GLV_OOW0], 0.5f) && near_f(f[VGPU98_GLV_OOW1], 0.5f),
              "a window-coordinate vertex through the layout");
        /* A Voodoo's snap bias (3 << 18) falls away. */
        for (i = 0; i < 3; i++) {
            wv[i].x += (float) (3 << 18);
            wv[i].y += (float) (3 << 18);
        }
        grDrawTriangle(&wv[0], &wv[1], &wv[2]);
        grFinish();
        for (i = 0; i < 3; i++) {
            wv[i].x -= (float) (3 << 18);
            wv[i].y -= (float) (3 << 18);
        }
        k = last_pkt(VGPU98_GL_DRAW);
        f = k ? draw_vertex(k, 1) : NULL;
        CHECK(f && (f[VGPU98_GLV_X] == 20.0f) && (f[VGPU98_GLV_Y] == 10.0f), "a biased window-coordinate vertex not in range");
        /* TMU1's 1/w without q1: an active TMU0's q0, else the vertex's. */
        {
            struct {
                float x, y, q, q0, s, t;
            } qv[3];

            for (i = 0; i < 3; i++) {
                qv[i].x  = (float) (10 + 10 * (i & 1));
                qv[i].y  = (float) (10 + 10 * (i >> 1));
                qv[i].q  = 0.5f;
                qv[i].q0 = 0.25f;
                qv[i].s  = 1.0f;
                qv[i].t  = 2.0f;
            }
            grVertexLayout(0x30, 0, 0);  /* no PARGB */
            grVertexLayout(0x50, 12, 1); /* Q0 */
            grTexCombine(0, 1, 0, 1, 0, FXFALSE, FXFALSE); /* TMU0 active: LOCAL */
            grDrawTriangle(&qv[0], &qv[1], &qv[2]);
            grFinish();
            k = last_pkt(VGPU98_GL_DRAW);
            f = k ? draw_vertex(k, 0) : NULL;
            CHECK(f && (f[VGPU98_GLV_OOW0] == 0.25f) && (f[VGPU98_GLV_OOW1] == 0.25f), "TMU1's 1/w not an active TMU0's q0");
            grTexCombine(0, 0, 0, 0, 0, FXFALSE, FXFALSE); /* TMU0 not active: ZERO */
            grDrawTriangle(&qv[0], &qv[1], &qv[2]);
            grFinish();
            k = last_pkt(VGPU98_GL_DRAW);
            f = k ? draw_vertex(k, 0) : NULL;
            CHECK(f && (f[VGPU98_GLV_OOW1] == 0.5f), "TMU1's 1/w not the vertex's with TMU0 not active");
            grVertexLayout(0x50, 0, 0);  /* no Q0 */
            grVertexLayout(0x30, 12, 1); /* PARGB again */
        }
        /* A strip of four: two triangles, the second wound as the first. */
        grDrawVertexArrayContiguous(4, 4, wv, sizeof(wv[0]));
        grFinish();
        k = last_pkt(VGPU98_GL_DRAW);
        CHECK(k && (k->p[2] == 6) && near_f(draw_vertex(k, 3)[VGPU98_GLV_SOW0], 2.0f) &&
                  near_f(draw_vertex(k, 4)[VGPU98_GLV_SOW0], 1.0f) && near_f(draw_vertex(k, 5)[VGPU98_GLV_SOW0], 3.0f),
              "the strip's triangles");
        /* Going on from it: one more triangle from its last two. */
        grDrawVertexArrayContiguous(7, 1, &wv[4], sizeof(wv[0]));
        grFinish();
        k = last_pkt(VGPU98_GL_DRAW);
        CHECK(k && (k->p[2] == 3) && near_f(draw_vertex(k, 0)[VGPU98_GLV_SOW0], 2.0f) &&
                  near_f(draw_vertex(k, 1)[VGPU98_GLV_SOW0], 3.0f) && near_f(draw_vertex(k, 2)[VGPU98_GLV_SOW0], 4.0f),
              "the strip going on");
        /* A strip of two, then one more: a triangle, as a Voodoo3 keeps them. */
        grDrawVertexArrayContiguous(4, 2, wv, sizeof(wv[0]));
        grDrawVertexArrayContiguous(7, 1, &wv[2], sizeof(wv[0]));
        grFinish();
        k = last_pkt(VGPU98_GL_DRAW);
        CHECK(k && (k->p[2] == 3) && near_f(draw_vertex(k, 2)[VGPU98_GLV_SOW0], 2.0f), "a strip of two going on");
        /* A fan of four: triangles around the first. */
        grDrawVertexArrayContiguous(5, 4, wv, sizeof(wv[0]));
        grFinish();
        k = last_pkt(VGPU98_GL_DRAW);
        CHECK(k && (k->p[2] == 6) && near_f(draw_vertex(k, 3)[VGPU98_GLV_SOW0], 0.0f) &&
                  near_f(draw_vertex(k, 4)[VGPU98_GLV_SOW0], 2.0f) && near_f(draw_vertex(k, 5)[VGPU98_GLV_SOW0], 3.0f),
              "the fan's triangles");
        /* Points through an array of pointers. */
        ptrs[0] = &wv[0];
        ptrs[1] = &wv[3];
        n       = count_pkts(VGPU98_GL_DRAW);
        grDrawVertexArray(0, 2, ptrs);
        grFinish();
        CHECK((count_pkts(VGPU98_GL_DRAW) == n + 1) && (last_pkt(VGPU98_GL_DRAW)->p[2] == 6), "two points");

        /* Clip coordinates: divided by w, through the viewport; colours 0-1. */
        grCoordinateSpace(1);
        grViewport(0, 0, 640, 480);
        grVertexLayout(0x04, 0, 0);  /* no Q */
        grVertexLayout(0x30, 0, 0);  /* no PARGB */
        grVertexLayout(0x40, 0, 0);  /* no ST0 */
        grVertexLayout(0x03, 8, 1);  /* W */
        grVertexLayout(0x20, 12, 1); /* RGB */
        cv.x = 1.0f;
        cv.y = -1.0f;
        cv.w = 2.0f;
        cv.r = 0.5f;
        cv.g = 1.0f;
        cv.b = 0.0f;
        grDrawPoint(&cv);
        grFinish();
        k = last_pkt(VGPU98_GL_DRAW);
        f = k ? draw_vertex(k, 0) : NULL;
        CHECK(f && near_f(f[VGPU98_GLV_X], 480.0f) && near_f(f[VGPU98_GLV_Y], 120.0f) && near_f(f[VGPU98_GLV_OOW], 0.5f) &&
                  near_f(f[VGPU98_GLV_R], 127.5f) && near_f(f[VGPU98_GLV_G], 255.0f),
              "a clip-coordinate vertex: %f, %f", f ? f[0] : 0.0f, f ? f[1] : 0.0f);
        grCoordinateSpace(0);

        /* TMU1's 1/w as a Voodoo3's Glide 3 sends it:
           in clip coordinates an active TMU0's q0/w; without q0 and q1 the
           vertex's q/w (no TMU w sent); with FOG_EXT each TMU its own, never
           q0. */
        {
            struct {
                float x, y, w, q, q0, fog;
            } pv[3];

            for (i = 0; i < 3; i++) {
                pv[i].x   = (float) (i & 1) * 0.1f;
                pv[i].y   = (float) (i >> 1) * 0.1f;
                pv[i].w   = 2.0f;
                pv[i].q   = 0.5f;
                pv[i].q0  = 0.25f;
                pv[i].fog = 0.0f;
            }
            grCoordinateSpace(1);
            grVertexLayout(0x20, 0, 0);  /* no RGB */
            grVertexLayout(0x04, 12, 1); /* Q */
            grVertexLayout(0x50, 16, 1); /* Q0 */
            grTexCombine(0, 1, 0, 1, 0, FXFALSE, FXFALSE); /* TMU0 active */
            grDrawTriangle(&pv[0], &pv[1], &pv[2]);
            grFinish();
            f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
            CHECK(f && near_f(f[VGPU98_GLV_OOW], 0.25f) && near_f(f[VGPU98_GLV_OOW0], 0.125f) && near_f(f[VGPU98_GLV_OOW1], 0.125f),
                  "clip coordinates: TMU1's 1/w not an active TMU0's q0/w: %g", f ? (double) f[VGPU98_GLV_OOW1] : 0.0);
            grTexCombine(0, 0, 0, 0, 0, FXFALSE, FXFALSE); /* TMU0 not active */
            grDrawTriangle(&pv[0], &pv[1], &pv[2]);
            grFinish();
            f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
            CHECK(f && near_f(f[VGPU98_GLV_OOW1], 0.25f), "clip coordinates: TMU1's 1/w not the vertex's q/w");
            grVertexLayout(0x50, 0, 0); /* no Q0: both TMUs keep q/w */
            grDrawTriangle(&pv[0], &pv[1], &pv[2]);
            grFinish();
            f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
            CHECK(f && near_f(f[VGPU98_GLV_OOW0], 0.25f) && near_f(f[VGPU98_GLV_OOW1], 0.25f),
                  "clip coordinates without q0 or q1: the TMUs' 1/w not q/w");
            /* Clip coordinates with W, Q, Q0 and FOG_EXT: TMU0 q0/w, TMU1
               1/w (each TMU its own, the fog coordinate in the vertex's W). */
            grVertexLayout(0x50, 16, 1); /* Q0 */
            grVertexLayout(0x05, 20, 1); /* FOG_EXT */
            grTexCombine(0, 1, 0, 1, 0, FXFALSE, FXFALSE);
            grDrawTriangle(&pv[0], &pv[1], &pv[2]);
            grFinish();
            f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
            CHECK(f && near_f(f[VGPU98_GLV_OOW0], 0.125f) && near_f(f[VGPU98_GLV_OOW1], 0.5f),
                  "clip coordinates with FOG_EXT: TMU1's 1/w not 1/w: %g", f ? (double) f[VGPU98_GLV_OOW1] : 0.0);
            /* Window coordinates with FOG_EXT (W off): TMU1 the vertex's Q,
               not q0; without Q, q0 again. */
            grCoordinateSpace(0);
            grVertexLayout(0x03, 0, 0); /* no W */
            grDrawTriangle(&pv[0], &pv[1], &pv[2]);
            grFinish();
            f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
            CHECK(f && (f[VGPU98_GLV_OOW0] == 0.25f) && (f[VGPU98_GLV_OOW1] == 0.5f), "FOG_EXT: TMU1's 1/w not the vertex's Q");
            grVertexLayout(0x04, 0, 0); /* no Q */
            grDrawTriangle(&pv[0], &pv[1], &pv[2]);
            grFinish();
            f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
            CHECK(f && (f[VGPU98_GLV_OOW1] == 0.25f), "FOG_EXT without Q: TMU1's 1/w not q0");
            grVertexLayout(0x05, 0, 0);
            grVertexLayout(0x50, 0, 0);
            /* Clip coordinates without ST1, the TMUs' textures of different
               aspects: TMU1's s and t TMU0's as sent, at TMU0's scale. */
            {
                struct {
                    FxI32 small_lod, large_lod, aspect, format;
                    void *data;
                } t0 = { 0, 7, 1, 0xa, NULL }, t1 = { 0, 7, 0, 0xa, NULL };

                grCoordinateSpace(1);
                grVertexLayout(0x03, 8, 1);  /* W */
                grVertexLayout(0x40, 12, 1); /* ST0 (q and q0 as s and t) */
                grTexSource(0, 0, 3, (GrTexInfo *) &t0);
                grTexSource(1, 0, 3, (GrTexInfo *) &t1);
                grDrawTriangle(&pv[0], &pv[1], &pv[2]);
                grFinish();
                f = last_pkt(VGPU98_GL_DRAW) ? draw_vertex(last_pkt(VGPU98_GL_DRAW), 0) : NULL;
                CHECK(f && (f[VGPU98_GLV_SOW1] == f[VGPU98_GLV_SOW0]) && (f[VGPU98_GLV_TOW1] == f[VGPU98_GLV_TOW0]),
                      "clip coordinates without ST1: TMU1's s and t not TMU0's as sent");
                grVertexLayout(0x40, 0, 0);
                grCoordinateSpace(0);
            }
            grVertexLayout(0x20, 12, 1); /* RGB again */
            grTexCombine(0, 0, 0, 0, 0, FXFALSE, FXFALSE);
        }

        /* The clip scales kept in the saved state: a 2:1 texture's t at 128. */
        {
            struct {
                FxI32 small_lod, large_lod, aspect, format;
                void *data;
            } ti = { 0, 7, 1, 0xa, NULL };
            struct {
                float x, y, w, s, t;
            } sv = { 0.0f, 0.0f, 1.0f, 1.0f, 1.0f };
            static DWORD keep[512];

            grCoordinateSpace(1);
            grVertexLayout(0x20, 0, 0); /* no RGB */
            grVertexLayout(0x40, 12, 1); /* ST0 */
            grTexSource(0, 0, 3, (GrTexInfo *) &ti);
            grGlideGetState(keep);
            ti.aspect = 0;
            grTexSource(0, 0, 3, (GrTexInfo *) &ti);
            grGlideSetState(keep);
            grDrawPoint(&sv);
            grFinish();
            k = last_pkt(VGPU98_GL_DRAW);
            CHECK(k && near_f(draw_vertex(k, 0)[VGPU98_GLV_SOW0], 256.0f) && near_f(draw_vertex(k, 0)[VGPU98_GLV_TOW0], 128.0f),
                  "the clip scales after the saved state: t %f", k ? draw_vertex(k, 0)[VGPU98_GLV_TOW0] : 0.0f);
            grVertexLayout(0x40, 0, 0);
            grCoordinateSpace(0);
        }
        /* The saved state, set again. */
        CHECK((grGet(0x06, 4, &sz) == 4) && (sz > 0) && (sz <= (FxI32) sizeof(saved)), "GR_GLIDE_STATE_SIZE %d", sz);
        grConstantColorValue(0x11223344u);
        grGlideGetState(saved);
        grConstantColorValue(0x55667788u);
        grGlideSetState(saved);
        grFinish();
        CHECK(reg_sent(VGPU98_GS_CONSTANT, 0) == 0x11223344u, "the constant colour not restored: %x",
              reg_sent(VGPU98_GS_CONSTANT, 0));
        /* The resolutions: 16 bytes each, as many as with a buffer. */
        {
            const FxI32 any[4] = { -1, -1, -1, -1 };
            static FxI32 out[24 * 9 * 3 * 2 * 4];

            n = grQueryResolutions((const GrResolution *) any, NULL);
            CHECK((n > 0) && !(n % 16) && (grQueryResolutions((const GrResolution *) any, (GrResolution *) out) == n),
                  "grQueryResolutions: %d bytes", n);
        }
        CHECK(grReset(0x29) == FXTRUE, "grReset(GR_VERTEX_PARAMETER)");
        CHECK(grSstWinClose(1) == FXTRUE, "grSstWinClose");
        CHECK(grSelectContext(1) == FXFALSE, "a closed window's context selected");
    } else if (!strcmp(scenario, "textures")) {
        /* Glide 3's textures: log2 numbers, the 6666 palette and P_8 after
           it, mirroring. */
        static DWORD pal[256];
        struct {
            FxI32 small_lod, large_lod, aspect, format;
            void *data;
        } info;
        const pkt_t *k;
        int          i;

        machine_glide(VGPU98_DEBUG_EVENTS);
        CHECK(grSstWinOpen(0, 7, 0, 0, 0, 2, 1) != 0, "grSstWinOpen refused");
        info.small_lod = 3;
        info.large_lod = 7;
        info.aspect    = 1;
        info.format    = 0x3;
        CHECK(grTexTextureMemRequired(3, (GrTexInfo *) &info) == 10912, "grTexTextureMemRequired %u",
              grTexTextureMemRequired(3, (GrTexInfo *) &info));
        for (i = 0; i < 256; i++)
            pal[i] = 0x80c0ff40u;
        grTexDownloadTable(3, pal);
        info.format = 0x5;
        grTexSource(1, 0x2000, 3, (GrTexInfo *) &info);
        grTexClampMode(1, 2, 1);
        grFinish();
        k = nth_pkt(VGPU98_GL_PALETTE, 0);
        /* Each channel's top six bits, widened again. */
        CHECK(k && (k->p[3] == 0x82c3ff41u), "the 6666 palette entry %x", k ? k->p[3] : 0);
        k = last_pkt(VGPU98_GL_STATE);
        CHECK(k && (k->p[1] <= VGPU98_GS_TMU(1) + VGPU98_GS_TEX_INFO) &&
                  (k->p[2 + VGPU98_GS_TMU(1) + VGPU98_GS_TEX_INFO - k->p[1]] == VGPU98_GS_TEXINFO(0x6, 3, 7, 1, 3, 1)) &&
                  (k->p[2 + VGPU98_GS_TMU(1) + VGPU98_GS_TEX_CLAMP - k->p[1]] == (2 | (1 << 8))),
              "P_8 after the 6666 palette, and mirroring");
        CHECK(grSstWinClose(1) == FXTRUE, "grSstWinClose");
    } else {
        fprintf(stderr, "test_glide3x: unknown scenario %s\n", scenario);
        return 2;
    }
#endif
    CHECK(m.dc_open == 0, "a display DC left open");
    /* The writer cuts a line at 159 characters: only the end of a long
       path's. */
    {
        int i;

        for (i = 0; i < n_lines; i++) {
            const char *text = strstr(lines[i], ": ");

            CHECK(!cut[i] || (text && (!strncmp(text + 2, "file ", 5) || !strncmp(text + 2, "program ", 8))),
                  "a line cut at the writer's limit: %s", lines[i]);
        }
    }
    if (failures || getenv("TEST_GLIDE_SHOW")) /* TEST_GLIDE_SHOW: the lines, passed or not */
        show_lines();
    printf("%s: %s, %d lines, %d failed\n",
#ifdef TEST_GLIDE3
           "test_glide3x",
#else
           "test_glide2x",
#endif
           scenario, n_lines, failures);
    return failures ? 1 : 0;
}

int
main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: test_glide SCENARIO\n");
        return 2;
    }
    return run(argv[1]);
}

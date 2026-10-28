/*
 * vgpu98gl.dll: its lines to the host's log and its diagnostics:
 * the level, the ICD's check, GL errors by
 * function, the names asked for that are not there, the swap interval's
 * decisions, the report every 5 seconds and the findings, and the fault
 * catcher.
 *
 * Lines go through BAR0's DEBUG register (driver32/logline.c) under the
 * shared block's log lock, as the Glide DLLs' and the DirectX DLL's do,
 * each starting "VGPU98 opengl <stamp> <PROGRAM>: ". Event lines are
 * written at any level but Off (diag_events), the rest at Diagnostics
 * "OpenGL" alone (diag_on); nothing at Off. The level is read once, when
 * the card is found. The hot paths only count, with plain increments
 * (ogl_dg).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "gpuenv.h"
#include "logline.h"
#include "state.h"
#include "texobj.h"

int        diag_on;     /* Diagnostics "OpenGL": every line, the report, the findings */
int        diag_events; /* any level but Off: the event lines */
ogl_diag_t ogl_dg;

static volatile DWORD *log_regs; /* BAR0, for the lines; NULL: none written */
static volatile DWORD *log_lock; /* the shared block's log lock, or own_lock */
static DWORD           own_lock; /* the log's lock while the block is another build's */
static DWORD           instance; /* the DLL's own, from DllMain: its file's path */
static char            prefix[64];   /* "VGPU98 opengl <stamp> <PROGRAM>: " */
static char            program[260]; /* the program's full path */

#define ERRORS_NAMED  64 /* GL errors named, each function and code once */
#define MISSES_NAMED  32 /* names DrvGetProcAddress does not have */
#define FORMATS_NAMED 8  /* pixel formats refused */
#define SWAPS_NAMED   32 /* swap interval decisions */
#define FORMATS_TOLD  16 /* windows' pixel formats told */
#define EXT_LINE      80 /* the extensions' names a line, at most */
#define NAME_MAX      64 /* a name in a line, at most */
#define REPORT_MS     5000

/* --- Lines ------------------------------------------------------------------ */

static void
write_line(const char *fmt, va_list ap)
{
    log_line(log_regs, log_lock, prefix, fmt, ap);
}

void
ogl_event(const char *fmt, ...)
{
    va_list ap;

    if (!log_regs || !diag_events)
        return;
    va_start(ap, fmt);
    write_line(fmt, ap);
    va_end(ap);
}

void
ogl_dbg(const char *fmt, ...)
{
    va_list ap;

    if (!log_regs || !diag_on)
        return;
    va_start(ap, fmt);
    write_line(fmt, ap);
    va_end(ap);
}

/* The ring's lines (gpuenv.h): its detail at OpenGL, its events at any
   level but Off. */
void
dbg(const char *fmt, ...)
{
    va_list ap;

    if (!log_regs || !diag_on)
        return;
    va_start(ap, fmt);
    write_line(fmt, ap);
    va_end(ap);
}

static int
ring_event(const char *fmt, va_list ap)
{
    if (!log_regs || !diag_events)
        return 0;
    write_line(fmt, ap);
    return 1;
}

int
dbg_event(const char *fmt, ...)
{
    va_list ap;
    int     r;

    va_start(ap, fmt);
    r = ring_event(fmt, ap);
    va_end(ap);
    return r;
}

int
dbg_event_own(const char *fmt, ...)
{
    va_list ap;
    int     r;

    va_start(ap, fmt);
    r = ring_event(fmt, ap);
    va_end(ap);
    return r;
}

static size_t
text_length(const char *s)
{
    size_t n = 0;

    while (s[n])
        n++;
    return n;
}

/* Do two strings match, letters in either case? */
static int
same_text(const char *a, const char *b, int any_case)
{
    for (;; a++, b++) {
        char x = *a, y = *b;

        if (any_case) {
            x = ((x >= 'a') && (x <= 'z')) ? (char) (x - 'a' + 'A') : x;
            y = ((y >= 'a') && (y <= 'z')) ? (char) (y - 'a' + 'A') : y;
        }
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
}

/* At most NAME_MAX characters of a name, for a line. */
static const char *
short_name(const char *s, char *buf)
{
    size_t n = 0;

    if (!s)
        return "(none)";
    while (s[n] && (n < NAME_MAX)) {
        buf[n] = s[n];
        n++;
    }
    buf[n] = 0;
    return buf;
}

/* Every line's start: the DLL's tag and the program's file name, without
   its folder (at most 20 characters, so every line fits after it; the full
   path has a line of its own at OpenGL). */
static void
make_prefix(void)
{
    static const char tag[] = "VGPU98 opengl " VGPU98_BUILD_STAMP;
    const char       *name  = program;
    const char       *s;
    size_t            n = 0, k = 0;

    if (!GetModuleFileNameA(0, program, sizeof(program)))
        program[0] = 0;
    program[sizeof(program) - 1] = 0;
    for (s = program; *s; s++) {
        if ((*s == '\\') || (*s == '/') || (*s == ':'))
            name = s + 1;
    }
    for (s = tag; *s && (n + 1 < sizeof(prefix)); s++)
        prefix[n++] = *s;
    if (n + 1 < sizeof(prefix))
        prefix[n++] = ' ';
    for (s = name; *s && (k < 20) && (n + 1 < sizeof(prefix)); s++, k++)
        prefix[n++] = *s;
    while (k && (prefix[n - 1] == ' ')) { /* a name cut at a space */
        n--;
        k--;
    }
    if (!k && (n + 1 < sizeof(prefix)))
        prefix[n++] = '?';
    for (s = ": "; *s && (n + 1 < sizeof(prefix)); s++)
        prefix[n++] = *s;
    prefix[n] = 0;
}

/* --- The level and the check --------------------------------------------------- */

void
ogl_diag_instance(DWORD dll_instance)
{
    instance = dll_instance;
}

void
ogl_diag_start(volatile DWORD *r, vgpu98_shared_t *s)
{
    const DWORD debug = r[VGPU98_REG_DEBUG / 4];

    diag_on     = (debug & VGPU98_DEBUG_OPENGL) != 0;
    diag_events = (debug & (VGPU98_DEBUG_EVENTS | VGPU98_DEBUG_LISTENING | VGPU98_DEBUG_GLIDE | VGPU98_DEBUG_OPENGL)) != 0;
    log_lock    = s ? &s->log_lock : &own_lock;
    make_prefix();
    log_regs = r;
}

void
ogl_diag_stop(void)
{
    log_regs = NULL;
}

/* The DLL's own file and whether its folder is the system folder; the
   program's file; the DLL's build against the display driver's (another
   means an old copy of one of them: an event at every level). */
void
ogl_diag_check(const vgpu98_escinfo_t *info)
{
    char  path[260], system[260], stamp[sizeof(info->stamp) + 1];
    DWORD folder = 0, k;
    int   has_folder = 0;

    if (info->size < sizeof(*info))
        ogl_event("possible stale OpenGL driver: built %s; the display driver gives no stamp (an older build)",
                  VGPU98_BUILD_STAMP);
    else {
        memcpy(stamp, info->stamp, sizeof(info->stamp));
        stamp[sizeof(info->stamp)] = 0;
        if (!same_text(stamp, VGPU98_BUILD_STAMP, 0))
            ogl_event("possible stale OpenGL driver: built %s, the display driver %s", VGPU98_BUILD_STAMP, stamp);
        else
            ogl_dbg("build %s, the display driver's %s", VGPU98_BUILD_STAMP, stamp);
    }
    if (!diag_on)
        return;
    path[0] = system[0] = 0;
    if (!GetModuleFileNameA(instance, path, sizeof(path)))
        path[0] = 0;
    path[sizeof(path) - 1] = 0;
    if (!GetSystemDirectoryA(system, sizeof(system)) || (text_length(system) >= sizeof(system) - 1))
        system[0] = 0;
    for (k = 0; path[k]; k++) {
        if (path[k] == '\\') {
            folder     = k;
            has_folder = 1;
        }
    }
    if (!path[0])
        ogl_dbg("file not known");
    else {
        int in_system = 0;

        if (has_folder) {
            path[folder] = 0;
            in_system    = system[0] && same_text(path, system, 1);
            path[folder] = '\\';
        }
        /* The verdict first: a long path may run past the line's end. */
        if (in_system)
            ogl_dbg("file (the system folder): %s", path);
        else
            ogl_dbg("file (not the system folder, %s): %s", system[0] ? system : "not known", path);
    }
    ogl_dbg("program %s", program[0] ? program : "not known");
}

void
ogl_diag_loaded(DWORD command_set)
{
    const char *s = ogl_ext_string();
    DWORD       n = 0;

    for (; *s; s++)
        n += *s == ' ';
    if (n)
        ogl_event("loaded; OpenGL 1.1 with %u extensions (the device's command set %u.%u)", n, command_set >> 16,
                  command_set & 0xffff);
    else
        ogl_event("loaded; OpenGL 1.1 alone (the device's command set %u.%u: an older 86Box)", command_set >> 16,
                  command_set & 0xffff);
    /* At OpenGL, the extensions offered, as many names a line as fit */
    if (diag_on && n) {
        char        line[EXT_LINE + 1];
        size_t      k = 0;
        const char *p = ogl_ext_string();

        while (*p) {
            const char *e = p;
            size_t      len;

            while (*e && (*e != ' '))
                e++;
            len = (size_t) (e - p);
            if (k && (k + 1 + len > EXT_LINE)) {
                line[k] = 0;
                ogl_dbg("extensions offered: %s", line);
                k = 0;
            }
            if (k)
                line[k++] = ' ';
            while ((p < e) && (k < EXT_LINE))
                line[k++] = *p++;
            p = *e ? e + 1 : e;
        }
        if (k) {
            line[k] = 0;
            ogl_dbg("extensions offered: %s", line);
        }
    }
}

/* At OpenGL, a window's pixel format as set (the first 16 windows). */
void
ogl_diag_pixel_format(DWORD hwnd, int pixel_format, DWORD format)
{
    static DWORD n;
    const DWORD  r = (DWORD) ogl_format_bits(format, OGL_BITS_RED), g = (DWORD) ogl_format_bits(format, OGL_BITS_GREEN);
    const DWORD  b = (DWORD) ogl_format_bits(format, OGL_BITS_BLUE), a = (DWORD) ogl_format_bits(format, OGL_BITS_ALPHA);
    const DWORD  depth   = (DWORD) ogl_format_bits(format, OGL_BITS_DEPTH);
    const DWORD  stencil = (DWORD) ogl_format_bits(format, OGL_BITS_STENCIL);
    const char  *buffers = (format & VGPU98_OGL_DOUBLE) ? "double-buffered" : "single-buffered";

    if (!diag_on || (n >= FORMATS_TOLD))
        return;
    n++;
    if (a)
        ogl_dbg("window %x: pixel format %d: colour %u-%u-%u-%u, depth %u, stencil %u, %s", hwnd, pixel_format, r, g, b, a,
                depth, stencil, buffers);
    else
        ogl_dbg("window %x: pixel format %d: colour %u-%u-%u, depth %u, stencil %u, %s", hwnd, pixel_format, r, g, b, depth,
                stencil, buffers);
}

/* --- GL errors and what is not there -------------------------------------------- */

static const char *
error_name(GLenum e)
{
    static const char *const names[] = { "GL_INVALID_ENUM",    "GL_INVALID_VALUE",    "GL_INVALID_OPERATION",
                                         "GL_STACK_OVERFLOW", "GL_STACK_UNDERFLOW", "GL_OUT_OF_MEMORY" };

    return ((e >= GL_INVALID_ENUM) && (e <= GL_OUT_OF_MEMORY)) ? names[e - GL_INVALID_ENUM] : "an error";
}

void
ogl_diag_error(const char *func, GLenum e)
{
    static struct {
        const char *func;
        GLenum      e;
    } seen[ERRORS_NAMED];
    static DWORD n_seen;
    static int   full_told;
    char         buf[NAME_MAX + 1];
    const char  *name = func ? func : "?";
    DWORD        i;

    ogl_dg.errors[((e >= GL_INVALID_ENUM) && (e <= GL_OUT_OF_MEMORY)) ? e - GL_INVALID_ENUM : 6]++;
    /* The same function's same error was said (two threads at once may
       both say a new one: a diagnostic's race). */
    for (i = 0; (i < n_seen) && (i < ERRORS_NAMED); i++) {
        if ((seen[i].func == func) && (seen[i].e == e))
            return;
    }
    if (n_seen >= ERRORS_NAMED) {
        if (!full_told) {
            full_told = 1;
            ogl_dbg("more kinds of GL error are counted in the report, not named");
        }
        return;
    }
    seen[n_seen].func = func;
    seen[n_seen].e    = e;
    n_seen++;
    if ((name[0] == 'o') && (name[1] == 'g') && (name[2] == 'l') && (name[3] == '_'))
        name += 4; /* the entry point's GL name */
    ogl_dbg("GL error %s in %s", error_name(e), short_name(name, buf));
}

void
ogl_diag_miss(const char *name)
{
    static char  seen[MISSES_NAMED][NAME_MAX + 1];
    static DWORD n_seen;
    static int   full_told;
    char         buf[NAME_MAX + 1];
    DWORD        i;

    short_name(name, buf);
    for (i = 0; i < n_seen; i++) {
        if (same_text(seen[i], buf, 0))
            return;
    }
    if (n_seen >= MISSES_NAMED) {
        if (!full_told) {
            full_told = 1;
            ogl_dbg("more functions asked for are not named");
        }
        return;
    }
    memcpy(seen[n_seen++], buf, sizeof(buf));
    ogl_dbg("no function %s", buf);
}

void
ogl_diag_format(DWORD hdc, LONG pixel_format, int formats, DWORD bpp)
{
    static DWORD n;

    if (n >= FORMATS_NAMED)
        return;
    n++;
    ogl_dbg("pixel format %d refused for DC %x: %d formats at %u bits a pixel%s", (int) pixel_format, hdc, formats, bpp,
            (n == FORMATS_NAMED) ? " (no more of these lines)" : "");
}

/* --- Swap decisions ------------------------------------------------------------- */

void
ogl_diag_swap(ogl_window_t *w, DWORD interval, int why, DWORD rects)
{
    static DWORD n;
    const DWORD  told = interval | ((DWORD) why << 8);

    if (w->swap_told == told)
        return;
    w->swap_told = told;
    if (n >= SWAPS_NAMED)
        return;
    n++;
    switch (why) {
        case OGL_SWAP_SENT:
            ogl_dbg("window %x: swap interval %u sent (the window covers the screen)", w->hwnd, interval);
            break;
        case OGL_SWAP_DESKTOP:
            ogl_dbg("window %x: swap interval %u not sent (a window on the desktop: it swaps at once)", w->hwnd, interval);
            break;
        case OGL_SWAP_COVERED:
            ogl_dbg("window %x: swap interval %u not sent (covered in part, %u rectangles visible: it swaps at once)", w->hwnd,
                    interval, rects);
            break;
        case OGL_SWAP_CURSOR:
            ogl_dbg("window %x: swap interval %u not sent (the cursor is kept out of it: it swaps at once)", w->hwnd, interval);
            break;
        default:
            ogl_dbg("window %x: swap interval 0: swaps at once", w->hwnd);
            break;
    }
    if (n == SWAPS_NAMED)
        ogl_dbg("no more swap interval lines");
}

/* --- The report and the findings ------------------------------------------------ */

static const char *
filter_name(GLenum f)
{
    switch (f) {
        case GL_NEAREST:
            return "NEAREST";
        case GL_LINEAR:
            return "LINEAR";
        case GL_NEAREST_MIPMAP_NEAREST:
            return "NEAREST_MIPMAP_NEAREST";
        case GL_LINEAR_MIPMAP_NEAREST:
            return "LINEAR_MIPMAP_NEAREST";
        case GL_NEAREST_MIPMAP_LINEAR:
            return "NEAREST_MIPMAP_LINEAR";
        case GL_LINEAR_MIPMAP_LINEAR:
            return "LINEAR_MIPMAP_LINEAR";
        default:
            return "?";
    }
}

/* A unit's texturing enabled with a texture that cannot draw (incomplete,
   or never given an image): noted when the registers choose the unit's
   texture (texobj.c), told once draws come with it (draw.c). */
void
ogl_diag_untextured(int unit, const ogl_tex_t *t)
{
    ogl_untex_t *u = &ogl_dg.untex[unit & 1];
    DWORD        k;

    u->on = t != NULL;
    if (!t)
        return;
    u->name   = t->name;
    u->w      = t->level[0].w;
    u->h      = t->level[0].h;
    u->ifmt   = (DWORD) t->level[0].ifmt;
    u->filter = t->p.min_filter;
    u->levels = 0;
    for (k = 0; k < OGL_LEVELS; k++)
        u->levels += t->level[k].base != 0;
}

void
ogl_diag_untextured_draw(void)
{
    static DWORD draws;
    static int   told;
    int          k;

    if (told || (++draws < 50))
        return;
    told = 1;
    for (k = 0; k < 2; k++) {
        const ogl_untex_t *u = &ogl_dg.untex[k];

        if (!u->on)
            continue;
        if (!u->levels)
            ogl_dbg("possible incomplete texture: unit %u's texture %u has no image, drawn untextured (%u draws)", k, u->name,
                    draws);
        else {
            ogl_dbg("possible incomplete texture: unit %u's texture %u drawn untextured (%u draws)", k, u->name, draws);
            ogl_dbg("  its level 0 %ux%u, format %x; %u levels defined; minification %s", u->w, u->h, u->ifmt, u->levels,
                    filter_name(u->filter));
        }
        break;
    }
}

/* At each SwapBuffers: the report every 5 seconds, and the
   slow path's finding. */
void
ogl_diag_swapped(void)
{
    static DWORD last_ms, slow_run;
    static int   slow_told;
    const DWORD  now = GetTickCount();
    DWORD        ds, cpu, all, errors, i;

    ogl_dg.swaps++;
    if (!diag_on)
        return;
    if (!last_ms) {
        last_ms = now ? now : 1;
        memset(&ogl_dg.swaps, 0, sizeof(ogl_dg) - offsetof(ogl_diag_t, swaps));
        return;
    }
    if (now - last_ms < REPORT_MS)
        return;
    ds     = (now - last_ms + 50) / 100;
    cpu    = ogl_dg.prims_cpu[0] + ogl_dg.prims_cpu[1] + ogl_dg.prims_cpu[2];
    all    = cpu + ogl_dg.prims_dev + ogl_dg.arrays_direct;
    errors = 0;
    for (i = 0; i < 7; i++)
        errors += ogl_dg.errors[i];
    /* Short lines: each fits after the longest prefix */
    ogl_dbg("report, %u.%u s: %u swaps; primitives: %u on the device, %u on the ICD's path", ds / 10, ds % 10, ogl_dg.swaps,
            ogl_dg.prims_dev, cpu);
    if (ogl_dg.arrays_direct || ogl_dg.lists) /* run 001's line of zeros, left out */
        ogl_dbg("  arrays drawn direct %u, display lists called %u", ogl_dg.arrays_direct, ogl_dg.lists);
    if (cpu)
        ogl_dbg("  the ICD's path: materials in glBegin %u, polygon modes %u, stippled strips %u", ogl_dg.prims_cpu[0],
                ogl_dg.prims_cpu[1], ogl_dg.prims_cpu[2]);
    if (errors)
        ogl_dbg("  GL errors %u: enum %u, value %u, operation %u, stack %u, out of memory %u", errors, ogl_dg.errors[0],
                ogl_dg.errors[1], ogl_dg.errors[2], ogl_dg.errors[3] + ogl_dg.errors[4], ogl_dg.errors[5]);
    /* The ICD's own path drawing at least half of 1000 or more primitives,
       three report lines in a row */
    if ((all >= 1000) && (cpu * 2 >= all)) {
        if ((++slow_run >= 3) && !slow_told) {
            slow_told = 1;
            ogl_dbg("possible slow path: the ICD's own path drew %u of %u primitives, three reports in a row", cpu, all);
        }
    } else
        slow_run = 0;
    memset(&ogl_dg.swaps, 0, sizeof(ogl_dg) - offsetof(ogl_diag_t, swaps));
    last_ms = now;
}

/* --- The fault catcher ---------------------------------------------------------- */

/* EXCEPTION_RECORD and EXCEPTION_POINTERS, as far as the catcher reads
   them, and CONTEXT's offsets of Eip and Esp (i386). */
typedef struct exc_record {
    DWORD code;
    DWORD flags;
    void *next;
    DWORD address;
} exc_record_t;

typedef struct exc_pointers {
    exc_record_t  *rec;
    const uint8_t *ctx;
} exc_pointers_t;

#define CTX_EIP 0xb8
#define CTX_ESP 0xc4

static ogl_filter_t   previous_filter;
static volatile DWORD in_filter;
static volatile DWORD placed;

/* The DLL's image's bytes, from its PE header. */
static DWORD
image_size(void)
{
    const uint8_t *b = (const uint8_t *) (uintptr_t) instance;
    DWORD          pe, size;

    if (!b)
        return 0;
    memcpy(&pe, b + 0x3c, 4);
    if (pe > 0x1000)
        return 0;
    memcpy(&size, b + pe + 80, 4);
    return size;
}

/* A fault the program does not handle: in the DLL, an event with the
   fault, its place in the DLL and the DLL's return addresses on the stack
   (vgpu98gl.map names them). Then the program's own filter, or Windows'. */
static LONG STDCALL
fault_filter(void *pointers)
{
    const exc_pointers_t *ep = (const exc_pointers_t *) pointers;

    if (ep && ep->rec && ep->ctx && !__atomic_exchange_n(&in_filter, 1, __ATOMIC_ACQ_REL)) {
        const DWORD base = instance, size = image_size();
        DWORD       eip, esp, rets[6], n_rets = 0, i;

        memcpy(&eip, ep->ctx + CTX_EIP, 4);
        memcpy(&esp, ep->ctx + CTX_ESP, 4);
        for (i = 0; (i < 256) && (n_rets < 6); i++) {
            const DWORD *at = (const DWORD *) (uintptr_t) (esp + i * 4);
            DWORD        v;

            if (IsBadReadPtr(at, 4))
                break;
            v = *at;
            if (size && (v - base < size))
                rets[n_rets++] = v - base;
        }
        while (n_rets < 6)
            rets[n_rets++] = 0;
        if (size && (eip - base < size)) {
            ogl_event("a fault in the OpenGL driver: code %08x at +%x", ep->rec->code, eip - base);
            ogl_event("  its addresses on the stack: +%x +%x +%x +%x +%x +%x", rets[0], rets[1], rets[2], rets[3], rets[4],
                      rets[5]);
        } else
            ogl_dbg("the program faulted (code %08x at %08x), not in the OpenGL driver", ep->rec->code, eip);
        __atomic_store_n(&in_filter, 0, __ATOMIC_RELEASE);
    }
    /* The program's filter from before: never the DLL's own, never one in
       memory that went. */
    {
        const ogl_filter_t prev = previous_filter;
        const DWORD        at   = (DWORD) (uintptr_t) prev;

        if (!prev || (prev == fault_filter) || ((at - instance) < image_size()) || IsBadReadPtr((const void *) prev, 4))
            return 0; /* EXCEPTION_CONTINUE_SEARCH */
        return prev(pointers);
    }
}

/* Put in place when the card is found, with the log there; the program's
   filter from before is called after it. */
void
ogl_fault_catcher(void)
{
    if (!log_regs || !diag_events || __atomic_exchange_n(&placed, 1, __ATOMIC_ACQ_REL))
        return;
    previous_filter = SetUnhandledExceptionFilter(fault_filter);
}

/* The program's filter from before back, if the DLL's is still the one in
   place (one the program set since stays). */
void
ogl_fault_catcher_end(void)
{
    ogl_filter_t now;

    if (!__atomic_exchange_n(&placed, 0, __ATOMIC_ACQ_REL))
        return;
    now = SetUnhandledExceptionFilter(previous_filter);
    if (now != fault_filter)
        SetUnhandledExceptionFilter(now);
}

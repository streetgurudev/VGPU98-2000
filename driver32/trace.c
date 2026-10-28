/*
 * vgpu98dx.dll: the DirectX 8 handshake trace (spec/directx-hal.md).
 * With Diagnostics "DirectX 8: stream audit", a line
 * for what the runtime asks of the driver and what the driver answers: the
 * queries, the surfaces and buffers it creates and destroys, the handles
 * they hold, the contexts, the first commands of each DirectX 8 context,
 * handle lookups that found nothing, Locks of buffers, point sprites, error
 * answers, clamped vertex windows. Each kind of line is bounded per process
 * (the rest counted in the report), the bounds new at each DirectX 8
 * context; off, it costs a test.
 *
 * And the flight recorder, with any Diagnostics:
 * each process's last callbacks and each context's last DrawPrimitives2
 * commands, shown when something ended badly (a context destroyed after
 * its process ended, a process that ended or left with contexts, a fault).
 * The DLL's data is shared by every process, so DDHELP's clean-up shows
 * what a process that died last did.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3d.h"

/* Lines of each kind a process may write. */
static const DWORD trace_budget[TRACE_KINDS] = { 96, 160, 192, 128, 32, 32, 32, 48, 48, 96, 16, 32, 64, 32 };

#define TRACE_PROCS 8

static struct {
    DWORD pid;
    DWORD used[TRACE_KINDS];
} procs[TRACE_PROCS];
static DWORD trace_next;
static DWORD trace_dropped[TRACE_KINDS];

int
trace_take(int kind)
{
    const DWORD pid = GetCurrentProcessId();
    DWORD       i;

    for (i = 0; (i < TRACE_PROCS) && (procs[i].pid != pid); i++)
        ;
    if (i == TRACE_PROCS) {
        /* A new process: the oldest slot. */
        i = trace_next++ % TRACE_PROCS;
        memset(&procs[i], 0, sizeof(procs[i]));
        procs[i].pid = pid;
    }
    if (procs[i].used[kind] >= trace_budget[kind]) {
        trace_dropped[kind]++;
        return 0;
    }
    procs[i].used[kind]++;
    return 1;
}

void
trace_reset(void)
{
    const DWORD pid = GetCurrentProcessId();
    DWORD       i;

    for (i = 0; i < TRACE_PROCS; i++) {
        if (procs[i].pid == pid)
            memset(procs[i].used, 0, sizeof(procs[i].used));
    }
    audit8_reset();
}

const char *
trace_kind(LPSURF s)
{
    const DWORD caps = s->ddsCaps.dwCaps;
    const DWORD c2   = s->lpSurfMore ? s->lpSurfMore->ddsCapsEx.dwCaps2 : 0;

    if (caps & DDSCAPS_EXECUTEBUFFER)
        return (c2 & DDSCAPS2_COMMANDBUFFER) ? "command buffer" : (c2 & DDSCAPS2_INDEXBUFFER) ? "index buffer" : "vertex buffer";
    if (c2 & DDSCAPS2_VOLUME)
        return (c2 & DDSCAPS2_MIPMAPSUBLEVEL) ? "volume level" : "volume";
    if (c2 & DDSCAPS2_CUBEMAP)
        return (c2 & DDSCAPS2_MIPMAPSUBLEVEL) ? "cube face level" : "cube map";
    if (caps & DDSCAPS_ZBUFFER)
        return "depth";
    if (caps & DDSCAPS_PRIMARYSURFACE)
        return "primary";
    if ((caps & DDSCAPS_TEXTURE) && (caps & DDSCAPS_3DDEVICE))
        return "texture target";
    if (caps & DDSCAPS_TEXTURE)
        return (c2 & DDSCAPS2_MIPMAPSUBLEVEL) ? "texture level" : "texture";
    if (caps & (DDSCAPS_FLIP | DDSCAPS_BACKBUFFER))
        return "back buffer";
    if (caps & DDSCAPS_3DDEVICE)
        return "target";
    return "surface";
}

const char *
trace_where(LPSURF s)
{
    const DWORD c2 = s->lpSurfMore ? s->lpSurfMore->ddsCapsEx.dwCaps2 : 0;

    if (c2 & (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE))
        return "managed";
    return (s->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) ? "system" : "video";
}

void
trace_command(const d3d_ctx_t *c, const uint8_t *at, const uint8_t *end)
{
    DWORD       w[6] = { 0, 0, 0, 0, 0, 0 };
    const DWORD have = (DWORD) (end - at) - 4;
    DWORD       i;

    for (i = 0; (i < 6) && (4 * i + 4 <= have); i++)
        w[i] = at[4 + 4 * i] | (at[5 + 4 * i] << 8) | (at[6 + 4 * i] << 16) | ((DWORD) at[7 + 4 * i] << 24);
    /* Short: a line holds 160 characters. */
    dbg(DLL_TAG " trace: context %u command %u count %u: %x %x %x %x %x %x", ctx_handle(c), at[0], at[2] | (at[3] << 8), w[0],
        w[1], w[2], w[3], w[4], w[5]);
}

/* The calling process's executable's name (its file name alone), for the
   context line and the PROC note;
   "?" if Windows does not say. */
const char *
trace_process(char *buf, DWORD size)
{
    char        path[260];
    const char *name = path;
    DWORD       n    = GetModuleFileNameA(0, path, sizeof(path));
    DWORD       i;

    if (!size)
        return buf;
    if (!n || (n >= sizeof(path))) {
        buf[0] = '?';
        buf[size > 1] = 0;
        return buf;
    }
    for (i = 0; i < n; i++) {
        if ((path[i] == '\\') || (path[i] == '/') || (path[i] == ':'))
            name = path + i + 1;
    }
    for (i = 0; name[i] && (i + 1 < size); i++)
        buf[i] = name[i];
    buf[i] = 0;
    return buf;
}

/* --- The texture trace ------------------------- */

/* fpVidMem as an offset in video memory, FFFFFFFF outside it. */
static DWORD
vmem_of(DWORD mem)
{
    return (sb && (mem >= sb->fb_linear) && (mem < sb->fb_linear + sb->vram_size)) ? mem - sb->fb_linear : 0xFFFFFFFFu;
}

DWORD
trace_vmem(LPSURF s)
{
    if (!s || !s->lpGbl || (s->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY))
        return 0xFFFFFFFFu;
    return vmem_of(s->lpGbl->fpVidMem);
}

/* At a flush that looked a stage's texture up: its handle naming another
   surface, or the same one at another address, than at the last. The
   surface before may be gone: only its pointer and address are named. */
void
tt_bind(d3d_ctx_t *c, DWORD stage, LPSURF top)
{
    const DWORD handle = c->tss[stage][D3DTSS_TEXTUREMAP];
    const DWORD mem    = top ? top->lpGbl->fpVidMem : 0;

    if (handle && (handle == c->tt_handle[stage]) && ((top != c->tt_surf[stage]) || (mem != c->tt_mem[stage]))) {
        DIAG(diag.tt_renamed++);
        if (TTRACE(TRACE_TEXBIND))
            dbg(DLL_TAG " texture trace: context %u stage %u: handle %x now names %x at %8x, not %x at %8x", ctx_handle(c), stage,
                handle, (DWORD) top, top ? trace_vmem(top) : 0xFFFFFFFFu, (DWORD) c->tt_surf[stage], vmem_of(c->tt_mem[stage]));
    }
    c->tt_handle[stage] = handle;
    c->tt_surf[stage]   = top;
    c->tt_mem[stage]    = mem;
}

/* An 8-bit texture sent with no palette bound (the GPU gets zeros). */
void
tt_nopal(d3d_ctx_t *c, DWORD stage, LPSURF top)
{
    if (!c->tt_nopal) {
        DIAG(diag.tt_dropped++);
        return;
    }
    c->tt_nopal--;
    dbg(DLL_TAG " texture trace: context %u stage %u: 8-bit texture %x (handle %x, %ux%u at %8x) with no palette bound: it reads 0",
        ctx_handle(c), stage, (DWORD) top, c->tss[stage][D3DTSS_TEXTUREMAP], top->lpGbl->wWidth, top->lpGbl->wHeight,
        trace_vmem(top));
}

/* SETPALETTE (a: the surface's handle, b: the flags) or UPDATEPALETTE (a:
   the first entry, b: how many, `first` its colour), and whether it changed
   anything. */
void
tt_palette(d3d_ctx_t *c, int update, DWORD palette, DWORD a, DWORD b, DWORD first, int changed)
{
    DIAG({
        if (update)
            diag.tt_updpal++;
        else
            diag.tt_setpal++;
    });
    if (!c->tt_pal) {
        DIAG(diag.tt_dropped++);
        return;
    }
    c->tt_pal--;
    if (update)
        dbg(DLL_TAG " texture trace: context %u: UPDATEPALETTE palette %u, %u entries from %u (the first %8x): %s", ctx_handle(c),
            palette, b, a, first, changed ? "changed" : "the same");
    else
        dbg(DLL_TAG " texture trace: context %u: SETPALETTE palette %u to surface %x, flags %x: %s", ctx_handle(c), palette, a, b,
            changed ? "changed" : "the same");
}

void
tt_texblt(d3d_ctx_t *c, DWORD hdst, DWORD hsrc, LONG x, LONG y, const RECTL *r, DWORD flags)
{
    LPSURF dst;
    LPSURF src;

    if (!c->tt_texblt) {
        DIAG(diag.tt_dropped++);
        return;
    }
    c->tt_texblt--;
    dst = hdst ? d3d7_surface(c->ddl, hdst) : NULL;
    src = d3d7_surface(c->ddl, hsrc);
    dbg(DLL_TAG " texture trace: context %u: TEXBLT %x (%s, %8x) to %x (%8x), %d,%d-%d,%d to %d,%d, flags %x", ctx_handle(c), hsrc,
        src ? trace_where(src) : "none", trace_vmem(src), hdst, trace_vmem(dst), r->left, r->top, r->right, r->bottom, x, y, flags);
}

/* A texture matrix set: the columns u and v take (rows 1-4), two lines (one
   holds 160 characters). */
void
tt_matrix(d3d_ctx_t *c, DWORD stage, const float *m)
{
    char a[4][16];
    int  col;

    DIAG(diag.tt_matrices++);
    if (!c->tt_mx) {
        DIAG(diag.tt_mx_untold++);
        return;
    }
    c->tt_mx--;
    for (col = 0; col < 2; col++)
        dbg(DLL_TAG " texture trace: context %u: TEXTURE%u's %s column: %s %s %s %s", ctx_handle(c), stage, col ? "v" : "u",
            fmt_float(a[0], m[col]), fmt_float(a[1], m[4 + col]), fmt_float(a[2], m[8 + col]), fmt_float(a[3], m[12 + col]));
}

/* A Lock of a texture: `direct`, the caller writes video memory itself (no
   SYNC; the host sees the writes by their pages' versions). */
void
tt_lock(LPSURF s, const DDHAL_LOCKDATA *d, int direct)
{
    const DWORD handle = s->lpSurfMore ? s->lpSurfMore->dwSurfaceHandle : 0;

    if (!(s->ddsCaps.dwCaps & DDSCAPS_TEXTURE) || !TTRACE(TRACE_TEXLOCK))
        return;
    if (d->bHasRect)
        dbg(DLL_TAG " texture trace: Lock of texture %x (handle %x, %ux%u at %8x), %d,%d-%d,%d, flags %x%s", (DWORD) s, handle,
            s->lpGbl->wWidth, s->lpGbl->wHeight, trace_vmem(s), d->rArea.left, d->rArea.top, d->rArea.right, d->rArea.bottom,
            d->dwFlags, direct ? ", direct" : ", after a SYNC");
    else
        dbg(DLL_TAG " texture trace: Lock of texture %x (handle %x, %ux%u at %8x), all of it, flags %x%s", (DWORD) s, handle,
            s->lpGbl->wWidth, s->lpGbl->wHeight, trace_vmem(s), d->dwFlags, direct ? ", direct" : ", after a SYNC");
}

void
trace_report(void)
{
    DWORD i;
    DWORD any = 0;

    for (i = 0; i < TRACE_LOCK + 1; i++)
        any |= trace_dropped[i];
    if (any)
        dbg(DLL_TAG " trace lines past their bounds: queries %u, surfaces %u, handles %u, gone %u, contexts %u, lookups %u, "
            "Locks %u",
            trace_dropped[0], trace_dropped[1], trace_dropped[2], trace_dropped[3], trace_dropped[4], trace_dropped[5],
            trace_dropped[6]);
    if (trace_dropped[7] | trace_dropped[8] | trace_dropped[9] | trace_dropped[10] | trace_dropped[11])
        dbg(DLL_TAG " trace lines past their bounds: points %u, answers %u, windows %u, blending %u, DIP2 %u", trace_dropped[7],
            trace_dropped[8], trace_dropped[9], trace_dropped[10], trace_dropped[11]);
    if (trace_dropped[TRACE_TEXBIND] | trace_dropped[TRACE_TEXLOCK])
        dbg(DLL_TAG " texture trace lines past their bounds: handles renamed %u, texture Locks %u", trace_dropped[TRACE_TEXBIND],
            trace_dropped[TRACE_TEXLOCK]);
    memset(trace_dropped, 0, sizeof(trace_dropped));
}

/* --- The flight recorder ------------------------------------------------------ */

#define FLIGHT_PROCS 8

static struct {
    DWORD         pid;   /* 0: free */
    DWORD         head;  /* callbacks recorded */
    DWORD         shown; /* its callbacks were shown since the last (0 none) */
    DWORD         faults; /* faults' dumps */
    DWORD         left;   /* its process's DLL_PROCESS_DETACH came, without contexts: freed at the next reap */
    flight_call_t call[FLIGHT_CALLS];
} flights[FLIGHT_PROCS];
static DWORD flight_turn;
static int   flight_detach_seen; /* a DLL_PROCESS_DETACH has come for a process (they do come) */

/* Whether a process has a live context (its ring is kept for a dump). */
static int
fl_has_context(DWORD pid)
{
    DWORD i;

    for (i = 0; i < MAX_CONTEXTS; i++) {
        if ((d3d_ctx[i].used == CTX_LIVE) && (d3d_ctx[i].pid == pid))
            return 1;
    }
    return 0;
}

/* The process's slot; a new process takes a free one, else one already
   shown, else the next in turn whose process has no live context (one
   that may yet die keeps its ring for the dump), else the next in turn.
   The process's own is looked for again at each try (two of its threads
   may make one at once). -1 only when other processes took the slot
   picked three times running. */
static int
flight_slot(DWORD pid, int make)
{
    DWORD i;
    DWORD tries;

    for (tries = 0; tries < 3; tries++) {
        DWORD pick = FLIGHT_PROCS;
        DWORD old;

        for (i = 0; i < FLIGHT_PROCS; i++) {
            if (__atomic_load_n(&flights[i].pid, __ATOMIC_ACQUIRE) == pid)
                return (int) i;
        }
        if (!make)
            return -1;
        for (i = 0; (i < FLIGHT_PROCS) && (pick == FLIGHT_PROCS); i++) {
            if (!flights[i].pid)
                pick = i;
        }
        for (i = 0; (i < FLIGHT_PROCS) && (pick == FLIGHT_PROCS); i++) {
            if (flights[i].shown || flights[i].left)
                pick = i;
        }
        for (i = 0; (i < FLIGHT_PROCS) && (pick == FLIGHT_PROCS); i++) {
            const DWORD t = __atomic_fetch_add(&flight_turn, 1, __ATOMIC_RELAXED) % FLIGHT_PROCS;

            if (!fl_has_context(flights[t].pid))
                pick = t;
        }
        if (pick == FLIGHT_PROCS)
            pick = __atomic_fetch_add(&flight_turn, 1, __ATOMIC_RELAXED) % FLIGHT_PROCS;
        old = flights[pick].pid;
        if (__atomic_compare_exchange_n(&flights[pick].pid, &old, pid, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
            flights[pick].head   = 0;
            flights[pick].shown  = 0;
            flights[pick].faults = 0;
            flights[pick].left   = 0;
            memset(flights[pick].call, 0, sizeof(flights[pick].call));
            return (int) pick;
        }
    }
    return -1;
}

flight_call_t *
flight_enter(const char *name)
{
    flight_call_t *e;
    DWORD          tid;
    DWORD          head;
    int            s;

    if (!diag_on || ((s = flight_slot(GetCurrentProcessId(), 1)) < 0))
        return NULL;
    /* A slot shown as ended that its ID comes back to is a new process's
       (Windows 98 gives an ended process's ID again). */
    if (flights[s].shown) {
        flights[s].shown  = 0;
        flights[s].faults = 0;
        memset(flights[s].call, 0, sizeof(flights[s].call));
        __atomic_store_n(&flights[s].head, 0, __ATOMIC_RELEASE);
    }
    tid  = GetCurrentThreadId();
    head = __atomic_load_n(&flights[s].head, __ATOMIC_RELAXED);
    /* A callback polled again (Flip, Lock, GetBltStatus asked until the GPU
       is done): one entry, counted. */
    if (head) {
        e = &flights[s].call[(head - 1) & (FLIGHT_CALLS - 1)];
        if ((e->name == name) && (e->thread == tid) && ((HRESULT) e->rv == DDERR_WASSTILLDRAWING)) {
            e->polls++;
            e->tick = (DWORD) (rdtsc() >> 10);
            e->a    = 0;
            e->b    = 0;
            e->rv   = FLIGHT_INSIDE;
            return e;
        }
    }
    e         = &flights[s].call[__atomic_fetch_add(&flights[s].head, 1, __ATOMIC_RELAXED) & (FLIGHT_CALLS - 1)];
    e->name   = name;
    e->thread = tid;
    e->tick   = (DWORD) (rdtsc() >> 10);
    e->a      = 0;
    e->b      = 0;
    e->polls  = 0;
    e->rv     = FLIGHT_INSIDE;
    return e;
}

static int
fl_same(const char *a, const char *b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *a == *b;
}

/* An error answer that is routine: a callback still drawing, a GUID the
   driver does not know, the vertical blank in progress, and
   ValidateTextureStageState's (ValidateDevice probing what the GPU can
   blend). */
static int
fl_routine(const char *name, DWORD v)
{
    return ((HRESULT) v == DDERR_WASSTILLDRAWING) || ((HRESULT) v == DDERR_CURRENTLYNOTAVAIL) ||
           ((HRESULT) v == DDERR_VERTICALBLANKINPROGRESS) || fl_same(name, "ValidateTextureStageState") ||
           fl_same(name, "GetScanLine");
}

void
flight_leave(flight_call_t *e, DWORD r, DWORD rv, DWORD a, DWORD b)
{
    const DWORD v = (r == DDHAL_DRIVER_HANDLED) ? rv : FLIGHT_NOTHANDLED;

    /* An entry the ring reused while this callback ran is another's. */
    if ((e->rv != FLIGHT_INSIDE) || (e->thread != GetCurrentThreadId()))
        return;
    if (!e->a && !e->b) {
        e->a = a;
        e->b = b;
    }
    e->rv = v;
    /* Error answers, not the routine ones. */
    if ((v != FLIGHT_NOTHANDLED) && v && diag_audit8 && !fl_routine(e->name, v) && TRACE(TRACE_ANSWER))
        dbg(DLL_TAG " trace: %s answered %x (figures %x, %x)", e->name, v, e->a, e->b);
}

void
flight_figures(DWORD a, DWORD b)
{
    const DWORD tid = GetCurrentThreadId();
    int         s;
    DWORD       head;
    DWORD       k;

    if (!diag_on || ((s = flight_slot(GetCurrentProcessId(), 0)) < 0))
        return;
    head = __atomic_load_n(&flights[s].head, __ATOMIC_RELAXED);
    for (k = 1; (k <= 8) && (k <= head); k++) {
        flight_call_t *e = &flights[s].call[(head - k) & (FLIGHT_CALLS - 1)];

        if ((e->thread == tid) && (e->rv == FLIGHT_INSIDE)) {
            e->a = a;
            e->b = b;
            return;
        }
    }
}

DWORD *
flight_command(d3d_ctx_t *c, const uint8_t *at, const uint8_t *end)
{
    DWORD *e;

    if (!diag_on)
        return NULL;
    e    = c->fl_cmd[c->fl_head++ & (FLIGHT_CMDS - 1)];
    e[0] = at[0] | ((DWORD) (at[2] | (at[3] << 8)) << 8) | ((DWORD) FLIGHT_RUNNING << 24);
    e[1] = (end - at >= 8) ? (at[4] | (at[5] << 8) | (at[6] << 16) | ((DWORD) at[7] << 24)) : 0;
    return e;
}

/* The lines' text, built by hand (dbg takes it as one %s). */
static char *
fl_str(char *p, char *end, const char *s)
{
    while (*s && (p < end))
        *p++ = *s++;
    return p;
}

static char *
fl_num(char *p, char *end, DWORD v, int hex)
{
    char  t[12];
    DWORD n = 0;

    do {
        const DWORD d = hex ? (v & 15) : (v % 10);

        t[n++] = (char) ((d < 10) ? '0' + d : 'A' + d - 10);
        v      = hex ? (v >> 4) : (v / 10);
    } while (v && (n < sizeof(t)));
    while (n && (p < end))
        *p++ = t[--n];
    return p;
}

/* A line's text: at most this much past the tag and the lead. */
#define FL_LINE 112

static void
fl_calls(DWORD pid)
{
    const int   s   = flight_slot(pid, 0);
    const DWORD now = (DWORD) (rdtsc() >> 10);
    DWORD     head;
    DWORD     age;
    DWORD     n;
    DWORD     k;
    DWORD     thread = 0;
    char      line[FL_LINE + 48];
    char     *p   = line;
    char     *end = line + FL_LINE;

    if (s < 0) {
        dbg(DLL_TAG " flight: process %x: no callbacks recorded", pid);
        return;
    }
    head = flights[s].head;
    n    = (head < FLIGHT_CALLS) ? head : FLIGHT_CALLS;
    /* The newest's age, by the clock rate the report measured (1024
       cycles a unit). */
    age  = n ? now - flights[s].call[(head - 1) & (FLIGHT_CALLS - 1)].tick : 0;
    if (diag_per_ms)
        dbg(DLL_TAG " flight: process %x, its last %u callbacks (oldest first; the newest %u ms ago):", pid, n,
            (DWORD) (((uint64_t) age << 10) / diag_per_ms));
    else
        dbg(DLL_TAG " flight: process %x, its last %u callbacks (oldest first; the newest %u K cycles ago):", pid, n, age);
    for (k = 0; k < n; k++) {
        const flight_call_t *e = &flights[s].call[(head - n + k) & (FLIGHT_CALLS - 1)];
        char                 item[96];
        char                *q  = item;
        char                *qe = item + sizeof(item) - 1;

        if (!k || (e->thread != thread)) {
            q = fl_str(q, qe, "[thread ");
            q = fl_num(q, qe, e->thread, 1);
            q = fl_str(q, qe, "] ");
        }
        thread = e->thread;
        q      = fl_str(q, qe, e->name ? e->name : "?");
        if (e->rv == FLIGHT_INSIDE)
            q = fl_str(q, qe, " inside");
        else if (e->rv == FLIGHT_NOTHANDLED)
            q = fl_str(q, qe, " -");
        else if (!e->rv)
            q = fl_str(q, qe, " ok");
        else {
            q = fl_str(q, qe, " ");
            q = fl_num(q, qe, e->rv, 1);
        }
        if (e->a || e->b) {
            q = fl_str(q, qe, " ");
            q = fl_num(q, qe, e->a, 1);
            q = fl_str(q, qe, "/");
            q = fl_num(q, qe, e->b, 1);
        }
        if (e->polls) {
            q = fl_str(q, qe, " (after ");
            q = fl_num(q, qe, e->polls, 0);
            q = fl_str(q, qe, " still drawing)");
        }
        *q = 0;
        if ((p > line) && (p + 2 + (q - item) > end)) {
            *p = 0;
            dbg(DLL_TAG " flight:  %s", line);
            p = line;
        }
        if (p > line)
            p = fl_str(p, line + sizeof(line) - 1, "; ");
        p = fl_str(p, line + sizeof(line) - 1, item);
    }
    if (p > line) {
        *p = 0;
        dbg(DLL_TAG " flight:  %s", line);
    }
}

static void
fl_commands(d3d_ctx_t *c)
{
    const DWORD n   = (c->fl_head < FLIGHT_CMDS) ? c->fl_head : FLIGHT_CMDS;
    char        line[FL_LINE + 48];
    char       *p   = line;
    char       *end = line + FL_LINE;
    DWORD       k;

    dbg(DLL_TAG " flight: context %u, its last %u commands (oldest first; opcode.count=first dword, ! running, F faulted, "
        "U unparsed):",
        ctx_handle(c), n);
    for (k = 0; k < n; k++) {
        const DWORD *e = c->fl_cmd[(c->fl_head - n + k) & (FLIGHT_CMDS - 1)];
        static const char *const mark[4] = { "", "!", "F", "U" };
        char                     item[32];
        char                    *q  = item;
        char                    *qe = item + sizeof(item) - 1;

        q  = fl_num(q, qe, e[0] & 0xff, 0);
        q  = fl_str(q, qe, ".");
        q  = fl_num(q, qe, (e[0] >> 8) & 0xffff, 0);
        q  = fl_str(q, qe, "=");
        q  = fl_num(q, qe, e[1], 1);
        q  = fl_str(q, qe, mark[(e[0] >> 24) & 3]);
        *q = 0;
        if ((p > line) && (p + 1 + (q - item) > end)) {
            *p = 0;
            dbg(DLL_TAG " flight:  %s", line);
            p = line;
        }
        if (p > line)
            p = fl_str(p, line + sizeof(line) - 1, " ");
        p = fl_str(p, line + sizeof(line) - 1, item);
    }
    if (p > line) {
        *p = 0;
        dbg(DLL_TAG " flight:  %s", line);
    }
}

void
flight_dump(const char *why, DWORD pid, d3d_ctx_t *c, int again)
{
    const int s = flight_slot(pid, 0);

    if (!diag_on)
        return;
    if (c && c->fl_dumped && !again)
        return;
    dbg(DLL_TAG " flight: %s (process %x%s)", why, pid, c ? (c->dx8 ? ", a DirectX 8 context" : ", a context") : "");
    if (again || (s < 0) || !flights[s].shown)
        fl_calls(pid);
    else
        dbg(DLL_TAG " flight: process %x's callbacks are shown above", pid);
    /* Shown as ended: its slot may go to a new process (a fault's leaves
       it, as the process runs on). */
    if (!again && (s >= 0))
        flights[s].shown = 1;
    if (c) {
        fl_commands(c);
        if (!again)
            c->fl_dumped = 1;
    }
}

static int fl_died(int s);

/* A slot's last callbacks on one line, newest last (as many as fit): a
   tidy exit after a failure still shows what came before it. */
static void
fl_last_line(int s)
{
    const DWORD n = (flights[s].head < 6) ? flights[s].head : 6;
    char        line[FL_LINE + 8];
    char       *p   = line;
    char       *end = line + FL_LINE;
    DWORD       k;

    if (!diag_on || !n)
        return;
    for (k = 0; k < n; k++) {
        const flight_call_t *e = &flights[s].call[(flights[s].head - n + k) & (FLIGHT_CALLS - 1)];

        if (p > line)
            p = fl_str(p, end, "; ");
        p = fl_str(p, end, e->name ? e->name : "?");
        if (e->rv == FLIGHT_INSIDE)
            p = fl_str(p, end, " inside");
        else if (e->rv == FLIGHT_NOTHANDLED)
            p = fl_str(p, end, " -");
        else if (e->rv) {
            p = fl_str(p, end, " ");
            p = fl_num(p, end, e->rv, 1);
        }
    }
    *p = 0;
    dbg(DLL_TAG " flight:  its last: %s", line);
}

void
flight_attach(DWORD pid)
{
    const int s = flight_slot(pid, 0);

    dbg(DLL_TAG " flight: process %x attaches (DLL_PROCESS_ATTACH)%s", pid, (s >= 0) ? "; a slot of its ID starts again" : "");
    /* An ID Windows gives again: the slot is the new process's, but what
       the old one left when it died is shown first. */
    if ((s >= 0) && !flights[s].shown && !flights[s].left && flights[s].head && fl_died(s))
        flight_dump("a process ended without leaving (its ID given again)", pid, NULL, 1);
    if (s >= 0) {
        flights[s].head   = 0;
        flights[s].shown  = 0;
        flights[s].faults = 0;
        flights[s].left   = 0;
        memset(flights[s].call, 0, sizeof(flights[s].call));
    }
}

void
flight_leaving(DWORD pid, int ending)
{
    const int s = flight_slot(pid, 0);
    DWORD     n;
    DWORD     k;

    /* Only a process's ending counts as DETACH seen working (Windows 98
       may pass `reserved` NULL at ExitProcess too). */
    if (ending)
        flight_detach_seen = 1;
    dbg(DLL_TAG " flight: process %x detaches (DLL_PROCESS_DETACH, %s), no live context, %u callbacks recorded", pid,
        ending ? "process ending" : "reserved NULL", (s >= 0) ? flights[s].head : 0);
    if (s < 0)
        return;
    fl_last_line(s);
    /* A process leaving without live contexts: its slot is free again,
       unless an error answer among its last callbacks says it may have
       left for a reason (3DMark ending a test it could not run). */
    n = (flights[s].head < FLIGHT_CALLS) ? flights[s].head : FLIGHT_CALLS;
    for (k = 0; diag_on && !flights[s].shown && (k < n); k++) {
        const flight_call_t *e = &flights[s].call[(flights[s].head - 1 - k) & (FLIGHT_CALLS - 1)];

        if ((e->rv != FLIGHT_INSIDE) && (e->rv != FLIGHT_NOTHANDLED) && e->rv && e->name && !fl_routine(e->name, e->rv)) {
            flight_dump("a process leaving after an error answer", pid, NULL, 0);
            return;
        }
    }
    /* Left: DirectDraw's own clean-up after ours may still record its
       callbacks here; the next reap frees the slot without a dump. */
    flights[s].left = 1;
}

/* Whether a slot's process looks to have died rather than left: a
   callback that never returned, or an error answer, among its last. */
static int
fl_died(int s)
{
    const DWORD n = (flights[s].head < FLIGHT_CALLS) ? flights[s].head : FLIGHT_CALLS;
    DWORD       k;

    for (k = 0; k < n; k++) {
        const flight_call_t *e = &flights[s].call[(flights[s].head - 1 - k) & (FLIGHT_CALLS - 1)];

        if (e->rv == FLIGHT_INSIDE)
            return 1;
        if ((e->rv != FLIGHT_NOTHANDLED) && e->rv && e->name && !fl_routine(e->name, e->rv))
            return 1;
    }
    return 0;
}

void
flight_reap(void)
{
    DWORD i;

    /* A process that died without a live context (DDHELP destroys none for
       it, and a crash brings no DLL_PROCESS_DETACH). */
    if (!diag_on)
        return;
    for (i = 0; i < FLIGHT_PROCS; i++) {
        const DWORD pid = flights[i].pid;

        if (!pid || flights[i].shown || !flights[i].head || fl_has_context(pid) || process_alive(pid))
            continue;
        /* Left as it should (its DETACH came): freed quietly. Ended without
           one: shown when it looks to have died, or when DETACH is seen to
           come for others. */
        if (flights[i].left)
            __atomic_store_n(&flights[i].pid, 0, __ATOMIC_RELEASE);
        else if (fl_died((int) i) || flight_detach_seen)
            flight_dump("a process ended without leaving", pid, NULL, 0);
        else
            __atomic_store_n(&flights[i].pid, 0, __ATOMIC_RELEASE);
    }
}

const char *
flight_current(void)
{
    const DWORD tid = GetCurrentThreadId();
    int         s;
    DWORD       head;
    DWORD       k;

    /* At Minimal the recorder is off: the event lines name no callback. */
    if (!diag_on)
        return "a callback";
    s = flight_slot(GetCurrentProcessId(), 0);
    if (s < 0)
        return "?";
    head = __atomic_load_n(&flights[s].head, __ATOMIC_RELAXED);
    for (k = 1; (k <= 8) && (k <= head); k++) {
        const flight_call_t *e = &flights[s].call[(head - k) & (FLIGHT_CALLS - 1)];

        if ((e->thread == tid) && (e->rv == FLIGHT_INSIDE) && e->name)
            return e->name;
    }
    return "?";
}

/* --- Still-drawing answers ------------------------------------------------------------ */

/* A callback's streak of still-drawing answers: from its first since the
   last answer that was not (busy_ok), answers under 100 ms apart. Shared
   by every process: a line, not a decision. */
static uint64_t busy_last[BUSY_KINDS];
static uint64_t busy_since[BUSY_KINDS]; /* 0: no streak */
static uint64_t busy_told[BUSY_KINDS];

void
busy_ok(int kind)
{
    busy_since[kind] = 0;
}

void
busy_answer(int kind)
{
    static const char *const names[BUSY_KINDS] = { "Lock", "Blt", "Flip", "GetFlipStatus", "GetBltStatus", "a buffer's Lock" };
    uint64_t *const          last  = busy_last;
    uint64_t *const          since = busy_since;
    uint64_t *const          told  = busy_told;
    const uint64_t           ms    = diag_per_ms;
    uint64_t                 now;

    diag.busy++;
    diag.busy_by[kind]++;
    if (!ms)
        return;
    now = rdtsc();
    if (!since[kind] || (now - last[kind] > 100 * ms)) {
        since[kind] = now; /* a new streak, a new episode */
        told[kind]  = 0;
    }
    last[kind] = now;
    if ((now - since[kind] >= 2000 * ms) && (!told[kind] || (now - told[kind] >= 10000 * ms))) {
        told[kind] = now;
        dbg(DLL_TAG ": %s answered still drawing for %u ms running", names[kind], (DWORD) ((now - since[kind]) / ms));
        gpu_state_line();
    }
}

void
flight_fault(d3d_ctx_t *c)
{
    const DWORD pid = GetCurrentProcessId();
    const int   s   = flight_slot(pid, 0);

    if (!diag_on || (s < 0) || (flights[s].faults >= 2))
        return;
    flights[s].faults++;
    flight_dump("after a fault", pid, c, 1);
}

/*
 * vgpu98dx.dll: entry points. DirectDraw's helper process (DDHELP) loads the
 * DLL after the display driver names it (DDGET32BITDRIVERNAME) and calls
 * DriverInit with the shared block's address; the DLL fills in its part of
 * the DirectDraw driver description there (spec/directx-hal.md).
 *
 * The DLL is loaded once, into the shared system arena, and every section is
 * shared: its globals are the same in every process that calls it.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3d.h"
#include "logline.h"

vgpu98_shared_t *sb;
volatile DWORD  *regs;
uint8_t         *vram;

static DWORD dll_instance;

/* --- Diagnostics -------------------------------------------------------- */

int        diag_on;
int        diag_events;
int        diag_audit;
int        diag_audit8;
int        diag_textures;
int        diag_shaders;
dll_diag_t diag;

static DWORD    diag_tick; /* GetTickCount at the last report; 0: none yet */
DWORD           diag_per_ms; /* the clock rate it measured */
static uint64_t diag_tsc;  /* rdtsc then */

/* The x87's control word. The Direct3D runtime leaves its precision at 24
   bits (single) unless the program asks otherwise. */
static uint16_t
fpu_cw(void)
{
    uint16_t cw;

    __asm__ volatile("fnstcw %0" : "=m"(cw));
    return cw;
}

static void
fpu_cw_load(uint16_t cw)
{
    __asm__ volatile("fldcw %0" : : "m"(cw) : "memory");
}

/* v (> 0) as six digits, 100000-999999, times 10^(*e - 5). It runs
   between the control word's changes (fmt_float), which hold it in place
   only as a call that writes memory: noipa keeps gcc from inlining it or
   taking it as a pure function it may move past them. */
static int __attribute__((noipa))
fmt_scale(double v, int *e)
{
    int m;

    *e = 5;
    while (v >= 1000000.0) {
        v /= 10.0;
        (*e)++;
    }
    while (v < 100000.0) {
        v *= 10.0;
        (*e)--;
    }
    m = (int) (v + 0.5);
    if (m > 999999) {
        m = 100000;
        (*e)++;
    }
    return m;
}

/* A float as the report shows it, in `buf` (16 bytes): six significant
   digits, positional from 0.001 to 999999 ("0.25", "1000"), else as
   d.ddde+NN ("1.5e+07"); "inf", "-inf", "nan". */
const char *
fmt_float(char *buf, float f)
{
    char     *p = buf;
    char      dig[6];
    double    v = f;
    int       e; /* the power of ten of dig[0] */
    int       m;
    int       i;
    uint16_t  cw;

    if (v != v)
        return "nan";
    if (v < 0.0) {
        *p++ = '-';
        v    = -v;
    }
    if (v > 3.5e38) {
        memcpy(p, "inf", 4);
        return buf;
    }
    if (v == 0.0) {
        memcpy(p, "0", 2);
        return buf;
    }
    /* The scaling in double precision, whatever the program set. */
    cw = fpu_cw();
    fpu_cw_load((uint16_t) ((cw & ~0x300u) | 0x200u));
    m = fmt_scale(v, &e);
    fpu_cw_load(cw);
    for (i = 5; i >= 0; i--) {
        dig[i] = (char) ('0' + m % 10);
        m /= 10;
    }
    if ((e >= -3) && (e <= 5)) {
        if (e < 0) {
            *p++ = '0';
            *p++ = '.';
            for (i = -1; i > e; i--)
                *p++ = '0';
            for (i = 0; i < 6; i++)
                *p++ = dig[i];
        } else {
            for (i = 0; i <= e; i++)
                *p++ = dig[i];
            if (e < 5) {
                *p++ = '.';
                for (i = e + 1; i < 6; i++)
                    *p++ = dig[i];
            }
        }
    } else {
        *p++ = dig[0];
        *p++ = '.';
        for (i = 1; i < 6; i++)
            *p++ = dig[i];
    }
    /* Trailing zeros of a fraction go, and a point left alone. */
    if (e != 5) {
        while (p[-1] == '0')
            p--;
        if (p[-1] == '.')
            p--;
    }
    if ((e < -3) || (e > 5)) {
        const int x = (e < 0) ? -e : e;

        *p++ = 'e';
        *p++ = (e < 0) ? '-' : '+';
        *p++ = (char) ('0' + x / 10);
        *p++ = (char) ('0' + x % 10);
    }
    *p = 0;
    return buf;
}

/* A line to the host's log through BAR0's DEBUG register (logline.c).
   Formats: %s %c %d %u %x, with a width for the numbers. The Glide DLLs
   write there too, so the line lock is the shared block's;
   before DriverInit has the block, the DLL's
   own. */
static void
dbg_write(const char *fmt, va_list ap)
{
    static DWORD own;

    log_line(regs, sb ? &sb->log_lock : &own, NULL, fmt, ap);
}

/* A line of the diagnostics ("Diagnostics" in the card's settings, any
   level but Off and Minimal). */
void
dbg(const char *fmt, ...)
{
    va_list ap;

    if (!regs || !diag_on)
        return;
    va_start(ap, fmt);
    dbg_write(fmt, ap);
    va_end(ap);
}

/* At Minimal, an event line from one place (its format) is written its first
   three times, then again after 10 seconds without one from there.
   The places share a table; the
   last entry stands for every place past it. */
#define EVENT_PLACES   32
#define EVENT_FIRST    3
#define EVENT_QUIET_MS 10000

static struct {
    const char *fmt;
    DWORD       n;
    DWORD       last;
} event_place[EVENT_PLACES];

static int
event_due(const char *fmt)
{
    const DWORD now = GetTickCount();
    DWORD       i;
    int         due;

    for (i = 0; i < EVENT_PLACES - 1; i++) {
        const char *seen = NULL;

        if (__atomic_compare_exchange_n(&event_place[i].fmt, &seen, fmt, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) || (seen == fmt))
            break;
    }
    due = (event_place[i].n < EVENT_FIRST) || (now - event_place[i].last >= EVENT_QUIET_MS);
    if (event_place[i].n < EVENT_FIRST)
        event_place[i].n++;
    event_place[i].last = now;
    return due;
}

int
dbg_event(const char *fmt, ...)
{
    va_list ap;

    if (!regs || !(diag_on || (diag_events && event_due(fmt))))
        return 0;
    va_start(ap, fmt);
    dbg_write(fmt, ap);
    va_end(ap);
    return 1;
}

int
dbg_event_own(const char *fmt, ...)
{
    va_list ap;

    if (!regs || !(diag_on || diag_events))
        return 0;
    va_start(ap, fmt);
    dbg_write(fmt, ap);
    va_end(ap);
    return 1;
}

/* --- Callback times --------------- */

/* Every guarded callback's time and calls since the last report, by place:
   the guard's wrapper hands them in with diag_on. */
#define CB_TIMED 48

typedef struct {
    const char *name;
    uint64_t    cycles;
    DWORD       calls;
} cb_entry_t;

static cb_entry_t cb_time[CB_TIMED];
static DWORD      cb_timed_n;

/* The last report: its thread, when it began and how long it took, so the
   callback it was written in is not charged with it. */
static DWORD    report_tid;
static uint64_t report_at;
static DWORD    report_cycles;

void
callback_time(BYTE *slot, const char *name, uint64_t t0, uint64_t t1)
{
    uint64_t cycles = t1 - t0;
    DWORD    k      = *slot;

    if ((report_at >= t0) && (report_at < t1) && (report_tid == GetCurrentThreadId()))
        cycles = (cycles > report_cycles) ? cycles - report_cycles : 0;
    if (!k) {
        BYTE mine;
        BYTE seen = 0;

        if (__atomic_load_n(&cb_timed_n, __ATOMIC_RELAXED) >= CB_TIMED)
            return;
        k = __atomic_add_fetch(&cb_timed_n, 1, __ATOMIC_RELAXED);
        if (k > CB_TIMED)
            return;
        mine = (BYTE) k;
        if (__atomic_compare_exchange_n(slot, &seen, mine, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            __atomic_store_n(&cb_time[k - 1].name, name, __ATOMIC_RELEASE);
        else
            k = seen; /* another thread's first call took its place */
    }
    cb_time[k - 1].cycles += cycles;
    cb_time[k - 1].calls++;
}

static int
same_name(const char *a, const char *b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *a == *b;
}

/* The callbacks besides Blt and the Locks, which have the time line's own
   figures, as they are now (other threads and processes add to them
   meanwhile): their entries into `out`, their time in all into *total. */
static DWORD
cb_snapshot(cb_entry_t *out, uint64_t *total)
{
    const DWORD n = (cb_timed_n < CB_TIMED) ? cb_timed_n : CB_TIMED;
    DWORD       m = 0;
    DWORD       i;

    *total = 0;
    for (i = 0; i < n; i++) {
        const char *name = __atomic_load_n(&cb_time[i].name, __ATOMIC_ACQUIRE);

        if (!name || same_name(name, "Blt") || same_name(name, "Lock") || same_name(name, "LockExecuteBuffer"))
            continue;
        out[m].name   = name;
        out[m].cycles = cb_time[i].cycles;
        out[m].calls  = cb_time[i].calls;
        *total += out[m].cycles;
        m++;
    }
    return m;
}

static char *
put_text(char *p, char *end, const char *s)
{
    while (*s && (p < end))
        *p++ = *s++;
    return p;
}

/* The three callbacks that took longest, then the rest, in milliseconds;
   nothing under 1 ms in all. A callback whose figures would not fit whole
   (the line holds what dbg leaves after its start) goes into the rest. */
#define CB_LINE      120
#define CB_REST_ROOM 24 /* "; the rest 4294967295 ms" */

static void
cb_report(DWORD per_ms, const cb_entry_t *cb, DWORD n, uint64_t total)
{
    char     line[CB_LINE];
    char    *p     = line;
    char    *end   = line + sizeof(line) - 1;
    uint64_t shown = 0;
    DWORD    top[3];
    DWORD    k;
    DWORD    i;

    if (!per_ms || (total < per_ms))
        return;
    for (k = 0; k < 3; k++) {
        char  item[64];
        char *q   = item;
        char *qe  = item + sizeof(item) - 1;

        top[k] = ~0u;
        for (i = 0; i < n; i++) {
            if ((cb[i].cycles < per_ms) || ((k > 0) && (i == top[0])) || ((k > 1) && (i == top[1])))
                continue;
            if ((top[k] == ~0u) || (cb[i].cycles > cb[top[k]].cycles))
                top[k] = i;
        }
        if (top[k] == ~0u)
            break;
        if (k)
            q = put_text(q, qe, ", ");
        q = put_text(q, qe, cb[top[k]].name);
        q = put_text(q, qe, " ");
        q = log_num(q, qe, (DWORD) (cb[top[k]].cycles / per_ms), 10, 0, 0);
        q = put_text(q, qe, " ms (");
        q = log_num(q, qe, cb[top[k]].calls, 10, 0, 0);
        q = put_text(q, qe, k ? ")" : " calls)");
        if ((q >= qe) || (q - item > (end - CB_REST_ROOM) - p))
            break;
        *q = 0;
        p  = put_text(p, end, item);
        shown += cb[top[k]].cycles;
    }
    if (!shown) {
        p = put_text(p, end, "none 1 ms alone, ");
        p = log_num(p, end, (DWORD) (total / per_ms), 10, 0, 0);
        p = put_text(p, end, " ms in all");
    } else if (total - shown >= per_ms) {
        p = put_text(p, end, "; the rest ");
        p = log_num(p, end, (DWORD) ((total - shown) / per_ms), 10, 0, 0);
        p = put_text(p, end, " ms");
    }
    *p = 0;
    dbg(DLL_TAG " other callbacks: %s", line);
}

void
diag_report(void)
{
    const DWORD now = GetTickCount();
    DWORD       ms;
    DWORD       per_ms;
    uint64_t    since;
    uint64_t    t_report;
    cb_entry_t  cbs[CB_TIMED];
    DWORD       n_cbs;
    uint64_t    cb_total;

    if (!diag_tick) {
        diag_tick = now ? now : 1;
        diag_tsc  = rdtsc();
        return;
    }
    ms = now - diag_tick;
    if (ms < 5000)
        return;
    /* The processor's clock rate, from the interval itself. */
    t_report    = rdtsc();
    per_ms      = (DWORD) ((t_report - diag_tsc) / ms);
    diag_per_ms = per_ms;
    flight_reap();
    n_cbs = cb_snapshot(cbs, &cb_total);
    if (diag.batches || diag.locks || diag.waits_ring || diag.waits_fence || diag.waits_lock || diag.busy ||
        (per_ms && (cb_total >= per_ms))) {
        /* The interface: DirectX 8's when its contexts made the calls. */
        dbg(DLL_TAG ", %u.%u s, DirectX %c interface: %u batches, %u state flushes (%u skipped), %u values sent, %u textures rebuilt",
            ms / 1000, (ms % 1000) / 100, (diag.batches8 && (2 * diag.batches8 >= diag.batches)) ? '8' : d3d7_on ? '7' : '6',
            diag.batches, diag.flushes, diag.flushes_skipped, diag.values, diag.tex_rebuilt);
        dbg(DLL_TAG " ring: %u RING_HEAD looks; vertices sent %u KB of %u KB, %u KB again; indices %u KB", diag.head_reads,
            (DWORD) (diag.vtx_sent >> 10), (DWORD) (diag.vtx_buffer >> 10), (DWORD) (diag.vtx_again >> 10),
            (DWORD) (diag.idx_sent >> 10));
        dbg(DLL_TAG " locks: %u (textures %u; direct %u, synchronized %u)", diag.locks, diag.locks_tex, diag.locks_direct,
            diag.locks_sync);
        if (diag.sh_locks[0] || diag.sh_locks[1] || diag.sh_refused || diag.sh_lost)
            dbg(DLL_TAG " locks through main memory: %u of surfaces the GPU drew into, %u of textures; %u KB read back, %u KB "
                        "written back; %u without a copy, %u not sent back",
                diag.sh_locks[0], diag.sh_locks[1], (DWORD) (diag.sh_read_bytes >> 10), (DWORD) (diag.sh_write_bytes >> 10),
                diag.sh_refused, diag.sh_lost);
        dbg(DLL_TAG " waits: ring %u, fence %u, lock %u; %u ms, %u Sleep(1); %u busy answers, %u ms until flips",
            diag.waits_ring, diag.waits_fence, diag.waits_lock, per_ms ? (DWORD) (diag.wait_cycles / per_ms) : 0, diag.sleeps,
            diag.busy, per_ms ? (DWORD) (diag.flip_cycles / per_ms) : 0);
        if (diag.busy || diag.long_waits)
            dbg(DLL_TAG " busy answers: Lock %u, Blt %u, Flip %u, GetFlipStatus %u, GetBltStatus %u, buffer Lock %u; long waits %u",
                diag.busy_by[BUSY_LOCK], diag.busy_by[BUSY_BLT], diag.busy_by[BUSY_FLIP], diag.busy_by[BUSY_FLIPSTATUS],
                diag.busy_by[BUSY_BLTSTATUS], diag.busy_by[BUSY_BUFFER_LOCK], diag.long_waits);
        dbg(DLL_TAG " time in the DLL: DrawPrimitives2 %u ms, Blt %u ms, Lock %u ms (waits included); the processor at %u MHz",
            per_ms ? (DWORD) (diag.cyc_dp2 / per_ms) : 0, per_ms ? (DWORD) (diag.cyc_blt / per_ms) : 0,
            per_ms ? (DWORD) (diag.cyc_lock / per_ms) : 0, (per_ms + 500) / 1000);
        cb_report(per_ms, cbs, n_cbs, cb_total);
        if (diag.batches) {
            const uint64_t parts = diag.cyc_dp2_setup + diag.cyc_dp2_state + diag.cyc_dp2_draws;
            const uint64_t rest  = (diag.cyc_dp2 > parts) ? diag.cyc_dp2 - parts : 0;

            dbg(DLL_TAG " DrawPrimitives2 cycles a call: setup %u, state %u (textures %u), draws %u, end %u; %u calls kept the state",
                (DWORD) (diag.cyc_dp2_setup / diag.batches), (DWORD) (diag.cyc_dp2_state / diag.batches),
                (DWORD) (diag.cyc_dp2_tex / diag.batches), (DWORD) (diag.cyc_dp2_draws / diag.batches),
                (DWORD) (rest / diag.batches), diag.calls_kept);
            dbg(DLL_TAG " sent: DECLARE %u, INDICES %u, STREAM %u; textures: %u new, %u evicted, %u changed, %u kept",
                diag.sent_declare, diag.sent_indices, diag.sent_stream, diag.tex_new, diag.tex_evicted, diag.tex_changed,
                diag.tex_reused);
        }
        /* The texture cache's fill, cube maps' descriptions built (not
           kept), the palettes recorded. */
        if (diag.batches) {
            DWORD pal_cap;
            const DWORD pals = d3d7_palettes_live(&pal_cap);

            dbg(DLL_TAG " textures cached: %u descriptions of %u, %u cube maps built; palettes %u of %u", tex_cache_held(),
                (DWORD) TEX_CACHE_SIZE, diag.tex_cubes, pals, pal_cap);
            /* While any: 8-bit textures drawn with no palette (zeros: what a
               black 8-bit texture would be), palettes with no room, batches
               past their list (every texture of the context fenced). */
            if (diag.pal_unbound || diag.pal_refused || diag.batch_overflows)
                dbg(DLL_TAG " textures: %u palettes sent with none bound, %u palettes with no room; %u batches past %u textures",
                    diag.pal_unbound, diag.pal_refused, diag.batch_overflows, (DWORD) BATCH_TEX_SIZE);
        }
        /* The heap: what it holds, its largest free block, its allocations'
           time (waits for its lock included). */
        if (diag.batches || diag.heap_allocs) {
            DWORD used_blocks, used_kb, free_blocks, largest_kb;

            heap_figures(&used_blocks, &used_kb, &free_blocks, &largest_kb);
            dbg(DLL_TAG " heap: %u blocks in use (%u KB), %u free, the largest %u KB; %u allocations, %u ms, %u refused", used_blocks,
                used_kb, free_blocks, largest_kb, diag.heap_allocs, per_ms ? (DWORD) (diag.cyc_heap / per_ms) : 0,
                diag.heap_refused);
        }
        /* A frame's share (in tenths of a millisecond): what DrawPrimitives2
           costs the emulated processor per frame. */
        if (diag.frames && per_ms && diag.batches) {
            const uint64_t parts = diag.cyc_dp2_setup + diag.cyc_dp2_state + diag.cyc_dp2_draws;
            const uint64_t rest  = (diag.cyc_dp2 > parts) ? diag.cyc_dp2 - parts : 0;
            const uint64_t div   = (uint64_t) diag.frames * per_ms;
            DWORD          t[5];

            t[0] = (DWORD) (diag.cyc_dp2 * 10 / div);
            t[1] = (DWORD) (diag.cyc_dp2_setup * 10 / div);
            t[2] = (DWORD) (diag.cyc_dp2_state * 10 / div);
            t[3] = (DWORD) (diag.cyc_dp2_draws * 10 / div);
            t[4] = (DWORD) (rest * 10 / div);
            dbg(DLL_TAG " per frame (%u): DrawPrimitives2 %u.%u ms (setup %u.%u, state %u.%u, draws %u.%u, end %u.%u), %u calls",
                diag.frames, t[0] / 10, t[0] % 10, t[1] / 10, t[1] % 10, t[2] / 10, t[2] % 10, t[3] / 10, t[3] % 10, t[4] / 10,
                t[4] % 10, diag.batches / diag.frames);
        }
    }
    if (diag_audit && diag.audit_draws)
        dbg(DLL_TAG " vertex audit: %u draws; past their call's vertices %u (by up to %u), past what the GPU has %u, on the ring %u",
            diag.audit_draws, diag.audit_past_call, diag.audit_furthest, diag.audit_past_gpu, diag.audit_ring);
    if (diag.faults)
        dbg(DLL_TAG " faults caught: %u (each place named above, its first three times)", diag.faults);
    /* The texture trace's figures: what it saw,
       named or not. */
    if (diag_textures && (diag.tt_renamed || diag.tt_setpal || diag.tt_updpal || diag.pal_unbound || diag.texblts ||
                          diag.locks_tex || diag.tt_dropped || diag.tt_matrices)) {
        /* Two lines: one holds 160 characters. */
        dbg(DLL_TAG " texture trace: handles renamed %u, SETPALETTE %u, UPDATEPALETTE %u, 8-bit textures with no palette %u",
            diag.tt_renamed, diag.tt_setpal, diag.tt_updpal, diag.pal_unbound);
        dbg(DLL_TAG " texture trace: TEXBLT %u, texture Locks %u, texture matrices %u (%u past a context's %u lines); other "
                    "lines past a context's bounds %u",
            diag.texblts, diag.locks_tex, diag.tt_matrices, diag.tt_mx_untold, TT_MATRICES, diag.tt_dropped);
    }
    if (diag_textures && (diag.vsum_notes || diag.vsum_skipped))
        dbg(DLL_TAG " texture races: %u VSUM notes (%u KB hashed, %u of index buffers), %u draws or windows without one (the budget; "
                    "%u of them quiet buffers')",
            diag.vsum_notes, (DWORD) (diag.vsum_bytes >> 10), diag.vsum_index, diag.vsum_skipped, diag.vsum_skipped_quiet);
    if (diag_audit8 || diag_textures)
        trace_report();
    if (diag_audit8 && d3d8_on) {
        DWORD handles, copies, trees, kb;

        d3d7_record_figures(&handles, &copies, &trees, &kb);
        if (copies || diag.rec_refused)
            dbg(DLL_TAG " records: %u handles, %u of them the DLL's copies, in %u trees (%u KB); %u trees refused for room",
                handles, copies, trees, kb, diag.rec_refused);
        if (diag.rec_checks)
            dbg(DLL_TAG " record checks %u; differing memory %u, pitch or size %u, caps %u; records moved %u; no record %u",
                diag.rec_checks, diag.rec_mismatches[0], diag.rec_mismatches[1], diag.rec_mismatches[2],
                diag.rec_mismatches[3], diag.rec_mismatches[4]);
    }
    if (diag_audit8 && diag.audit8_draws)
        dbg(DLL_TAG " stream audit: %u draws; index buffer %u, window %u, stream %u, declared %u, stride %u, unbound %u, untaken %u",
            diag.audit8_draws, diag.audit8_faults[0], diag.audit8_faults[1], diag.audit8_faults[2], diag.audit8_faults[6],
            diag.audit8_faults[3], diag.audit8_faults[4], diag.audit8_faults[5]);
    if (diag_audit8 && diag.audit8_draws && diag.batches)
        dbg(DLL_TAG " stream audit cost: %u cycles a call; %u indices scanned, %u ranges reused, %u changed without a new version",
            (DWORD) (diag.audit8_cycles / diag.batches), diag.audit8_scanned, diag.audit8_reused, diag.audit8_changed);
    if (diag.draws_depth[0] || diag.draws_depth[1] || diag.draws_depth[2])
        dbg(DLL_TAG " depth: draws without Z %u, Z-buffered %u, W-buffered %u; testing a 16-bit depth buffer %u, a 24-bit one %u",
            diag.draws_depth[0], diag.draws_depth[1], diag.draws_depth[2], diag.draws_d16, diag.draws_d24);
    if (diag.winfo || diag.winfo_refused) {
        char nl[16], nh[16], fl[16], fh[16];

        dbg(DLL_TAG " WINFO %u: near %s to %s, finite far %s to %s; infinite far %u, refused %u", diag.winfo,
            diag.winfo ? fmt_float(nl, diag.wnear_lo) : "-", diag.winfo ? fmt_float(nh, diag.wnear_hi) : "-",
            diag.winfo_finite ? fmt_float(fl, diag.wfar_lo) : "-", diag.winfo_finite ? fmt_float(fh, diag.wfar_hi) : "-",
            diag.winfo_infinite, diag.winfo_refused);
    }
    if (d3d7_on && (diag.batches || diag.surfaces_ex || diag.texblts || diag.texblts_refused || diag.texblt_preloads)) {
        dbg(DLL_TAG " dx7: %u CreateSurfaceEx, %u handles live; TEXBLT %u (refused %u, preloads %u; %u levels, %u KB loaded)",
            diag.surfaces_ex, d3d7_live_handles(), diag.texblts, diag.texblts_refused, diag.texblt_preloads,
            diag.texblt_copies, (DWORD) (diag.texblt_bytes >> 10));
        dbg(DLL_TAG " dx7 state blocks: %u recorded, %u executed, %u captured, %u live", diag.sb_recorded, diag.sb_executed,
            diag.sb_captured, d3d7_sb_live());
    }
    if (diag.x8_draws || diag.x8_skipped || diag.x8_shaders || diag.x8_shaders_bad || diag.bufblts || diag.bufblts_refused ||
        diag.volblts || diag.volblts_refused || diag.rec_levelless || diag.rec_kept || diag.rec_moved || diag.tex_missing ||
        diag.rec_adopted || diag.rec_unadopted || diag.present_waits) {
        const DWORD *k = diag.x8_cmds; /* by command, from CREATEVERTEXSHADER (45) */

        /* Short: a line holds 160 characters. */
        dbg(DLL_TAG " dx8 runtime %x: draws %u (indexed %u), streams 1 %u, 2 %u, 3-4 %u, 5+ %u; pieces %u; presents waited %u",
            d3d8_runtime(), diag.x8_draws, diag.x8_draws_indexed, diag.x8_streams[0], diag.x8_streams[1], diag.x8_streams[2],
            diag.x8_streams[3], diag.x8_split, diag.present_waits);
        if (diag.x8_skipped)
            dbg(DLL_TAG " dx8 draws skipped %u: no shader %u, no stream %u, stride %u, no indices %u, size %u", diag.x8_skipped,
                diag.x8_skip_why[0], diag.x8_skip_why[1], diag.x8_skip_why[2], diag.x8_skip_why[3], diag.x8_skip_why[4]);
        dbg(DLL_TAG " dx8 ring: vertices %u KB, indices %u KB; pool indices the GPU read %u KB; BUFFERBLT %u (waited %u), VOLUMEBLT %u",
            (DWORD) (diag.x8_ring_bytes >> 10), (DWORD) (diag.x8_ring_idx >> 10), (DWORD) (diag.x8_pool_idx >> 10),
            diag.bufblts, diag.bufblt_waits, diag.volblts);
        if (diag.x8_ring_clamped)
            dbg(DLL_TAG " dx8 ring windows cut at their buffer's end: %u (a draw's stream whose vertices reach past it)",
                diag.x8_ring_clamped);
        if (diag.bufblts_refused || diag.volblts_refused)
            dbg(DLL_TAG " dx8 refused: BUFFERBLT %u, VOLUMEBLT %u (a handle named no usable buffer or volume; a BUFFERBLT range past its end)",
                diag.bufblts_refused, diag.volblts_refused);
        if (diag.rec_levelless || diag.rec_kept || diag.rec_moved || diag.tex_missing || diag.rec_adopted || diag.rec_unadopted)
            /* Short: the buffers first, so a long tag never cuts them. */
            dbg(DLL_TAG " dx8 copies: buffers adopted %u (not %u); texture lookups failed %u; mip chains short %u; levels kept %u;"
                " trees split %u",
                diag.rec_adopted, diag.rec_unadopted, diag.tex_missing, diag.rec_levelless, diag.rec_kept, diag.rec_moved);
        dbg(DLL_TAG " dx8 commands: DRAWPRIMITIVE %u, DRAWINDEXEDPRIMITIVE %u, 2 forms %u/%u, fans %u; STREAM %u/UM %u, INDICES %u",
            k[52 - 45], k[53 - 45], k[59 - 45], k[60 - 45], k[58 - 45], k[49 - 45], k[50 - 45], k[51 - 45]);
        /* The rest by kind (those stepped over included). */
        if (k[45 - 45] | k[46 - 45] | k[48 - 45] | k[54 - 45] | k[55 - 45] | k[56 - 45] | k[57 - 45] | k[65 - 45] | k[66 - 45] |
            k[67 - 45])
            /* Shaders made/deleted (pixel: /set), constants, MULTIPLYTRANSFORM,
               ADDDIRTYRECT/ADDDIRTYBOX. */
            dbg(DLL_TAG " dx8 commands: VS %u/%u, constants %u; PS %u/%u/%u, constants %u; MULTIPLYTRANSFORM %u; dirty %u/%u",
                k[45 - 45], k[46 - 45], k[48 - 45], k[54 - 45], k[55 - 45], k[56 - 45], k[57 - 45], k[65 - 45], k[66 - 45],
                k[67 - 45]);
        if (diag.x8_blend[0] | diag.x8_blend[1] | diag.x8_blend[2] | diag.x8_blend[3] | diag.x8_blend[4] | diag.x8_blend[5])
            dbg(DLL_TAG " dx8 blending: indexed (indices from UBYTE4 %u, last weight %u, decl %u, none %u); unindexed %u, tweened %u",
                diag.x8_blend[0], diag.x8_blend[1], diag.x8_blend[2], diag.x8_blend[3], diag.x8_blend[4], diag.x8_blend[5]);
        dbg(DLL_TAG " dx8 shaders: SETVERTEXSHADER %u (FVF %u, shader %u); declarations %u, unusable %u; pixel shaders %u, patches %u",
            k[47 - 45], diag.x8_vs_fvf, diag.x8_vs_decl, diag.x8_shaders, diag.x8_shaders_bad, diag.x8_pshaders, diag.x8_patches);
        if (diag.vs_versions[0] | diag.vs_versions[1] | diag.vs_versions[2] | diag.ps_versions[0] | diag.ps_versions[1] |
            diag.ps_versions[2] | diag.ps_versions[3] | diag.ps_versions[4] | diag.ps_versions[5])
            dbg(DLL_TAG " dx8 shader versions asked: vs 1.0 %u, 1.1 %u, other %u; ps 1.0 %u, 1.1 %u, 1.2 %u, 1.3 %u, 1.4 %u, other %u",
                diag.vs_versions[0], diag.vs_versions[1], diag.vs_versions[2], diag.ps_versions[0], diag.ps_versions[1],
                diag.ps_versions[2], diag.ps_versions[3], diag.ps_versions[4], diag.ps_versions[5]);
        if (diag.lock_kind[0])
            dbg(DLL_TAG " dx8 Locks of vertex buffers %u: discard %u, nooverwrite %u, read %u; waited %u, copied %u (%u KB)",
                diag.lock_kind[0], diag.lock_flags[0][0], diag.lock_flags[0][1], diag.lock_flags[0][2], diag.lock_waits[0],
                diag.lock_copies[0], (DWORD) (diag.lock_copy_bytes[0] >> 10));
        if (diag.lock_kind[1])
            dbg(DLL_TAG " dx8 Locks of index buffers %u: discard %u, nooverwrite %u, read %u; waited %u, copied %u (%u KB)",
                diag.lock_kind[1], diag.lock_flags[1][0], diag.lock_flags[1][1], diag.lock_flags[1][2], diag.lock_waits[1],
                diag.lock_copies[1], (DWORD) (diag.lock_copy_bytes[1] >> 10));
    }
    /* Command set 2.2's programs, while any (the
       largest programs are the session's). */
    if (diag.vs_made || diag.vs_refused || diag.vs_deleted || diag.ps_made || diag.ps_deleted || diag.ps_bound || diag.ps_refused)
        dbg(DLL_TAG " dx8 programs: vs made %u (largest %u), refused %u, deleted %u; "
                    "ps made %u (largest %u), refused %u, deleted %u, bound %u",
            diag.vs_made, prog_largest[0], diag.vs_refused, diag.vs_deleted, diag.ps_made, prog_largest[1],
            diag.ps_refused, diag.ps_deleted, diag.ps_bound);
    if (diag.prog_draws[0] || diag.prog_draws[1] || diag.ps_draws_unknown || diag.prog_sent || diag.prog_binds || diag.prog_consts)
        dbg(DLL_TAG " dx8 program draws: vs %u, ps %u, no shader %u; sent PROGRAM %u (%u KB), BIND %u, CONSTANTS %u (%u regs)",
            diag.prog_draws[0], diag.prog_draws[1], diag.ps_draws_unknown, diag.prog_sent, (diag.prog_sent_dw * 4) >> 10,
            diag.prog_binds, diag.prog_consts, diag.prog_const_regs);
    if (diag.x8_rt_idx_draws || diag.buf_drawn_locked || diag.x8_rt_ib_flagged)
        dbg(DLL_TAG " buffers: DIP2 indices through the ring %u draws; %u index buffers taken (largest %u bytes); drawn locked %u",
            diag.x8_rt_idx_draws, diag.x8_rt_ib_flagged, diag.x8_rt_ib_largest, diag.buf_drawn_locked);
    if (diag.sys_blts)
        dbg(DLL_TAG " blits from system memory: %u (%u KB, %u decompressed)", diag.sys_blts,
            (DWORD) (diag.sys_blt_bytes >> 10), diag.sys_blt_dxt);
    if (diag.gpu_errors || diag.calls_dropped)
        dbg(DLL_TAG " GPU errors: %u (%u not logged), the last %s (first %x); contexts stopped %u, their calls dropped %u",
            diag.gpu_errors, diag.gpu_errors_unlogged, diag.gpu_errors ? gpu_op_name(diag.err_op) : "-", diag.err_arg,
            diag.ctx_stopped, diag.calls_dropped);
    if (pool_on && (diag.batches || diag.buf_locks || diag.buf_refused || diag.buf_sysmem)) {
        DWORD kb, cap_kb, chunks, count[PB_KINDS + 1], kb_by[PB_KINDS + 1];
        DWORD fails = 0;
        DWORD i;

        pool_figures(&kb, &cap_kb, &chunks, count, kb_by);
        /* Short: a line holds 160 characters. */
        dbg(DLL_TAG " buffers: pool %u of %u KB, %u chunks; vertex %u (%u KB), index %u (%u KB), runtime %u (%u KB), command %u",
            kb, cap_kb, chunks, count[PB_VERTEX], kb_by[PB_VERTEX], count[PB_INDEX], kb_by[PB_INDEX], count[PB_IMPLICIT],
            kb_by[PB_IMPLICIT], count[PB_COMMAND]);
        dbg(DLL_TAG " buffers the GPU read: %u KB static, %u KB dynamic, %u KB the runtime's (%u calls through the ring)",
            (DWORD) (diag.buf_static >> 10), (DWORD) (diag.buf_dynamic >> 10), (DWORD) (diag.buf_runtime >> 10),
            diag.buf_unswapped);
        dbg(DLL_TAG " buffer Locks %u (waited %u, renamed %u); swaps %u; system memory %u (%u KB), %u new, %u back; refused %u",
            diag.buf_locks, diag.buf_waits, diag.buf_renames, diag.buf_swaps, count[PB_KINDS], kb_by[PB_KINDS], diag.buf_sysmem,
            diag.buf_sys_back, diag.buf_refused);
        if (diag.rn_spare || diag.rn_fresh || diag.spares_freed)
            dbg(DLL_TAG " buffers renamed: %u to their own spares, %u to fresh blocks (%u KB copied in); %u spares given back "
                        "for room",
                diag.rn_spare, diag.rn_fresh, (DWORD) (diag.rn_fresh_bytes >> 10), diag.spares_freed);
        /* The chunks without a buffer, and what went back while contexts ran. */
        {
            DWORD free_chunks, free_kb;

            pool_chunk_figures(&free_chunks, &free_kb);
            if (free_chunks || diag.pool_given_back)
                dbg(DLL_TAG " pool chunks without a buffer: %u (%u KB); %u KB given back", free_chunks, free_kb,
                    diag.pool_given_back);
        }
        /* Whether the runtime rewrites its own buffers with no Lock that
           would wait or rename. */
        if (diag.rt_windows || diag.rt_locks[0] || diag.rt_locks[1] || diag.rt_locks[2])
            dbg(DLL_TAG " runtime buffers: %u windows, Locks %u discard %u nooverwrite %u other; checked %u, changed %u (%u with NOOVERWRITE)",
                diag.rt_windows, diag.rt_locks[0], diag.rt_locks[1], diag.rt_locks[2], diag.rt_checked, diag.rt_changed,
                diag.rt_changed_noow);
        for (i = 0; i < POOL_GROW_STEPS; i++)
            fails += diag.grow_fails[i];
        if (fails)
            dbg(DLL_TAG " pool grows failed %u: cap %u, record %u, run %u, pages %u, copy %u, absent %u, adapter %u, unnamed %u",
                fails, diag.grow_fails[1], diag.grow_fails[2], diag.grow_fails[3], diag.grow_fails[4], diag.grow_fails[5],
                diag.grow_fails[6], diag.grow_fails[7], diag.grow_fails[0]);
    }
    since = diag.flip_since;
    memset(&diag, 0, sizeof(diag));
    diag.flip_since = since;
    {
        DWORD i;

        for (i = 0; i < CB_TIMED; i++) {
            cb_time[i].cycles = 0;
            cb_time[i].calls  = 0;
        }
    }
    diag_tick       = now ? now : 1;
    diag_tsc        = rdtsc();
    /* The report's own time, which the callback it was written in leaves
       out of its own (callback_time). */
    report_cycles   = (DWORD) (diag_tsc - t_report);
    report_tid      = GetCurrentThreadId();
    report_at       = t_report;
}

/* --- Entry points ------------------------------------------------------- */

#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1

BOOL STDCALL DllMain(DWORD instance, DWORD reason, void *reserved);
DWORD STDCALL DriverInit(DWORD context);

BOOL STDCALL
DllMain(DWORD instance, DWORD reason, void *reserved)
{
    (void) reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        dll_instance = instance;
        guard_init(instance);
        flight_attach(GetCurrentProcessId());
    } else if (reason == DLL_PROCESS_DETACH) {
        /* A process leaving (even from inside a callback, at ExitProcess)
           gives back the locks it holds; one leaving with contexts live
           shows what it last did. */
        ctx_leaving(GetCurrentProcessId(), reserved != NULL);
        dlock_release_process(GetCurrentProcessId());
        gpu_release_process(GetCurrentProcessId());
        shadow_process_gone(GetCurrentProcessId());
    }
    return 1;
}

/* The GPU's profile, if BAR0 is mapped and speaks the interfaces this DLL
   was written for; 0 otherwise (no acceleration), with the reason. */
static DWORD
read_profile(const char **why)
{
    *why = NULL;
    if (!regs) {
        *why = "the GPU's registers are not mapped";
        return 0;
    }
    if (reg_read(VGPU98_REG_ID) != VGPU98_ID_MAGIC) {
        *why = "the register window is not a VGPU98 GPU's";
        return 0;
    }
    if ((reg_read(VGPU98_REG_HW_VERSION) >> 16) != (VGPU98_HW_VERSION >> 16)) {
        *why = "the GPU's register interface is another version";
        return 0;
    }
    if ((reg_read(VGPU98_REG_COMMAND_SET) >> 16) != (VGPU98_COMMAND_SET >> 16)) {
        *why = "the GPU's command set is another version";
        return 0;
    }
    /* FIXED8 only from a command set 2.1 GPU, PROGRAMS from a 2.2 one,
       DOWNLOAD from a 2.3 one (its minor version). */
    return reg_read(VGPU98_REG_PROFILE) &
           (VGPU98_PROFILE_COMMANDS | VGPU98_PROFILE_BLIT | VGPU98_PROFILE_RENDER3D | VGPU98_PROFILE_SYSRING |
            VGPU98_PROFILE_STATUSWB | VGPU98_PROFILE_VERTEX | VGPU98_PROFILE_GPUVM |
            (((reg_read(VGPU98_REG_COMMAND_SET) & 0xffff) >= 1) ? VGPU98_PROFILE_FIXED8 : 0) |
            (((reg_read(VGPU98_REG_COMMAND_SET) & 0xffff) >= 2) ? VGPU98_PROFILE_PROGRAMS : 0) |
            (((reg_read(VGPU98_REG_COMMAND_SET) & 0xffff) >= 3) ? VGPU98_PROFILE_DOWNLOAD : 0));
}

/* The host's log listens: BAR0 is mapped, is a VGPU98 GPU's, and DEBUG
   says so (the setting cannot change without a reset). */
static int
log_listens(void)
{
    return regs && (reg_read(VGPU98_REG_ID) == VGPU98_ID_MAGIC) && (reg_read(VGPU98_REG_DEBUG) & VGPU98_DEBUG_LISTENING);
}

/* Diagnostics "Minimal": the log takes the event lines alone (DEBUG's
   EVENTS, LISTENING clear). */
static int
log_events(void)
{
    return regs && (reg_read(VGPU98_REG_ID) == VGPU98_ID_MAGIC) && (reg_read(VGPU98_REG_DEBUG) & VGPU98_DEBUG_EVENTS) &&
           !(reg_read(VGPU98_REG_DEBUG) & VGPU98_DEBUG_LISTENING);
}

static DWORD driver_init(DWORD context);

static DWORD STDCALL
driver_init_g(void *context)
{
    return driver_init((DWORD) context);
}

/* DriverInit inside the guard: after a fault, the driver does not start. */
DWORD STDCALL
DriverInit(DWORD context)
{
    guard_fault_t f;
    const DWORD   r = guard_run(driver_init_g, (void *) context, &f);

    if (f.faulted) {
        guard_recover("DriverInit", &f);
        return 0;
    }
    return r;
}

static DWORD
driver_init(DWORD context)
{
    vgpu98_shared_t *s = (vgpu98_shared_t *) context;
    const char      *why;
    DWORD            profile;

    if (!s || (s->magic != SB_MAGIC))
        return 0; /* not a VGPU98 display driver's block: nobody to tell */
    if ((s->version != SB_VERSION) || (s->size != sizeof(vgpu98_shared_t))) {
        /* The display driver is from another build. The block's first
           fields still lead to the register window, to say so. */
        regs        = (volatile DWORD *) s->regs_linear;
        diag_on     = log_listens();
        diag_events = log_events();
        dbg_event(DLL_TAG ": shared block version %u (%u bytes), expected %u (%u bytes): the display driver is from another build",
                  s->version, s->size, SB_VERSION, (DWORD) sizeof(vgpu98_shared_t));
        regs        = NULL;
        diag_on     = 0;
        diag_events = 0;
        return 0;
    }
    sb          = s;
    vram        = (uint8_t *) sb->fb_linear;
    regs        = (volatile DWORD *) sb->regs_linear;
    diag_on     = log_listens();
    diag_events = log_events();
    diag_audit = diag_on && (reg_read(VGPU98_REG_DEBUG) & VGPU98_DEBUG_AUDIT);
    if (diag_audit)
        dbg(DLL_TAG ": the vertex audit is on (each draw's furthest vertex against the vertices its call names)");
    diag_audit8 = diag_on && (reg_read(VGPU98_REG_DEBUG) & VGPU98_DEBUG_AUDIT8);
    if (diag_audit8)
        dbg(DLL_TAG ": the stream audit is on (each DirectX 8 draw's indices, windows, streams and declaration)");
    diag_textures = diag_on && (reg_read(VGPU98_REG_DEBUG) & VGPU98_DEBUG_TEXTURES);
    if (diag_textures)
        dbg(DLL_TAG ": the texture audit is on (stage handles, palettes, TEXBLTs and texture Locks traced)");
    diag_shaders = !diag_on                                                ? 0
                   : !(reg_read(VGPU98_REG_DEBUG) & VGPU98_DEBUG_SHADERS)   ? 0
                   : (reg_read(VGPU98_REG_DEBUG) & VGPU98_DEBUG_SHADERS14) ? 2
                                                                            : 1;
    if (diag_shaders)
        dbg(DLL_TAG ": shader capture is on (each shader's instructions logged%s)",
            (diag_shaders == 2) ? ", with its GPU program" : "");

    profile = read_profile(&why);
    if (why)
        dbg_event(DLL_TAG ": no acceleration: %s", why);
    /* Acceleration needs the command processor and the 2D packets at least. */
    if ((profile & (VGPU98_PROFILE_COMMANDS | VGPU98_PROFILE_BLIT)) != (VGPU98_PROFILE_COMMANDS | VGPU98_PROFILE_BLIT)) {
        if (!why)
            dbg_event(DLL_TAG ": no acceleration: the GPU's profile %x lacks the command processor or the 2D packets", profile);
        profile = 0;
    } else if (!(profile & VGPU98_PROFILE_RENDER3D))
        dbg_event(DLL_TAG ": Direct3D unavailable: the GPU has no 3D (86Box's Vulkan lines say why)");
    else if (!(profile & VGPU98_PROFILE_VERTEX)) {
        /* Command set 2.0 draws only through its vertex pipeline. */
        dbg_event(DLL_TAG ": Direct3D unavailable: the GPU's profile %x has 3D without the vertex pipeline", profile);
        profile &= ~VGPU98_PROFILE_RENDER3D;
    }
    dbg(DLL_TAG ": DriverInit, profile %x, %u KB video memory", profile, sb->vram_size >> 10);
    if (profile)
        gpu_ring_choose(profile);
    /* The pool holds the execute buffers Direct3D draws from. */
    if (profile & VGPU98_PROFILE_RENDER3D)
        pool_init(profile);
    /* Surfaces Locked through copies in main memory: with the pool and
       DOWNLOAD. */
    shadow_init(profile);

    sb->profile = profile;
    caps_init(profile);
    /* DirectX 8's caps and formats, and DDHALINFO_GETDRIVERINFO2 for the
       display driver, with PROFILE.FIXED8 (command set 2.1). */
    d3d8_init(profile);
    if (d3d8_on)
        dbg(DLL_TAG ": command set %s: the DirectX 8 driver interface is offered%s", d3d8_programs ? "2.2" : "2.1",
            d3d8_programs ? ", with shader programs" : "");
    else if (profile & VGPU98_PROFILE_RENDER3D)
        dbg(DLL_TAG ": command set 2.0 (the GPU has no PROFILE.FIXED8): DirectX 8 programs use the DirectX 7 interface");
    d3d_init();
    d3d7_init();
    ddraw_fill_callbacks(profile);
    sb->dll_hinstance = dll_instance;
    sb->dll_ready     = 1;
    return 1;
}

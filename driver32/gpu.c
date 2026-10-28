/*
 * vgpu98dx.dll: the GPU's command ring (spec/gpu-registers.md,
 * spec/directx-hal.md). Every writer of the ring, and every caller that
 * programs or resets it, holds the ring lock in the shared block, so the
 * DirectDraw callbacks of several processes and the display driver's mode
 * changes never interleave. A batch is: take the lock, make sure the GPU
 * runs, write packets, end with a FENCE, write RING_TAIL, let go.
 *
 * The ring is in main memory when the mini-VDD allocated a buffer for it
 * (interface 2.1): writing it costs no bus cycles. Otherwise, or if the GPU
 * refuses it, the ring is 128 KB at the start of the GPU's reserve in video
 * memory. With the buffer comes a status page (interface 2.2): the GPU
 * writes FENCE, RING_HEAD, STATUS and VBLANK_COUNT there, and every poll
 * reads memory instead of a register over the bus.
 *
 * Waits never trust a GPU blindly: they watch STATUS (ERROR, RUNNING, HELD)
 * and give up only when neither FENCE nor RING_HEAD has moved for a second
 * (the OpenGL ICD: 5 seconds).
 * They yield for their first 20 ms and only then sleep: a fence completes
 * within a vertical blank, and Windows 98's Sleep(1) can take 5 ms.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#if defined(VGPU98_GLIDE_DLL) || defined(VGPU98_ICD_DLL)
#    include "gpuenv.h" /* glide/gpuenv.h or opengl/gpuenv.h: what the ring needs of a Glide DLL or the OpenGL ICD */
#else
#    include "dll.h"
#endif

/* How long a wait gives the GPU without progress before calling it stuck:
   a second, unless the DLL's environment says otherwise (the OpenGL ICD's
   opengl/gpuenv.h: its device may spend over a second making a pipeline). */
#ifndef NO_PROGRESS_MS
#    define NO_PROGRESS_MS   1000
#    define NO_PROGRESS_TEXT "a second"
#endif
#define TAKEOVER_MS    5000
#define SPIN_MS        20
#define SYS_RING_MIN   0x100000 /* the smallest buffer in main memory worth using */
#define DOORBELL_BYTES 0x10000  /* a long batch rings at least this often, so the GPU starts on it */

/* The ring, chosen at the first DriverInit (every section of the DLL is
   shared, so every process sees the same choice). A Glide DLL, whose data is
   its process's, chooses by the same rule: what two
   instances of this file must agree on is in the shared block or the GPU's
   registers, never only in these statics. */
static volatile uint8_t *ring_sys;   /* the buffer in main memory (linear), NULL: video memory */
static DWORD             ring_sys_phys;
static DWORD             ring_bytes = SB_RING_SIZE;
static DWORD             ring_mask  = SB_RING_SIZE - 1;
static int               ring_chosen;
static int               ring_check; /* the next batch compares the GPU's ring with this one */

/* While the lock is held: bytes of the ring known to be free (the GPU only
   frees more), so a packet that fits needs no register read. 0: unknown. */
static DWORD ring_room;
static DWORD ring_rung; /* RING_TAIL as last written: read again whenever the lock is taken, as another kind of client may have rung since */
static DWORD ring_rings; /* doorbells rung, ever (gpu_mark) */
static DWORD ring_thread; /* the thread ID of the ring lock's holder in the DLL (a fault's recovery) */

/* The GPU's status block (interface 2.2): FENCE, RING_HEAD, STATUS and
   VBLANK_COUNT as the GPU last wrote them, in the page after the ring
   buffer. NULL: read the registers. */
static volatile DWORD *status_blk;
static DWORD           status_phys;

/* A status block that stopped moving: a power-on reset of the GPU while
   Windows runs (a standby that reset the card) turns it off, and the block
   would go on saying what it said last. VBLANK_COUNT in it moves at every
   vertical blank; now and then (every 256th look, so polling stays cheap)
   the DLL checks that it did within the last 100 ms, and otherwise
   programs STATUS_BASE again, which has the GPU write the block at once. */
static DWORD st_looks;
static DWORD st_vblank;
static DWORD st_vblank_ms;

static void
status_check(void)
{
    DWORD vb;
    DWORD now;

    if (++st_looks & 255)
        return;
    vb  = status_blk[3];
    now = GetTickCount();
    if (vb != st_vblank) {
        st_vblank    = vb;
        st_vblank_ms = now;
        return;
    }
    if (now - st_vblank_ms < 100)
        return;
    st_vblank_ms = now;
    if (reg_read(VGPU98_REG_STATUS_BASE) != status_phys)
        reg_write(VGPU98_REG_STATUS_BASE, status_phys);
}

static DWORD
st_fence(void)
{
    if (!status_blk)
        return reg_read(VGPU98_REG_FENCE);
    status_check();
    return status_blk[0];
}

static DWORD
st_head(void)
{
    return status_blk ? status_blk[1] : reg_read(VGPU98_REG_RING_HEAD);
}

DWORD
gpu_status(void)
{
    if (!status_blk)
        return reg_read(VGPU98_REG_STATUS);
    status_check();
    return status_blk[2];
}

DWORD
gpu_vblank_count(void)
{
    return status_blk ? status_blk[3] : reg_read(VGPU98_REG_VBLANK_COUNT);
}

static volatile DWORD *
ring_at(DWORD offset)
{
    if (ring_sys)
        return (volatile DWORD *) (ring_sys + offset);
    return (volatile DWORD *) (vram + sb->reserve_base + offset);
}

static DWORD
ring_base_reg(void)
{
    return ring_sys ? (ring_sys_phys | VGPU98_RING_SYSMEM) : sb->reserve_base;
}

/* Chooses the ring, once: the mini-VDD's buffer when the GPU can read a ring
   in main memory and the buffer is sound, else video memory. */
void
gpu_ring_choose(DWORD profile)
{
    const char *why = NULL;
    DWORD       size;
    DWORD       phys;
    DWORD       linear;

    if (ring_chosen)
        return;
    ring_chosen = 1;
    ring_check  = 1;
    if (!(profile & VGPU98_PROFILE_SYSRING))
        why = "the GPU reads its ring only there";
    else {
        size   = reg_read(VGPU98_REG_SYSBUF_SIZE);
        phys   = reg_read(VGPU98_REG_SYSBUF_PHYS);
        linear = reg_read(VGPU98_REG_SYSBUF_LINEAR);
        if (!size)
            why = "the mini-VDD has no buffer for it";
        /* Above the first megabyte, as the GPU requires; in the shared or
           system arena, where every process sees it. */
        else if ((phys < 0x100000) || (phys & 0xfff) || (linear < 0x80000000u) || (size & (size - 1)) ||
                 (size < SYS_RING_MIN) || (size > VGPU98_RING_SYS_SIZE_MAX))
            why = "the mini-VDD's buffer is not usable";
        else {
            ring_sys      = (volatile uint8_t *) linear;
            ring_sys_phys = phys;
            ring_bytes    = size;
            ring_mask     = size - 1;
            /* The status page follows the ring when the mini-VDD allocated
               one; the GPU takes STATUS_BASE only for memory it can write. */
            if ((profile & VGPU98_PROFILE_STATUSWB) && (reg_read(VGPU98_REG_SYSBUF_STATUS) == phys + size)) {
                status_blk  = (volatile DWORD *) (linear + size);
                status_phys = phys + size;
                reg_write(VGPU98_REG_STATUS_BASE, status_phys);
                if (reg_read(VGPU98_REG_STATUS_BASE) != status_phys)
                    status_blk = NULL;
            }
            dbg(DLL_TAG ": ring %u KB in main memory, status %s", size >> 10,
                status_blk ? "in main memory" : "in registers");
            return;
        }
    }
    dbg(DLL_TAG ": ring %u KB in video memory (%s)", (DWORD) SB_RING_SIZE >> 10, why);
}

/* One turn of a wait that began `waited` ms ago: yield, and after a while
   sleep. */
static void
wait_pause(DWORD waited)
{
    if (waited < SPIN_MS) {
        Sleep(0);
        return;
    }
    DIAG(diag.sleeps++);
    Sleep(1);
}

/* --- The ring lock -------------------------------------------------------- */

static int
lock_try(void)
{
    DWORD expected = 0;

    return __atomic_compare_exchange_n(&sb->lock, &expected, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static void ring_abandon(void);

/* A wait over LONG_WAIT_MS writes a line, and again every LONG_AGAIN_MS:
   what it waits for, the callback it is in, and the GPU's state.
   `told` is the wait's own. */
#define LONG_WAIT_MS  2000
#define LONG_AGAIN_MS 10000

void
gpu_state_line(void)
{
    const DWORD now = GetTickCount();

    /* The display driver writes no time when it takes the lock. */
    if (!sb->lock)
        dbg_event_own(DLL_TAG ":   the GPU: fence %x done, %x issued; ring %x to %x, rung %x; status %x; ring lock free",
                      st_fence(), sb->fence_issued, st_head(), sb->ring_tail, ring_rung, gpu_status());
    else if (sb->lock_owner == SB_DRIVER_OWNER)
        dbg_event_own(DLL_TAG ":   the GPU: fence %x done, %x issued; ring %x to %x, rung %x; status %x; ring lock the display driver's",
                      st_fence(), sb->fence_issued, st_head(), sb->ring_tail, ring_rung, gpu_status());
    else
        dbg_event_own(DLL_TAG ":   the GPU: fence %x done, %x issued; ring %x to %x, rung %x; status %x; ring lock %x's, %u ms",
                      st_fence(), sb->fence_issued, st_head(), sb->ring_tail, ring_rung, gpu_status(), sb->lock_owner,
                      now - sb->lock_time);
}

/* A wait over 2 seconds, and again every 10 while it lasts: at Minimal too.
   The episode's first line goes by its
   place's rate; its repeats by `told` alone, so a wait that lasts says so. */
static void
wait_told(const char *what, DWORD target, DWORD start, DWORD now, DWORD *told)
{
    const int again = *told != 0;

    if (!(diag_on || diag_events) || (now - start < LONG_WAIT_MS) || (again && (now - *told < LONG_AGAIN_MS)))
        return;
    *told = now ? now : 1;
    DIAG(diag.long_waits++);
    if (again ? dbg_event_own(DLL_TAG ": waiting %u ms in %s for %s %x", now - start, flight_current(), what, target)
              : dbg_event(DLL_TAG ": waiting %u ms in %s for %s %x", now - start, flight_current(), what, target))
        gpu_state_line();
}

/* The lock is this thread's: its holder and time, and the last doorbell as
   the GPU has it, whoever rang it. */
static void
lock_got(DWORD pid, DWORD now)
{
    sb->lock_owner = pid;
    sb->lock_time  = now;
    ring_thread    = GetCurrentThreadId();
    ring_rung      = reg_read(VGPU98_REG_RING_TAIL);
}

static void
lock_take(void)
{
    const DWORD pid = GetCurrentProcessId();
    DWORD       start;
    DWORD       since;
    DWORD       fence;
    DWORD       head;
    DWORD       told = 0;
    uint64_t    t0   = 0;

    /* Free: no clock but the holder's start. */
    if (lock_try()) {
        lock_got(pid, GetTickCount());
        return;
    }
    if (diag_on) {
        diag.waits_lock++;
        t0 = rdtsc();
    }
    start = since = GetTickCount();
    fence = st_fence();
    head  = st_head();
    for (;;) {
        DWORD now;

        if (lock_try()) {
            lock_got(pid, GetTickCount());
            if (t0)
                diag.wait_cycles += rdtsc() - t0;
            return;
        }
        now = GetTickCount();
        if ((st_fence() != fence) || (st_head() != head)) {
            fence = st_fence();
            head  = st_head();
            since = now;
        }
        wait_told("the ring lock, its holder", sb->lock_owner, start, now, &told);
        /* A holder that has gone (its process ended inside a callback): taken
           over only after five seconds without the GPU moving, so a holder
           still at work is never displaced. The display driver is never. */
        if ((sb->lock_owner != SB_DRIVER_OWNER) && (now - since >= TAKEOVER_MS) && (now - sb->lock_time >= TAKEOVER_MS)) {
            dbg_event(DLL_TAG ": taking the ring lock over from process %x", sb->lock_owner);
            ring_rung = reg_read(VGPU98_REG_RING_TAIL);
            ring_abandon();
            lock_got(pid, now);
            if (t0)
                diag.wait_cycles += rdtsc() - t0;
            return;
        }
        wait_pause(now - start);
    }
}

static void
lock_release(void)
{
    sb->lock_owner = 0;
    ring_thread    = 0;
    __atomic_store_n(&sb->lock, 0, __ATOMIC_RELEASE);
}

int
gpu_holds(void)
{
    return sb && sb->lock && (sb->lock_owner == GetCurrentProcessId()) && (ring_thread == GetCurrentThreadId());
}

void
gpu_release_process(DWORD pid)
{
    if (sb && sb->lock && (sb->lock_owner == pid)) {
        dbg_event(DLL_TAG ": process %x ended holding the ring lock", pid);
        ring_rung = reg_read(VGPU98_REG_RING_TAIL);
        ring_abandon();
        lock_release();
    }
}

/* --- Packets the GPU refused --------------------------------------------- */

/* The batches lately written, by the fences around them, with the context
   each drew for (a token, ctx_token; 0: DirectDraw's own), so a packet the
   GPU refused names the context responsible. The GPU stops on the refused
   packet, so the failing batch is the oldest whose closing fence it has not
   reached. Under the ring lock; forgotten when the ring restarts. */
#define BATCHES 32

typedef struct batch_rec {
    DWORD fence0; /* the last fence issued before the batch */
    DWORD fence;  /* its own, at its end */
    DWORD owner;
} batch_rec_t;

static batch_rec_t batches[BATCHES];
static DWORD       batch_n;      /* recorded since the ring started */
static int         batch_open;   /* a batch is being written ... */
static DWORD       batch_fence0; /* ... after this fence ... */
static DWORD       batch_owner;  /* ... for this context */

/* A kind of refusal, as the DLL sees it: the error, the packet and its first
   dword. The first three of a kind are logged (again after 10 seconds
   without one), the rest counted. */
#define ERR_KINDS     4
#define ERR_LOG_FIRST 3
#define ERR_QUIET_MS  10000

typedef struct err_kind {
    DWORD code;
    DWORD op;
    DWORD arg;
    DWORD logged; /* since it was last quiet */
    DWORD tick;   /* last seen; 0: a free record */
} err_kind_t;

static err_kind_t err_kinds[ERR_KINDS];

void
gpu_batch_owner(DWORD token)
{
    batch_owner = token;
}

/* The token of the context whose batch the GPU stopped in, given the last
   fence it completed; 0 for DirectDraw's own batches, or when the batch is
   older than the table. */
static DWORD
batch_of(DWORD done)
{
    const DWORD n = (batch_n < BATCHES) ? batch_n : BATCHES;
    DWORD       i;

    for (i = 0; i < n; i++) {
        const batch_rec_t *b = &batches[(batch_n - n + i) % BATCHES];

        if ((LONG) (b->fence - done) > 0)
            return ((LONG) (b->fence0 - done) > 0) ? 0 : b->owner;
    }
    return batch_open ? batch_owner : 0;
}

const char *
gpu_op_name(DWORD op)
{
    static const char *const names[] = {
        "NOP", "FENCE", "SYNC", "FLIP", "DISCARD", "05h", "06h", "07h", "FILL", "BLT", "UPLOAD", "DOWNLOAD", "0Ch", "0Dh", "0Eh",
        "0Fh", "TARGET", "VIEWPORT", "WRANGE", "STATE", "TEXTURE", "PALETTE", "16h", "17h", "CLEAR", "ZRANGE", "DECLARE",
        "STREAM", "INDICES", "INLINE", "DRAW", "TRANSFORM", "LIGHT", "LIGHTENABLE", "MATERIAL", "CLIPPLANE"
    };

    static const char *const glide[] = { "GL_OPEN",  "GL_CLOSE",    "GL_STATE",    "GL_DRAW",     "GL_CLEAR", "GL_SWAP",
                                         "GL_TEXDATA", "GL_PALETTE", "GL_LFB_READ", "GL_LFB_WRITE", "GL_GAMMA" };

    if ((op >= 0x40) && (op - 0x40 < ARRAY_SIZE(glide)))
        return glide[op - 0x40];
    return (op < ARRAY_SIZE(names)) ? names[op] : "?";
}

/* Counts a refusal of its kind; 1 if it is to be logged. */
static int
err_note(DWORD code, DWORD op, DWORD arg)
{
    const DWORD now = GetTickCount();
    err_kind_t *e   = NULL;
    err_kind_t *old = &err_kinds[0];
    DWORD       i;

    for (i = 0; i < ERR_KINDS; i++) {
        err_kind_t *k = &err_kinds[i];

        if (k->tick && (k->code == code) && (k->op == op) && (k->arg == arg)) {
            e = k;
            break;
        }
        /* The record to reuse: a free one, else the one seen least lately. */
        if (!k->tick) {
            if (old->tick)
                old = k;
        } else if (old->tick && ((now - k->tick) > (now - old->tick)))
            old = k;
    }
    if (!e) {
        e         = old;
        e->code   = code;
        e->op     = op;
        e->arg    = arg;
        e->logged = 0;
    } else if (now - e->tick >= ERR_QUIET_MS)
        e->logged = 0;
    e->tick = now ? now : 1;
    if (e->logged < ERR_LOG_FIRST) {
        e->logged++;
        return 1;
    }
    return 0;
}

/* The GPU stopped on a packet (under the lock, before the ring restarts):
   names it from the ring, counts it, and tells the context it was for,
   which stops if it keeps sending it (ctx_refused). */
static void
error_seen(void)
{
    const DWORD code  = reg_read(VGPU98_REG_ERROR_CODE);
    const DWORD at    = reg_read(VGPU98_REG_ERROR_OFFSET);
    const DWORD owner = batch_of(reg_read(VGPU98_REG_FENCE));
    DWORD       op    = ~0u;
    DWORD       len   = 0;
    DWORD       arg   = 0;
    const char *name  = "?";

    if (!(at & 3) && (ring_bytes >= 8) && (at <= ring_bytes - 8)) {
        const volatile DWORD *p = ring_at(at);
        const DWORD           h = p[0];

        op   = VGPU98_PKT_OP(h);
        len  = VGPU98_PKT_LEN(h);
        arg  = len ? p[1] : 0;
        name = gpu_op_name(op);
    }
    DIAG({
        diag.gpu_errors++;
        diag.err_op  = op;
        diag.err_arg = arg;
    });
    if (!err_note(code, op, arg))
        DIAG(diag.gpu_errors_unlogged++);
    else if (ctx_token_live(owner)) /* err_note's first three a kind being the rate */
        dbg_event_own(DLL_TAG ": GPU error %u at ring offset %x: %s (%x), %u dwords, first %x; context %u; resetting", code,
                      at, name, op, len, arg, CTX_TOKEN_HANDLE(owner));
    else
        dbg_event_own(DLL_TAG ": GPU error %u at ring offset %x: %s (%x), %u dwords, first %x; no context; resetting", code,
                      at, name, op, len, arg);
    if (owner)
        ctx_refused(owner, name, op, arg, code);
}

/* --- Starting the GPU ------------------------------------------------------ */

int
gpu_held(void)
{
    return (gpu_status() & VGPU98_STATUS_HELD) != 0;
}

/* Programs the ring and starts the GPU (under the lock): RESET (the hold bits
   are left alone), FENCE at the last value issued so every wait on lost work
   ends, the ring, RUN. Every context sends its whole state again. A GPU that
   cannot read the ring in main memory refuses it at RUN (ERROR): the ring is
   then in video memory for good. */
static void
ring_start(void)
{
    DWORD st = reg_read(VGPU98_REG_STATUS);

    if (st & VGPU98_STATUS_ERROR)
        error_seen();
    /* Everything recorded is gone with the ring (a batch being written goes
       on in the new one). */
    batch_n = 0;
    reg_write(VGPU98_REG_CONTROL, VGPU98_CTRL_RESET);
    reg_write(VGPU98_REG_FENCE, sb->fence_issued);
    reg_write(VGPU98_REG_RING_BASE, ring_base_reg());
    reg_write(VGPU98_REG_RING_SIZE, ring_bytes);
    if (status_blk)
        reg_write(VGPU98_REG_STATUS_BASE, status_phys); /* a power-on reset clears it */
    sb->ring_tail   = 0;
    ring_room       = 0;
    ring_rung       = 0;
    ring_check      = 0;
    sb->state_epoch++;
    sb->state_owner = 0;
    reg_write(VGPU98_REG_CONTROL, VGPU98_CTRL_RUN);
    if (ring_sys && (reg_read(VGPU98_REG_STATUS) & VGPU98_STATUS_ERROR)) {
        dbg_event(DLL_TAG ": the GPU refused the ring in main memory (error %u); the ring is now %u KB in video memory",
                  reg_read(VGPU98_REG_ERROR_CODE), (DWORD) SB_RING_SIZE >> 10);
        ring_sys   = NULL;
        ring_bytes = SB_RING_SIZE;
        ring_mask  = SB_RING_SIZE - 1;
        reg_write(VGPU98_REG_CONTROL, VGPU98_CTRL_RESET);
        reg_write(VGPU98_REG_RING_BASE, ring_base_reg());
        reg_write(VGPU98_REG_RING_SIZE, ring_bytes);
        reg_write(VGPU98_REG_CONTROL, VGPU98_CTRL_RUN);
    }
}

/* The GPU runs the ring this DLL chose: checked at the first batch after the
   choice, as a GPU left running by an earlier load of the DLL may not. */
static int
ring_current(void)
{
    return (reg_read(VGPU98_REG_RING_BASE) == ring_base_reg()) && (reg_read(VGPU98_REG_RING_SIZE) == ring_bytes);
}

/* A GPU running another ring finishes what it has (while it runs, up to a
   second), stops, and starts on this DLL's (under the lock). */
static void
ring_switch(void)
{
    const DWORD start = GetTickCount();

    while (!gpu_fence_done(sb->fence_issued) &&
           ((gpu_status() & (VGPU98_STATUS_ERROR | VGPU98_STATUS_RUNNING)) == VGPU98_STATUS_RUNNING) &&
           (GetTickCount() - start < NO_PROGRESS_MS))
        wait_pause(GetTickCount() - start);
    dbg_event(DLL_TAG ": the GPU runs another ring; moving it to this one");
    reg_write(VGPU98_REG_CONTROL, 0);
    ring_start();
}

static void
recover(const char *why)
{
    lock_take();
    if (!gpu_held()) {
        DWORD st = gpu_status();

        if ((st & VGPU98_STATUS_ERROR) || !(st & VGPU98_STATUS_RUNNING) || why) {
            if (why)
                dbg_event(DLL_TAG ": GPU %s; resetting", why);
            ring_start();
        }
    }
    lock_release();
}

HRESULT
gpu_begin(void)
{
    DWORD st;

    if (!regs)
        return DDERR_GENERIC;
    lock_take();
    ring_room = 0; /* another holder may have written since */
    st = gpu_status();
    if (st & VGPU98_STATUS_HELD) {
        lock_release();
        return DDERR_SURFACELOST;
    }
    if ((st & VGPU98_STATUS_ERROR) || !(st & VGPU98_STATUS_RUNNING))
        ring_start();
    else if (ring_check && !ring_current())
        ring_switch();
    ring_check   = 0;
    batch_open   = 1;
    batch_fence0 = sb->fence_issued;
    batch_owner  = 0;
    return DD_OK;
}

/* A holder that ended inside a batch (its lock taken over, or given back for
   it): what it wrote past the last doorbell goes unread (a packet may be
   half written), and every context sends its whole state again (the GPU
   may hold part of the ended one's). Holding the lock. */
static void
ring_abandon(void)
{
    if (!sb)
        return;
    sb->ring_tail   = ring_rung;
    ring_room       = 0;
    batch_open      = 0;
    sb->state_epoch++;
    sb->state_owner = 0;
}

void
gpu_kick(void)
{
    DWORD st;

    if (!regs)
        return;
    st = gpu_status();
    if (!(st & VGPU98_STATUS_HELD) && ((st & VGPU98_STATUS_ERROR) || !(st & VGPU98_STATUS_RUNNING)))
        recover(NULL);
}

/* --- Writing packets -------------------------------------------------------- */

static DWORD
ring_used(void)
{
    return (sb->ring_tail - st_head()) & ring_mask;
}

DWORD
gpu_ring_free(void)
{
    return ring_bytes - 4 - ring_used();
}

static void
doorbell(void)
{
    reg_write(VGPU98_REG_RING_TAIL, sb->ring_tail);
    ring_rung = sb->ring_tail;
    ring_rings++;
}

/* Waits (holding the lock) until `bytes` are free. Returns the free bytes,
   or 0 if the GPU stopped: the batch is then abandoned, the GPU restarted if
   it can be. */
static DWORD
wait_room_slow(DWORD bytes)
{
    const DWORD start = GetTickCount();
    DWORD       since = start;
    DWORD       head  = st_head();
    DWORD       told  = 0;

    /* Let the GPU take what is already written. */
    doorbell();
    for (;;) {
        DWORD st   = gpu_status();
        DWORD room = gpu_ring_free();
        DWORD now;

        if (room >= bytes)
            return room;
        if (st & VGPU98_STATUS_HELD)
            return 0;
        if ((st & VGPU98_STATUS_ERROR) || !(st & VGPU98_STATUS_RUNNING)) {
            ring_start();
            return 0;
        }
        now = GetTickCount();
        wait_told("ring room, bytes", bytes, start, now, &told);
        if (st_head() != head) {
            head  = st_head();
            since = now;
        } else if (now - since >= NO_PROGRESS_MS) {
            if (dbg_event(DLL_TAG ": GPU made no progress for " NO_PROGRESS_TEXT " in %s; resetting", flight_current()))
                gpu_state_line();
            ring_start();
            return 0;
        }
        wait_pause(now - start);
    }
}

/* Room for `bytes` (holding the lock), taken from the cached free space;
   RING_HEAD is read only when that runs short. 0 if the GPU stopped. */
static int
wait_room(DWORD bytes)
{
    uint64_t t0;

    if (ring_room >= bytes) {
        ring_room -= bytes;
        return 1;
    }
    ring_room = gpu_ring_free();
    DIAG(diag.head_reads++);
    if (ring_room < bytes) {
        if (!diag_on)
            ring_room = wait_room_slow(bytes);
        else {
            diag.waits_ring++;
            t0        = rdtsc();
            ring_room = wait_room_slow(bytes);
            diag.wait_cycles += rdtsc() - t0;
        }
        if (!ring_room)
            return 0;
    }
    ring_room -= bytes;
    return 1;
}

volatile DWORD *
gpu_packet(DWORD op, DWORD len)
{
    const DWORD bytes = 4 + 4 * len;

    /* The largest packet is the same whichever the ring: half the ring in
       video memory. */
    if (bytes > SB_RING_SIZE / 2) {
        dbg_event(DLL_TAG ": packet %x of %u dwords is too long for the ring", op, len);
        return NULL;
    }
    /* Every packet before this one is written: a long batch lets the GPU
       start on them. */
    if (((sb->ring_tail - ring_rung) & ring_mask) >= DOORBELL_BYTES)
        doorbell();
    /* A packet never wraps: NOP to the end of the ring, then start at 0. */
    if (sb->ring_tail + bytes > ring_bytes) {
        DWORD pad = ring_bytes - sb->ring_tail;

        if (!wait_room(pad))
            return NULL;
        *ring_at(sb->ring_tail) = VGPU98_PKT(VGPU98_OP_NOP, pad / 4 - 1);
        sb->ring_tail = 0;
    }
    if (!wait_room(bytes))
        return NULL;
    *ring_at(sb->ring_tail) = VGPU98_PKT(op, len);
    {
        volatile DWORD *payload = ring_at(sb->ring_tail + 4);

        sb->ring_tail = (sb->ring_tail + bytes) & ring_mask;
        return payload;
    }
}

DWORD
gpu_fence(void)
{
    volatile DWORD *p = gpu_packet(VGPU98_OP_FENCE, 1);

    if (!p)
        return sb->fence_issued;
    sb->fence_issued++;
    *p = sb->fence_issued;
    return sb->fence_issued;
}

DWORD
gpu_finish(void)
{
    DWORD fence = gpu_fence();

    if (batch_open) {
        batch_rec_t *b = &batches[batch_n++ % BATCHES];

        b->fence0  = batch_fence0;
        b->fence   = fence;
        b->owner   = batch_owner;
        batch_open = 0;
    }
    doorbell();
    DIAG(diag_report());
    return fence;
}

void
gpu_release(void)
{
    batch_open = 0; /* a batch given up without its fence */
    lock_release();
}

void
gpu_mark(gpu_mark_t *m)
{
    m->tail  = sb->ring_tail;
    m->rings = ring_rings;
}

void
gpu_unwind(const gpu_mark_t *m)
{
    if (!gpu_holds())
        return;
    sb->ring_tail   = (ring_rings != m->rings) ? ring_rung : m->tail;
    ring_room       = 0;
    sb->state_epoch++;
    sb->state_owner = 0;
}

void
gpu_fault_end(void)
{
    DWORD fence;

    if (!gpu_holds())
        return;
    sb->ring_tail   = ring_rung;
    ring_room       = 0;
    sb->state_epoch++;
    sb->state_owner = 0;
    fence           = gpu_finish();
    dp2_fence_read(fence);
    gpu_release();
    /* The faulting call may not have given the surfaces and buffers it had
       the GPU read their fence: the GPU finishes them now. */
    gpu_wait(fence);
}

DWORD
gpu_end(void)
{
    DWORD fence = gpu_finish();

    gpu_release();
    return fence;
}

/* --- Waiting ---------------------------------------------------------------- */

int
gpu_fence_done(DWORD v)
{
    return (LONG) (st_fence() - v) >= 0;
}

static HRESULT
wait_fence(DWORD v)
{
    const DWORD start = GetTickCount();
    DWORD       since = start;
    DWORD       fence = st_fence();
    DWORD       head  = st_head();
    DWORD       told  = 0;
    int         open_told = 0;

    for (;;) {
        DWORD st;
        DWORD now;

        if (gpu_fence_done(v))
            return DD_OK;
        st = gpu_status();
        if (st & VGPU98_STATUS_HELD)
            return DDERR_SURFACELOST;
        if ((st & VGPU98_STATUS_ERROR) || !(st & VGPU98_STATUS_RUNNING)) {
            recover(NULL);
            return DD_OK; /* FENCE now holds the last value issued */
        }
        now = GetTickCount();
        wait_told("fence", v, start, now, &told);
        if ((st_fence() != fence) || (st_head() != head)) {
            fence = st_fence();
            head  = st_head();
            since = now;
        } else if (now - since >= NO_PROGRESS_MS) {
            /* A fence not issued: the next of a batch another process has
               open (under the ring lock), waited for (the GPU is not
               stuck: its holder rings it); else the waiter's mistake. */
            if (((LONG) (v - sb->fence_issued) > 0) && (v == sb->fence_issued + 1) && sb->lock &&
                (sb->lock_owner != GetCurrentProcessId())) {
                if (!open_told) {
                    open_told = 1;
                    dbg_event(DLL_TAG ": a wait in %s for fence %x, not yet issued: a batch is open (ring lock %x); waiting",
                              flight_current(), v, sb->lock_owner);
                }
                since = now;
            } else {
                if (((LONG) (v - sb->fence_issued) > 0)
                        ? dbg_event(DLL_TAG ": a wait in %s for fence %x, never issued (the last %x)", flight_current(), v,
                                    sb->fence_issued)
                        : dbg_event(DLL_TAG ": a wait in %s for fence %x made no progress", flight_current(), v))
                    gpu_state_line();
                recover("made no progress for " NO_PROGRESS_TEXT);
                return DD_OK;
            }
        }
        wait_pause(now - start);
    }
}

HRESULT
gpu_wait(DWORD v)
{
    uint64_t t0;
    HRESULT  hr;

    /* Done already: no clock, no further registers. */
    if (gpu_fence_done(v))
        return DD_OK;
    if (!diag_on)
        return wait_fence(v);
    diag.waits_fence++;
    t0 = rdtsc();
    hr = wait_fence(v);
    diag.wait_cycles += rdtsc() - t0;
    return hr;
}

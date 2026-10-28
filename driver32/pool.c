/*
 * vgpu98dx.dll: the pool, main memory the GPU reads through its page table
 * (interface 3.0: spec/gpu-registers.md, spec/minivdd.md), and DirectDraw's
 * execute buffers in it (spec/directx-hal.md): applications' vertex buffers,
 * the runtime's own vertex buffers and its command buffers.
 *
 * The mini-VDD maps chunks of locked pages for the DLL (DeviceIoControl on
 * \\.\VGPU98, in whatever process asks: the chunks belong to the DLL as a
 * whole, never to a process). The DLL carves them into blocks, one per
 * buffer. Every section of the DLL is shared, so the chunk, block and buffer
 * tables are the same in every process, under the pool lock. While any
 * Direct3D context exists, a chunk that has had no buffer for 10 seconds
 * goes back while 2 MB of such chunks stay (chunk_tidy); when the last
 * context goes, all but one go back (those the GPU may still read once it
 * has).
 *
 * A buffer the pool cannot hold goes in system memory, as the DDK has it:
 * the calling process's own, which the GPU never reads (DrawPrimitives2
 * sends its vertices through the ring), so its Locks never wait. It moves
 * into the pool the next time it may change memory and the pool has room.
 *
 * The GPU reads a buffer when a packet that names it executes, not when it
 * is queued, so nothing may write a buffer while such a packet waits: a
 * buffer keeps the fence of the last batch that read it, a Lock waits for
 * it (or, with DISCARDCONTENTS, hands back fresh memory; a DirectX 8
 * buffer's, fresh memory with its contents), and memory freed or renamed is
 * reused only once that fence has passed. A DirectX 6 or 7 application
 * vertex buffer's draws may reach the DLL during its Lock (issued before
 * it): a rename keeps its old block until the Unlock, which those draws
 * read. A block a rename leaves stays
 * the buffer's own, a spare for its next rename, so the memory a rename
 * hands out holds that buffer's bytes, never another's.
 * The GPU never writes the pool, so a Lock
 * never needs a copy back.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3d.h"

#define POOL_CHUNKS   256        /* the mini-VDD keeps as many chunk records */
#define POOL_BLOCKS   32768
#define POOL_BUFFERS  16384      /* DirectX 8's managed buffers take two each, a system memory copy too */
#define CHUNK_BYTES   0x100000u  /* the usual chunk: 1 MB */
#define BLOCK_ALIGN   32u
#define NONE          0xFFFFFFFFu
#define GROW_RETRY_MS 1000       /* a failed grow of a size is not tried again for this long */
#define GROW_LOGGED   3          /* failures of a step logged in full */
#define GROW_SIZES    3
#define LOCK_COPY_MAX (256u << 10) /* a busy DirectX 8 buffer, or a DirectX 6/7 one drawn while locked, Locked without
                                       flags is copied up to this size */

/* The chunk sizes asked for, the next when one cannot be had (a larger
   buffer's own size if it is larger): small enough at the end that a game's
   buffers still fit while the system is short of locked pages, large enough
   that a burst of them does not use up the mini-VDD's 256 chunk records. */
static const DWORD chunk_sizes[GROW_SIZES] = { CHUNK_BYTES, 0x40000u, 0x10000u };

/* The mini-VDD's requests (spec/minivdd.md). */
#define POOL_GROW    1 /* in: pages; out: linear address, GPU address, pages */
#define POOL_RELEASE 2 /* in: a chunk's GPU address */
#define POOL_QUERY   3 /* out: the cap in pages, the pages mapped, GPUVM_ENTRIES */

typedef struct chunk {
    DWORD linear;     /* 0: a free record */
    DWORD gpu;
    DWORD bytes;
    DWORD first;      /* its first block: they cover it in address order */
    DWORD busy_bytes; /* its buffers' blocks (block_take, block_free): a new buffer goes to the fullest chunk */
    DWORD free_since; /* GetTickCount when chunk_tidy first found it one free block the GPU no longer reads; 0 not */
} chunk_t;

/* While games run: a new buffer
   takes, of the first POOL_PICK free blocks that fit, the one in the
   chunk with the most bytes in use, so buffers gather in few chunks; and,
   at most every POOL_TIDY_MS, a chunk wholly free for POOL_IDLE_MS goes
   back to the mini-VDD while the chunks wholly free would still hold
   POOL_RESERVE without it. */
#define POOL_PICK    16
#define POOL_TIDY_MS 1000
#define POOL_IDLE_MS 10000
#define POOL_RESERVE (2u << 20)

typedef struct block {
    DWORD chunk;   /* NONE: an unused record */
    DWORD offset;
    DWORD size;
    DWORD next;    /* the chunk's next block, NONE after the last; unused records: the next unused */
    DWORD prev;
    int   busy;    /* a buffer's */
    int   pending; /* free, but the GPU may read it until `fence` completes */
    DWORD fence;
    int   listed;  /* on the free list (every recorded block that is not busy) */
    DWORD fnext;   /* the free list's next and previous */
    DWORD fprev;
} block_t;

int pool_on;

static dlock_t pool_lock;
static DWORD   pool_cap;   /* bytes the mini-VDD maps at most */
static DWORD   pool_bytes; /* bytes in chunks */
static DWORD   pool_inited;
static chunk_t chunks[POOL_CHUNKS];
static block_t blocks[POOL_BLOCKS];
static DWORD   block_unused = NONE; /* the first unused block record */
static DWORD   free_head    = NONE; /* the free list: free blocks, most recently freed first */
static pbuf_t  bufs[POOL_BUFFERS];
static DWORD   bufs_low; /* no record below this one is free: a new buffer's search starts there (under the lock) */
static DWORD   grow_failed_at[GROW_SIZES]; /* GetTickCount of the last failed grow of each size; 0 none since one succeeded */
static DWORD   grow_logged[POOL_GROW_STEPS];
static DWORD   sys_tried_at;        /* GetTickCount of the last try to move a buffer from system memory that found no room; 0 none */

/* POOL_GROW's failure steps (spec/minivdd.md), 0 when it names none. */
static const char *const grow_step[POOL_GROW_STEPS] = {
    "no step named", "past the cap", "no chunk record", "no free run of the page table", "no pages (_PageAllocate)",
    "the pages not found (_CopyPageTable)", "a page not present", "a page in the adapter area",
};

/* --- Locks ------------------------------------------------------------------------ */

#define LOCK_TAKEOVER_MS 2000
#define LOCK_SPIN_MS     20
#define DLOCKS           16

static dlock_t *dlocks[DLOCKS]; /* every lock taken so far, for a detaching process */
static DWORD    n_dlocks;

void
dlock_take(dlock_t *l, const char *what)
{
    DWORD expected = 0;
    DWORD start    = 0;

    while (!__atomic_compare_exchange_n(&l->v, &expected, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        const DWORD now = GetTickCount();

        expected = 0;
        if (!start)
            start = now ? now : 1;
        /* Held this long by the same holder as far as both clocks tell (a
           new holder writes its time just after taking the lock): the work
           under these locks takes microseconds, so it has gone. */
        if ((now - start >= LOCK_TAKEOVER_MS) && (now - l->time >= LOCK_TAKEOVER_MS)) {
            dbg_event(DLL_TAG ": taking the %s lock over from process %x", what, l->owner);
            break;
        }
        /* Yield, and after a while sleep, so a holder of lower priority runs. */
        Sleep((now - start < LOCK_SPIN_MS) ? 0 : 1);
    }
    l->owner  = GetCurrentProcessId();
    l->thread = GetCurrentThreadId();
    l->time   = GetTickCount();
    if (!l->listed) {
        const DWORD i = __atomic_fetch_add(&n_dlocks, 1, __ATOMIC_RELAXED);

        l->listed = 1;
        if (i < DLOCKS)
            dlocks[i] = l;
    }
}

void
dlock_give(dlock_t *l)
{
    l->owner  = 0;
    l->thread = 0;
    __atomic_store_n(&l->v, 0, __ATOMIC_RELEASE);
}

void
dlock_release_process(DWORD pid)
{
    DWORD i;

    for (i = 0; (i < n_dlocks) && (i < DLOCKS); i++) {
        if (dlocks[i] && dlocks[i]->v && (dlocks[i]->owner == pid))
            dlock_give(dlocks[i]);
    }
}

void
dlock_release_thread(void)
{
    const DWORD pid = GetCurrentProcessId();
    const DWORD tid = GetCurrentThreadId();
    DWORD       i;

    for (i = 0; (i < n_dlocks) && (i < DLOCKS); i++) {
        if (dlocks[i] && dlocks[i]->v && (dlocks[i]->owner == pid) && (dlocks[i]->thread == tid))
            dlock_give(dlocks[i]);
    }
}

static void
lock(void)
{
    dlock_take(&pool_lock, "pool");
}

static void
unlock(void)
{
    dlock_give(&pool_lock);
}

/* The later of two fences (they wrap). */
static DWORD
fence_later(DWORD a, DWORD b)
{
    return ((LONG) (a - b) > 0) ? a : b;
}

/* --- The mini-VDD ------------------------------------------------------------ */

/* A request to the mini-VDD; 0 if it failed. A handle per request: they are
   rare, and a handle belongs to the process that opens it. */
static int
ioctl(DWORD code, void *in, DWORD in_bytes, void *out, DWORD out_bytes)
{
    const DWORD h = CreateFileA("\\\\.\\VGPU98", 0, 0, NULL, 0, 0, 0);
    DWORD       got = 0;
    BOOL        ok;

    if (h == INVALID_HANDLE_VALUE)
        return 0;
    ok = DeviceIoControl(h, code, in, in_bytes, out, out_bytes, &got, NULL);
    CloseHandle(h);
    return ok && (got == out_bytes);
}

/* --- Blocks ---------------------------------------------------------------------- */

static DWORD
block_record(void)
{
    const DWORD b = block_unused;

    if (b != NONE)
        block_unused = blocks[b].next;
    return b;
}

/* Free blocks are on a list of their own, so an allocation looks at them
   alone, not at every buffer's (under the lock). */
static void
free_link(DWORD b)
{
    block_t *k = &blocks[b];

    k->listed = 1;
    k->fprev  = NONE;
    k->fnext  = free_head;
    if (free_head != NONE)
        blocks[free_head].fprev = b;
    free_head = b;
}

static void
free_unlink(DWORD b)
{
    block_t *k = &blocks[b];

    if (!k->listed)
        return;
    if (k->fprev != NONE)
        blocks[k->fprev].fnext = k->fnext;
    else
        free_head = k->fnext;
    if (k->fnext != NONE)
        blocks[k->fnext].fprev = k->fprev;
    k->listed = 0;
}

static void
block_unrecord(DWORD b)
{
    free_unlink(b);
    blocks[b].chunk = NONE;
    blocks[b].next  = block_unused;
    block_unused    = b;
}

/* May a free block be handed out? Its last reader is done (or the GPU is
   held: a hold comes with a RESET, so nothing queued will read it). */
static int
block_ready(block_t *k)
{
    if (k->pending && (gpu_fence_done(k->fence) || gpu_held()))
        k->pending = 0;
    return !k->pending;
}

/* Merges free block b with its free successor: with `mixed`, whatever the
   GPU may still read of either (the merged block waits for the later
   fence: a chunk leaving the pool); else only two ready ones, or two the
   GPU may read, so a block given back pending (a rename's old memory) never
   holds a ready neighbour back. Ready ones meet again once its fence passes
   (block_take). */
static void
block_merge_next(DWORD b, int mixed)
{
    block_t    *k = &blocks[b];
    const DWORD n = k->next;

    if ((n == NONE) || blocks[n].busy)
        return;
    if (!mixed && (!block_ready(k) != !block_ready(&blocks[n])))
        return;
    if (blocks[n].pending)
        k->fence = k->pending ? fence_later(k->fence, blocks[n].fence) : blocks[n].fence;
    k->pending |= blocks[n].pending;
    k->size += blocks[n].size;
    k->next = blocks[n].next;
    if (k->next != NONE)
        blocks[k->next].prev = b;
    block_unrecord(n);
}

/* A ready free block of `size` bytes (a multiple of BLOCK_ALIGN), split off
   the one in the fullest chunk of the first POOL_PICK on the free list that
   fit; NONE if none (under the lock). */
static DWORD
block_take(DWORD size)
{
    DWORD best = NONE;
    DWORD seen = 0;
    DWORD b;

    for (b = free_head; (b != NONE) && (seen < POOL_PICK); b = blocks[b].fnext) {
        block_t *k = &blocks[b];

        if (!block_ready(k))
            continue;
        /* Ready neighbours a pending block kept apart, together again (one
           taking the best so far in, the best is it). */
        while ((k->next != NONE) && !blocks[k->next].busy && block_ready(&blocks[k->next])) {
            if (k->next == best)
                best = b;
            block_merge_next(b, 0);
        }
        if (k->size < size)
            continue;
        seen++;
        if ((best == NONE) || (chunks[k->chunk].busy_bytes > chunks[blocks[best].chunk].busy_bytes))
            best = b;
    }
    if (best != NONE) {
        block_t *k = &blocks[best];

        b = best;
        if (k->size > size) {
            const DWORD t = block_record();

            if (t != NONE) {
                blocks[t].chunk   = k->chunk;
                blocks[t].offset  = k->offset + size;
                blocks[t].size    = k->size - size;
                blocks[t].next    = k->next;
                blocks[t].prev    = b;
                blocks[t].busy    = 0;
                blocks[t].pending = 0;
                blocks[t].fence   = 0;
                if (k->next != NONE)
                    blocks[k->next].prev = t;
                k->next = t;
                k->size = size;
                free_link(t);
            }
        }
        k->busy = 1;
        free_unlink(b);
        chunks[k->chunk].busy_bytes += k->size; /* the whole block when no record split it */
        chunks[k->chunk].free_since = 0;
        return b;
    }
    return NONE;
}

/* The earliest fence after which a run of free blocks of `size` bytes with
   a pending one in it is all ready (pending blocks merge only with pending
   ones, so a run may mix them: block_take merges it once all are ready); 0
   if there is none (under the lock). A run starts at a free block with no
   free one before it. */
static int
block_pending_fit(DWORD size, DWORD *fence)
{
    int   found = 0;
    DWORD b;

    for (b = free_head; b != NONE; b = blocks[b].fnext) {
        const block_t *k       = &blocks[b];
        DWORD          run     = 0;
        DWORD          f       = 0;
        int            pending = 0;
        DWORD          n;

        if ((k->prev != NONE) && !blocks[k->prev].busy)
            continue;
        for (n = b; (run < size) && (n != NONE) && !blocks[n].busy; n = blocks[n].next) {
            run += blocks[n].size;
            if (blocks[n].pending) {
                f       = pending ? fence_later(f, blocks[n].fence) : blocks[n].fence;
                pending = 1;
            }
        }
        if (pending && (run >= size) && (!found || ((LONG) (f - *fence) < 0))) {
            *fence = f;
            found  = 1;
        }
    }
    return found;
}

/* A chunk record and its first block, claimed for a chunk of `bytes` the
   mini-VDD is asked for outside the lock (under the lock): chunk_sizes[step],
   or the buffer's size if it is larger. NONE if there is no record, the cap
   is reached, a grow of the size failed less than GROW_RETRY_MS ago, or the
   size is the one a step before asked for. */
#define CHUNK_CLAIMED 1u /* chunk_t.linear while the chunk is being made */

static DWORD
chunk_claim(DWORD size, int step, DWORD *bytes_out)
{
    DWORD bytes = (size > chunk_sizes[step]) ? size : chunk_sizes[step];
    DWORD c;
    DWORD b;

    if ((step && (size >= chunk_sizes[step - 1])) ||
        (grow_failed_at[step] && (GetTickCount() - grow_failed_at[step] < GROW_RETRY_MS)))
        return NONE;
    bytes = (bytes + 0xfff) & ~0xfffu;
    if (bytes > pool_cap - pool_bytes) {
        /* The rest of the cap, if the request fits in it. */
        bytes = pool_cap - pool_bytes;
        if (bytes < size)
            return NONE;
    }
    for (c = 0; (c < POOL_CHUNKS) && chunks[c].linear; c++)
        ;
    if ((c == POOL_CHUNKS) || ((b = block_record()) == NONE))
        return NONE;
    /* Busy until the chunk is there, so nothing takes it. */
    blocks[b].chunk   = c;
    blocks[b].offset  = 0;
    blocks[b].size    = bytes;
    blocks[b].next    = NONE;
    blocks[b].prev    = NONE;
    blocks[b].busy    = 1;
    blocks[b].pending = 0;
    blocks[b].fence   = 0;
    blocks[b].listed  = 0;
    chunks[c].linear     = CHUNK_CLAIMED;
    chunks[c].gpu        = 0;
    chunks[c].bytes      = bytes;
    chunks[c].first      = b;
    chunks[c].busy_bytes = 0; /* its first block is busy only until the chunk is there */
    chunks[c].free_since = 0;
    pool_bytes += bytes;
    *bytes_out = bytes;
    return c;
}

/* Asks the mini-VDD for a claimed chunk of chunk_sizes[size_step] (without
   the lock), then makes it part of the pool, or gives the claim up. A
   failure names the mini-VDD's step: the first GROW_LOGGED of a step
   logged, all counted. */
static int
chunk_make(DWORD c, DWORD bytes, int size_step)
{
    DWORD in     = bytes >> 12;
    DWORD out[3] = { NONE, 0, 0 };
    DWORD step   = 0;
    int   ok;

    ok = ioctl(POOL_GROW, &in, sizeof(in), out, sizeof(out)) && out[0] && !(out[1] & 0xfff);
    if (ok && (out[2] != in)) {
        /* Not what was asked: it goes straight back. */
        DWORD gpu = out[1];

        ioctl(POOL_RELEASE, &gpu, sizeof(gpu), NULL, 0);
        ok = 0;
    } else if (!ok && !out[0] && (out[1] < POOL_GROW_STEPS)) {
        /* A refusal writes 0 and the step that failed. */
        step = out[1];
    }
    if (!ok) {
        DIAG(diag.grow_fails[step]++);
        if (__atomic_fetch_add(&grow_logged[step], 1, __ATOMIC_RELAXED) < GROW_LOGGED)
            dbg_event_own(DLL_TAG ": the mini-VDD could not map %u KB more for the pool: %s (%u KB of %u KB mapped)",
                          bytes >> 10, grow_step[step], (pool_bytes - bytes) >> 10, pool_cap >> 10);
    }
    lock();
    if (ok) {
        chunks[c].linear             = out[0];
        chunks[c].gpu                = out[1];
        blocks[chunks[c].first].busy = 0;
        free_link(chunks[c].first);
        grow_failed_at[size_step]    = 0;
    } else {
        const DWORD now = GetTickCount();

        block_unrecord(chunks[c].first);
        chunks[c].linear = 0;
        pool_bytes -= bytes;
        grow_failed_at[size_step] = now ? now : 1;
    }
    unlock();
    return ok;
}

/* Chunks taken out of the pool, for the mini-VDD to release once the lock
   is let go and the GPU has read them (pool_release_queued). */
typedef struct release {
    DWORD gpu;
    int   pending; /* the GPU may read it until `fence` completes */
    DWORD fence;
} release_t;

static release_t release_q[POOL_CHUNKS];
static DWORD     release_n;

/* Is chunk c one free block? */
static int
chunk_whole_free(DWORD c)
{
    const block_t *k;

    if (!chunks[c].linear || (chunks[c].linear == CHUNK_CLAIMED))
        return 0;
    k = &blocks[chunks[c].first];
    return !k->busy && (k->next == NONE);
}

/* With no Direct3D context left, chunks wholly free leave the pool, but for
   one kept for the next buffers: one the GPU no longer reads before one it
   may, one of the usual size before others, then the smallest (a large one
   would hold locked pages with no program drawing). One the GPU may still
   read goes with the fence it waits for (under the lock). While contexts
   exist, chunk_tidy gives chunks back. */
/* A chunk's free blocks, pending or not, merged into runs (a chunk about to
   leave: under the lock, no context existing). */
static void
chunk_merge(DWORD c)
{
    DWORD b;

    if (!chunks[c].linear || (chunks[c].linear == CHUNK_CLAIMED))
        return;
    for (b = chunks[c].first; b != NONE; b = blocks[b].next) {
        while (!blocks[b].busy && (blocks[b].next != NONE) && !blocks[blocks[b].next].busy)
            block_merge_next(b, 1);
    }
}

static void
chunk_trim(int merge)
{
    DWORD keep       = NONE;
    int   keep_score = 0;
    DWORD c;

    if (d3d_contexts())
        return;
    /* With `merge` (the last context gone: pool_trim), a chunk's free
       blocks, pending or not, as one: it leaves whole, once the GPU has read
       the last of it. */
    for (c = 0; merge && (c < POOL_CHUNKS); c++)
        chunk_merge(c);
    for (c = 0; c < POOL_CHUNKS; c++) {
        int score;

        if (!chunk_whole_free(c))
            continue;
        score = (block_ready(&blocks[chunks[c].first]) ? 0 : 2) + ((chunks[c].bytes == CHUNK_BYTES) ? 0 : 1);
        if ((keep == NONE) || (score < keep_score) ||
            ((score == keep_score) && (chunks[c].bytes < chunks[keep].bytes))) {
            keep       = c;
            keep_score = score;
        }
    }
    for (c = 0; (c < POOL_CHUNKS) && (release_n < POOL_CHUNKS); c++) {
        block_t *k;

        if (!chunk_whole_free(c) || (c == keep))
            continue;
        k                            = &blocks[chunks[c].first];
        release_q[release_n].gpu     = chunks[c].gpu;
        release_q[release_n].pending = !block_ready(k);
        release_q[release_n].fence   = k->fence;
        release_n++;
        block_unrecord(chunks[c].first);
        pool_bytes -= chunks[c].bytes;
        chunks[c].linear = 0;
    }
}

/* A chunk's free blocks the GPU no longer reads, merged into runs (under
   the lock): a pending one stays apart, as everywhere while contexts run. */
static void
chunk_merge_ready(DWORD c)
{
    DWORD b;

    for (b = chunks[c].first; b != NONE; b = blocks[b].next) {
        while (!blocks[b].busy && block_ready(&blocks[b]) && (blocks[b].next != NONE) && !blocks[blocks[b].next].busy &&
               block_ready(&blocks[blocks[b].next]))
            block_merge_next(b, 0);
    }
}

/* While a Direct3D context exists, at most every POOL_TIDY_MS (under the
   lock, pool_release_queued afterwards): a chunk without a buffer whose
   free blocks the GPU no longer reads is noted free; one free for
   POOL_IDLE_MS goes back, the longest free first, while the chunks wholly
   free would still hold POOL_RESERVE without it. The GPU reads
   nothing of it: its last batch is done. */
static DWORD tidy_at;

static void
chunk_tidy(void)
{
    const DWORD now        = GetTickCount();
    DWORD       free_bytes = 0;
    DWORD       c;

    if (!d3d_contexts() || (tidy_at && (now - tidy_at < POOL_TIDY_MS)))
        return;
    tidy_at = now ? now : 1;
    for (c = 0; c < POOL_CHUNKS; c++) {
        if (!chunks[c].linear || (chunks[c].linear == CHUNK_CLAIMED))
            continue;
        if (!chunks[c].busy_bytes)
            chunk_merge_ready(c);
        if (chunks[c].busy_bytes || !chunk_whole_free(c) || !block_ready(&blocks[chunks[c].first])) {
            chunks[c].free_since = 0;
            continue;
        }
        if (!chunks[c].free_since)
            chunks[c].free_since = now ? now : 1;
        free_bytes += chunks[c].bytes;
    }
    while (release_n < POOL_CHUNKS) {
        DWORD pick = NONE;

        /* The longest free of those that can go without touching the
           reserve: a large chunk that cannot leaves the smaller ones free
           to. */
        for (c = 0; c < POOL_CHUNKS; c++) {
            if (!chunks[c].linear || (chunks[c].linear == CHUNK_CLAIMED) || !chunks[c].free_since ||
                (now - chunks[c].free_since < POOL_IDLE_MS) || (free_bytes < chunks[c].bytes + POOL_RESERVE))
                continue;
            if ((pick == NONE) || ((LONG) (chunks[c].free_since - chunks[pick].free_since) < 0))
                pick = c;
        }
        if (pick == NONE)
            break;
        release_q[release_n].gpu     = chunks[pick].gpu;
        release_q[release_n].pending = 0;
        release_q[release_n].fence   = 0;
        release_n++;
        block_unrecord(chunks[pick].first);
        pool_bytes -= chunks[pick].bytes;
        free_bytes -= chunks[pick].bytes;
        DIAG(diag.pool_given_back += chunks[pick].bytes >> 10);
        chunks[pick].linear     = 0;
        chunks[pick].free_since = 0;
    }
}

static void pool_release_queued(void);

/* At the end of a DrawPrimitives2 call, outside every lock: the tidy when
   it is due, so a game that frees no buffer after a load still gives its
   idle chunks back, and the chunks queued under the ring lock (a write's
   rename frees there) handed to the mini-VDD. The time read without the
   lock: a call early or late by one changes nothing. */
void
pool_idle(void)
{
    const DWORD now = GetTickCount();

    if (!pool_on || (!release_n && tidy_at && (now - tidy_at < POOL_TIDY_MS)))
        return;
    lock();
    chunk_tidy();
    unlock();
    pool_release_queued();
}

/* The mini-VDD releases the chunks chunk_trim took out (without the lock),
   each once the GPU has read it (or a hold dropped what was queued). */
static void
pool_release_queued(void)
{
    release_t q[POOL_CHUNKS];
    DWORD     n;
    DWORD     i;

    if (!release_n)
        return;
    lock();
    n = release_n;
    memcpy(q, release_q, n * sizeof(release_t));
    release_n = 0;
    unlock();
    for (i = 0; i < n; i++) {
        if (q[i].pending)
            gpu_wait(q[i].fence);
        if (!ioctl(POOL_RELEASE, &q[i].gpu, sizeof(q[i].gpu), NULL, 0))
            dbg_event(DLL_TAG ": the mini-VDD refused to release the pool's chunk at GPU address %x", q[i].gpu);
    }
}

static void block_free(DWORD b, int pending, DWORD fence);

/* --- A buffer's own spare blocks --- */

/* A block leaving buffer b (a rename's old one, the kept block at the
   Unlock): one of its spares while it has room and the block holds its
   size, else back to the pool. A queued packet may read it until `fence`
   if `drawn`. Under the lock. */
static void
spare_put(pbuf_t *b, DWORD k, int drawn, DWORD fence)
{
    if ((b->n_spare < PB_SPARES) && (blocks[k].size >= b->size)) {
        b->spare[b->n_spare]       = k;
        b->spare_drawn[b->n_spare] = drawn;
        b->spare_fence[b->n_spare] = fence;
        b->n_spare++;
    } else
        block_free(k, drawn, fence);
}

/* One of b's spares no queued packet reads; NONE if none is ready. Under
   the lock. */
static DWORD
spare_take(pbuf_t *b)
{
    DWORD i;

    for (i = 0; i < b->n_spare; i++) {
        if (!b->spare_drawn[i] || gpu_fence_done(b->spare_fence[i]) || gpu_held()) {
            const DWORD k = b->spare[i];

            b->n_spare--;
            b->spare[i]       = b->spare[b->n_spare];
            b->spare_drawn[i] = b->spare_drawn[b->n_spare];
            b->spare_fence[i] = b->spare_fence[b->n_spare];
            return k;
        }
    }
    return NONE;
}

/* b's spares back to the pool (under the lock). */
static void
spares_free(pbuf_t *b)
{
    while (b->n_spare) {
        b->n_spare--;
        block_free(b->spare[b->n_spare], b->spare_drawn[b->n_spare], b->spare_fence[b->n_spare]);
    }
}

/* Every buffer's spares back to the pool, an allocation of `size` bytes
   having found no room, when they hold that much together (a request they
   could never meet leaves them); whether any went. Under the lock. */
static int
spares_free_all(DWORD size)
{
    uint64_t held = 0;
    DWORD    i;
    DWORD    k;
    int      any = 0;

    for (i = 0; i < POOL_BUFFERS; i++) {
        for (k = 0; bufs[i].gbl && (k < bufs[i].n_spare); k++)
            held += blocks[bufs[i].spare[k]].size;
    }
    if (held < size)
        return 0;
    for (i = 0; i < POOL_BUFFERS; i++) {
        if (bufs[i].gbl && bufs[i].n_spare) {
            DIAG(diag.spares_freed += bufs[i].n_spare);
            spares_free(&bufs[i]);
            any = 1;
        }
    }
    return any;
}

/* Frees block b, which the GPU may read until `fence` if `pending` (under the
   lock; pool_release_queued afterwards). */
static void
block_free(DWORD b, int pending, DWORD fence)
{
    const DWORD c = blocks[b].chunk; /* b's record may merge away below */

    chunks[c].busy_bytes -= (chunks[c].busy_bytes >= blocks[b].size) ? blocks[b].size : chunks[c].busy_bytes;
    blocks[b].busy    = 0;
    blocks[b].pending = pending;
    blocks[b].fence   = fence;
    free_link(b);
    block_merge_next(b, 0);
    if ((blocks[b].prev != NONE) && !blocks[blocks[b].prev].busy)
        block_merge_next(blocks[b].prev, 0);
    /* With no context left (buffers destroyed after the last one), this
       block's chunk may leave whole: its free blocks merge whatever the GPU
       still reads; while contexts run, chunks free for a while go back. */
    if (!d3d_contexts())
        chunk_merge(c);
    chunk_trim(0);
    chunk_tidy();
}

void
pool_trim(void)
{
    if (!pool_on)
        return;
    lock();
    chunk_trim(1);
    unlock();
    pool_release_queued();
}

/* A block of `size` bytes: from the chunks, from a new chunk (the first of
   chunk_sizes that can be had), or, with `may_wait`, once the GPU has
   finished with a freed one. NONE if the pool cannot hold it. */
static DWORD
block_alloc(DWORD size, int may_wait)
{
    int step  = 0;
    int spent = 0; /* the buffers' spares went back for room */
    int tries;

    if (!size || (size > pool_cap))
        return NONE;
    size = (size + BLOCK_ALIGN - 1) & ~(BLOCK_ALIGN - 1);
    for (tries = 0; tries < 8; tries++) {
        DWORD b;
        DWORD c     = NONE;
        DWORD bytes = 0;
        DWORD fence = 0;
        int   wait  = 0;

        lock();
        b = block_take(size);
        while ((b == NONE) && (c == NONE) && (step < GROW_SIZES)) {
            c = chunk_claim(size, step, &bytes);
            if (c == NONE)
                step++;
        }
        /* No room: the buffers' spare blocks go back first. */
        if ((b == NONE) && (c == NONE) && !spent) {
            spent = 1;
            if (spares_free_all(size)) {
                unlock();
                pool_release_queued();
                continue;
            }
        }
        if ((b == NONE) && (c == NONE) && may_wait)
            wait = block_pending_fit(size, &fence);
        unlock();
        if (b != NONE)
            return b;
        if (c != NONE) {
            if (!chunk_make(c, bytes, step))
                step++; /* a smaller chunk, or a freed block */
            continue;   /* else take from it */
        }
        if (!wait)
            break;
        gpu_wait(fence);
    }
    return NONE;
}

static DWORD
block_linear(DWORD b)
{
    return chunks[blocks[b].chunk].linear + blocks[b].offset;
}

/* --- Buffers ------------------------------------------------------------------- */

/* The buffer whose global record `gbl` is: the runtime's, for a buffer not
   adopted; the buffer's own, for an adopted one (its copy's). NULL if
   none. */
static pbuf_t *
buffer_by_gbl(DDRAWI_DDRAWSURFACE_GBL *gbl)
{
    const DWORD i = gbl ? gbl->dwReserved1 - 1 : NONE;

    return ((i < POOL_BUFFERS) && (bufs[i].gbl == gbl)) ? &bufs[i] : NULL;
}

/* A buffer's memory as the pool holds it. */
static DWORD
buffer_mem(const pbuf_t *b)
{
    return b->sys ? b->mem : block_linear(b->block);
}

/* An application's vertex or index buffer by its record: DirectX 8's are
   the ones adopted. DirectDraw's own system memory buffers are never the
   DLL's. */
static int
record_adoptable(LPSURF s)
{
    return !(s->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) && s->lpSurfMore &&
           (s->lpSurfMore->ddsCapsEx.dwCaps2 & (DDSCAPS2_VERTEXBUFFER | DDSCAPS2_INDEXBUFFER));
}

/* May this process's call take adopted buffer b as the one it names? Its
   own process's calls, and (`ended_too`) anyone's for a buffer whose
   process ended: DDHELP destroying what it left. A process that ended
   without destroying its DirectDraw object may have left adopted buffers
   under the object's address, which another process's object may take
   with the same handles (d3d7_mark_dx8's takeover). */
static int
buffer_ours(const pbuf_t *b, DWORD pid, int ended_too)
{
    return (b->pid == pid) || (ended_too && !process_alive(b->pid));
}

/* The adopted buffer under (ddl, handle), by a walk of the table: for a
   record whose handle the map no longer holds (its DirectDraw object or its
   process forgotten before the destroy came), or holds for another process.
   This process's first, else (`ended_too`) one a process that ended left.
   Read without the lock, as buffer_by_gbl reads: adoption and freeing
   change these fields in the buffer's own calls. */
static pbuf_t *
buffer_adopted(const void *ddl, DWORD handle, int ended_too)
{
    const DWORD pid  = GetCurrentProcessId();
    pbuf_t     *left = NULL;
    DWORD       k;

    for (k = 0; k < POOL_BUFFERS; k++) {
        pbuf_t *b = &bufs[k];

        if ((b->gbl != &b->own) || (b->ddl != ddl) || (b->handle != handle))
            continue;
        if (b->pid == pid)
            return b;
        if (ended_too && !left && !process_alive(b->pid))
            left = b;
    }
    return left;
}

/* An adopted buffer a record names by the index in it alone (a record
   without its handle or object, as a destroy notice may come): the index's
   buffer if adopted, of its kind, and at the memory the record holds (each
   Lock writes the buffer's memory into the call's record). */
static pbuf_t *
buffer_by_memory(LPSURF s, int ended_too)
{
    const DWORD i = s->lpGbl->dwReserved1 - 1;
    pbuf_t     *b;

    if (i >= POOL_BUFFERS)
        return NULL;
    b = &bufs[i];
    if ((b->gbl != &b->own) || !s->lpGbl->fpVidMem || (b->own.fpVidMem != s->lpGbl->fpVidMem) ||
        !buffer_ours(b, GetCurrentProcessId(), ended_too))
        return NULL;
    return b;
}

/* The DLL's buffer behind a surface, in the pool or in system memory; NULL
   if it is not one. A DirectX 8 runtime's record may be one it reuses at
   once, so an application's vertex or index buffer is
   found, in a process with DirectX 8 on its DirectDraw object, by its
   handle's copy first; then (a buffer not adopted) by the index in its
   record; then among the adopted buffers by its DirectDraw object and
   handle; then by the index and the memory the record holds. A buffer a
   process that ended left is taken only for a destroy (`ended_too`:
   DDHELP's). The DLL's copy of an adopted buffer holds the buffer's own
   record. Every other surface (the runtime's own vertex and command
   buffers, DirectDraw's system memory ones) by the index alone, as before;
   DirectX 7's buffers by the index, the later steps finding nothing of
   theirs. */
static pbuf_t *
buffer_find(LPSURF s, int ended_too)
{
    const DDRAWI_DDRAWSURFACE_MORE *m = s ? s->lpSurfMore : NULL;
    pbuf_t                         *b;

    if (!pool_on || !s || !(s->ddsCaps.dwCaps & DDSCAPS_EXECUTEBUFFER))
        return NULL;
    if (heap_owns(s)) {
        /* A copy: an adopted buffer's only while the buffer is still the
           one adopted under the copy's object and handle. */
        b = buffer_by_gbl(s->lpGbl);
        if (b && (b->gbl == &b->own) && (!m || (b->ddl != m->lpDD_lcl) || (b->handle != m->dwSurfaceHandle)))
            return NULL;
        return b;
    }
    if (!record_adoptable(s))
        return buffer_by_gbl(s->lpGbl);
    if (surf_dx8(s)) {
        b = buffer_by_gbl(d3d7_buffer_gbl(m->lpDD_lcl, m->dwSurfaceHandle));
        if (b && (b->gbl == &b->own) && (b->ddl == m->lpDD_lcl) && (b->handle == m->dwSurfaceHandle) &&
            buffer_ours(b, GetCurrentProcessId(), 0))
            return b;
    }
    b = buffer_by_gbl(s->lpGbl);
    if (b)
        return b;
    if (m->dwSurfaceHandle && m->lpDD_lcl && (b = buffer_adopted(m->lpDD_lcl, m->dwSurfaceHandle, ended_too)))
        return b;
    return buffer_by_memory(s, ended_too);
}

static pbuf_t *
buffer_of(LPSURF s)
{
    return buffer_find(s, 0);
}

DDRAWI_DDRAWSURFACE_GBL *
pool_adopt(LPSURF s, void *ddl, DWORD handle)
{
    DDRAWI_DDRAWSURFACE_GBL *g   = s->lpGbl;
    const DWORD              pid = GetCurrentProcessId();
    DWORD                    i;
    pbuf_t                  *again;
    pbuf_t                  *b;

    if (!pool_on || !handle || !ddl || !(s->ddsCaps.dwCaps & DDSCAPS_EXECUTEBUFFER) || !record_adoptable(s))
        return NULL;
    /* A handle recorded again: its buffer adopted already (the map's, read
       before the pool's lock). */
    again = buffer_by_gbl(d3d7_buffer_gbl(ddl, handle));
    lock();
    /* The record CreateExecuteBuffer filled, at the memory it gave it
       (nothing moves a buffer before its CreateSurfaceEx); else one that
       carries its index and memory (a record the runtime gives this call
       alone). */
    b = buffer_by_gbl(g);
    if (b && (buffer_mem(b) != g->fpVidMem))
        b = NULL;
    i = g->dwReserved1 - 1;
    if (!b && (i < POOL_BUFFERS) && bufs[i].gbl && (bufs[i].gbl != &bufs[i].own) && (bufs[i].pid == pid) &&
        (buffer_mem(&bufs[i]) == g->fpVidMem))
        b = &bufs[i];
    if (!b && again && (again->gbl == &again->own) && (again->ddl == ddl) && (again->handle == handle) && (again->pid == pid))
        b = again;
    if (!b)
        b = buffer_adopted(ddl, handle, 0);
    if (!b || ((b->kind != PB_VERTEX) && (b->kind != PB_INDEX))) {
        unlock();
        return NULL;
    }
    if (b->gbl != &b->own) {
        /* The memory and size from the pool's record, the rest from the
           runtime's. */
        memset(&b->own, 0, sizeof(b->own));
        b->own.dwRefCnt      = 1;
        b->own.dwGlobalFlags = g->dwGlobalFlags | DDRAWISURFGBL_SYSMEMEXECUTEBUFFER;
        b->own.dwBlockSizeY  = g->dwBlockSizeY;
        b->own.dwBlockSizeX  = g->dwBlockSizeX;
        b->own.fpVidMem      = buffer_mem(b);
        b->own.lPitch        = (LONG) b->size;
        b->own.wHeight       = g->wHeight;
        b->own.wWidth        = g->wWidth;
        b->own.dwReserved1   = (DWORD) (b - bufs) + 1;
        b->own.ddpfSurface   = g->ddpfSurface;
        b->ddl               = ddl;
        b->handle            = handle;
        b->gbl               = &b->own;
    }
    unlock();
    return &b->own;
}

int
pool_has(LPSURF s)
{
    return buffer_of(s) != NULL;
}

DWORD
exebuf_bytes(LPSURF s)
{
    const pbuf_t *b = buffer_of(s);

    /* DirectDraw's own (DDSCAPS_SYSTEMMEMORY): the DDK does not say its
       linear size is the memory's, so 0. */
    return b ? b->size : 0;
}

pbuf_t *
pool_buffer(LPSURF s)
{
    pbuf_t *b = buffer_of(s);

    return (b && !b->sys) ? b : NULL;
}

static DWORD
block_gpu(DWORD b)
{
    return chunks[blocks[b].chunk].gpu + blocks[b].offset;
}

DWORD
pool_gpu(const pbuf_t *b)
{
    return block_gpu(b->block);
}

DWORD
pool_block_alloc(DWORD size)
{
    return pool_on ? block_alloc(size, 0) : NONE;
}

void
pool_block_free(DWORD k, DWORD fence)
{
    lock();
    block_free(k, 1, fence);
    unlock();
    pool_release_queued();
}

DWORD
pool_block_gpu(DWORD k)
{
    return block_gpu(k);
}

uint8_t *
pool_block_mem(DWORD k)
{
    return (uint8_t *) block_linear(k);
}

DWORD
pool_version_new(void)
{
    DWORD v;

    do
        v = __atomic_add_fetch(&sb->buf_version, 1, __ATOMIC_RELAXED);
    while (!v);
    return v;
}

void
pool_drawn(pbuf_t *b, DWORD fence)
{
    b->fence = fence;
    b->drawn = 1;
}

int
pool_kept(const pbuf_t *b, const uint8_t **mem, DWORD *gpu, DWORD *size, DWORD *version)
{
    if (!b->kept_on)
        return 0;
    *mem     = (const uint8_t *) block_linear(b->kept);
    *gpu     = block_gpu(b->kept);
    *size    = b->kept_size;
    *version = b->kept_version;
    return 1;
}

void
pool_kept_drawn(pbuf_t *b, DWORD fence)
{
    if (b->kept_on) {
        b->kept_fence = fence;
        b->kept_drawn = 1;
    }
}

/* The buffer's kept block goes, once no queued packet reads it: one of its
   spares with `spare`, else back to the pool (under the lock;
   pool_release_queued afterwards). */
static void
kept_free(pbuf_t *b, int spare)
{
    if (b->kept_on && spare)
        spare_put(b, b->kept, b->kept_drawn, b->kept_fence);
    else if (b->kept_on)
        block_free(b->kept, b->kept_drawn, b->kept_fence);
    b->kept_on = 0;
}

void
pool_drawn_all(DWORD fence)
{
    DWORD i;

    for (i = 0; i < POOL_BUFFERS; i++) {
        if (bufs[i].gbl && !bufs[i].sys)
            pool_drawn(&bufs[i], fence);
    }
}

/* --- The runtime buffers' count --- */

/* The last RT_WINDOWS windows sent from the runtime's own buffers, each
   with a sum of its first and last RT_SAMPLE bytes; the oldest is checked
   as a new one takes its place. Checked only in its own process, while its
   buffer has the number it had (no Lock since that would wait or rename,
   not destroyed: a new buffer has a new number) and the same memory. */
#define RT_WINDOWS 32
#define RT_SAMPLE  64

typedef struct {
    pbuf_t *b;
    DWORD   pid;
    DWORD   gen;
    DWORD   mem;
    DWORD   off;
    DWORD   len;
    DWORD   sum;
    DWORD   noow;
} rt_win_t;

static rt_win_t rt_win[RT_WINDOWS];
static DWORD rt_next;

static DWORD
rt_sum(const uint8_t *p, DWORD len)
{
    const DWORD n = (len < RT_SAMPLE) ? len : RT_SAMPLE;
    DWORD       h = 0x811C9DC5u;
    DWORD       i;

    for (i = 0; i < n; i++)
        h = (h ^ p[i]) * 0x01000193u;
    for (i = len - n; i < len; i++)
        h = (h ^ p[i]) * 0x01000193u;
    return h;
}

void
pool_rt_window(pbuf_t *b, const uint8_t *at, DWORD len)
{
    const DWORD                    pid = GetCurrentProcessId();
    const DDRAWI_DDRAWSURFACE_GBL *g   = b->gbl;
    DWORD                          base;
    DWORD                          off;
    DWORD                          k;
    rt_win_t                       e;
    rt_win_t                       w;

    if (!len || b->sys || !g)
        return;
    base = g->fpVidMem;
    off  = (DWORD) at - base;
    if (((DWORD) at < base) || (off > b->size) || (len > b->size - off))
        return;
    k = __atomic_fetch_add(&rt_next, 1, __ATOMIC_RELAXED) % RT_WINDOWS;
    diag.rt_windows++;
    /* The entry as it was, and its buffer's record, read once (another
       thread may take the place meanwhile). */
    e = rt_win[k];
    if (e.b && (e.pid == pid) && (e.b->rt_gen == e.gen) && !e.b->sys) {
        const DDRAWI_DDRAWSURFACE_GBL *eg   = e.b->gbl;
        const DWORD                    size = e.b->size;

        if (eg && (eg->fpVidMem == e.mem) && (e.off <= size) && (e.len <= size - e.off)) {
            diag.rt_checked++;
            if (rt_sum((const uint8_t *) e.mem + e.off, e.len) != e.sum) {
                diag.rt_changed++;
                if (e.b->rt_noow != e.noow)
                    diag.rt_changed_noow++;
            }
        }
    }
    w.b      = b;
    w.pid    = pid;
    w.gen    = b->rt_gen;
    w.mem    = base;
    w.off    = off;
    w.len    = len;
    w.sum    = rt_sum(at, len);
    w.noow   = b->rt_noow;
    rt_win[k] = w;
}

/* Might a queued packet still read the buffer? */
static int
buffer_busy(pbuf_t *b)
{
    if (b->drawn && (gpu_fence_done(b->fence) || gpu_held()))
        b->drawn = 0;
    return b->drawn;
}

/* buffer_rename's ways. */
#define RN_KEEP  1 /* the contents copied in */
#define RN_WAIT  2 /* a block the GPU is still to finish with may be waited for */
#define RN_HOLD  4 /* the old block kept until the Unlock (the contents from before the Lock) */
#define RN_SPARE 8 /* the buffer's own spares: one taken first, the old block one */

/* A block of the buffer's leaving it: one of its spares with RN_SPARE, else
   back to the pool (under the lock). */
static void
rename_leave(pbuf_t *b, DWORD k, int drawn, DWORD fence, int how)
{
    if (how & RN_SPARE)
        spare_put(b, k, drawn, fence);
    else
        block_free(k, drawn, fence);
}

/* New memory of `size` bytes for the buffer (`how`: RN_*). With RN_SPARE
   it is one of the buffer's spares no queued packet reads, else a fresh
   block, which gets the buffer's contents whatever RN_KEEP says (a fresh
   block holds another buffer's bytes); with RN_KEEP the
   contents go in either way. The old block goes once the GPU has read it
   (to the spares with RN_SPARE), or with RN_HOLD is kept until the Unlock,
   an older kept block going. A rename while a Lock with a kept block is
   open keeps that one (the contents from before the first Lock) and lets
   the old go. 0 if the pool has none. */
static int
buffer_rename(pbuf_t *b, DWORD size, int how)
{
    DWORD nb    = NONE;
    int   fresh = 1;

    if ((how & RN_SPARE) && (size == b->size)) {
        lock();
        nb = spare_take(b);
        unlock();
        fresh = nb == NONE;
    }
    if (nb == NONE)
        nb = block_alloc(size, (how & RN_WAIT) != 0);
    if (nb == NONE)
        return 0;
    if ((how & RN_KEEP) || (fresh && (how & RN_SPARE)))
        memcpy((void *) block_linear(nb), (const void *) block_linear(b->block), (size < b->size) ? size : b->size);
    DIAG({
        if (how & RN_SPARE) {
            diag.rn_spare += !fresh;
            diag.rn_fresh += fresh;
            diag.rn_fresh_bytes += fresh ? ((size < b->size) ? size : b->size) : 0;
        }
    });
    lock();
    if ((how & RN_HOLD) && b->locked && b->kept_on)
        rename_leave(b, b->block, b->drawn, b->fence, how);
    else if (how & RN_HOLD) {
        kept_free(b, (how & RN_SPARE) != 0);
        b->kept_on      = 1;
        b->kept         = b->block;
        b->kept_version = b->version;
        b->kept_size    = b->size;
        b->kept_drawn   = b->drawn;
        b->kept_fence   = b->fence;
    } else
        rename_leave(b, b->block, b->drawn, b->fence, how);
    unlock();
    pool_release_queued();
    b->block         = nb;
    b->size          = size;
    b->drawn         = 0;
    b->version       = pool_version_new();
    b->gbl->fpVidMem = block_linear(nb);
    b->gbl->lPitch   = (LONG) size;
    return 1;
}

/* A buffer in system memory into the pool, `size` bytes, its contents kept
   if `keep`, when a block can be had without waiting for the GPU; a try that
   finds no room keeps every buffer from trying again for GROW_RETRY_MS. 1
   if it moved. Only in its own process (its memory is that process's). The
   old memory goes now when the buffer may change memory (a Lock, a swap);
   with `hold_old` (outside them) it stays until Destroy, so a pointer the
   runtime kept from an earlier Lock still names memory. */
static int
buffer_to_pool(pbuf_t *b, DWORD size, int keep, int hold_old)
{
    const DWORD now = GetTickCount();
    DWORD       blk;

    if (!b->sys || (b->pid != GetCurrentProcessId()) || (sys_tried_at && (now - sys_tried_at < GROW_RETRY_MS)))
        return 0;
    blk = block_alloc(size, 0);
    if (blk == NONE) {
        sys_tried_at = now ? now : 1;
        return 0;
    }
    if (keep)
        memcpy((void *) block_linear(blk), (const void *) b->mem, (size < b->size) ? size : b->size);
    if (hold_old)
        b->old_mem = b->mem;
    else
        VirtualFree((void *) b->mem, 0, MEM_RELEASE);
    b->sys           = 0;
    b->mem           = 0;
    b->block         = blk;
    b->size          = size;
    b->drawn         = 0;
    b->version       = pool_version_new();
    b->gbl->fpVidMem = block_linear(blk);
    b->gbl->lPitch   = (LONG) size;
    DIAG(diag.buf_sys_back++);
    return 1;
}

void
pool_settle(LPSURF s)
{
    pbuf_t *b = buffer_of(s);

    if (b && b->sys && ((b->kind == PB_VERTEX) || (b->kind == PB_INDEX)) && !b->locked)
        buffer_to_pool(b, b->size, 1, 1);
}

int
pool_write(pbuf_t *b, DWORD offset, const void *src, DWORD n, int how)
{
    if ((offset >= b->size) || !n)
        return 1;
    if (n > b->size - offset)
        n = b->size - offset;
    if (!(how & POOL_WRITE_IN_PLACE) && ((how & POOL_WRITE_BATCH) || buffer_busy(b))) {
        /* Fresh memory, the old contents kept: the old block goes once the
           GPU passed the packets that read it (the open batch's are covered
           by the first fence after them). Only a free block: the caller
           holds the ring lock, so no chunk is made (the mini-VDD's
           POOL_GROW), and chunks this frees are released by the next call
           outside it (its fence may be the open batch's). */
        DWORD nb;

        lock();
        nb = block_take((b->size + BLOCK_ALIGN - 1) & ~(BLOCK_ALIGN - 1));
        unlock();
        if (nb == NONE)
            return 0;
        memcpy((void *) block_linear(nb), (const void *) b->gbl->fpVidMem, b->size);
        lock();
        block_free(b->block, 1, (how & POOL_WRITE_BATCH) ? sb->fence_issued + 1 : b->fence);
        unlock();
        b->block         = nb;
        b->drawn         = 0;
        b->batch         = 0; /* nothing reads the new block yet: a second BUFFERBLT of the call writes it in place */
        b->gbl->fpVidMem = block_linear(nb);
        DIAG(diag.buf_renames++);
    }
    /* BUFFERBLT's source may be the same buffer. */
    memmove((uint8_t *) b->gbl->fpVidMem + offset, src, n);
    b->version = pool_version_new();
    return 1;
}

/* A pool buffer into fresh system memory of `size` bytes (a swap's), when
   the pool cannot give it that much: the DDK has a buffer too large for the
   driver's memory allocated there. Its block goes once the GPU has read it.
   Only in its own process. */
static int
buffer_to_sys(pbuf_t *b, DWORD size)
{
    void *mem;

    if (b->pid != GetCurrentProcessId())
        return 0;
    mem = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!mem)
        return 0;
    lock();
    block_free(b->block, b->drawn, b->fence);
    unlock();
    pool_release_queued();
    b->block         = NONE;
    b->sys           = 1;
    b->mem           = (DWORD) mem;
    b->size          = size;
    b->drawn         = 0;
    b->version       = pool_version_new();
    b->gbl->fpVidMem = (DWORD) mem;
    b->gbl->lPitch   = (LONG) size;
    DIAG(diag.buf_sysmem++);
    return 1;
}

/* A buffer in system memory, larger: fresh memory of `size` bytes (a
   swap's; the GPU never read the old). Only in its own process. */
static int
buffer_sys_grow(pbuf_t *b, DWORD size)
{
    void *mem;

    if (b->pid != GetCurrentProcessId())
        return 0;
    mem = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!mem)
        return 0;
    VirtualFree((void *) b->mem, 0, MEM_RELEASE);
    b->mem           = (DWORD) mem;
    b->size          = size;
    b->version       = pool_version_new();
    b->gbl->fpVidMem = (DWORD) mem;
    b->gbl->lPitch   = (LONG) size;
    return 1;
}

int
pool_swap(LPSURF s, DWORD size)
{
    pbuf_t *b = buffer_of(s);
    int     busy;

    if (!b || (b->kind != PB_IMPLICIT))
        return 1;
    if (size < b->size)
        size = b->size;
    if (b->sys) {
        /* Its vertices went through the ring: into the pool if it has
           room, else larger if that was asked for. */
        if (buffer_to_pool(b, size, 0, 0) || (size == b->size))
            return 1;
        return buffer_sys_grow(b, size);
    }
    busy = buffer_busy(b);
    if (!busy && (size == b->size))
        return 1;
    if (buffer_rename(b, size, RN_WAIT)) {
        DIAG(diag.buf_swaps++);
        return 1;
    }
    if ((size > b->size) && buffer_to_sys(b, size))
        return 1;
    /* No fresh memory: the runtime may write the buffer again as soon as
       DrawPrimitives2 returns. */
    if (busy) {
        gpu_wait(b->fence);
        b->drawn = 0;
    }
    return size == b->size;
}

/* --- The execute-buffer callbacks ------------------------------------------------- */

/* The kind of buffer the surface is. */
static DWORD
exebuf_kind(LPSURF s)
{
    const DWORD caps2 = s->lpSurfMore ? s->lpSurfMore->ddsCapsEx.dwCaps2 : 0;

    if (caps2 & DDSCAPS2_COMMANDBUFFER)
        return PB_COMMAND;
    if (caps2 & DDSCAPS2_INDEXBUFFER)
        return PB_INDEX; /* DirectX 8: an application's index buffer, locked as its vertex buffers are */
    return (caps2 & DDSCAPS2_VERTEXBUFFER) ? PB_VERTEX : PB_IMPLICIT;
}

/* Execute buffers the application put in system memory itself are
   DirectDraw's; all others are the DLL's, in the pool or, when it cannot
   hold them, in system memory. */
GUARDED(static, CanCreateExecuteBuffer, DDHAL_CANCREATESURFACEDATA, ddRVal)
{
    const DDSURFACEDESC *desc = d->lpDDSurfaceDesc;

    if (desc->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY)
        return DDHAL_DRIVER_NOTHANDLED;
    d->ddRVal = (!(desc->dwFlags & DDSD_WIDTH) || !desc->dwWidth) ? DDERR_INVALIDPARAMS : DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

/* A global record keeps nothing of a buffer gone. */
static void
buffer_record_clear(DDRAWI_DDRAWSURFACE_GBL *g)
{
    g->dwReserved1 = 0;
    g->fpVidMem    = 0;
    g->dwGlobalFlags &= ~DDRAWISURFGBL_SYSMEMEXECUTEBUFFER;
}

/* A buffer's memory goes (in the pool once no queued packet reads it), and
   the surface keeps nothing of it. A buffer in system memory is its
   process's: destroyed in another (DirectDraw cleaning up after a process
   that ended), its memory went with the process. */
static void
buffer_destroy(pbuf_t *b)
{
    DDRAWI_DDRAWSURFACE_GBL *gbl = b->gbl;
    const DWORD              pid = b->pid;

    if (b->old_mem && (b->pid == GetCurrentProcessId()))
        VirtualFree((void *) b->old_mem, 0, MEM_RELEASE);
    b->old_mem = 0;
    /* The record cleared before the buffer's slot is free (an adopted
       buffer's is in the slot, which another process may take at once). */
    if (b->sys) {
        if (b->pid == GetCurrentProcessId())
            VirtualFree((void *) b->mem, 0, MEM_RELEASE);
        lock();
        kept_free(b, 0);
        spares_free(b);
        buffer_record_clear(gbl);
        b->gbl    = NULL;
        b->ddl    = NULL;
        b->handle = 0;
        if ((DWORD) (b - bufs) < bufs_low)
            bufs_low = (DWORD) (b - bufs);
        unlock();
        pool_release_queued();
    } else {
        lock();
        kept_free(b, 0);
        spares_free(b);
        block_free(b->block, b->drawn, b->fence);
        buffer_record_clear(gbl);
        b->gbl    = NULL;
        b->ddl    = NULL;
        b->handle = 0;
        if ((DWORD) (b - bufs) < bufs_low)
            bufs_low = (DWORD) (b - bufs);
        unlock();
        pool_release_queued();
    }
    /* Destroyed in another process: DirectDraw cleaning up after one that
       ended, whose contexts may never be destroyed (ctx_reap). */
    if (pid != GetCurrentProcessId())
        ctx_reap(pid);
}

GUARDED(static, CreateExecuteBuffer, DDHAL_CREATESURFACEDATA, ddRVal)
{
    const DDSURFACEDESC *desc = d->lpDDSurfaceDesc;
    const DWORD          size = desc->dwWidth;
    DWORD                i;

    flight_figures(size, (d->dwSCnt && d->lplpSList[0]->lpSurfMore) ? d->lplpSList[0]->lpSurfMore->ddsCapsEx.dwCaps2 : 0);
    if (desc->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) {
        if (d->dwSCnt && TRACE(TRACE_SURFACE))
            dbg(DLL_TAG " trace: CreateExecuteBuffer %s, %u bytes, in system memory: left to DirectDraw", trace_kind(d->lplpSList[0]),
                size);
        return DDHAL_DRIVER_NOTHANDLED;
    }
    d->ddRVal = DD_OK;
    for (i = 0; i < d->dwSCnt; i++) {
        LPSURF                   s   = d->lplpSList[i];
        DDRAWI_DDRAWSURFACE_GBL *gbl = s->lpGbl;
        pbuf_t                  *b   = NULL;
        DWORD                    blk = NONE;
        void                    *mem = NULL;
        DWORD                    k;

        if (size) {
            blk = block_alloc(size, 1);
            if (blk == NONE)
                mem = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        }
        if ((blk != NONE) || mem) {
            lock();
            for (k = bufs_low; k < POOL_BUFFERS; k++) {
                if (!bufs[k].gbl) {
                    b         = &bufs[k];
                    b->gbl    = gbl;
                    b->ddl    = NULL;
                    b->handle = 0;
                    break;
                }
            }
            bufs_low = k + (k < POOL_BUFFERS);
            if (!b && (blk != NONE))
                block_free(blk, 0, 0);
            unlock();
            pool_release_queued();
            if (!b && mem)
                VirtualFree(mem, 0, MEM_RELEASE);
        }
        if (!b) {
            DIAG(diag.buf_refused += (size != 0));
            /* None of this request's buffers stays. */
            while (i--) {
                b = buffer_of(d->lplpSList[i]);
                if (b)
                    buffer_destroy(b);
            }
            d->ddRVal = (size ? DDERR_OUTOFMEMORY : DDERR_INVALIDPARAMS);
            if (TRACE(TRACE_SURFACE))
                dbg(DLL_TAG " trace: CreateExecuteBuffer %s, %u bytes: refused (%x)", trace_kind(s), size, d->ddRVal);
            return DDHAL_DRIVER_HANDLED;
        }
        DIAG(diag.buf_sysmem += (mem != NULL));
        b->block      = blk;
        b->sys        = (mem != NULL);
        b->mem        = (DWORD) mem;
        b->old_mem    = 0;
        b->pid        = GetCurrentProcessId();
        b->size       = size;
        b->kind       = exebuf_kind(s);
        b->version    = pool_version_new();
        b->fence      = 0;
        b->drawn      = 0;
        b->dynamic    = 0;
        b->lock_flags = 0;
        b->locked     = 0;
        b->batch      = 0;
        b->runtime_ib = 0;
        b->rt_gen     = diag_on ? pool_version_new() : 0; /* a new buffer: no window of another's is its */
        b->rt_noow    = 0;
        b->tt_since     = 0;
        b->kept_on      = 0;
        b->drawn_locked = 0;
        b->n_spare      = 0;
        b->tt_id        = 0;
        if (diag_textures) {
            b->tt_id = pool_version_new() & 0x00FFFFFF; /* the audit's: no two buffers alive share one (in practice) */
            if (!b->tt_id)
                b->tt_id = 1;
        }
        gbl->dwReserved1 = (DWORD) (b - bufs) + 1;
        gbl->fpVidMem    = mem ? (DWORD) mem : block_linear(blk);
        gbl->lPitch      = (LONG) size; /* dwLinearSize */
        gbl->dwGlobalFlags |= DDRAWISURFGBL_SYSMEMEXECUTEBUFFER;
        s->dwReserved1 = 0;
        if (TRACE(TRACE_SURFACE))
            dbg(DLL_TAG " trace: CreateExecuteBuffer %s %x, %u bytes: in %s, at %x", trace_kind(s), (DWORD) s, size,
                mem ? "system memory (the pool had no room)" : "the pool", gbl->fpVidMem);
    }
    d->lpDDSurfaceDesc->lPitch = (LONG) size;
    return DDHAL_DRIVER_HANDLED;
}

GUARDED_FIG(static, DestroyExecuteBuffer, DDHAL_DESTROYSURFACEDATA, ddRVal, d->lpDDSurface, 0)
{
    pbuf_t                  *b = buffer_find(d->lpDDSurface, 1);
    DDRAWI_DDRAWSURFACE_GBL *g = d->lpDDSurface->lpGbl;

    if (!b)
        return DDHAL_DRIVER_NOTHANDLED;
    /* A DirectX 8 vertex or index buffer's handle goes with it: an adopted
       buffer's by the buffer (its object, handle and copy), as the call's
       record may name another process's entry under the same object
       address, or no handle at all. */
    if (b->gbl == &b->own)
        d3d7_buffer_gone(b->ddl, b->handle, &b->own);
    else
        d3d7_surface_gone(d->lpDDSurface);
    buffer_destroy(b);
    /* The call's record keeps nothing of it either (an adopted buffer's
       own record was the one buffer_destroy cleared). */
    buffer_record_clear(g);
    d->ddRVal = DD_OK;
    return DDHAL_DRIVER_HANDLED;
}

/* The pointer a Lock hands out: the buffer's memory as it is now (a rename
   just gave it new memory), at the rectangle's start if there is one. */
static DWORD
locked(DDHAL_LOCKDATA *d, pbuf_t *b)
{
    DWORD at = 0;

    if (d->bHasRect && (d->rArea.left > 0) && ((DWORD) d->rArea.left < b->size))
        at = (DWORD) d->rArea.left;
    d->lpSurfData = (void *) (b->gbl->fpVidMem + at);
    /* An adopted buffer: the call's record (the runtime's, valid for the
       call) learns where the buffer is now, as a buffer's record always
       did when renames wrote it. */
    if (d->lpDDSurface->lpGbl != b->gbl) {
        d->lpDDSurface->lpGbl->fpVidMem = b->gbl->fpVidMem;
        d->lpDDSurface->lpGbl->lPitch   = b->gbl->lPitch;
    }
    d->ddRVal     = DD_OK;
    b->locked     = 1;
    b->lock_flags = d->dwFlags; /* only a Lock given: one refused (still drawing) leaves an open one's */
    DIAG({
        diag.buf_locks++; /* given (still-drawing answers are not counted) */
        busy_ok(BUSY_BUFFER_LOCK);
    });
    /* The runtime buffers' count (pool_rt_window): a Lock of the runtime's
       own buffer that would wait or rename, were the GPU reading it in
       place (plain, DISCARDCONTENTS), gives it a new number; NOOVERWRITE
       promises only to leave what queued draws read. */
    DIAG({
        if ((b->kind == PB_IMPLICIT) && !b->sys) {
            if (d->dwFlags & DDLOCK_DISCARDCONTENTS) {
                diag.rt_locks[0]++;
                b->rt_gen = pool_version_new();
            } else if (d->dwFlags & DDLOCK_NOOVERWRITE) {
                diag.rt_locks[1]++;
                b->rt_noow++;
            } else {
                diag.rt_locks[2]++;
                if (!(d->dwFlags & DDLOCK_READONLY))
                    b->rt_gen = pool_version_new();
            }
        }
    });
    /* What is written under the Lock is a new version from the Lock on (and
       again at the Unlock): a draw sent before the Unlock comes (or one
       that never comes) never names the old contents' version. */
    if (!(d->dwFlags & DDLOCK_READONLY))
        b->version = pool_version_new();
    /* The report's Locks given, by kind and flag (busy answers are not). */
    DIAG({
        if ((b->kind == PB_VERTEX) || (b->kind == PB_INDEX)) {
            const int k = b->kind == PB_INDEX;

            diag.lock_kind[k]++;
            diag.lock_flags[k][0] += (d->dwFlags & DDLOCK_DISCARDCONTENTS) != 0;
            diag.lock_flags[k][1] += (d->dwFlags & DDLOCK_NOOVERWRITE) != 0;
            diag.lock_flags[k][2] += (d->dwFlags & DDLOCK_READONLY) != 0;
        }
    });
    return DDHAL_DRIVER_HANDLED;
}

/* The texture audit's record of a Lock given:
   the buffer's Locks since its last VSUM note, which the note carries
   (dp2.c): how each was given (with no draw that read the buffer pending,
   while one was without a wait or new memory, after a wait, with new
   memory; a READONLY one writes nothing). LOCK_BUSY_HERE is only
   lock_exebuf's: the buffer was busy at the Lock's start. */
#define LOCK_BUSY_HERE 0x00800000

static DWORD
lock_given(DDHAL_LOCKDATA *d, pbuf_t *b, DWORD tl)
{
    if (tl) {
        DWORD n = b->tt_since >> VGPU98_NOTE_LOCK_COUNT_SHIFT;

        if (!(tl & LOCK_BUSY_HERE))
            tl |= VGPU98_NOTE_LOCK_IDLE;
        else if (!(tl & (VGPU98_NOTE_LOCK_WAITED | VGPU98_NOTE_LOCK_RENAMED | DDLOCK_READONLY)))
            tl |= VGPU98_NOTE_LOCK_UNWAITED | ((tl & DDLOCK_NOOVERWRITE) ? VGPU98_NOTE_LOCK_NOOW_BUSY : 0);
        tl &= ~(DWORD) LOCK_BUSY_HERE;
        n           = (n < 255) ? n + 1 : 255;
        b->tt_since = ((b->tt_since | tl) & ~VGPU98_NOTE_LOCK_COUNT) | (n << VGPU98_NOTE_LOCK_COUNT_SHIFT);
    }
    return locked(d, b);
}

/* The CPU must not write what a queued packet still reads: a Lock waits for
   the buffer's last batch, unless the caller promises to write nothing a
   queued draw reads (NOOVERWRITE), only reads (the GPU never writes the
   pool), or drops the contents (DISCARDCONTENTS: fresh memory, the old
   freed once read). A DirectX 8 vertex or index buffer (surf_dx8) of up to
   LOCK_COPY_MAX, not locked already, gets fresh memory with its contents
   copied instead of a wait, when a block can be had at once.
   A DirectX 6 or 7 application vertex buffer
   (pre_lock) keeps a rename's old block until the Unlock,
   and once drawn while
   locked gets a copy at its plain write Locks, busy or not. No other idle
   Lock renames; a rename takes the buffer's own spare first.
   Command buffers only the DLL reads, and buffers in system memory are read
   before DrawPrimitives2 returns. */
static DWORD
lock_exebuf(DDHAL_LOCKDATA *d)
{
    pbuf_t     *b     = buffer_of(d->lpDDSurface);
    const DWORD flags = d->dwFlags;
    DWORD       tl    = 0; /* the texture audit's record of this Lock (lock_given) */
    int         pre_lock;  /* a DirectX 6 or 7 application vertex buffer: a rename keeps its old block */

    if (!b)
        return DDHAL_DRIVER_NOTHANDLED;
    pre_lock = (b->kind == PB_VERTEX) && !surf_dx8(d->lpDDSurface);
    if (diag_audit8)
        (void) surf_own(d->lpDDSurface); /* the record self-check: an adopted buffer's copy against the runtime's record */
    if (((b->kind == PB_VERTEX) || (b->kind == PB_INDEX)) && TRACE(TRACE_LOCK))
        dbg(DLL_TAG " trace: Lock of %s %x (handle %x), flags %x, %u bytes at %x%s", trace_kind(d->lpDDSurface), (DWORD) d->lpDDSurface,
            d->lpDDSurface->lpSurfMore ? d->lpDDSurface->lpSurfMore->dwSurfaceHandle : 0, flags, b->size, b->gbl->fpVidMem,
            b->sys ? " (system memory)" : "");
    if (flags & (DDLOCK_NOOVERWRITE | DDLOCK_DISCARDCONTENTS))
        b->dynamic = 1;
    /* An application's buffer in system memory may get memory in the pool
       now: a Lock may hand out a new pointer. Its contents go with it, even
       for DISCARDCONTENTS: draws during the Lock read them (DirectX 6 and
       7), and a fresh block never shows another buffer's bytes. */
    if (b->sys && ((b->kind == PB_VERTEX) || (b->kind == PB_INDEX)))
        buffer_to_pool(b, b->size, 1, 0);
    /* The texture audit's record of the Lock, kept once it is given
       (lock_given), for the next VSUM note of the buffer (dp2.c). */
    if (diag_textures && (b->kind != PB_COMMAND))
        tl = VGPU98_NOTE_LOCK_SEEN | (flags & VGPU98_NOTE_LOCK_FLAGS) | (buffer_busy(b) ? LOCK_BUSY_HERE : 0);
    /* A plain write Lock of a DirectX 6 or 7 application vertex buffer once
       drawn while locked, busy or not: a copy (the buffer's own spare
       first), its old block kept as the contents from before the Lock
       however late the runtime sends the draws issued before it.
       Up to LOCK_COPY_MAX, a
       block at once, and not while an earlier Lock's pointer may still be
       written. */
    if (pre_lock && b->drawn_locked && !b->sys && !b->locked && (b->size <= LOCK_COPY_MAX) &&
        !(flags & (DDLOCK_READONLY | DDLOCK_NOOVERWRITE | DDLOCK_DISCARDCONTENTS)) &&
        buffer_rename(b, b->size, RN_KEEP | RN_SPARE | RN_HOLD)) {
        DIAG(diag.buf_renames++);
        if (TRACE(TRACE_LOCK))
            dbg(DLL_TAG " trace: Lock of buffer %x: drawn while locked before, copied to %x", (DWORD) d->lpDDSurface,
                b->gbl->fpVidMem);
        return lock_given(d, b, tl ? tl | VGPU98_NOTE_LOCK_RENAMED : 0);
    }
    /* Any other idle Lock keeps the buffer's memory, an idle
       DISCARDCONTENTS one too: draws that reach the DLL during it read the
       contents from before it through the ring (dp2.c). */
    if (b->sys || (b->kind == PB_COMMAND) || (flags & (DDLOCK_READONLY | DDLOCK_NOOVERWRITE)) || !buffer_busy(b))
        return lock_given(d, b, tl);
    /* A busy DISCARDCONTENTS Lock: new memory, the buffer's own spare first.
       A DirectX 6 or 7 application vertex buffer keeps its old block until
       the Unlock: draws issued before the Lock may reach the DLL during it. */
    if ((flags & DDLOCK_DISCARDCONTENTS) &&
        buffer_rename(b, b->size, RN_WAIT | RN_SPARE | (pre_lock ? RN_HOLD : 0))) {
        DIAG(diag.buf_renames++);
        return lock_given(d, b, tl ? tl | VGPU98_NOTE_LOCK_RENAMED : 0);
    }
    /* A busy plain Lock of a DirectX 8 vertex or index buffer of up to
       LOCK_COPY_MAX: a copy instead of the wait (the buffer's own spare
       first). Not while an earlier Lock's pointer may still be written. */
    if (!(flags & DDLOCK_DISCARDCONTENTS) && (b->size <= LOCK_COPY_MAX) && !b->locked &&
        ((b->kind == PB_INDEX) || (b->kind == PB_VERTEX)) && surf_dx8(d->lpDDSurface) &&
        buffer_rename(b, b->size, RN_KEEP | RN_SPARE)) {
        DIAG({
            diag.lock_copies[b->kind == PB_INDEX]++;
            diag.lock_copy_bytes[b->kind == PB_INDEX] += b->size;
        });
        if (TRACE(TRACE_LOCK))
            dbg(DLL_TAG " trace: Lock of buffer %x (handle %x): in use, copied to %x", (DWORD) d->lpDDSurface,
                d->lpDDSurface->lpSurfMore ? d->lpDDSurface->lpSurfMore->dwSurfaceHandle : 0, b->gbl->fpVidMem);
        return lock_given(d, b, tl ? tl | VGPU98_NOTE_LOCK_RENAMED : 0);
    }
    if (!(flags & DDLOCK_WAIT)) {
        gpu_kick();
        if (buffer_busy(b)) {
            DIAG(busy_answer(BUSY_BUFFER_LOCK));
            d->ddRVal = DDERR_WASSTILLDRAWING;
            return DDHAL_DRIVER_HANDLED;
        }
        return lock_given(d, b, tl & ~LOCK_BUSY_HERE);
    }
    /* Done waiting, or the GPU is held: a hold drops what was queued, so
       nothing reads the buffer either way. (gpu_wait counts its time.) */
    gpu_wait(b->fence);
    DIAG({
        diag.buf_waits++;
        if ((b->kind == PB_VERTEX) || (b->kind == PB_INDEX))
            diag.lock_waits[b->kind == PB_INDEX]++;
    });
    b->drawn = 0;
    return lock_given(d, b, tl ? tl | VGPU98_NOTE_LOCK_WAITED : 0);
}

/* Its time goes into the report's time inside Lock, waits and copies
   included. */
GUARDED_FIG(static, LockExecuteBuffer, DDHAL_LOCKDATA, ddRVal, d->lpDDSurface, d->lpSurfData)
{
    const uint64_t t0 = diag_on ? rdtsc() : 0;
    const DWORD    r  = lock_exebuf(d);

    DIAG(diag.cyc_lock += rdtsc() - t0);
    return r;
}

/* Contents written under the Lock are a new version. */
GUARDED_FIG(static, UnlockExecuteBuffer, DDHAL_UNLOCKDATA, ddRVal, d->lpDDSurface, 0)
{
    pbuf_t *b = buffer_of(d->lpDDSurface);

    if (!b)
        return DDHAL_DRIVER_NOTHANDLED;
    if (!(b->lock_flags & DDLOCK_READONLY))
        b->version = pool_version_new();
    b->lock_flags = 0;
    b->locked     = 0;
    /* The contents from before the Lock go once the draws that read them
       are done. */
    if (b->kept_on) {
        lock();
        kept_free(b, 1); /* one of the buffer's spares */
        unlock();
        pool_release_queued();
    }
    return DDHAL_DRIVER_NOTHANDLED;
}

void
pool_fill_callbacks(void)
{
    DDHAL_DDEXEBUFCALLBACKS *cb = &sb->cb_exebuf;

    memset(cb, 0, sizeof(*cb));
    cb->dwSize = sizeof(*cb);
    if (!pool_on)
        return;
    cb->dwFlags = DDHAL_EXEBUFCB32_CANCREATEEXEBUF | DDHAL_EXEBUFCB32_CREATEEXEBUF | DDHAL_EXEBUFCB32_DESTROYEXEBUF |
                  DDHAL_EXEBUFCB32_LOCKEXEBUF | DDHAL_EXEBUFCB32_UNLOCKEXEBUF;
    cb->CanCreateExecuteBuffer = (DWORD) CanCreateExecuteBuffer;
    cb->CreateExecuteBuffer    = (DWORD) CreateExecuteBuffer;
    cb->DestroyExecuteBuffer   = (DWORD) DestroyExecuteBuffer;
    cb->LockExecuteBuffer      = (DWORD) LockExecuteBuffer;
    cb->UnlockExecuteBuffer    = (DWORD) UnlockExecuteBuffer;
}

/* --- Starting ------------------------------------------------------------------ */

void
pool_init(DWORD profile)
{
    DWORD q[3];
    DWORD i;

    if (pool_inited)
        return;
    pool_inited = 1;
    for (i = 0; i < POOL_BLOCKS; i++) {
        blocks[i].chunk  = NONE;
        blocks[i].next   = (i + 1 < POOL_BLOCKS) ? i + 1 : NONE;
        blocks[i].listed = 0;
    }
    block_unused = 0;
    free_head    = NONE;
    if (!(profile & VGPU98_PROFILE_GPUVM)) {
        dbg_event(DLL_TAG ": no pool: the GPU does not read main memory");
        return;
    }
    if (!ioctl(POOL_QUERY, NULL, 0, q, sizeof(q))) {
        dbg_event(DLL_TAG ": no pool: the mini-VDD does not answer (\\\\.\\VGPU98)");
        return;
    }
    if (!q[0]) {
        dbg_event(DLL_TAG ": no pool: the mini-VDD has none (no page table for the GPU)");
        return;
    }
    pool_cap = (q[0] > 0xFFFFF) ? 0xFFFFF000u : q[0] << 12;
    pool_on  = 1;
    dbg(DLL_TAG ": pool of up to %u KB in main memory (%u KB mapped already)", pool_cap >> 10, q[1] << 2);
}

void
pool_figures(DWORD *kb, DWORD *cap_kb, DWORD *nchunks, DWORD count[PB_KINDS + 1], DWORD kb_by[PB_KINDS + 1])
{
    DWORD i;

    *kb      = pool_bytes >> 10;
    *cap_kb  = pool_cap >> 10;
    *nchunks = 0;
    for (i = 0; i < POOL_CHUNKS; i++)
        *nchunks += chunks[i].linear != 0;
    for (i = 0; i <= PB_KINDS; i++)
        count[i] = kb_by[i] = 0;
    for (i = 0; i < POOL_BUFFERS; i++) {
        if (bufs[i].gbl && (bufs[i].kind < PB_KINDS)) {
            const DWORD at = bufs[i].sys ? PB_KINDS : bufs[i].kind;

            count[at]++;
            kb_by[at] += bufs[i].size >> 10;
        }
    }
}

/* The chunks without a buffer (and their kilobytes) now: waiting out
   POOL_IDLE_MS, kept in the reserve, or with free blocks the GPU may still
   read (the report's). Read without
   the pool's lock, as pool_figures reads: the report runs under the ring
   lock, and a count needs no lock order of its own. */
void
pool_chunk_figures(DWORD *free_chunks, DWORD *free_kb)
{
    DWORD i;

    *free_chunks = 0;
    *free_kb     = 0;
    for (i = 0; i < POOL_CHUNKS; i++) {
        if (chunks[i].linear && (chunks[i].linear != CHUNK_CLAIMED) && !chunks[i].busy_bytes) {
            (*free_chunks)++;
            *free_kb += chunks[i].bytes >> 10;
        }
    }
}

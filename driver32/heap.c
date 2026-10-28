/*
 * vgpu98dx.dll: a small heap for the Direct3D state that grows with what a
 * program creates: state blocks, light tables, texture palettes, and DirectX
 * 8's surface records (d3d7.c). It lives in
 * the DLL's data, which every process shares, under its own lock; what a
 * process allocates only its own contexts and surfaces use, and they give it
 * back when they go (ContextDestroy, DestroyDDLocal, ContextDestroyAll).
 *
 * The allocator is heapcore.c's: size-class free lists over blocks with a
 * 16-byte header, a freed block merging with free neighbours on both
 * sides, so an allocation costs the same however many blocks the heap holds
 * (first fit from a low mark before,
 * whose walk grew with the blocks).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3d.h"
#include "heapcore.h"

#define HEAP_BYTES (24u << 20) /* DirectX 8's palettes, shaders, state blocks and surface records too */

static uint8_t heap[HEAP_BYTES] __attribute__((aligned(16)));
static hc_t    hc;
static int     heap_ready;
static dlock_t heap_lock;
static DWORD   heap_refused_told; /* "no bytes left" lines since the last context was made */

void
heap_lines_again(void)
{
    __atomic_store_n(&heap_refused_told, 0, __ATOMIC_RELAXED);
}

void *
heap_alloc(DWORD bytes)
{
    void          *p;
    const uint64_t t0 = diag_on ? rdtsc() : 0;

    if (!bytes || (bytes > HEAP_BYTES))
        return NULL;
    dlock_take(&heap_lock, "heap");
    if (!heap_ready) {
        hc_init(&hc, heap, HEAP_BYTES);
        heap_ready = 1;
    }
    p = hc_alloc(&hc, bytes);
    dlock_give(&heap_lock);
    DIAG({
        diag.heap_allocs++;
        diag.cyc_heap += rdtsc() - t0;
    });
    /* The first three lines from each context's creation on
       (heap_lines_again); the rest counted in the report. */
    if (!p) {
        DIAG(diag.heap_refused++);
        if (__atomic_fetch_add(&heap_refused_told, 1, __ATOMIC_RELAXED) < 3)
            dbg_event_own(DLL_TAG ": the DLL's heap has no %u bytes left (more counted in the report's heap)", bytes);
    }
    return p;
}

void
heap_free(void *p)
{
    if (!p || !heap_owns(p))
        return;
    dlock_take(&heap_lock, "heap");
    hc_free(&hc, p);
    dlock_give(&heap_lock);
}

int
heap_owns(const void *p)
{
    return (DWORD) ((const uint8_t *) p - heap) < HEAP_BYTES;
}

/* A larger copy of a block (its first `old` bytes kept), the old freed; NULL
   (and the old kept) if there is no room. */
void *
heap_grow(void *p, DWORD old, DWORD bytes)
{
    void *n = heap_alloc(bytes);

    if (!n)
        return NULL;
    if (p) {
        memcpy(n, p, old);
        heap_free(p);
    }
    return n;
}

/* The heap's figures for the report (under its lock). */
void
heap_figures(DWORD *used_blocks, DWORD *used_kb, DWORD *free_blocks, DWORD *largest_kb)
{
    hc_figures_t f = { 0 };

    dlock_take(&heap_lock, "heap");
    if (heap_ready)
        hc_figures(&hc, &f);
    else
        f.largest_free = HEAP_BYTES;
    dlock_give(&heap_lock);
    *used_blocks = f.used_blocks;
    *used_kb     = f.used_bytes >> 10;
    *free_blocks = f.free_blocks;
    *largest_kb  = f.largest_free >> 10;
}

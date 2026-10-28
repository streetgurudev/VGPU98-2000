/*
 * The DLL's heap allocator (driver32/heapcore.c) on the host: what it
 * refuses, random allocations and frees with the heap checked whole as
 * they go and every block's bytes its own until it is freed, a double free
 * ignored, exhaustion, every block freed back into one, at the DLL's 24 MB
 * and a smaller heap; a hole of a request's size used again, and a request
 * off a size step that only a later block of its list fits.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "heapcore.h"

#define SMALL_SIZE  (4u << 20)
#define DLL_SIZE    (24u << 20) /* the DLL's HEAP_BYTES */
#define LIVE_MAX    32768
#define OPS         200000
#define CHECK_EVERY 997

static uint8_t  small_heap[SMALL_SIZE] __attribute__((aligned(16)));
static uint8_t  dll_heap[DLL_SIZE] __attribute__((aligned(16)));
static uint8_t  tiny_heap[2224] __attribute__((aligned(16)));
static uint8_t  elsewhere[64] __attribute__((aligned(16))); /* memory outside the heaps */
static uint32_t rng = 0x2026u;

typedef struct live {
    uint8_t *p;
    uint32_t n;
    uint8_t  fill;
} live_t;

static live_t   live[LIVE_MAX];
static uint32_t n_live;
static uint8_t *heap_lo;
static uint32_t heap_bytes;

static uint32_t
rnd(void)
{
    rng = rng * 1664525u + 1013904223u;
    return rng >> 8;
}

/* Mostly what the DLL asks for (surface records, palettes, light tables),
   now and then a large block. */
static uint32_t
size_pick(void)
{
    const uint32_t r = rnd() % 100;

    if (r < 60)
        return 1 + rnd() % 256;
    if (r < 90)
        return 1 + rnd() % 4096;
    if (r < 99)
        return 1 + rnd() % 65536;
    return 1 + rnd() % (512u << 10);
}

static void
fill(const live_t *l)
{
    uint32_t i;

    for (i = 0; i < l->n; i++)
        l->p[i] = (uint8_t) (l->fill + i);
}

static int
intact(const live_t *l)
{
    uint32_t i;

    for (i = 0; i < l->n; i++) {
        if (l->p[i] != (uint8_t) (l->fill + i))
            return 0;
    }
    return 1;
}

static int
whole(const hc_t *h)
{
    const char *why;

    if (hc_check(h, &why))
        return 1;
    fprintf(stderr, "test_heap: the heap is not whole: %s\n", why);
    return 0;
}

/* A block of n bytes into the live list; 0 if the heap had none. */
static int
take(hc_t *h, uint32_t n)
{
    live_t l;

    l.n = n;
    l.p = (uint8_t *) hc_alloc(h, n);
    if (!l.p)
        return 0;
    CHECK(((uintptr_t) l.p & 15) == 0);
    CHECK((l.p >= heap_lo) && (l.p + l.n <= heap_lo + heap_bytes));
    l.fill = (uint8_t) rnd();
    fill(&l);
    live[n_live++] = l;
    return 1;
}

static void
give(hc_t *h, uint32_t k)
{
    CHECK(intact(&live[k]));
    hc_free(h, live[k].p);
    live[k] = live[--n_live];
}

/* Random churn, a double free, every block back, then exhaustion by 1024-
   byte blocks (1000 and the header) with every other freed, over a heap of
   `size` bytes at `base`. */
static void
run(uint8_t *base, uint32_t size)
{
    hc_t         h;
    hc_figures_t f;
    uint32_t     i;
    uint32_t     fails = 0;

    heap_lo    = base;
    heap_bytes = size;
    n_live     = 0;
    hc_init(&h, base, size);
    CHECK(whole(&h));
    CHECK((h.free_blocks == 1) && (h.used_blocks == 0));
    CHECK(hc_alloc(&h, 0) == NULL);
    CHECK(hc_alloc(&h, size) == NULL); /* its header does not fit */
    CHECK(hc_alloc(&h, 0xFFFFFFFFu) == NULL);
    hc_free(&h, NULL);
    hc_free(&h, base + 3);
    hc_free(&h, elsewhere + 16);
    CHECK(whole(&h));

    /* The largest block there is, then nothing. */
    {
        void *p = hc_alloc(&h, size - 16);

        CHECK(p != NULL);
        CHECK(hc_alloc(&h, 1) == NULL);
        hc_free(&h, p);
        CHECK(whole(&h) && (h.free_blocks == 1));
    }

    for (i = 0; i < OPS; i++) {
        if (n_live && ((n_live == LIVE_MAX) || (rnd() % 100 < 45)))
            give(&h, rnd() % n_live);
        else
            (void) take(&h, size_pick());
        if (!(i % CHECK_EVERY) && !whole(&h) && (++fails > 3))
            break;
    }
    CHECK(!fails);
    CHECK(whole(&h));
    hc_figures(&h, &f);
    CHECK(f.used_blocks == n_live);
    CHECK(f.free_bytes == size - f.used_bytes);
    CHECK(f.largest_free <= f.free_bytes);

    if (n_live) {
        const uint32_t used = h.used_blocks;

        hc_free(&h, live[n_live - 1].p);
        hc_free(&h, live[n_live - 1].p);
        CHECK(h.used_blocks == used - 1);
        n_live--;
        CHECK(whole(&h));
    }
    while (n_live)
        give(&h, rnd() % n_live);
    CHECK(whole(&h));
    CHECK((h.free_blocks == 1) && (h.used_blocks == 0) && (h.used_bytes == 0));

    /* Exhaustion: blocks of 1024 bytes fill the heap in address order from
       one free block; every other freed, a hole takes 1000 bytes again
       (the same address) but none 1500; then all back. */
    while ((n_live < LIVE_MAX) && take(&h, 1000))
        ;
    CHECK(n_live == size / 1024);
    CHECK(hc_alloc(&h, 1) == NULL);
    CHECK(whole(&h));
    for (i = 0; i < n_live; i += 2) {
        CHECK(intact(&live[i]));
        hc_free(&h, live[i].p);
    }
    CHECK(whole(&h));
    CHECK(h.free_blocks == n_live / 2);
    CHECK(hc_alloc(&h, 1500) == NULL);
    {
        uint8_t *p = (uint8_t *) hc_alloc(&h, 1000);

        CHECK(p == live[n_live - 2].p); /* the last hole freed, its list's first */
        hc_free(&h, p);
    }
    for (i = 1; i < n_live; i += 2) {
        CHECK(intact(&live[i]));
        hc_free(&h, live[i].p);
    }
    n_live = 0;
    CHECK(whole(&h));
    CHECK((h.free_blocks == 1) && (h.used_bytes == 0));
    hc_figures(&h, &f);
    CHECK(f.largest_free == size);
}

int
main(void)
{
    run(small_heap, SMALL_SIZE);
    run(dll_heap, DLL_SIZE);

    /* Off a size step: blocks of 1040, 32, 1120 and 32 bytes fill 2224, the
       1120 then the 1040 freed (the 1040 first on their list, which holds
       1024-1151). A request of 1100 bytes needs 1120: the list's first block
       is short, no list beyond holds any, so the list is walked to the
       1120-byte hole. */
    {
        hc_t     h;
        uint8_t *a, *b, *c, *d, *p;

        hc_init(&h, tiny_heap, sizeof(tiny_heap));
        a = (uint8_t *) hc_alloc(&h, 1024);
        b = (uint8_t *) hc_alloc(&h, 16);
        c = (uint8_t *) hc_alloc(&h, 1104);
        d = (uint8_t *) hc_alloc(&h, 16);
        CHECK(a && b && c && d);
        CHECK(hc_alloc(&h, 1) == NULL);
        hc_free(&h, c);
        hc_free(&h, a);
        CHECK(whole(&h));
        p = (uint8_t *) hc_alloc(&h, 1100);
        CHECK(p == c);
        CHECK(hc_alloc(&h, 1100) == NULL);
        p = (uint8_t *) hc_alloc(&h, 1020);
        CHECK(p == a);
        CHECK(whole(&h));
    }

    CHECK_DONE();
}

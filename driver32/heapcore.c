/*
 * vgpu98dx.dll: the heap's allocator, two-level size-class free lists over
 * boundary-tagged blocks (heapcore.h).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stddef.h>
#include <stdint.h>
#include "heapcore.h"

typedef struct hc_hdr {
    uint32_t size; /* bytes, header included */
    uint32_t used;
    uint32_t prev; /* the size of the block before, 0 for the first */
    uint32_t pad;
} hc_hdr_t;

/* A free block's list links, after its header (offsets, HC_NONE none). */
typedef struct hc_links {
    uint32_t next;
    uint32_t prev;
} hc_links_t;

static hc_hdr_t *
hdr(const hc_t *h, uint32_t off)
{
    return (hc_hdr_t *) (h->base + off);
}

static hc_links_t *
links(const hc_t *h, uint32_t off)
{
    return (hc_links_t *) (h->base + off + sizeof(hc_hdr_t));
}

/* The highest bit set (v not 0). */
static int
top_bit(uint32_t v)
{
    return 31 - __builtin_clz(v);
}

/* The list a free block of `size` bytes (HC_MIN or more) goes on: its power
   of two, then the next three bits. */
static uint32_t
list_of(uint32_t size)
{
    const int fl = top_bit(size);

    return (uint32_t) (fl - HC_FL_MIN) * HC_SL + ((size >> (fl - 3)) & (HC_SL - 1));
}

/* The first list whose every block is `size` bytes or more: the size rounded
   up to the next step of its power of two (HC_LISTS when past the last). */
static uint32_t
list_fit(uint32_t size)
{
    const uint64_t r = (uint64_t) size + (1u << (top_bit(size) - 3)) - 1;

    return (r > 0xFFFFFFFFu) ? HC_LISTS : list_of((uint32_t) r);
}

static void
map_set(hc_t *h, uint32_t i)
{
    h->map[i >> 5] |= 1u << (i & 31);
}

static void
map_clear(hc_t *h, uint32_t i)
{
    h->map[i >> 5] &= ~(1u << (i & 31));
}

/* The first list from i on that holds a block; HC_LISTS none. */
static uint32_t
map_find(const hc_t *h, uint32_t i)
{
    uint32_t w;
    uint32_t bits;

    if (i >= HC_LISTS)
        return HC_LISTS;
    w    = i >> 5;
    bits = h->map[w] & (~0u << (i & 31));
    while (!bits) {
        if (++w >= HC_MAP)
            return HC_LISTS;
        bits = h->map[w];
    }
    i = (w << 5) + (uint32_t) __builtin_ctz(bits);
    return (i < HC_LISTS) ? i : HC_LISTS;
}

/* A free block onto the front of its size's list. */
static void
list_add(hc_t *h, uint32_t off)
{
    const uint32_t i = list_of(hdr(h, off)->size);
    hc_links_t    *l = links(h, off);

    l->prev = HC_NONE;
    l->next = h->head[i];
    if (h->head[i] != HC_NONE)
        links(h, h->head[i])->prev = off;
    h->head[i] = off;
    map_set(h, i);
    h->free_blocks++;
}

/* A free block off its list (before its size changes). */
static void
list_remove(hc_t *h, uint32_t off)
{
    const uint32_t i = list_of(hdr(h, off)->size);
    hc_links_t    *l = links(h, off);

    if (l->prev != HC_NONE)
        links(h, l->prev)->next = l->next;
    else
        h->head[i] = l->next;
    if (l->next != HC_NONE)
        links(h, l->next)->prev = l->prev;
    if (h->head[i] == HC_NONE)
        map_clear(h, i);
    h->free_blocks--;
}

void
hc_init(hc_t *h, void *base, uint32_t size)
{
    uint32_t i;

    h->base = (uint8_t *) base;
    h->size = size & ~(HC_ALIGN - 1);
    for (i = 0; i < HC_LISTS; i++)
        h->head[i] = HC_NONE;
    for (i = 0; i < HC_MAP; i++)
        h->map[i] = 0;
    h->used_blocks = 0;
    h->used_bytes  = 0;
    h->free_blocks = 0;
    if (h->size < HC_MIN) {
        h->size = 0;
        return;
    }
    hdr(h, 0)->size = h->size;
    hdr(h, 0)->used = 0;
    hdr(h, 0)->prev = 0;
    hdr(h, 0)->pad  = 0;
    list_add(h, 0);
}

void *
hc_alloc(hc_t *h, uint32_t bytes)
{
    uint64_t  need64 = ((uint64_t) bytes + sizeof(hc_hdr_t) + HC_ALIGN - 1) & ~(uint64_t) (HC_ALIGN - 1);
    uint32_t  need;
    uint32_t  i;
    uint32_t  off;
    hc_hdr_t *b;

    if (!bytes || (need64 > h->size))
        return NULL;
    need = (uint32_t) need64;
    if (need < HC_MIN)
        need = HC_MIN;
    /* The list of the size itself first, its first block if it fits (a hole
       of this size used again); then the first list whose every block fits;
       then, with none, the size's own list walked for one that does. */
    i   = list_of(need);
    off = h->head[i];
    if ((off == HC_NONE) || (hdr(h, off)->size < need)) {
        const uint32_t j = map_find(h, list_fit(need));

        if (j != HC_LISTS)
            off = h->head[j];
        else {
            for (off = h->head[i]; (off != HC_NONE) && (hdr(h, off)->size < need); off = links(h, off)->next)
                ;
            if (off == HC_NONE)
                return NULL;
        }
    }
    b = hdr(h, off);
    list_remove(h, off);
    /* The rest a block of its own, when it holds one. */
    if (b->size - need >= HC_MIN) {
        const uint32_t rest = off + need;
        hc_hdr_t      *t    = hdr(h, rest);

        t->size = b->size - need;
        t->used = 0;
        t->prev = need;
        t->pad  = 0;
        if (rest + t->size < h->size)
            hdr(h, rest + t->size)->prev = t->size;
        b->size = need;
        list_add(h, rest);
    }
    b->used = 1;
    h->used_blocks++;
    h->used_bytes += b->size;
    return b + 1;
}

void
hc_free(hc_t *h, void *p)
{
    const uintptr_t at = (uintptr_t) p;
    const uintptr_t lo = (uintptr_t) h->base + sizeof(hc_hdr_t);
    uint32_t        off;
    hc_hdr_t       *b;

    if (!p || (at < lo) || (at - lo >= h->size))
        return;
    off = (uint32_t) (at - lo);
    if (off & (HC_ALIGN - 1))
        return;
    b = hdr(h, off);
    if (!b->used)
        return;
    b->used = 0;
    h->used_blocks--;
    h->used_bytes -= b->size;
    /* With the free block after it, then with the free one before. */
    if (off + b->size < h->size) {
        const uint32_t n = off + b->size;

        if (!hdr(h, n)->used) {
            list_remove(h, n);
            b->size += hdr(h, n)->size;
            if (off + b->size < h->size)
                hdr(h, off + b->size)->prev = b->size;
        }
    }
    if (b->prev && !hdr(h, off - b->prev)->used) {
        const uint32_t q = off - b->prev;

        list_remove(h, q);
        hdr(h, q)->size += b->size;
        if (q + hdr(h, q)->size < h->size)
            hdr(h, q + hdr(h, q)->size)->prev = hdr(h, q)->size;
        off = q;
    }
    list_add(h, off);
}

void
hc_figures(const hc_t *h, hc_figures_t *f)
{
    uint32_t i;

    f->used_blocks  = h->used_blocks;
    f->used_bytes   = h->used_bytes;
    f->free_blocks  = h->free_blocks;
    f->free_bytes   = h->size - h->used_bytes;
    f->largest_free = 0;
    for (i = HC_LISTS; i-- > 0;) {
        uint32_t off;

        if (h->head[i] == HC_NONE)
            continue;
        for (off = h->head[i]; off != HC_NONE; off = links(h, off)->next) {
            if (hdr(h, off)->size > f->largest_free)
                f->largest_free = hdr(h, off)->size;
        }
        break;
    }
}

int
hc_check(const hc_t *h, const char **why)
{
    const uint32_t max_blocks = h->size / HC_MIN + 1;
    uint32_t       off        = 0;
    uint32_t       prev_size  = 0;
    int            prev_free  = 0;
    uint32_t       used_blocks = 0, used_bytes = 0, free_blocks = 0, listed = 0;
    uint32_t       i;

    while (off < h->size) {
        const hc_hdr_t *b = hdr(h, off);

        if ((b->size < HC_MIN) || (b->size & (HC_ALIGN - 1)) || (b->size > h->size - off)) {
            *why = "a block's size is not a block's";
            return 0;
        }
        if (b->prev != prev_size) {
            *why = "a header's prev is not the size of the block before";
            return 0;
        }
        if (!b->used && prev_free) {
            *why = "two free blocks touch";
            return 0;
        }
        if (b->used) {
            used_blocks++;
            used_bytes += b->size;
        } else {
            /* On its size's list: with the counts below, every listed block
               is a free block's start, once. */
            uint32_t n = 0;
            uint32_t q;

            for (q = h->head[list_of(b->size)]; (q != HC_NONE) && (q != off) && (q < h->size) && (++n <= max_blocks);
                 q = links(h, q)->next)
                ;
            if (q != off) {
                *why = "a free block is not on its size's list";
                return 0;
            }
            free_blocks++;
        }
        prev_free = !b->used;
        prev_size = b->size;
        off += b->size;
    }
    if (off != h->size) {
        *why = "the blocks do not end at the heap's end";
        return 0;
    }
    for (i = 0; i < HC_LISTS; i++) {
        const int bit = (h->map[i >> 5] >> (i & 31)) & 1;
        uint32_t  back = HC_NONE;
        uint32_t  n    = 0;

        if (bit != (h->head[i] != HC_NONE)) {
            *why = "the bitmap is not the lists";
            return 0;
        }
        for (off = h->head[i]; off != HC_NONE; off = links(h, off)->next) {
            if ((off >= h->size) || (off & (HC_ALIGN - 1)) || hdr(h, off)->used || (list_of(hdr(h, off)->size) != i) ||
                (links(h, off)->prev != back) || (++n > max_blocks)) {
                *why = "a list holds a block that is not its own";
                return 0;
            }
            back = off;
            listed++;
        }
    }
    if (listed != free_blocks) {
        *why = "the lists do not hold every free block once";
        return 0;
    }
    if ((used_blocks != h->used_blocks) || (used_bytes != h->used_bytes) || (free_blocks != h->free_blocks)) {
        *why = "the counts are not the blocks'";
        return 0;
    }
    *why = NULL;
    return 1;
}

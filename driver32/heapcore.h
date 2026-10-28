/*
 * vgpu98dx.dll: the heap's allocator (heap.c wraps it with its lock and its
 * log line). Plain C with no Windows types, so the suite compiles it on the
 * host (tools/tests/test_heap.c). Include <stdint.h> (or the DLL's dll.h)
 * first.
 *
 * Blocks carry a 16-byte header: their size (header included, a multiple
 * of 16, 32 at least), whether they are in use, and the size of the block
 * before them, so a freed block merges with free neighbours on both sides.
 * Free blocks are on lists by size (two levels: the size's power of two,
 * then eight steps within it), a bitmap naming the lists that hold any; a
 * free block's first two dwords after its header link it into its list.
 * An allocation takes the first block of its size's own list when that
 * fits (a hole of its size used again), else the first block of the first
 * list whose every block fits, so it costs the same however many blocks the
 * heap holds; with neither, it walks its size's list for one that fits.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_HEAPCORE_H
#define VGPU98_HEAPCORE_H

#define HC_ALIGN   16u
#define HC_MIN     32u /* a block: its header and a free block's two links */
#define HC_FL_MIN  5   /* log2(HC_MIN) */
#define HC_FL_MAX  31
#define HC_SL      8   /* steps within a power of two */
#define HC_LISTS   ((HC_FL_MAX - HC_FL_MIN + 1) * HC_SL)
#define HC_MAP     ((HC_LISTS + 31) / 32)
#define HC_NONE    0xFFFFFFFFu

typedef struct hc {
    uint8_t *base;            /* 16-byte aligned */
    uint32_t size;            /* a multiple of HC_ALIGN, HC_MIN at least */
    uint32_t head[HC_LISTS];  /* each list's first free block (its offset), HC_NONE none */
    uint32_t map[HC_MAP];     /* the lists that hold any, a bit each */
    uint32_t used_blocks;
    uint32_t used_bytes;      /* headers and rounding included */
    uint32_t free_blocks;
} hc_t;

typedef struct hc_figures {
    uint32_t used_blocks;
    uint32_t used_bytes;
    uint32_t free_blocks;
    uint32_t free_bytes;
    uint32_t largest_free; /* the largest free block (its bytes, header included) */
} hc_figures_t;

/* One free block over all of [base, base + size); size rounded down to
   HC_ALIGN. */
void  hc_init(hc_t *h, void *base, uint32_t size);
/* `bytes` (1 or more), 16-byte aligned; NULL if no free block fits. */
void *hc_alloc(hc_t *h, uint32_t bytes);
/* A block hc_alloc gave (NULL, an address outside the heap, or a block not
   in use: nothing happens). */
void  hc_free(hc_t *h, void *p);
/* The heap's figures (largest_free from the fullest list's blocks: a walk
   of that list alone). */
void  hc_figures(const hc_t *h, hc_figures_t *f);
/* For the tests: every block walked, 1 if the heap is whole (the blocks
   tile it, each header's `prev` its neighbour's size, no two free blocks
   touch, every free block on its size's list once and every listed block
   free, the bitmap as the lists, the counts as the blocks); else 0 and
   `why` names the first fault. */
int   hc_check(const hc_t *h, const char **why);

#endif /* VGPU98_HEAPCORE_H */

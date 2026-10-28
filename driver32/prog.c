/*
 * vgpu98dx.dll: the GPU's programs (command set 2.2, spec/directx-hal.md,
 * Shaders). A shader the translator (xlate.c) took keeps its program here:
 * an id from the table every process shares, its instructions in the DLL's
 * heap, and the state epoch the GPU was last sent it in (dp2.c sends it
 * again after a reset, which loses every program).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3d.h"
#include "xlate.h"

int d3d8_programs;
/* The largest program made of each kind (0 vertex, 1 pixel), a session */
DWORD prog_largest[2];

/* Ids 1-4095, a bit each, id 0 never given: every process's shaders take
   theirs here, so no two programs share one. */
static DWORD prog_ids[(VGPU98_PROG_MAX_ID + 1) / 32] = { 1 };

static DWORD
prog_id_take(void)
{
    DWORD w;

    for (w = 0; w < ARRAY_SIZE(prog_ids); w++) {
        DWORD v = __atomic_load_n(&prog_ids[w], __ATOMIC_RELAXED);

        while (v != ~0u) {
            const DWORD bit = (DWORD) __builtin_ctz(~v);

            if (__atomic_compare_exchange_n(&prog_ids[w], &v, v | (1u << bit), 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
                return 32 * w + bit;
        }
    }
    return 0;
}

static void
prog_id_give(DWORD id)
{
    if (id && (id <= VGPU98_PROG_MAX_ID))
        __atomic_fetch_and(&prog_ids[id / 32], ~(1u << (id % 32)), __ATOMIC_ACQ_REL);
}

void *
prog_scratch(void)
{
    return heap_alloc(sizeof(xl_out_t));
}

int
prog_make(d3d_prog_t *p, const void *translated, DWORD range)
{
    const xl_out_t *o   = (const xl_out_t *) translated;
    DWORD          *ins = heap_alloc(o->n * VGPU98_PROG_INS_DWORDS * sizeof(DWORD));
    DWORD           id;

    if (!ins)
        return 0;
    if (!(id = prog_id_take())) {
        heap_free(ins);
        return 0;
    }
    memcpy(ins, o->ins, o->n * VGPU98_PROG_INS_DWORDS * sizeof(DWORD));
    p->id     = id;
    p->kind   = o->kind;
    p->range  = range;
    p->n      = o->n;
    p->ins    = ins;
    p->stages = o->info.stages;
    p->epoch  = sb->state_epoch - 1; /* not sent */
    DIAG({
        const DWORD k = o->kind == VGPU98_PROG_PIXEL;

        if (o->n > prog_largest[k])
            prog_largest[k] = o->n;
    });
    return 1;
}

/* The id goes back to the table: the next program given it replaces the
   GPU's (the DLL sends no free; the GPU keeps at most 4095). */
void
prog_free(d3d_prog_t *p)
{
    prog_id_give(p->id);
    heap_free(p->ins);
    memset(p, 0, sizeof(*p));
}

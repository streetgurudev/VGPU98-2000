/*
 * What driver32/gpu.c, the ring's client, needs of a Glide DLL:
 * the card's addresses, the log, the
 * counters it keeps, and the DirectX DLL's hooks as a Glide DLL has them.
 * gpu.c is built into each Glide DLL with VGPU98_GLIDE_DLL defined, which
 * has it include this file, so both kinds of client drive the ring by the
 * same code.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_GLIDE_GPUENV_H
#define VGPU98_GLIDE_GPUENV_H

#include "glidedll.h"
#include "vid_vgpu98_gpu.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* The card, as card.c found it: the shared block, BAR0, video memory. */
extern vgpu98_shared_t *sb;
extern volatile DWORD  *regs;
extern uint8_t         *vram;

static inline DWORD
reg_read(DWORD off)
{
    return regs[off / 4];
}

#ifdef REG_WRITE_HOOK /* the host test's GPU (tools/tests/test_glide.c) */
void REG_WRITE_HOOK(DWORD off, DWORD v);
static inline void
reg_write(DWORD off, DWORD v)
{
    REG_WRITE_HOOK(off, v);
}
#else
static inline void
reg_write(DWORD off, DWORD v)
{
    regs[off / 4] = v;
}
#endif

static inline uint64_t
rdtsc(void)
{
    uint64_t v;

    __asm__ volatile("rdtsc" : "=A"(v));
    return v;
}

/* The ring's lines follow the DLL's own prefix ("<tag> <PROGRAM>: "). */
#define DLL_TAG "ring"

/* dbg: a line at Diagnostics "Glide"; dbg_event and dbg_event_own: an event
   line at any level but Off. 1 if written. */
void dbg(const char *fmt, ...);
int  dbg_event(const char *fmt, ...);
int  dbg_event_own(const char *fmt, ...);

/* diag_on: Diagnostics "Glide", where the ring's counters are kept (the
   Glide report); diag_events: any level but
   Off. */
extern int diag_on;
extern int diag_events;

/* The ring's counters, by the names gpu.c gives them. */
typedef struct glide_ring_diag {
    DWORD    sleeps;
    DWORD    waits_lock;
    DWORD    waits_ring;
    DWORD    waits_fence;
    DWORD    long_waits;
    DWORD    head_reads;
    DWORD    gpu_errors;
    DWORD    gpu_errors_unlogged;
    DWORD    err_op;
    DWORD    err_arg;
    uint64_t wait_cycles;
} glide_ring_diag_t;

extern glide_ring_diag_t diag;

#define DIAG(stmt)        \
    do {                  \
        if (diag_on) {    \
            stmt;         \
        }                 \
    } while (0)

/* The export the program is in ("a Glide call" outside one). */
const char *flight_current(void);

/* A Glide DLL's batches draw for no Direct3D context: these are the
   DirectX DLL's, and see only token 0. */
#define CTX_TOKEN_HANDLE(t) (t)
static inline int
ctx_token_live(DWORD token)
{
    (void) token;
    return 0;
}

static inline void
ctx_refused(DWORD token, const char *name, DWORD op, DWORD arg, DWORD code)
{
    (void) token;
    (void) name;
    (void) op;
    (void) arg;
    (void) code;
}

/* After a fault, the DirectX DLL tells its buffers the fence they wait for;
   a Glide DLL has none. */
static inline void
dp2_fence_read(DWORD fence)
{
    (void) fence;
}

/* As a batch ends, with diag_on: the Glide report's turn. */
void diag_report(void);

/* KERNEL32's, which gpu.c calls. */
DLLIMPORT DWORD STDCALL GetTickCount(void);
DLLIMPORT DWORD STDCALL GetCurrentProcessId(void);
DLLIMPORT DWORD STDCALL GetCurrentThreadId(void);

#include "gpu.h"

#endif /* VGPU98_GLIDE_GPUENV_H */

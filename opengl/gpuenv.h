/*
 * What driver32/gpu.c, the ring's client, needs of the OpenGL ICD:
 * the card's addresses, and the DirectX
 * DLL's hooks as the ICD has them. gpu.c is built into the ICD with
 * VGPU98_ICD_DLL defined, which has it include this file, so the ICD drives
 * the ring by the same code as the DirectX DLL and the Glide DLLs.
 *
 * The ring's lines go to the host's log through the ICD's writer (diag.c).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_ICD_GPUENV_H
#define VGPU98_ICD_GPUENV_H

#include "icd.h"

static inline DWORD
reg_read(DWORD off)
{
    return regs[off / 4];
}

#ifdef REG_WRITE_HOOK /* the host test's GPU (tools/tests/test_ogl.c) */
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
#ifdef __i386__
    uint64_t v;

    __asm__ volatile("rdtsc" : "=A"(v));
    return v;
#else
    return 0;
#endif
}

#define DLL_TAG "ring"

/* The ICD gives the GPU 5 seconds without progress before a wait calls it
   stuck (driver32/gpu.c's one second elsewhere): the OpenGL device may
   spend over a second making a pipeline its warm-up did not. */
#define NO_PROGRESS_MS   5000
#define NO_PROGRESS_TEXT "5 seconds"

/* The ring's lines follow the ICD's own prefix ("VGPU98 opengl <stamp>
   <PROGRAM>: "; diag.c). dbg: a line at Diagnostics "OpenGL"; dbg_event
   and dbg_event_own: an event line at any level but Off. 1 if written.
   diag_on and diag_events are icd.h's. */
void dbg(const char *fmt, ...);
int  dbg_event(const char *fmt, ...);
int  dbg_event_own(const char *fmt, ...);

/* The ring's counters, by the names gpu.c gives them (kept, never shown). */
typedef struct icd_ring_diag {
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
} icd_ring_diag_t;

extern icd_ring_diag_t diag;

#define DIAG(stmt) \
    do {           \
    } while (0)

static inline const char *
flight_current(void)
{
    return "an OpenGL call";
}

/* The ICD's batches draw for no Direct3D context: these are the DirectX
   DLL's, and see only token 0. */
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

static inline void
dp2_fence_read(DWORD fence)
{
    (void) fence;
}

static inline void
diag_report(void)
{
}

/* KERNEL32's, which gpu.c calls. */
DLLIMPORT DWORD STDCALL GetTickCount(void);
DLLIMPORT DWORD STDCALL GetCurrentProcessId(void);
DLLIMPORT DWORD STDCALL GetCurrentThreadId(void);

#include "gpu.h"

#endif /* VGPU98_ICD_GPUENV_H */

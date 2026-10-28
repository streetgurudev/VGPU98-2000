/*
 * The GPU's command ring as its clients drive it (gpu.c): the ring lock,
 * packets, fences and recovery (spec/directx-hal.md). vgpu98dx.dll and the
 * Glide DLLs build gpu.c from the same source; each
 * gives it what it needs of the rest of the DLL (dll.h, glide/gpuenv.h).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_GPU_H
#define VGPU98_GPU_H

#include "shared.h"


/* Chooses where the ring is, at the first DriverInit with acceleration: the
   mini-VDD's buffer in main memory, else video memory. Logs the choice. */
void      gpu_ring_choose(DWORD profile);

/* Starts a batch: takes the ring lock and makes sure the GPU runs. DD_OK, or
   DDERR_SURFACELOST while a hold bit keeps the GPU stopped (the lock is not
   held then). */
HRESULT   gpu_begin(void);
/* Restarts a GPU that stopped and is not held (after a reset, say), so the
   fences it lost complete; for callbacks that only test fences. */
void      gpu_kick(void);
/* Ends a batch: a FENCE, the doorbell, the lock released. Returns the fence. */
DWORD     gpu_end(void);
/* gpu_end in two steps, for work that must still hold the lock once the
   fence is known: the FENCE and the doorbell, then the release. */
DWORD     gpu_finish(void);
void      gpu_release(void);
/* The ring's place before a DrawPrimitives2 command (gpu_mark), and back to
   it after the command faulted (gpu_unwind): what the command wrote past the
   last doorbell goes unread; the GPU may hold part of what went before one,
   so every context sends its whole state again. Nothing unless this thread
   holds the lock (gpu_holds). */
typedef struct gpu_mark {
    DWORD tail;
    DWORD rings;
} gpu_mark_t;
void    gpu_mark(gpu_mark_t *m);
void    gpu_unwind(const gpu_mark_t *m);
/* After a fault: a batch this thread holds the ring lock for ends (what it
   wrote past the last doorbell dropped, a fence after the rest), the lock
   goes and the GPU is waited for. Nothing without the lock. */
void    gpu_fault_end(void);
/* This thread holds the ring lock. */
int     gpu_holds(void);
/* Room for a packet of `len` payload dwords; returns where its payload goes
   (the header is written). NULL if the GPU stopped while waiting for room:
   the batch is then lost, and gpu_end still ends it. */
volatile DWORD *gpu_packet(DWORD op, DWORD len);
/* A FENCE packet in the current batch; returns its value. */
DWORD     gpu_fence(void);
/* Is fence v complete? */
int       gpu_fence_done(DWORD v);
/* Waits for fence v without the lock. DD_OK, or DDERR_SURFACELOST when held. */
HRESULT   gpu_wait(DWORD v);
/* Is the GPU held by the display driver or the mini-VDD? */
int       gpu_held(void);
/* STATUS and VBLANK_COUNT, from the GPU's status block in main memory when
   there is one (no bus cycles), else from the registers. */
DWORD     gpu_status(void);
/* The GPU's state in a line (fence, ring, status, ring lock): the long
   waits' and the still-drawing streaks' second line. */
void      gpu_state_line(void);
DWORD     gpu_vblank_count(void);
/* Bytes of ring free right now, for DDGBS_CANBLT. */
DWORD     gpu_ring_free(void);
/* Releases the ring lock if the process holds it (ContextDestroyAll). */
void      gpu_release_process(DWORD pid);
/* The Direct3D context the open batch draws for, by its token (ctx_token;
   0, the default: DirectDraw's own), so a packet the GPU refuses names it. */
void      gpu_batch_owner(DWORD token);
/* An opcode's name, for the log. */
const char *gpu_op_name(DWORD op);

#endif /* VGPU98_GPU_H */

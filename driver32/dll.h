/*
 * vgpu98dx.dll: the DirectDraw and Direct3D driver for the VGPU98 2000, for
 * the DirectX 6, 7 and 8 driver interfaces (spec/directx-hal.md). Internal
 * interfaces between its source files.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_DLL_H
#define VGPU98_DLL_H

#define VGPU98_DLL 1

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "shared.h"
#include "stamp.h"
#include "vid_vgpu98_card.h"
#include "vid_vgpu98_gpu.h"

#define STDCALL   __attribute__((stdcall))
#define DLLIMPORT __declspec(dllimport)
#define DLLEXPORT __declspec(dllexport)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* rt.c */
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

/* Do n dwords differ? memcmp compares bytes; the state the DLL compares on
   every flush is dwords. */
static inline int
dwords_differ(const void *a, const void *b, DWORD n)
{
    const DWORD *p = a;
    const DWORD *q = b;

    while (n--) {
        if (*p++ != *q++)
            return 1;
    }
    return 0;
}

/* KERNEL32: the DLL's only imports. Handles are DWORDs here. */
DLLIMPORT DWORD STDCALL GetTickCount(void);
DLLIMPORT DWORD STDCALL GetCurrentProcessId(void);
DLLIMPORT void STDCALL  Sleep(DWORD ms);
DLLIMPORT DWORD STDCALL CreateFileA(const char *name, DWORD access, DWORD share, void *security, DWORD disposition,
                                    DWORD flags, DWORD template_file);
DLLIMPORT BOOL STDCALL  DeviceIoControl(DWORD device, DWORD code, void *in, DWORD in_bytes, void *out, DWORD out_bytes,
                                        DWORD *returned, void *overlapped);
DLLIMPORT BOOL STDCALL  CloseHandle(DWORD handle);
DLLIMPORT void *STDCALL VirtualAlloc(void *address, DWORD bytes, DWORD type, DWORD protect);
DLLIMPORT BOOL STDCALL  VirtualFree(void *address, DWORD bytes, DWORD type);
DLLIMPORT DWORD STDCALL OpenProcess(DWORD access, BOOL inherit, DWORD pid);
DLLIMPORT BOOL STDCALL  GetExitCodeProcess(DWORD process, DWORD *code);
DLLIMPORT DWORD STDCALL GetLastError(void);
DLLIMPORT DWORD STDCALL GetCurrentThreadId(void);
DLLIMPORT void STDCALL  RtlUnwind(void *target_frame, void *target_ip, void *record, DWORD return_value);
DLLIMPORT DWORD STDCALL GetModuleFileNameA(DWORD module, char *name, DWORD size); /* module 0: the process's executable */

#define INVALID_HANDLE_VALUE 0xFFFFFFFF
#define MEM_COMMIT           0x1000
#define MEM_RESERVE          0x2000
#define MEM_RELEASE          0x8000
#define PAGE_READWRITE       0x04
#define PROCESS_QUERY_INFORMATION 0x0400
#define STILL_ACTIVE         0x103
#define ERROR_INVALID_PARAMETER 87

/* --- main.c: the hardware and the shared block -------------------------- */

extern vgpu98_shared_t *sb;  /* the shared block, from DriverInit's context */
extern volatile DWORD  *regs; /* BAR0 */
extern uint8_t         *vram; /* video memory through the framebuffer mapping */

static inline DWORD
reg_read(DWORD off)
{
    return regs[off / 4];
}

static inline void
reg_write(DWORD off, DWORD v)
{
    regs[off / 4] = v;
}

/* d3d.c: moved by every surface created or destroyed and every texture
   handle created, destroyed or swapped; cached texture descriptions of
   DirectX 6 handles built before it moved are built again (a DirectX 7
   surface handle has a generation of its own, d3d7.c). */
extern DWORD surf_epoch;

/* d3d.c: the world epoch, moved by everything a context's state is built
   from that can change without a state command: a surface created,
   destroyed, lost or restored, a flip (which also moves DirectX 7 handles
   along a flipping chain), a texture handle created, destroyed or swapped,
   a DirectX 7 surface handle recorded or forgotten, a source colour key
   set, a surface attached, a process's end. A DrawPrimitives2 call checks
   its context's state in full only when it moved (spec/directx-hal.md).
   Palettes have an epoch and versions of their own, which SETPALETTE and
   UPDATEPALETTE move instead. Moved after the
   change where the DLL makes it (a release, so the change is seen first),
   before where DirectDraw does. */
extern DWORD world_epoch;

static inline void
world_moved(void)
{
    __atomic_add_fetch(&world_epoch, 1, __ATOMIC_RELEASE);
}

/* A surface's driver-reserved field (dwReserved1 of its local object; the
   global one's holds the last-use fence). */
#define SURF_GPU_WRITES 0x1 /* a 3D target or Z buffer: the GPU may hold newer pixels than video memory */
#define SURF_SLICES     0x2 /* a volume level CreateSurface laid out: slices its rows' height apart (DirectX 8) */

void dbg(const char *fmt, ...); /* a line to the host's diagnostic log (BAR0 DEBUG), with diag_on */
/* An event line: with diag_on as dbg; at
   Minimal (diag_events) too, a place's first three, then again after 10
   seconds without one from there. 1 if it was written. */
int  dbg_event(const char *fmt, ...);
/* An event line its caller limits already (the next line of one written:
   the GPU's state, a fault's callers; a wait's repeats; a fault's place, a
   GPU error's kind, a pool grow's step, each with its own first three):
   with diag_on or diag_events. 1 if it was written. */
int  dbg_event_own(const char *fmt, ...);

/* The start of every line the DLL writes: its name and build stamp. */
#define DLL_TAG "VGPU98 DLL " VGPU98_BUILD_STAMP

/* POOL_GROW's failure steps (spec/minivdd.md): 1-7, 0 when it names none. */
#define POOL_GROW_STEPS 8

/* The kinds of mismatch DirectX 8's record self-check counts (d3d7.c):
   memory, pitch or size, caps, a buffer's record moved, no record. */
#define REC_KINDS 5

/* Diagnostics: counted only while the host's
   log listens (BAR0 DEBUG reads 1). With Diagnostics off the only cost is
   DIAG()'s flag test. */
typedef struct dll_diag {
    DWORD    batches;     /* DrawPrimitives2 calls */
    DWORD    flushes;     /* state flushes: one per draw command */
    DWORD    flushes_skipped; /* draw commands that found the GPU holding all the state */
    DWORD    head_reads;  /* RING_HEAD reads for room in the ring */
    DWORD    values;      /* state values sent: the ones that changed */
    DWORD    tex_rebuilt; /* texture descriptions built: none cached, or something they were built from changed */
    DWORD    tex_cubes;   /* cube maps' descriptions, built at each use and not kept */
    DWORD    pal_unbound; /* PALETTE packets sent for an 8-bit texture with no palette bound or recorded (zeros) */
    DWORD    pal_refused; /* palettes or their colours with no room: the table full or the heap */
    DWORD    heap_refused; /* heap allocations with no block that fits */
    DWORD    batch_overflows; /* batches past BATCH_TEX textures: every texture of the context fenced */
    DWORD    tex_new;     /* of those, none cached for the texture */
    DWORD    tex_evicted; /* its set held eight others: the least recently used went */
    DWORD    tex_changed; /* something it was built from changed */
    DWORD    calls_kept;  /* DrawPrimitives2 calls that kept their context's state from the call before */
    DWORD    sent_declare; /* DECLARE packets: sent when the GPU holds another declaration */
    DWORD    sent_indices; /* INDICES: after a reset */
    DWORD    sent_stream;  /* STREAM: when stream 0 changes */
    DWORD    locks;
    DWORD    locks_tex;   /* of textures */
    DWORD    locks_direct; /* without a round trip: the GPU never wrote the surface */
    DWORD    locks_sync;   /* with SYNC, FENCE and a wait */
    DWORD    waits_ring;  /* for room in the ring */
    DWORD    waits_fence; /* for a fence */
    DWORD    waits_lock;  /* for the ring lock */
    DWORD    sleeps;      /* Sleep(1) calls in those waits */
    DWORD    busy;        /* DDERR_WASSTILLDRAWING answers */
    uint64_t wait_cycles; /* rdtsc */
    uint64_t flip_cycles; /* from Flip's first busy answer to its success */
    uint64_t flip_since;  /* rdtsc at that first busy answer; 0: none */
    uint64_t vtx_sent;    /* bytes of the calls' vertices sent, as their draws needed them */
    uint64_t vtx_buffer;  /* bytes of the calls' vertex buffers: what sending all of them would cost */
    uint64_t vtx_again;   /* bytes sent a second time (overwritten by inline vertices) */
    uint64_t idx_sent;    /* bytes of indices sent */
    /* The pool's buffers (summary buffers). */
    DWORD    buf_locks;   /* Locks */
    DWORD    buf_waits;   /* of which waited for the GPU */
    DWORD    buf_renames; /* DISCARDCONTENTS Locks of a buffer in use, given fresh memory */
    DWORD    rn_spare;     /* renames to one of the buffer's own spares ... */
    DWORD    rn_fresh;     /* ... to a fresh block, the buffer's contents copied in ... */
    uint64_t rn_fresh_bytes;
    DWORD    spares_freed; /* ... and spares given back to the pool for room */
    DWORD    sh_locks[2];  /* Locks through a copy in main memory: GPU-written surfaces, textures ... */
    uint64_t sh_read_bytes;  /* ... the bytes DOWNLOAD read back ... */
    uint64_t sh_write_bytes; /* ... and UPLOAD sent back ... */
    DWORD    sh_refused;     /* ... Locks without a copy (no entry or block) ... */
    DWORD    sh_lost;        /* ... and Unlocks whose copy could not be sent back */
    DWORD    pool_given_back; /* kilobytes of chunks given back while contexts ran */
    DWORD    buf_swaps;   /* the runtime's vertex buffers given fresh memory (SWAPVERTEXBUFFER) */
    DWORD    buf_sysmem;  /* buffers the pool could not hold, put in system memory */
    DWORD    buf_sys_back; /* buffers moved from system memory into the pool */
    DWORD    buf_refused; /* buffers refused: no memory at all, or no record */
    DWORD    grow_fails[POOL_GROW_STEPS]; /* the mini-VDD's failed grows, by step */
    DWORD    buf_unswapped; /* calls drawing from the runtime's own buffer without a swap: through the ring */
    uint64_t buf_static;  /* bytes the GPU was told to read: application buffers never locked with NOOVERWRITE or DISCARDCONTENTS */
    uint64_t buf_dynamic; /* application buffers locked so */
    uint64_t buf_runtime; /* the runtime's own */
    /* The DirectX 7 interface (summary dx7). */
    DWORD    surfaces_ex;   /* CreateSurfaceEx calls that recorded handles */
    DWORD    texblts;       /* TEXBLTs */
    /* The texture trace (diag_textures). */
    DWORD    tt_renamed; /* stage handles that named another surface or address than at their last flush */
    DWORD    tt_setpal;  /* SETPALETTEs ... */
    DWORD    tt_updpal;  /* ... and UPDATEPALETTEs seen */
    DWORD    tt_dropped; /* its per-context lines past their budgets */
    DWORD    tt_matrices; /* texture matrices set ... */
    DWORD    tt_mx_untold; /* ... past a context's TT_MATRICES lines */
    /* The vertex race detector's VSUM notes (dp2.c):
       sent, the bytes they hashed, and draws left without one (the budget). */
    DWORD    vsum_notes;
    uint64_t vsum_bytes;
    DWORD    vsum_skipped;
    DWORD    vsum_skipped_quiet; /* ... of them, quiet buffers' (vsum_quiet) */
    DWORD    vsum_index;         /* index buffer windows hashed */
    DWORD    texblts_refused; /* TEXBLTs whose textures' handles named nothing */
    DWORD    texblt_preloads; /* TEXBLTs with no destination: a preload of a texture the driver manages (none) */
    DWORD    texblt_copies; /* levels copied in video memory (BLT) */
    uint64_t texblt_bytes;  /* bytes loaded from system memory (UPLOAD) */
    DWORD    sb_recorded;   /* state blocks recorded */
    DWORD    sb_executed;
    DWORD    sb_captured;
    /* Packets the GPU refused. */
    DWORD    gpu_errors;
    DWORD    gpu_errors_unlogged; /* of those, not logged (repeats) */
    DWORD    err_op;              /* the last one's opcode (~0: unknown) */
    DWORD    err_arg;             /* and first dword */
    DWORD    ctx_stopped;         /* contexts that stopped drawing */
    DWORD    calls_dropped;       /* DrawPrimitives2, Clear and Clear2 calls of stopped contexts */
    DWORD    sys_blts;      /* DirectDraw's system-to-video blits through UPLOAD */
    DWORD    sys_blt_dxt;   /* of which decompressed DXT */
    uint64_t sys_blt_bytes;
    /* Time inside the DLL (rdtsc), waits included. */
    uint64_t cyc_dp2;  /* DrawPrimitives2 */
    uint64_t cyc_dp2_setup; /* of which before its first command */
    uint64_t cyc_dp2_state; /* in state flushes */
    uint64_t cyc_dp2_tex;   /* of which their textures */
    DWORD    tex_reused;    /* stages whose texture description a flush reused */
    uint64_t cyc_dp2_draws; /* in the rest of its commands */
    uint64_t cyc_blt;  /* Blt */
    uint64_t cyc_lock; /* Lock */
    uint64_t cyc_heap; /* heap_alloc, its lock's wait included */
    DWORD    heap_allocs;
    /* The runtime's own vertex buffers (PB_IMPLICIT) in the pool, read through
       the ring by DirectX 8 draws:
       windows sent; their Locks (DISCARDCONTENTS, NOOVERWRITE, the rest);
       windows checked again later, those changed with no Lock between that
       would have waited or renamed, and of those, with a NOOVERWRITE Lock
       between. */
    DWORD    rt_windows;
    DWORD    rt_locks[3];
    DWORD    rt_checked;
    DWORD    rt_changed;
    DWORD    rt_changed_noow;
    /* The vertex audit (diag_audit): draws checked, those reaching past the
       vertices their call names, of those past what the GPU can read, of
       those on the ring path, and the furthest past (vertices) */
    DWORD    audit_draws;
    DWORD    audit_past_call;
    DWORD    audit_past_gpu;
    DWORD    audit_ring;
    DWORD    audit_furthest;
    /* The depth: draws by the depth test they make
       (none: no depth buffer or ZENABLE 0; Z-buffered; W-buffered), those
       testing a 16-bit and a 24-bit depth buffer; WINFO commands taken (of
       them with a finite far value, with an infinite one) and refused, and
       the least and greatest near and finite far values. */
    DWORD    draws_depth[3];
    DWORD    draws_d16;
    DWORD    draws_d24;
    DWORD    winfo;
    DWORD    winfo_finite;
    DWORD    winfo_infinite;
    DWORD    winfo_refused;
    float    wnear_lo;
    float    wnear_hi;
    float    wfar_lo;
    float    wfar_hi;
    /* The DirectX 8 interface (summary dx8): its commands by kind (45-67);
       draws through streams, of them indexed, by streams read (1, 2, 3-4, 5
       or more); draws skipped, by why (dp2.c's X8_SKIP_*); vertex bytes
       through the ring, index bytes read in place and through the ring;
       vertex shaders made (declarations the GPU reads, and the others),
       pixel shaders and patches stepped over; VOLUMEBLTs, BUFFERBLTs and of
       those the ones that waited; draws too large for the ring at once, sent
       in pieces; presentation blits that waited for a frame (ddraw.c). */
    DWORD    x8_cmds[23];
    DWORD    x8_draws;
    DWORD    x8_draws_indexed;
    DWORD    x8_streams[4];
    DWORD    x8_skipped;
    DWORD    x8_skip_why[5];
    uint64_t x8_ring_bytes;
    uint64_t x8_pool_idx;
    uint64_t x8_ring_idx;
    DWORD    x8_shaders;
    DWORD    x8_shaders_bad;
    DWORD    x8_pshaders;
    DWORD    x8_patches;
    DWORD    volblts;
    DWORD    volblts_refused; /* VOLUMEBLTs whose volumes' handles named nothing, or were no volumes */
    DWORD    bufblts;
    DWORD    bufblts_refused; /* BUFFERBLTs whose buffers' handles named no usable buffer */
    DWORD    bufblt_waits;
    DWORD    x8_split;
    DWORD    present_waits;
    /* DirectX 8 draws sent with vertex blending (immediate ones too; VERTEXBLEND
       as the GPU takes it; pre-transformed ones not counted): with indexed
       blending on, by where their matrix indices came from (dp2.c's
       X8_IDX_*: an FVF code with LASTBETA_UBYTE4, an FVF code's last
       weight, a declaration's BLENDINDICES, none: the GPU's 0-3); with it
       off, 1-3 weights; tweened. */
    DWORD    x8_blend[6];
    /* Ring windows cut at their buffer's end (dp2.c's x8_clamped). */
    DWORD    x8_ring_clamped;
    /* DirectX 8 draws whose indices came from the runtime's own index
       buffer, sent through the ring; calls that
       drew from a buffer still locked, given a new version for it. */
    DWORD    x8_rt_idx_draws;
    DWORD    buf_drawn_locked;
    DWORD    x8_rt_ib_flagged;  /* index buffers taken for the runtime's for good ... */
    DWORD    x8_rt_ib_largest;  /* ... the largest (bytes) */
    /* Pixel shaders made, deleted, bound (SETPIXELSHADER changing the
       handle), refused; draws with a handle bound that names no shader
       kept. */
    DWORD    ps_made;
    DWORD    ps_deleted;
    DWORD    ps_bound;
    DWORD    ps_refused;
    DWORD    ps_draws_unknown; /* draws with a handle bound that names no shader kept (refused, unknown) */
    /* Command set 2.2: vertex shaders made into
       programs, refused and deleted; draws with a program of each kind
       bound (0 vertex, 1 pixel); PROGRAM packets and their dwords, BINDs,
       CONSTANTS packets and the registers they set. */
    DWORD    vs_made;
    DWORD    vs_refused;
    DWORD    vs_deleted;
    DWORD    prog_draws[2];
    DWORD    prog_sent;
    DWORD    prog_sent_dw;
    DWORD    prog_binds;
    DWORD    prog_consts;
    DWORD    prog_const_regs;
    /* Waits over 2 seconds named (gpu.c), still-drawing answers by
       callback (BUSY_*). */
    DWORD    long_waits;
    DWORD    busy_by[6];
    /* The stream audit (diag_audit8): DirectX 8 draws checked, and those
       with each fault (dp2.c's A8_*). */
    DWORD    audit8_draws;
    DWORD    audit8_faults[7];
    uint64_t audit8_cycles;  /* its time ... */
    DWORD    audit8_scanned; /* ... indices it read ... */
    DWORD    audit8_reused;  /* ... index ranges it reused ... */
    DWORD    audit8_changed; /* ... and those found changed when scanned again (no new version) */
    /* DirectX 8's shader handles: SETVERTEXSHADER's records with an FVF
       code and with a shader's handle; the functions the DLL was asked to make, by
       version (vertex shaders 1.0, 1.1, other; pixel shaders 1.0-1.3, 1.4,
       other). */
    DWORD    x8_vs_fvf;
    DWORD    x8_vs_decl;
    DWORD    vs_versions[3];
    DWORD    ps_versions[6];
    /* Locks given of vertex [0] (DirectX 7 programs' too) and index [1]
       buffers, by flag (DISCARDCONTENTS, NOOVERWRITE, READONLY), and those
       that waited. */
    DWORD    lock_kind[2];
    DWORD    lock_flags[2][3];
    DWORD    lock_waits[2];
    DWORD    lock_copies[2]; /* busy DirectX 8 buffers given fresh memory with their contents instead of a wait */
    uint64_t lock_copy_bytes[2]; /* the bytes those copies moved */
    /* Frames (Flips, and DirectX 8's windowed presents), and the calls of
       DirectX 8 contexts among the batches. */
    DWORD    frames;
    DWORD    batches8;
    /* Faults the guard caught (guard.c). */
    DWORD    faults;
    /* DirectX 8's surface records (d3d7.c): trees
       the heap had no room for; the self-check's checks and mismatches by
       kind (REC_KINDS). */
    DWORD    rec_refused;
    DWORD    rec_levelless; /* mip chains whose copy reached fewer levels than their creation named */
    DWORD    rec_kept;      /* a level announced again on its own, its copy in its chain's tree kept */
    DWORD    rec_moved;     /* trees left split by a new tree: some of their handles moved into it, others stayed */
    DWORD    rec_adopted;   /* copies of the pool's vertex and index buffers reading the pool's record, a handle recorded again
                               counted again */
    DWORD    rec_unadopted; /* ... copied without it (the copy's memory as at CreateSurfaceEx): none expected */
    DWORD    tex_missing;   /* DirectX 8 stages whose texture handle named nothing */
    DWORD    rec_checks;
    DWORD    rec_mismatches[REC_KINDS];
} dll_diag_t;

extern int        diag_on;
/* Diagnostics "Minimal": BAR0 DEBUG reads VGPU98_DEBUG_EVENTS, not LISTENING.
   diag_on stays 0 (nothing counted, no reports, no flight recorder); the
   event lines (dbg_event) are written, and DEBUG_IDLE. */
extern int        diag_events;
/* The vertex audit: BAR0 DEBUG reads VGPU98_DEBUG_AUDIT (Diagnostics
   "DirectX 7: vertex audit"). Only with diag_on. */
extern int        diag_audit;
/* The stream audit: BAR0 DEBUG reads VGPU98_DEBUG_AUDIT8 (Diagnostics
   "DirectX 8: stream + texture audit (slow)", which sets VGPU98_DEBUG_AUDIT
   and VGPU98_DEBUG_TEXTURES too). Only with diag_on. */
extern int        diag_audit8;
/* The texture audit: BAR0 DEBUG reads VGPU98_DEBUG_TEXTURES (Diagnostics
   "DirectX: texture audit" and the stream + texture audit): the texture
   trace. Only with diag_on. */
extern int        diag_textures;
/* Shader capture: BAR0 DEBUG reads VGPU98_DEBUG_SHADERS (the card's
   "DirectX 8 shaders: Capture", at a full level): pshader.c and d3d8.c log
   each shader's instructions and its first draw's states; 2, with
   VGPU98_DEBUG_SHADERS14 ("Capture, with GPU programs"), its GPU program
   too. Only with diag_on. */
extern int        diag_shaders;
/* A float as text (dbg takes no %f). */
const char       *fmt_float(char *buf, float f);
extern dll_diag_t diag;

#define DIAG(stmt)        \
    do {                  \
        if (diag_on) {    \
            stmt;         \
        }                 \
    } while (0)

static inline uint64_t
rdtsc(void)
{
    uint64_t v;

    __asm__ volatile("rdtsc" : "=A"(v));
    return v;
}

/* As a batch ends, under the ring lock: the report lines, every 5 seconds. */
void diag_report(void);

/* --- The fault guard (guard.c) ---------------------- */

/* A fault the guard caught: the step the code inside the guard was at (with
   its figures: GUARD_STEP), the exception's code, the faulting instruction,
   for an access violation the address touched and whether a write, and the
   DLL's return addresses found on the stack (0 past the last). guard_run
   clears `faulted` and `step` (its assembly knows them first). */
#define GUARD_STACK 6

typedef struct guard_fault {
    int   faulted;
    DWORD step;
    DWORD step_a;
    DWORD step_b;
    DWORD code;
    DWORD eip;
    DWORD addr;
    DWORD write;
    DWORD stack[GUARD_STACK];
} guard_fault_t;

typedef DWORD(STDCALL *guard_fn)(void *);

/* The record guard_run builds on its stack (the chain's link and handler
   first, as Win32 has them). */
typedef struct guard_frame {
    struct guard_frame *prev;
    void               *handler;
    guard_fault_t      *fault;
    DWORD               ebp; /* guard_run's */
} guard_frame_t;

struct exc_record;
struct exc_context;
int guard_handler(struct exc_record *rec, guard_frame_t *frame, struct exc_context *ctx, void *dispatch);

/* fn(arg) inside the DLL's exception frame: its result, or 0 with
   f->faulted set when it faulted (the call abandoned where it was). */
DWORD STDCALL guard_run(guard_fn fn, void *arg, guard_fault_t *f);
/* The image's range (DllMain). */
void          guard_init(DWORD base);
/* The fault's lines: `where`, `cmd` (a DrawPrimitives2 command's place, or
   ""), the step f names, f's figures; then the callers. Rate-limited per
   faulting instruction, `where`, command (`op`, 0 outside one) and step;
   every fault counted. */
void          guard_report(const char *where, const char *cmd, DWORD op, const guard_fault_t *f);
/* After a fault outside a DrawPrimitives2 command: the line; the batch this
   thread held ended (what it wrote past the last doorbell dropped, the GPU
   waited for through the rest); this thread's locks released; 86Box asked
   for a capture. */
void          guard_recover(const char *where, guard_fault_t *f);
/* Asks 86Box for a capture (with Diagnostics). */
void          guard_capture(void);
/* A deliberate fault inside a guard, and the line that says it was caught
   (the stream audit's). */
void          guard_selftest(void);

/* The step the code inside the innermost guard is at (a fault's line names
   it), with its figures: a stage or stream and a handle. Kept in that
   guard's own record, found at FS:[0], so a step is this thread's and this
   guard's alone and goes with the guard. */
#define GUARD_STEP_NONE    0
#define GUARD_STEP_STATE   1 /* the state flush */
#define GUARD_STEP_TEXTURE 2 /* a stage (a) and its texture's handle (b) */
#define GUARD_STEP_STREAM  3 /* a stream (a) and its buffer's handle (b) */
#define GUARD_STEP_INDICES 4 /* the index buffer's handle (b) */
#define GUARD_STEP_AUDIT   5 /* the stream audit */
#define GUARD_STEP_PACKETS 6 /* the packets */
#define GUARD_STEP_SETTLE  7 /* a bound buffer settled before the ring lock (b: its handle) */
#define GUARD_STEP_BLT     8 /* a TEXBLT or VOLUMEBLT: source (a) and destination (b) handles */
#define GUARD_STEP_TARGET  9 /* the render target (a) */

/* The innermost guard's record, NULL outside every guard (the chain's head
   is then another's frame, or FFFFFFFFh at its end; 0 is never one). */
static inline guard_fault_t *
guard_current(void)
{
    const guard_frame_t *fr;

    __asm__ volatile("movl %%fs:0, %0" : "=r"(fr));
    return fr && ((DWORD) fr != 0xFFFFFFFF) && (fr->handler == (void *) guard_handler) ? fr->fault : NULL;
}

/* The barrier keeps the step's stores ahead of the work they name. */
#define GUARD_STEP(s, a, b)                         \
    do {                                            \
        guard_fault_t *const g_ = guard_current();  \
                                                    \
        if (g_) {                                   \
            g_->step   = (s);                       \
            g_->step_a = (DWORD) (a);               \
            g_->step_b = (DWORD) (b);               \
        }                                           \
        __asm__ volatile("" : : : "memory");        \
    } while (0)

/* --- The flight recorder (trace.c) ------------------- */

/* With Diagnostics on, each process's last FLIGHT_CALLS callbacks: the
   callback's name, thread and time, its answer (FLIGHT_INSIDE while it
   runs, FLIGHT_NOTHANDLED when DirectDraw does the work) and two figures
   it names. Shown (flight_dump) when something ended badly. */
#define FLIGHT_CALLS      32 /* a power of two */
#define FLIGHT_INSIDE     0xFFFFFFFFu
#define FLIGHT_NOTHANDLED 0xFFFFFFFEu

typedef struct flight_call {
    const char *name; /* in the DLL's image, at the same address in every process */
    DWORD       thread;
    DWORD       tick;
    DWORD       rv;
    DWORD       a;
    DWORD       b;
    DWORD       polls; /* the same callback of the same thread before, answered "still drawing" (one entry) */
} flight_call_t;

/* A callback starts: its entry (NULL without Diagnostics). */
flight_call_t *flight_enter(const char *name);
/* It ends with result r (DDHAL_DRIVER_*) and answer rv; figures a and b
   unless the callback named its own (flight_figures). With the stream
   audit, an error answer is traced (TRACE_ANSWER). */
void           flight_leave(flight_call_t *e, DWORD r, DWORD rv, DWORD a, DWORD b);
/* The figures of this thread's callback running now. */
void           flight_figures(DWORD a, DWORD b);
/* With diag_on, a guarded callback's time from t0 to t1 (rdtsc, waits
   included, a report written inside it left out) and call, for the
   report's callback times; *slot is the callback's place, taken at its
   first call. */
void           callback_time(BYTE *slot, const char *name, uint64_t t0, uint64_t t1);

/* A callback inside the guard: GUARDED(static, Blt, DDHAL_BLTDATA, ddRVal)
   followed by Blt's body. After a fault the callback answers
   DDHAL_DRIVER_HANDLED with DDERR_GENERIC in its data's `rv`. The flight
   recorder notes it, with GUARDED_FIG's figures fa and fb: expressions of
   the data's own fields (read after the call, outside the guard: no
   pointer of it followed). */
#define GUARDED_FIG(storage, name, type, rv, fa, fb)                                       \
    static DWORD STDCALL name##_body(type *d);                                             \
    static BYTE          name##_timed; /* its place in the callback times, 0 none yet */  \
    storage DWORD STDCALL name(type *d)                                                    \
    {                                                                                      \
        flight_call_t *const fl_ = flight_enter(#name);                                    \
        const uint64_t       t0_ = diag_on ? rdtsc() : 0;                                  \
        guard_fault_t        f_;                                                           \
        DWORD                r_ = guard_run((guard_fn) (void (*)(void)) name##_body, d, &f_); \
                                                                                           \
        if (t0_)                                                                           \
            callback_time(&name##_timed, #name, t0_, rdtsc());                             \
        if (f_.faulted) {                                                                  \
            guard_recover(#name, &f_);                                                     \
            d->rv = DDERR_GENERIC;                                                         \
            r_    = DDHAL_DRIVER_HANDLED;                                                  \
        }                                                                                  \
        if (fl_)                                                                           \
            flight_leave(fl_, r_, (DWORD) d->rv, (DWORD) (fa), (DWORD) (fb));              \
        return r_;                                                                         \
    }                                                                                      \
    static DWORD STDCALL name##_body(type *d)
#define GUARDED(storage, name, type, rv) GUARDED_FIG(storage, name, type, rv, 0, 0)

static inline void
outb(uint16_t port, uint8_t v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

/* --- gpu.c: the ring, fences and recovery ------------------------------- */

#include "gpu.h"

/* The callback this thread is in, as the flight recorder has it ("?"
   without one; "a callback" without diag_on, the recorder off). */
const char *flight_current(void);
/* A still-drawing answer (BUSY_*): counted by callback; a streak of one
   callback's over 2 seconds named (with diag_on). */
#define BUSY_LOCK        0
#define BUSY_BLT         1
#define BUSY_FLIP        2
#define BUSY_FLIPSTATUS  3
#define BUSY_BLTSTATUS   4
#define BUSY_BUFFER_LOCK 5
#define BUSY_KINDS       6
void        busy_answer(int kind);
/* An answer that was not still drawing: the callback's streak ends. */
void        busy_ok(int kind);

static inline void
pkt_put(volatile DWORD **p, DWORD v)
{
    *(*p)++ = v;
}

static inline void
pkt_putf(volatile DWORD **p, float f)
{
    union {
        float f;
        DWORD u;
    } c;

    c.f = f;
    *(*p)++ = c.u;
}

/* Copies `n` dwords to the ring a dword at a time (rep movsd): with the ring
   in video memory every write is a bus write, so never byte by byte. */
static inline void
copy_dwords(volatile DWORD *dst, const void *src, DWORD n)
{
    void *d = (void *) dst;

    __asm__ volatile("cld\n\t"
                     "rep movsl"
                     : "+D"(d), "+S"(src), "+c"(n)
                     :
                     : "memory");
}

/* --- surface.c: DirectDraw surfaces as the GPU sees them ---------------- */

typedef struct gsurf {
    DWORD offset; /* in video memory */
    DWORD width;
    DWORD height;
    DWORD pitch;
    DWORD format; /* VGPU98_FMT_* */
} gsurf_t;

/* The GPU format of a pixel format, 0 if the GPU has none (command set
   2.1's with the DirectX 8 interface only). */
DWORD fmt_from_ddpf(const DDPIXELFORMAT *pf);
/* Bytes per pixel of a format other than DXT. */
DWORD fmt_bytes(DWORD f);
/* The surface's format: its own pixel format, or the display's. */
DWORD surf_format(LPSURF s);
/* Describes a video memory surface; 0 if it is not one the GPU can use. */
int   surf_describe(LPSURF s, gsurf_t *g);
/* Bytes the surface occupies in video memory. */
DWORD surf_bytes(const gsurf_t *g);
void  pkt_surf(volatile DWORD **p, const gsurf_t *g);
int   fmt_is_dxt(DWORD f);
/* Describes a system memory surface the GPU's formats cover, with its
   memory (offset 0); 0 if it is not one. */
int   surf_describe_sys(LPSURF s, gsurf_t *g, const uint8_t **mem);
/* A DXT rectangle widened to whole blocks (the GPU takes no less); others
   as they are. */
void  surf_blocks(const gsurf_t *g, RECTL *r);
/* A rectangle's rows and the bytes of each as memory holds them (rows of
   blocks for DXT), and the byte of its top left in memory laid out as g. */
void  rect_rows(DWORD format, const RECTL *r, DWORD *rows, DWORD *row_bytes);
DWORD rect_start(const gsurf_t *g, const RECTL *r);

/* Surfaces Locked through copies in main memory (shadow.c):
   with PROFILE.DOWNLOAD and the pool. */
#define SHADOW_GPU     0 /* a surface the GPU drew into */
#define SHADOW_TEXTURE 1 /* a texture DISCARDCONTENTS-Locked frame after frame */
extern int shadow_on;
void  shadow_init(DWORD profile);
/* Lock d of a surface (own its record, g its description) through its
   copy: 1 if handled (d->ddRVal says how), 0 to take the usual path. */
int   shadow_lock(DDHAL_LOCKDATA *d, LPSURF own, const gsurf_t *g, int kind);
/* Unlock: 1 if the surface's Lock went through its copy (sent back). */
int   shadow_unlock(DDHAL_UNLOCKDATA *d);
/* Whether texture s, DISCARDCONTENTS-Locked now, was so in the frames
   before too. */
int   shadow_hot(LPSURF s);
/* A surface destroyed: its copy goes. A process gone: its copies go. */
void  shadow_forget(LPSURF s);
void  shadow_process_gone(DWORD pid);
/* Frames shown so far (FLIPs and presenting blits), for shadow_hot. */
DWORD ddraw_frames(void);
/* Writes rectangle `dr` of a video memory surface with rectangle `sr` (the
   same size) of a system memory one, through UPLOAD packets with the texels
   inline, in the current batch. Returns the bytes sent; 0 when there was
   nothing to send or the ring stopped (*ok then 0). */
DWORD upload_rect(const gsurf_t *dst, const RECTL *dr, const gsurf_t *src, const uint8_t *mem, const RECTL *sr, int *ok);
/* upload_rect from a DXT surface in system memory to one of a 16- or
   32-bit colour format (argb_bytes not 0), at most DXT_STRIP texels wide:
   the blocks decompressed (DirectDraw's system-to-video blits may ask for
   it). The rectangles need not be whole blocks. Nothing for others. */
#define DXT_STRIP 4096
DWORD argb_bytes(DWORD format);
DWORD upload_rect_dxt(const gsurf_t *dst, const RECTL *dr, const gsurf_t *src, const uint8_t *mem, const RECTL *sr, int *ok);
/* A BLT packet: rectangle `sr` of one video memory surface to `dr` of
   another, no keys or effects. 0 if the ring stopped. */
int   blt_packet(const gsurf_t *src, const RECTL *sr, const gsurf_t *dst, const RECTL *dr);
/* The record behind a surface the runtime hands in a call (d3d7.c): the
   DLL's copy when it keeps one (DirectX 8), else the surface itself. With
   the stream audit, the copy is checked against the runtime's surface. */
LPSURF surf_own(LPSURF s);
/* A DirectX 8 runtime's surface: one with a handle, on a DirectDraw object
   a DirectX 8 context was made on, in this process (d3d7.c). */
int    surf_dx8(LPSURF s);
/* The last-use fence (the driver's reserved dword) of a record: a surface
   the map holds (the DLL's copy, or the runtime's lasting surface). One the
   runtime hands in a call goes through surf_own first. Never a buffer's:
   the pool keeps buffers' fences, and an adopted buffer's copy holds the
   pool's index there. */
static inline void
surf_used(LPSURF s, DWORD fence)
{
    s->lpGbl->dwReserved1 = fence;
}

/* --- Locks of the DLL's shared tables (pool.c) ------------------------------ */

/* A lock of tables every process's callbacks change, held only for table
   work (microseconds, never across a wait or a request to the mini-VDD). A
   holder whose process ended inside a callback is taken over after 2
   seconds; a process detaching gives back what it holds. */
typedef struct dlock {
    DWORD v;
    DWORD owner; /* process ID of the holder */
    DWORD time;  /* its GetTickCount when it took the lock */
    DWORD listed; /* in the list a detaching process's locks are looked for in */
    DWORD thread; /* thread ID of the holder (a fault's recovery releases its thread's) */
} dlock_t;

void dlock_take(dlock_t *l, const char *what);
void dlock_give(dlock_t *l);
/* A process detaches: the DLL's locks it holds. */
void dlock_release_process(DWORD pid);
/* After a fault: the DLL's locks this thread holds (another thread of the
   process may hold one rightly). */
void dlock_release_thread(void);

/* --- pool.c: main memory the GPU reads, and the execute buffers in it ---- */

/* Execute buffers (spec/directx-hal.md): what the runtime asked for. */
#define PB_VERTEX   0 /* an application's vertex buffer (DDSCAPS2_VERTEXBUFFER) */
#define PB_IMPLICIT 1 /* the runtime's own vertex buffer, or an execute buffer of the old API */
#define PB_COMMAND  2 /* the runtime's command buffer: only the DLL reads it */
#define PB_INDEX    3 /* DirectX 8: an application's index buffer (DDSCAPS2_INDEXBUFFER) */
#define PB_KINDS    4
#define PB_SPARES   2 /* a buffer's own spare blocks at most */

/* A buffer of the DLL's, in the pool or (sys) in system memory. The tables
   are shared by every process; a buffer's own fields are its process's
   (Direct3D serializes a device's calls). A DirectX 8 application's vertex
   or index buffer is adopted at CreateSurfaceEx (pool_adopt): its global
   record is then `own`, which the DLL's copy of the surface points at, and
   the runtime's record (one the runtime may reuse at once) is no longer
   written but in the calls that hand it in. */
typedef struct pbuf {
    DDRAWI_DDRAWSURFACE_GBL *gbl;     /* the surface it backs (`own` once adopted); NULL: a free record */
    DDRAWI_DDRAWSURFACE_GBL  own;     /* an adopted buffer's global record: its memory, size, flags and index */
    void                    *ddl;     /* adopted under this DirectDraw object and handle; NULL: not adopted */
    DWORD                    handle;
    DWORD                    block;   /* its memory in the pool (NONE in system memory) */
    int                      sys;     /* in system memory: the GPU never reads it */
    DWORD                    mem;     /* there, VirtualAlloc's address in process pid */
    DWORD                    old_mem; /* system memory it left outside a Lock, kept until Destroy (0 none) */
    DWORD                    pid;
    DWORD                    size;    /* bytes */
    DWORD                    kind;    /* PB_* */
    DWORD                    version; /* its contents' version, as STREAM names it; 0 none yet */
    DWORD                    fence;   /* the last batch that read it */
    int                      drawn;   /* a batch read it, and its fence was not yet seen complete */
    int                      dynamic; /* locked with NOOVERWRITE or DISCARDCONTENTS at least once */
    DWORD                    lock_flags; /* the flags of its Lock in progress */
    int                      locked;     /* a Lock in progress handed out its memory */
    DWORD                    batch;      /* the DrawPrimitives2 call that listed it as read (dp2.c), 0 none */
    int                      runtime_ib; /* an index buffer the runtime rewrites without Locks (a DRAWINDEXEDPRIMITIVE2
                                            used it): read in place, its indices sent through the ring (dp2.c) */
    /* With diag_on, the runtime buffers' count (pool_rt_window): a number
       new at its creation and at each Lock that would wait or rename (plain
       or DISCARDCONTENTS), and its NOOVERWRITE Locks. */
    DWORD                    rt_gen;
    DWORD                    rt_noow;
    /* With diag_textures: the Locks given since its last VSUM note, as the
       next note's lock word (VGPU98_NOTE_LOCK_*). */
    DWORD                    tt_since;
    DWORD                    tt_id; /* its id in VSUM notes (24 bits, never 0), kept by renames */
    /* DirectX 6 and 7:
       the block a Lock's rename left, kept until the Unlock as the buffer's
       contents from before the Lock (the draws that reach the DLL during
       the Lock read it), with its version and size, and whether a batch
       reads it until kept_fence. */
    int                      kept_on;
    DWORD                    kept;
    DWORD                    kept_version;
    DWORD                    kept_size;
    int                      kept_drawn;
    DWORD                    kept_fence;
    /* A draw reached the DLL while it was locked for writing (not
       NOOVERWRITE): its busy plain Locks copy instead of waiting. */
    int                      drawn_locked;
    /* Its own spare blocks: blocks
       a rename left, for its next renames once no queued packet reads them
       (spare_drawn: one may until spare_fence). Under the pool's lock. */
    DWORD                    n_spare;
    DWORD                    spare[PB_SPARES];
    int                      spare_drawn[PB_SPARES];
    DWORD                    spare_fence[PB_SPARES];
} pbuf_t;

/* At DriverInit, once: the mini-VDD's pool, if the GPU reads main memory.
   Logs the outcome. */
void    pool_init(DWORD profile);
extern int pool_on;
/* The execute-buffer callbacks into the shared block's table (none without
   the pool). */
void    pool_fill_callbacks(void);
/* The pool buffer behind a surface, NULL if it is not one (a buffer in
   system memory is not). */
pbuf_t *pool_buffer(LPSURF s);
/* The surface is one of the DLL's execute buffers (in the pool or in system
   memory): its buffer record is the DLL's own. */
int     pool_has(LPSURF s);
/* CreateSurfaceEx of a DirectX 8 application's vertex or index buffer of the
   DLL's (DDSCAPS2_VERTEXBUFFER, DDSCAPS2_INDEXBUFFER), under DirectDraw
   object `ddl` and `handle`: the pool record takes a global record of its
   own (its memory and size the pool's, flags and format the runtime's), and
   keeps the pair to be found by. The
   own record (for the DLL's copy to point at), or NULL for any other
   surface. A buffer adopted already (its handle recorded again) keeps its
   own. */
DDRAWI_DDRAWSURFACE_GBL *pool_adopt(LPSURF s, void *ddl, DWORD handle);
/* Free chunks go back, but for one, once no Direct3D context exists. */
void    pool_trim(void);
/* The bytes of one of the DLL's execute buffers (in the pool or in system
   memory); 0 for other surfaces, DirectDraw's own included. */
DWORD   exebuf_bytes(LPSURF s);
/* Before DrawPrimitives2 reads an application's vertex buffer in system
   memory, not locked: it moves into the pool, contents and all, if there is
   room (without the ring lock). */
void    pool_settle(LPSURF s);
/* A buffer's GPU address. */
DWORD   pool_gpu(const pbuf_t *b);
/* A new version from the system-wide counter (never 0). */
DWORD   pool_version_new(void);
/* The batch with this fence read the buffer; pool_drawn_all: every
   buffer in the pool (a batch that read more than it could list). */
void    pool_drawn(pbuf_t *b, DWORD fence);
/* A block of the pool outside the buffers (shadow.c's copies): POOL_NO_BLOCK
   if none can be had at once; freed once the GPU passed `fence`; its GPU
   address and where the DLL reads it. */
#define POOL_NO_BLOCK 0xFFFFFFFFu
DWORD   pool_block_alloc(DWORD size);
void    pool_block_free(DWORD k, DWORD fence);
DWORD   pool_block_gpu(DWORD k);
uint8_t *pool_block_mem(DWORD k);
/* The buffer's kept block (its contents from before its Lock): where the
   DLL reads it, its GPU address, bytes and version; 0 if it has none. The
   batch with this fence read it (pool_kept_drawn). */
int     pool_kept(const pbuf_t *b, const uint8_t **mem, DWORD *gpu, DWORD *size, DWORD *version);
void    pool_kept_drawn(pbuf_t *b, DWORD fence);
void    pool_drawn_all(DWORD fence);
/* With diag_on: a DirectX 8 draw sent the `len` bytes at `at`, in the
   runtime's own buffer b (in the pool), through the ring; a window sent 32
   earlier is checked for a change with no waiting or renaming Lock between. */
void    pool_rt_window(pbuf_t *b, const uint8_t *at, DWORD len);
/* The pool buffers the open DirectX 8 batch read, fenced (dp2.c: a batch
   ended after a fault). */
void    dp2_fence_read(DWORD fence);
/* DrawPrimitives2's SWAPVERTEXBUFFER on the runtime's own vertex buffer
   (any other surface: nothing to do, 1): fresh memory of at least `size`
   bytes (REQVERTEXBUFSIZE; 0 its size) whenever the GPU may still read the
   old or the buffer is smaller; the old goes once the GPU has read it. From
   the pool, or, when the pool cannot hold that much, system memory (the
   DDK); a buffer in system memory moves into the pool when there is room.
   Without fresh memory waits until the GPU has read the old (without the
   ring lock). 0 if the buffer is still smaller than `size`. */
int     pool_swap(LPSURF s, DWORD size);
/* DirectX 8's BUFFERBLT into a pool buffer: `n` bytes from `src` at
   `offset`, in order with the commands. A buffer a queued packet may still
   read (with POOL_WRITE_BATCH: the open batch read it) gets fresh memory
   first, its contents copied, the old freed once read (by the fence after
   the open batch's packets); with POOL_WRITE_IN_PLACE (no batch open, none
   to be had) it is written where it is. Fresh memory is only a free block:
   under the ring lock no chunk is made. 0 if none could be had: the caller
   ends its batch and waits. */
#define POOL_WRITE_BATCH    1
#define POOL_WRITE_IN_PLACE 2
int     pool_write(pbuf_t *b, DWORD offset, const void *src, DWORD n, int how);
/* For the report: KB of the pool and its cap, chunks, live buffers by kind
   and their KB, those in system memory (whatever their kind) at index
   PB_KINDS. */
void    pool_figures(DWORD *kb, DWORD *cap_kb, DWORD *chunks, DWORD count[PB_KINDS + 1], DWORD kb_by[PB_KINDS + 1]);
/* The chunks without a buffer and their kilobytes (the report's). */
void    pool_chunk_figures(DWORD *free_chunks, DWORD *free_kb);
/* At the end of a DrawPrimitives2 call, outside every lock: the pool's tidy
   when due, and the chunks queued to go back handed to the mini-VDD. */
void    pool_idle(void);

/* --- caps.c ------------------------------------------------------------- */

void caps_init(DWORD profile);
extern D3DHAL_GLOBALDRIVERDATA caps_global;
extern D3DHAL_D3DEXTENDEDCAPS  caps_extended;
extern DDSURFACEDESC           caps_texture_formats[];
extern DWORD                   caps_num_texture_formats;
extern DWORD                   caps_zformats[1 + 3 * sizeof(DDPIXELFORMAT) / 4];

/* --- ddraw.c: the DirectDraw callbacks ---------------------------------- */

void ddraw_fill_callbacks(DWORD profile);
DWORD STDCALL GetDriverInfo(DDHAL_GETDRIVERINFODATA *d);

/* --- d3d.c, dp2.c: Direct3D --------------------------------------------- */

extern D3DHAL_CALLBACKS  d3d_callbacks;
extern D3DHAL_CALLBACKS2 d3d_callbacks2;
extern D3DHAL_CALLBACKS3 d3d_callbacks3;
extern PFND3DPARSEUNKNOWNCOMMAND d3d_parse_unknown;
void d3d_init(void);
/* A context's token: its handle (the low CTX_TOKEN_BITS bits) and the
   serial number of its creation, so a batch of a context since destroyed
   never names the next context in its slot. */
#define CTX_TOKEN_BITS      6
#define CTX_TOKEN_HANDLE(t) ((t) & ((1u << CTX_TOKEN_BITS) - 1))
/* The GPU refused a packet of a context (under the ring lock). The same
   packet refused GPU_ERR_STOP times, each within GPU_ERR_WINDOW_MS of the
   one before, stops the context: it draws no more, and says so once. */
#define GPU_ERR_STOP      3
#define GPU_ERR_WINDOW_MS 2000
void ctx_refused(DWORD token, const char *name, DWORD op, DWORD arg, DWORD code);
/* Is the token a context that still exists? */
int  ctx_token_live(DWORD token);
/* Direct3D contexts that exist, in every process. */
DWORD d3d_contexts(void);
/* A process leaving (DLL_PROCESS_DETACH; `ending`: the process ends, not
   a FreeLibrary): the flight recorder's lines for each context it left
   live. */
void ctx_leaving(DWORD pid, int ending);
/* Whether a process still runs (an ID that names none: one that ended). */
int  process_alive(DWORD pid);
/* The processor's cycles a millisecond, as the last report measured (0
   before one). */
extern DWORD diag_per_ms;
/* Contexts of a process that ended without destroying them (pid; 0 any
   process): everything of the process goes, as ContextDestroyAll does. */
void ctx_reap(DWORD pid);

#endif /* VGPU98_DLL_H */

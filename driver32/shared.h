/*
 * The shared block: the memory the 16-bit display driver (vgpu98.drv) and
 * the DirectX driver DLL (vgpu98dx.dll) both use (spec/directx-hal.md). It
 * lives in the display driver's fixed, locked data segment; the DLL gets its
 * linear address as DriverInit's context. The Glide DLLs (glide/) find it
 * through the display driver's escape below, for the card's registers and
 * the log's line lock.
 *
 * Fixed-width fields only, naturally aligned, so the layout is the same on
 * every compiler: tools/sbgen compiles this header on the host and writes the
 * offsets as a NASM include (shared.inc) for the 16-bit driver.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_SHARED_H
#define VGPU98_SHARED_H

#include "ddi.h"

#define SB_MAGIC          0x42534756 /* "VGSB" */
#define SB_VERSION        2
#define SB_MAX_MODES      128        /* DirectDraw's mode list */
#define SB_DRIVER_OWNER   0xFFFFFFFF /* lock_owner while the display driver holds the ring lock */
#define SB_RING_SIZE      0x20000    /* 128 KB at the start of the GPU's reserve */

/* The first five fields keep their offsets in every version: a DLL from
   another build can still reach the register window to say so. */
typedef struct vgpu98_shared {
    DWORD magic;
    DWORD version;
    DWORD size;

    /* Written by vgpu98.drv when it loads and on every mode change. */
    DWORD fb_linear;    /* flat address of the framebuffer (all of video memory) */
    DWORD regs_linear;  /* flat address of BAR0's registers, 0 if they could not be mapped */
    DWORD vram_size;
    DWORD reserve_base; /* video memory offset of the GPU's reserve: the ring starts here */
    DWORD mode_width;   /* the mode on the screen */
    DWORD mode_height;
    DWORD mode_bpp;
    DWORD mode_pitch;
    DWORD mode_refresh;
    DWORD mode_serial;  /* changes with every mode change */

    /* The ring lock (spec/directx-hal.md): 0 free, 1 held. Taken with a
       locked bit test and set by the 16-bit driver, with a compare and
       exchange by the DLL; the two agree because the value is 0 or 1. */
    DWORD lock;
    DWORD lock_owner;   /* process ID of the holder, SB_DRIVER_OWNER for the display driver */
    DWORD lock_time;    /* the holder's GetTickCount when it took the lock (DLL holders) */

    /* The ring, as the DLL drives it (under the lock). */
    DWORD fence_issued; /* the last fence value written into the ring */
    DWORD fence_flip;   /* the fence written straight after the last FLIP */
    DWORD flip_start;   /* the video memory offset the last FLIP showed */
    DWORD ring_tail;    /* byte offset in the ring of the next packet */
    DWORD state_epoch;  /* changes whenever the GPU loses its state (reset, recovery) */
    DWORD state_owner;  /* the context whose state the GPU holds, 0 for none */

    /* Written by the DLL in DriverInit. */
    DWORD dll_ready;     /* 1 once DriverInit has filled in its part */
    DWORD dll_hinstance; /* the DLL's instance handle: DDCREATEDRIVEROBJECT returns it */
    DWORD profile;       /* PROFILE as the DLL found it */
    /* The last version given a buffer's contents in main memory (STREAM:
       spec/gpu-coherency.md): one counter for every process, which lives as
       long as the display driver, so no version is ever used twice. */
    DWORD buf_version;
    /* DDHALINFO flags the DLL asks the display driver to add to its
       description: DDHALINFO_GETDRIVERINFO2 when it answers DirectX 8's
       GetDriverInfo2 (spec/directx-hal.md). 0 from a DLL of an older build. */
    DWORD hal_flags;
    /* The DLL's audit notes' last sequence number:
       it lives as long as the display driver, so a DLL loaded again goes on
       counting where the last left (the GPU takes notes in increasing
       order). */
    DWORD note_seq;
    /* The host log's line lock: BAR0's DEBUG register collects one line at
       a time, and the DirectX DLL and the Glide DLLs (every process that
       loads them) write to it. 0 free, 1 held. */
    DWORD log_lock;
    DWORD reserved[1];

    /* The DirectDraw driver description. The DLL fills in ddCaps,
       GetDriverInfo, the Direct3D pointers and its callback tables; the
       display driver fills in the rest at DDCREATEDRIVEROBJECT and on every
       mode change, then calls SetInfo. */
    DDHALINFO                halinfo;
    DDHAL_DDCALLBACKS        cb_dd;
    DDHAL_DDSURFACECALLBACKS cb_surf;
    DDHAL_DDPALETTECALLBACKS cb_pal;
    DDHAL_DDEXEBUFCALLBACKS  cb_exebuf; /* dwFlags 0: no execute buffers of the driver's own */
    VIDMEM                   heap;
    DWORD                    num_modes;
    DDHALMODEINFO            modes[SB_MAX_MODES];
} vgpu98_shared_t;

_Static_assert((offsetof(vgpu98_shared_t, halinfo) % 4) == 0, "the driver description must be dword aligned");

/* The display driver's escape for the Glide DLLs:
   ExtEscape(VGPU98_ESC_INFO) answers where the shared block is and the
   driver's build stamp, in an output buffer of
   vgpu98_escinfo_t. Its input is vgpu98_escquery_t, which names
   the question and the output buffer's size: the 16-bit driver never learns
   the size otherwise, so without it the escape writes nothing.
   QUERYESCSUPPORT answers the escape too. A Voodoo3's Glide finds its card
   through its display driver the same way. */
#define VGPU98_ESC_INFO 0x7098
#define ESC_INFO_MAGIC  0x49454756 /* "VGEI" */

/* The display driver's escape for the OpenGL ICD:
   ExtEscape(VGPU98_ESC_ACCESS) with vgpu98_escaccess_t keeps the
   DIB engine's software cursor out of a rectangle of the screen (begin 1:
   DIB_BeginAccess with CURSOREXCLUDE) and lets it back (begin 0:
   DIB_EndAccess), around the GPU's writes of a window's pixels. It answers
   1; a driver without it answers 0. */
#define VGPU98_ESC_ACCESS 0x7099

typedef struct vgpu98_escaccess {
    WORD begin;                   /* 1 begin, 0 end */
    WORD left, top, right, bottom; /* on the screen, inclusive */
} vgpu98_escaccess_t;

typedef struct vgpu98_escquery {
    DWORD magic;    /* ESC_INFO_MAGIC */
    DWORD out_size; /* the output buffer's bytes: at least ESC_INFO_HEAD */
} vgpu98_escquery_t;

/* The answer. The driver writes as much of it as out_size has room for,
   the head or all of it, and sets size to that: a DLL that asks for the
   head (16 bytes, the first Glide DLLs') gets just that, and one that asks
   for all of it from an older driver gets the head, without the stamp. */
typedef struct vgpu98_escinfo {
    DWORD magic;         /* ESC_INFO_MAGIC */
    DWORD size;          /* the bytes written: ESC_INFO_HEAD or sizeof(vgpu98_escinfo_t) */
    DWORD shared_linear; /* the shared block's flat address, 0 if the driver could not find it */
    DWORD sb_version;    /* SB_VERSION, as the display driver was built */
    char  stamp[16];     /* the display driver's build stamp, NUL-padded */
} vgpu98_escinfo_t;

#define ESC_INFO_HEAD offsetof(vgpu98_escinfo_t, stamp) /* the answer without the stamp: 16 bytes */

_Static_assert(ESC_INFO_HEAD == 16, "the escape's answer's head is not 16 bytes");

#endif /* VGPU98_SHARED_H */

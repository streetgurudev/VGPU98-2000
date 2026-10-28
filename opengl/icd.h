/*
 * vgpu98gl.dll: the VGPU98 2000's OpenGL installable client driver (ICD),
 * which Windows' opengl32.dll loads for a window on the card's desktop
 * (spec/opengl.md). This header is what its files
 * share: Windows' imports, the card, the process's windows and contexts,
 * the batches to the GPU and the device's registers.
 *
 * The DLL is an ordinary one, loaded into each program that uses OpenGL,
 * its data the process's own. opengl32.dll owns WGL and GDI's
 * pixel format calls and forwards them to the ICD's Drv* exports; a GL call
 * jumps through the 336-entry table DrvSetContext returns (glfuncs.h).
 * The ICD keeps GL's state and records commands for the card's
 * OpenGL device (command set 2.5, and 2.6 for its extensions:
 * vid_vgpu98_oglcmd.h) into a batch of the
 * context's own, copied into the GPU's ring as one.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_ICD_H
#define VGPU98_ICD_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "glenum.h"
#include "shared.h"
#include "stamp.h"
#include "vid_vgpu98_card.h"
#include "vid_vgpu98_gpu.h"
#include "vid_vgpu98_oglcmd.h"

#define STDCALL __attribute__((stdcall))
#ifndef DLLIMPORT /* the host test (tools/tests/test_ogl.c) defines it empty, for its stand-ins */
#    define DLLIMPORT __declspec(dllimport)
#endif
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* rt.c (driver32/): what gcc expects of a C library. */
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

/* --- Windows' imports: KERNEL32, GDI32, USER32 and DDRAW alone --------------- */

/* Handles are DWORDs here, as in the Glide DLLs. */
typedef struct ogl_rect {
    LONG left, top, right, bottom;
} ogl_rect_t;

typedef struct ogl_point {
    LONG x, y;
} ogl_point_t;

DLLIMPORT DWORD STDCALL TlsAlloc(void);
DLLIMPORT BOOL STDCALL  TlsFree(DWORD index);
DLLIMPORT void *STDCALL TlsGetValue(DWORD index);
DLLIMPORT BOOL STDCALL  TlsSetValue(DWORD index, void *value);
DLLIMPORT DWORD STDCALL GetProcessHeap(void);
DLLIMPORT void *STDCALL HeapAlloc(DWORD heap, DWORD flags, DWORD bytes);
DLLIMPORT void *STDCALL HeapReAlloc(DWORD heap, DWORD flags, void *p, DWORD bytes);
DLLIMPORT BOOL STDCALL  HeapFree(DWORD heap, DWORD flags, void *p);
DLLIMPORT void STDCALL  Sleep(DWORD ms);
DLLIMPORT void STDCALL  SetLastError(DWORD error);
DLLIMPORT DWORD STDCALL CreateDCA(const char *driver, const char *device, const char *output, const void *devmode);
DLLIMPORT int STDCALL   ExtEscape(DWORD dc, int escape, int in_bytes, const char *in, int out_bytes, char *out);
DLLIMPORT BOOL STDCALL  DeleteDC(DWORD dc);
DLLIMPORT DWORD STDCALL WindowFromDC(DWORD dc);
DLLIMPORT BOOL STDCALL  GetClientRect(DWORD hwnd, ogl_rect_t *rect);
DLLIMPORT BOOL STDCALL  ClientToScreen(DWORD hwnd, ogl_point_t *point);
DLLIMPORT BOOL STDCALL  IsWindow(DWORD hwnd);
DLLIMPORT HRESULT STDCALL DirectDrawCreate(void *guid, void **dd, void *outer);
/* The diagnostics' (diag.c): the files' paths, the fault catcher. */
typedef LONG(STDCALL *ogl_filter_t)(void *exception_pointers);
DLLIMPORT DWORD STDCALL        GetModuleFileNameA(DWORD module, char *path, DWORD size);
DLLIMPORT DWORD STDCALL        GetSystemDirectoryA(char *path, DWORD size);
DLLIMPORT ogl_filter_t STDCALL SetUnhandledExceptionFilter(ogl_filter_t filter);
DLLIMPORT BOOL STDCALL         IsBadReadPtr(const void *p, DWORD bytes);

#define HEAP_ZERO_MEMORY   0x00000008
#define QUERYESCSUPPORT    8
#define ERROR_INVALID_DATA 13 /* SetLastError's, for WGL_EXT_swap_control's refusal */

/* --- The card (main.c) -------------------------------------------------------- */

/* As main.c found it: the shared block, BAR0, video memory (gpuenv.h's
   names, which driver32/gpu.c uses too). */
extern vgpu98_shared_t *sb;
extern volatile DWORD  *regs;
extern uint8_t         *vram;
/* PROFILE.VSYNC: the SWAP's OVERRIDE flag is the card's. */
extern int ogl_vsync_overrides;

/* 1 if the VGPU98 2000 is there and its GPU draws OpenGL (PROFILE.OGL with
   3D), found once a process, at the first call that needs it. */
int ogl_card(void);

/* The process's heap: zeroed blocks; NULL when there is no room. */
void *ogl_alloc(DWORD bytes);
void *ogl_realloc(void *p, DWORD bytes);
void  ogl_free(void *p);

/* --- Pixel formats (main.c) --------------------------------------------------- */

/* The formats for the desktop's depth now, double-buffered first; 0 at a
   depth with none (8 bpp). Each is a drawable format
   (VGPU98_OGL_COLOR_* | depth << VGPU98_OGL_DEPTH_SHIFT | VGPU98_OGL_DOUBLE). */
int   ogl_formats(DWORD bpp, const DWORD **formats);
void  ogl_describe(DWORD format, ogl_pfd_t *pfd);
/* A drawable format's colour, alpha, depth and stencil bits. */
int   ogl_format_bits(DWORD format, int which);
#define OGL_BITS_RED     0
#define OGL_BITS_GREEN   1
#define OGL_BITS_BLUE    2
#define OGL_BITS_ALPHA   3
#define OGL_BITS_DEPTH   4
#define OGL_BITS_STENCIL 5

/* --- Windows and their drawables (device.c) ---------------------------------- */

/* A window with a pixel format set: the drawable the device keeps for it. */
typedef struct ogl_window {
    DWORD hwnd;           /* 0: free */
    int   pixel_format;   /* 1-based, as the program set it */
    DWORD format;         /* the drawable format */
    DWORD drawable;       /* the device's ID, 0 until a context is made current on it */
    DWORD w, h;           /* the drawable's size as last sent */
    /* At most two swaps in flight: the fences of the last two. */
    DWORD swap_fence[2];
    /* Drawing into the front buffer since it was last shown, and when it
       was last shown (16 ms). */
    int   front_drawn;
    DWORD front_shown_ms;
    /* The front image holds the window's pixels (loaded from video memory,
       or the last frame swapped); 0 after the drawable is made or resized,
       and at wglMakeCurrent. */
    int   front_loaded;
    /* WGL_EXT_swap_control: the vertical blanks SwapBuffers waits for (1
       until the program sets it, the extension's default). */
    int   swap_interval;
    /* Diagnostics: the swap interval's decision last said (diag.c) */
    DWORD swap_told;
} ogl_window_t;

#define OGL_WINDOWS 32

/* The window of a DC, with its pixel format; NULL for none. */
ogl_window_t *ogl_window_of(DWORD dc);
ogl_window_t *ogl_window_find(DWORD hwnd);
/* Sets a window's pixel format: 0 if the DC has no window or no room. */
int           ogl_window_set_format(DWORD dc, int pixel_format, DWORD format);

/* --- Share groups and contexts (main.c, device.c) ------------------------------ */

typedef struct ogl_group {
    DWORD id;     /* the device's */
    int   refs;   /* contexts in it */
    int   opened; /* the device has it */
    void *lists;    /* its display lists (lists.c) */
    void *textures; /* its texture names (texobj.c) */
    /* The textures' IDs on the device in use (texobj.c): a bit each, 1 to
       VGPU98_OGL_TEXTURES - 1, the group's own (never wrapping onto a live one) */
    DWORD tex_ids[VGPU98_OGL_TEXTURES / 32];
} ogl_group_t;

#define OGL_BATCH_DWORDS 16384 /* 64 KB */
#define OGL_CONTEXTS     64

typedef struct ogl_state ogl_state_t;

typedef struct ogl_ctx {
    DWORD         id;    /* the device's */
    int           slot;  /* in the process's table: the DHGLRC is slot + 1 */
    int           pixel_format;
    DWORD         format; /* the drawable format of its pixel format */
    ogl_group_t  *group;
    ogl_window_t *win;    /* the window it is current on, NULL */
    DWORD         thread; /* the thread it is current on, 0 */
    int           made_current; /* once: the viewport and scissor box were set to the window */
    int           opened;       /* the device has it */

    /* The batch */
    DWORD *batch;
    DWORD  batch_n;
    DWORD  last_fence;
    DWORD  epoch; /* sb->state_epoch as the last batch went: a change may have lost what went */
    int    synced; /* a batch with its objects went (before it, nothing on the device can be lost) */
    DWORD  bound; /* the drawable the device binds it to, as last sent */

    /* The device's registers: as the ICD has them, as sent (recorded into a
       batch), as the device had them before the batch being recorded (what
       goes again after a restart), and the run changed since sent */
    DWORD reg[VGPU98_OGL_R_COUNT];
    DWORD sent[VGPU98_OGL_R_COUNT];
    DWORD base[VGPU98_OGL_R_COUNT];
    DWORD dirty_lo, dirty_hi;

    ogl_state_t *gl; /* GL's state (state.h) */
} ogl_ctx_t;

/* The calling thread's current context, NULL for none: for GL's entry
   points, quick while one thread alone has a context current. */
ogl_ctx_t *ogl_current(void);
/* The same from the thread's own slot, always: for the Drv* exports, which
   any thread may call. */
ogl_ctx_t *ogl_thread_current(void);
extern ogl_ctx_t *ogl_ctx_table[OGL_CONTEXTS];

/* --- The batch and the device (device.c) ---------------------------------------- */

/* Room in the context's batch for a packet of `len` payload dwords: its
   payload, which the caller fills (the batch is sent first when full).
   NULL if the packet is longer than any batch. */
DWORD *ogl_cmd(ogl_ctx_t *c, DWORD op, DWORD len);
/* The batch to the ring: a FENCE ends it; its value is returned (0 when
   nothing went). While the GPU is held, the batch waits. */
DWORD  ogl_flush(ogl_ctx_t *c);
/* Waits for a fence. 1 when it completed. */
int    ogl_wait(DWORD fence);
/* Everything recorded done (glFinish). */
void   ogl_finish(ogl_ctx_t *c);

/* A register changed: sent before the next command that draws. */
void   ogl_reg(ogl_ctx_t *c, DWORD index, DWORD value);
void   ogl_regf(ogl_ctx_t *c, DWORD index, float value);
void   ogl_regs(ogl_ctx_t *c, DWORD index, const DWORD *values, DWORD n);
/* The changed registers into the batch (STATE). */
void   ogl_sync(ogl_ctx_t *c);

/* The device's objects: the context's group and itself (opened), and the
   window's drawable at the window's size now (resized when it changed),
   bound to the context. 0 when the window has no drawable (no room). */
int    ogl_objects(ogl_ctx_t *c);
/* The context's close (and its group's, with its last context), at
   DrvDeleteContext and at the DLL's detach: `detaching` waits for nothing. */
void   ogl_close(ogl_ctx_t *c, int detaching);
/* The window's client size now, 0 when it has none. */
int    ogl_client_size(DWORD hwnd, DWORD *w, DWORD *h);
/* SwapBuffers: the back buffer to the front and the window. */
int    ogl_swap(ogl_window_t *win);
/* The front buffer drawn into: shown at glFlush, glFinish and every 16 ms. */
void   ogl_front_show(ogl_ctx_t *c, int now);
/* The window's front image takes what video memory shows of it, unless it
   holds it already (before drawing into or reading the front buffer). */
void   ogl_front_load(ogl_ctx_t *c);
/* The DLL's detach: every object closed on the device, nothing waited for;
   `ending`: the process ends (a killed thread may hold the ring's lock). */
void   ogl_detach(int ending);

/* An ID for a new device object: the process ID's low bits and a count. */
DWORD  ogl_new_id(void);

/* The DRAW being built (draw.c) into the batch, before any other command. */
void   ogl_draw_flush(ogl_ctx_t *c);

/* The staging surface in video memory that read-backs are written into:
   its rows OGL_STAGING_STRIDE bytes apart, OGL_STAGING_ROWS
   of them; its offset in video memory. NULL when there is none. */
#define OGL_STAGING_STRIDE 8448 /* 2112 pixels of 4 bytes: a 2048 level with its border */
#define OGL_STAGING_ROWS   256
uint8_t *ogl_staging(DWORD *offset);

/* --- The GL entry points (table.c) ---------------------------------------------- */

/* DrvSetContext's answer: a count, then the 336 functions (glfuncs.h). */
#define OGL_TABLE_ENTRIES 336
typedef struct ogl_proc_table {
    int   count;
    void *entry[OGL_TABLE_ENTRIES];
} ogl_proc_table_t;

extern const ogl_proc_table_t ogl_table;

/* --- The extensions (ext.c) ------------------------------ */

#define OGL_EXT_SWAP_CONTROL           0x01 /* WGL_EXT_swap_control */
#define OGL_EXT_COMPILED_VERTEX_ARRAY  0x02
#define OGL_EXT_SGIS_MULTITEXTURE      0x04
#define OGL_EXT_ARB_MULTITEXTURE       0x08
#define OGL_EXT_POINT_PARAMETERS       0x10
#define OGL_EXT_PALETTED_TEXTURE       0x20
#define OGL_EXT_SHARED_TEXTURE_PALETTE 0x40
#define OGL_EXT_EDGE_CLAMP             0x80 /* GL_EXT_texture_edge_clamp and GL_SGIS_texture_edge_clamp (command set 2.7) */

/* The extensions offered for the device's command set (at the card's
   discovery); whether one is; GL_EXTENSIONS; DrvGetProcAddress's answer
   (NULL for a name no offered extension has). */
void        ogl_ext_init(DWORD command_set);
int         ogl_ext_on(DWORD ext);
const char *ogl_ext_string(void);
void       *ogl_ext_proc(const char *name);
/* The device's registers: command set 2.6's on a 2.6 device, else 2.5's
   (an older device drops a STATE past them). */
DWORD       ogl_reg_count(void);

/* --- Diagnostics (diag.c) ------------------------- */

/* diag_on: Diagnostics "OpenGL"; diag_events: any level but Off (both 0
   until the card is found). */
extern int diag_on;
extern int diag_events;

/* A line: an event (any level but Off) or a detail (OpenGL alone). */
void ogl_event(const char *fmt, ...);
void ogl_dbg(const char *fmt, ...);

/* The DLL's instance (DllMain); the lines to BAR0 at r from now on, under
   the block's log lock (NULL: a lock of the DLL's own, for another build's
   block), the level read from DEBUG; no more lines. */
void ogl_diag_instance(DWORD dll_instance);
void ogl_diag_start(volatile DWORD *r, vgpu98_shared_t *s);
void ogl_diag_stop(void);
/* At the card's finding: the stamps (a stale driver is an event), and at
   OpenGL the files; the loaded line. */
void ogl_diag_check(const vgpu98_escinfo_t *info);
void ogl_diag_loaded(DWORD command_set);
/* At OpenGL, a window's pixel format as set (its drawable format). */
void ogl_diag_pixel_format(DWORD hwnd, int pixel_format, DWORD format);
/* At OpenGL: a GL error, by the function that raised it; a name
   DrvGetProcAddress does not have; a pixel format refused. */
void ogl_diag_error(const char *func, GLenum e);
void ogl_diag_miss(const char *name);
void ogl_diag_format(DWORD hdc, LONG pixel_format, int formats, DWORD bpp);

/* What became of a window's swap interval at a SwapBuffers */
#define OGL_SWAP_AT_ONCE 0 /* interval 0 */
#define OGL_SWAP_SENT    1
#define OGL_SWAP_DESKTOP 2 /* not sent: the window does not cover the screen */
#define OGL_SWAP_COVERED 3 /* not sent: covered in part */
#define OGL_SWAP_CURSOR  4 /* not sent: the cursor kept out */
void ogl_diag_swap(ogl_window_t *w, DWORD interval, int why, DWORD rects);
/* Each SwapBuffers: the report every 5 seconds and the slow path's finding. */
void ogl_diag_swapped(void);

/* A unit's texturing on with a texture that cannot draw (as last chosen
   for the registers) */
typedef struct ogl_untex {
    int    on;
    DWORD  name, w, h, ifmt, levels;
    GLenum filter;
} ogl_untex_t;

/* The counters: plain increments on the hot paths, the report's from
   `swaps` on (started again at each report) */
typedef struct ogl_diag {
    ogl_untex_t untex[2];
    DWORD       swaps;
    DWORD       prims_dev;    /* glBegin/glEnd primitives the device draws */
    DWORD       prims_cpu[3]; /* the ICD's own path's: materials, polygon modes, stippled strips */
    DWORD       arrays_direct;
    DWORD       lists;     /* display lists called */
    DWORD       errors[7]; /* GL errors by code, from GL_INVALID_ENUM; [6] another */
} ogl_diag_t;

extern ogl_diag_t ogl_dg;

/* A draw with a unit's texturing on and a texture that cannot draw (texobj.h's
   ogl_diag_untextured notes it): the incomplete texture's finding. */
void ogl_diag_untextured_draw(void);

/* The fault catcher: in place when the card is found (any level but Off),
   gone at a FreeLibrary. */
void ogl_fault_catcher(void);
void ogl_fault_catcher_end(void);

#endif /* VGPU98_ICD_H */

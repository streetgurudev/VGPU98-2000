/*
 * The Glide DLLs (glide2x.dll, glide3x.dll): what both share. Glide is the
 * Voodoo cards' own 3D interface; these DLLs give it to programs on the
 * VGPU98 2000, answering as a Voodoo3 (spec/glide.md).
 *
 * Each DLL is an ordinary one, loaded into the program that uses it, with
 * its data per process: Glide's state belongs to the program, as on a
 * Voodoo3. It finds the card through the display driver's escape at its
 * first Glide call, and writes to the host's log as the DirectX DLL does;
 * at Diagnostics "Glide", with its own checks and a record of the calls.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_GLIDEDLL_H
#define VGPU98_GLIDEDLL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "shared.h"
#include "stamp.h"
#include "vid_vgpu98_card.h"

#define STDCALL __attribute__((stdcall))
#ifndef DLLIMPORT /* the host test (tools/tests/test_glide.c) defines it empty, for its stand-ins */
#    define DLLIMPORT __declspec(dllimport)
#endif

/* Glide's calling convention: every export is stdcall, its symbol decorated
   with its arguments' bytes (_grDrawTriangle@12), as Glide programs import it. */
#define FX_CALL STDCALL

/* Glide's base types, as its headers name them. */
typedef uint8_t  FxU8;
typedef int8_t   FxI8;
typedef uint16_t FxU16;
typedef int16_t  FxI16;
typedef uint32_t FxU32;
typedef int32_t  FxI32;
typedef int32_t  FxBool;
typedef float    FxFloat;

#define FXFALSE 0
#define FXTRUE  1

/* rt.c (driver32/): what gcc expects of a C library. */
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

/* KERNEL32, GDI32, USER32 and DDRAW: the DLLs' only imports, as a Voodoo3's
   Glide imports from them. Handles are DWORDs here. */
DLLIMPORT DWORD STDCALL GetModuleFileNameA(DWORD module, char *name, DWORD size); /* module 0: the program */
DLLIMPORT void STDCALL  Sleep(DWORD ms);
DLLIMPORT DWORD STDCALL GetSystemDirectoryA(char *path, DWORD size);
DLLIMPORT char *STDCALL GetEnvironmentStringsA(void);
DLLIMPORT BOOL STDCALL  FreeEnvironmentStringsA(char *block);
DLLIMPORT DWORD STDCALL CreateDCA(const char *driver, const char *device, const char *output, const void *devmode);
DLLIMPORT int STDCALL   ExtEscape(DWORD dc, int escape, int in_bytes, const char *in, int out_bytes, char *out);
DLLIMPORT BOOL STDCALL  DeleteDC(DWORD dc);

/* USER32 and DDRAW: the program's window and the screen's mode, as a
   Voodoo3's Glide takes them. */
DLLIMPORT DWORD STDCALL   GetActiveWindow(void);
DLLIMPORT DWORD STDCALL   GetForegroundWindow(void);
DLLIMPORT HRESULT STDCALL DirectDrawCreate(void *guid, void **dd, void *outer);

/* USER32: a window described in the log. */
typedef struct glide_rect {
    LONG left, top, right, bottom;
} glide_rect_t;
#define GWL_EXSTYLE   (-20)
#define WS_EX_TOPMOST 0x00000008
DLLIMPORT int STDCALL   GetClassNameA(DWORD hwnd, char *name, int size);
DLLIMPORT int STDCALL   GetWindowTextA(DWORD hwnd, char *text, int size);
DLLIMPORT BOOL STDCALL  GetWindowRect(DWORD hwnd, glide_rect_t *rect);
DLLIMPORT LONG STDCALL  GetWindowLongA(DWORD hwnd, int index);
DLLIMPORT DWORD STDCALL GetWindowThreadProcessId(DWORD hwnd, DWORD *process);

/* KERNEL32: the fault catcher. */
typedef LONG(STDCALL *glide_filter_t)(void *exception_pointers);
DLLIMPORT glide_filter_t STDCALL SetUnhandledExceptionFilter(glide_filter_t filter);
DLLIMPORT BOOL STDCALL           IsBadReadPtr(const void *p, DWORD bytes);

#define QUERYESCSUPPORT 8

/* --- The Voodoo3 the DLLs answer as ------------------------------------- */

/* A Voodoo3 3000: 16 MB, two TMUs, as its own Glide reports it (the
   Voodoo3 1.07.00 driver's, read for facts): Glide 2 gives the frame buffer
   what the two TMUs' 2 MB each leave, and both of Glide's version strings
   are that release's. Glide's numbers are its own, whatever the card's
   video memory. */
#define VOODOO3_MEMORY_MB   16
#define VOODOO3_TMUS        2
#define VOODOO3_TMU_MB      2
#define VOODOO3_FB_MB       (VOODOO3_MEMORY_MB - VOODOO3_TMUS * VOODOO3_TMU_MB)
#define VOODOO3_FBI_REV     0x11002 /* Glide 2: 1000h a Banshee-class chip, 10000h a Voodoo3 */
#define VOODOO3_TMU_REV     0x11002
#define VOODOO3_REVISION    0x11100 /* Glide 3's GR_REVISION_FB and GR_REVISION_TMU */
#define VOODOO3_GLIDE2      "2.61.00.0658"
#define VOODOO3_GLIDE3      "3.10.00.0658"

/* --- The card (card.c) ---------------------------------------------------- */

/* Each DLL's own: "VGPU98 glide2x <stamp>" and "Glide 2.61", say. */
extern const char glide_tag[];
extern const char glide_api[];

/* 1 if the VGPU98 2000 is there, found at the first call in the process
   (the display driver's escape, the shared block, BAR0). Without it the DLL
   reports no board, as a Voodoo3's Glide does without its card. */
int glide_card(void);

/* --- The log (card.c) ----------------------------------------------------- */

/* Lines start "<tag> <PROGRAM>: ". An event line is shown at every level but
   Off; a Glide line only at Diagnostics "Glide". */
void glide_event(const char *fmt, ...);
void glide_dbg(const char *fmt, ...);

/* An export's calls in the process, for the call record: each export has
   one, put on the process's list at its first call (its "first call" line
   at Glide). The count is a plain increment: two threads at once may lose
   one. */
typedef struct glide_export {
    const char          *name;
    int                  does_nothing; /* 1 for an export that does nothing yet */
    DWORD                calls;
    volatile DWORD       listed;
    struct glide_export *next;
} glide_export_t;

/* `args`: the export's first four argument dwords, for the recorder of the
   last calls (a stdcall export's are above its frame's return address). */
void glide_called(glide_export_t *e, const DWORD *args);

#define GLIDE_EXPORT(name, nothing)                                                        \
    do {                                                                                   \
        static glide_export_t export_ = { #name, nothing, 0, 0, NULL };                    \
        glide_called(&export_, (const DWORD *) __builtin_frame_address(0) + 2);            \
    } while (0)

/* Every export starts with one of these: an export that does something,
   or one that does nothing yet,
   touching none of its arguments and returning 0, FXFALSE or NULL. */
#define CALLED(name)  GLIDE_EXPORT(name, 0)
#define NOT_YET(name) GLIDE_EXPORT(name, 1)

/* At Glide, the process's calls so far: the exports called, busiest first,
   those that do something and those that do nothing yet, each with its
   count since the DLL loaded. At grGlideShutdown, and when the DLL unloads
   (ExitProcess unloads it too; a program ended by TerminateProcess or a
   fault writes none) if calls came since. One record at a time. */
void glide_record(void);

/* grGlideShutdown came: the unload writes no recorder of the last calls. */
void glide_shutdown_seen(void);

/* The names of Glide's screen resolutions and refresh rates (the same in
   Glide 2 and 3), for the log: "640x480", "60 Hz"; "?" for others. */
const char *glide_resolution(FxU32 res);
const char *glide_refresh(FxU32 rate);

/* grSstWinOpen refused, until Glide draws: an event line with what was
   asked, for the process's first three (at Glide, its first 64), then one
   saying no more come (a program may retry every frame). */
void glide_refuse_window(FxU32 hwnd, FxU32 res, FxU32 refresh, FxU32 color_format, FxU32 origin, FxU32 col_buffers,
                         FxU32 aux_buffers);

#endif /* VGPU98_GLIDEDLL_H */

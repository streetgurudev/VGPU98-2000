/*
 * vgpu98cp.cpl: the VGPU98 2000's own settings.
 * Two pages, VGPU98 Color (gamma) and VGPU98 3D (the V-sync
 * overrides), reached three ways: the tabs under Display Properties >
 * Settings > Advanced (a property sheet extension), the Control Panel's
 * VGPU98 2000 icon (CPlApplet), and vgpu98_apply, which rundll32 runs when
 * Windows starts to give the card the saved settings. Internal interfaces
 * between its source files.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_CPL_H
#define VGPU98_CPL_H

#include <stdarg.h>

#include "win.h"
#include "ramp.h"

#define CPL_NAME "VGPU98 2000"

/* rt.c (the DirectX DLL's runtime) */
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

/* --- main.c ---------------------------------------------------------------- */

extern HINSTANCE cp_instance;
/* comctl32 counts the module's pages here (pcRefParent): the DLL stays
   loaded while one lives. */
extern UINT cp_page_refs;

/* --- card.c: the card, and lines to 86Box's log ---------------------------- */

/* What the display driver says: its card, its build stamp, the card's
   gamma (PROFILE.GAMMA) and V-sync overrides (PROFILE.VSYNC). Looked up
   once a process. */
typedef struct cp_card_t {
    int  found;    /* the VGPU98 2000's display driver */
    int  bar0;     /* its registers reached (the shared block's mapping) */
    int  gamma;    /* the card has the gamma table */
    int  vsync;    /* the card has VSYNC_CONTROL */
    char stamp[16];
} cp_card_t;

const cp_card_t *cp_card(void);
/* The screen's refresh rate now, in Hz; 0 if the display driver does not
   say. */
DWORD cp_refresh(void);
/* VSYNC_CONTROL written, and what it reads back; 0 without the register. */
DWORD cp_vsync_write(DWORD control);
DWORD cp_vsync_read(void);
/* A line to the log at every Diagnostics level but Off: "VGPU98 control
   <stamp> <program>: ...". %s %c %d %u %x, as the other DLLs' lines. */
void cp_event(const char *fmt, ...);

/* --- settings.c: the saved settings, and the start-up entry ----------------- */

/* The V-sync overrides: a mode an API (VGPU98_VSYNC_APP, _ON, _OFF), in
   VSYNC_CONTROL's order and the page's. */
#define CP_APIS 3
typedef struct cp_vsync_t {
    DWORD mode[CP_APIS]; /* Direct3D, OpenGL, Glide */
} cp_vsync_t;

extern const char *const cp_api_names[CP_APIS]; /* "Direct3D", "OpenGL", "Glide" */
extern const char *const cp_mode_names[3];      /* for the log: "application's choice", ... */

/* Gamma's saved settings, or the defaults; saved. */
void  cp_load(ramp_settings_t *s);
void  cp_save(const ramp_settings_t *s);
void  cp_vsync_defaults(cp_vsync_t *v);
int   cp_vsync_is_default(const cp_vsync_t *v);
/* The V-sync overrides' saved settings, or the defaults; saved. */
void  cp_vsync_load(cp_vsync_t *v);
void  cp_vsync_save(const cp_vsync_t *v);
/* The settings as VSYNC_CONTROL has them, and back. */
DWORD cp_vsync_control(const cp_vsync_t *v);
void  cp_vsync_from_control(cp_vsync_t *v, DWORD control);
/* Both pages' saved settings to the card (rundll32's at start-up). */
void  cp_apply_saved(void);

/* --- page.c: the VGPU98 Color page; sync.c: the VGPU98 3D page -------------- */

/* A page for a property sheet: `way` names how it was opened, for the log
   ("the Advanced tab", "the Control Panel"). 0 if its template did not
   fit, and the page is left out. */
int cp_color_page(PROPSHEETPAGEA *page, const char *way);
int cp_3d_page(PROPSHEETPAGEA *page, const char *way);
/* Text for the pages: a number or a string at p, NUL-ended; the end. */
char *cp_put_uint(char *p, unsigned v);
char *cp_put_text(char *p, const char *s);

#endif /* VGPU98_CPL_H */

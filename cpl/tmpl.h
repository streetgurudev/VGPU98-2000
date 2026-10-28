/*
 * vgpu98cp.cpl: dialog templates built in memory.
 * The linker writes no dialog resources, so each page's DLGTEMPLATE is
 * written here into a buffer of its own, which must outlive the property
 * sheet's page (comctl32 makes the dialog when its tab is first shown).
 * Plain C, shared with the host test (tools/tests/test_tmpl.c).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_CPL_TMPL_H
#define VGPU98_CPL_TMPL_H

#include <stddef.h>
#include <stdint.h>

#define TMPL_WORDS 2048 /* a page's buffer: 4 KB */

typedef struct tmpl_t {
    uint16_t *buf;   /* dword-aligned */
    size_t    words; /* its room */
    uint16_t *p;
    uint16_t *count; /* the header's item count */
    int       items;
    int       full;  /* it ran out of room: the template is not used */
} tmpl_t;

/* The header: a DLGTEMPLATE of `style` (DS_SETFONT added: the font follows
   the title), no menu, the dialog class, `title`, then the font's point size
   and face name. `buf` must start on a dword, as the items are aligned from
   its start. */
void        tmpl_begin(tmpl_t *t, uint16_t *buf, size_t words, uint32_t style, int cx, int cy, const char *title,
                       int point, const char *face);
/* An item on a dword: a DLGITEMTEMPLATE of `style` (WS_CHILD and WS_VISIBLE
   added), its class an atom (`cls` NULL) or a name, its text, no creation
   data. */
void        tmpl_item(tmpl_t *t, uint32_t style, uint32_t ex_style, int x, int y, int cx, int cy, int id, uint16_t atom,
                      const char *cls, const char *text);
/* The template, or NULL if it did not fit. */
const void *tmpl_end(tmpl_t *t);

#endif /* VGPU98_CPL_TMPL_H */

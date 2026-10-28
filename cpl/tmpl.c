/*
 * vgpu98cp.cpl: dialog templates built in memory.
 * The layout is Windows' DLGTEMPLATE and DLGITEMTEMPLATE: the header's
 * style, extended style, item count, place and size, then its menu, class
 * and title, then with DS_SETFONT the font's point size and face; each item
 * on a dword, its style, extended style, place, size and id, then its class
 * (FFFFh and an atom, or a name), its text, and the bytes of creation data.
 * Strings are UTF-16 and NUL-ended (ASCII here).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "tmpl.h"

#define WS_CHILD   0x40000000u
#define WS_VISIBLE 0x10000000u
#define DS_SETFONT 0x00000040u

static void
put_word(tmpl_t *t, uint16_t v)
{
    if ((size_t) (t->p - t->buf) >= t->words) {
        t->full = 1;
        return;
    }
    *t->p++ = v;
}

static void
put_dword(tmpl_t *t, uint32_t v)
{
    put_word(t, (uint16_t) v);
    put_word(t, (uint16_t) (v >> 16));
}

static void
put_text(tmpl_t *t, const char *s)
{
    while (*s)
        put_word(t, (uint8_t) *s++);
    put_word(t, 0);
}

void
tmpl_begin(tmpl_t *t, uint16_t *buf, size_t words, uint32_t style, int cx, int cy, const char *title, int point,
           const char *face)
{
    t->buf   = buf;
    t->words = words;
    t->p     = buf;
    t->items = 0;
    t->full  = 0;
    put_dword(t, style | DS_SETFONT); /* the font is always written */
    put_dword(t, 0);
    t->count = t->p;
    put_word(t, 0); /* the items, at the end */
    put_word(t, 0);
    put_word(t, 0);
    put_word(t, (uint16_t) cx);
    put_word(t, (uint16_t) cy);
    put_word(t, 0); /* no menu */
    put_word(t, 0); /* the dialog class */
    put_text(t, title);
    put_word(t, (uint16_t) point);
    put_text(t, face);
}

void
tmpl_item(tmpl_t *t, uint32_t style, uint32_t ex_style, int x, int y, int cx, int cy, int id, uint16_t atom,
          const char *cls, const char *text)
{
    if ((t->p - t->buf) & 1)
        put_word(t, 0); /* each item on a dword */
    put_dword(t, style | WS_CHILD | WS_VISIBLE);
    put_dword(t, ex_style);
    put_word(t, (uint16_t) x);
    put_word(t, (uint16_t) y);
    put_word(t, (uint16_t) cx);
    put_word(t, (uint16_t) cy);
    put_word(t, (uint16_t) id);
    if (cls)
        put_text(t, cls);
    else {
        put_word(t, 0xFFFF);
        put_word(t, atom);
    }
    put_text(t, text);
    put_word(t, 0); /* no creation data */
    t->items++;
}

const void *
tmpl_end(tmpl_t *t)
{
    if (t->full)
        return NULL;
    *t->count = (uint16_t) t->items;
    return t->buf;
}

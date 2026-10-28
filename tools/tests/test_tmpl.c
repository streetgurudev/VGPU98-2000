/*
 * The control program's dialog templates (cpl/tmpl.c),
 * walked from their bytes by a reader of the test's own as
 * Windows reads a DLGTEMPLATE: the header and its font, the item count, each
 * item on a dword from the buffer's start with its class (an atom or a
 * name), its text and its creation data, and where the template ends. Then
 * room that runs out anywhere (no template, and nothing written past the
 * room), room of exactly the template's size, and a template the size of a
 * real page in a page's buffer.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tmpl.h"

static int failures;

#define CHECK(cond, ...)                                 \
    do {                                                 \
        if (!(cond)) {                                   \
            fprintf(stderr, "test_tmpl:%d: ", __LINE__); \
            fprintf(stderr, __VA_ARGS__);                \
            fputc('\n', stderr);                         \
            failures++;                                  \
        }                                                \
    } while (0)

/* Windows' values, written out here rather than taken from cpl/win.h. */
#define WS_CHILD             0x40000000u
#define WS_VISIBLE           0x10000000u
#define WS_DISABLED          0x08000000u
#define WS_CAPTION           0x00C00000u
#define WS_VSCROLL           0x00200000u
#define WS_GROUP             0x00020000u
#define WS_TABSTOP           0x00010000u
#define WS_EX_CLIENTEDGE     0x00000200u
#define WS_EX_NOPARENTNOTIFY 0x00000004u
#define DS_3DLOOK            0x0004u
#define DS_SETFONT           0x0040u
#define BS_AUTOCHECKBOX      0x0003u
#define BS_GROUPBOX          0x0007u
#define BS_AUTORADIOBUTTON   0x0009u
#define SS_RIGHT             0x0002u
#define SS_BLACKFRAME        0x0007u
#define CBS_DROPDOWNLIST     0x0003u
#define TBS_AUTOTICKS        0x0001u
#define BUTTON               0x0080u /* the class atoms */
#define STATIC               0x0082u
#define COMBOBOX             0x0085u

#define CANARY    0xA5A5u
#define SPARE     64 /* canary words after a page's buffer */
#define MAX_ITEMS 64

/* The buffer the templates are built in, on a dword, canaries after it. */
static union {
    uint32_t d[(TMPL_WORDS + SPARE) / 2];
    uint16_t w[TMPL_WORDS + SPARE];
} mem;

typedef struct item_t {
    uint32_t    style; /* as given to tmpl_item */
    uint32_t    ex_style;
    int         x, y, cx, cy, id;
    uint16_t    atom;
    const char *cls; /* NULL for the atom */
    const char *text;
} item_t;

typedef struct page_t {
    const char   *what;
    uint32_t      style;
    int           cx, cy;
    const char   *title;
    int           point;
    const char   *face;
    const item_t *items;
    int           n;
} page_t;

/* What the walk found, in words from the buffer's start. */
typedef struct walk_t {
    size_t header;           /* the header's end */
    size_t start[MAX_ITEMS]; /* each item's first word, after any padding */
    size_t stop[MAX_ITEMS];  /* the word after its creation data */
    size_t end;              /* the word after the template */
    int    padded;           /* items after a padding word */
    int    flush;            /* items already on a dword */
} walk_t;

/* The page through the builder into `words` of buf: tmpl_end's answer. */
static const void *
build(const page_t *pg, uint16_t *buf, size_t words)
{
    tmpl_t t;
    int    i;

    tmpl_begin(&t, buf, words, pg->style, pg->cx, pg->cy, pg->title, pg->point, pg->face);
    for (i = 0; i < pg->n; i++) {
        const item_t *it = &pg->items[i];

        tmpl_item(&t, it->style, it->ex_style, it->x, it->y, it->cx, it->cy, it->id, it->atom, it->cls, it->text);
    }
    return tmpl_end(&t);
}

/* The page's size in words by the layout, worked from its strings. */
static size_t
expected_words(const page_t *pg)
{
    /* Style 2, extended style 2, count, x, y, cx, cy, menu, class: 11. */
    size_t w = 11 + strlen(pg->title) + 1 + 1 + strlen(pg->face) + 1;
    int    i;

    for (i = 0; i < pg->n; i++) {
        w += w & 1; /* to a dword */
        /* Style 2, extended style 2, x, y, cx, cy, id: 9; then the class,
           the text and the creation data. */
        w += 9 + (pg->items[i].cls ? strlen(pg->items[i].cls) + 1 : 2) + strlen(pg->items[i].text) + 1 + 1;
    }
    return w;
}

/* The test's own reader: little-endian words from bytes, never past len. */
typedef struct reader_t {
    const uint8_t *b;
    size_t         len;  /* the bytes it may read */
    size_t         at;   /* bytes from the buffer's start */
    int            over; /* it would have read past len */
} reader_t;

static uint16_t
rd_peek(const reader_t *r)
{
    if (r->at + 2 > r->len)
        return 0;
    return (uint16_t) (r->b[r->at] | (r->b[r->at + 1] << 8));
}

static uint16_t
rd_word(reader_t *r)
{
    uint16_t v;

    if (r->at + 2 > r->len) {
        r->over = 1;
        return 0;
    }
    v = rd_peek(r);
    r->at += 2;
    return v;
}

static uint32_t
rd_dword(reader_t *r)
{
    uint32_t lo = rd_word(r);
    uint32_t hi = rd_word(r);

    return lo | (hi << 16);
}

/* A NUL-ended UTF-16 string read to its NUL, as Windows reads it, against
   `want`: `owner`'s (the header's, an item's) `field`. */
static void
rd_string(reader_t *r, const char *what, const char *owner, const char *field, const char *want)
{
    const size_t at  = r->at / 2;
    const size_t n   = strlen(want);
    size_t       i   = 0;
    long         bad = -1;
    uint16_t     c;

    for (;;) {
        c = rd_word(r);
        if (r->over || !c)
            break;
        if ((bad < 0) && ((i >= n) || (c != (uint8_t) want[i])))
            bad = (long) i;
        i++;
    }
    CHECK(!r->over && (i == n) && (bad < 0),
          "%s: %s %s at word %u: %u characters to its NUL, the first wrong at %ld, expected \"%s\" (%u)%s", what, owner,
          field, (unsigned) at, (unsigned) i, bad, want, (unsigned) n, r->over ? ", no NUL" : "");
}

/* A word against `want`. */
static void
rd_check(reader_t *r, const char *what, const char *owner, const char *field, uint32_t want)
{
    const size_t at = r->at / 2;
    uint16_t     v  = rd_word(r);

    CHECK(v == (uint16_t) want, "%s: %s %s at word %u: %04X, expected %04X", what, owner, field, (unsigned) at, v,
          (unsigned) (uint16_t) want);
}

/* The template at tmpl, of at most `room` words, walked from its bytes
   against the page. Returns 1 if it held. */
static int
walk(const char *what, const page_t *pg, const void *tmpl, size_t room, walk_t *w)
{
    const int before = failures;
    reader_t  r;
    uint32_t  v;
    uint16_t  n;
    int       i;

    memset(w, 0, sizeof(*w));
    r.b    = tmpl;
    r.len  = room * 2;
    r.at   = 0;
    r.over = 0;
    CHECK(((uintptr_t) tmpl & 3) == 0, "%s: the template not on a dword", what);

    /* The header, as written: the style with DS_SETFONT (the font is always
       written) but without WS_CHILD or WS_VISIBLE added, the item count, at
       0, 0, no menu, the dialog class. */
    v = rd_dword(&r);
    CHECK(v == (pg->style | DS_SETFONT), "%s: style %08X, expected %08X", what, v, pg->style | DS_SETFONT);
    v = rd_dword(&r);
    CHECK(v == 0, "%s: extended style %08X", what, v);
    n = rd_word(&r);
    CHECK(n == pg->n, "%s: %u items, expected %d", what, n, pg->n);
    rd_check(&r, what, "the header's", "x", 0);
    rd_check(&r, what, "the header's", "y", 0);
    rd_check(&r, what, "the header's", "cx", (uint32_t) pg->cx);
    rd_check(&r, what, "the header's", "cy", (uint32_t) pg->cy);
    rd_check(&r, what, "the header's", "menu", 0);
    rd_check(&r, what, "the header's", "class", 0);
    rd_string(&r, what, "the header's", "title", pg->title);
    /* DS_SETFONT: the point size and the face. */
    rd_check(&r, what, "the header's", "point size", (uint32_t) pg->point);
    rd_string(&r, what, "the header's", "face", pg->face);
    w->header = r.at / 2;

    for (i = 0; (i < n) && (i < pg->n) && (i < MAX_ITEMS) && !r.over; i++) {
        const item_t *it = &pg->items[i];
        char          owner[24];
        uint16_t      c;

        snprintf(owner, sizeof(owner), "item %d's", i + 1);
        if (r.at & 3) {
            v = rd_word(&r);
            CHECK(v == 0, "%s: %s padding %04X", what, owner, v);
            w->padded++;
        } else
            w->flush++;
        CHECK((r.at & 3) == 0, "%s: %s start at byte %u", what, owner, (unsigned) r.at);
        w->start[i] = r.at / 2;
        v           = rd_dword(&r);
        CHECK(v == (it->style | WS_CHILD | WS_VISIBLE), "%s: %s style %08X, expected %08X", what, owner, v,
              it->style | WS_CHILD | WS_VISIBLE);
        v = rd_dword(&r);
        CHECK(v == it->ex_style, "%s: %s extended style %08X, expected %08X", what, owner, v, it->ex_style);
        rd_check(&r, what, owner, "x", (uint32_t) it->x);
        rd_check(&r, what, owner, "y", (uint32_t) it->y);
        rd_check(&r, what, owner, "cx", (uint32_t) it->cx);
        rd_check(&r, what, owner, "cy", (uint32_t) it->cy);
        rd_check(&r, what, owner, "id", (uint32_t) it->id);
        /* The class: FFFFh and an atom, or a name. */
        if (rd_peek(&r) == 0xFFFF) {
            rd_word(&r);
            c = rd_word(&r);
            CHECK(!it->cls && (c == it->atom), "%s: %s class atom %04X, expected %s %04X", what, owner, c,
                  it->cls ? it->cls : "atom", it->atom);
        } else {
            CHECK(it->cls != NULL, "%s: %s class a name, expected atom %04X", what, owner, it->atom);
            rd_string(&r, what, owner, "class", it->cls ? it->cls : "");
        }
        /* The text: a string (FFFFh would be a resource's ordinal). */
        CHECK(rd_peek(&r) != 0xFFFF, "%s: %s text an ordinal", what, owner);
        rd_string(&r, what, owner, "text", it->text);
        rd_check(&r, what, owner, "creation data", 0);
        w->stop[i] = r.at / 2;
    }
    w->end = r.at / 2;
    CHECK(!r.over, "%s: the walk ran past its %u words", what, (unsigned) room);
    return failures == before;
}

static void
fill_canaries(void)
{
    size_t k;

    for (k = 0; k < TMPL_WORDS + SPARE; k++)
        mem.w[k] = CANARY;
}

/* Words from `from` to the end of mem that are not canaries. */
static size_t
written_from(size_t from)
{
    size_t k;
    size_t n = 0;

    for (k = from; k < TMPL_WORDS + SPARE; k++)
        if (mem.w[k] != CANARY)
            n++;
    return n;
}

/* The page built in a page's buffer and walked: tmpl_end gives the buffer,
   the walk ends where the layout says (and at `known` words if given), the
   builder's last word is the one before it and nothing after it changed. */
static void
check_page(const page_t *pg, size_t known, walk_t *w)
{
    const void *tm;
    size_t      want = expected_words(pg);

    fill_canaries();
    tm = build(pg, mem.w, TMPL_WORDS);
    CHECK(tm == mem.w, "%s: tmpl_end gave %p, expected the buffer %p", pg->what, tm, (void *) mem.w);
    if (!tm) {
        memset(w, 0, sizeof(*w));
        return;
    }
    walk(pg->what, pg, tm, TMPL_WORDS, w);
    CHECK(w->end == want, "%s: the walk ends at word %u, the layout at %u", pg->what, (unsigned) w->end,
          (unsigned) want);
    if (known)
        CHECK(w->end == known, "%s: the walk ends at word %u, expected %u", pg->what, (unsigned) w->end,
              (unsigned) known);
    /* The last word (a creation-data word, or the face's NUL) written as 0,
       none after it. */
    CHECK((w->end > 0) && (mem.w[w->end - 1] == 0), "%s: the last word, %u, is %04X", pg->what,
          (unsigned) w->end - 1, (w->end > 0) ? mem.w[w->end - 1] : 0);
    CHECK(!written_from(w->end), "%s: %u words written after the walk's end at %u", pg->what,
          (unsigned) written_from(w->end), (unsigned) w->end);
}

/* The page built into `room` words with canaries after them: no template,
   and no canary written. */
static void
short_room(const page_t *pg, size_t room, const char *where)
{
    const void *tm;
    size_t      hit;

    fill_canaries();
    tm  = build(pg, mem.w, room);
    hit = written_from(room);
    CHECK(!tm && !hit, "%s in %u words (%s): %s, %u words written past the room", pg->what, (unsigned) room, where,
          tm ? "a template" : "NULL", (unsigned) hit);
}

/* A small page, by hand: odd and even strings, each class form, an empty
   text, an id of -1 (IDC_STATIC). Its words: the header's 11, the title 10
   (21), the point size (22), the face 14 (36); then each item at a word
   (padded to a dword or not), its 9 fixed words, its class, text and
   creation word:
     1 at 36:        9 + 2 + 14 ("Vertical sync") + 1 = 26, to 62
     2 at 62:        9 + 2 + 10 ("Direct3D:") + 1     = 22, to 84
     3 at 84:        9 + 2 + 1 ("") + 1               = 13, to 97
     4 at 98 (pad):  9 + 2 + 8 ("OpenGL:") + 1        = 20, to 118
     5 at 118:       9 + 2 + 1 + 1                    = 13, to 131
     6 at 132 (pad): 9 + 18 ("msctls_trackbar32") + 6 ("Gamma") + 1 = 34, to 166
     7 at 166:       9 + 2 + 17 ("Restore Defaults") + 1 = 29, to 195
     8 at 196 (pad): 9 + 7 ("Static") + 17 ("Build 2026-10-27") + 1 = 34, to 230
   230 words, three items padded and five not. */
static const item_t small_items[] = {
    { BS_GROUPBOX, 0, 7, 7, 238, 80, -1, BUTTON, NULL, "Vertical sync" },
    { 0, 0, 14, 22, 50, 8, -1, STATIC, NULL, "Direct3D:" },
    { CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, 70, 20, 120, 60, 1001, COMBOBOX, NULL, "" },
    { 0, 0, 14, 40, 50, 8, -1, STATIC, NULL, "OpenGL:" },
    { CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 70, 38, 120, 60, 1002, COMBOBOX, NULL, "" },
    { TBS_AUTOTICKS | WS_TABSTOP, WS_EX_NOPARENTNOTIFY, 70, 60, 120, 16, 1010, 0, "msctls_trackbar32", "Gamma" },
    /* WS_CHILD already set: the same style. */
    { WS_CHILD | WS_TABSTOP, 0, 170, 196, 75, 14, 1003, BUTTON, NULL, "Restore Defaults" },
    { SS_RIGHT, 0, 7, 205, 150, 8, 0xFFFF, 0, "Static", "Build 2026-10-27" },
};

static const page_t small_page = {
    "the small page", DS_SETFONT | DS_3DLOOK | WS_CHILD | WS_DISABLED | WS_CAPTION, 252, 218, "VGPU98 3D", 8,
    "MS Sans Serif", small_items, (int) (sizeof(small_items) / sizeof(small_items[0])),
};

/* No items, an empty title: the header alone, 11 + 1 + 1 + 7 words. */
static const page_t empty_page = {
    "the empty page", DS_SETFONT, 100, 50, "", 10, "Tahoma", NULL, 0,
};

/* A style without DS_SETFONT: tmpl_begin adds it, as it writes the font. */
static const page_t no_font_page = {
    "the page without DS_SETFONT", DS_3DLOOK, 100, 50, "", 10, "Tahoma", NULL, 0,
};

static void
small(void)
{
    walk_t w;

    check_page(&small_page, 230, &w);
    CHECK(w.header == 36, "the small page's header ends at %u, expected 36", (unsigned) w.header);
    CHECK((w.padded == 3) && (w.flush == 5), "the small page: %d items padded and %d not, expected 3 and 5", w.padded,
          w.flush);
    CHECK((w.start[3] == 98) && (w.start[5] == 132) && (w.start[7] == 196) && (w.stop[2] == 97),
          "the small page's items at %u, %u and %u, item 3 to %u", (unsigned) w.start[3], (unsigned) w.start[5],
          (unsigned) w.start[7], (unsigned) w.stop[2]);
    check_page(&empty_page, 20, &w);
    check_page(&no_font_page, 20, &w);
}

static void
overflow(void)
{
    walk_t      w;
    walk_t      w2;
    const void *tm;
    size_t      size;
    size_t      room;

    /* The size from a first build into a page's buffer. */
    check_page(&small_page, 0, &w);
    size = w.end;
    CHECK(size == 230, "the small page takes %u words", (unsigned) size);

    /* Out in the header: before the item count (whose place tmpl_end fills
       last), in the title, at the face's NUL. */
    short_room(&small_page, 0, "no room");
    short_room(&small_page, 3, "the extended style's high word out");
    short_room(&small_page, 4, "the item count out");
    short_room(&small_page, 15, "in the title");
    short_room(&small_page, w.header - 1, "the face's NUL out");
    /* The header whole, the first item's first word out. */
    short_room(&small_page, w.header, "the first item out");
    /* In an item's middle: item 2's style and extended style in, its x out. */
    short_room(&small_page, w.start[1] + 4, "item 2's x out");
    /* In an item's class name and text. */
    short_room(&small_page, w.start[5] + 12, "in item 6's class name");
    short_room(&small_page, w.stop[5] - 3, "in item 6's text");
    /* On an item's last word: item 3's creation word out; item 3 whole and
       item 4's padding word out; the last item's creation word out. */
    short_room(&small_page, w.stop[2] - 1, "item 3's creation word out");
    short_room(&small_page, w.stop[2], "item 4's padding out");
    short_room(&small_page, size - 1, "the last creation word out");
    /* Every room short of the size. */
    for (room = 0; room < size; room++)
        short_room(&small_page, room, "every room");

    /* Exactly the size: the template, walked within that room, nothing
       written past it. */
    fill_canaries();
    tm = build(&small_page, mem.w, size);
    CHECK(tm == mem.w, "the small page in exactly %u words: %p", (unsigned) size, tm);
    if (tm) {
        walk("the small page in exactly its words", &small_page, tm, size, &w2);
        CHECK(w2.end == size, "the small page in exactly its words ends at %u", (unsigned) w2.end);
    }
    CHECK(!written_from(size), "the small page in exactly %u words: %u words written past it", (unsigned) size,
          (unsigned) written_from(size));

    /* The empty page in exactly its 20, and in 19. */
    fill_canaries();
    tm = build(&empty_page, mem.w, 20);
    CHECK((tm == mem.w) && !written_from(20), "the empty page in exactly 20 words: %p, %u past", tm,
          (unsigned) written_from(20));
    short_room(&empty_page, 19, "the face's NUL out");
}

/* A page the size of the real ones: the 3D page's controls and the Color
   page's, some texts long. */
static const char text_on[] = "Always on: a game that asks not to wait for the vertical blank waits for one. Always off: "
                              "no game waits for it.";
static const char text_rate[] = "The refresh rate is 75 Hz, set on the Adapter tab. A game shows at most one new "
                                "picture each refresh, whatever it asks.";
static const char text_tones[] = "Gamma changes the middle tones, brightness lifts or lowers every level, and "
                                 "contrast spreads the levels out from the middle.";
static const char text_off[] = "The V-sync overrides are off in 86Box's settings: every program decides for itself, "
                               "as on any card of the time.";

static const item_t big_items[] = {
    { BS_GROUPBOX, 0, 7, 7, 238, 96, -1, BUTTON, NULL, "Vertical sync" },
    { 0, 0, 14, 22, 50, 8, -1, STATIC, NULL, "Direct3D:" },
    { CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 70, 20, 120, 60, 1001, COMBOBOX, NULL, "" },
    { 0, 0, 14, 40, 50, 8, -1, STATIC, NULL, "OpenGL:" },
    { CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 70, 38, 120, 60, 1002, COMBOBOX, NULL, "" },
    { 0, 0, 14, 58, 50, 8, -1, STATIC, NULL, "Glide:" },
    { CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 0, 70, 56, 120, 60, 1003, COMBOBOX, NULL, "" },
    { 0, 0, 14, 74, 224, 24, -1, STATIC, NULL, text_on },
    { 0, 0, 7, 108, 238, 24, 1004, STATIC, NULL, text_rate },
    { BS_GROUPBOX, 0, 7, 7, 238, 150, -1, BUTTON, NULL, "Color" },
    { BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 0, 14, 20, 50, 10, 1101, BUTTON, NULL, "All colors" },
    { BS_AUTORADIOBUTTON, 0, 70, 20, 40, 10, 1102, BUTTON, NULL, "Red" },
    { BS_AUTORADIOBUTTON, 0, 115, 20, 40, 10, 1103, BUTTON, NULL, "Green" },
    { BS_AUTORADIOBUTTON, 0, 160, 20, 40, 10, 1104, BUTTON, NULL, "Blue" },
    { 0, 0, 14, 40, 45, 8, -1, STATIC, NULL, "Gamma:" },
    { TBS_AUTOTICKS | WS_TABSTOP, 0, 60, 36, 140, 16, 1111, 0, "msctls_trackbar32", "" },
    { SS_RIGHT, 0, 205, 40, 30, 8, 1121, STATIC, NULL, "1.00" },
    { 0, 0, 14, 60, 45, 8, -1, STATIC, NULL, "Brightness:" },
    { TBS_AUTOTICKS | WS_TABSTOP, 0, 60, 56, 140, 16, 1112, 0, "msctls_trackbar32", "" },
    { SS_RIGHT, 0, 205, 60, 30, 8, 1122, STATIC, NULL, "0" },
    { 0, 0, 14, 80, 45, 8, -1, STATIC, NULL, "Contrast:" },
    { TBS_AUTOTICKS | WS_TABSTOP, 0, 60, 76, 140, 16, 1113, 0, "msctls_trackbar32", "" },
    { SS_RIGHT, 0, 205, 80, 30, 8, 1123, STATIC, NULL, "100" },
    { SS_BLACKFRAME, 0, 14, 100, 64, 48, 1130, STATIC, NULL, "" },
    { 0, 0, 82, 140, 10, 8, -1, STATIC, NULL, "0" },
    { SS_RIGHT, 0, 60, 100, 20, 8, -1, STATIC, NULL, "255" },
    { BS_AUTOCHECKBOX | WS_TABSTOP, 0, 100, 100, 130, 10, 1131, BUTTON, NULL, "Link the three colors" },
    { 0, 0, 100, 114, 138, 32, -1, STATIC, NULL, text_tones },
    { 0, 0, 7, 165, 238, 16, 1005, STATIC, NULL, text_off },
    { 0, 0, 7, 185, 160, 8, 1006, 0, "Static", "VGPU98 2000 control program, build 2026-10-27, interface 3.6" },
    { WS_TABSTOP, 0, 170, 196, 75, 14, 1007, BUTTON, NULL, "Restore Defaults" },
    { 0, 0, 14, 130, 80, 8, 1141, STATIC, NULL, "Direct3D now: application's choice" },
    { 0, 0, 14, 140, 80, 8, 1142, STATIC, NULL, "OpenGL now: always off" },
    { 0, 0, 14, 150, 80, 8, 1143, STATIC, NULL, "Glide now: always on" },
    { 0, 0, 100, 150, 30, 8, -1, 0, "Static", "Red" },
    { 0, 0, 135, 150, 30, 8, -1, 0, "Static", "Green" },
    { 0, 0, 170, 150, 30, 8, -1, 0, "Static", "Blue" },
    { SS_RIGHT, WS_EX_CLIENTEDGE, 200, 150, 40, 10, 1144, STATIC, NULL, "75 Hz" },
    { 0, 0, 7, 205, 150, 8, 1008, STATIC, NULL, "The card was found: VGPU98 2000, 128 MB." },
    { 0, 0, 160, 205, 85, 8, 1009, STATIC, NULL, "Changes apply at Apply." },
};

static const page_t big_page = {
    "the full-size page", DS_SETFONT | DS_3DLOOK | WS_CHILD | WS_DISABLED | WS_CAPTION, 252, 218, "VGPU98 Color", 8,
    "MS Sans Serif", big_items, (int) (sizeof(big_items) / sizeof(big_items[0])),
};

static void
full_size(void)
{
    walk_t w;
    size_t longest = 0;
    int    i;

    for (i = 0; i < big_page.n; i++)
        if (strlen(big_items[i].text) > longest)
            longest = strlen(big_items[i].text);
    CHECK(big_page.n == 40, "the full-size page has %d items", big_page.n);
    check_page(&big_page, 0, &w);
    CHECK((w.padded > 0) && (w.flush > 0), "the full-size page: %d items padded and %d not", w.padded, w.flush);
    printf("test_tmpl: a %d-item page takes %u of %d words (%d padded, its longest text %u characters)\n", big_page.n,
           (unsigned) w.end, TMPL_WORDS, w.padded, (unsigned) longest);
}

int
main(void)
{
    small();
    overflow();
    full_size();
    printf("test_tmpl: %d failed\n", failures);
    return failures ? 1 : 0;
}

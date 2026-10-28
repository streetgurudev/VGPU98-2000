/*
 * The Glide core: the utility library's .3df texture files, gu3dfGetInfo
 * and gu3dfLoad for both DLLs.
 *
 * A .3df file has four text lines (the version, the format's name, the
 * levels' range, the aspect), then for YIQ formats the NCC table's values
 * and for palette formats the palette, as big-endian words, then the
 * texels level by level from the largest, 16-bit ones big-endian. Glide 2
 * reports the levels and the aspect in its own numbers, Glide 3 in log2.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcore.h"
#include "vid_vgpu98_glcpu.h"

#define GENERIC_READ    0x80000000u
#define FILE_SHARE_READ 0x00000001u
#define OPEN_EXISTING   3
#define FILE_BEGIN      0
#define NO_FILE         0xffffffffu /* INVALID_HANDLE_VALUE */
#define HEADER_MAX      128

typedef struct header {
    int   format;
    int   small, large, aspect; /* log2 */
    int   aspect_w, aspect_h;   /* the file's two numbers */
    DWORD bytes;                /* the header's own */
} header_t;

static int
lower(int c)
{
    return ((c >= 'A') && (c <= 'Z')) ? c - 'A' + 'a' : c;
}

static int
same(const char *a, const char *b)
{
    while (*a && *b && (lower(*a) == lower(*b))) {
        a++;
        b++;
    }
    return !*a && !*b;
}

/* The header's words (at most 15 characters each kept) up to its fourth
   newline; the bytes it took in *used. -1 without four lines. */
static int
words(const char *text, DWORD n, char w[][16], int max, DWORD *used)
{
    DWORD i = 0, lines = 0;
    int   k = 0;

    while ((i < n) && (lines < 4)) {
        int c = 0;

        if (text[i] == '\n') {
            lines++;
            i++;
            continue;
        }
        if ((text[i] == ' ') || (text[i] == '\t') || (text[i] == '\r')) {
            i++;
            continue;
        }
        if (k == max)
            return -1;
        while ((i < n) && (text[i] != ' ') && (text[i] != '\t') && (text[i] != '\r') && (text[i] != '\n')) {
            if (c < 15)
                w[k][c++] = text[i];
            i++;
        }
        w[k++][c] = 0;
    }
    *used = i;
    return (lines == 4) ? k : -1;
}

/* A decimal number of at most 6 digits; -1 if it is not one. */
static int
number(const char *s)
{
    int v = 0, n = 0;

    if (!*s)
        return -1;
    while ((*s >= '0') && (*s <= '9') && (n < 7)) {
        v = v * 10 + (*s++ - '0');
        n++;
    }
    return (*s || (n > 6)) ? -1 : v;
}

static int
log2_of(int v)
{
    int k;

    for (k = 0; k <= 8; k++) {
        if (v == (1 << k))
            return k;
    }
    return -1;
}

static int
format_of(const char *name)
{
    static const struct {
        const char *name;
        int         format;
    } names[] = {
        { "I8", 0x3 },     { "A8", 0x2 },       { "AI44", 0x4 },     { "YIQ", 0x1 },      { "RGB332", 0x0 },
        { "RGB565", 0xa }, { "ARGB8332", 0x8 }, { "ARGB1555", 0xb }, { "AYIQ8422", 0x9 }, { "ARGB4444", 0xc },
        { "AI88", 0xd },   { "P8", 0x5 },       { "AP88", 0xe },
    };
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (same(name, names[i].name))
            return names[i].format;
    }
    return -1;
}

static int
read_all(DWORD file, void *buf, DWORD n)
{
    DWORD got = 0;

    return n ? (ReadFile(file, buf, n, &got, NULL) && (got == n)) : 1;
}

/* "3df v<version>", the format's name, "lod range: <small> <large>",
   "aspect ratio: <w> <h>" (one of them 1, the other 1, 2, 4 or 8). */
static int
read_header(DWORD file, header_t *h)
{
    char  text[HEADER_MAX];
    char  w[12][16];
    DWORD got = 0, used = 0;
    int   n;

    if (!ReadFile(file, text, sizeof(text), &got, NULL) || !got)
        return 0;
    n = words(text, got, w, 12, &used);
    if ((n != 11) || !same(w[0], "3df") || (lower(w[1][0]) != 'v') || !same(w[3], "lod") || !same(w[4], "range:") ||
        !same(w[7], "aspect") || !same(w[8], "ratio:"))
        return 0;
    h->format   = format_of(w[2]);
    h->small    = log2_of(number(w[5]));
    h->large    = log2_of(number(w[6]));
    h->aspect_w = number(w[9]);
    h->aspect_h = number(w[10]);
    h->bytes    = used;
    /* An aspect of powers of two whose ratio is at most 8 ("2 4" is 1:2). */
    if ((h->format < 0) || (h->small < 0) || (h->large < h->small) || (log2_of(h->aspect_w) < 0) ||
        (log2_of(h->aspect_h) < 0))
        return 0;
    h->aspect = log2_of(h->aspect_w) - log2_of(h->aspect_h);
    if ((h->aspect < -3) || (h->aspect > 3))
        return 0;
    return 1;
}

static DWORD
level_bytes_all(const header_t *h)
{
    vgpu98_gltex_t t;
    DWORD          n = 0;
    int            k;

    t.format  = h->format;
    t.aspect  = h->aspect;
    t.evenodd = 3;
    for (k = h->small; k <= h->large; k++)
        n += vgpu98_gl_level_bytes(&t, k);
    return n;
}

/* The header in the program's numbers: Glide 2's levels (0 the 256 one)
   and aspects (0 for 8:1), or Glide 3's log2; the size as a Voodoo3's
   library gives it, the short side the large one over the aspect's
   width number. */
static void
fill_header(const header_t *h, glc_3df_info_t *info, int glide3)
{
    const FxU32 large = 1u << h->large;

    if (h->aspect >= 0) {
        info->width  = large;
        info->height = large / (FxU32) h->aspect_w;
    } else {
        info->width  = large / (FxU32) h->aspect_h;
        info->height = large;
    }
    info->small_lod    = glide3 ? h->small : 8 - h->small;
    info->large_lod    = glide3 ? h->large : 8 - h->large;
    info->aspect_ratio = glide3 ? h->aspect : 3 - h->aspect;
    info->format       = h->format;
    info->mem_required = level_bytes_all(h);
}

FxBool
glc_3df_info(const char *name, glc_3df_info_t *info, int glide3)
{
    header_t h;
    DWORD    file;
    int      ok;

    if (!name || !info)
        return FXFALSE;
    file = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, 0);
    if (file == NO_FILE)
        return FXFALSE;
    ok = read_header(file, &h);
    CloseHandle(file);
    if (!ok)
        return FXFALSE;
    fill_header(&h, info, glide3);
    return FXTRUE;
}

static DWORD
be16(const uint8_t *p)
{
    return ((DWORD) p[0] << 8) | p[1];
}

FxBool
glc_3df_load(const char *name, glc_3df_info_t *info, int glide3)
{
    header_t h;
    DWORD    file, i, n;
    uint8_t  buf[1024];
    uint8_t *texels;
    int      ok;

    if (!name || !info || !info->data)
        return FXFALSE;
    file = CreateFileA(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, 0);
    if (file == NO_FILE)
        return FXFALSE;
    ok = read_header(file, &h);
    if (ok)
        ok = SetFilePointer(file, (LONG) h.bytes, NULL, FILE_BEGIN) == h.bytes;
    if (ok) {
        fill_header(&h, info, glide3);
        if ((h.format == 0x1) || (h.format == 0x9)) {
            /* The NCC table: 16 Y values (8 bits), 4 x 3 I and 4 x 3 Q (9-bit
               signed), each a big-endian 16-bit word. */
            ok = read_all(file, buf, (16 + 12 + 12) * 2);
            for (i = 0; ok && (i < 16); i++)
                info->table.ncc.yRGB[i] = (FxU8) be16(buf + i * 2);
            for (i = 0; ok && (i < 12); i++) {
                /* The raw 9 bits, as a Voodoo3's library keeps them. */
                info->table.ncc.iRGB[i / 3][i % 3] = (FxI16) (be16(buf + 32 + i * 2) & 0x1ff);
                info->table.ncc.qRGB[i / 3][i % 3] = (FxI16) (be16(buf + 56 + i * 2) & 0x1ff);
            }
            for (i = 0; ok && (i < 4); i++) {
                const FxU8 *y = &info->table.ncc.yRGB[i * 4];

                info->table.ncc.packed_data[i] = (FxU32) y[0] | ((FxU32) y[1] << 8) | ((FxU32) y[2] << 16) | ((FxU32) y[3] << 24);
                info->table.ncc.packed_data[4 + i] = (((FxU32) info->table.ncc.iRGB[i][0] & 0x1ff) << 18) |
                                                     (((FxU32) info->table.ncc.iRGB[i][1] & 0x1ff) << 9) |
                                                     ((FxU32) info->table.ncc.iRGB[i][2] & 0x1ff);
                info->table.ncc.packed_data[8 + i] = (((FxU32) info->table.ncc.qRGB[i][0] & 0x1ff) << 18) |
                                                     (((FxU32) info->table.ncc.qRGB[i][1] & 0x1ff) << 9) |
                                                     ((FxU32) info->table.ncc.qRGB[i][2] & 0x1ff);
            }
        } else if ((h.format == 0x5) || (h.format == 0xe)) {
            /* The palette: 256 big-endian 32-bit entries. */
            ok = read_all(file, buf, 1024);
            for (i = 0; ok && (i < 256); i++)
                info->table.palette[i] = (be16(buf + i * 4) << 16) | be16(buf + i * 4 + 2);
        }
    }
    /* The texels, 16-bit ones turned from big-endian. */
    n      = ok ? level_bytes_all(&h) : 0;
    texels = (uint8_t *) info->data;
    if (ok)
        ok = read_all(file, texels, n);
    if (ok && (h.format & 8)) {
        for (i = 0; i + 1 < n; i += 2) {
            const uint8_t t = texels[i];

            texels[i]     = texels[i + 1];
            texels[i + 1] = t;
        }
    }
    CloseHandle(file);
    return ok ? FXTRUE : FXFALSE;
}

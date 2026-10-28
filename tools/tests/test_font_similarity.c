/*
 * Checks that the VGPU98 fonts stay an independent design: compares them with the
 * fonts in a reference VGA BIOS image and fails if more glyphs are identical than
 * the ceilings for that font allow. A glyph counts as identical if it matches the
 * reference glyph exactly, or ("shifted") after moving the reference up or down
 * by up to two rows and sideways by up to one column.
 *
 * The reference image is only read, never copied into the tree or the build. CTest
 * runs this check only when the image exists locally (VGPU98_REFERENCE_VGA_ROM).
 *
 * Glyphs whose shape the cell itself dictates are not counted: blanks (00h, 20h,
 * FFh) and shades, box drawing and blocks (B0h-DFh). Simple shapes drawn in the
 * same two-pixel-stem style can still coincide, which is what the ceilings allow;
 * the identical glyphs are always listed so a reviewer can see them.
 *
 * Usage: test_font_similarity REFERENCE_ROM GEN_DIR
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static const struct {
    const char *inc;
    int         height;
    int         exact;   /* ceiling for exact matches */
    int         similar; /* ceiling for exact plus shifted matches */
} fonts[] = {
    /* Ceilings sit just above the counts after Audit 2: 8x16 0 exact, 9 in all
       (+ - . = _ guillemets, division sign, degree); 8x8 3 exact (+ - >), 11 in all;
       8x14 10 exact (H J U V m o w x and two symbols: the 8x14 font is derived from
       the 8x16 design, and its 14-row metrics match the reference's 8x14 font), 19
       in all. Raise a ceiling only after looking at the list it prints. */
    { "font8x16.inc", 16, 2, 12 },
    { "font8x8.inc", 8, 5, 14 },
    { "font8x14.inc", 14, 12, 22 },
};

/* 1 if `ours` equals the reference glyph moved by dy rows and dx columns. */
static int
matches_shifted(const uint8_t *ours, const uint8_t *ref, int h, int dy, int dx)
{
    int r;

    for (r = 0; r < h; r++) {
        int     s = r - dy;
        uint8_t v = 0;

        if ((s >= 0) && (s < h))
            v = (uint8_t) ((dx > 0) ? (ref[s] >> dx) : (dx < 0) ? (ref[s] << -dx) : ref[s]);
        if (ours[r] != v)
            return 0;
    }
    return 1;
}

static int
exempt(int c)
{
    return (c == 0x00) || (c == 0x20) || (c == 0xff) || ((c >= 0xb0) && (c <= 0xdf));
}

/* Reads every 0xNN byte after "db" in a generated include file. */
static size_t
read_inc(const char *dir, const char *name, uint8_t *out, size_t max)
{
    char   path[1024];
    char  *text;
    char  *p;
    size_t n = 0;

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    text = (char *) read_file(path, NULL);
    for (p = text; (p = strstr(p, "db ")) != NULL;) {
        char *end = strchr(p, '\n');

        if (end == NULL)
            end = p + strlen(p);
        p += 3;
        while ((p < end) && (*p != ';')) {
            if ((p[0] == '0') && (p[1] == 'x')) {
                if (n < max)
                    out[n] = (uint8_t) strtoul(p, &p, 16);
                n++;
            } else
                p++;
        }
    }
    free(text);
    return n;
}

static int
all(const uint8_t *p, int n, uint8_t v)
{
    int i;

    for (i = 0; i < n; i++) {
        if (p[i] != v)
            return 0;
    }
    return 1;
}

/* A half block: `first` for rows 0..k-1 and `second` for the rest, splitting within a
   row of the middle. */
static int
half_block(const uint8_t *p, int h, uint8_t first, uint8_t second)
{
    int k;

    for (k = h / 2 - 1; k <= h / 2 + 1; k++) {
        if (all(p, k, first) && all(p + k, h - k, second))
            return 1;
    }
    return 0;
}

/* Finds a code page 437 font table of the given height: blank 00h and 20h, a solid
   DBh, and the half blocks DCh and DFh. Returns its offset or -1. */
static long
find_table(const uint8_t *rom, size_t size, int h)
{
    size_t o;

    for (o = 0; o + 256u * h <= size; o++) {
        const uint8_t *t = rom + o;

        if (all(t, h, 0x00) && all(t + 0x20 * h, h, 0x00) && all(t + 0xdb * h, h, 0xff) &&
            half_block(t + 0xdc * h, h, 0x00, 0xff) && half_block(t + 0xdf * h, h, 0xff, 0x00))
            return (long) o;
    }
    return -1;
}

int
main(int argc, char **argv)
{
    uint8_t *rom;
    size_t   size;
    size_t   f;
    int      failed = 0;

    set_progname("test_font_similarity");
    if (argc != 3)
        die("usage: test_font_similarity REFERENCE_ROM GEN_DIR");
    rom = read_file(argv[1], &size);

    for (f = 0; f < sizeof(fonts) / sizeof(fonts[0]); f++) {
        int      h = fonts[f].height;
        uint8_t  ours[256 * 16];
        long     at = find_table(rom, size, h);
        int      same = 0;
        int      shifted = 0;
        int      c;

        if (read_inc(argv[2], fonts[f].inc, ours, sizeof(ours)) != (size_t) 256 * h)
            die("%s has the wrong size", fonts[f].inc);
        if (at < 0)
            die("no 8x%d font table found in %s", h, argv[1]);

        printf("%s: reference table at %05lXh; identical:", fonts[f].inc, at);
        for (c = 0; c < 256; c++) {
            if (!exempt(c) && (memcmp(ours + c * h, rom + at + c * h, (size_t) h) == 0)) {
                printf(" %02X", c);
                same++;
            }
        }
        printf(" (%d, ceiling %d); shifted:", same, fonts[f].exact);
        for (c = 0; c < 256; c++) {
            int dy;
            int dx;
            int hit = 0;

            if (exempt(c) || (memcmp(ours + c * h, rom + at + c * h, (size_t) h) == 0))
                continue;
            for (dy = -2; (dy <= 2) && !hit; dy++) {
                for (dx = -1; (dx <= 1) && !hit; dx++)
                    hit = matches_shifted(ours + c * h, rom + at + c * h, h, dy, dx);
            }
            if (hit) {
                printf(" %02X", c);
                shifted++;
            }
        }
        printf(" (%d in all, ceiling %d)\n", same + shifted, fonts[f].similar);
        if ((same > fonts[f].exact) || (same + shifted > fonts[f].similar)) {
            fprintf(stderr, "%s: %d identical and %d shifted glyphs; the ceilings are %d and %d in all\n",
                    fonts[f].inc, same, shifted, fonts[f].exact, fonts[f].similar);
            failed = 1;
        }
    }

    free(rom);
    return failed;
}

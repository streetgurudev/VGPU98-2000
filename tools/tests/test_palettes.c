/*
 * Spot checks of the default palettes against the standard VGA values.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "check.h"
#include "palettes.h"

static uint8_t pal64[3][64][3];
static uint8_t pal256[256][3];

static int
rgb(const uint8_t *entry, int r, int g, int b)
{
    return (entry[0] == r) && (entry[1] == g) && (entry[2] == b);
}

int
main(void)
{
    int i;

    build_palettes(pal64, pal256);

    /* EGA: code 14h is brown, 07h light gray, 38h dark gray, 3Fh white. */
    CHECK(rgb(pal64[PAL_EGA64][0x14], 0x2a, 0x15, 0x00));
    CHECK(rgb(pal64[PAL_EGA64][0x07], 0x2a, 0x2a, 0x2a));
    CHECK(rgb(pal64[PAL_EGA64][0x38], 0x15, 0x15, 0x15));
    CHECK(rgb(pal64[PAL_EGA64][0x3f], 0x3f, 0x3f, 0x3f));

    /* CGA emulation: colour 6 is brown, 16h yellow; bits 3 and 5 are ignored. */
    CHECK(rgb(pal64[PAL_CGA64][0x06], 0x2a, 0x15, 0x00));
    CHECK(rgb(pal64[PAL_CGA64][0x16], 0x3f, 0x3f, 0x15));
    CHECK(rgb(pal64[PAL_CGA64][0x10], 0x15, 0x15, 0x15));
    for (i = 0; i < 64; i++)
        CHECK(rgb(pal64[PAL_CGA64][i], pal64[PAL_CGA64][i & 0x17][0], pal64[PAL_CGA64][i & 0x17][1],
                  pal64[PAL_CGA64][i & 0x17][2]));

    /* Monochrome: video or intensity alone is normal; both is bright. */
    CHECK(rgb(pal64[PAL_MONO64][0x00], 0x00, 0x00, 0x00));
    CHECK(rgb(pal64[PAL_MONO64][0x08], 0x2a, 0x2a, 0x2a));
    CHECK(rgb(pal64[PAL_MONO64][0x10], 0x2a, 0x2a, 0x2a));
    CHECK(rgb(pal64[PAL_MONO64][0x18], 0x3f, 0x3f, 0x3f));

    /* 256 colours: the 16 colours, the gray ramp, then 9 blocks of 24 hues. */
    CHECK(rgb(pal256[6], 0x2a, 0x15, 0x00));
    CHECK(rgb(pal256[15], 0x3f, 0x3f, 0x3f));
    CHECK(rgb(pal256[16], 0x00, 0x00, 0x00));
    CHECK(rgb(pal256[23], 0x18, 0x18, 0x18));
    CHECK(rgb(pal256[31], 0x3f, 0x3f, 0x3f));
    CHECK(rgb(pal256[32], 0x00, 0x00, 0x3f));  /* blue */
    CHECK(rgb(pal256[36], 0x3f, 0x00, 0x3f));  /* magenta */
    CHECK(rgb(pal256[40], 0x3f, 0x00, 0x00));  /* red */
    CHECK(rgb(pal256[44], 0x3f, 0x3f, 0x00));  /* yellow */
    CHECK(rgb(pal256[48], 0x00, 0x3f, 0x00));  /* green */
    CHECK(rgb(pal256[52], 0x00, 0x3f, 0x3f));  /* cyan */
    CHECK(rgb(pal256[33], 0x10, 0x00, 0x3f));
    CHECK(rgb(pal256[56], 0x1f, 0x1f, 0x3f));  /* first entry of the second block */
    CHECK(rgb(pal256[104], 0x00, 0x00, 0x1c)); /* medium intensity blue */
    CHECK(rgb(pal256[247], 0x0b, 0x0c, 0x10)); /* last hue entry */
    for (i = 248; i < 256; i++)
        CHECK(rgb(pal256[i], 0, 0, 0));

    CHECK_DONE();
}

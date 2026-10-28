/*
 * cardc: default VGA DAC palettes, generated from the standard rules.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "palettes.h"

#include <string.h>

void
build_palettes(uint8_t pal64[3][64][3], uint8_t pal256[256][3])
{
    /* Ramps from the minimum to the maximum of each intensity/saturation block. */
    static const uint8_t ramps[9][5] = {
        { 0x00, 0x10, 0x1f, 0x2f, 0x3f }, { 0x1f, 0x27, 0x2f, 0x37, 0x3f }, { 0x2d, 0x31, 0x36, 0x3a, 0x3f },
        { 0x00, 0x07, 0x0e, 0x15, 0x1c }, { 0x0e, 0x11, 0x15, 0x18, 0x1c }, { 0x14, 0x16, 0x18, 0x1a, 0x1c },
        { 0x00, 0x04, 0x08, 0x0c, 0x10 }, { 0x08, 0x0a, 0x0c, 0x0e, 0x10 }, { 0x0b, 0x0c, 0x0d, 0x0f, 0x10 },
    };
    /* 24 hues: ramp step (0-4) for red, green and blue, from blue round to blue. */
    static const uint8_t hues[24][3] = {
        { 0, 0, 4 }, { 1, 0, 4 }, { 2, 0, 4 }, { 3, 0, 4 }, { 4, 0, 4 }, { 4, 0, 3 }, { 4, 0, 2 }, { 4, 0, 1 },
        { 4, 0, 0 }, { 4, 1, 0 }, { 4, 2, 0 }, { 4, 3, 0 }, { 4, 4, 0 }, { 3, 4, 0 }, { 2, 4, 0 }, { 1, 4, 0 },
        { 0, 4, 0 }, { 0, 4, 1 }, { 0, 4, 2 }, { 0, 4, 3 }, { 0, 4, 4 }, { 0, 3, 4 }, { 0, 2, 4 }, { 0, 1, 4 },
    };
    static const uint8_t grays[16] = {
        0x00, 0x05, 0x08, 0x0b, 0x0e, 0x11, 0x14, 0x18, 0x1c, 0x20, 0x24, 0x28, 0x2d, 0x32, 0x38, 0x3f
    };
    /* The 16 colours of the 16-colour modes, as EGA colour codes. */
    static const uint8_t ega16[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f
    };
    int i;
    int c;

    for (i = 0; i < 64; i++) {
        int on = i & 0x18;

        /* EGA: bits 0-2 are blue, green, red at 2/3; bits 3-5 add 1/3. */
        pal64[PAL_EGA64][i][0] = (uint8_t) (((i >> 2) & 1) * 0x2a + ((i >> 5) & 1) * 0x15);
        pal64[PAL_EGA64][i][1] = (uint8_t) (((i >> 1) & 1) * 0x2a + ((i >> 4) & 1) * 0x15);
        pal64[PAL_EGA64][i][2] = (uint8_t) ((i & 1) * 0x2a + ((i >> 3) & 1) * 0x15);

        /* CGA emulation: bits 0-2 are blue, green, red; bit 4 is intensity; colour 6 is brown. */
        for (c = 0; c < 3; c++) {
            uint8_t v = ((i >> (2 - c)) & 1) ? 0x2a : 0x00;

            if (i & 0x10)
                v += 0x15;
            pal64[PAL_CGA64][i][c] = v;
        }
        if ((i & 0x17) == 0x06)
            pal64[PAL_CGA64][i][1] = 0x15;

        /* Monochrome: bit 3 is video, bit 4 is intensity. Either alone is normal, both is bright. */
        for (c = 0; c < 3; c++)
            pal64[PAL_MONO64][i][c] = (uint8_t) ((on == 0x18) ? 0x3f : (on ? 0x2a : 0x00));
    }

    memset(pal256, 0, 256 * 3);
    for (i = 0; i < 16; i++) {
        memcpy(pal256[i], pal64[PAL_EGA64][ega16[i]], 3);
        pal256[16 + i][0] = pal256[16 + i][1] = pal256[16 + i][2] = grays[i];
    }
    for (i = 0; i < 9 * 24; i++) {
        const uint8_t *ramp = ramps[i / 24];
        const uint8_t *hue  = hues[i % 24];

        for (c = 0; c < 3; c++)
            pal256[32 + i][c] = ramp[hue[c]];
    }
    /* Entries 248-255 stay black. */
}

/*
 * Tests for the display mode table loader and the EDID builder, run on the real
 * card/display-modes.card.
 *
 * Usage: test_edid DISPLAY_MODES_CARD
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <string.h>

#include "check.h"
#include "edid.h"
#include "modes.h"
#include "util.h"

static const mode_limits_t limits = { 1920, 1440, 50, 160 };

int
main(int argc, char **argv)
{
    static dmodes_t modes;
    uint8_t         e[EDID_SIZE];
    edid_params_t   p;
    uint8_t         sum = 0;
    size_t          i;
    size_t          j;
    size_t          desktop = 0;
    size_t          vbe     = 0;
    int             found_101 = 0;

    set_progname("test_edid");
    if (argc != 2)
        die("usage: test_edid DISPLAY_MODES_CARD");
    modes_load(argv[1], &limits, &modes);

    for (i = 0; i < modes.count; i++) {
        const dmode_t *m = &modes.mode[i];

        CHECK((m->width % 8) == 0);
        if (m->enabled && m->desktop)
            desktop++;
        for (j = 0; j < MODE_DEPTHS; j++) {
            if (m->vbe[j])
                vbe++;
            if ((m->vbe[j] == 0x101) && (m->width == 640) && (m->height == 480) && (mode_bpp[j] == 8))
                found_101 = 1;
            if (!m->enabled)
                CHECK(m->vbe[j] == 0);
        }
    }
    CHECK(desktop == 14); /* the Voodoo3 desktop list */
    CHECK(found_101);
    CHECK(vbe == 19 * 4); /* every enabled mode at every depth */
    CHECK(mode_vtotal(480) == 504); /* 480 / 20 = 24 blank lines */
    CHECK(mode_vtotal(400) == 423); /* the 23-line minimum */
    CHECK(mode_vtotal(1200) == 1260);
    CHECK(mode_htotal(640) == 800);
    CHECK(mode_bytes_per_pixel(15) == 2);

    memset(&p, 0, sizeof(p));
    memcpy(p.vendor, "VGP", 4);
    p.product           = 0x2000;
    p.serial            = 1;
    p.week              = 41;
    p.year              = 2026;
    p.name              = "VGPU98 Screen";
    p.width_mm          = 400;
    p.height_mm         = 300;
    p.preferred_width   = 1024;
    p.preferred_height  = 768;
    p.preferred_refresh = 75;
    edid_build(e, &p, &modes);

    for (i = 0; i < EDID_SIZE; i++)
        sum = (uint8_t) (sum + e[i]);
    CHECK(sum == 0);
    CHECK((e[0] == 0x00) && (e[1] == 0xff) && (e[7] == 0x00));
    CHECK((e[8] == 0x58) && (e[9] == 0xf0)); /* "VGP" */
    CHECK((e[18] == 1) && (e[19] == 3));
    CHECK(e[35] & 0x20); /* 640x480 at 60 Hz */
    CHECK(e[36] & 0x01); /* 1280x1024 at 75 Hz */
    CHECK(e[24] & 0x02); /* preferred timing in descriptor 1 */
    /* Descriptor 1: 1024x768 at 75 Hz on the engine's timing (spec/modes.md),
       worked out by hand: 1184 x 806 x 75 = 71.5728 MHz, 160 pixels and 38 lines
       of blanking. */
    CHECK((e[54] | (e[55] << 8)) == 7157);
    CHECK((e[56] == 0x00) && (e[57] == 160) && (e[58] == 0x40));
    CHECK((e[59] == 0x00) && (e[60] == 38) && (e[61] == 0x30));
    /* Descriptor 2: range limits 56-160 Hz, 30-126 kHz, 240 MHz: 800x600 at 56 Hz
       and 640x480 at 160 Hz bound the frame rate, 640x480 at 60 Hz (30.24 kHz) and
       1600x1200 at 100 Hz (126 kHz) the line rate, 1920x1440 at 75 Hz the clock
       (235.9 MHz, rounded up to 10 MHz). */
    CHECK((e[72 + 3] == 0xfd) && (e[72 + 5] == 56) && (e[72 + 6] == 160));
    CHECK((e[72 + 7] == 30) && (e[72 + 8] == 126) && (e[72 + 9] == 24));
    /* Standard timings: every desktop resolution with a 4:3, 5:4, 16:10 or 16:9
       shape that the established timings do not cover, at its highest rate up to
       123 Hz: eight in the block, the rest in descriptor 4 (FAh). 1600x1024 has no
       standard-timing shape. */
    {
        static const uint8_t want[14][2] = {
            { 0x59, 0x59 }, /* 960x720 at 85 */   { 0x71, 0x7c }, /* 1152x864 at 120 */
            { 0x81, 0x59 }, /* 1280x960 at 85 */  { 0xa9, 0x68 }, /* 1600x1200 at 100 */
            { 0xc1, 0x4f }, /* 1792x1344 at 75 */ { 0xc9, 0x4f }, /* 1856x1392 at 75 */
            { 0xd1, 0xd9 }, /* 1920x1080 at 85 */ { 0xd1, 0x19 }, /* 1920x1200 at 85 */
            { 0xd1, 0x4f }, /* 1920x1440 at 75 */ { 0x01, 0x01 }, { 0x01, 0x01 }, { 0x01, 0x01 },
            { 0x01, 0x01 }, { 0x01, 0x01 }
        };

        for (i = 0; i < 8; i++)
            CHECK((e[38 + 2 * i] == want[i][0]) && (e[39 + 2 * i] == want[i][1]));
        CHECK((e[108] == 0) && (e[109] == 0) && (e[110] == 0) && (e[111] == 0xfa) && (e[112] == 0));
        for (i = 0; i < 6; i++)
            CHECK((e[113 + 2 * i] == want[8 + i][0]) && (e[114 + 2 * i] == want[8 + i][1]));
        CHECK(e[125] == 0x0a);
    }
    /* Every desktop mode and rate fits the horizontal range and pixel clock limit. */
    for (i = 0; i < modes.count; i++) {
        const dmode_t *m = &modes.mode[i];

        if (!m->enabled || !m->desktop)
            continue;
        for (j = 0; j < m->n_refresh; j++) {
            uint32_t h   = mode_vtotal(m->height) * m->refresh[j];
            uint32_t clk = mode_htotal(m->width) * mode_vtotal(m->height) * m->refresh[j];

            CHECK((h >= e[72 + 7] * 1000u) && (h <= e[72 + 8] * 1000u));
            CHECK(clk <= e[72 + 9] * 10000000u);
        }
    }
    CHECK((e[90 + 3] == 0xfc) && (memcmp(e + 95, "VGPU98 Screen", 13) == 0));
    CHECK(e[126] == 0);

    CHECK_DONE();
}

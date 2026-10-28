/*
 * EDID 1.3 builder. See edid.h.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "edid.h"

#include <string.h>

#include "util.h"

/* Established timings (bytes 35-37): resolution, refresh, byte, bit. */
static const struct {
    uint16_t w, h;
    uint8_t  hz, byte, bit;
} established[] = {
    { 640, 480, 60, 35, 5 },   { 640, 480, 72, 35, 3 },   { 640, 480, 75, 35, 2 },   { 800, 600, 56, 35, 1 },
    { 800, 600, 60, 35, 0 },   { 800, 600, 72, 36, 7 },   { 800, 600, 75, 36, 6 },   { 1024, 768, 60, 36, 3 },
    { 1024, 768, 70, 36, 2 },  { 1024, 768, 75, 36, 1 },  { 1280, 1024, 75, 36, 0 },
};

static const dmode_t *
find_mode(const dmodes_t *modes, uint32_t w, uint32_t h)
{
    size_t i;

    for (i = 0; i < modes->count; i++) {
        if (modes->mode[i].enabled && modes->mode[i].desktop && (modes->mode[i].width == w) &&
            (modes->mode[i].height == h))
            return &modes->mode[i];
    }
    return NULL;
}

static int
has_rate(const dmode_t *m, uint32_t hz)
{
    size_t i;

    for (i = 0; i < m->n_refresh; i++) {
        if (m->refresh[i] == hz)
            return 1;
    }
    return 0;
}

/* The highest rate a standard timing can carry (60-123 Hz), or 0 if none. */
static uint8_t
max_std_rate(const dmode_t *m)
{
    uint8_t best = 0;
    size_t  i;

    for (i = 0; i < m->n_refresh; i++) {
        if ((m->refresh[i] >= 60) && (m->refresh[i] <= 123) && (m->refresh[i] > best))
            best = m->refresh[i];
    }
    return best;
}

/* Standard timing aspect code (EDID 1.3): 00 16:10, 01 4:3, 10 5:4, 11 16:9; -1 if none. */
static int
aspect_code(uint32_t w, uint32_t h)
{
    if (w * 10 == h * 16)
        return 0;
    if (w * 3 == h * 4)
        return 1;
    if (w * 4 == h * 5)
        return 2;
    if (w * 9 == h * 16)
        return 3;
    return -1;
}

static void
descriptor_text(uint8_t *d, uint8_t tag, const char *text)
{
    size_t n = strlen(text);
    size_t i;

    if (n > 13)
        die("EDID text '%s' is longer than 13 characters", text);
    memset(d, 0, 18);
    d[3] = tag;
    for (i = 0; i < 13; i++)
        d[5 + i] = (uint8_t) ((i < n) ? text[i] : (i == n) ? 0x0a : 0x20);
}

void
edid_build(uint8_t *out, const edid_params_t *p, const dmodes_t *modes)
{
    static const uint8_t header[8] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };
    uint32_t             hmin = 0xffffffff, hmax = 0, vmin = 0xff, vmax = 0, clkmax = 0;
    uint32_t             id;
    uint32_t             w, h, ht, vt, clk;
    uint8_t             *d;
    uint8_t              sum = 0;
    size_t               i, j, n_std = 0;
    uint8_t              std[14][2];
    const dmode_t       *pref;

    memset(out, 0, EDID_SIZE);
    memcpy(out, header, 8);

    /* Vendor and product */
    for (i = 0; i < 3; i++) {
        if ((p->vendor[i] < 'A') || (p->vendor[i] > 'Z'))
            die("EDID vendor ID must be three upper-case letters");
    }
    id     = ((uint32_t) (p->vendor[0] - '@') << 10) | ((uint32_t) (p->vendor[1] - '@') << 5) |
             (uint32_t) (p->vendor[2] - '@');
    out[8] = (uint8_t) (id >> 8);
    out[9] = (uint8_t) id;
    out[10] = (uint8_t) p->product;
    out[11] = (uint8_t) (p->product >> 8);
    for (i = 0; i < 4; i++)
        out[12 + i] = (uint8_t) (p->serial >> (8 * i));
    if ((p->year < 1990) || (p->year > 2245) || (p->week > 53))
        die("EDID week/year out of range");
    out[16] = p->week;
    out[17] = (uint8_t) (p->year - 1990);
    out[18] = 1; /* version 1.3 */
    out[19] = 3;

    /* Basic display parameters: analog input, separate sync; size in cm; gamma 2.2;
       DPMS standby/suspend/off, RGB colour, sRGB, preferred timing in descriptor 1, GTF. */
    out[20] = 0x08;
    out[21] = (uint8_t) ((p->width_mm + 5) / 10);
    out[22] = (uint8_t) ((p->height_mm + 5) / 10);
    out[23] = 120;
    out[24] = 0xe0 | 0x08 | 0x04 | 0x02 | 0x01;

    /* sRGB chromaticity: red 0.640/0.330, green 0.300/0.600, blue 0.150/0.060,
       white 0.3127/0.3290, as 10-bit fractions of 1024. */
    {
        static const uint16_t c[8] = { 655, 338, 307, 614, 154, 61, 320, 337 };

        out[25] = (uint8_t) (((c[0] & 3) << 6) | ((c[1] & 3) << 4) | ((c[2] & 3) << 2) | (c[3] & 3));
        out[26] = (uint8_t) (((c[4] & 3) << 6) | ((c[5] & 3) << 4) | ((c[6] & 3) << 2) | (c[7] & 3));
        for (i = 0; i < 8; i++)
            out[27 + i] = (uint8_t) (c[i] >> 2);
    }

    /* Established timings present in the mode table */
    for (i = 0; i < sizeof(established) / sizeof(established[0]); i++) {
        const dmode_t *m = find_mode(modes, established[i].w, established[i].h);

        if ((m != NULL) && has_rate(m, established[i].hz))
            out[established[i].byte] |= (uint8_t) (1u << established[i].bit);
    }

    /* Standard timings: the other desktop resolutions with a 4:3, 5:4, 16:10 or 16:9
       aspect, at their highest rate up to 123 Hz, in table order: eight in the block,
       six more in an FAh descriptor (descriptor 4). */
    for (i = 0; i < 14; i++) {
        std[i][0] = 0x01;
        std[i][1] = 0x01;
    }
    for (i = 0; i < modes->count; i++) {
        const dmode_t *m = &modes->mode[i];
        int            a;
        int            covered = 0;
        uint8_t        hz;

        if (!m->enabled || !m->desktop)
            continue;
        for (j = 0; j < sizeof(established) / sizeof(established[0]); j++) {
            if ((established[j].w == m->width) && (established[j].h == m->height) &&
                (out[established[j].byte] & (1u << established[j].bit)))
                covered = 1;
        }
        a  = aspect_code(m->width, m->height);
        hz = max_std_rate(m);
        if (covered || (a < 0) || (m->width < 256) || (m->width > 2288) || (hz == 0))
            continue;
        if (n_std == 14)
            die("EDID: more than 14 desktop resolutions need standard timings");
        std[n_std][0] = (uint8_t) (m->width / 8 - 31);
        std[n_std][1] = (uint8_t) ((a << 6) | (hz - 60));
        n_std++;
    }
    for (i = 0; i < 8; i++) {
        out[38 + 2 * i]     = std[i][0];
        out[38 + 2 * i + 1] = std[i][1];
    }

    /* Range limits over every enabled desktop mode at every rate */
    for (i = 0; i < modes->count; i++) {
        const dmode_t *m = &modes->mode[i];

        if (!m->enabled || !m->desktop)
            continue;
        ht = mode_htotal(m->width);
        vt = mode_vtotal(m->height);
        for (j = 0; j < m->n_refresh; j++) {
            uint32_t hz   = m->refresh[j];
            uint32_t hkhz = vt * hz;  /* lines per second */

            clk = ht * vt * hz;
            if (hz < vmin)
                vmin = hz;
            if (hz > vmax)
                vmax = hz;
            if (hkhz < hmin)
                hmin = hkhz;
            if (hkhz > hmax)
                hmax = hkhz;
            if (clk > clkmax)
                clkmax = clk;
        }
    }
    if (vmax == 0)
        die("EDID: no enabled desktop modes");

    /* Descriptor 1: preferred detailed timing */
    pref = find_mode(modes, p->preferred_width, p->preferred_height);
    if ((pref == NULL) || !has_rate(pref, p->preferred_refresh))
        die("EDID preferred mode %ux%u at %u Hz is not an enabled desktop mode", p->preferred_width,
            p->preferred_height, p->preferred_refresh);
    w   = pref->width;
    h   = pref->height;
    ht  = mode_htotal(w);
    vt  = mode_vtotal(h);
    clk = (ht * vt * p->preferred_refresh + 5000) / 10000;
    d   = out + 54;
    d[0]  = (uint8_t) clk;
    d[1]  = (uint8_t) (clk >> 8);
    d[2]  = (uint8_t) w;
    d[3]  = (uint8_t) (ht - w);
    d[4]  = (uint8_t) (((w >> 8) << 4) | ((ht - w) >> 8));
    d[5]  = (uint8_t) h;
    d[6]  = (uint8_t) (vt - h);
    d[7]  = (uint8_t) (((h >> 8) << 4) | ((vt - h) >> 8));
    d[8]  = 48; /* horizontal front porch */
    d[9]  = 32; /* horizontal sync width */
    d[10] = (3 << 4) | 4; /* vertical front porch 3, sync 4 */
    d[11] = 0;
    d[12] = (uint8_t) p->width_mm;
    d[13] = (uint8_t) p->height_mm;
    d[14] = (uint8_t) (((p->width_mm >> 8) << 4) | (p->height_mm >> 8));
    d[17] = 0x1e; /* separate sync, both polarities positive */

    /* Descriptor 2: range limits (kHz rounded outward, clock in 10 MHz units) */
    d     = out + 72;
    d[3]  = 0xfd;
    d[5]  = (uint8_t) vmin;
    d[6]  = (uint8_t) vmax;
    d[7]  = (uint8_t) (hmin / 1000);
    d[8]  = (uint8_t) ((hmax + 999) / 1000);
    d[9]  = (uint8_t) ((clkmax + 9999999) / 10000000);
    d[10] = 0x00; /* no secondary GTF curve */
    d[11] = 0x0a;
    for (i = 12; i < 18; i++)
        d[i] = 0x20;
    if ((vmax > 255) || ((hmax + 999) / 1000 > 255) || ((clkmax + 9999999) / 10000000 > 255))
        die("EDID range limits do not fit one byte");

    /* Descriptor 3: name. Descriptor 4: standard timings 9-14 (the serial number
       is in the header). */
    descriptor_text(out + 90, 0xfc, p->name);
    d    = out + 108;
    d[3] = 0xfa;
    for (i = 0; i < 6; i++) {
        d[5 + 2 * i] = std[8 + i][0];
        d[6 + 2 * i] = std[8 + i][1];
    }
    d[17] = 0x0a;

    out[126] = 0; /* no extension blocks */
    for (i = 0; i < 127; i++)
        sum = (uint8_t) (sum + out[i]);
    out[127] = (uint8_t) (0x100 - sum);
}

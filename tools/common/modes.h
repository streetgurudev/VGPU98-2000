/*
 * Display mode table (card/display-modes.card): loader and timing rules shared by
 * cardc, infgen and the tests. Format and rules: spec/modes.md
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_MODES_H
#define VGPU98_MODES_H

#include <stddef.h>
#include <stdint.h>

#define MODES_MAX      64
#define MODES_REFRESH  12
#define MODE_DEPTHS    4 /* 8, 15, 16, 32 bpp, in that order */
#define VBE_OEM_FIRST  0x120
#define VBE_LAST       0x1ff

/* Blanking the display engine adds to every mode (spec/display-engine.md). */
#define MODE_H_BLANK   160 /* pixels per line */
#define MODE_V_BLANK   23  /* minimum lines per frame */
#define MODE_V_DIV     20  /* or height / MODE_V_DIV lines, if more */

extern const uint8_t mode_bpp[MODE_DEPTHS];

typedef struct dmode_t {
    uint16_t width;
    uint16_t height;
    int      desktop;               /* Windows desktop mode at 8, 16 and 32 bpp */
    int      enabled;
    uint8_t  refresh[MODES_REFRESH]; /* refresh[0] is the default */
    size_t   n_refresh;
    uint16_t vbe[MODE_DEPTHS];      /* 0 = no VBE mode for that depth */
    int      line;
} dmode_t;

typedef struct dmodes_t {
    dmode_t mode[MODES_MAX];
    size_t  count;
} dmodes_t;

typedef struct mode_limits_t {
    uint32_t max_width;
    uint32_t max_height;
    uint32_t refresh_min;
    uint32_t refresh_max;
} mode_limits_t;

/* Loads and checks the table; dies with a file:line message on any error. */
void modes_load(const char *path, const mode_limits_t *limits, dmodes_t *out);

/* Bytes per pixel for a bits-per-pixel value (15 -> 2). */
uint32_t mode_bytes_per_pixel(uint32_t bpp);

/* Total lines per frame (visible plus blanking) and total pixels per line. */
uint32_t mode_vtotal(uint32_t height);
uint32_t mode_htotal(uint32_t width);

#endif /* VGPU98_MODES_H */

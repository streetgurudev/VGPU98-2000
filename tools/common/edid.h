/*
 * EDID 1.3 block for the card's virtual monitor, built from the mode table.
 * Layout: VESA E-EDID standard, release A; our choices: spec/modes.md
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_EDID_H
#define VGPU98_EDID_H

#include <stdint.h>

#include "modes.h"

#define EDID_SIZE 128

typedef struct edid_params_t {
    char        vendor[4];  /* three upper-case letters (PNP manufacturer ID) */
    uint16_t    product;
    uint32_t    serial;
    uint8_t     week;
    uint16_t    year;
    const char *name;       /* up to 13 characters */
    uint16_t    width_mm;   /* physical image size */
    uint16_t    height_mm;
    uint16_t    preferred_width;
    uint16_t    preferred_height;
    uint8_t     preferred_refresh;
} edid_params_t;

/* Fills out[] (128 bytes, checksum included). Dies on parameters it cannot encode. */
void edid_build(uint8_t *out, const edid_params_t *p, const dmodes_t *modes);

#endif /* VGPU98_EDID_H */

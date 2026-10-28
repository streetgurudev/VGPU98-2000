/*
 * cardc: default VGA DAC palettes.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_PALETTES_H
#define VGPU98_PALETTES_H

#include <stdint.h>

enum palette { PAL_CGA64, PAL_EGA64, PAL_MONO64, PAL_VGA256, PAL_COUNT };

/*
 * Builds the default palettes as 6-bit red, green, blue triples:
 *   pal64[PAL_CGA64]   200-line modes (CGA emulation)
 *   pal64[PAL_EGA64]   350/400-line colour modes
 *   pal64[PAL_MONO64]  monochrome modes
 *   pal256             mode 13h
 */
void build_palettes(uint8_t pal64[3][64][3], uint8_t pal256[256][3]);

#endif /* VGPU98_PALETTES_H */

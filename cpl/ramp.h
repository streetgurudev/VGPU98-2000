/*
 * vgpu98cp.cpl: the gamma ramp's arithmetic,
 * plain C shared with the host test (tools/tests/test_ramp.c).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_CPL_RAMP_H
#define VGPU98_CPL_RAMP_H

#include <stdint.h>

#define RAMP_GAMMA_MIN       50  /* hundredths */
#define RAMP_GAMMA_MAX       250
#define RAMP_BRIGHTNESS_MIN  (-50) /* percent of full scale */
#define RAMP_BRIGHTNESS_MAX  50
#define RAMP_CONTRAST_MIN    50  /* percent */
#define RAMP_CONTRAST_MAX    150
#define RAMP_WORDS           768 /* red, green, blue: 256 words each, as DDIGammaRamp takes them */

/* Each channel's settings: red, green, blue. */
typedef struct ramp_settings_t {
    int gamma[3];
    int brightness[3];
    int contrast[3];
} ramp_settings_t;

/* Gamma 1.00, brightness 0, contrast 100%: the identity. */
void   ramp_defaults(ramp_settings_t *s);
int    ramp_is_default(const ramp_settings_t *s);
/* Each value into its range. */
void   ramp_clamp(ramp_settings_t *s);
/* The ramp: each channel's level i/255 raised to 1/gamma, scaled about the
   middle by the contrast, offset by the brightness, clamped to 0-1, in 16
   bits (the identity is i * 257, each word's high byte its index). */
void   ramp_build(const ramp_settings_t *s, uint16_t *words);
/* x to the power y, for x in [0, 1] and y > 0, without a C library. */
double ramp_pow(double x, double y);

#endif /* VGPU98_CPL_RAMP_H */

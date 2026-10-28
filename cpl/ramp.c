/*
 * vgpu98cp.cpl: the gamma ramp's arithmetic.
 * The DLL has no C library, so the power function is its own: a natural
 * logarithm by halving into [1, 2) and atanh's series, and an exponential
 * by ln 2's multiples and the Taylor series. Eight bits a channel is what
 * the card shows; the series give far more.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "ramp.h"

#define LN2 0.69314718055994530942

void
ramp_defaults(ramp_settings_t *s)
{
    int c;

    for (c = 0; c < 3; c++) {
        s->gamma[c]      = 100;
        s->brightness[c] = 0;
        s->contrast[c]   = 100;
    }
}

int
ramp_is_default(const ramp_settings_t *s)
{
    int c;

    for (c = 0; c < 3; c++) {
        if ((s->gamma[c] != 100) || s->brightness[c] || (s->contrast[c] != 100))
            return 0;
    }
    return 1;
}

static int
clamp_int(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

void
ramp_clamp(ramp_settings_t *s)
{
    int c;

    for (c = 0; c < 3; c++) {
        s->gamma[c]      = clamp_int(s->gamma[c], RAMP_GAMMA_MIN, RAMP_GAMMA_MAX);
        s->brightness[c] = clamp_int(s->brightness[c], RAMP_BRIGHTNESS_MIN, RAMP_BRIGHTNESS_MAX);
        s->contrast[c]   = clamp_int(s->contrast[c], RAMP_CONTRAST_MIN, RAMP_CONTRAST_MAX);
    }
}

/* ln x for x > 0. */
static double
ramp_ln(double x)
{
    double t;
    double t2;
    double term;
    double sum;
    int    e = 0;
    int    k;

    while (x < 1.0) {
        x *= 2.0;
        e--;
    }
    while (x >= 2.0) {
        x *= 0.5;
        e++;
    }
    /* ln x = 2 atanh((x - 1) / (x + 1)), the argument below 1/3. */
    t    = (x - 1.0) / (x + 1.0);
    t2   = t * t;
    term = t;
    sum  = 0.0;
    for (k = 1; k < 40; k += 2) {
        sum += term / k;
        term *= t2;
    }
    return 2.0 * sum + e * LN2;
}

/* e to the power z, for z <= 0. */
static double
ramp_exp(double z)
{
    double r;
    double term = 1.0;
    double sum  = 1.0;
    int    k    = 0;
    int    n;

    while (z < -LN2) {
        z += LN2;
        k++;
    }
    /* z in (-ln 2, 0]: the series, then halved k times. */
    r = z;
    for (n = 1; n < 30; n++) {
        term *= r / n;
        sum += term;
    }
    while (k--)
        sum *= 0.5;
    return sum;
}

double
ramp_pow(double x, double y)
{
    if (x <= 0.0)
        return 0.0;
    if (x >= 1.0)
        return 1.0;
    return ramp_exp(y * ramp_ln(x));
}

void
ramp_build(const ramp_settings_t *s, uint16_t *words)
{
    int c;
    int i;

    for (c = 0; c < 3; c++) {
        const double inv      = 100.0 / (double) s->gamma[c];
        const double contrast = s->contrast[c] / 100.0;
        const double offset   = s->brightness[c] / 100.0;

        for (i = 0; i < 256; i++) {
            double v = i / 255.0;

            if (s->gamma[c] != 100)
                v = ramp_pow(v, inv);
            v = (v - 0.5) * contrast + 0.5 + offset;
            if (v < 0.0)
                v = 0.0;
            if (v > 1.0)
                v = 1.0;
            words[c * 256 + i] = (uint16_t) (int) (v * 65535.0 + 0.5);
        }
    }
}

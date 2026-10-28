/*
 * The control program's gamma ramp (cpl/ramp.c):
 * the identity the display driver turns its table off for, each setting's
 * effect at known levels, every channel on its own, monotonic curves across
 * the settings' ranges, clamping, and the DLL's own power function against
 * the C library's.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ramp.h"

static int failures;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            fprintf(stderr, "test_ramp:%d: ", __LINE__);  \
            fprintf(stderr, __VA_ARGS__);                 \
            fputc('\n', stderr);                          \
            failures++;                                   \
        }                                                 \
    } while (0)

/* The ramp's word i of channel c as the formula has it, through the C
   library. */
static double
expected(int gamma, int brightness, int contrast, int i)
{
    double v = pow(i / 255.0, 100.0 / gamma);

    v = (v - 0.5) * (contrast / 100.0) + 0.5 + brightness / 100.0;
    if (v < 0.0)
        v = 0.0;
    if (v > 1.0)
        v = 1.0;
    return v * 65535.0;
}

static void
identity(void)
{
    ramp_settings_t s;
    uint16_t        w[RAMP_WORDS];
    int             i;

    ramp_defaults(&s);
    CHECK(ramp_is_default(&s), "the defaults are not the defaults");
    ramp_build(&s, w);
    /* i * 257: each word's high byte its index, the driver's identity. */
    for (i = 0; i < RAMP_WORDS; i++)
        CHECK(w[i] == (i % 256) * 257, "identity word %d: %04X", i, w[i]);
}

/* Each setting alone, against the formula through the C library: within a
   unit of the 16 bits. */
static void
settings(void)
{
    static const int gammas[]       = { 50, 80, 100, 180, 220, 250 };
    static const int brightnesses[] = { -50, -20, 0, 15, 50 };
    static const int contrasts[]    = { 50, 75, 100, 125, 150 };
    unsigned         g, b, k;
    int              i;

    for (g = 0; g < sizeof(gammas) / sizeof(gammas[0]); g++) {
        for (b = 0; b < sizeof(brightnesses) / sizeof(brightnesses[0]); b++) {
            for (k = 0; k < sizeof(contrasts) / sizeof(contrasts[0]); k++) {
                ramp_settings_t s;
                uint16_t        w[RAMP_WORDS];
                int             c;

                ramp_defaults(&s);
                for (c = 0; c < 3; c++) {
                    s.gamma[c]      = gammas[g];
                    s.brightness[c] = brightnesses[b];
                    s.contrast[c]   = contrasts[k];
                }
                ramp_build(&s, w);
                for (i = 0; i < 256; i++) {
                    const double e = expected(gammas[g], brightnesses[b], contrasts[k], i);

                    CHECK(fabs(w[i] - e) <= 1.0, "gamma %d brightness %d contrast %d, level %d: %u, expected %.1f",
                          gammas[g], brightnesses[b], contrasts[k], i, w[i], e);
                    /* Never falling: the screen's levels keep their order. */
                    if (i)
                        CHECK(w[i] >= w[i - 1], "gamma %d brightness %d contrast %d: level %d below level %d",
                              gammas[g], brightnesses[b], contrasts[k], i, i - 1);
                }
            }
        }
    }
}

/* Known values: gamma 2.2 at 128, a full brightness's floor and ceiling, the
   lowest contrast's ends. */
static void
known(void)
{
    ramp_settings_t s;
    uint16_t        w[RAMP_WORDS];

    ramp_defaults(&s);
    s.gamma[0] = s.gamma[1] = s.gamma[2] = 220;
    ramp_build(&s, w);
    /* (128/255)^(1/2.2) = 0.731038...: 47908.6 of 65535. */
    CHECK((w[128] >= 47908) && (w[128] <= 47909), "gamma 2.2 at 128: %u", w[128]);
    CHECK((w[0] == 0) && (w[255] == 65535), "gamma 2.2's ends: %u, %u", w[0], w[255]);

    ramp_defaults(&s);
    s.brightness[0] = s.brightness[1] = s.brightness[2] = 50;
    ramp_build(&s, w);
    CHECK(w[0] == 32768, "brightness +50 at 0: %u", w[0]);
    CHECK((w[128] == 65535) && (w[255] == 65535), "brightness +50 from 128: %u, %u", w[128], w[255]);

    ramp_defaults(&s);
    s.brightness[0] = s.brightness[1] = s.brightness[2] = -50;
    ramp_build(&s, w);
    CHECK((w[0] == 0) && (w[127] == 0), "brightness -50 to 127: %u, %u", w[0], w[127]);
    CHECK(w[255] == 32768, "brightness -50 at 255: %u", w[255]);

    ramp_defaults(&s);
    s.contrast[0] = s.contrast[1] = s.contrast[2] = 50;
    ramp_build(&s, w);
    CHECK((w[0] == 16384) && (w[255] == 49151), "contrast 50%%'s ends: %u, %u", w[0], w[255]);
}

/* A channel set on its own leaves the other two the identity. */
static void
channels(void)
{
    int c;

    for (c = 0; c < 3; c++) {
        ramp_settings_t s;
        uint16_t        w[RAMP_WORDS];
        int             o;
        int             i;

        ramp_defaults(&s);
        s.gamma[c]      = 180;
        s.brightness[c] = 10;
        s.contrast[c]   = 120;
        CHECK(!ramp_is_default(&s), "channel %d changed is the defaults", c);
        ramp_build(&s, w);
        for (o = 0; o < 3; o++) {
            int same = 1;

            for (i = 0; i < 256; i++)
                same &= w[o * 256 + i] == i * 257;
            CHECK(same == (o != c), "channel %d set: channel %d %s", c, o, same ? "the identity" : "changed");
        }
    }
}

static void
clamping(void)
{
    ramp_settings_t s;
    int             c;

    for (c = 0; c < 3; c++) {
        s.gamma[c]      = (c == 0) ? 10 : 900;
        s.brightness[c] = (c == 0) ? -400 : 400;
        s.contrast[c]   = (c == 0) ? 0 : 1000;
    }
    ramp_clamp(&s);
    CHECK((s.gamma[0] == RAMP_GAMMA_MIN) && (s.gamma[1] == RAMP_GAMMA_MAX), "gamma clamped to %d, %d", s.gamma[0],
          s.gamma[1]);
    CHECK((s.brightness[0] == RAMP_BRIGHTNESS_MIN) && (s.brightness[2] == RAMP_BRIGHTNESS_MAX),
          "brightness clamped to %d, %d", s.brightness[0], s.brightness[2]);
    CHECK((s.contrast[0] == RAMP_CONTRAST_MIN) && (s.contrast[1] == RAMP_CONTRAST_MAX), "contrast clamped to %d, %d",
          s.contrast[0], s.contrast[1]);
}

/* The power function on every level and the exponents the settings give. */
static void
power(void)
{
    static const double ys[] = { 0.4, 1.0 / 2.2, 0.5, 1.0, 1.25, 2.0, 2.5, 4.0 };
    unsigned            k;
    int                 i;

    for (k = 0; k < sizeof(ys) / sizeof(ys[0]); k++) {
        for (i = 0; i <= 255; i++) {
            const double x = i / 255.0;
            const double e = pow(x, ys[k]);
            const double g = ramp_pow(x, ys[k]);

            CHECK(fabs(g - e) <= 1e-9, "%.6f to the %.4f: %.12f, expected %.12f", x, ys[k], g, e);
        }
    }
    CHECK(ramp_pow(0.0, 2.0) == 0.0, "0 to the 2: %f", ramp_pow(0.0, 2.0));
    CHECK(ramp_pow(1.0, 0.4) == 1.0, "1 to the 0.4: %f", ramp_pow(1.0, 0.4));
}

int
main(void)
{
    identity();
    settings();
    known();
    channels();
    clamping();
    power();
    printf("test_ramp: %d failed\n", failures);
    return failures ? 1 : 0;
}

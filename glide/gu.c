/*
 * The Glide DLLs: the utility library's arithmetic both share, which needs
 * no card and is exactly defined, so a program
 * using it before a window opens gets Glide's answer.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glidedll.h"

float FX_CALL guFogTableIndexToW(int i);
void FX_CALL  guFogGenerateExp(FxU8 *fogtable, float density);
void FX_CALL  guFogGenerateExp2(FxU8 *fogtable, float density);
void FX_CALL  guFogGenerateLinear(FxU8 *fogtable, float nearZ, float farZ);
int FX_CALL   guEncodeRLE16(void *dst, void *src, FxU32 width, FxU32 height);

static float
fog_w(int i)
{
    int    e = 3 + (i >> 2);
    double p = 1.0;

    if (e > 128)
        e = 128;
    if (e < -160)
        e = -160;
    for (; e > 0; e--)
        p *= 2.0;
    for (; e < 0; e++)
        p *= 0.5;
    return (float) p / (float) (8 - (i & 3));
}

/* The w that fog table entry I stands for: 2 to the power 3 + I/4 (I/4
   rounded down), over 8 - I mod 4, as Glide computes it in single
   precision. The power of two is exact; one past the floats' range is
   infinity and one below it 0, as Glide's own conversion gives them. */
float FX_CALL
guFogTableIndexToW(int i)
{
    CALLED(guFogTableIndexToW);
    return fog_w(i);
}

/* e to the power x, through the FPU: 2^(x log2 e), as 2^int times
   2^frac. */
static double
exp_f(double x)
{
    double t = x * 1.4426950408889634, r;

    if (t < -1000.0)
        return 0.0;
    if (t > 1000.0)
        t = 1000.0;
    __asm__("fld %%st(0)\n\t"
            "frndint\n\t"
            "fxch\n\t"
            "fsub %%st(1), %%st\n\t"
            "f2xm1\n\t"
            "fld1\n\t"
            "faddp\n\t"
            "fscale\n\t"
            "fstp %%st(1)"
            : "=t"(r)
            : "0"(t));
    return r;
}

/* An entry: clamped to [0, 1], times 255, cut to an integer. */
static FxU8
fog_entry(double f)
{
    if (!(f > 0.0))
        return 0;
    if (f >= 1.0)
        return 255;
    return (FxU8) (f * 255.0);
}

/* The utility library's fog tables, as a Voodoo3's builds them: for entry
   i at w_i, (1 - e^-(d w_i)) / (1 - e^-(d w_63)); the same with (d w)
   squared; and (min(w_i, 65535) - near) / (far - near). */
void FX_CALL
guFogGenerateExp(FxU8 *fogtable, float density)
{
    double last;
    int    i;

    CALLED(guFogGenerateExp);
    if (!fogtable)
        return;
    last = 1.0 - exp_f(-(double) density * fog_w(63));
    for (i = 0; i < 64; i++)
        fogtable[i] = fog_entry((last != 0.0) ? (1.0 - exp_f(-(double) density * fog_w(i))) / last : 0.0);
}

void FX_CALL
guFogGenerateExp2(FxU8 *fogtable, float density)
{
    double d63, last;
    int    i;

    CALLED(guFogGenerateExp2);
    if (!fogtable)
        return;
    d63  = (double) density * fog_w(63);
    last = 1.0 - exp_f(-d63 * d63);
    for (i = 0; i < 64; i++) {
        const double d = (double) density * fog_w(i);

        fogtable[i] = fog_entry((last != 0.0) ? (1.0 - exp_f(-d * d)) / last : 0.0);
    }
}

void FX_CALL
guFogGenerateLinear(FxU8 *fogtable, float nearZ, float farZ)
{
    int i;

    CALLED(guFogGenerateLinear);
    if (!fogtable)
        return;
    for (i = 0; i < 64; i++) {
        double w = fog_w(i);

        if (w > 65535.0)
            w = 65535.0;
        fogtable[i] = fog_entry((farZ != nearZ) ? (w - nearZ) / ((double) farZ - nearZ) : 0.0);
    }
}

/* Run-length 565 pixels: a word for each run of one colour, its length in
   the top half and the colour in the bottom, runs going on across rows;
   with no dst, only counted. The bytes made. (A Voodoo3's encoder stops
   early on pictures of many runs; this one encodes them whole.) */
int FX_CALL
guEncodeRLE16(void *dst, void *src, FxU32 width, FxU32 height)
{
    const uint16_t *in  = (const uint16_t *) src;
    FxU32          *out = (FxU32 *) dst;
    FxU32           n, i = 0, bytes = 0;

    CALLED(guEncodeRLE16);
    if (!src)
        return 0;
    n = width * height;
    while (i < n) {
        const uint16_t c   = in[i];
        FxU32          run = 1;

        while ((i + run < n) && (in[i + run] == c) && (run < 0xffff))
            run++;
        if (out)
            *out++ = (run << 16) | c;
        bytes += 4;
        i += run;
    }
    return (int) bytes;
}

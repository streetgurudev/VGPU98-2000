/*
 * The ICD's arithmetic (glmath.h): the x87's own instructions for the C
 * library's functions, and GL's matrix operations.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glmath.h"

#ifdef __i386__

double
ogl_sqrt(double x)
{
    double r;

    __asm__("fsqrt" : "=t"(r) : "0"(x));
    return r;
}

double
ogl_sin(double x)
{
    double r;

    __asm__("fsin" : "=t"(r) : "0"(x));
    return r;
}

double
ogl_cos(double x)
{
    double r;

    __asm__("fcos" : "=t"(r) : "0"(x));
    return r;
}

double
ogl_fabs(double x)
{
    double r;

    __asm__("fabs" : "=t"(r) : "0"(x));
    return r;
}

/* x rounded by the x87's current mode (nearest). */
static double
rint87(double x)
{
    double r;

    __asm__("frndint" : "=t"(r) : "0"(x));
    return r;
}

double
ogl_floor(double x)
{
    double r = rint87(x);

    return (r > x) ? r - 1.0 : r;
}

double
ogl_ceil(double x)
{
    double r = rint87(x);

    return (r < x) ? r + 1.0 : r;
}

/* 2 to the power x. */
static double
exp2_87(double x)
{
    const double i = rint87(x);
    double       f = x - i, r;

    /* f2xm1 takes -1 to 1: 2^f - 1; then scaled by 2^i. */
    __asm__("f2xm1" : "=t"(f) : "0"(f));
    f += 1.0;
    __asm__("fscale" : "=t"(r) : "0"(f), "u"(i));
    return r;
}

/* log2(x), x > 0. */
static double
log2_87(double x)
{
    double r;

    __asm__("fyl2x" : "=t"(r) : "0"(x), "u"(1.0) : "st(1)");
    return r;
}

double
ogl_pow(double x, double y)
{
    if (x <= 0.0)
        return (x == 0.0) ? ((y > 0.0) ? 0.0 : 1.0) : 0.0;
    if (y == 0.0)
        return 1.0;
    return exp2_87(y * log2_87(x));
}

double
ogl_exp(double x)
{
    if (x < -1000.0)
        return 0.0;
    return exp2_87(x * 1.4426950408889634);
}

double
ogl_log(double x)
{
    return log2_87(x) * 0.6931471805599453;
}

#else /* the host's tests */

#    include <math.h>

double
ogl_sqrt(double x)
{
    return sqrt(x);
}

double
ogl_sin(double x)
{
    return sin(x);
}

double
ogl_cos(double x)
{
    return cos(x);
}

double
ogl_fabs(double x)
{
    return fabs(x);
}

double
ogl_floor(double x)
{
    return floor(x);
}

double
ogl_ceil(double x)
{
    return ceil(x);
}

double
ogl_pow(double x, double y)
{
    if (x <= 0.0)
        return (x == 0.0) ? ((y > 0.0) ? 0.0 : 1.0) : 0.0;
    return pow(x, y);
}

double
ogl_exp(double x)
{
    return exp(x);
}

double
ogl_log(double x)
{
    return log(x);
}

#endif

int32_t
ogl_round(double v)
{
    if (!(v == v))
        return 0;
    if (v >= 2147483647.0)
        return 2147483647;
    if (v <= -2147483648.0)
        return (int32_t) 0x80000000u;
    return (int32_t) ((v >= 0.0) ? ogl_floor(v + 0.5) : -ogl_floor(-v + 0.5));
}

void
ogl_mat_identity(float *m)
{
    int i;

    for (i = 0; i < 16; i++)
        m[i] = (i % 5) ? 0.0f : 1.0f;
}

void
ogl_mat_mul(float *out, const float *a, const float *b)
{
    float r[16];
    int   c, k;

    for (c = 0; c < 4; c++) {
        for (k = 0; k < 4; k++)
            r[c * 4 + k] = a[k] * b[c * 4] + a[4 + k] * b[c * 4 + 1] + a[8 + k] * b[c * 4 + 2] + a[12 + k] * b[c * 4 + 3];
    }
    memcpy(out, r, sizeof(r));
}

void
ogl_mat_vec(float *out, const float *m, const float *v)
{
    float r[4];
    int   k;

    for (k = 0; k < 4; k++)
        r[k] = m[k] * v[0] + m[4 + k] * v[1] + m[8 + k] * v[2] + m[12 + k] * v[3];
    memcpy(out, r, sizeof(r));
}

/* Gauss-Jordan with partial pivoting, in double. */
int
ogl_mat_invert(float *out, const float *m)
{
    double a[4][8];
    int    r, c, k;

    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++) {
            a[r][c]     = m[c * 4 + r];
            a[r][c + 4] = (r == c) ? 1.0 : 0.0;
        }
    }
    for (c = 0; c < 4; c++) {
        int    p    = c;
        double best = ogl_fabs(a[c][c]);

        for (r = c + 1; r < 4; r++) {
            if (ogl_fabs(a[r][c]) > best) {
                best = ogl_fabs(a[r][c]);
                p    = r;
            }
        }
        if (best == 0.0 || !(best == best)) {
            ogl_mat_identity(out);
            return 0;
        }
        if (p != c) {
            for (k = 0; k < 8; k++) {
                const double t = a[c][k];

                a[c][k] = a[p][k];
                a[p][k] = t;
            }
        }
        {
            const double d = a[c][c];

            for (k = 0; k < 8; k++)
                a[c][k] /= d;
        }
        for (r = 0; r < 4; r++) {
            if (r != c) {
                const double f = a[r][c];

                if (f != 0.0) {
                    for (k = 0; k < 8; k++)
                        a[r][k] -= f * a[c][k];
                }
            }
        }
    }
    for (r = 0; r < 4; r++) {
        for (c = 0; c < 4; c++)
            out[c * 4 + r] = (float) a[r][c + 4];
    }
    return 1;
}

void
ogl_plane_through(double *out, const double *p, const float *inv)
{
    double r[4];
    int    c;

    /* The row vector p times the matrix: element c = sum over k of p[k]
       times inverse (row k, column c). */
    for (c = 0; c < 4; c++)
        r[c] = p[0] * inv[c * 4] + p[1] * inv[c * 4 + 1] + p[2] * inv[c * 4 + 2] + p[3] * inv[c * 4 + 3];
    memcpy(out, r, sizeof(r));
}

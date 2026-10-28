/*
 * The ICD's arithmetic: the few C library functions
 * GL's math needs, on the x87 (the DLL links no C library), and the 4x4
 * matrix operations GL defines. On the host (the tests) the C library's.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_ICD_GLMATH_H
#define VGPU98_ICD_GLMATH_H

#include "icd.h"

double ogl_sqrt(double x);
double ogl_sin(double x);
double ogl_cos(double x);
double ogl_floor(double x);
double ogl_ceil(double x);
double ogl_pow(double x, double y); /* x > 0, or x 0 with y > 0 */
double ogl_exp(double x);
double ogl_log(double x);           /* x > 0 */
double ogl_fabs(double x);

static inline float
ogl_clampf(float v, float lo, float hi)
{
    return (v > lo) ? ((v < hi) ? v : hi) : lo; /* not a number: lo */
}

static inline int
ogl_finite(float v)
{
    return (v == v) && (v - v == 0.0f);
}

/* A float to the nearest integer (halves away from zero), clamped to an
   int's range. */
int32_t ogl_round(double v);

/* GL's matrices: 16 floats, column-major (element (row r, column c) at
   [c * 4 + r]). */
void ogl_mat_identity(float *m);
/* out = a * b (out may be a or b). */
void ogl_mat_mul(float *out, const float *a, const float *b);
/* out = the inverse of m; 0 (out the identity) if m is singular. */
int  ogl_mat_invert(float *out, const float *m);
/* out = m * v (v four floats). */
void ogl_mat_vec(float *out, const float *m, const float *v);
/* A plane (a, b, c, d) through the inverse of m: p * m^-1, as GL takes
   clip planes and eye planes into eye coordinates. */
void ogl_plane_through(double *out, const double *p, const float *inverse);

#endif /* VGPU98_ICD_GLMATH_H */

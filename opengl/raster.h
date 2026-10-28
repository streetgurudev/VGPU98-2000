/*
 * The ICD's own vertex path (raster.c):
 * a vertex as GL 1.1's per-vertex operations leave it, clipping, and the
 * viewport.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_ICD_RASTER_H
#define VGPU98_ICD_RASTER_H

#include "prim.h"

typedef struct ogl_cvtx {
    float   eye[4];
    float   clip[4];
    float   front[4]; /* the lit (or current) colour, clamped, for the front face */
    float   back[4];  /* for the back face (two-sided lighting; else the front's) */
    float   tex[OGL_UNITS][4]; /* each unit's, generated and through its texture matrix */
    float   fog;      /* the fog distance: |z| in eye coordinates */
    uint8_t edge;
} ogl_cvtx_t;

#define OGL_CLIP_MAX 512 /* a polygon's vertices through the clip at most (the rest dropped) */

/* A vertex through transform, lighting (mats: its front and back
   materials) and texture coordinates. The modelview's inverse transpose
   must be current (ogl_normal_matrix). */
void ogl_cpu_vertex(const ogl_state_t *s, const ogl_vtx_t *in, const ogl_material_t *mats, ogl_cvtx_t *out);
/* Clipping: a point inside (1), a line clipped (0: wholly outside), a
   polygon clipped into out (its count, 0 when wholly outside). */
int  ogl_clip_point(const ogl_state_t *s, const ogl_cvtx_t *v);
int  ogl_clip_line(const ogl_state_t *s, ogl_cvtx_t *a, ogl_cvtx_t *b);
int  ogl_clip_polygon(const ogl_state_t *s, const ogl_cvtx_t *in, int n, ogl_cvtx_t *out);
/* Window coordinates x, y, z of a clipped vertex. */
void ogl_window(const ogl_state_t *s, const ogl_cvtx_t *v, float *win);
/* A selection hit's depth (window z). */
void ogl_select_hit(ogl_state_t *s, float z);
/* glDrawPixels', glCopyPixels' and glBitmap's token and hit in the render
   modes, at the raster position. */
void ogl_raster_token(ogl_ctx_t *c, GLenum token);

/* What the ICD's path draws in GL_RENDER, clipped (draw.c): a point; a
   line (its colours in front; `reset` starts the stipple counter again); a
   filled polygon (its colours in front, its first vertex the provoking
   one). `polygon`: a polygon's vertex or edge (its polygon mode). */
void ogl_draw_cpu_point(ogl_ctx_t *c, const ogl_cvtx_t *v, const float *color, int polygon);
void ogl_draw_cpu_line(ogl_ctx_t *c, const ogl_cvtx_t *a, const ogl_cvtx_t *b, int reset, int polygon);
void ogl_draw_cpu_polygon(ogl_ctx_t *c, const ogl_cvtx_t *v, int n);

#endif /* VGPU98_ICD_RASTER_H */

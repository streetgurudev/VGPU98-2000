/*
 * Primitives between glBegin and glEnd (GL 1.1, sections 2.6 and 2.7):
 * each vertex with the current values it takes, gathered until glEnd, then
 * drawn: through the ICD's own vertex path in the selection and feedback
 * render modes and where GL's rules need it (raster.c), else
 * as the device's DRAW (draw.c).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_ICD_PRIM_H
#define VGPU98_ICD_PRIM_H

#include "state.h"

typedef struct ogl_vtx {
    float   obj[4];    /* object coordinates */
    float   color[4];  /* the current colour as given (not clamped) */
    float   normal[3];
    float   tex[OGL_UNITS][4]; /* each unit's texture coordinates */
    float   index;
    uint8_t edge;      /* the edge flag */
    uint8_t mat;       /* the materials it takes: prim.c's copy n (0: those at glBegin) */
} ogl_vtx_t;

#define OGL_PRIM_MATERIALS 255

typedef struct ogl_prim {
    GLenum          mode;
    ogl_vtx_t      *v;
    DWORD           n, cap;
    ogl_material_t (*mats)[2]; /* the materials as glBegin had them, then each glMaterial's between */
    int             n_mats;
} ogl_prim_t;

/* glBegin's check and start, a vertex, glEnd (prim.c). */
void ogl_prim_begin(ogl_ctx_t *c, GLenum mode);
void ogl_prim_vertex(ogl_ctx_t *c, const float *obj);
/* A vertex with values of its own (an evaluator's: the current values are
   left as they are). `tex` is unit 0's (evaluators feed unit 0, as
   glTexCoord does); the other units take their current coordinates. */
void ogl_prim_vertex_with(ogl_ctx_t *c, const float *obj, const float *color, const float *normal, const float *tex,
                          float index);
void ogl_prim_end(ogl_ctx_t *c);
/* glMaterial between glBegin and glEnd: the vertices after it take it. */
void ogl_prim_material(ogl_ctx_t *c);
void ogl_prim_free(ogl_ctx_t *c);

/* Colour material: the current colour into the tracked materials (light.c). */
void ogl_color_material(ogl_ctx_t *c);
void ogl_material_set(ogl_material_t *m, GLenum pname, const float *v);

/* What a primitive becomes once gathered (prim.c's glEnd): through the
   ICD's own path (raster.c) in GL_SELECT and GL_FEEDBACK, and in GL_RENDER
   where GL's rules need it; else the device's DRAW (draw.c). */
void ogl_cpu_prim(ogl_ctx_t *c, const ogl_prim_t *p);
void ogl_draw_prim(ogl_ctx_t *c, const ogl_prim_t *p);

/* Vertices for the device's DRAW from elsewhere than glBegin and glEnd:
   the vertex arrays drawn directly (arrays.c).
   n vertices in primitive order, each with a mark below
   `marks` (the vertices of one mark are one vertex, sent once a DRAW) and
   its values in DRAW's layout (VGPU98_OGLV_*). */
typedef struct ogl_vsrc {
    DWORD n, marks;
    DWORD (*mark)(const struct ogl_vsrc *s, DWORD i);
    void  (*vertex)(const struct ogl_vsrc *s, DWORD i, float *out);
    ogl_ctx_t  *c;
    const void *priv;
} ogl_vsrc_t;

/* Would a primitive of this mode take the device's path in GL_RENDER (no
   polygon mode but GL_FILL, no stippled strip or loop)? */
int  ogl_draw_direct_ok(const ogl_state_t *s, GLenum mode);
/* The vertices into the device's DRAW as a primitive of the mode. 0 when
   there is no room to mark them (nothing drawn, no error recorded). */
int  ogl_draw_src(ogl_ctx_t *c, GLenum mode, const ogl_vsrc_t *src);
/* The context's DRAW being built, freed with it (draw.c). */
void ogl_draw_free(ogl_ctx_t *c);

#endif /* VGPU98_ICD_PRIM_H */

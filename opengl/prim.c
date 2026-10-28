/*
 * Primitives between glBegin and glEnd (prim.h): glBegin's rules, each
 * vertex with the current values, glMaterial between them, and glEnd
 * handing the primitive on.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "prim.h"
#include "regs.h"

static ogl_prim_t *
prim_of(ogl_ctx_t *c)
{
    if (!c->gl->prim)
        c->gl->prim = ogl_alloc(sizeof(ogl_prim_t));
    return c->gl->prim;
}

void
ogl_prim_free(ogl_ctx_t *c)
{
    ogl_prim_t *p = c->gl->prim;

    if (!p)
        return;
    ogl_free(p->v);
    ogl_free(p->mats);
    ogl_free(p);
    c->gl->prim = NULL;
}

void
ogl_prim_begin(ogl_ctx_t *c, GLenum mode)
{
    ogl_prim_t *p;

    if (mode > GL_POLYGON) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    p = prim_of(c);
    if (!p) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    if (!p->mats) {
        p->mats = ogl_alloc((OGL_PRIM_MATERIALS + 1) * sizeof(*p->mats));
        if (!p->mats) {
            ogl_error(c, GL_OUT_OF_MEMORY);
            return;
        }
    }
    p->mode   = mode;
    p->n      = 0;
    p->n_mats = 1;
    memcpy(p->mats[0], c->gl->lighting.material, sizeof(p->mats[0]));
    c->gl->in_begin   = 1;
    c->gl->begin_mode = mode;
}

/* A vertex with the values given (the current ones, or an evaluator's):
   outside glBegin and glEnd it does nothing (GL leaves it undefined). */
void
ogl_prim_vertex_with(ogl_ctx_t *c, const float *obj, const float *color, const float *normal, const float *tex, float index)
{
    ogl_prim_t *p = c->gl->prim;
    ogl_vtx_t  *v;
    int         u;

    if (!c->gl->in_begin || !p)
        return;
    if (p->n == p->cap) {
        const DWORD cap = p->cap ? 2 * p->cap : 256;
        ogl_vtx_t  *nv;

        if (cap > 0x1000000u) {
            ogl_error(c, GL_OUT_OF_MEMORY);
            return;
        }
        nv = ogl_realloc(p->v, cap * sizeof(ogl_vtx_t));
        if (!nv) {
            ogl_error(c, GL_OUT_OF_MEMORY);
            return;
        }
        p->v   = nv;
        p->cap = cap;
    }
    v = &p->v[p->n++];
    memcpy(v->obj, obj, sizeof(v->obj));
    memcpy(v->color, color, sizeof(v->color));
    memcpy(v->normal, normal, sizeof(v->normal));
    memcpy(v->tex[0], tex, sizeof(v->tex[0]));
    for (u = 1; u < OGL_UNITS; u++) /* the other units: their current coordinates */
        memcpy(v->tex[u], c->gl->current.texcoord[u], sizeof(v->tex[u]));
    v->index = index;
    v->edge  = c->gl->current.edge_flag;
    v->mat   = (uint8_t) (p->n_mats - 1);
}

void
ogl_prim_vertex(ogl_ctx_t *c, const float *obj)
{
    const ogl_current_t *cur = &c->gl->current;

    ogl_prim_vertex_with(c, obj, cur->color, cur->normal, cur->texcoord[0], cur->index);
}

/* glMaterial between glBegin and glEnd: a copy of the materials now for the
   vertices after it (the last copy reused past OGL_PRIM_MATERIALS). */
void
ogl_prim_material(ogl_ctx_t *c)
{
    ogl_prim_t *p = c->gl->prim;

    if (!p || !p->mats)
        return;
    if (p->n_mats <= OGL_PRIM_MATERIALS)
        p->n_mats++;
    memcpy(p->mats[p->n_mats - 1], c->gl->lighting.material, sizeof(p->mats[0]));
}

void
ogl_prim_end(ogl_ctx_t *c)
{
    ogl_prim_t *p = c->gl->prim;

    if (!c->gl->in_begin || !p) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    c->gl->in_begin   = 0;
    c->gl->begin_mode = GL_NONE;
    if (c->gl->render_mode != GL_RENDER)
        ogl_cpu_prim(c, p);
    else
        ogl_draw_prim(c, p);
    /* Materials changed between glBegin and glEnd stay as the last set them. */
    if (p->n_mats > 1)
        ogl_regs_lighting(c);
    p->n = 0;
}

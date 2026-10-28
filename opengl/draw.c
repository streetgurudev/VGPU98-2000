/*
 * Primitives drawn by the device:
 * what glBegin and glEnd gathered, and the vertex arrays drawn
 * directly, as the DRAW command's
 * unique vertices and 16-bit indices. A vertex source marks its vertices:
 * glBegin's by their place, the arrays' by their element, so an element
 * given twice is sent once a DRAW.
 *
 * - The device's path: each vertex as given (object coordinates, the
 *   current colour, normal and texture coordinates); the device transforms
 *   and lights it. Quads, strips, fans and polygons become triangles with
 *   GL's provoking vertex first; strips and loops of lines become lines.
 * - The ICD's own path (raster.c), where GL's rules do not map onto the
 *   device's: polygon modes other than GL_FILL, glMaterial between glBegin
 *   and glEnd, and stippled line strips and loops. Its points, lines and
 *   triangles reach the device clipped, in clip coordinates, lit and with
 *   their final texture coordinates (DRAW_CLIP).
 *
 * Consecutive primitives of one kind share a DRAW: it is built here and
 * goes into the batch before any other command (device.c's ogl_cmd), when
 * its kind changes, or when it is full.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glmath.h"
#include "prim.h"
#include "raster.h"
#include "regs.h"

/* A DRAW's most: 4 + 768 * 16 + 4096 / 2 dwords, inside the largest packet;
   with DRAW_TEX1's 20-float vertices, 640 of them. */
#define DB_VMAX  768
#define DB_VMAX1 640
#define DB_IMAX 4096

typedef struct ogl_draw {
    /* The DRAW being built */
    DWORD    control;
    DWORD    nv, ni;
    float    v[DB_VMAX * VGPU98_OGL_VERTEX1_DWORDS]; /* a vertex source writes 20 floats at the next place */
    uint16_t ix[DB_IMAX];
    /* The device's path shares a vertex among the primitives that use it:
       stamp[i] is `pass` while source vertex i is in the DRAW, at local[i]. */
    DWORD    *stamp;
    uint16_t *local;
    DWORD     cap;
    DWORD     pass;
    /* The ICD's path: the line stipple counter, in fragments */
    float stipple;
} ogl_draw_t;

static ogl_draw_t *
draw_of(ogl_ctx_t *c)
{
    if (!c->gl->draw) {
        c->gl->draw = ogl_alloc(sizeof(ogl_draw_t));
        if (c->gl->draw)
            c->gl->draw->pass = 1;
    }
    return c->gl->draw;
}

/* A DRAW's vertex size by its control: 20 floats with unit 1's coordinates
   (DRAW_TEX1), else 16. */
static DWORD
vd_of(DWORD control)
{
    return (control & VGPU98_OGL_DRAW_TEX1) ? VGPU98_OGL_VERTEX1_DWORDS : VGPU98_OGL_VERTEX_DWORDS;
}

/* DRAW_TEX1 while unit 1 draws with a texture (its register, which only a
   2.6 device has, as sent before the DRAW: draw.c's prepare). */
static DWORD
tex1(const ogl_ctx_t *c)
{
    return c->reg[VGPU98_OGL_R_UNIT1 + VGPU98_OGL_U_TEXTURE_ID] ? VGPU98_OGL_DRAW_TEX1 : 0;
}

/* The next pass: past 2^32 the stamps start again from zero, so one a lap
   old is never taken for this one (audit 3, item 4). */
static void
next_pass(ogl_draw_t *d)
{
    if (!++d->pass) {
        if (d->stamp)
            memset(d->stamp, 0, d->cap * sizeof(DWORD));
        d->pass = 1;
    }
}

void
ogl_draw_free(ogl_ctx_t *c)
{
    ogl_draw_t *d = c->gl->draw;

    if (!d)
        return;
    ogl_free(d->stamp);
    ogl_free(d->local);
    ogl_free(d);
    c->gl->draw = NULL;
}

void
ogl_draw_flush(ogl_ctx_t *c)
{
    ogl_draw_t *d = c->gl ? c->gl->draw : NULL;
    DWORD       nv, ni, k, vd, *p;

    if (!d || !d->ni)
        return;
    /* Taken first: the batch may be sent to make room (ogl_flush comes
       back here, to nothing). */
    nv    = d->nv;
    ni    = d->ni;
    d->nv = d->ni = 0;
    next_pass(d);
    vd    = vd_of(d->control);
    p = ogl_cmd(c, VGPU98_OGL_DRAW, VGPU98_OGL_LEN_DRAW + nv * vd + (ni + 1) / 2);
    if (!p)
        return;
    p[0] = c->id;
    p[1] = d->control;
    p[2] = nv;
    p[3] = ni;
    memcpy(&p[VGPU98_OGL_LEN_DRAW], d->v, nv * vd * 4);
    p += VGPU98_OGL_LEN_DRAW + nv * vd;
    for (k = 0; k < ni; k += 2)
        p[k / 2] = d->ix[k] | ((k + 1 < ni) ? (DWORD) d->ix[k + 1] << 16 : 0);
    /* Drawn into the front buffer: shown soon. */
    if ((c->reg[VGPU98_OGL_R_DRAW_BUFFERS] & 1u) && c->win) {
        c->win->front_drawn = 1;
        ogl_front_show(c, 0);
    }
}

/* The DRAW being built takes this control (one of another goes first). */
static void
control(ogl_ctx_t *c, ogl_draw_t *d, DWORD ctl)
{
    if (d->ni && (d->control != ctl))
        ogl_draw_flush(c);
    d->control = ctl;
}

/* Room for nv vertices and ni indices (the DRAW goes first if not). */
static void
room(ogl_ctx_t *c, ogl_draw_t *d, DWORD nv, DWORD ni)
{
    if ((d->nv + nv > ((d->control & VGPU98_OGL_DRAW_TEX1) ? DB_VMAX1 : DB_VMAX)) || (d->ni + ni > DB_IMAX))
        ogl_draw_flush(c);
}

/* --- The device's path ---------------------------------------------------------------------- */

/* A source vertex once a DRAW: its mark's stamp is `pass` while it is in
   the DRAW, at local[mark]. */
static void
dev_add(ogl_ctx_t *c, ogl_draw_t *d, const ogl_vsrc_t *src, DWORD k, DWORD i0, DWORD i1, DWORD i2)
{
    const DWORD idx[3] = { i0, i1, i2 };
    DWORD       mk[3], j, fresh = 0;

    for (j = 0; j < k; j++) {
        mk[j] = src->mark(src, idx[j]);
        fresh += d->stamp[mk[j]] != d->pass;
    }
    room(c, d, fresh, k);
    for (j = 0; j < k; j++) {
        const DWORD m = mk[j];

        if (d->stamp[m] != d->pass) {
            d->stamp[m] = d->pass;
            d->local[m] = (uint16_t) d->nv;
            src->vertex(src, idx[j], &d->v[d->nv * vd_of(d->control)]);
            d->nv++;
        }
        d->ix[d->ni++] = d->local[m];
    }
}

/* Room for the source's marks in the stamps. 0 without memory (the error
   recorded when `report`). */
static int
dev_marks(ogl_ctx_t *c, ogl_draw_t *d, DWORD marks, int report)
{
    DWORD    *st;
    uint16_t *lo;

    if (marks <= d->cap)
        return 1;
    st = ogl_realloc(d->stamp, marks * sizeof(DWORD));
    if (st)
        d->stamp = st;
    lo = st ? ogl_realloc(d->local, marks * sizeof(uint16_t)) : NULL;
    if (!lo) {
        if (report)
            ogl_error(c, GL_OUT_OF_MEMORY);
        return 0;
    }
    d->local = lo;
    d->cap   = marks;
    next_pass(d); /* the new entries are zero: never a pass */
    return 1;
}

/* The source's vertices as a primitive of the mode: points, lines or
   triangles, GL's provoking vertex kept. */
static void
dev_shape(ogl_ctx_t *c, ogl_draw_t *d, GLenum mode, const ogl_vsrc_t *src)
{
    const DWORD n = src->n;
    DWORD       i, kind;

    switch (mode) {
        case GL_POINTS:
            kind = VGPU98_OGL_POINTS;
            break;
        case GL_LINES:
        case GL_LINE_STRIP:
        case GL_LINE_LOOP:
            kind = VGPU98_OGL_LINES;
            break;
        default:
            kind = VGPU98_OGL_TRIANGLES;
            break;
    }
    control(c, d, kind | tex1(c));
    /* This primitive's vertices are new to the DRAW, whatever came before. */
    next_pass(d);
    switch (mode) {
        case GL_POINTS:
            for (i = 0; i < n; i++)
                dev_add(c, d, src, 1, i, 0, 0);
            break;
        case GL_LINES:
            for (i = 0; i + 1 < n; i += 2)
                dev_add(c, d, src, 2, i, i + 1, 0);
            break;
        case GL_LINE_STRIP:
        case GL_LINE_LOOP:
            for (i = 0; i + 1 < n; i++)
                dev_add(c, d, src, 2, i, i + 1, 0);
            if ((mode == GL_LINE_LOOP) && (n > 1))
                dev_add(c, d, src, 2, n - 1, 0, 0);
            break;
        /* Triangles with GL's provoking vertex first (GL 1.1, table 2.2),
           each a rotation of GL's order, so its winding is kept. */
        case GL_TRIANGLES:
            for (i = 0; i + 2 < n; i += 3)
                dev_add(c, d, src, 3, i + 2, i, i + 1);
            break;
        case GL_TRIANGLE_STRIP:
            for (i = 0; i + 2 < n; i++) {
                if (i & 1)
                    dev_add(c, d, src, 3, i + 2, i + 1, i);
                else
                    dev_add(c, d, src, 3, i + 2, i, i + 1);
            }
            break;
        case GL_TRIANGLE_FAN:
            for (i = 1; i + 1 < n; i++)
                dev_add(c, d, src, 3, i + 1, 0, i);
            break;
        case GL_QUADS:
            /* Split along the diagonal from the provoking vertex (the
               fourth), so both halves hold it. */
            for (i = 0; i + 3 < n; i += 4) {
                dev_add(c, d, src, 3, i + 3, i, i + 1);
                dev_add(c, d, src, 3, i + 3, i + 1, i + 2);
            }
            break;
        case GL_QUAD_STRIP:
            /* Quad j is 2j, 2j+1, 2j+3, 2j+2 in order; 2j+3 provokes. */
            for (i = 0; i + 3 < n; i += 2) {
                dev_add(c, d, src, 3, i + 3, i + 2, i);
                dev_add(c, d, src, 3, i + 3, i, i + 1);
            }
            break;
        default: /* GL_POLYGON: its first vertex provokes */
            for (i = 1; i + 1 < n; i++)
                dev_add(c, d, src, 3, 0, i, i + 1);
            break;
    }
}

/* glBegin's vertices as a source: vertex i is the primitive's i-th, its
   own mark (a vertex given twice is two). */
static DWORD
prim_mark(const ogl_vsrc_t *s, DWORD i)
{
    (void) s;
    return i;
}

static void
prim_vertex(const ogl_vsrc_t *s, DWORD i, float *out)
{
    const ogl_vtx_t *v = &((const ogl_prim_t *) s->priv)->v[i];

    memcpy(&out[VGPU98_OGLV_X], v->obj, 4 * sizeof(float));
    memcpy(&out[VGPU98_OGLV_R], v->color, 4 * sizeof(float));
    memcpy(&out[VGPU98_OGLV_NX], v->normal, 3 * sizeof(float));
    out[VGPU98_OGLV_STIPPLE] = 0.0f;
    memcpy(&out[VGPU98_OGLV_S], v->tex[0], 4 * sizeof(float));
    memcpy(&out[VGPU98_OGLV_S1], v->tex[1], 4 * sizeof(float));
}

/* Does a primitive need the ICD's own path? `mats`: the
   materials glMaterial between glBegin and glEnd gave (1: none). Its
   reason, for the diagnostics' report: 1 materials with lighting, 2
   polygon modes, 3 a stippled strip; 0 none. */
static int
cpu_path(const ogl_state_t *s, GLenum mode, int mats)
{
    if ((mats > 1) && (s->enables & OGL_E_LIGHTING))
        return 1;
    if ((mode >= GL_TRIANGLES) && ((s->raster.polygon_mode[0] != GL_FILL) || (s->raster.polygon_mode[1] != GL_FILL)))
        return 2;
    return (((mode == GL_LINE_STRIP) || (mode == GL_LINE_LOOP)) && (s->enables & OGL_E_LINE_STIPPLE)) ? 3 : 0;
}

int
ogl_draw_direct_ok(const ogl_state_t *s, GLenum mode)
{
    return (s->render_mode == GL_RENDER) && !cpu_path(s, mode, 1);
}

/* Ready to draw: the objects on the device, the front image loaded when
   drawing into it, the registers as they are now sent (any DRAW built with
   the old ones first). The DRAW being built; NULL when nothing can be
   drawn. */
static ogl_draw_t *
prepare(ogl_ctx_t *c)
{
    const ogl_window_t *w = c->win;
    ogl_draw_t         *d;

    /* The window's size is looked at again by glClear, the swap and
       wglMakeCurrent, not at every primitive. */
    if (!c->opened || !w || !w->drawable || (c->bound != w->drawable)) {
        if (!ogl_objects(c))
            return NULL;
    }
    d = draw_of(c);
    if (!d) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return NULL;
    }
    if (c->reg[VGPU98_OGL_R_DRAW_BUFFERS] & 1u)
        ogl_front_load(c);
    ogl_sync(c);
    /* Diagnostics "OpenGL": a draw with a unit's texturing on and a texture
       that cannot draw */
    if (diag_on && (ogl_dg.untex[0].on || ogl_dg.untex[1].on))
        ogl_diag_untextured_draw();
    return d;
}

void
ogl_draw_prim(ogl_ctx_t *c, const ogl_prim_t *p)
{
    ogl_draw_t *d;
    ogl_vsrc_t  src;
    int         why;

    if (!p->n)
        return;
    d = prepare(c);
    if (!d)
        return;
    why = cpu_path(c->gl, p->mode, p->n_mats);
    if (why) {
        ogl_dg.prims_cpu[why - 1]++;
        ogl_cpu_prim(c, p);
        return;
    }
    ogl_dg.prims_dev++;
    if (!dev_marks(c, d, p->n, 1))
        return;
    src.n      = p->n;
    src.marks  = p->n;
    src.mark   = prim_mark;
    src.vertex = prim_vertex;
    src.c      = c;
    src.priv   = p;
    dev_shape(c, d, p->mode, &src);
}

int
ogl_draw_src(ogl_ctx_t *c, GLenum mode, const ogl_vsrc_t *src)
{
    ogl_draw_t *d;

    if (!src->n)
        return 1;
    d = prepare(c);
    if (!d)
        return 1; /* nothing to draw into */
    if (!dev_marks(c, d, src->marks, 0))
        return 0; /* no room for the marks: the caller's other way (item 3) */
    ogl_dg.arrays_direct++;
    dev_shape(c, d, mode, src);
    return 1;
}


/* --- The ICD's path (raster.c's primitives in GL_RENDER) ---------------------------------- */

static void
cpu_vertex(float *out, const ogl_cvtx_t *v, const float *color, float stipple)
{
    memcpy(&out[VGPU98_OGLV_X], v->clip, 4 * sizeof(float));
    memcpy(&out[VGPU98_OGLV_R], color, 4 * sizeof(float));
    out[VGPU98_OGLV_NX]      = v->fog;
    out[VGPU98_OGLV_NY]      = 0.0f;
    out[VGPU98_OGLV_NZ]      = 0.0f;
    out[VGPU98_OGLV_STIPPLE] = stipple;
    memcpy(&out[VGPU98_OGLV_S], v->tex[0], 4 * sizeof(float));
    memcpy(&out[VGPU98_OGLV_S1], v->tex[1], 4 * sizeof(float));
}

void
ogl_draw_cpu_point(ogl_ctx_t *c, const ogl_cvtx_t *v, const float *color, int polygon)
{
    ogl_draw_t *d = draw_of(c);
    float       size = 0.0f, col[4];

    if (!d)
        return;
    memcpy(col, color, sizeof(col));
    /* EXT_point_parameters: the size by the eye distance in
       the STIPPLE place, the fade in the colour's alpha (0: the device's
       POINT_SIZE, as GL 1.1) */
    if (ogl_point_params_on(&c->gl->raster)) {
        const double w = (v->eye[3] != 0.0f) ? v->eye[3] : 1.0;
        const double d2 = ((double) v->eye[0] * v->eye[0] + (double) v->eye[1] * v->eye[1] + (double) v->eye[2] * v->eye[2]) / (w * w);
        float        fade;

        size = ogl_point_size(&c->gl->raster, (float) ogl_sqrt(d2), &fade);
        col[3] *= fade;
        if (!(size > 0.0f))
            size = 1.0f / 1024.0f; /* above 0, so the device takes it (and keeps it to 1 at least) */
    }
    control(c, d, VGPU98_OGL_POINTS | VGPU98_OGL_DRAW_CLIP | (polygon ? VGPU98_OGL_DRAW_POLYGON : 0) | tex1(c));
    room(c, d, 1, 1);
    cpu_vertex(&d->v[d->nv * vd_of(d->control)], v, col, size);
    d->ix[d->ni++] = (uint16_t) d->nv++;
}

void
ogl_draw_cpu_line(ogl_ctx_t *c, const ogl_cvtx_t *a, const ogl_cvtx_t *b, int reset, int polygon)
{
    ogl_draw_t *d = draw_of(c);
    float       wa[3], wb[3], dx, dy;

    if (!d)
        return;
    /* The stipple counter goes on along a strip or a polygon's edges: the
       line's start, then its length in fragments (the major axis's). */
    if (reset)
        d->stipple = 0.0f;
    ogl_window(c->gl, a, wa);
    ogl_window(c->gl, b, wb);
    control(c, d, VGPU98_OGL_LINES | VGPU98_OGL_DRAW_CLIP | VGPU98_OGL_DRAW_STIPPLE_ON | (polygon ? VGPU98_OGL_DRAW_POLYGON : 0) | tex1(c));
    room(c, d, 2, 2);
    cpu_vertex(&d->v[d->nv * vd_of(d->control)], a, a->front, d->stipple);
    d->ix[d->ni++] = (uint16_t) d->nv++;
    dx = wb[0] - wa[0];
    dy = wb[1] - wa[1];
    if (dx < 0.0f)
        dx = -dx;
    if (dy < 0.0f)
        dy = -dy;
    d->stipple += (dx > dy) ? dx : dy;
    if (!(d->stipple < 65536.0f * 256.0f))
        d->stipple = 0.0f; /* far past any pattern's period, or not a number */
    cpu_vertex(&d->v[d->nv * vd_of(d->control)], b, b->front, d->stipple);
    d->ix[d->ni++] = (uint16_t) d->nv++;
}

void
ogl_draw_cpu_polygon(ogl_ctx_t *c, const ogl_cvtx_t *v, int n)
{
    ogl_draw_t *d = draw_of(c);
    DWORD       first;
    int         i;

    if (!d || (n < 3))
        return;
    if (n > DB_VMAX1)
        n = DB_VMAX1;
    control(c, d, VGPU98_OGL_TRIANGLES | VGPU98_OGL_DRAW_CLIP | tex1(c));
    room(c, d, (DWORD) n, 3 * (DWORD) (n - 2));
    first = d->nv;
    for (i = 0; i < n; i++)
        cpu_vertex(&d->v[(d->nv++) * vd_of(d->control)], &v[i], v[i].front, 0.0f);
    /* A fan from its first vertex, which holds the provoking colour when
       flat (raster.c) */
    for (i = 1; i + 1 < n; i++) {
        d->ix[d->ni++] = (uint16_t) first;
        d->ix[d->ni++] = (uint16_t) (first + (DWORD) i);
        d->ix[d->ni++] = (uint16_t) (first + (DWORD) i + 1);
    }
}

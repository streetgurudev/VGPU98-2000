/*
 * The Glide core: drawing.
 *
 * Every primitive reaches the device as triangles, as a Voodoo3's Glide
 * builds them: a triangle culled here by its signed area (zero-area ones
 * draw nothing); a point as a small triangle covering exactly the pixel
 * its position falls in; a line as a one-pixel-wide parallelogram, its
 * ends moved half a pixel across its major axis. Points and lines are
 * never culled. Consecutive triangles with no state change between them
 * go out as one DRAW. x and y are taken as a Voodoo's setup unit takes
 * them: culled from the program's
 * own, then wrapped.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcore.h"

static float
floor_f(float v)
{
    const float t = (float) (int32_t) v;

    return (t > v) ? t - 1.0f : t;
}

static float
abs_f(float v)
{
    return (v < 0.0f) ? -v : v;
}

#define COORD_MAX 134217728.0f /* 2^27: from it on, no 32-bit 12.4 value */

/* x and y each a number inside +-2^27. The compares are quiet: a program
   that unmasked the x87's invalid-operation exception gets none here from
   a NaN. */
static int
xy_ok(const glc_vtx_t *v)
{
    const float x = v->f[VGPU98_GLV_X], y = v->f[VGPU98_GLV_Y];

    return __builtin_isgreater(x, -COORD_MAX) && __builtin_isless(x, COORD_MAX) && __builtin_isgreater(y, -COORD_MAX) &&
           __builtin_isless(y, COORD_MAX);
}

/* One coordinate (xy_ok's) as a Voodoo's setup unit takes it: 12.4 fixed
   point in 16 bits, so whole multiples of 4096 pixels fall away (a
   program's snap bias, 3 << 18 = 192 x 4096, among them), into -2048 up
   to, not including, 2048. Exact: the multiple taken away in double (at
   any x87 precision, for every input), the program's
   fraction kept whole. `k`: the multiple taken away. */
static void
voodoo_coord(float *c, int32_t *k)
{
    const double d = *c;
    double       q;

    *k = 0;
    if (__builtin_isgreaterequal(*c, -2048.0f) && __builtin_isless(*c, 2048.0f))
        return;
    q  = (d + 2048.0) / 4096.0;
    *k = (int32_t) q; /* toward zero: one less below zero, unless exact */
    if ((double) *k > q)
        (*k)--;
    *c = (float) (d - (double) *k * 4096.0);
}

static int wrap_told; /* the wrap's note said in this window */
static int bad_told;  /* the finding said in this window */

void
glc_draw_open(void)
{
    wrap_told = 0;
    bad_told  = 0;
}

/* A vertex's x and y as the setup unit takes them (`k` their multiples). */
static void
voodoo_xy(glc_vtx_t *v, int32_t *k)
{
    voodoo_coord(&v->f[VGPU98_GLV_X], &k[0]);
    voodoo_coord(&v->f[VGPU98_GLV_Y], &k[1]);
}

/* A vertex sent wrapped (`was` before, `now` after): counted, and the
   window's first noted (at Glide). */
static void
wrapped(const glc_vtx_t *was, const glc_vtx_t *now, const int32_t *k)
{
    if (!k[0] && !k[1])
        return;
    glc_count(GLC_WRAPPED);
    if (!wrap_told) {
        wrap_told = 1;
        glide_dbg("x and y wrapped as a Voodoo wraps them: the first x %d y %d as %d %d", (int) was->f[VGPU98_GLV_X],
                  (int) was->f[VGPU98_GLV_Y], (int) now->f[VGPU98_GLV_X], (int) now->f[VGPU98_GLV_Y]);
    }
}

/* A primitive dropped for its vertices: counted, and past a real
   problem's threshold (100 in the window, one in ten of those asked at
   least) a finding, once a window. */
static void
drop_bad(void)
{
    glc_count(GLC_DROPPED);
    if (!bad_told) {
        const DWORD n     = glc_window_counter(GLC_DROPPED);
        const DWORD asked = glc_window_counter(GLC_TRIANGLES_IN) + glc_window_counter(GLC_POINTS) + glc_window_counter(GLC_LINES);

        if ((n >= 100) && (n >= asked / 10)) {
            bad_told = 1;
            glide_event("possible bad vertices: %u of %u primitives dropped in this window (not a number, or past 2^27)", n,
                        asked);
        }
    }
}

/* Glide 2's delta0 mode: the flat colour (grConstantColorValue4's red,
   green and blue) the triangles' iterated colour starts at, unchanging. */
static int   delta0_on;
static float delta0_rgb[3];

void
glc_delta0(int on, const float *rgba)
{
    delta0_on = on;
    if (on && rgba) {
        delta0_rgb[0] = rgba[0];
        delta0_rgb[1] = rgba[1];
        delta0_rgb[2] = rgba[2];
    }
}

int
glc_delta0_on(void)
{
    return delta0_on;
}

void
glc_delta0_get(float *rgb)
{
    rgb[0] = delta0_rgb[0];
    rgb[1] = delta0_rgb[1];
    rgb[2] = delta0_rgb[2];
}

static void
emit(const glc_vtx_t *a, const glc_vtx_t *b, const glc_vtx_t *c)
{
    float *o = glc_vertices(3);
    int    i;

    if (!o)
        return;
    memcpy(o, a->f, sizeof(a->f));
    memcpy(o + VGPU98_GL_VERTEX_DWORDS, b->f, sizeof(b->f));
    memcpy(o + 2 * VGPU98_GL_VERTEX_DWORDS, c->f, sizeof(c->f));
    if (delta0_on) {
        for (i = 0; i < 3; i++) {
            o[i * VGPU98_GL_VERTEX_DWORDS + VGPU98_GLV_R] = delta0_rgb[0];
            o[i * VGPU98_GL_VERTEX_DWORDS + VGPU98_GLV_G] = delta0_rgb[1];
            o[i * VGPU98_GL_VERTEX_DWORDS + VGPU98_GLV_B] = delta0_rgb[2];
        }
    }
    if (glc_drawing_front())
        glc_front_written();
}

/* A triangle's corners as the setup unit takes them, sent; 1 if sent. Not
   when they wrapped by different multiples of 4096 (counted): near the
   wrap's edge the Voodoo's spans then run apart or its rows out of order
   and it draws nothing of them; farther out it draws a wrong shape
   (86Box's Voodoo model). The DLL draws nothing either
   way, so it never drops a triangle the Voodoo draws right. */
static int
send_wrapped(const glc_vtx_t *a0, const glc_vtx_t *b0, const glc_vtx_t *c0)
{
    glc_vtx_t va = *a0, vb = *b0, vc = *c0;
    int32_t   ka[2], kb[2], kc[2];

    voodoo_xy(&va, ka);
    voodoo_xy(&vb, kb);
    voodoo_xy(&vc, kc);
    if ((ka[0] != kb[0]) || (ka[0] != kc[0]) || (ka[1] != kb[1]) || (ka[1] != kc[1])) {
        glc_count(GLC_APART);
        return 0;
    }
    wrapped(a0, &va, ka);
    wrapped(b0, &vb, kb);
    wrapped(c0, &vc, kc);
    emit(&va, &vb, &vc);
    return 1;
}

void
glc_triangle(const glc_vtx_t *a, const glc_vtx_t *b, const glc_vtx_t *c)
{
    const DWORD cull = glc_reg_get(VGPU98_GS_CULL);
    float       area;

    glc_count(GLC_TRIANGLES_IN);
    if (!xy_ok(a) || !xy_ok(b) || !xy_ok(c)) {
        drop_bad();
        return;
    }
    /* The zero test and the cull from the program's own coordinates, as
       Glide and the setup unit take them (a bias's differences are exact). */
    area = (b->f[VGPU98_GLV_X] - a->f[VGPU98_GLV_X]) * (c->f[VGPU98_GLV_Y] - a->f[VGPU98_GLV_Y]) -
           (c->f[VGPU98_GLV_X] - a->f[VGPU98_GLV_X]) * (b->f[VGPU98_GLV_Y] - a->f[VGPU98_GLV_Y]);
    /* GR_CULL_NEGATIVE drops negative areas, GR_CULL_POSITIVE positive. */
    if ((!(area < 0.0f) && !(area > 0.0f)) || ((cull == 1) && (area < 0.0f)) || ((cull == 2) && (area > 0.0f))) {
        glc_count(GLC_CULLED);
        return;
    }
    if (send_wrapped(a, b, c))
        glc_count(GLC_TRIANGLES);
}

void
glc_point(const glc_vtx_t *p0)
{
    glc_vtx_t pv = *p0, v[3];
    int32_t   k[2];
    float     x, y;
    int       i;

    glc_count(GLC_POINTS);
    if (!xy_ok(p0)) {
        drop_bad();
        return;
    }
    /* Its position wrapped first; the pixel x and y fall in, at the
       Voodoo's 1/16 of a pixel; legs of 1.5 pixels from its corner: its
       centre alone is inside, and no other centre lies on an edge. */
    voodoo_xy(&pv, k);
    wrapped(p0, &pv, k);
    x = floor_f(floor_f(pv.f[VGPU98_GLV_X] * 16.0f + 0.5f) * (1.0f / 16.0f));
    y = floor_f(floor_f(pv.f[VGPU98_GLV_Y] * 16.0f + 0.5f) * (1.0f / 16.0f));
    for (i = 0; i < 3; i++)
        v[i] = pv;
    v[0].f[VGPU98_GLV_X] = x;
    v[0].f[VGPU98_GLV_Y] = y;
    v[1].f[VGPU98_GLV_X] = x + 1.5f;
    v[1].f[VGPU98_GLV_Y] = y;
    v[2].f[VGPU98_GLV_X] = x;
    v[2].f[VGPU98_GLV_Y] = y + 1.5f;
    emit(&v[0], &v[1], &v[2]);
}

void
glc_line(const glc_vtx_t *a, const glc_vtx_t *b)
{
    const glc_vtx_t *p = a, *q = b;
    glc_vtx_t        v[4];
    float            dx, dy;
    int              k;

    glc_count(GLC_LINES);
    if (!xy_ok(a) || !xy_ok(b)) {
        drop_bad();
        return;
    }
    /* From the program's own ends (the half pixel exact on a bias's 1/16
       grid): the end with the smaller y first. */
    if (b->f[VGPU98_GLV_Y] < a->f[VGPU98_GLV_Y]) {
        p = b;
        q = a;
    }
    dx = q->f[VGPU98_GLV_X] - p->f[VGPU98_GLV_X];
    dy = q->f[VGPU98_GLV_Y] - p->f[VGPU98_GLV_Y];
    if (!(abs_f(dx) > 0.0f) && !(abs_f(dy) > 0.0f))
        return;
    /* Half a pixel each way across the major axis; each triangle as the
       setup unit takes it. */
    k    = (abs_f(dx) >= abs_f(dy)) ? VGPU98_GLV_Y : VGPU98_GLV_X;
    v[0] = *p;
    v[1] = *q;
    v[2] = *q;
    v[3] = *p;
    v[0].f[k] -= 0.5f;
    v[1].f[k] -= 0.5f;
    v[2].f[k] += 0.5f;
    v[3].f[k] += 0.5f;
    send_wrapped(&v[0], &v[1], &v[2]);
    send_wrapped(&v[0], &v[2], &v[3]);
}

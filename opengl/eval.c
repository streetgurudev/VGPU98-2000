/*
 * Evaluators (GL 1.1, section 5.1): one- and two-dimensional maps of
 * Bernstein polynomials, glEvalCoord and glEvalPoint, the grids and
 * glEvalMesh, automatic normals, and glGetMap. An evaluated vertex carries
 * its evaluated colour, index, normal and texture coordinates (else the
 * current ones); the current values themselves do not change.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "glmath.h"
#include "lists.h"
#include "prim.h"

/* The targets' order: GL_MAP1_COLOR_4 (index 0) to GL_MAP1_VERTEX_4 (8),
   and likewise from GL_MAP2_COLOR_4. */
#define M_COLOR   0
#define M_INDEX   1
#define M_NORMAL  2
#define M_TEX1    3
#define M_TEX2    4
#define M_TEX3    5
#define M_TEX4    6
#define M_VERTEX3 7
#define M_VERTEX4 8

int
ogl_map_index(GLenum target, int *two)
{
    if ((target >= GL_MAP1_COLOR_4) && (target <= GL_MAP1_VERTEX_4)) {
        *two = 0;
        return (int) (target - GL_MAP1_COLOR_4);
    }
    if ((target >= GL_MAP2_COLOR_4) && (target <= GL_MAP2_VERTEX_4)) {
        *two = 1;
        return (int) (target - GL_MAP2_COLOR_4);
    }
    return -1;
}

GLint
ogl_map_components_of(GLenum target)
{
    int two;
    int i = ogl_map_index(target, &two);

    return (i < 0) ? 0 : ogl_map_components(i);
}

/* --- Bernstein polynomials ------------------------------------------------------------- */

/* A curve of `order` control points (k floats each, `stride` floats apart)
   at t: its point, and its derivative when d is given. de Casteljau's. */
static void
curve(const float *pts, int k, int stride, int order, float t, float *out, float *d)
{
    double work[OGL_EVAL_ORDER][4];
    int    i, j, c;

    for (i = 0; i < order; i++)
        for (c = 0; c < k; c++)
            work[i][c] = pts[i * stride + c];
    for (j = 1; j < order; j++) {
        /* The last level's two points give the derivative. */
        if ((j == order - 1) && d) {
            for (c = 0; c < k; c++)
                d[c] = (float) ((order - 1) * (work[1][c] - work[0][c]));
        }
        for (i = 0; i < order - j; i++)
            for (c = 0; c < k; c++)
                work[i][c] = work[i][c] * (1.0 - t) + work[i + 1][c] * t;
    }
    if (d && (order == 1)) {
        for (c = 0; c < k; c++)
            d[c] = 0.0f;
    }
    for (c = 0; c < k; c++)
        out[c] = (float) work[0][c];
}

/* A surface at (u, v): its point and partial derivatives (when du and dv
   are given). Each row along v first, then the column along u. */
static void
surface(const ogl_map_t *m, int k, float u, float v, float *out, float *du, float *dv)
{
    float rows[OGL_EVAL_ORDER][4];
    float rowd[OGL_EVAL_ORDER][4];
    int   i;

    for (i = 0; i < m->uorder; i++)
        curve(m->points + (size_t) i * (size_t) m->vorder * (size_t) k, k, k, m->vorder, v, rows[i], dv ? rowd[i] : NULL);
    curve(&rows[0][0], k, 4, m->uorder, u, out, du);
    if (dv)
        curve(&rowd[0][0], k, 4, m->uorder, u, dv, NULL);
}

/* --- Maps ---------------------------------------------------------------------------- */

static void
map1(ogl_ctx_t *c, GLenum target, float u1, float u2, GLint stride, GLint order, const float *points)
{
    int        two, i, k, j;
    ogl_map_t *m;
    float     *p;

    i = ogl_map_index(target, &two);
    if ((i < 0) || two) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    k = ogl_map_components(i);
    if ((u1 == u2) || (stride < k) || (order < 1) || (order > OGL_EVAL_ORDER)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c) || !points)
        return;
    p = ogl_alloc((DWORD) (order * k) * sizeof(float));
    if (!p) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    for (j = 0; j < order; j++)
        memcpy(p + j * k, points + (size_t) j * (size_t) stride, (size_t) k * sizeof(float));
    m = &c->gl->map1[i];
    ogl_free(m->points);
    m->points = p;
    m->uorder = order;
    m->vorder = 0;
    m->u1     = u1;
    m->u2     = u2;
}

void STDCALL
ogl_Map1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points)
{
    const GLint k = ogl_map_components_of(target);
    OGL_CTX();

    /* Compiled with its points as GL copies them (the stride gone). */
    if (ogl_dl_on(c)) {
        const int ok = k && (stride >= k) && (order >= 1) && (order <= OGL_EVAL_ORDER) && points;
        float    *p  = ok ? ogl_alloc((DWORD) (order * k) * sizeof(float)) : NULL;
        DWORD     w[4];
        int       j, only;

        for (j = 0; p && (j < order); j++)
            memcpy(p + j * k, points + (size_t) j * (size_t) stride, (size_t) k * sizeof(float));
        w[0] = target;
        w[1] = ogl_fw(u1);
        w[2] = ogl_fw(u2);
        w[3] = (DWORD) (p ? order : 0);
        only = ogl_dl_data(c, OP_MAP1, w, 4, p, p ? (DWORD) (order * k) * 4 : 0);
        ogl_free(p);
        if (only)
            return;
    }
    map1(c, target, u1, u2, stride, order, points);
}

void STDCALL
ogl_Map1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points)
{
    const GLint k = ogl_map_components_of(target);
    float      *p = NULL;
    GLint       j, i;

    if (points && k && (stride >= k) && (order >= 1) && (order <= OGL_EVAL_ORDER)) {
        p = ogl_alloc((DWORD) (order * k) * sizeof(float));
        for (j = 0; p && (j < order); j++)
            for (i = 0; i < k; i++)
                p[j * k + i] = (float) points[(size_t) j * (size_t) stride + (size_t) i];
    }
    ogl_Map1f(target, (float) u1, (float) u2, p ? k : stride, order, p);
    ogl_free(p);
}

static void
map2(ogl_ctx_t *c, GLenum target, float u1, float u2, GLint ustride, GLint uorder, float v1, float v2, GLint vstride,
     GLint vorder, const float *points)
{
    int        two, i, k, a, b;
    ogl_map_t *m;
    float     *p;

    i = ogl_map_index(target, &two);
    if ((i < 0) || !two) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    k = ogl_map_components(i);
    if ((u1 == u2) || (v1 == v2) || (ustride < k) || (vstride < k) || (uorder < 1) || (uorder > OGL_EVAL_ORDER) ||
        (vorder < 1) || (vorder > OGL_EVAL_ORDER)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c) || !points)
        return;
    p = ogl_alloc((DWORD) (uorder * vorder * k) * sizeof(float));
    if (!p) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    for (a = 0; a < uorder; a++)
        for (b = 0; b < vorder; b++)
            memcpy(p + (a * vorder + b) * k, points + (size_t) a * (size_t) ustride + (size_t) b * (size_t) vstride,
                   (size_t) k * sizeof(float));
    m = &c->gl->map2[i];
    ogl_free(m->points);
    m->points = p;
    m->uorder = uorder;
    m->vorder = vorder;
    m->u1     = u1;
    m->u2     = u2;
    m->v1     = v1;
    m->v2     = v2;
}

void STDCALL
ogl_Map2f(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder, GLfloat v1, GLfloat v2, GLint vstride,
          GLint vorder, const GLfloat *points)
{
    const GLint k = ogl_map_components_of(target);
    OGL_CTX();

    if (ogl_dl_on(c)) {
        const int ok = k && (ustride >= k) && (vstride >= k) && (uorder >= 1) && (uorder <= OGL_EVAL_ORDER) &&
                       (vorder >= 1) && (vorder <= OGL_EVAL_ORDER) && points;
        float    *p  = ok ? ogl_alloc((DWORD) (uorder * vorder * k) * sizeof(float)) : NULL;
        DWORD     w[7];
        int       a, b, only;

        for (a = 0; p && (a < uorder); a++)
            for (b = 0; b < vorder; b++)
                memcpy(p + (a * vorder + b) * k, points + (size_t) a * (size_t) ustride + (size_t) b * (size_t) vstride,
                       (size_t) k * sizeof(float));
        w[0] = target;
        w[1] = ogl_fw(u1);
        w[2] = ogl_fw(u2);
        w[3] = (DWORD) (p ? uorder : 0);
        w[4] = ogl_fw(v1);
        w[5] = ogl_fw(v2);
        w[6] = (DWORD) (p ? vorder : 0);
        only = ogl_dl_data(c, OP_MAP2, w, 7, p, p ? (DWORD) (uorder * vorder * k) * 4 : 0);
        ogl_free(p);
        if (only)
            return;
    }
    map2(c, target, u1, u2, ustride, uorder, v1, v2, vstride, vorder, points);
}

void STDCALL
ogl_Map2d(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder, GLdouble v1, GLdouble v2, GLint vstride,
          GLint vorder, const GLdouble *points)
{
    const GLint k = ogl_map_components_of(target);
    float      *p = NULL;
    GLint       a, b, i;

    if (points && k && (ustride >= k) && (vstride >= k) && (uorder >= 1) && (uorder <= OGL_EVAL_ORDER) && (vorder >= 1) &&
        (vorder <= OGL_EVAL_ORDER)) {
        p = ogl_alloc((DWORD) (uorder * vorder * k) * sizeof(float));
        for (a = 0; p && (a < uorder); a++)
            for (b = 0; b < vorder; b++)
                for (i = 0; i < k; i++)
                    p[(a * vorder + b) * k + i] = (float) points[(size_t) a * (size_t) ustride + (size_t) b * (size_t) vstride + (size_t) i];
    }
    if (p)
        ogl_Map2f(target, (float) u1, (float) u2, k * vorder, uorder, (float) v1, (float) v2, k, vorder, p);
    else
        ogl_Map2f(target, (float) u1, (float) u2, ustride, uorder, (float) v1, (float) v2, vstride, vorder, NULL);
    ogl_free(p);
}

/* --- Evaluating ------------------------------------------------------------------------------ */

/* The vertex of glEvalCoord1 at u, when a vertex map is enabled. */
static void
eval1(ogl_ctx_t *c, float u)
{
    const ogl_state_t *s = c->gl;
    const uint64_t     en = s->enables;
    float              obj[4], color[4], normal[3], tex[4], index;
    int                vi, ti;

    vi = (en & OGL_E_MAP1(M_VERTEX4)) ? M_VERTEX4 : (en & OGL_E_MAP1(M_VERTEX3)) ? M_VERTEX3 : -1;
    if (vi < 0)
        return;
    memcpy(color, s->current.color, sizeof(color));
    memcpy(normal, s->current.normal, sizeof(normal));
    memcpy(tex, s->current.texcoord[0], sizeof(tex));
    index = s->current.index;
#define AT(m) (((u) - (m)->u1) / ((m)->u2 - (m)->u1))
    obj[3] = 1.0f;
    curve(s->map1[vi].points, ogl_map_components(vi), ogl_map_components(vi), s->map1[vi].uorder, AT(&s->map1[vi]), obj, NULL);
    if (en & OGL_E_MAP1(M_INDEX))
        curve(s->map1[M_INDEX].points, 1, 1, s->map1[M_INDEX].uorder, AT(&s->map1[M_INDEX]), &index, NULL);
    if (en & OGL_E_MAP1(M_COLOR))
        curve(s->map1[M_COLOR].points, 4, 4, s->map1[M_COLOR].uorder, AT(&s->map1[M_COLOR]), color, NULL);
    if (en & OGL_E_MAP1(M_NORMAL))
        curve(s->map1[M_NORMAL].points, 3, 3, s->map1[M_NORMAL].uorder, AT(&s->map1[M_NORMAL]), normal, NULL);
    /* The highest-numbered texture coordinate map enabled. */
    for (ti = M_TEX4; ti >= M_TEX1; ti--) {
        if (en & OGL_E_MAP1(ti))
            break;
    }
    if (ti >= M_TEX1) {
        const int k = ogl_map_components(ti);

        tex[0] = 0.0f;
        tex[1] = 0.0f;
        tex[2] = 0.0f;
        tex[3] = 1.0f;
        curve(s->map1[ti].points, k, k, s->map1[ti].uorder, AT(&s->map1[ti]), tex, NULL);
    }
#undef AT
    ogl_prim_vertex_with(c, obj, color, normal, tex, index);
}

static void
eval2(ogl_ctx_t *c, float u, float v)
{
    const ogl_state_t *s = c->gl;
    const uint64_t     en = s->enables;
    float              obj[4], color[4], normal[3], tex[4], index;
    int                vi, ti;

    vi = (en & OGL_E_MAP2(M_VERTEX4)) ? M_VERTEX4 : (en & OGL_E_MAP2(M_VERTEX3)) ? M_VERTEX3 : -1;
    if (vi < 0)
        return;
    memcpy(color, s->current.color, sizeof(color));
    memcpy(normal, s->current.normal, sizeof(normal));
    memcpy(tex, s->current.texcoord[0], sizeof(tex));
    index = s->current.index;
#define AU(m) ((u - (m)->u1) / ((m)->u2 - (m)->u1))
#define AV(m) ((v - (m)->v1) / ((m)->v2 - (m)->v1))
    obj[3] = 1.0f;
    if (en & OGL_E_AUTO_NORMAL) {
        /* The normal from the partial derivatives: their cross product, of
           the point divided by w for a 4-component map. */
        float du[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, dv[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float pu[3], pv[3];
        int   k;

        surface(&s->map2[vi], ogl_map_components(vi), AU(&s->map2[vi]), AV(&s->map2[vi]), obj, du, dv);
        if (vi == M_VERTEX4) {
            const float w = (obj[3] != 0.0f) ? obj[3] : 1.0f;

            for (k = 0; k < 3; k++) {
                pu[k] = (du[k] * w - obj[k] * du[3]) / (w * w);
                pv[k] = (dv[k] * w - obj[k] * dv[3]) / (w * w);
            }
        } else {
            memcpy(pu, du, sizeof(pu));
            memcpy(pv, dv, sizeof(pv));
        }
        normal[0] = pu[1] * pv[2] - pu[2] * pv[1];
        normal[1] = pu[2] * pv[0] - pu[0] * pv[2];
        normal[2] = pu[0] * pv[1] - pu[1] * pv[0];
    } else {
        surface(&s->map2[vi], ogl_map_components(vi), AU(&s->map2[vi]), AV(&s->map2[vi]), obj, NULL, NULL);
        if (en & OGL_E_MAP2(M_NORMAL))
            surface(&s->map2[M_NORMAL], 3, AU(&s->map2[M_NORMAL]), AV(&s->map2[M_NORMAL]), normal, NULL, NULL);
    }
    if (en & OGL_E_MAP2(M_INDEX))
        surface(&s->map2[M_INDEX], 1, AU(&s->map2[M_INDEX]), AV(&s->map2[M_INDEX]), &index, NULL, NULL);
    if (en & OGL_E_MAP2(M_COLOR))
        surface(&s->map2[M_COLOR], 4, AU(&s->map2[M_COLOR]), AV(&s->map2[M_COLOR]), color, NULL, NULL);
    for (ti = M_TEX4; ti >= M_TEX1; ti--) {
        if (en & OGL_E_MAP2(ti))
            break;
    }
    if (ti >= M_TEX1) {
        tex[0] = 0.0f;
        tex[1] = 0.0f;
        tex[2] = 0.0f;
        tex[3] = 1.0f;
        surface(&s->map2[ti], ogl_map_components(ti), AU(&s->map2[ti]), AV(&s->map2[ti]), tex, NULL, NULL);
    }
#undef AU
#undef AV
    ogl_prim_vertex_with(c, obj, color, normal, tex, index);
}

void STDCALL
ogl_EvalCoord1f(GLfloat u)
{
    OGL_CTX();
    OGL_DL(OP_EVALCOORD1, ogl_fw(u));
    eval1(c, u);
}

void STDCALL ogl_EvalCoord1d(GLdouble u) { ogl_EvalCoord1f((float) u); }
void STDCALL ogl_EvalCoord1dv(const GLdouble *u) { ogl_EvalCoord1f((float) u[0]); }
void STDCALL ogl_EvalCoord1fv(const GLfloat *u) { ogl_EvalCoord1f(u[0]); }

void STDCALL
ogl_EvalCoord2f(GLfloat u, GLfloat v)
{
    OGL_CTX();
    OGL_DL(OP_EVALCOORD2, ogl_fw(u), ogl_fw(v));
    eval2(c, u, v);
}

void STDCALL ogl_EvalCoord2d(GLdouble u, GLdouble v) { ogl_EvalCoord2f((float) u, (float) v); }
void STDCALL ogl_EvalCoord2dv(const GLdouble *u) { ogl_EvalCoord2f((float) u[0], (float) u[1]); }
void STDCALL ogl_EvalCoord2fv(const GLfloat *u) { ogl_EvalCoord2f(u[0], u[1]); }

/* --- Grids and meshes ---------------------------------------------------------------------- */

void STDCALL
ogl_MapGrid1f(GLint un, GLfloat u1, GLfloat u2)
{
    OGL_CTX();
    OGL_DL(OP_MAPGRID1, (DWORD) un, ogl_fw(u1), ogl_fw(u2));
    if (un < 1) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->eval.grid1_n  = un;
    c->gl->eval.grid1_u1 = u1;
    c->gl->eval.grid1_u2 = u2;
}

void STDCALL
ogl_MapGrid1d(GLint un, GLdouble u1, GLdouble u2)
{
    ogl_MapGrid1f(un, (float) u1, (float) u2);
}

void STDCALL
ogl_MapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2)
{
    ogl_eval_attr_t *e;
    OGL_CTX();

    OGL_DL(OP_MAPGRID2, (DWORD) un, ogl_fw(u1), ogl_fw(u2), (DWORD) vn, ogl_fw(v1), ogl_fw(v2));
    if ((un < 1) || (vn < 1)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c))
        return;
    e           = &c->gl->eval;
    e->grid2_un = un;
    e->grid2_u1 = u1;
    e->grid2_u2 = u2;
    e->grid2_vn = vn;
    e->grid2_v1 = v1;
    e->grid2_v2 = v2;
}

void STDCALL
ogl_MapGrid2d(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2)
{
    ogl_MapGrid2f(un, (float) u1, (float) u2, vn, (float) v1, (float) v2);
}

/* A grid's coordinate i of n between a and b: exactly b at n. */
static float
grid(GLint i, GLint n, float a, float b)
{
    return (i == n) ? b : a + (float) i * (b - a) / (float) n;
}

void STDCALL
ogl_EvalPoint1(GLint i)
{
    OGL_CTX();
    OGL_DL(OP_EVALPOINT1, (DWORD) i);
    eval1(c, grid(i, c->gl->eval.grid1_n, c->gl->eval.grid1_u1, c->gl->eval.grid1_u2));
}

void STDCALL
ogl_EvalPoint2(GLint i, GLint j)
{
    const ogl_eval_attr_t *e;
    OGL_CTX();

    OGL_DL(OP_EVALPOINT2, (DWORD) i, (DWORD) j);
    e = &c->gl->eval;
    eval2(c, grid(i, e->grid2_un, e->grid2_u1, e->grid2_u2), grid(j, e->grid2_vn, e->grid2_v1, e->grid2_v2));
}

void STDCALL
ogl_EvalMesh1(GLenum mode, GLint i1, GLint i2)
{
    const ogl_eval_attr_t *e;
    GLint                  i;
    OGL_CTX();

    OGL_DL(OP_EVALMESH1, mode, (DWORD) i1, (DWORD) i2);
    if ((mode != GL_POINT) && (mode != GL_LINE)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    e = &c->gl->eval;
    ogl_prim_begin(c, (mode == GL_POINT) ? GL_POINTS : GL_LINE_STRIP);
    for (i = i1; i <= i2; i++)
        eval1(c, grid(i, e->grid1_n, e->grid1_u1, e->grid1_u2));
    ogl_prim_end(c);
}

void STDCALL
ogl_EvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2)
{
    const ogl_eval_attr_t *e;
    GLint                  i, j;
    OGL_CTX();

    OGL_DL(OP_EVALMESH2, mode, (DWORD) i1, (DWORD) i2, (DWORD) j1, (DWORD) j2);
    if ((mode != GL_POINT) && (mode != GL_LINE) && (mode != GL_FILL)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    e = &c->gl->eval;
#define U(i) grid((i), e->grid2_un, e->grid2_u1, e->grid2_u2)
#define V(j) grid((j), e->grid2_vn, e->grid2_v1, e->grid2_v2)
    if (mode == GL_POINT) {
        ogl_prim_begin(c, GL_POINTS);
        for (j = j1; j <= j2; j++)
            for (i = i1; i <= i2; i++)
                eval2(c, U(i), V(j));
        ogl_prim_end(c);
    } else if (mode == GL_LINE) {
        for (j = j1; j <= j2; j++) {
            ogl_prim_begin(c, GL_LINE_STRIP);
            for (i = i1; i <= i2; i++)
                eval2(c, U(i), V(j));
            ogl_prim_end(c);
        }
        for (i = i1; i <= i2; i++) {
            ogl_prim_begin(c, GL_LINE_STRIP);
            for (j = j1; j <= j2; j++)
                eval2(c, U(i), V(j));
            ogl_prim_end(c);
        }
    } else {
        for (j = j1; j < j2; j++) {
            ogl_prim_begin(c, GL_QUAD_STRIP);
            for (i = i1; i <= i2; i++) {
                eval2(c, U(i), V(j));
                eval2(c, U(i), V(j + 1));
            }
            ogl_prim_end(c);
        }
    }
#undef U
#undef V
}

/* --- glGetMap -------------------------------------------------------------------------------- */

/* The map's values for a query, as floats; their count (0 after an error). */
static int
get_map(ogl_ctx_t *c, GLenum target, GLenum query, float *out, int room)
{
    const ogl_map_t *m;
    int              two, i, k, n;

    i = ogl_map_index(target, &two);
    if ((i < 0) || ((query != GL_COEFF) && (query != GL_ORDER) && (query != GL_DOMAIN))) {
        ogl_error(c, GL_INVALID_ENUM);
        return 0;
    }
    if (ogl_inside(c))
        return 0;
    m = two ? &c->gl->map2[i] : &c->gl->map1[i];
    k = ogl_map_components(i);
    switch (query) {
        case GL_ORDER:
            out[0] = (float) m->uorder;
            out[1] = (float) m->vorder;
            return two ? 2 : 1;
        case GL_DOMAIN:
            out[0] = m->u1;
            out[1] = m->u2;
            out[2] = m->v1;
            out[3] = m->v2;
            return two ? 4 : 2;
        default:
            n = (two ? m->uorder * m->vorder : m->uorder) * k;
            if (n > room)
                n = room;
            if (!two) {
                memcpy(out, m->points, (size_t) n * sizeof(float));
                return n;
            } else {
                /* GL gives a surface's points with the u index running
                   fastest; they are kept with v fastest. */
                int a, b;

                for (b = 0; b < m->vorder; b++)
                    for (a = 0; a < m->uorder; a++)
                        memcpy(out + (b * m->uorder + a) * k, m->points + (a * m->vorder + b) * k, (size_t) k * sizeof(float));
                return n;
            }
    }
}

#define MAP_MAX (OGL_EVAL_ORDER * OGL_EVAL_ORDER * 4)

void STDCALL
ogl_GetMapfv(GLenum target, GLenum query, GLfloat *v)
{
    float       *buf;
    int          n;
    OGL_CTX();

    if (!c->gl->map_buf)
        c->gl->map_buf = ogl_alloc(MAP_MAX * sizeof(float));
    buf = c->gl->map_buf;
    if (!buf) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    n = get_map(c, target, query, buf, MAP_MAX);
    if (v && n)
        memcpy(v, buf, (size_t) n * sizeof(float));
}

void STDCALL
ogl_GetMapdv(GLenum target, GLenum query, GLdouble *v)
{
    float       *buf;
    int          n, i;
    OGL_CTX();

    if (!c->gl->map_buf)
        c->gl->map_buf = ogl_alloc(MAP_MAX * sizeof(float));
    buf = c->gl->map_buf;
    if (!buf) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    n = get_map(c, target, query, buf, MAP_MAX);
    for (i = 0; v && (i < n); i++)
        v[i] = buf[i];
}

void STDCALL
ogl_GetMapiv(GLenum target, GLenum query, GLint *v)
{
    float       *buf;
    int          n, i;
    OGL_CTX();

    if (!c->gl->map_buf)
        c->gl->map_buf = ogl_alloc(MAP_MAX * sizeof(float));
    buf = c->gl->map_buf;
    if (!buf) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    n = get_map(c, target, query, buf, MAP_MAX);
    for (i = 0; v && (i < n); i++)
        v[i] = ogl_round(buf[i]);
}

/*
 * The matrix stacks and their calls (GL 1.1, section 2.10.2): the
 * modelview, projection and texture stacks (a texture stack each unit, the
 * active unit's addressed), each call on the stack glMatrixMode chose. The
 * matrices are GL's column-major floats; GL's
 * double forms are taken to float, the precision the ICD keeps.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "glmath.h"
#include "lists.h"
#include "regs.h"

/* The current stack's top. */
static float *
top(ogl_state_t *s)
{
    switch (s->transform.matrix_mode) {
        case GL_PROJECTION:
            return s->projection[s->projection_depth - 1].m;
        case GL_TEXTURE:
            return s->texmatrix[s->texture.active][s->texture_depth[s->texture.active] - 1].m; /* the active unit's */
        default:
            return s->modelview[s->modelview_depth - 1].m;
    }
}

/* The top changed: its registers, and for the modelview the inverse
   transpose for normals. */
static void
changed(ogl_ctx_t *c)
{
    if (c->gl->transform.matrix_mode == GL_MODELVIEW)
        c->gl->normal_stale = 1;
    ogl_regs_matrices(c);
}

static void
mult(ogl_ctx_t *c, const float *m)
{
    float *t = top(c->gl);

    ogl_mat_mul(t, t, m);
    changed(c);
}

void STDCALL
ogl_MatrixMode(GLenum mode)
{
    OGL_CTX();
    OGL_DL(OP_MATRIXMODE, mode);
    if ((mode != GL_MODELVIEW) && (mode != GL_PROJECTION) && (mode != GL_TEXTURE)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->transform.matrix_mode = mode;
}

void STDCALL
ogl_LoadIdentity(void)
{
    OGL_CTX();
    OGL_DL0(OP_LOADIDENTITY);
    if (ogl_inside(c))
        return;
    ogl_mat_identity(top(c->gl));
    changed(c);
}

void STDCALL
ogl_LoadMatrixf(const GLfloat *m)
{
    OGL_CTX();
    if (ogl_dl_on(c) && m && ogl_dl(c, OP_LOADMATRIX, (const DWORD *) m, 16))
        return;
    if (ogl_inside(c) || !m)
        return;
    memcpy(top(c->gl), m, 16 * sizeof(float));
    changed(c);
}

void STDCALL
ogl_LoadMatrixd(const GLdouble *m)
{
    float f[16];
    int   i;

    if (!m)
        return;
    for (i = 0; i < 16; i++)
        f[i] = (float) m[i];
    ogl_LoadMatrixf(f);
}

void STDCALL
ogl_MultMatrixf(const GLfloat *m)
{
    OGL_CTX();
    if (ogl_dl_on(c) && m && ogl_dl(c, OP_MULTMATRIX, (const DWORD *) m, 16))
        return;
    if (ogl_inside(c) || !m)
        return;
    mult(c, m);
}

void STDCALL
ogl_MultMatrixd(const GLdouble *m)
{
    float f[16];
    int   i;

    if (!m)
        return;
    for (i = 0; i < 16; i++)
        f[i] = (float) m[i];
    ogl_MultMatrixf(f);
}

/* glRotate's matrix: a rotation by angle degrees about (x, y, z), the
   axis normalized (GL 1.1, section 2.10.2). A zero axis leaves the matrix
   as it is (the specification's rotation is undefined there; the
   identity). */
void STDCALL
ogl_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    float  m[16];
    double len, a, s, co, ic;
    OGL_CTX();

    OGL_DL(OP_ROTATE, ogl_fw(angle), ogl_fw(x), ogl_fw(y), ogl_fw(z));
    if (ogl_inside(c))
        return;
    len = ogl_sqrt((double) x * x + (double) y * y + (double) z * z);
    if (!(len > 0.0))
        return;
    x  = (float) (x / len);
    y  = (float) (y / len);
    z  = (float) (z / len);
    a  = (double) angle * 3.14159265358979323846 / 180.0;
    s  = ogl_sin(a);
    co = ogl_cos(a);
    ic = 1.0 - co;
    m[0]  = (float) (x * x * ic + co);
    m[1]  = (float) (y * x * ic + z * s);
    m[2]  = (float) (x * z * ic - y * s);
    m[3]  = 0.0f;
    m[4]  = (float) (x * y * ic - z * s);
    m[5]  = (float) (y * y * ic + co);
    m[6]  = (float) (y * z * ic + x * s);
    m[7]  = 0.0f;
    m[8]  = (float) (x * z * ic + y * s);
    m[9]  = (float) (y * z * ic - x * s);
    m[10] = (float) (z * z * ic + co);
    m[11] = 0.0f;
    m[12] = 0.0f;
    m[13] = 0.0f;
    m[14] = 0.0f;
    m[15] = 1.0f;
    mult(c, m);
}

void STDCALL
ogl_Rotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z)
{
    ogl_Rotatef((float) angle, (float) x, (float) y, (float) z);
}

void STDCALL
ogl_Scalef(GLfloat x, GLfloat y, GLfloat z)
{
    float *t;
    int    i;
    OGL_CTX();

    OGL_DL(OP_SCALE, ogl_fw(x), ogl_fw(y), ogl_fw(z));
    if (ogl_inside(c))
        return;
    t = top(c->gl);
    for (i = 0; i < 4; i++) {
        t[i] *= x;
        t[4 + i] *= y;
        t[8 + i] *= z;
    }
    changed(c);
}

void STDCALL
ogl_Scaled(GLdouble x, GLdouble y, GLdouble z)
{
    ogl_Scalef((float) x, (float) y, (float) z);
}

void STDCALL
ogl_Translatef(GLfloat x, GLfloat y, GLfloat z)
{
    float *t;
    int    i;
    OGL_CTX();

    OGL_DL(OP_TRANSLATE, ogl_fw(x), ogl_fw(y), ogl_fw(z));
    if (ogl_inside(c))
        return;
    t = top(c->gl);
    for (i = 0; i < 4; i++)
        t[12 + i] += t[i] * x + t[4 + i] * y + t[8 + i] * z;
    changed(c);
}

void STDCALL
ogl_Translated(GLdouble x, GLdouble y, GLdouble z)
{
    ogl_Translatef((float) x, (float) y, (float) z);
}

void STDCALL
ogl_Frustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    float m[16];
    OGL_CTX();

    if (ogl_dl_on(c)) {
        DWORD w[12];

        ogl_dw(&w[0], l);
        ogl_dw(&w[2], r);
        ogl_dw(&w[4], b);
        ogl_dw(&w[6], t);
        ogl_dw(&w[8], n);
        ogl_dw(&w[10], f);
        if (ogl_dl(c, OP_FRUSTUM, w, 12))
            return;
    }
    if ((n <= 0.0) || (f <= 0.0) || (l == r) || (b == t) || (n == f)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c))
        return;
    memset(m, 0, sizeof(m));
    m[0]  = (float) (2.0 * n / (r - l));
    m[5]  = (float) (2.0 * n / (t - b));
    m[8]  = (float) ((r + l) / (r - l));
    m[9]  = (float) ((t + b) / (t - b));
    m[10] = (float) (-(f + n) / (f - n));
    m[11] = -1.0f;
    m[14] = (float) (-2.0 * f * n / (f - n));
    mult(c, m);
}

void STDCALL
ogl_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
    float m[16];
    OGL_CTX();

    if (ogl_dl_on(c)) {
        DWORD w[12];

        ogl_dw(&w[0], l);
        ogl_dw(&w[2], r);
        ogl_dw(&w[4], b);
        ogl_dw(&w[6], t);
        ogl_dw(&w[8], n);
        ogl_dw(&w[10], f);
        if (ogl_dl(c, OP_ORTHO, w, 12))
            return;
    }
    if ((l == r) || (b == t) || (n == f)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c))
        return;
    memset(m, 0, sizeof(m));
    m[0]  = (float) (2.0 / (r - l));
    m[5]  = (float) (2.0 / (t - b));
    m[10] = (float) (-2.0 / (f - n));
    m[12] = (float) (-(r + l) / (r - l));
    m[13] = (float) (-(t + b) / (t - b));
    m[14] = (float) (-(f + n) / (f - n));
    m[15] = 1.0f;
    mult(c, m);
}

void STDCALL
ogl_PushMatrix(void)
{
    ogl_state_t *s;
    OGL_CTX();

    OGL_DL0(OP_PUSHMATRIX);
    if (ogl_inside(c))
        return;
    s = c->gl;
    switch (s->transform.matrix_mode) {
        case GL_PROJECTION:
            if (s->projection_depth >= OGL_PROJECTION_DEPTH) {
                ogl_error(c, GL_STACK_OVERFLOW);
                return;
            }
            s->projection[s->projection_depth] = s->projection[s->projection_depth - 1];
            s->projection_depth++;
            break;
        case GL_TEXTURE:
            if (s->texture_depth[s->texture.active] >= OGL_TEXTURE_DEPTH) {
                ogl_error(c, GL_STACK_OVERFLOW);
                return;
            }
            s->texmatrix[s->texture.active][s->texture_depth[s->texture.active]] =
                s->texmatrix[s->texture.active][s->texture_depth[s->texture.active] - 1];
            s->texture_depth[s->texture.active]++;
            break;
        default:
            if (s->modelview_depth >= OGL_MODELVIEW_DEPTH) {
                ogl_error(c, GL_STACK_OVERFLOW);
                return;
            }
            s->modelview[s->modelview_depth] = s->modelview[s->modelview_depth - 1];
            s->modelview_depth++;
            break;
    }
}

void STDCALL
ogl_PopMatrix(void)
{
    ogl_state_t *s;
    int         *depth;
    OGL_CTX();

    OGL_DL0(OP_POPMATRIX);
    if (ogl_inside(c))
        return;
    s     = c->gl;
    depth = (s->transform.matrix_mode == GL_PROJECTION) ? &s->projection_depth
            : (s->transform.matrix_mode == GL_TEXTURE)  ? &s->texture_depth[s->texture.active]
                                                        : &s->modelview_depth;
    if (*depth <= 1) {
        ogl_error(c, GL_STACK_UNDERFLOW);
        return;
    }
    (*depth)--;
    changed(c);
}

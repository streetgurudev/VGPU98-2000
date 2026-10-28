/*
 * Vertex arrays (GL 1.1, section 2.8): the client's array pointers,
 * glArrayElement, glDrawArrays, glDrawElements and glInterleavedArrays.
 * An element is read when the call is made, as GL defines it: its enabled
 * arrays' values through the current-value commands (edge flag, texture
 * coordinates, colour, index, normal), then the vertex. So a list compiling
 * the call keeps the values, as GL requires.
 *
 * EXT_compiled_vertex_array: the
 * vertex array, when enabled at glLockArraysEXT, is converted once for the
 * locked range and read from there until the unlock (made again when it is
 * pointed at again); the other arrays are read at each call, as programs
 * rewrite them under a lock (Quake 3's fog). glDrawArrays and glDrawElements go straight into the device's
 * DRAW where the primitive takes the device's path (GL_RENDER, no list
 * being compiled), each array element once a call.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "lists.h"
#include "prim.h"

/* An array as it was locked, and its elements converted (4 floats each,
   as array_vals gives them), made at the first read after the lock. */
typedef struct ogl_lock_arr {
    int           on; /* enabled at the lock */
    GLint         size;
    GLenum        type;
    GLsizei       stride;
    const GLvoid *ptr;
    float        *v;
    int           failed; /* no room to convert: read as it is */
} ogl_lock_arr_t;

struct ogl_lock {
    ogl_lock_arr_t a[OGL_ARRAYS];
};

/* The most elements a lock converts (16 bytes each), and a direct draw's
   marks (6 bytes each, draw.c): past these, the arrays are read as they
   are, and draws go through glBegin and glEnd. */
#define LOCK_MAX   0x100000
#define DIRECT_MAX 0x100000

static int
type_bytes(GLenum type)
{
    switch (type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            return 1;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
            return 2;
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FLOAT:
            return 4;
        case GL_DOUBLE:
            return 8;
        default:
            return 0;
    }
}

static void
set_array(ogl_ctx_t *c, int which, GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
    ogl_array_t *a;

    if (ogl_inside(c))
        return;
    a         = &c->gl->client.array[which];
    a->size   = size;
    a->type   = type;
    a->stride = stride;
    a->ptr    = ptr;
    /* Pointed again under a lock: its conversion is made again from what
       the array holds now (Quake 3 rewrites a buffer and points at it
       again, item 1). */
    if (c->gl->lock) {
        ogl_lock_arr_t *t = &c->gl->lock->a[which];

        ogl_free(t->v);
        t->v      = NULL;
        t->failed = 0;
        t->size   = size;
        t->type   = type;
        t->stride = stride;
        t->ptr    = ptr;
    }
}

void STDCALL
ogl_VertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
    OGL_CTX();
    if ((size < 2) || (size > 4) || (stride < 0)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if ((type != GL_SHORT) && (type != GL_INT) && (type != GL_FLOAT) && (type != GL_DOUBLE)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    set_array(c, OGL_ARRAY_VERTEX, size, type, stride, ptr);
}

void STDCALL
ogl_NormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr)
{
    OGL_CTX();
    if (stride < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if ((type != GL_BYTE) && (type != GL_SHORT) && (type != GL_INT) && (type != GL_FLOAT) && (type != GL_DOUBLE)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    set_array(c, OGL_ARRAY_NORMAL, 3, type, stride, ptr);
}

void STDCALL
ogl_ColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
    OGL_CTX();
    if ((size < 3) || (size > 4) || (stride < 0)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (!type_bytes(type)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    set_array(c, OGL_ARRAY_COLOR, size, type, stride, ptr);
}

void STDCALL
ogl_IndexPointer(GLenum type, GLsizei stride, const GLvoid *ptr)
{
    OGL_CTX();
    if (stride < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if ((type != GL_UNSIGNED_BYTE) && (type != GL_SHORT) && (type != GL_INT) && (type != GL_FLOAT) && (type != GL_DOUBLE)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    set_array(c, OGL_ARRAY_INDEX, 1, type, stride, ptr);
}

void STDCALL
ogl_TexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr)
{
    OGL_CTX();
    if ((size < 1) || (size > 4) || (stride < 0)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if ((type != GL_SHORT) && (type != GL_INT) && (type != GL_FLOAT) && (type != GL_DOUBLE)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    set_array(c, OGL_ARRAY_TEX(c->gl->client.client_active), size, type, stride, ptr); /* the client unit's */
}

void STDCALL
ogl_EdgeFlagPointer(GLsizei stride, const GLvoid *ptr)
{
    OGL_CTX();
    if (stride < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    set_array(c, OGL_ARRAY_EDGEFLAG, 1, GL_UNSIGNED_BYTE, stride, ptr);
}

/* Element i's component k of an array, as a float: plain (vertices,
   texture coordinates, indices), or normalized as GL converts colours and
   normals. */
static float
fetch(const ogl_array_t *a, GLint i, int k, int normalized)
{
    const int      tb     = type_bytes(a->type);
    const DWORD    stride = a->stride ? (DWORD) a->stride : (DWORD) (tb * a->size);
    const uint8_t *p      = (const uint8_t *) a->ptr + (size_t) i * stride + (size_t) k * tb;

    switch (a->type) {
        case GL_BYTE: {
            const GLbyte v = *(const GLbyte *) p;

            return normalized ? (float) ((2.0 * v + 1.0) / 255.0) : (float) v;
        }
        case GL_UNSIGNED_BYTE: {
            const GLubyte v = *p;

            return normalized ? (float) v / 255.0f : (float) v;
        }
        case GL_SHORT: {
            GLshort v;

            memcpy(&v, p, 2);
            return normalized ? (float) ((2.0 * v + 1.0) / 65535.0) : (float) v;
        }
        case GL_UNSIGNED_SHORT: {
            GLushort v;

            memcpy(&v, p, 2);
            return normalized ? (float) v / 65535.0f : (float) v;
        }
        case GL_INT: {
            GLint v;

            memcpy(&v, p, 4);
            return normalized ? (float) ((2.0 * (double) v + 1.0) / 4294967295.0) : (float) v;
        }
        case GL_UNSIGNED_INT: {
            GLuint v;

            memcpy(&v, p, 4);
            return normalized ? (float) ((double) v / 4294967295.0) : (float) v;
        }
        case GL_FLOAT: {
            GLfloat v;

            memcpy(&v, p, 4);
            return v;
        }
        default: {
            GLdouble v;

            memcpy(&v, p, 8);
            return (float) v;
        }
    }
}

/* Element i of an array as 4 floats, as GL takes each kind: a vertex's
   missing z and w 0 and 1, a texture coordinate's t, r and q 0, 0 and 1, a
   colour's alpha 1; colours and normals of integer types normalized; an
   index or an edge flag in the first. */
static void
array_vals(const ogl_array_t *a, int which, GLint i, float *out)
{
    int k;

    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    switch (which) {
        case OGL_ARRAY_EDGEFLAG: {
            const DWORD stride = a->stride ? (DWORD) a->stride : 1u;

            out[0] = ((const GLboolean *) a->ptr)[(size_t) i * stride] ? 1.0f : 0.0f;
            return;
        }
        case OGL_ARRAY_INDEX:
            out[0] = fetch(a, i, 0, 0);
            return;
        case OGL_ARRAY_NORMAL:
        case OGL_ARRAY_COLOR: {
            const int n = (a->type != GL_FLOAT) && (a->type != GL_DOUBLE);

            for (k = 0; k < ((which == OGL_ARRAY_NORMAL) ? 3 : a->size); k++)
                out[k] = fetch(a, i, k, n);
            return;
        }
        default: /* vertices, texture coordinates */
            for (k = 0; k < a->size; k++)
                out[k] = fetch(a, i, k, 0);
            return;
    }
}

/* The locked arrays: those enabled now taken as they are. */
static void
lock_take(ogl_ctx_t *c)
{
    struct ogl_lock *l = c->gl->lock;
    int              k;

    if (!l) {
        l = c->gl->lock = ogl_alloc(sizeof(struct ogl_lock));
        if (!l)
            return; /* the arrays read as they are */
    }
    for (k = 0; k < OGL_ARRAYS; k++) {
        const ogl_array_t *a = &c->gl->client.array[k];
        ogl_lock_arr_t    *t = &l->a[k];

        ogl_free(t->v);
        memset(t, 0, sizeof(*t));
        t->on     = (k == OGL_ARRAY_VERTEX) && a->enabled && a->ptr; /* positions alone */
        t->size   = a->size;
        t->type   = a->type;
        t->stride = a->stride;
        t->ptr    = a->ptr;
    }
}

void
ogl_arrays_unlock(ogl_ctx_t *c)
{
    struct ogl_lock *l = c->gl->lock;
    int              k;

    if (!l)
        return;
    for (k = 0; k < OGL_ARRAYS; k++)
        ogl_free(l->a[k].v);
    ogl_free(l);
    c->gl->lock = NULL;
}

void
ogl_arrays_lock_again(ogl_ctx_t *c)
{
    ogl_arrays_unlock(c);
    if (c->gl->client.lock_count)
        lock_take(c);
}

/* Element i of an array (array_vals'), from the lock's conversion when the
   array was enabled at the lock, is as it was then and i is in the locked
   range. */
static void
elem_vals(ogl_ctx_t *c, int which, GLint i, float *out)
{
    const ogl_array_t *a = &c->gl->client.array[which];
    struct ogl_lock   *l = c->gl->lock;
    const GLint        first = c->gl->client.lock_first;
    const GLsizei      count = c->gl->client.lock_count;

    if (l && count && (i >= first) && ((int64_t) i - first < count)) {
        ogl_lock_arr_t *t = &l->a[which];

        if (t->on && (t->ptr == a->ptr) && (t->size == a->size) && (t->type == a->type) && (t->stride == a->stride) &&
            !t->failed) {
            if (!t->v) {
                GLsizei e;

                t->v = ((count <= LOCK_MAX) && ((int64_t) first + count - 1 <= 0x7fffffff)) ? ogl_alloc((DWORD) count * 4 * sizeof(float)) : NULL;
                if (!t->v) {
                    t->failed = 1;
                    array_vals(a, which, i, out);
                    return;
                }
                for (e = 0; e < count; e++)
                    array_vals(a, which, first + e, &t->v[(DWORD) e * 4]);
            }
            memcpy(out, &t->v[(DWORD) (i - first) * 4], 4 * sizeof(float));
            return;
        }
    }
    array_vals(a, which, i, out);
}

static int
array_on(const ogl_ctx_t *c, int which)
{
    return c->gl->client.array[which].enabled && c->gl->client.array[which].ptr;
}

/* One element through the current-value commands, the vertex last. */
static void
element(ogl_ctx_t *c, GLint i)
{
    float v[4];
    int   u;

    if (array_on(c, OGL_ARRAY_EDGEFLAG)) {
        elem_vals(c, OGL_ARRAY_EDGEFLAG, i, v);
        ogl_EdgeFlag(v[0] != 0.0f);
    }
    for (u = 0; u < OGL_UNITS; u++) {
        if (array_on(c, OGL_ARRAY_TEX(u))) {
            elem_vals(c, OGL_ARRAY_TEX(u), i, v);
            ogl_multitexcoord(c, u, v[0], v[1], v[2], v[3]);
        }
    }
    if (array_on(c, OGL_ARRAY_COLOR)) {
        elem_vals(c, OGL_ARRAY_COLOR, i, v);
        ogl_Color4f(v[0], v[1], v[2], v[3]);
    }
    if (array_on(c, OGL_ARRAY_INDEX)) {
        elem_vals(c, OGL_ARRAY_INDEX, i, v);
        ogl_Indexf(v[0]);
    }
    if (array_on(c, OGL_ARRAY_NORMAL)) {
        elem_vals(c, OGL_ARRAY_NORMAL, i, v);
        ogl_Normal3f(v[0], v[1], v[2]);
    }
    if (array_on(c, OGL_ARRAY_VERTEX)) {
        elem_vals(c, OGL_ARRAY_VERTEX, i, v);
        ogl_Vertex4f(v[0], v[1], v[2], v[3]);
    }
}

void STDCALL
ogl_ArrayElement(GLint i)
{
    OGL_CTX();
    element(c, i);
}

/* --- Arrays drawn directly ----------------------------------------------------------- */

/* A direct draw's elements: first + i (glDrawArrays), or the i-th index. */
typedef struct arr_src {
    GLint         first;
    const GLvoid *indices; /* NULL for glDrawArrays */
    GLenum        type;
} arr_src_t;

static GLint
index_at(const GLvoid *indices, GLenum type, DWORD k)
{
    if (type == GL_UNSIGNED_BYTE)
        return ((const GLubyte *) indices)[k];
    if (type == GL_UNSIGNED_SHORT) {
        GLushort v;

        memcpy(&v, (const uint8_t *) indices + 2 * (size_t) k, 2);
        return v;
    }
    {
        GLuint v;

        memcpy(&v, (const uint8_t *) indices + 4 * (size_t) k, 4);
        return (GLint) v;
    }
}

static GLint
src_element(const ogl_vsrc_t *s, DWORD i)
{
    const arr_src_t *a = s->priv;

    return a->indices ? index_at(a->indices, a->type, i) : a->first + (GLint) i;
}

/* The mark of vertex i: its element (an element given twice is one vertex
   of the DRAW), counted from the call's first for glDrawArrays. */
static DWORD
src_mark(const ogl_vsrc_t *s, DWORD i)
{
    const arr_src_t *a = s->priv;

    return a->indices ? (DWORD) index_at(a->indices, a->type, i) : i;
}

/* Vertex i in DRAW's layout: each value from its array when enabled, else
   the current value. */
static void
src_vertex(const ogl_vsrc_t *s, DWORD i, float *out)
{
    ogl_ctx_t           *c   = s->c;
    const ogl_current_t *cur = &c->gl->current;
    const GLint          e   = src_element(s, i);
    int                  u;

    elem_vals(c, OGL_ARRAY_VERTEX, e, &out[VGPU98_OGLV_X]);
    if (array_on(c, OGL_ARRAY_COLOR))
        elem_vals(c, OGL_ARRAY_COLOR, e, &out[VGPU98_OGLV_R]);
    else
        memcpy(&out[VGPU98_OGLV_R], cur->color, 4 * sizeof(float));
    if (array_on(c, OGL_ARRAY_NORMAL)) {
        float n[4];

        elem_vals(c, OGL_ARRAY_NORMAL, e, n);
        memcpy(&out[VGPU98_OGLV_NX], n, 3 * sizeof(float));
    } else
        memcpy(&out[VGPU98_OGLV_NX], cur->normal, 3 * sizeof(float));
    out[VGPU98_OGLV_STIPPLE] = 0.0f;
    for (u = 0; u < OGL_UNITS; u++) {
        float *t = &out[u ? VGPU98_OGLV_S1 : VGPU98_OGLV_S];

        if (array_on(c, OGL_ARRAY_TEX(u)))
            elem_vals(c, OGL_ARRAY_TEX(u), e, t);
        else
            memcpy(t, cur->texcoord[u], 4 * sizeof(float));
    }
}

/* After a direct draw the current values are its last element's, as the
   calls through glBegin and glEnd leave them. */
static void
last_values(ogl_ctx_t *c, GLint e)
{
    ogl_current_t *cur = &c->gl->current;
    float          v[4];
    int            u;

    if (array_on(c, OGL_ARRAY_EDGEFLAG)) {
        elem_vals(c, OGL_ARRAY_EDGEFLAG, e, v);
        cur->edge_flag = (v[0] != 0.0f) ? GL_TRUE : GL_FALSE;
    }
    for (u = 0; u < OGL_UNITS; u++) {
        if (array_on(c, OGL_ARRAY_TEX(u)))
            elem_vals(c, OGL_ARRAY_TEX(u), e, cur->texcoord[u]);
    }
    if (array_on(c, OGL_ARRAY_COLOR)) {
        elem_vals(c, OGL_ARRAY_COLOR, e, cur->color);
        if (c->gl->enables & OGL_E_COLOR_MATERIAL)
            ogl_color_material(c);
    }
    if (array_on(c, OGL_ARRAY_INDEX)) {
        elem_vals(c, OGL_ARRAY_INDEX, e, v);
        cur->index = v[0];
    }
    if (array_on(c, OGL_ARRAY_NORMAL)) {
        elem_vals(c, OGL_ARRAY_NORMAL, e, v);
        memcpy(cur->normal, v, 3 * sizeof(float));
    }
}

/* May the call go straight into the DRAW? */
static int
direct_ok(ogl_ctx_t *c, GLenum mode)
{
    return !ogl_dl_on(c) && array_on(c, OGL_ARRAY_VERTEX) && ogl_draw_direct_ok(c->gl, mode);
}

/* 0 when it could not (no room for its marks): the caller goes through
   glBegin and glEnd instead. */
static int
direct(ogl_ctx_t *c, GLenum mode, DWORD n, DWORD marks, const arr_src_t *a)
{
    ogl_vsrc_t src;

    src.n      = n;
    src.marks  = marks;
    src.mark   = src_mark;
    src.vertex = src_vertex;
    src.c      = c;
    src.priv   = a;
    if (!ogl_draw_src(c, mode, &src))
        return 0;
    last_values(c, src_element(&src, n - 1));
    return 1;
}

void STDCALL
ogl_DrawArrays(GLenum mode, GLint first, GLsizei count)
{
    GLsizei k;
    OGL_CTX();

    if (count < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (mode > GL_POLYGON) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    if (count && (count <= DIRECT_MAX) && ((int64_t) first + count - 1 <= 0x7fffffff) && direct_ok(c, mode)) {
        arr_src_t a;

        a.first   = first;
        a.indices = NULL;
        a.type    = 0;
        if (direct(c, mode, (DWORD) count, (DWORD) count, &a))
            return;
    }
    ogl_Begin(mode);
    for (k = 0; k < count; k++)
        element(c, first + k);
    ogl_End();
}

void STDCALL
ogl_DrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    GLsizei k;
    OGL_CTX();

    if (count < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if ((mode > GL_POLYGON) || ((type != GL_UNSIGNED_BYTE) && (type != GL_UNSIGNED_SHORT) && (type != GL_UNSIGNED_INT))) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c) || !indices)
        return;
    if (count && (count <= DIRECT_MAX) && direct_ok(c, mode)) {
        DWORD     top = 0;
        arr_src_t a;

        /* The marks run to the largest element (each one vertex). */
        for (k = 0; k < count; k++) {
            const DWORD e = (DWORD) index_at(indices, type, (DWORD) k);

            if (e > top)
                top = e;
        }
        if (top < DIRECT_MAX) {
            a.first   = 0;
            a.indices = indices;
            a.type    = type;
            if (direct(c, mode, (DWORD) count, top + 1, &a))
                return;
        }
    }
    ogl_Begin(mode);
    for (k = 0; k < count; k++)
        element(c, index_at(indices, type, (DWORD) k));
    ogl_End();
}

/* --- EXT_compiled_vertex_array -------------------------------------------------------- */

void STDCALL
ogl_LockArraysEXT(GLint first, GLsizei count)
{
    OGL_CTX();
    if ((first < 0) || (count <= 0)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c))
        return;
    if (c->gl->client.lock_count) {
        ogl_error(c, GL_INVALID_OPERATION); /* locked already */
        return;
    }
    c->gl->client.lock_first = first;
    c->gl->client.lock_count = count;
    lock_take(c);
}

void STDCALL
ogl_UnlockArraysEXT(void)
{
    OGL_CTX();
    if (ogl_inside(c))
        return;
    if (!c->gl->client.lock_count) {
        ogl_error(c, GL_INVALID_OPERATION); /* not locked */
        return;
    }
    c->gl->client.lock_first = 0;
    c->gl->client.lock_count = 0;
    ogl_arrays_unlock(c);
}

/* glInterleavedArrays' formats (GL 1.1, Table 2.5): texture coordinates,
   colour and normal sizes and the colour's type, the vertex's size. */
static const struct {
    GLenum format;
    int    t, c, n, v;
    GLenum ctype;
} interleaved[] = {
    { GL_V2F, 0, 0, 0, 2, 0 },
    { GL_V3F, 0, 0, 0, 3, 0 },
    { GL_C4UB_V2F, 0, 4, 0, 2, GL_UNSIGNED_BYTE },
    { GL_C4UB_V3F, 0, 4, 0, 3, GL_UNSIGNED_BYTE },
    { GL_C3F_V3F, 0, 3, 0, 3, GL_FLOAT },
    { GL_N3F_V3F, 0, 0, 3, 3, 0 },
    { GL_C4F_N3F_V3F, 0, 4, 3, 3, GL_FLOAT },
    { GL_T2F_V3F, 2, 0, 0, 3, 0 },
    { GL_T4F_V4F, 4, 0, 0, 4, 0 },
    { GL_T2F_C4UB_V3F, 2, 4, 0, 3, GL_UNSIGNED_BYTE },
    { GL_T2F_C3F_V3F, 2, 3, 0, 3, GL_FLOAT },
    { GL_T2F_N3F_V3F, 2, 0, 3, 3, 0 },
    { GL_T2F_C4F_N3F_V3F, 2, 4, 3, 3, GL_FLOAT },
    { GL_T4F_C4F_N3F_V4F, 4, 4, 3, 4, GL_FLOAT },
};

void STDCALL
ogl_InterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer)
{
    ogl_array_t *a;
    unsigned     i;
    int          off = 0, size, tex;
    OGL_CTX();

    for (i = 0; i < ARRAY_SIZE(interleaved); i++) {
        if (interleaved[i].format == format)
            break;
    }
    if (i == ARRAY_SIZE(interleaved)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (stride < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c))
        return;
    /* Each part's place, and the whole's size (a C4UB colour is one dword). */
    size = 4 * (interleaved[i].t + interleaved[i].n + interleaved[i].v) +
           ((interleaved[i].ctype == GL_UNSIGNED_BYTE) ? 4 : 4 * interleaved[i].c);
    if (!stride)
        stride = size;
    a   = c->gl->client.array;
    tex = OGL_ARRAY_TEX(c->gl->client.client_active); /* the client unit's coordinates */
    a[OGL_ARRAY_EDGEFLAG].enabled = GL_FALSE;
    a[OGL_ARRAY_INDEX].enabled    = GL_FALSE;
    a[tex].enabled = interleaved[i].t ? GL_TRUE : GL_FALSE;
    if (interleaved[i].t) {
        a[tex].size   = interleaved[i].t;
        a[tex].type   = GL_FLOAT;
        a[tex].stride = stride;
        a[tex].ptr    = pointer;
        off += 4 * interleaved[i].t;
    }
    a[OGL_ARRAY_COLOR].enabled = interleaved[i].c ? GL_TRUE : GL_FALSE;
    if (interleaved[i].c) {
        a[OGL_ARRAY_COLOR].size   = interleaved[i].c;
        a[OGL_ARRAY_COLOR].type   = interleaved[i].ctype;
        a[OGL_ARRAY_COLOR].stride = stride;
        a[OGL_ARRAY_COLOR].ptr    = (const uint8_t *) pointer + off;
        off += (interleaved[i].ctype == GL_UNSIGNED_BYTE) ? 4 : 4 * interleaved[i].c;
    }
    a[OGL_ARRAY_NORMAL].enabled = interleaved[i].n ? GL_TRUE : GL_FALSE;
    if (interleaved[i].n) {
        a[OGL_ARRAY_NORMAL].type   = GL_FLOAT;
        a[OGL_ARRAY_NORMAL].stride = stride;
        a[OGL_ARRAY_NORMAL].ptr    = (const uint8_t *) pointer + off;
        off += 12;
    }
    a[OGL_ARRAY_VERTEX].enabled = GL_TRUE;
    a[OGL_ARRAY_VERTEX].size    = interleaved[i].v;
    a[OGL_ARRAY_VERTEX].type    = GL_FLOAT;
    a[OGL_ARRAY_VERTEX].stride  = stride;
    a[OGL_ARRAY_VERTEX].ptr     = (const uint8_t *) pointer + off;
    /* Under a lock: its conversion taken again for the arrays as they are now */
    if (c->gl->lock)
        ogl_arrays_lock_again(c);
}

void STDCALL
ogl_GetPointerv(GLenum pname, GLvoid **params)
{
    const ogl_array_t *a;
    OGL_CTX();

    if (!params)
        return;
    a = c->gl->client.array;
    switch (pname) {
        case GL_VERTEX_ARRAY_POINTER:
            *params = (GLvoid *) a[OGL_ARRAY_VERTEX].ptr;
            break;
        case GL_NORMAL_ARRAY_POINTER:
            *params = (GLvoid *) a[OGL_ARRAY_NORMAL].ptr;
            break;
        case GL_COLOR_ARRAY_POINTER:
            *params = (GLvoid *) a[OGL_ARRAY_COLOR].ptr;
            break;
        case GL_INDEX_ARRAY_POINTER:
            *params = (GLvoid *) a[OGL_ARRAY_INDEX].ptr;
            break;
        case GL_TEXTURE_COORD_ARRAY_POINTER:
            *params = (GLvoid *) a[OGL_ARRAY_TEX(c->gl->client.client_active)].ptr;
            break;
        case GL_EDGE_FLAG_ARRAY_POINTER:
            *params = (GLvoid *) a[OGL_ARRAY_EDGEFLAG].ptr;
            break;
        case GL_FEEDBACK_BUFFER_POINTER:
            *params = (GLvoid *) c->gl->feedback_buffer;
            break;
        case GL_SELECTION_BUFFER_POINTER:
            *params = (GLvoid *) c->gl->select_buffer;
            break;
        default:
            ogl_error(c, GL_INVALID_ENUM);
            break;
    }
}

/*
 * GL 1.1's remaining state calls: points, lines and polygons (section 3),
 * fog and hints, the per-fragment tests, the write masks and buffers
 * (section 4), clip planes, the texture environment and coordinate
 * generation, with their queries. Each checks GL's rules, keeps the state,
 * and turns it into the device's registers.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "glmath.h"
#include "lists.h"
#include "pixel.h"
#include "regs.h"

static float
int_color(GLint v)
{
    return (float) ((2.0 * (double) v + 1.0) / 4294967295.0);
}

static GLint
color_int(float f)
{
    const double v = ((double) f * 4294967295.0 - 1.0) / 2.0;

    if (v >= 2147483647.0)
        return 2147483647;
    if (v <= -2147483648.0)
        return (GLint) 0x80000000u;
    return (GLint) (v + ((v >= 0.0) ? 0.5 : -0.5));
}

/* --- Points, lines, polygons ------------------------------------------------------- */

void STDCALL
ogl_PointSize(GLfloat size)
{
    OGL_CTX();
    OGL_DL(OP_POINTSIZE, ogl_fw(size));
    if (!(size > 0.0f)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->raster.point_size = size;
    ogl_regs_raster(c);
}

/* --- EXT_point_parameters ------------------------------- */

float
ogl_point_size(const ogl_raster_t *r, float d, float *fade)
{
    const double den  = (double) r->point_atten[0] + (double) r->point_atten[1] * d + (double) r->point_atten[2] * d * d;
    float        size = r->point_size;

    /* size * sqrt(1 / (a + b d + c d^2)), clamped to the bounds; below the
       threshold, the threshold's size with alpha faded by (size / it)^2 */
    if (den > 0.0)
        size = (float) (size * ogl_sqrt(1.0 / den));
    if (size < r->point_min)
        size = r->point_min;
    if (size > r->point_max)
        size = r->point_max;
    *fade = 1.0f;
    if (size < r->point_fade) {
        const float f = size / r->point_fade;

        *fade = f * f;
        size  = r->point_fade;
    }
    return size;
}

static void
point_param(GLenum pname, const GLfloat *v, int scalar)
{
    ogl_raster_t *r;
    OGL_CTX();

    /* Compiled with which call it was: the scalar one refuses the
       attenuation when the list runs too */
    OGL_DL(OP_POINTPARAM, pname, ogl_fw(v[0]), ogl_fw((pname == GL_DISTANCE_ATTENUATION_EXT) ? v[1] : 0.0f),
           ogl_fw((pname == GL_DISTANCE_ATTENUATION_EXT) ? v[2] : 0.0f), (DWORD) scalar);
    if (((pname != GL_POINT_SIZE_MIN_EXT) && (pname != GL_POINT_SIZE_MAX_EXT) && (pname != GL_POINT_FADE_THRESHOLD_SIZE_EXT) &&
         (pname != GL_DISTANCE_ATTENUATION_EXT)) ||
        (scalar && (pname == GL_DISTANCE_ATTENUATION_EXT))) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if ((pname != GL_DISTANCE_ATTENUATION_EXT) && !(v[0] >= 0.0f)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c))
        return;
    r = &c->gl->raster;
    switch (pname) {
        case GL_POINT_SIZE_MIN_EXT:
            r->point_min = v[0];
            break;
        case GL_POINT_SIZE_MAX_EXT:
            r->point_max = v[0];
            break;
        case GL_POINT_FADE_THRESHOLD_SIZE_EXT:
            r->point_fade = v[0];
            break;
        default:
            memcpy(r->point_atten, v, sizeof(r->point_atten));
            break;
    }
    ogl_regs_raster(c);
}

void STDCALL
ogl_PointParameterfEXT(GLenum pname, GLfloat param)
{
    const GLfloat v[3] = { param, 0.0f, 0.0f };

    point_param(pname, v, 1);
}

void STDCALL
ogl_PointParameterfvEXT(GLenum pname, const GLfloat *params)
{
    GLfloat v[3] = { 0.0f, 0.0f, 0.0f };

    if (params)
        memcpy(v, params, ((pname == GL_DISTANCE_ATTENUATION_EXT) ? 3 : 1) * sizeof(GLfloat));
    point_param(pname, v, 0);
}

void STDCALL
ogl_LineWidth(GLfloat width)
{
    OGL_CTX();
    OGL_DL(OP_LINEWIDTH, ogl_fw(width));
    if (!(width > 0.0f)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->raster.line_width = width;
    ogl_regs_raster(c);
}

void STDCALL
ogl_LineStipple(GLint factor, GLushort pattern)
{
    OGL_CTX();
    OGL_DL(OP_LINESTIPPLE, (DWORD) factor, pattern);
    if (ogl_inside(c))
        return;
    /* The factor is clamped to 1-256. */
    c->gl->raster.line_repeat  = (factor < 1) ? 1 : (factor > 256) ? 256 : factor;
    c->gl->raster.line_pattern = pattern;
    ogl_regs_raster(c);
}

void STDCALL
ogl_CullFace(GLenum mode)
{
    OGL_CTX();
    OGL_DL(OP_CULLFACE, mode);
    if ((mode != GL_FRONT) && (mode != GL_BACK) && (mode != GL_FRONT_AND_BACK)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->raster.cull_mode = mode;
    ogl_regs_raster(c);
}

void STDCALL
ogl_FrontFace(GLenum mode)
{
    OGL_CTX();
    OGL_DL(OP_FRONTFACE, mode);
    if ((mode != GL_CW) && (mode != GL_CCW)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->raster.front_face = mode;
    ogl_regs_raster(c);
}

void STDCALL
ogl_PolygonMode(GLenum face, GLenum mode)
{
    ogl_raster_t *r;
    OGL_CTX();

    OGL_DL(OP_POLYGONMODE, face, mode);
    if (((face != GL_FRONT) && (face != GL_BACK) && (face != GL_FRONT_AND_BACK)) ||
        ((mode != GL_POINT) && (mode != GL_LINE) && (mode != GL_FILL))) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    r = &c->gl->raster;
    if (face != GL_BACK)
        r->polygon_mode[0] = mode;
    if (face != GL_FRONT)
        r->polygon_mode[1] = mode;
}

void STDCALL
ogl_PolygonOffset(GLfloat factor, GLfloat units)
{
    OGL_CTX();
    OGL_DL(OP_POLYGONOFFSET, ogl_fw(factor), ogl_fw(units));
    if (ogl_inside(c))
        return;
    c->gl->raster.offset_factor = factor;
    c->gl->raster.offset_units  = units;
    ogl_regs_raster(c);
}

void
ogl_stipple_apply(ogl_ctx_t *c, const DWORD *rows)
{
    if (ogl_inside(c))
        return;
    memcpy(c->gl->stipple, rows, sizeof(c->gl->stipple));
    ogl_regs_raster(c);
}

/* The pattern is pixel data: unpacked as a 32x32 GL_COLOR_INDEX image of
   type GL_BITMAP by the unpack state, when the command is (compiled or)
   run. A row's bit 31 is its leftmost pixel. */
void STDCALL
ogl_PolygonStipple(const GLubyte *mask)
{
    uint8_t bits[32 * 32];
    DWORD   rows[32];
    int     y, x;
    OGL_CTX();

    if (!mask)
        return;
    ogl_unpack_bits(&c->gl->client.unpack, 32, 32, mask, bits);
    for (y = 0; y < 32; y++) {
        rows[y] = 0;
        for (x = 0; x < 32; x++) {
            if (bits[y * 32 + x])
                rows[y] |= 0x80000000u >> x;
        }
    }
    if (ogl_dl_on(c) && ogl_dl(c, OP_POLYGONSTIPPLE, rows, 32))
        return;
    ogl_stipple_apply(c, rows);
}

/* Packed by the pack state as glReadPixels packs a GL_COLOR_INDEX image of
   type GL_BITMAP. */
void STDCALL
ogl_GetPolygonStipple(GLubyte *mask)
{
    const ogl_store_t *st;
    DWORD              row_bytes, y, x;
    OGL_CTX();

    if (ogl_inside(c) || !mask)
        return;
    st        = &c->gl->client.pack;
    row_bytes = ogl_row_bytes(st, GL_COLOR_INDEX, GL_BITMAP, 32);
    for (y = 0; y < 32; y++) {
        uint8_t *row = mask + (st->skip_rows + y) * row_bytes;

        for (x = 0; x < 32; x++) {
            const DWORD bit  = (DWORD) st->skip_pixels + x;
            uint8_t    *b    = row + bit / 8;
            const int   sh   = st->lsb_first ? (int) (bit % 8) : 7 - (int) (bit % 8);
            const int   on   = (c->gl->stipple[y] >> (31 - x)) & 1;

            *b = (uint8_t) ((*b & ~(1u << sh)) | ((unsigned) on << sh));
        }
    }
}

/* --- Fog --------------------------------------------------------------------------------- */

void STDCALL
ogl_Fogfv(GLenum pname, const GLfloat *params)
{
    ogl_fog_t *f;
    float      v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int        i;
    OGL_CTX();

    if (params)
        memcpy(v, params, ((pname == GL_FOG_COLOR) ? 4 : 1) * sizeof(float));
    OGL_DL(OP_FOG, pname, ogl_fw(v[0]), ogl_fw(v[1]), ogl_fw(v[2]), ogl_fw(v[3]));
    if (ogl_inside(c))
        return;
    f = &c->gl->fog;
    switch (pname) {
        case GL_FOG_MODE: {
            const GLenum m = (GLenum) v[0];

            if ((m != GL_LINEAR) && (m != GL_EXP) && (m != GL_EXP2)) {
                ogl_error(c, GL_INVALID_ENUM);
                return;
            }
            f->mode = m;
            break;
        }
        case GL_FOG_DENSITY:
            if (v[0] < 0.0f) {
                ogl_error(c, GL_INVALID_VALUE);
                return;
            }
            f->density = v[0];
            break;
        case GL_FOG_START:
            f->start = v[0];
            break;
        case GL_FOG_END:
            f->end = v[0];
            break;
        case GL_FOG_INDEX:
            f->index = v[0];
            break;
        case GL_FOG_COLOR:
            for (i = 0; i < 4; i++)
                f->color[i] = ogl_clampf(v[i], 0.0f, 1.0f);
            break;
        default:
            ogl_error(c, GL_INVALID_ENUM);
            return;
    }
    ogl_regs_fog(c);
}

void STDCALL
ogl_Fogf(GLenum pname, GLfloat param)
{
    OGL_CTX();
    if (pname == GL_FOG_COLOR) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    ogl_Fogfv(pname, &param);
}

void STDCALL
ogl_Fogi(GLenum pname, GLint param)
{
    ogl_Fogf(pname, (GLfloat) param);
}

void STDCALL
ogl_Fogiv(GLenum pname, const GLint *params)
{
    float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int   i;

    if (params) {
        if (pname == GL_FOG_COLOR) {
            for (i = 0; i < 4; i++)
                v[i] = int_color(params[i]);
        } else
            v[0] = (float) params[0];
    }
    ogl_Fogfv(pname, v);
}

void STDCALL
ogl_Hint(GLenum target, GLenum mode)
{
    ogl_hints_t *h;
    OGL_CTX();

    OGL_DL(OP_HINT, target, mode);
    if ((mode != GL_FASTEST) && (mode != GL_NICEST) && (mode != GL_DONT_CARE)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    h = &c->gl->hints;
    switch (target) {
        case GL_PERSPECTIVE_CORRECTION_HINT:
            h->perspective = mode;
            break;
        case GL_POINT_SMOOTH_HINT:
            h->point_smooth = mode;
            break;
        case GL_LINE_SMOOTH_HINT:
            h->line_smooth = mode;
            break;
        case GL_POLYGON_SMOOTH_HINT:
            h->polygon_smooth = mode;
            break;
        case GL_FOG_HINT:
            h->fog = mode;
            break;
        default:
            ogl_error(c, GL_INVALID_ENUM);
            break;
    }
}

/* --- Per-fragment operations ------------------------------------------------------------- */

static int
is_func(GLenum f)
{
    return (f >= GL_NEVER) && (f <= GL_ALWAYS);
}

void STDCALL
ogl_AlphaFunc(GLenum func, GLclampf ref)
{
    OGL_CTX();
    OGL_DL(OP_ALPHAFUNC, func, ogl_fw(ref));
    if (!is_func(func)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->color_buffer.alpha_func = func;
    c->gl->color_buffer.alpha_ref  = ogl_clampf(ref, 0.0f, 1.0f);
    ogl_regs_fragment(c);
}

static int
is_src_factor(GLenum f)
{
    switch (f) {
        case GL_ZERO:
        case GL_ONE:
        case GL_DST_COLOR:
        case GL_ONE_MINUS_DST_COLOR:
        case GL_SRC_ALPHA:
        case GL_ONE_MINUS_SRC_ALPHA:
        case GL_DST_ALPHA:
        case GL_ONE_MINUS_DST_ALPHA:
        case GL_SRC_ALPHA_SATURATE:
            return 1;
        default:
            return 0;
    }
}

static int
is_dst_factor(GLenum f)
{
    switch (f) {
        case GL_ZERO:
        case GL_ONE:
        case GL_SRC_COLOR:
        case GL_ONE_MINUS_SRC_COLOR:
        case GL_SRC_ALPHA:
        case GL_ONE_MINUS_SRC_ALPHA:
        case GL_DST_ALPHA:
        case GL_ONE_MINUS_DST_ALPHA:
            return 1;
        default:
            return 0;
    }
}

void STDCALL
ogl_BlendFunc(GLenum sfactor, GLenum dfactor)
{
    OGL_CTX();
    OGL_DL(OP_BLENDFUNC, sfactor, dfactor);
    if (!is_src_factor(sfactor) || !is_dst_factor(dfactor)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->color_buffer.blend_src = sfactor;
    c->gl->color_buffer.blend_dst = dfactor;
    ogl_regs_fragment(c);
}

void STDCALL
ogl_LogicOp(GLenum opcode)
{
    OGL_CTX();
    OGL_DL(OP_LOGICOP, opcode);
    if ((opcode < GL_CLEAR) || (opcode > GL_SET)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->color_buffer.logic_op = opcode;
    ogl_regs_fragment(c);
}

void STDCALL
ogl_DepthFunc(GLenum func)
{
    OGL_CTX();
    OGL_DL(OP_DEPTHFUNC, func);
    if (!is_func(func)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->buffers.depth_func = func;
    ogl_regs_fragment(c);
}

void STDCALL
ogl_DepthMask(GLboolean flag)
{
    OGL_CTX();
    OGL_DL(OP_DEPTHMASK, flag);
    if (ogl_inside(c))
        return;
    c->gl->buffers.depth_mask = flag ? GL_TRUE : GL_FALSE;
    ogl_regs_fragment(c);
}

void STDCALL
ogl_StencilFunc(GLenum func, GLint ref, GLuint mask)
{
    OGL_CTX();
    OGL_DL(OP_STENCILFUNC, func, (DWORD) ref, mask);
    if (!is_func(func)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->buffers.stencil_func       = func;
    c->gl->buffers.stencil_ref        = ref;
    c->gl->buffers.stencil_value_mask = mask;
    ogl_regs_fragment(c);
}

static int
is_stencil_op(GLenum op)
{
    return (op == GL_KEEP) || (op == GL_ZERO) || (op == GL_REPLACE) || (op == GL_INCR) || (op == GL_DECR) ||
           (op == GL_INVERT);
}

void STDCALL
ogl_StencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
    OGL_CTX();
    OGL_DL(OP_STENCILOP, fail, zfail, zpass);
    if (!is_stencil_op(fail) || !is_stencil_op(zfail) || !is_stencil_op(zpass)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->buffers.stencil_fail  = fail;
    c->gl->buffers.stencil_zfail = zfail;
    c->gl->buffers.stencil_zpass = zpass;
    ogl_regs_fragment(c);
}

void STDCALL
ogl_StencilMask(GLuint mask)
{
    OGL_CTX();
    OGL_DL(OP_STENCILMASK, mask);
    if (ogl_inside(c))
        return;
    c->gl->buffers.stencil_write_mask = mask;
    ogl_regs_fragment(c);
}

void STDCALL
ogl_ColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a)
{
    GLboolean *m;
    OGL_CTX();

    OGL_DL(OP_COLORMASK, r, g, b, a);
    if (ogl_inside(c))
        return;
    m    = c->gl->color_buffer.color_mask;
    m[0] = r ? GL_TRUE : GL_FALSE;
    m[1] = g ? GL_TRUE : GL_FALSE;
    m[2] = b ? GL_TRUE : GL_FALSE;
    m[3] = a ? GL_TRUE : GL_FALSE;
    ogl_regs_fragment(c);
}

void STDCALL
ogl_IndexMask(GLuint mask)
{
    OGL_CTX();
    OGL_DL(OP_INDEXMASK, mask);
    if (ogl_inside(c))
        return;
    c->gl->color_buffer.index_mask = mask;
}

void STDCALL
ogl_ClearIndex(GLfloat i)
{
    OGL_CTX();
    OGL_DL(OP_CLEARINDEX, ogl_fw(i));
    if (ogl_inside(c))
        return;
    c->gl->color_buffer.clear_index = i;
}

void STDCALL
ogl_ClearAccum(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    float *v;
    OGL_CTX();

    OGL_DL(OP_CLEARACCUM, ogl_fw(r), ogl_fw(g), ogl_fw(b), ogl_fw(a));
    if (ogl_inside(c))
        return;
    v    = c->gl->buffers.accum_clear;
    v[0] = ogl_clampf(r, -1.0f, 1.0f);
    v[1] = ogl_clampf(g, -1.0f, 1.0f);
    v[2] = ogl_clampf(b, -1.0f, 1.0f);
    v[3] = ogl_clampf(a, -1.0f, 1.0f);
}

/* No accumulation buffer: every operation is an error. */
void STDCALL
ogl_Accum(GLenum op, GLfloat value)
{
    OGL_CTX();
    OGL_DL(OP_ACCUM, op, ogl_fw(value));
    if ((op != GL_ACCUM) && (op != GL_LOAD) && (op != GL_RETURN) && (op != GL_MULT) && (op != GL_ADD)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    ogl_error(c, GL_INVALID_OPERATION);
}

/* --- The buffers drawn into and read ------------------------------------------------------ */

void STDCALL
ogl_DrawBuffer(GLenum mode)
{
    DWORD mask;
    int   dbl;
    OGL_CTX();

    OGL_DL(OP_DRAWBUFFER, mode);
    dbl = (c->format & VGPU98_OGL_DOUBLE) != 0;
    switch (mode) {
        case GL_NONE:
        case GL_FRONT_LEFT:
        case GL_FRONT_RIGHT:
        case GL_BACK_LEFT:
        case GL_BACK_RIGHT:
        case GL_FRONT:
        case GL_BACK:
        case GL_LEFT:
        case GL_RIGHT:
        case GL_FRONT_AND_BACK:
        case GL_AUX0:
        case GL_AUX1:
        case GL_AUX2:
        case GL_AUX3:
            break;
        default:
            ogl_error(c, GL_INVALID_ENUM);
            return;
    }
    if (ogl_inside(c))
        return;
    /* No right buffers (no stereo), no aux buffers; without a back buffer
       the back names find none: an error when none of the buffers named
       exists. */
    mask = ogl_draw_mask(mode);
    if (!dbl)
        mask &= 1u;
    if ((mode != GL_NONE) && !mask) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    c->gl->color_buffer.draw_buffer = mode;
    ogl_regs_fragment(c);
}

void STDCALL
ogl_ReadBuffer(GLenum mode)
{
    int dbl;
    OGL_CTX();

    OGL_DL(OP_READBUFFER, mode);
    dbl = (c->format & VGPU98_OGL_DOUBLE) != 0;
    switch (mode) {
        case GL_FRONT_LEFT:
        case GL_FRONT:
        case GL_LEFT:
            break;
        case GL_BACK_LEFT:
        case GL_BACK:
            if (!dbl) {
                ogl_error(c, GL_INVALID_OPERATION);
                return;
            }
            break;
        case GL_FRONT_RIGHT:
        case GL_BACK_RIGHT:
        case GL_RIGHT:
        case GL_AUX0:
        case GL_AUX1:
        case GL_AUX2:
        case GL_AUX3:
            ogl_error(c, GL_INVALID_OPERATION);
            return;
        default:
            ogl_error(c, GL_INVALID_ENUM);
            return;
    }
    if (ogl_inside(c))
        return;
    c->gl->pixel_mode.read_buffer = mode;
    ogl_regs_fragment(c);
}

/* --- Clip planes ----------------------------------------------------------------------------- */

/* The modelview's inverse, for planes given now. */
static const float *
inverse(ogl_state_t *s)
{
    ogl_normal_matrix(s); /* keeps the inverse with it */
    return s->modelview_inverse;
}

void STDCALL
ogl_ClipPlane(GLenum plane, const GLdouble *eq)
{
    double e[4] = { 0.0, 0.0, 0.0, 0.0 };
    OGL_CTX();

    if (eq)
        memcpy(e, eq, sizeof(e));
    if (ogl_dl_on(c)) {
        DWORD w[9];

        w[0] = plane;
        ogl_dw(&w[1], e[0]);
        ogl_dw(&w[3], e[1]);
        ogl_dw(&w[5], e[2]);
        ogl_dw(&w[7], e[3]);
        if (ogl_dl(c, OP_CLIPPLANE, w, 9))
            return;
    }
    if ((plane < GL_CLIP_PLANE0) || (plane >= GL_CLIP_PLANE0 + OGL_MAX_CLIP_PLANES)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    ogl_plane_through(c->gl->transform.clip_plane[plane - GL_CLIP_PLANE0], e, inverse(c->gl));
    ogl_regs_clip(c);
}

void STDCALL
ogl_GetClipPlane(GLenum plane, GLdouble *eq)
{
    OGL_CTX();
    if ((plane < GL_CLIP_PLANE0) || (plane >= GL_CLIP_PLANE0 + OGL_MAX_CLIP_PLANES)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c) || !eq)
        return;
    memcpy(eq, c->gl->transform.clip_plane[plane - GL_CLIP_PLANE0], 4 * sizeof(double));
}

/* --- The texture environment -------------------------------------------------------------- */

void STDCALL
ogl_TexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int   i;
    OGL_CTX();

    if (params)
        memcpy(v, params, ((pname == GL_TEXTURE_ENV_COLOR) ? 4 : 1) * sizeof(float));
    OGL_DL(OP_TEXENV, target, pname, ogl_fw(v[0]), ogl_fw(v[1]), ogl_fw(v[2]), ogl_fw(v[3]));
    if (target != GL_TEXTURE_ENV) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    if (pname == GL_TEXTURE_ENV_MODE) {
        const GLenum m = (GLenum) v[0];

        if ((m != GL_MODULATE) && (m != GL_DECAL) && (m != GL_BLEND) && (m != GL_REPLACE)) {
            ogl_error(c, GL_INVALID_ENUM);
            return;
        }
        OGL_UNIT(c)->env_mode = m;
    } else if (pname == GL_TEXTURE_ENV_COLOR) {
        for (i = 0; i < 4; i++)
            OGL_UNIT(c)->env_color[i] = ogl_clampf(v[i], 0.0f, 1.0f);
    } else {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    ogl_regs_texture(c);
}

void STDCALL
ogl_TexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    OGL_CTX();
    if (pname == GL_TEXTURE_ENV_COLOR) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    ogl_TexEnvfv(target, pname, &param);
}

void STDCALL
ogl_TexEnvi(GLenum target, GLenum pname, GLint param)
{
    ogl_TexEnvf(target, pname, (GLfloat) param);
}

void STDCALL
ogl_TexEnviv(GLenum target, GLenum pname, const GLint *params)
{
    float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int   i;

    if (params) {
        if (pname == GL_TEXTURE_ENV_COLOR) {
            for (i = 0; i < 4; i++)
                v[i] = int_color(params[i]);
        } else
            v[0] = (float) params[0];
    }
    ogl_TexEnvfv(target, pname, v);
}

void STDCALL
ogl_GetTexEnvfv(GLenum target, GLenum pname, GLfloat *params)
{
    OGL_CTX();
    if ((target != GL_TEXTURE_ENV) || ((pname != GL_TEXTURE_ENV_MODE) && (pname != GL_TEXTURE_ENV_COLOR))) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c) || !params)
        return;
    if (pname == GL_TEXTURE_ENV_MODE)
        params[0] = (float) OGL_UNIT(c)->env_mode;
    else
        memcpy(params, OGL_UNIT(c)->env_color, 4 * sizeof(float));
}

void STDCALL
ogl_GetTexEnviv(GLenum target, GLenum pname, GLint *params)
{
    int i;
    OGL_CTX();

    if ((target != GL_TEXTURE_ENV) || ((pname != GL_TEXTURE_ENV_MODE) && (pname != GL_TEXTURE_ENV_COLOR))) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c) || !params)
        return;
    if (pname == GL_TEXTURE_ENV_MODE)
        params[0] = (GLint) OGL_UNIT(c)->env_mode;
    else {
        for (i = 0; i < 4; i++)
            params[i] = color_int(OGL_UNIT(c)->env_color[i]);
    }
}

/* --- Texture coordinate generation --------------------------------------------------------- */

void STDCALL
ogl_TexGendv(GLenum coord, GLenum pname, const GLdouble *params)
{
    double v[4] = { 0.0, 0.0, 0.0, 0.0 };
    int    i;
    OGL_CTX();

    if (params)
        memcpy(v, params, ((pname == GL_TEXTURE_GEN_MODE) ? 1 : 4) * sizeof(double));
    if (ogl_dl_on(c)) {
        DWORD w[10];

        w[0] = coord;
        w[1] = pname;
        for (i = 0; i < 4; i++)
            ogl_dw(&w[2 + 2 * i], v[i]);
        if (ogl_dl(c, OP_TEXGEN, w, 10))
            return;
    }
    if ((coord < GL_S) || (coord > GL_Q)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    i = (int) (coord - GL_S);
    switch (pname) {
        case GL_TEXTURE_GEN_MODE: {
            const GLenum m = (GLenum) v[0];

            if (((m != GL_OBJECT_LINEAR) && (m != GL_EYE_LINEAR) && (m != GL_SPHERE_MAP)) ||
                ((m == GL_SPHERE_MAP) && (i >= 2))) {
                ogl_error(c, GL_INVALID_ENUM);
                return;
            }
            OGL_UNIT(c)->gen_mode[i] = m;
            break;
        }
        case GL_OBJECT_PLANE:
            memcpy(OGL_UNIT(c)->obj_plane[i], v, sizeof(v));
            break;
        case GL_EYE_PLANE:
            /* Into eye coordinates through the modelview's inverse now. */
            ogl_plane_through(OGL_UNIT(c)->eye_plane[i], v, inverse(c->gl));
            break;
        default:
            ogl_error(c, GL_INVALID_ENUM);
            return;
    }
    ogl_regs_texgen(c);
}

void STDCALL
ogl_TexGend(GLenum coord, GLenum pname, GLdouble param)
{
    OGL_CTX();
    if (pname != GL_TEXTURE_GEN_MODE) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    ogl_TexGendv(coord, pname, &param);
}

void STDCALL
ogl_TexGenf(GLenum coord, GLenum pname, GLfloat param)
{
    ogl_TexGend(coord, pname, (GLdouble) param);
}

void STDCALL
ogl_TexGeni(GLenum coord, GLenum pname, GLint param)
{
    ogl_TexGend(coord, pname, (GLdouble) param);
}

void STDCALL
ogl_TexGenfv(GLenum coord, GLenum pname, const GLfloat *params)
{
    double v[4] = { 0.0, 0.0, 0.0, 0.0 };
    int    i;

    for (i = 0; params && (i < ((pname == GL_TEXTURE_GEN_MODE) ? 1 : 4)); i++)
        v[i] = params[i];
    ogl_TexGendv(coord, pname, v);
}

void STDCALL
ogl_TexGeniv(GLenum coord, GLenum pname, const GLint *params)
{
    double v[4] = { 0.0, 0.0, 0.0, 0.0 };
    int    i;

    for (i = 0; params && (i < ((pname == GL_TEXTURE_GEN_MODE) ? 1 : 4)); i++)
        v[i] = params[i];
    ogl_TexGendv(coord, pname, v);
}

void STDCALL
ogl_GetTexGendv(GLenum coord, GLenum pname, GLdouble *params)
{
    int i;
    OGL_CTX();

    if ((coord < GL_S) || (coord > GL_Q) ||
        ((pname != GL_TEXTURE_GEN_MODE) && (pname != GL_OBJECT_PLANE) && (pname != GL_EYE_PLANE))) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c) || !params)
        return;
    i = (int) (coord - GL_S);
    if (pname == GL_TEXTURE_GEN_MODE)
        params[0] = (double) OGL_UNIT(c)->gen_mode[i];
    else
        memcpy(params, (pname == GL_OBJECT_PLANE) ? OGL_UNIT(c)->obj_plane[i] : OGL_UNIT(c)->eye_plane[i],
               4 * sizeof(double));
}

void STDCALL
ogl_GetTexGenfv(GLenum coord, GLenum pname, GLfloat *params)
{
    double v[4] = { 0.0, 0.0, 0.0, 0.0 };
    int    i;
    OGL_CTX();

    if ((coord < GL_S) || (coord > GL_Q) ||
        ((pname != GL_TEXTURE_GEN_MODE) && (pname != GL_OBJECT_PLANE) && (pname != GL_EYE_PLANE))) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (c->gl->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    ogl_GetTexGendv(coord, pname, v);
    for (i = 0; params && (i < ((pname == GL_TEXTURE_GEN_MODE) ? 1 : 4)); i++)
        params[i] = (float) v[i];
}

void STDCALL
ogl_GetTexGeniv(GLenum coord, GLenum pname, GLint *params)
{
    double v[4] = { 0.0, 0.0, 0.0, 0.0 };
    int    i;
    OGL_CTX();

    if ((coord < GL_S) || (coord > GL_Q) ||
        ((pname != GL_TEXTURE_GEN_MODE) && (pname != GL_OBJECT_PLANE) && (pname != GL_EYE_PLANE))) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (c->gl->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    ogl_GetTexGendv(coord, pname, v);
    for (i = 0; params && (i < ((pname == GL_TEXTURE_GEN_MODE) ? 1 : 4)); i++)
        params[i] = ogl_round(v[i]);
}

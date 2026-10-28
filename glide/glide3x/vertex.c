/*
 * glide3x.dll: drawing: the vertex layout,
 * window and clip coordinates, the viewport and depth range, Glide 3's
 * points, lines and triangles and its vertex arrays in every mode, on the
 * Glide core (glcore.h, draw.c).
 *
 * A vertex is read through the layout into the device's form as a
 * Voodoo3's Glide 3 builds its parameters: in window coordinates the
 * program's own values (x, y, 65535/z, q, colours 0-255, s/q and t/q in
 * the 256 level's texels); in clip coordinates divided by w and mapped
 * through the viewport, the depth range and each TMU's scale from
 * grTexSource. A parameter the layout leaves out keeps its last value.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glide3x.h"
#include "glcore.h"

#define GR_PARAM_XY      0x01
#define GR_PARAM_Z       0x02
#define GR_PARAM_W       0x03
#define GR_PARAM_Q       0x04
#define GR_PARAM_FOG_EXT 0x05
#define GR_PARAM_A       0x10
#define GR_PARAM_RGB     0x20
#define GR_PARAM_PARGB   0x30
#define GR_PARAM_ST0     0x40
#define GR_PARAM_ST1     0x41
#define GR_PARAM_ST2     0x42
#define GR_PARAM_Q0      0x50
#define GR_PARAM_Q1      0x51
#define GR_PARAM_Q2      0x52

#define GR_WINDOW_COORDS 0
#define GR_CLIP_COORDS   1

#define GR_POINTS                  0
#define GR_LINE_STRIP              1
#define GR_LINES                   2
#define GR_POLYGON                 3
#define GR_TRIANGLE_STRIP          4
#define GR_TRIANGLE_FAN            5
#define GR_TRIANGLES               6
#define GR_TRIANGLE_STRIP_CONTINUE 7
#define GR_TRIANGLE_FAN_CONTINUE   8

#define GR_AA_ORDERED          0x01
#define GR_ALLOW_MIPMAP_DITHER 0x02
#define GR_PASSTHRU            0x03
#define GR_SHAMELESS_PLUG      0x04
#define GR_VIDEO_SMOOTHING     0x05

/* The layout's parameters, in the order a Voodoo3's Glide 3 keeps them. */
enum { L_XY, L_Z, L_W, L_A, L_FOG, L_RGB, L_PARGB, L_ST0, L_ST1, L_Q, L_Q0, L_Q1, L_COUNT };

/* grGlideGetVertexLayout's image: 108 bytes, as a Voodoo3's. */
typedef struct layout {
    struct {
        FxI32 mode;
        FxI32 offset;
    } p[L_COUNT];
    FxI32 color_byte; /* 1: PARGB (the last colour parameter enabled), 0: floats */
    FxI32 spare[2];
} layout_t;

_Static_assert(sizeof(layout_t) == 108, "the vertex layout is not 108 bytes");

static layout_t  lay;
static glc_vtx_t last;          /* the last vertex: what a parameter left out keeps */
static int       space;         /* GR_WINDOW_COORDS or GR_CLIP_COORDS */
static float     vp[4];         /* the viewport: x, y, width, height */
static float     zr[2] = { 0.0f, 1.0f }; /* the depth range */
static float     st_scale[2][2] = { { 256.0f, 256.0f }, { 256.0f, 256.0f } }; /* each TMU's, from grTexSource */
static int       aa_ordered;

/* A strip's or fan's vertices kept (two at most), for the CONTINUE modes,
   as a Voodoo3's setup unit keeps them; kept only straight after a strip
   or a fan (a polygon is a fan). */
static glc_vtx_t strip[2];
static int       strip_odd, strip_n;
static glc_vtx_t fan_c, fan_last;
static int       fan_n;

static int
param_index(FxU32 param)
{
    switch (param) {
        case GR_PARAM_XY:
            return L_XY;
        case GR_PARAM_Z:
            return L_Z;
        case GR_PARAM_W:
            return L_W;
        case GR_PARAM_Q:
            return L_Q;
        case GR_PARAM_FOG_EXT:
            return L_FOG;
        case GR_PARAM_A:
            return L_A;
        case GR_PARAM_RGB:
            return L_RGB;
        case GR_PARAM_PARGB:
            return L_PARGB;
        case GR_PARAM_ST0:
            return L_ST0;
        case GR_PARAM_ST1:
            return L_ST1;
        case GR_PARAM_Q0:
            return L_Q0;
        case GR_PARAM_Q1:
            return L_Q1;
        default:
            return -1; /* ST2, Q2: a third TMU, which a Voodoo3 has not */
    }
}

/* The layout empty, colours floats, every kept value as at the start. */
static void
layout_reset(void)
{
    memset(&lay, 0, sizeof(lay));
    memset(&last, 0, sizeof(last));
    last.f[VGPU98_GLV_OOW]  = 1.0f;
    last.f[VGPU98_GLV_OOW0] = 1.0f;
    last.f[VGPU98_GLV_OOW1] = 1.0f;
}

/* The layout as it starts, once (a program may set it before its window). */
static void
ready(void)
{
    static int done;

    if (!done) {
        done = 1;
        layout_reset();
    }
}

/* At grSstWinOpen, as a Voodoo3's Glide 3 sets them there: window
   coordinates, the viewport the window. The layout stays the program's. */
void
glide3x_draw_open(void)
{
    ready();
    space    = GR_WINDOW_COORDS;
    vp[0]    = 0.0f;
    vp[1]    = 0.0f;
    vp[2]    = (float) glc_width();
    vp[3]    = (float) glc_height();
    zr[0]    = 0.0f;
    zr[1]    = 1.0f;
    strip_n  = fan_n = 0;
}

void
glide3x_layout_reset(void)
{
    layout_reset();
}

/* grTexSource's s and t scales for clip coordinates: 256 the long side. */
void
glide3x_tex_scale(FxI32 tmu, int aspect)
{
    if ((tmu != 0) && (tmu != 1))
        return;
    st_scale[tmu][0] = (aspect >= 0) ? 256.0f : (float) (256 >> -aspect);
    st_scale[tmu][1] = (aspect <= 0) ? 256.0f : (float) (256 >> aspect);
}

void
glide3x_viewport(FxI32 *out)
{
    out[0] = (FxI32) vp[0];
    out[1] = (FxI32) vp[1];
    out[2] = (FxI32) vp[2];
    out[3] = (FxI32) vp[3];
}

/* The state grGlideGetState keeps of drawing: the layout, the space, the
   viewport, the depth range. */
typedef struct draw_state {
    layout_t lay;
    FxI32    space;
    float    vp[4];
    float    zr[2];
    FxI32    aa;
    float    st_scale[2][2]; /* grTexSource's, as a Voodoo3's GrState keeps them */
} draw_state_t;

DWORD
glide3x_draw_state_bytes(void)
{
    return sizeof(draw_state_t);
}

void
glide3x_draw_state_get(void *out)
{
    draw_state_t s;

    s.lay   = lay;
    s.space = space;
    memcpy(s.vp, vp, sizeof(vp));
    memcpy(s.zr, zr, sizeof(zr));
    s.aa = aa_ordered;
    memcpy(s.st_scale, st_scale, sizeof(st_scale));
    memcpy(out, &s, sizeof(s));
}

void
glide3x_draw_state_set(const void *in)
{
    draw_state_t s;

    memcpy(&s, in, sizeof(s));
    lay        = s.lay;
    space      = (s.space == GR_CLIP_COORDS) ? GR_CLIP_COORDS : GR_WINDOW_COORDS;
    memcpy(vp, s.vp, sizeof(vp));
    memcpy(zr, s.zr, sizeof(zr));
    aa_ordered = s.aa != 0;
    memcpy(st_scale, s.st_scale, sizeof(st_scale));
    strip_n = fan_n = 0;
}

void FX_CALL
grVertexLayout(FxU32 param, FxI32 offset, FxU32 mode)
{
    const int i = param_index(param);

    CALLED(grVertexLayout);
    ready();
    if (i < 0)
        return;
    lay.p[i].mode   = mode ? 1 : 0;
    lay.p[i].offset = offset;
    /* The colour's kind: the last of them enabled. */
    if (mode && ((i == L_A) || (i == L_RGB)))
        lay.color_byte = 0;
    else if (mode && (i == L_PARGB))
        lay.color_byte = 1;
}

void FX_CALL
grGlideGetVertexLayout(void *layout)
{
    CALLED(grGlideGetVertexLayout);
    if (layout)
        memcpy(layout, &lay, sizeof(lay));
}

void FX_CALL
grGlideSetVertexLayout(const void *layout)
{
    CALLED(grGlideSetVertexLayout);
    if (layout)
        memcpy(&lay, layout, sizeof(lay));
}

void FX_CALL
grCoordinateSpace(GrCoordinateSpaceMode_t mode)
{
    CALLED(grCoordinateSpace);
    if ((mode == GR_WINDOW_COORDS) || (mode == GR_CLIP_COORDS))
        space = mode;
}

/* x and y from the origin's corner; a negative width or height mirrors. */
void FX_CALL
grViewport(FxI32 x, FxI32 y, FxI32 width, FxI32 height)
{
    CALLED(grViewport);
    vp[0] = (float) x;
    vp[1] = (float) y;
    vp[2] = (float) width;
    vp[3] = (float) height;
}

void FX_CALL
grDepthRange(FxFloat n, FxFloat f)
{
    CALLED(grDepthRange);
    zr[0] = n;
    zr[1] = f;
}

void FX_CALL
grEnable(GrEnableMode_t mode)
{
    CALLED(grEnable);
    if (mode == GR_AA_ORDERED)
        aa_ordered = 1;
    else if (mode == GR_ALLOW_MIPMAP_DITHER)
        glc_mipmap_dither(1);
}

void FX_CALL
grDisable(GrEnableMode_t mode)
{
    CALLED(grDisable);
    if (mode == GR_AA_ORDERED)
        aa_ordered = 0;
    else if (mode == GR_ALLOW_MIPMAP_DITHER)
        glc_mipmap_dither(0);
}

/* --- Reading vertices -------------------------------------------------------- */

static float
fget(const uint8_t *v, int i, int k)
{
    float f;

    memcpy(&f, v + lay.p[i].offset + k * 4, 4);
    return f;
}

static int
on(int i)
{
    return lay.p[i].mode != 0;
}

/* An active TMU0's q0 reaches TMU1 (the setup unit's W0 goes to both),
   unless the layout has GR_PARAM_FOG_EXT and the vertex's w (window: Q;
   clip: W): a Voodoo3's Glide 3 then puts the fog coordinate in the
   vertex's W and sends each active TMU its own, q1 or else the vertex's. */
static int
q0_reaches_tmu1(int window)
{
    return on(L_Q0) && glc_tmu_active(0) && !(on(L_FOG) && on(window ? L_Q : L_W));
}

static void
read_vertex(glc_vtx_t *o, const void *vertex)
{
    const uint8_t *v = (const uint8_t *) vertex;
    float         *f = o->f;
    float          oow = 1.0f, pw;

    ready();
    *o = last;
    f[VGPU98_GLV_X] = fget(v, L_XY, 0);
    f[VGPU98_GLV_Y] = fget(v, L_XY, 1);
    if (space == GR_CLIP_COORDS) {
        /* Divided by w, through the viewport and the depth range. */
        const float w = on(L_W) ? fget(v, L_W, 0) : 1.0f;

        oow             = (w != 0.0f) ? 1.0f / w : 0.0f;
        f[VGPU98_GLV_X] = f[VGPU98_GLV_X] * oow * (vp[2] * 0.5f) + (vp[0] + vp[2] * 0.5f);
        f[VGPU98_GLV_Y] = f[VGPU98_GLV_Y] * oow * (vp[3] * 0.5f) + (vp[1] + vp[3] * 0.5f);
        if (on(L_Z))
            f[VGPU98_GLV_OOZ] = fget(v, L_Z, 0) * oow * ((zr[1] - zr[0]) * 0.5f * 65535.0f) + (zr[1] + zr[0]) * 0.5f * 65535.0f;
        pw = on(L_Q) ? fget(v, L_Q, 0) * oow : oow;
        f[VGPU98_GLV_OOW] = pw;
        f[VGPU98_GLV_FOG] = on(L_FOG) ? fget(v, L_FOG, 0) * oow : pw;
        if (lay.color_byte && on(L_PARGB)) {
            FxU32 c;

            memcpy(&c, v + lay.p[L_PARGB].offset, 4);
            f[VGPU98_GLV_A] = (float) (c >> 24);
            f[VGPU98_GLV_R] = (float) ((c >> 16) & 0xff);
            f[VGPU98_GLV_G] = (float) ((c >> 8) & 0xff);
            f[VGPU98_GLV_B] = (float) (c & 0xff);
        } else {
            if (on(L_RGB)) {
                f[VGPU98_GLV_R] = fget(v, L_RGB, 0) * 255.0f;
                f[VGPU98_GLV_G] = fget(v, L_RGB, 1) * 255.0f;
                f[VGPU98_GLV_B] = fget(v, L_RGB, 2) * 255.0f;
            }
            if (on(L_A))
                f[VGPU98_GLV_A] = fget(v, L_A, 0) * 255.0f;
        }
        if (on(L_ST0)) {
            f[VGPU98_GLV_SOW0] = fget(v, L_ST0, 0) * oow * st_scale[0][0];
            f[VGPU98_GLV_TOW0] = fget(v, L_ST0, 1) * oow * st_scale[0][1];
        }
        if (on(L_ST1)) {
            f[VGPU98_GLV_SOW1] = fget(v, L_ST1, 0) * oow * st_scale[1][0];
            f[VGPU98_GLV_TOW1] = fget(v, L_ST1, 1) * oow * st_scale[1][1];
        } else if (on(L_ST0)) {
            /* TMU0's, as sent: scaled by TMU0's texture. */
            f[VGPU98_GLV_SOW1] = f[VGPU98_GLV_SOW0];
            f[VGPU98_GLV_TOW1] = f[VGPU98_GLV_TOW0];
        }
        {
            /* Without its own, a TMU keeps what the vertex's w gave every
               unit: Q/w, or 1/w with FOG_EXT (the fog coordinate in the
               vertex's W, each TMU sent 1/w). */
            const float base = on(L_FOG) ? oow : pw;

            f[VGPU98_GLV_OOW0] = on(L_Q0) ? fget(v, L_Q0, 0) * oow : base;
            /* TMU1's: its q1, else an active TMU0's q0, else the vertex's. */
            f[VGPU98_GLV_OOW1] = on(L_Q1) ? fget(v, L_Q1, 0) * oow : q0_reaches_tmu1(0) ? f[VGPU98_GLV_OOW0] : base;
        }
    } else {
        /* Window coordinates: the program's own values. */
        if (on(L_Z))
            f[VGPU98_GLV_OOZ] = fget(v, L_Z, 0);
        if (on(L_Q))
            f[VGPU98_GLV_OOW] = fget(v, L_Q, 0);
        /* Table fog on the fog coordinate without one reads q, as a
           Voodoo3's Glide 3 indexes it then. */
        f[VGPU98_GLV_FOG] = on(L_FOG) ? fget(v, L_FOG, 0) : f[VGPU98_GLV_OOW];
        if (lay.color_byte && on(L_PARGB)) {
            FxU32 c;

            memcpy(&c, v + lay.p[L_PARGB].offset, 4);
            f[VGPU98_GLV_A] = (float) (c >> 24);
            f[VGPU98_GLV_R] = (float) ((c >> 16) & 0xff);
            f[VGPU98_GLV_G] = (float) ((c >> 8) & 0xff);
            f[VGPU98_GLV_B] = (float) (c & 0xff);
        } else {
            if (on(L_RGB)) {
                f[VGPU98_GLV_R] = fget(v, L_RGB, 0);
                f[VGPU98_GLV_G] = fget(v, L_RGB, 1);
                f[VGPU98_GLV_B] = fget(v, L_RGB, 2);
            }
            if (on(L_A))
                f[VGPU98_GLV_A] = fget(v, L_A, 0);
        }
        if (on(L_ST0)) {
            f[VGPU98_GLV_SOW0] = fget(v, L_ST0, 0);
            f[VGPU98_GLV_TOW0] = fget(v, L_ST0, 1);
        }
        /* TMU1's s and t are TMU0's without their own. */
        if (on(L_ST1)) {
            f[VGPU98_GLV_SOW1] = fget(v, L_ST1, 0);
            f[VGPU98_GLV_TOW1] = fget(v, L_ST1, 1);
        } else if (on(L_ST0)) {
            f[VGPU98_GLV_SOW1] = f[VGPU98_GLV_SOW0];
            f[VGPU98_GLV_TOW1] = f[VGPU98_GLV_TOW0];
        }
        f[VGPU98_GLV_OOW0] = on(L_Q0) ? fget(v, L_Q0, 0) : f[VGPU98_GLV_OOW];
        f[VGPU98_GLV_OOW1] = on(L_Q1) ? fget(v, L_Q1, 0) : q0_reaches_tmu1(1) ? f[VGPU98_GLV_OOW0] : f[VGPU98_GLV_OOW];
    }
    /* Diagnostics: where an active TMU1's came from (the window's summary). */
    if (glc_tmu_active(1)) {
        glc_count(on(L_Q1) ? GLC_T1W_OWN : q0_reaches_tmu1(space != GR_CLIP_COORDS) ? GLC_T1W_TMU0 : GLC_T1W_VERTEX);
        glc_count(on(L_ST1) ? GLC_T1ST_OWN : GLC_T1ST_TMU0);
    }
    last = *o;
}

/* --- Drawing ------------------------------------------------------------------ */

static void
aa_said(void)
{
    if (aa_ordered)
        glc_deferred(GLC_DEFER_AA);
}

void FX_CALL
grDrawPoint(const void *pt)
{
    glc_vtx_t v;

    CALLED(grDrawPoint);
    strip_n = fan_n = 0;
    if (!pt)
        return;
    aa_said();
    read_vertex(&v, pt);
    glc_point(&v);
}

void FX_CALL
grDrawLine(const void *v1, const void *v2)
{
    glc_vtx_t a, b;

    CALLED(grDrawLine);
    strip_n = fan_n = 0;
    if (!v1 || !v2)
        return;
    aa_said();
    read_vertex(&a, v1);
    read_vertex(&b, v2);
    glc_line(&a, &b);
}

void FX_CALL
grDrawTriangle(const void *a, const void *b, const void *c)
{
    glc_vtx_t va, vb, vc;

    CALLED(grDrawTriangle);
    strip_n = fan_n = 0;
    if (!a || !b || !c)
        return;
    aa_said();
    read_vertex(&va, a);
    read_vertex(&vb, b);
    read_vertex(&vc, c);
    glc_triangle(&va, &vb, &vc);
}

void FX_CALL
grAADrawTriangle(const void *a, const void *b, const void *c, FxBool ab_antialias, FxBool bc_antialias,
                 FxBool ca_antialias)
{
    glc_vtx_t va, vb, vc;

    CALLED(grAADrawTriangle);
    (void) ab_antialias;
    (void) bc_antialias;
    (void) ca_antialias;
    strip_n = fan_n = 0;
    glc_deferred(GLC_DEFER_AA);
    if (!a || !b || !c)
        return;
    read_vertex(&va, a);
    read_vertex(&vb, b);
    read_vertex(&vc, c);
    glc_triangle(&va, &vb, &vc);
}

/* The n-th vertex of an array: of pointers, or at base + n * stride. */
typedef struct varray {
    void *const   *ptrs;
    const uint8_t *base;
    FxU32          stride;
} varray_t;

static const void *
vertex_at(const varray_t *a, FxU32 n)
{
    return a->ptrs ? a->ptrs[n] : (const void *) (a->base + (size_t) n * a->stride);
}

/* A strip's next vertex: from the third on a triangle with the two before
   it, every other one swapped, so every triangle of it winds as its first
   does. */
static void
strip_step(const glc_vtx_t *v)
{
    if (strip_n < 2) {
        strip[strip_n++] = *v;
        return;
    }
    if (strip_odd)
        glc_triangle(&strip[1], &strip[0], v);
    else
        glc_triangle(&strip[0], &strip[1], v);
    strip[0]  = strip[1];
    strip[1]  = *v;
    strip_odd = !strip_odd;
}

/* A fan's next vertex: the centre, then from the third on a triangle with
   the centre and the one before. */
static void
fan_step(const glc_vtx_t *v)
{
    if (fan_n == 0) {
        fan_c = *v;
        fan_n = 1;
    } else if (fan_n == 1) {
        fan_last = *v;
        fan_n    = 2;
    } else {
        glc_triangle(&fan_c, &fan_last, v);
        fan_last = *v;
    }
}

static void
draw_array(FxU32 mode, FxU32 count, const varray_t *a)
{
    glc_vtx_t v, w;
    FxU32     i;

    /* What a strip or fan kept goes with any other mode. */
    if ((mode != GR_TRIANGLE_STRIP_CONTINUE) && (mode != GR_TRIANGLE_STRIP))
        strip_n = 0;
    if ((mode != GR_TRIANGLE_FAN_CONTINUE) && (mode != GR_TRIANGLE_FAN) && (mode != GR_POLYGON))
        fan_n = 0;
    switch (mode) {
        case GR_POINTS:
            aa_said();
            for (i = 0; i < count; i++) {
                read_vertex(&v, vertex_at(a, i));
                glc_point(&v);
            }
            break;
        case GR_LINE_STRIP:
            aa_said();
            if (count < 2)
                break;
            read_vertex(&v, vertex_at(a, 0));
            for (i = 1; i < count; i++) {
                read_vertex(&w, vertex_at(a, i));
                glc_line(&v, &w);
                v = w;
            }
            break;
        case GR_LINES:
            aa_said();
            for (i = 0; i < count / 2 * 2; i += 2) {
                read_vertex(&v, vertex_at(a, i));
                read_vertex(&w, vertex_at(a, i + 1));
                glc_line(&v, &w);
            }
            break;
        case GR_POLYGON:
        case GR_TRIANGLE_FAN:
            fan_n = 0;
            __attribute__((fallthrough)); /* a new fan */
        case GR_TRIANGLE_FAN_CONTINUE:
            for (i = 0; i < count; i++) {
                read_vertex(&v, vertex_at(a, i));
                fan_step(&v);
            }
            break;
        case GR_TRIANGLE_STRIP:
            strip_n   = 0;
            strip_odd = 0;
            __attribute__((fallthrough)); /* a new strip */
        case GR_TRIANGLE_STRIP_CONTINUE:
            for (i = 0; i < count; i++) {
                read_vertex(&v, vertex_at(a, i));
                strip_step(&v);
            }
            break;
        case GR_TRIANGLES:
            aa_said();
            for (i = 0; i < count / 3 * 3; i += 3) {
                glc_vtx_t u;

                read_vertex(&u, vertex_at(a, i));
                read_vertex(&v, vertex_at(a, i + 1));
                read_vertex(&w, vertex_at(a, i + 2));
                glc_triangle(&u, &v, &w);
            }
            break;
        default:
            break;
    }
}

void FX_CALL
grDrawVertexArray(FxU32 mode, FxU32 Count, void *pointers)
{
    varray_t a;

    CALLED(grDrawVertexArray);
    if (!pointers)
        return;
    a.ptrs   = (void *const *) pointers;
    a.base   = NULL;
    a.stride = 0;
    draw_array(mode, Count, &a);
}

void FX_CALL
grDrawVertexArrayContiguous(FxU32 mode, FxU32 Count, void *pointers, FxU32 stride)
{
    varray_t a;

    CALLED(grDrawVertexArrayContiguous);
    if (!pointers)
        return;
    a.ptrs   = NULL;
    a.base   = (const uint8_t *) pointers;
    a.stride = stride;
    draw_array(mode, Count, &a);
}

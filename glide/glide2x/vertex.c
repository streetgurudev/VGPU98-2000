/*
 * glide2x.dll: drawing: Glide 2's
 * vertices, each read into the device's form with grHints' STW hint, and
 * its points, lines, triangles and polygons, the antialiased forms drawn
 * plain, and the utility library's clipped triangles and polygons.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glide2x.h"
#include "glcore.h"

/* GrVertex (60 bytes: GLIDE_NUM_TMU 2), as Glide 2 programs pass it. */
struct GrVertex {
    float x, y, z;
    float r, g, b;
    float ooz;
    float a;
    float oow;
    struct {
        float sow, tow, oow;
    } tmuvtx[GLIDE_NUM_TMU];
};

_Static_assert(sizeof(GrVertex) == 60, "GrVertex is not 60 bytes");

#define GR_HINT_STWHINT            0
#define GR_HINT_ALLOW_MIPMAP_DITHER 3
#define GR_STWHINT_W_DIFF_TMU0  0x02
#define GR_STWHINT_ST_DIFF_TMU1 0x10
#define GR_STWHINT_W_DIFF_TMU1  0x08

static FxU32 stw_hint; /* grHints' GR_HINT_STWHINT */

/* A vertex in the device's form, each TMU's s, t and 1/w as a Voodoo3's
   Glide sends them to its setup unit: the
   vertex's 1/w reaches every TMU; an active TMU0's own 1/w only with the
   hint, and to TMU1 as well; an active TMU0's s and t to both; an active
   TMU1's own 1/w only with the hint; an active TMU1's own s and t when
   TMU0 is not active, or with the hint. A TMU that is not active samples
   nothing, whatever reached it. */
static void
read_vertex(glc_vtx_t *o, const GrVertex *v)
{
    float      *f  = o->f;
    const int   a0 = glc_tmu_active(0), a1 = glc_tmu_active(1);
    const FxU32 hint = stw_hint;
    const int   w0  = a0 && (hint & GR_STWHINT_W_DIFF_TMU0);
    const int   w1  = a1 && (hint & GR_STWHINT_W_DIFF_TMU1);
    const int   st1 = a1 && (!a0 || (hint & GR_STWHINT_ST_DIFF_TMU1));

    f[VGPU98_GLV_X]    = v->x;
    f[VGPU98_GLV_Y]    = v->y;
    f[VGPU98_GLV_OOZ]  = v->ooz;
    f[VGPU98_GLV_OOW]  = v->oow;
    f[VGPU98_GLV_R]    = v->r;
    f[VGPU98_GLV_G]    = v->g;
    f[VGPU98_GLV_B]    = v->b;
    f[VGPU98_GLV_A]    = v->a;
    f[VGPU98_GLV_SOW0] = v->tmuvtx[0].sow;
    f[VGPU98_GLV_TOW0] = v->tmuvtx[0].tow;
    f[VGPU98_GLV_OOW0] = w0 ? v->tmuvtx[0].oow : v->oow;
    if (st1) {
        f[VGPU98_GLV_SOW1] = v->tmuvtx[1].sow;
        f[VGPU98_GLV_TOW1] = v->tmuvtx[1].tow;
    } else {
        f[VGPU98_GLV_SOW1] = v->tmuvtx[0].sow;
        f[VGPU98_GLV_TOW1] = v->tmuvtx[0].tow;
    }
    /* Its own, else what TMU0's reached it with: TMU0's own, or the vertex's. */
    f[VGPU98_GLV_OOW1] = w1 ? v->tmuvtx[1].oow : f[VGPU98_GLV_OOW0];
    /* Diagnostics: where an active TMU1's came from (the window's summary). */
    if (a1) {
        glc_count(w1 ? GLC_T1W_OWN : w0 ? GLC_T1W_TMU0 : GLC_T1W_VERTEX);
        glc_count(st1 ? GLC_T1ST_OWN : GLC_T1ST_TMU0);
    }
    f[VGPU98_GLV_FOG]  = 0.0f;
    f[VGPU98_GLV_PAD]  = 0.0f;
}

FxU32
glide2x_stw_hint(void)
{
    return stw_hint;
}

void
glide2x_stw_hint_set(FxU32 hint)
{
    stw_hint = hint;
}

void FX_CALL
grHints(GrHint_t hintType, FxU32 hintMask)
{
    CALLED(grHints);
    if (hintType == GR_HINT_STWHINT)
        stw_hint = hintMask;
    else if (hintType == GR_HINT_ALLOW_MIPMAP_DITHER)
        glc_mipmap_dither(hintMask != 0);
}

/* --- Points, lines, triangles --------------------------------------------- */

void FX_CALL
grDrawPoint(const GrVertex *pt)
{
    glc_vtx_t v;

    CALLED(grDrawPoint);
    if (!pt)
        return;
    read_vertex(&v, pt);
    glc_point(&v);
}

void FX_CALL
grDrawLine(const GrVertex *v1, const GrVertex *v2)
{
    glc_vtx_t a, b;

    CALLED(grDrawLine);
    if (!v1 || !v2)
        return;
    read_vertex(&a, v1);
    read_vertex(&b, v2);
    glc_line(&a, &b);
}

void FX_CALL
grDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    glc_vtx_t va, vb, vc;

    CALLED(grDrawTriangle);
    if (!a || !b || !c)
        return;
    read_vertex(&va, a);
    read_vertex(&vb, b);
    read_vertex(&vc, c);
    glc_triangle(&va, &vb, &vc);
}

/* A fan from the first vertex: triangles (0, i, i + 1), by index or in
   order. */
static void
fan(int nverts, const int ilist[], const GrVertex *vlist)
{
    glc_vtx_t first, prev, next;
    int       i;

    if ((nverts < 3) || !vlist)
        return;
    read_vertex(&first, &vlist[ilist ? ilist[0] : 0]);
    read_vertex(&prev, &vlist[ilist ? ilist[1] : 1]);
    for (i = 2; i < nverts; i++) {
        read_vertex(&next, &vlist[ilist ? ilist[i] : i]);
        glc_triangle(&first, &prev, &next);
        prev = next;
    }
}

void FX_CALL
grDrawPolygon(int nverts, const int ilist[], const GrVertex *vlist)
{
    CALLED(grDrawPolygon);
    if (ilist)
        fan(nverts, ilist, vlist);
}

void FX_CALL
grDrawPolygonVertexList(int nverts, const GrVertex *vlist)
{
    CALLED(grDrawPolygonVertexList);
    fan(nverts, NULL, vlist);
}

/* Planar polygons promise flat parameters: a Voodoo3 draws them as the
   plain ones. */
void FX_CALL
grDrawPlanarPolygon(int nverts, const int ilist[], const GrVertex *vlist)
{
    CALLED(grDrawPlanarPolygon);
    if (ilist)
        fan(nverts, ilist, vlist);
}

void FX_CALL
grDrawPlanarPolygonVertexList(int nverts, const GrVertex *vlist)
{
    CALLED(grDrawPlanarPolygonVertexList);
    fan(nverts, NULL, vlist);
}

/* --- The antialiased forms, drawn plain (edge antialiasing is deferred) -- */

void FX_CALL
grAADrawPoint(const GrVertex *pt)
{
    glc_vtx_t v;

    CALLED(grAADrawPoint);
    glc_deferred(GLC_DEFER_AA);
    if (!pt)
        return;
    read_vertex(&v, pt);
    glc_point(&v);
}

void FX_CALL
grAADrawLine(const GrVertex *v1, const GrVertex *v2)
{
    glc_vtx_t a, b;

    CALLED(grAADrawLine);
    glc_deferred(GLC_DEFER_AA);
    if (!v1 || !v2)
        return;
    read_vertex(&a, v1);
    read_vertex(&b, v2);
    glc_line(&a, &b);
}

void FX_CALL
grAADrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c, FxBool ab_antialias, FxBool bc_antialias,
                 FxBool ca_antialias)
{
    glc_vtx_t va, vb, vc;

    CALLED(grAADrawTriangle);
    (void) ab_antialias;
    (void) bc_antialias;
    (void) ca_antialias;
    glc_deferred(GLC_DEFER_AA);
    if (!a || !b || !c)
        return;
    read_vertex(&va, a);
    read_vertex(&vb, b);
    read_vertex(&vc, c);
    glc_triangle(&va, &vb, &vc);
}

void FX_CALL
grAADrawPolygon(const int nverts, const int ilist[], const GrVertex *vlist)
{
    CALLED(grAADrawPolygon);
    glc_deferred(GLC_DEFER_AA);
    if (ilist)
        fan(nverts, ilist, vlist);
}

void FX_CALL
grAADrawPolygonVertexList(const int nverts, const GrVertex *vlist)
{
    CALLED(grAADrawPolygonVertexList);
    glc_deferred(GLC_DEFER_AA);
    fan(nverts, NULL, vlist);
}

/* --- The utility library's clipped draws ---------------------------------- */

/* Each parameter of a Glide 2 vertex, as floats, for the clipper. */
#define VFLOATS ((int) (sizeof(GrVertex) / sizeof(float)))

static void
lerp(GrVertex *o, const GrVertex *a, const GrVertex *b, float t)
{
    const float *pa = (const float *) a;
    const float *pb = (const float *) b;
    float       *po = (float *) o;
    int          i;

    for (i = 0; i < VFLOATS; i++)
        po[i] = pa[i] + (pb[i] - pa[i]) * t;
}

/* Room for a clipped polygon: each edge adds at most one vertex to a convex
   one, and doubles a twisted one at worst. */
#define CLIP_MAX 256

/* One edge of the clip window: keeps the side where the coordinate (0 x,
   1 y) is at least lo (keep_ge) or below hi. -1 when the polygon would
   pass CLIP_MAX vertices (a twisted one: dropped). */
static int
clip_edge(const GrVertex *in, int n, GrVertex *out, int axis, float limit, int keep_ge)
{
    int i, m = 0;

    if (n < 0)
        return -1;
    for (i = 0; i < n; i++) {
        const GrVertex *a  = &in[i];
        const GrVertex *b  = &in[(i + 1) % n];
        const float     ca = axis ? a->y : a->x;
        const float     cb = axis ? b->y : b->x;
        const int       ia = keep_ge ? (ca >= limit) : (ca < limit);
        const int       ib = keep_ge ? (cb >= limit) : (cb < limit);

        if (m + 2 > CLIP_MAX)
            return -1;
        if (ia)
            out[m++] = *a;
        if (ia != ib)
            lerp(&out[m++], a, b, (limit - ca) / (cb - ca));
    }
    return m;
}

/* Sutherland-Hodgman against the clip window (right, bottom, left, top),
   each parameter interpolated in screen space, then snapped and drawn as a
   fan, as guDrawTriangleWithClip does. */
static void
clip_poly(int n, const GrVertex *vlist, int aa)
{
    static GrVertex a[CLIP_MAX], b[CLIP_MAX];
    const DWORD     mn = glc_reg_get(VGPU98_GS_CLIP_MIN), mx = glc_reg_get(VGPU98_GS_CLIP_MAX);
    const float     x0 = (float) (mn & 0xffff), y0 = (float) (mn >> 16);
    const float     x1 = (float) (mx & 0xffff), y1 = (float) (mx >> 16);
    int             i, inside = 1;

    if ((n < 3) || (n > 100) || !vlist)
        return;
    for (i = 0; i < n; i++)
        inside &= (vlist[i].x >= x0) && (vlist[i].x < x1) && (vlist[i].y >= y0) && (vlist[i].y < y1);
    if (inside) {
        if (aa)
            glc_deferred(GLC_DEFER_AA);
        fan(n, NULL, vlist);
        return;
    }
    for (i = 0; i < n; i++)
        a[i] = vlist[i];
    n = clip_edge(a, n, b, 0, x1, 0);
    n = clip_edge(b, n, a, 1, y1, 0);
    n = clip_edge(a, n, b, 0, x0, 1);
    n = clip_edge(b, n, a, 1, y0, 1);
    if (n < 3)
        return;
    for (i = 0; i < n; i++) {
        /* The 1/16-pixel snap, as the utility library adds and takes 2^19. */
        volatile float sx = a[i].x + 524288.0f, sy = a[i].y + 524288.0f;

        a[i].x = sx - 524288.0f;
        a[i].y = sy - 524288.0f;
    }
    if (aa)
        glc_deferred(GLC_DEFER_AA);
    fan(n, NULL, a);
}

void FX_CALL
guDrawTriangleWithClip(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    GrVertex v[3];

    CALLED(guDrawTriangleWithClip);
    if (!a || !b || !c)
        return;
    v[0] = *a;
    v[1] = *b;
    v[2] = *c;
    clip_poly(3, v, 0);
}

void FX_CALL
guAADrawTriangleWithClip(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    GrVertex v[3];

    CALLED(guAADrawTriangleWithClip);
    if (!a || !b || !c)
        return;
    v[0] = *a;
    v[1] = *b;
    v[2] = *c;
    clip_poly(3, v, 1);
}

void FX_CALL
guDrawPolygonVertexListWithClip(int nverts, const GrVertex *vlist)
{
    CALLED(guDrawPolygonVertexListWithClip);
    clip_poly(nverts, vlist, 0);
}

/* --- The utility library's multipass helpers ----------------------------- */

/* A Voodoo3's library draws these in two passes on one TMU, blending the
   second onto the first: here one pass with the first texture (said once). */
static GrMipMapId_t mp_mmid[2] = { (GrMipMapId_t) -1, (GrMipMapId_t) -1 };
static int          mp_fnc;

void FX_CALL
guMPInit(void)
{
    CALLED(guMPInit);
    mp_mmid[0] = mp_mmid[1] = (GrMipMapId_t) -1;
    mp_fnc                  = 0; /* GR_MPTEXTURECOMBINE_ADD */
}

void FX_CALL
guMPTexCombineFunction(GrMPTextureCombineFnc_t tc)
{
    CALLED(guMPTexCombineFunction);
    mp_fnc = tc;
    (void) mp_fnc;
}

void FX_CALL
guMPTexSource(GrChipID_t virtual_tmu, GrMipMapId_t mmid)
{
    CALLED(guMPTexSource);
    if ((virtual_tmu == 0) || (virtual_tmu == 1))
        mp_mmid[virtual_tmu] = mmid;
}

void FX_CALL
guMPDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    glc_vtx_t va, vb, vc;

    CALLED(guMPDrawTriangle);
    glc_deferred(GLC_DEFER_MULTIPASS);
    if (!a || !b || !c)
        return;
    glide2x_tex_source(mp_mmid[0]);
    read_vertex(&va, a);
    read_vertex(&vb, b);
    read_vertex(&vc, c);
    glc_triangle(&va, &vb, &vc);
}

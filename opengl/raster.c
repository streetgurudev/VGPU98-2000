/*
 * The ICD's own vertex path: GL 1.1's
 * per-vertex operations in C (transform, lighting, texture coordinate
 * generation and the texture matrix, clipping, the viewport), and what
 * needs them at once: glRasterPos, and the selection and feedback render
 * modes (GL 1.1, sections 2.12-2.13 and 5.2-5.3).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "glmath.h"
#include "lists.h"
#include "prim.h"
#include "raster.h"
#include "regs.h"

/* --- Lighting ---------------------------------------------------------------------------- */

static float
dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void
normalize3(float *v)
{
    const double l = ogl_sqrt((double) dot3(v, v));

    if (l > 0.0) {
        v[0] = (float) (v[0] / l);
        v[1] = (float) (v[1] / l);
        v[2] = (float) (v[2] / l);
    }
}

/* GL 1.1's lighting equation for one face (section 2.13.1), the material
   given (colour material applied), n the eye normal facing the light side,
   eye the vertex in eye coordinates. */
static void
light_face(const ogl_state_t *s, const ogl_material_t *m, const float *n, const float *eye, float *out)
{
    const ogl_lighting_t *lt = &s->lighting;
    float                 rgb[3];
    int                   i, k;

    for (k = 0; k < 3; k++)
        rgb[k] = m->emission[k] + lt->model_ambient[k] * m->ambient[k];
    for (i = 0; i < OGL_MAX_LIGHTS; i++) {
        const ogl_light_t *l = &lt->light[i];
        float              vp[3], h[3];
        double             att = 1.0, spot = 1.0, ndotvp;

        if (!(s->enables & OGL_E_LIGHT(i)))
            continue;
        /* VP: towards the light, and the attenuation of a positional one. */
        if (l->position[3] != 0.0f) {
            double d;

            for (k = 0; k < 3; k++)
                vp[k] = l->position[k] / l->position[3] - eye[k] / ((eye[3] != 0.0f) ? eye[3] : 1.0f);
            d = ogl_sqrt((double) dot3(vp, vp));
            if (d > 0.0) {
                for (k = 0; k < 3; k++)
                    vp[k] = (float) (vp[k] / d);
            }
            att = (double) l->attenuation[0] + (double) l->attenuation[1] * d + (double) l->attenuation[2] * d * d;
            att = (att > 0.0) ? 1.0 / att : 1.0;
        } else {
            memcpy(vp, l->position, sizeof(vp));
            normalize3(vp);
        }
        /* The spotlight: (the cosine to its axis)^exponent inside its cone. */
        if (l->spot_cutoff != 180.0f) {
            float  dir[3];
            double cosv;

            memcpy(dir, l->spot_dir, sizeof(dir));
            normalize3(dir);
            cosv = -(double) dot3(vp, dir);
            if (cosv >= ogl_cos((double) l->spot_cutoff * 3.14159265358979323846 / 180.0))
                spot = ogl_pow((cosv > 0.0) ? cosv : 0.0, l->spot_exponent);
            else
                spot = 0.0;
        }
        ndotvp = dot3(n, vp);
        for (k = 0; k < 3; k++) {
            double term = (double) l->ambient[k] * m->ambient[k];

            if (ndotvp > 0.0) {
                double ndoth;

                term += ndotvp * l->diffuse[k] * m->diffuse[k];
                /* The half vector: VP plus the direction to the eye (0, 0, 1
                   without a local viewer). */
                if (lt->local_viewer) {
                    float pe[3];

                    pe[0] = -eye[0];
                    pe[1] = -eye[1];
                    pe[2] = -eye[2];
                    normalize3(pe);
                    h[0] = vp[0] + pe[0];
                    h[1] = vp[1] + pe[1];
                    h[2] = vp[2] + pe[2];
                } else {
                    h[0] = vp[0];
                    h[1] = vp[1];
                    h[2] = vp[2] + 1.0f;
                }
                normalize3(h);
                ndoth = dot3(n, h);
                if (ndoth > 0.0)
                    term += ogl_pow(ndoth, m->shininess) * l->specular[k] * m->specular[k];
            }
            rgb[k] += (float) (att * spot * term);
        }
    }
    for (k = 0; k < 3; k++)
        out[k] = ogl_clampf(rgb[k], 0.0f, 1.0f);
    out[3] = ogl_clampf(m->diffuse[3], 0.0f, 1.0f);
}

/* --- One vertex ------------------------------------------------------------------------------ */

void
ogl_cpu_vertex(const ogl_state_t *s, const ogl_vtx_t *in, const ogl_material_t *mats, ogl_cvtx_t *out)
{
    const float *mv = s->modelview[s->modelview_depth - 1].m;
    const float *pr = s->projection[s->projection_depth - 1].m;
    float        n[3];
    int          k;

    ogl_mat_vec(out->eye, mv, in->obj);
    ogl_mat_vec(out->clip, pr, out->eye);
    out->edge = in->edge;
    out->fog  = ogl_finite(out->eye[2]) ? (float) ogl_fabs(out->eye[2]) : 0.0f;
    /* The normal through the modelview's inverse transpose. */
    for (k = 0; k < 3; k++)
        n[k] = s->normal[k] * in->normal[0] + s->normal[3 + k] * in->normal[1] + s->normal[6 + k] * in->normal[2];
    if (s->enables & OGL_E_NORMALIZE)
        normalize3(n);
    if (s->enables & OGL_E_LIGHTING) {
        ogl_material_t m[2];
        float          nb[3];
        int            f;

        memcpy(m, mats, sizeof(m));
        if (s->enables & OGL_E_COLOR_MATERIAL) {
            for (f = 0; f < 2; f++) {
                if ((s->lighting.cm_face == GL_FRONT_AND_BACK) || (s->lighting.cm_face == (f ? GL_BACK : GL_FRONT)))
                    ogl_material_set(&m[f], s->lighting.cm_mode, in->color);
            }
        }
        light_face(s, &m[0], n, out->eye, out->front);
        nb[0] = -n[0];
        nb[1] = -n[1];
        nb[2] = -n[2];
        if (s->lighting.two_side)
            light_face(s, &m[1], nb, out->eye, out->back);
        else
            memcpy(out->back, out->front, sizeof(out->back));
    } else {
        for (k = 0; k < 4; k++)
            out->front[k] = out->back[k] = ogl_clampf(in->color[k], 0.0f, 1.0f);
    }
    /* Each unit's texture coordinates: generated where enabled, then the
       unit's matrix. */
    for (int u = 0; u < OGL_UNITS; u++) {
        const ogl_texunit_t *tu = &s->texture.unit[u];
        float                t[4];

        memcpy(t, in->tex[u], sizeof(t));
        for (k = 0; k < 4; k++) {
            if (!(tu->en & (OGL_TE_GEN_S << k)))
                continue;
            switch (tu->gen_mode[k]) {
                case GL_OBJECT_LINEAR: {
                    const double *p = tu->obj_plane[k];

                    t[k] = (float) (p[0] * in->obj[0] + p[1] * in->obj[1] + p[2] * in->obj[2] + p[3] * in->obj[3]);
                    break;
                }
                case GL_EYE_LINEAR: {
                    const double *p = tu->eye_plane[k];

                    t[k] = (float) (p[0] * out->eye[0] + p[1] * out->eye[1] + p[2] * out->eye[2] + p[3] * out->eye[3]);
                    break;
                }
                default: { /* GL_SPHERE_MAP: s and t only */
                    float  u[3], r[3];
                    double mm;

                    memcpy(u, out->eye, sizeof(u));
                    normalize3(u);
                    for (int j = 0; j < 3; j++)
                        r[j] = u[j] - 2.0f * n[j] * dot3(n, u);
                    mm = 2.0 * ogl_sqrt((double) r[0] * r[0] + (double) r[1] * r[1] + ((double) r[2] + 1.0) * (r[2] + 1.0));
                    if (mm > 0.0)
                        t[k] = (float) (r[k] / mm + 0.5);
                    break;
                }
            }
        }
        ogl_mat_vec(out->tex[u], s->texmatrix[u][s->texture_depth[u] - 1].m, t);
    }
}

/* --- Clipping --------------------------------------------------------------------------------- */

/* A vertex's distance to clip plane p (0-5 the view volume's: x, y, z
   against -w and w; 6-11 the user's), at or above 0 inside. */
static double
plane_dist(const ogl_state_t *s, const ogl_cvtx_t *v, int p)
{
    if (p < 6) {
        const double w = v->clip[3];
        const double a = v->clip[p >> 1];

        return (p & 1) ? w - a : w + a;
    }
    {
        const double *e = s->transform.clip_plane[p - 6];

        return e[0] * v->eye[0] + e[1] * v->eye[1] + e[2] * v->eye[2] + e[3] * v->eye[3];
    }
}

static int
plane_on(const ogl_state_t *s, int p)
{
    return (p < 6) || (s->enables & OGL_E_CLIP(p - 6));
}

static void
lerp(ogl_cvtx_t *out, const ogl_cvtx_t *a, const ogl_cvtx_t *b, float t)
{
    int k;

    for (k = 0; k < 4; k++) {
        int u;

        out->eye[k]   = a->eye[k] + (b->eye[k] - a->eye[k]) * t;
        out->clip[k]  = a->clip[k] + (b->clip[k] - a->clip[k]) * t;
        out->front[k] = a->front[k] + (b->front[k] - a->front[k]) * t;
        out->back[k]  = a->back[k] + (b->back[k] - a->back[k]) * t;
        for (u = 0; u < OGL_UNITS; u++)
            out->tex[u][k] = a->tex[u][k] + (b->tex[u][k] - a->tex[u][k]) * t;
    }
    out->fog = a->fog + (b->fog - a->fog) * t;
}

int
ogl_clip_point(const ogl_state_t *s, const ogl_cvtx_t *v)
{
    int p;

    for (p = 0; p < 12; p++) {
        if (plane_on(s, p) && (plane_dist(s, v, p) < 0.0))
            return 0;
    }
    return 1;
}

int
ogl_clip_line(const ogl_state_t *s, ogl_cvtx_t *a, ogl_cvtx_t *b)
{
    int p;

    for (p = 0; p < 12; p++) {
        double da, db;

        if (!plane_on(s, p))
            continue;
        da = plane_dist(s, a, p);
        db = plane_dist(s, b, p);
        if ((da < 0.0) && (db < 0.0))
            return 0;
        if (da < 0.0) {
            ogl_cvtx_t n;

            lerp(&n, a, b, (float) (da / (da - db)));
            n.edge = a->edge;
            *a     = n;
        } else if (db < 0.0) {
            ogl_cvtx_t n;

            lerp(&n, a, b, (float) (da / (da - db)));
            n.edge = b->edge;
            *b     = n;
        }
    }
    return 1;
}

/* Sutherland-Hodgman against every plane. An edge's flag goes with the
   vertex that starts it; an edge the clipping made (along a plane) is a
   boundary of the clipped polygon with its flag set. Returns the count
   (in out, at most OGL_CLIP_MAX: a concave polygon can grow by half at
   each plane; what does not fit is dropped). */
int
ogl_clip_polygon(const ogl_state_t *s, const ogl_cvtx_t *in, int n, ogl_cvtx_t *out)
{
    ogl_cvtx_t       *buf[2] = { s->clip_buf, s->clip_buf + OGL_CLIP_MAX }; /* the context's (emit_polygon made it) */
    const ogl_cvtx_t *src = in;
    int               cnt = n, p, i, which = 0;

    if (n > OGL_CLIP_MAX - 12)
        n = cnt = OGL_CLIP_MAX - 12;
    for (p = 0; p < 12; p++) {
        ogl_cvtx_t *dst = buf[which];
        int         m   = 0;

        if (!plane_on(s, p))
            continue;
        for (i = 0; i < cnt; i++) {
            const ogl_cvtx_t *a  = &src[i];
            const ogl_cvtx_t *b  = &src[(i + 1) % cnt];
            const double      da = plane_dist(s, a, p);
            const double      db = plane_dist(s, b, p);

            if ((da >= 0.0) && (m < OGL_CLIP_MAX))
                dst[m++] = *a;
            if (((da >= 0.0) != (db >= 0.0)) && (m < OGL_CLIP_MAX)) {
                ogl_cvtx_t *x = &dst[m++];

                lerp(x, a, b, (float) (da / (da - db)));
                /* Entering: the new vertex starts the rest of a's edge (a's
                   flag); leaving: it starts an edge along the plane. */
                x->edge = (da >= 0.0) ? 1 : a->edge;
            }
        }
        src   = dst;
        cnt   = m;
        which = !which;
        if (!cnt)
            return 0;
    }
    memcpy(out, src, (size_t) cnt * sizeof(ogl_cvtx_t));
    return cnt;
}

void
ogl_window(const ogl_state_t *s, const ogl_cvtx_t *v, float *win)
{
    const double w = (v->clip[3] != 0.0f) ? v->clip[3] : 1.0;
    const double n = s->viewport.near_val, f = s->viewport.far_val;

    win[0] = (float) ((v->clip[0] / w + 1.0) * s->viewport.w / 2.0 + s->viewport.x);
    win[1] = (float) ((v->clip[1] / w + 1.0) * s->viewport.h / 2.0 + s->viewport.y);
    win[2] = (float) (v->clip[2] / w * (f - n) / 2.0 + (n + f) / 2.0);
}

/* --- Selection --------------------------------------------------------------------------------- */

static void
select_word(ogl_state_t *s, GLuint v)
{
    if (s->select_count < (GLuint) s->select_size)
        s->select_buffer[s->select_count] = v;
    else
        s->select_overflow = 1;
    s->select_count++;
}

/* A hit's depth: window z (0-1) scaled to the unsigned integers. */
static GLuint
hit_z(float z)
{
    const double d = (double) ogl_clampf(z, 0.0f, 1.0f) * 4294967295.0;

    return (GLuint) ((d >= 4294967295.0) ? 4294967295.0 : d + 0.5);
}

void
ogl_select_hit(ogl_state_t *s, float z)
{
    if (!s->select_hit) {
        s->select_hit = 1;
        s->select_min = z;
        s->select_max = z;
    } else {
        if (z < s->select_min)
            s->select_min = z;
        if (z > s->select_max)
            s->select_max = z;
    }
}

/* The hit since the name stack last changed, as a record. */
static void
select_flush(ogl_state_t *s)
{
    int i;

    if (!s->select_hit)
        return;
    if (!s->select_overflow) {
        select_word(s, (GLuint) s->name_depth);
        select_word(s, hit_z(s->select_min));
        select_word(s, hit_z(s->select_max));
        for (i = 0; i < s->name_depth; i++)
            select_word(s, s->name_stack[i]);
        if (!s->select_overflow)
            s->select_hits++;
    }
    s->select_hit = 0;
}

/* --- Feedback --------------------------------------------------------------------------------- */

static void
fb_word(ogl_state_t *s, float v)
{
    if (s->feedback_count < (GLuint) s->feedback_size)
        s->feedback_buffer[s->feedback_count] = v;
    else
        s->feedback_overflow = 1;
    s->feedback_count++;
}

/* A vertex's values by the feedback type: window x, y (z), clip w, then the
   colour and texture coordinates. */
static void
fb_vertex(ogl_state_t *s, const ogl_cvtx_t *v, const float *color)
{
    float win[3];
    int   k;

    ogl_window(s, v, win);
    fb_word(s, win[0]);
    fb_word(s, win[1]);
    if (s->feedback_type != GL_2D)
        fb_word(s, win[2]);
    if (s->feedback_type == GL_4D_COLOR_TEXTURE)
        fb_word(s, v->clip[3]);
    if ((s->feedback_type == GL_3D_COLOR) || (s->feedback_type == GL_3D_COLOR_TEXTURE) ||
        (s->feedback_type == GL_4D_COLOR_TEXTURE)) {
        for (k = 0; k < 4; k++)
            fb_word(s, color[k]);
    }
    if ((s->feedback_type == GL_3D_COLOR_TEXTURE) || (s->feedback_type == GL_4D_COLOR_TEXTURE)) {
        for (k = 0; k < 4; k++)
            fb_word(s, v->tex[0][k]); /* unit 0's */
    }
}

/* --- Primitives through the ICD's path ------------------------------------------------------------ */

/* Each goes to the render mode's sink: selection's hits, feedback's tokens,
   or (GL_RENDER) the device's DRAW, clipped (draw.c). */

static void
emit_point(ogl_ctx_t *c, const ogl_cvtx_t *v, const float *color, int polygon)
{
    ogl_state_t *s = c->gl;
    float        win[3];

    if (!ogl_clip_point(s, v))
        return;
    if (s->render_mode == GL_RENDER)
        ogl_draw_cpu_point(c, v, color, polygon);
    else if (s->render_mode == GL_FEEDBACK) {
        fb_word(s, (float) GL_POINT_TOKEN);
        fb_vertex(s, v, color);
    } else {
        ogl_window(s, v, win);
        ogl_select_hit(s, win[2]);
    }
}

static void
emit_line(ogl_ctx_t *c, ogl_cvtx_t a, ogl_cvtx_t b, const float *ca, const float *cb, int reset, int polygon)
{
    ogl_state_t *s = c->gl;
    float        win[3];

    /* The colours go through the clip with their vertices. */
    memcpy(a.front, ca, sizeof(a.front));
    memcpy(b.front, cb, sizeof(b.front));
    if (!ogl_clip_line(s, &a, &b))
        return;
    if (s->render_mode == GL_RENDER)
        ogl_draw_cpu_line(c, &a, &b, reset, polygon);
    else if (s->render_mode == GL_FEEDBACK) {
        fb_word(s, (float) (reset ? GL_LINE_RESET_TOKEN : GL_LINE_TOKEN));
        fb_vertex(s, &a, a.front);
        fb_vertex(s, &b, b.front);
    } else {
        ogl_window(s, &a, win);
        ogl_select_hit(s, win[2]);
        ogl_window(s, &b, win);
        ogl_select_hit(s, win[2]);
    }
}

/* A polygon (n vertices, i its provoking one for flat shading): culled by
   its facing in window coordinates, drawn by its face's polygon mode. */
static void
emit_polygon(ogl_ctx_t *c, const ogl_cvtx_t *v, int n, int provoking)
{
    ogl_state_t      *s = c->gl;
    ogl_cvtx_t       *cl;
    float             win[OGL_CLIP_MAX][3];
    double            area = 0.0;
    int               m, i, back, first;
    GLenum            mode;
    const int         flat = s->lighting.shade_model == GL_FLAT;

    if (n < 3)
        return;
    if (!s->clip_buf) {
        s->clip_buf = ogl_alloc(3 * OGL_CLIP_MAX * sizeof(ogl_cvtx_t));
        if (!s->clip_buf) {
            ogl_error(c, GL_OUT_OF_MEMORY);
            return;
        }
    }
    cl = s->clip_buf + 2 * OGL_CLIP_MAX;
    m  = ogl_clip_polygon(s, v, n, cl);
    if (m < 3)
        return;
    for (i = 0; i < m; i++)
        ogl_window(s, &cl[i], win[i]);
    for (i = 0; i < m; i++) {
        const int j = (i + 1) % m;

        area += (double) win[i][0] * win[j][1] - (double) win[j][0] * win[i][1];
    }
    /* Counter-clockwise (area above 0) is front when FrontFace is GL_CCW. */
    back = (s->raster.front_face == GL_CCW) ? (area < 0.0) : (area > 0.0);
    if ((s->enables & OGL_E_CULL_FACE) &&
        ((s->raster.cull_mode == GL_FRONT_AND_BACK) || (s->raster.cull_mode == (back ? GL_BACK : GL_FRONT))))
        return;
    /* Flat shading: every vertex the provoking one's colour; the face's
       colour in front. */
    for (i = 0; i < m; i++) {
        float *col = back ? cl[i].back : cl[i].front;

        if (flat)
            memcpy(col, back ? v[provoking].back : v[provoking].front, 4 * sizeof(float));
        if (back)
            memcpy(cl[i].front, col, 4 * sizeof(float));
    }
    mode = s->raster.polygon_mode[back];
    if (mode == GL_POINT) {
        for (i = 0; i < m; i++) {
            if (cl[i].edge)
                emit_point(c, &cl[i], cl[i].front, 1);
        }
        return;
    }
    if (mode == GL_LINE) {
        /* The edges flagged as the boundary's; the stipple counter starts
           again at the polygon's first, and goes on along the others. */
        for (i = 0, first = 1; i < m; i++) {
            if (cl[i].edge) {
                emit_line(c, cl[i], cl[(i + 1) % m], cl[i].front, cl[(i + 1) % m].front, first, 1);
                first = 0;
            }
        }
        return;
    }
    if (s->render_mode == GL_RENDER)
        ogl_draw_cpu_polygon(c, cl, m);
    else if (s->render_mode == GL_FEEDBACK) {
        fb_word(s, (float) GL_POLYGON_TOKEN);
        fb_word(s, (float) m);
        for (i = 0; i < m; i++)
            fb_vertex(s, &cl[i], cl[i].front);
    } else {
        for (i = 0; i < m; i++)
            ogl_select_hit(s, win[i][2]);
    }
}

void
ogl_cpu_prim(ogl_ctx_t *c, const ogl_prim_t *p)
{
    ogl_state_t     *s = c->gl;
    ogl_cvtx_t      *v;
    const int        flat = s->lighting.shade_model == GL_FLAT;
    DWORD            i, n = p->n;

    if (!n)
        return;
    ogl_normal_matrix(s);
    v = ogl_alloc(n * sizeof(ogl_cvtx_t));
    if (!v) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    for (i = 0; i < n; i++)
        ogl_cpu_vertex(s, &p->v[i], p->mats[p->v[i].mat], &v[i]);
    switch (p->mode) {
        case GL_POINTS:
            for (i = 0; i < n; i++)
                emit_point(c, &v[i], v[i].front, 0);
            break;
        case GL_LINES:
            for (i = 0; i + 1 < n; i += 2)
                emit_line(c, v[i], v[i + 1], flat ? v[i + 1].front : v[i].front, v[i + 1].front, 1, 0);
            break;
        case GL_LINE_STRIP:
        case GL_LINE_LOOP:
            for (i = 0; i + 1 < n; i++)
                emit_line(c, v[i], v[i + 1], flat ? v[i + 1].front : v[i].front, v[i + 1].front, i == 0, 0);
            if ((p->mode == GL_LINE_LOOP) && (n > 1))
                emit_line(c, v[n - 1], v[0], flat ? v[0].front : v[n - 1].front, v[0].front, 0, 0);
            break;
        case GL_TRIANGLES:
            for (i = 0; i + 2 < n; i += 3)
                emit_polygon(c, &v[i], 3, 2);
            break;
        case GL_TRIANGLE_STRIP:
            for (i = 0; i + 2 < n; i++) {
                ogl_cvtx_t t[3];

                /* Every other triangle's first two swapped, so all keep the
                   strip's winding; the provoking vertex is i + 2. */
                t[0] = v[(i & 1) ? i + 1 : i];
                t[1] = v[(i & 1) ? i : i + 1];
                t[2] = v[i + 2];
                emit_polygon(c, t, 3, 2);
            }
            break;
        case GL_TRIANGLE_FAN:
            for (i = 1; i + 1 < n; i++) {
                ogl_cvtx_t t[3];

                t[0] = v[0];
                t[1] = v[i];
                t[2] = v[i + 1];
                emit_polygon(c, t, 3, 2);
            }
            break;
        case GL_QUADS:
            for (i = 0; i + 3 < n; i += 4)
                emit_polygon(c, &v[i], 4, 3);
            break;
        case GL_QUAD_STRIP:
            for (i = 0; i + 3 < n; i += 2) {
                ogl_cvtx_t q[4];

                /* Vertices 2j, 2j+1, 2j+3, 2j+2 make quad j (GL 1.1, Figure
                   2.4); its provoking vertex is 2j+3. */
                q[0] = v[i];
                q[1] = v[i + 1];
                q[2] = v[i + 3];
                q[3] = v[i + 2];
                emit_polygon(c, q, 4, 2);
            }
            break;
        default: /* GL_POLYGON: its first vertex provokes */
            emit_polygon(c, v, (int) ((n < OGL_CLIP_MAX - 12) ? n : OGL_CLIP_MAX - 12), 0);
            break;
    }
    ogl_free(v);
}

/* --- The raster position ------------------------------------------------------------------------ */

void STDCALL
ogl_RasterPos4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    ogl_state_t *s;
    ogl_vtx_t    in;
    ogl_cvtx_t   v;
    float        win[3];
    OGL_CTX();

    OGL_DL(OP_RASTERPOS, ogl_fw(x), ogl_fw(y), ogl_fw(z), ogl_fw(w));
    if (ogl_inside(c))
        return;
    s = c->gl;
    ogl_normal_matrix(s);
    memset(&in, 0, sizeof(in));
    in.obj[0] = x;
    in.obj[1] = y;
    in.obj[2] = z;
    in.obj[3] = w;
    memcpy(in.color, s->current.color, sizeof(in.color));
    memcpy(in.normal, s->current.normal, sizeof(in.normal));
    memcpy(in.tex, s->current.texcoord, sizeof(in.tex));
    in.index = s->current.index;
    in.edge  = 1;
    ogl_cpu_vertex(s, &in, s->lighting.material, &v);
    /* Valid when the point is inside the view volume and the user's planes
       (the point clip). */
    if (!ogl_clip_point(s, &v)) {
        s->current.raster_valid = GL_FALSE;
        return;
    }
    ogl_window(s, &v, win);
    /* A valid raster position is a hit (GL 1.1, 5.2). */
    if (s->render_mode == GL_SELECT)
        ogl_select_hit(s, win[2]);
    s->current.raster_valid    = GL_TRUE;
    s->current.raster_pos[0]   = win[0];
    s->current.raster_pos[1]   = win[1];
    s->current.raster_pos[2]   = win[2];
    s->current.raster_pos[3]   = v.clip[3];
    s->current.raster_distance = (float) ogl_sqrt((double) v.eye[0] * v.eye[0] + (double) v.eye[1] * v.eye[1] +
                                                  (double) v.eye[2] * v.eye[2]);
    memcpy(s->current.raster_color, v.front, sizeof(v.front));
    s->current.raster_index = s->current.index;
    memcpy(s->current.raster_texcoord, v.tex, sizeof(v.tex));
}

void STDCALL ogl_RasterPos2d(GLdouble x, GLdouble y) { ogl_RasterPos4f((float) x, (float) y, 0.0f, 1.0f); }
void STDCALL ogl_RasterPos2dv(const GLdouble *v) { ogl_RasterPos4f((float) v[0], (float) v[1], 0.0f, 1.0f); }
void STDCALL ogl_RasterPos2f(GLfloat x, GLfloat y) { ogl_RasterPos4f(x, y, 0.0f, 1.0f); }
void STDCALL ogl_RasterPos2fv(const GLfloat *v) { ogl_RasterPos4f(v[0], v[1], 0.0f, 1.0f); }
void STDCALL ogl_RasterPos2i(GLint x, GLint y) { ogl_RasterPos4f((float) x, (float) y, 0.0f, 1.0f); }
void STDCALL ogl_RasterPos2iv(const GLint *v) { ogl_RasterPos4f((float) v[0], (float) v[1], 0.0f, 1.0f); }
void STDCALL ogl_RasterPos2s(GLshort x, GLshort y) { ogl_RasterPos4f((float) x, (float) y, 0.0f, 1.0f); }
void STDCALL ogl_RasterPos2sv(const GLshort *v) { ogl_RasterPos4f((float) v[0], (float) v[1], 0.0f, 1.0f); }
void STDCALL ogl_RasterPos3d(GLdouble x, GLdouble y, GLdouble z) { ogl_RasterPos4f((float) x, (float) y, (float) z, 1.0f); }
void STDCALL ogl_RasterPos3dv(const GLdouble *v) { ogl_RasterPos4f((float) v[0], (float) v[1], (float) v[2], 1.0f); }
void STDCALL ogl_RasterPos3f(GLfloat x, GLfloat y, GLfloat z) { ogl_RasterPos4f(x, y, z, 1.0f); }
void STDCALL ogl_RasterPos3fv(const GLfloat *v) { ogl_RasterPos4f(v[0], v[1], v[2], 1.0f); }
void STDCALL ogl_RasterPos3i(GLint x, GLint y, GLint z) { ogl_RasterPos4f((float) x, (float) y, (float) z, 1.0f); }
void STDCALL ogl_RasterPos3iv(const GLint *v) { ogl_RasterPos4f((float) v[0], (float) v[1], (float) v[2], 1.0f); }
void STDCALL ogl_RasterPos3s(GLshort x, GLshort y, GLshort z) { ogl_RasterPos4f((float) x, (float) y, (float) z, 1.0f); }
void STDCALL ogl_RasterPos3sv(const GLshort *v) { ogl_RasterPos4f((float) v[0], (float) v[1], (float) v[2], 1.0f); }
void STDCALL ogl_RasterPos4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w) { ogl_RasterPos4f((float) x, (float) y, (float) z, (float) w); }
void STDCALL ogl_RasterPos4dv(const GLdouble *v) { ogl_RasterPos4f((float) v[0], (float) v[1], (float) v[2], (float) v[3]); }
void STDCALL ogl_RasterPos4fv(const GLfloat *v) { ogl_RasterPos4f(v[0], v[1], v[2], v[3]); }
void STDCALL ogl_RasterPos4i(GLint x, GLint y, GLint z, GLint w) { ogl_RasterPos4f((float) x, (float) y, (float) z, (float) w); }
void STDCALL ogl_RasterPos4iv(const GLint *v) { ogl_RasterPos4f((float) v[0], (float) v[1], (float) v[2], (float) v[3]); }
void STDCALL ogl_RasterPos4s(GLshort x, GLshort y, GLshort z, GLshort w) { ogl_RasterPos4f((float) x, (float) y, (float) z, (float) w); }
void STDCALL ogl_RasterPos4sv(const GLshort *v) { ogl_RasterPos4f((float) v[0], (float) v[1], (float) v[2], (float) v[3]); }

/* A pixel rectangle's or bitmap's token and hit at the raster position
   (glDrawPixels, glCopyPixels, glBitmap in the render modes). */
void
ogl_raster_token(ogl_ctx_t *c, GLenum token)
{
    ogl_state_t *s = c->gl;
    ogl_cvtx_t   v;

    if (!s->current.raster_valid)
        return;
    /* Pixel rectangles are hits; bitmaps are not (GL 1.1, appendix B). */
    if (s->render_mode == GL_SELECT) {
        if (token != GL_BITMAP_TOKEN)
            ogl_select_hit(s, s->current.raster_pos[2]);
        return;
    }
    if (s->render_mode != GL_FEEDBACK)
        return;
    fb_word(s, (float) token);
    /* The raster position's own values (window coordinates already). */
    fb_word(s, s->current.raster_pos[0]);
    fb_word(s, s->current.raster_pos[1]);
    if (s->feedback_type != GL_2D)
        fb_word(s, s->current.raster_pos[2]);
    if (s->feedback_type == GL_4D_COLOR_TEXTURE)
        fb_word(s, s->current.raster_pos[3]);
    memcpy(v.front, s->current.raster_color, sizeof(v.front));
    memcpy(v.tex, s->current.raster_texcoord, sizeof(v.tex));
    if ((s->feedback_type == GL_3D_COLOR) || (s->feedback_type == GL_3D_COLOR_TEXTURE) ||
        (s->feedback_type == GL_4D_COLOR_TEXTURE)) {
        int k;

        for (k = 0; k < 4; k++)
            fb_word(s, v.front[k]);
    }
    if ((s->feedback_type == GL_3D_COLOR_TEXTURE) || (s->feedback_type == GL_4D_COLOR_TEXTURE)) {
        int k;

        for (k = 0; k < 4; k++)
            fb_word(s, v.tex[0][k]);
    }
}

/* --- The render mode calls ----------------------------------------------------------------------- */

GLint STDCALL
ogl_RenderMode(GLenum mode)
{
    ogl_state_t *s;
    GLint        r = 0;
    OGL_CTX(0);

    if ((mode != GL_RENDER) && (mode != GL_SELECT) && (mode != GL_FEEDBACK)) {
        ogl_error(c, GL_INVALID_ENUM);
        return 0;
    }
    if (ogl_inside(c))
        return 0;
    s = c->gl;
    if ((mode == GL_SELECT) && !s->select_buffer) {
        ogl_error(c, GL_INVALID_OPERATION);
        return 0;
    }
    if ((mode == GL_FEEDBACK) && !s->feedback_buffer) {
        ogl_error(c, GL_INVALID_OPERATION);
        return 0;
    }
    /* The mode left: its count, -1 when its buffer overflowed. */
    if (s->render_mode == GL_SELECT) {
        select_flush(s);
        r = s->select_overflow ? -1 : (GLint) s->select_hits;
    } else if (s->render_mode == GL_FEEDBACK)
        r = s->feedback_overflow ? -1 : (GLint) s->feedback_count;
    s->render_mode = mode;
    if (mode == GL_SELECT) {
        s->select_count    = 0;
        s->select_hits     = 0;
        s->select_overflow = 0;
        s->select_hit      = 0;
        s->name_depth      = 0;
    } else if (mode == GL_FEEDBACK) {
        s->feedback_count    = 0;
        s->feedback_overflow = 0;
    }
    return r;
}

void STDCALL
ogl_SelectBuffer(GLsizei size, GLuint *buffer)
{
    OGL_CTX();
    if (size < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c))
        return;
    if (c->gl->render_mode == GL_SELECT) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    c->gl->select_buffer = buffer;
    c->gl->select_size   = size;
}

void STDCALL
ogl_FeedbackBuffer(GLsizei size, GLenum type, GLfloat *buffer)
{
    OGL_CTX();
    if ((type != GL_2D) && (type != GL_3D) && (type != GL_3D_COLOR) && (type != GL_3D_COLOR_TEXTURE) &&
        (type != GL_4D_COLOR_TEXTURE)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (size < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c))
        return;
    if (c->gl->render_mode == GL_FEEDBACK) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    c->gl->feedback_buffer = buffer;
    c->gl->feedback_size   = size;
    c->gl->feedback_type   = type;
}

void STDCALL
ogl_PassThrough(GLfloat token)
{
    OGL_CTX();
    OGL_DL(OP_PASSTHROUGH, ogl_fw(token));
    if (ogl_inside(c))
        return;
    if (c->gl->render_mode == GL_FEEDBACK) {
        fb_word(c->gl, (float) GL_PASS_THROUGH_TOKEN);
        fb_word(c->gl, token);
    }
}

/* The name stack: changed only in GL_SELECT mode (ignored otherwise), each
   change writing the hit since the last. */
void STDCALL
ogl_InitNames(void)
{
    OGL_CTX();
    OGL_DL0(OP_INITNAMES);
    if (ogl_inside(c) || (c->gl->render_mode != GL_SELECT))
        return;
    select_flush(c->gl);
    c->gl->name_depth = 0;
}

void STDCALL
ogl_LoadName(GLuint name)
{
    OGL_CTX();
    OGL_DL(OP_LOADNAME, name);
    if (ogl_inside(c) || (c->gl->render_mode != GL_SELECT))
        return;
    if (!c->gl->name_depth) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    select_flush(c->gl);
    c->gl->name_stack[c->gl->name_depth - 1] = name;
}

void STDCALL
ogl_PushName(GLuint name)
{
    OGL_CTX();
    OGL_DL(OP_PUSHNAME, name);
    if (ogl_inside(c) || (c->gl->render_mode != GL_SELECT))
        return;
    /* An error changes nothing: no record is written for it. */
    if (c->gl->name_depth >= OGL_NAME_DEPTH) {
        ogl_error(c, GL_STACK_OVERFLOW);
        return;
    }
    select_flush(c->gl);
    c->gl->name_stack[c->gl->name_depth++] = name;
}

void STDCALL
ogl_PopName(void)
{
    OGL_CTX();
    OGL_DL0(OP_POPNAME);
    if (ogl_inside(c) || (c->gl->render_mode != GL_SELECT))
        return;
    if (!c->gl->name_depth) {
        ogl_error(c, GL_STACK_UNDERFLOW);
        return;
    }
    select_flush(c->gl);
    c->gl->name_depth--;
}

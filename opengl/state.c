/*
 * GL 1.1's state for a new context, and its errors.
 * Every value is the one the specification starts with; the
 * viewport and scissor box take the window's size when the context is first
 * made current (main.c).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "lists.h"
#include "prim.h"
#include "regs.h"
#include "state.h"
#include "texobj.h"

/* glGetError's order: the flags are kept as bits, the lowest given first;
   one flag a kind, as GL allows. */
static const GLenum errors[] = {
    GL_INVALID_ENUM, GL_INVALID_VALUE, GL_INVALID_OPERATION, GL_STACK_OVERFLOW, GL_STACK_UNDERFLOW, GL_OUT_OF_MEMORY,
};

void
ogl_error_at(ogl_ctx_t *c, GLenum error, const char *func)
{
    unsigned i;

    if (diag_on)
        ogl_diag_error(func, error);
    for (i = 0; i < ARRAY_SIZE(errors); i++) {
        if (errors[i] == error) {
            c->gl->error_flags |= 1u << i;
            return;
        }
    }
}

GLenum STDCALL ogl_GetError(void);

GLenum STDCALL
ogl_GetError(void)
{
    ogl_ctx_t *c = ogl_current();
    unsigned   i;

    if (!c)
        return GL_NO_ERROR;
    /* Between glBegin and glEnd it is itself an error (and answers 0). */
    if (c->gl->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return 0;
    }
    for (i = 0; i < ARRAY_SIZE(errors); i++) {
        if (c->gl->error_flags & (1u << i)) {
            c->gl->error_flags &= ~(1u << i);
            return errors[i];
        }
    }
    return GL_NO_ERROR;
}

static void
set4(float *v, float a, float b, float c, float d)
{
    v[0] = a;
    v[1] = b;
    v[2] = c;
    v[3] = d;
}

static void
identity(ogl_matrix_t *m)
{
    int i;

    memset(m, 0, sizeof(*m));
    for (i = 0; i < 4; i++)
        m->m[5 * i] = 1.0f;
}

/* An evaluator map's default: order 1 over 0-1, its one point the target's
   default (GL_MAP1_COLOR_4 to GL_MAP1_VERTEX_4 in order). */
static const struct {
    int   k;
    float v[4];
} map_defaults[9] = {
    { 4, { 1.0f, 1.0f, 1.0f, 1.0f } }, /* COLOR_4 */
    { 1, { 1.0f } },                   /* INDEX */
    { 3, { 0.0f, 0.0f, 1.0f } },       /* NORMAL */
    { 1, { 0.0f } },                   /* TEXTURE_COORD_1 */
    { 2, { 0.0f, 0.0f } },             /* TEXTURE_COORD_2 */
    { 3, { 0.0f, 0.0f, 0.0f } },       /* TEXTURE_COORD_3 */
    { 4, { 0.0f, 0.0f, 0.0f, 1.0f } }, /* TEXTURE_COORD_4 */
    { 3, { 0.0f, 0.0f, 0.0f } },       /* VERTEX_3 */
    { 4, { 0.0f, 0.0f, 0.0f, 1.0f } }, /* VERTEX_4 */
};

int
ogl_map_components(int target_index)
{
    return ((target_index >= 0) && (target_index < 9)) ? map_defaults[target_index].k : 0;
}

static int
map_default(ogl_map_t *m, int i, int two)
{
    const int k = map_defaults[i].k;

    ogl_free(m->points);
    m->points = ogl_alloc((DWORD) k * sizeof(float));
    if (!m->points)
        return 0;
    memcpy(m->points, map_defaults[i].v, (size_t) k * sizeof(float));
    m->uorder = 1;
    m->vorder = two ? 1 : 0;
    m->u1     = 0.0f;
    m->u2     = 1.0f;
    m->v1     = 0.0f;
    m->v2     = 1.0f;
    return 1;
}

int
ogl_state_init(ogl_ctx_t *c)
{
    ogl_state_t *s;
    const int    dbl = (c->format & VGPU98_OGL_DOUBLE) != 0;
    int          i;

    s = c->gl = ogl_alloc(sizeof(ogl_state_t));
    if (!s)
        return 0;
    s->enables = OGL_E_DITHER;

    set4(s->current.color, 1.0f, 1.0f, 1.0f, 1.0f);
    s->current.index = 1.0f;
    for (i = 0; i < OGL_UNITS; i++) {
        set4(s->current.texcoord[i], 0.0f, 0.0f, 0.0f, 1.0f);
        set4(s->current.raster_texcoord[i], 0.0f, 0.0f, 0.0f, 1.0f);
    }
    s->current.normal[2] = 1.0f;
    set4(s->current.raster_pos, 0.0f, 0.0f, 0.0f, 1.0f);
    set4(s->current.raster_color, 1.0f, 1.0f, 1.0f, 1.0f);
    s->current.raster_index = 1.0f;
    s->current.raster_valid = GL_TRUE;
    s->current.edge_flag    = GL_TRUE;

    s->raster.point_size      = 1.0f;
    s->raster.point_max       = OGL_POINT_MAX;
    s->raster.point_fade      = 1.0f;
    s->raster.point_atten[0]  = 1.0f;
    s->raster.line_width      = 1.0f;
    s->raster.line_pattern    = 0xffff;
    s->raster.line_repeat     = 1;
    s->raster.cull_mode       = GL_BACK;
    s->raster.front_face      = GL_CCW;
    s->raster.polygon_mode[0] = GL_FILL;
    s->raster.polygon_mode[1] = GL_FILL;
    memset(s->stipple, 0xff, sizeof(s->stipple));

    for (i = 0; i < 4; i++)
        s->pixel_mode.scale[i] = 1.0f;
    s->pixel_mode.depth_scale = 1.0f;
    s->pixel_mode.zoom_x      = 1.0f;
    s->pixel_mode.zoom_y      = 1.0f;
    s->pixel_mode.read_buffer = dbl ? GL_BACK : GL_FRONT;

    s->lighting.shade_model = GL_SMOOTH;
    for (i = 0; i < 2; i++) {
        ogl_material_t *m = &s->lighting.material[i];

        set4(m->ambient, 0.2f, 0.2f, 0.2f, 1.0f);
        set4(m->diffuse, 0.8f, 0.8f, 0.8f, 1.0f);
        set4(m->specular, 0.0f, 0.0f, 0.0f, 1.0f);
        set4(m->emission, 0.0f, 0.0f, 0.0f, 1.0f);
        m->indexes[1] = 1.0f;
        m->indexes[2] = 1.0f;
    }
    set4(s->lighting.model_ambient, 0.2f, 0.2f, 0.2f, 1.0f);
    s->lighting.cm_face = GL_FRONT_AND_BACK;
    s->lighting.cm_mode = GL_AMBIENT_AND_DIFFUSE;
    for (i = 0; i < OGL_MAX_LIGHTS; i++) {
        ogl_light_t *l = &s->lighting.light[i];
        const float  d = i ? 0.0f : 1.0f;

        set4(l->ambient, 0.0f, 0.0f, 0.0f, 1.0f);
        set4(l->diffuse, d, d, d, 1.0f);
        set4(l->specular, d, d, d, 1.0f);
        set4(l->position, 0.0f, 0.0f, 1.0f, 0.0f);
        l->spot_dir[2]    = -1.0f;
        l->spot_cutoff    = 180.0f;
        l->attenuation[0] = 1.0f;
    }

    s->fog.mode    = GL_EXP;
    s->fog.density = 1.0f;
    s->fog.end     = 1.0f;

    s->buffers.depth_func         = GL_LESS;
    s->buffers.depth_mask         = GL_TRUE;
    s->buffers.depth_clear        = 1.0;
    s->buffers.stencil_func       = GL_ALWAYS;
    s->buffers.stencil_value_mask = 0xffffffffu;
    s->buffers.stencil_write_mask = 0xffffffffu;
    s->buffers.stencil_fail       = GL_KEEP;
    s->buffers.stencil_zfail      = GL_KEEP;
    s->buffers.stencil_zpass      = GL_KEEP;

    s->viewport.far_val = 1.0;

    s->transform.matrix_mode = GL_MODELVIEW;

    s->color_buffer.alpha_func  = GL_ALWAYS;
    s->color_buffer.blend_src   = GL_ONE;
    s->color_buffer.blend_dst   = GL_ZERO;
    s->color_buffer.logic_op    = GL_COPY;
    s->color_buffer.draw_buffer = dbl ? GL_BACK : GL_FRONT;
    s->color_buffer.index_mask  = 0xffffffffu;
    for (i = 0; i < 4; i++)
        s->color_buffer.color_mask[i] = GL_TRUE;

    s->hints.perspective    = GL_DONT_CARE;
    s->hints.point_smooth   = GL_DONT_CARE;
    s->hints.line_smooth    = GL_DONT_CARE;
    s->hints.polygon_smooth = GL_DONT_CARE;
    s->hints.fog            = GL_DONT_CARE;

    s->eval.grid1_n  = 1;
    s->eval.grid1_u2 = 1.0f;
    s->eval.grid2_un = 1;
    s->eval.grid2_vn = 1;
    s->eval.grid2_u2 = 1.0f;
    s->eval.grid2_v2 = 1.0f;
    for (i = 0; i < 9; i++) {
        if (!map_default(&s->map1[i], i, 0) || !map_default(&s->map2[i], i, 1))
            return 0;
    }

    for (i = 0; i < OGL_UNITS; i++) {
        ogl_texunit_t *u = &s->texture.unit[i];
        int            k;

        u->env_mode = GL_MODULATE;
        for (k = 0; k < 4; k++)
            u->gen_mode[k] = GL_EYE_LINEAR;
        u->obj_plane[0][0] = 1.0;
        u->obj_plane[1][1] = 1.0;
        u->eye_plane[0][0] = 1.0;
        u->eye_plane[1][1] = 1.0;
    }

    s->client.pack.alignment   = 4;
    s->client.unpack.alignment = 4;
    for (i = 0; i < OGL_ARRAYS; i++) {
        s->client.array[i].size = 4;
        s->client.array[i].type = GL_FLOAT;
    }
    s->client.array[OGL_ARRAY_NORMAL].size   = 3;
    s->client.array[OGL_ARRAY_INDEX].size    = 1;
    s->client.array[OGL_ARRAY_EDGEFLAG].size = 1;
    s->client.array[OGL_ARRAY_EDGEFLAG].type = GL_UNSIGNED_BYTE; /* edge flags are GLbooleans: no query gives it */

    for (i = 0; i < OGL_MODELVIEW_DEPTH; i++)
        identity(&s->modelview[i]);
    for (i = 0; i < OGL_PROJECTION_DEPTH; i++)
        identity(&s->projection[i]);
    for (i = 0; i < OGL_UNITS * OGL_TEXTURE_DEPTH; i++)
        identity(&s->texmatrix[i / OGL_TEXTURE_DEPTH][i % OGL_TEXTURE_DEPTH]);
    for (i = 0; i < OGL_UNITS; i++)
        s->texture_depth[i] = 1;
    s->modelview_depth  = 1;
    s->projection_depth = 1;
    s->normal_stale     = 1;

    for (i = 0; i < OGL_PIXEL_MAPS; i++)
        s->pixel_map[i].size = 1;
    ogl_palette_init(&s->shared_pal);

    s->render_mode   = GL_RENDER;
    s->feedback_type = GL_2D;
    s->begin_mode  = GL_NONE;

    ogl_regs_all(c);
    return 1;
}

void
ogl_state_free(ogl_ctx_t *c)
{
    ogl_state_t *s = c->gl;
    int          i;

    if (!s)
        return;
    for (i = 0; i < 9; i++) {
        ogl_free(s->map1[i].points);
        ogl_free(s->map2[i].points);
    }
    for (i = 0; i < s->attrib_depth; i++)
        ogl_free(s->attrib[i]);
    for (i = 0; i < s->client_depth; i++)
        ogl_free(s->client_attrib[i]);
    ogl_prim_free(c);
    ogl_draw_free(c);
    ogl_arrays_unlock(c);
    ogl_free(s->clip_buf);
    ogl_free(s->map_buf);
    ogl_dl_ctx_free(c);
    ogl_tex_ctx_free(c);
    ogl_free(s);
    c->gl = NULL;
}

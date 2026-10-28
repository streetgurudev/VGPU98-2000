/*
 * The attribute stacks (GL 1.1, section 6.1.12): glPushAttrib saves the
 * groups its mask names, glPopAttrib restores the last push's; the client
 * stack likewise for the pixel store and the vertex arrays. A group's
 * enables are saved with it (state.h keeps them as one mask: each group
 * restores its own bits). GL_TEXTURE_BIT saves the bound textures' names
 * and parameters too, and its pop binds them again and restores their
 * parameters.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "lists.h"
#include "regs.h"
#include "texobj.h"

struct ogl_attrib_rec {
    GLbitfield         mask;
    uint64_t           enables;
    ogl_current_t      current;
    ogl_raster_t       raster;
    uint32_t           stipple[32];
    ogl_pixel_mode_t   pixel_mode;
    ogl_lighting_t     lighting;
    ogl_fog_t          fog;
    ogl_buffers_t      buffers;
    ogl_viewport_t     viewport;
    ogl_transform_t    transform;
    ogl_color_buffer_t color_buffer;
    ogl_hints_t        hints;
    ogl_eval_attr_t    eval;
    GLuint             list_base;
    ogl_texture_attr_t texture;
    ogl_texparams_t    tex_params[OGL_UNITS][2]; /* each unit's bound 1D and 2D textures' */
    ogl_scissor_t      scissor;
};

struct ogl_client_rec {
    GLbitfield  mask;
    ogl_store_t pack, unpack;
    ogl_array_t array[OGL_ARRAYS];
    GLint       lock_first; /* EXT_compiled_vertex_array's, vertex-array state */
    GLsizei     lock_count;
    int         client_active; /* the multitexture extensions' */
    int         coord_set;
};

/* Each group's enables. */
#define EN_POINT    (OGL_E_POINT_SMOOTH)
#define EN_LINE     (OGL_E_LINE_SMOOTH | OGL_E_LINE_STIPPLE)
#define EN_POLYGON  (OGL_E_CULL_FACE | OGL_E_POLYGON_SMOOTH | OGL_E_POLYGON_STIPPLE | OGL_E_OFFSET_POINT | \
                     OGL_E_OFFSET_LINE | OGL_E_OFFSET_FILL)
#define EN_LIGHTING (OGL_E_LIGHTING | OGL_E_COLOR_MATERIAL | OGL_E_LIGHT_ALL)
#define EN_FOG      (OGL_E_FOG)
#define EN_DEPTH    (OGL_E_DEPTH_TEST)
#define EN_STENCIL  (OGL_E_STENCIL_TEST)
#define EN_TRANSFORM (OGL_E_NORMALIZE | OGL_E_CLIP_ALL)
#define EN_COLOR    (OGL_E_ALPHA_TEST | OGL_E_BLEND | OGL_E_DITHER | OGL_E_INDEX_LOGIC_OP | OGL_E_COLOR_LOGIC_OP)
#define EN_EVAL     (OGL_E_MAP1_ALL | OGL_E_MAP2_ALL | OGL_E_AUTO_NORMAL)
#define EN_SCISSOR  (OGL_E_SCISSOR_TEST)
/* GL_TEXTURE_BIT's enables are each unit's (ogl_texunit_t's en). */

/* A record of the context's state for the mask's groups: everything is
   copied, the restore takes only those groups. NULL without memory. */
static ogl_attrib_rec_t *
save(ogl_ctx_t *c, GLbitfield mask)
{
    const ogl_state_t *s = c->gl;
    ogl_attrib_rec_t  *r = ogl_alloc(sizeof(ogl_attrib_rec_t));
    int                i;

    if (!r)
        return NULL;
    r->mask         = mask;
    r->enables      = s->enables;
    r->current      = s->current;
    r->raster       = s->raster;
    memcpy(r->stipple, s->stipple, sizeof(r->stipple));
    r->pixel_mode   = s->pixel_mode;
    r->lighting     = s->lighting;
    r->fog          = s->fog;
    r->buffers      = s->buffers;
    r->viewport     = s->viewport;
    r->transform    = s->transform;
    r->color_buffer = s->color_buffer;
    r->hints        = s->hints;
    r->eval         = s->eval;
    r->list_base    = s->list_base;
    r->texture      = s->texture;
    r->scissor      = s->scissor;
    for (i = 0; i < 2 * OGL_UNITS; i++) {
        const ogl_tex_t *t = ogl_tex_bound_unit(c, i / 2, (i & 1) ? GL_TEXTURE_2D : GL_TEXTURE_1D);

        if (t)
            r->tex_params[i / 2][i & 1] = t->p;
    }
    return r;
}

static void
restore_enables(ogl_state_t *s, const ogl_attrib_rec_t *r, uint64_t bits)
{
    s->enables = (s->enables & ~bits) | (r->enables & bits);
}

/* The record's groups into the context's state (its registers then sent
   whole); the record freed. */
static void
restore(ogl_ctx_t *c, ogl_attrib_rec_t *r)
{
    ogl_state_t     *s = c->gl;
    const GLbitfield m = r->mask;

    if (m & GL_CURRENT_BIT)
        s->current = r->current;
    if (m & GL_POINT_BIT) {
        s->raster.point_size = r->raster.point_size;
        s->raster.point_min  = r->raster.point_min;
        s->raster.point_max  = r->raster.point_max;
        s->raster.point_fade = r->raster.point_fade;
        memcpy(s->raster.point_atten, r->raster.point_atten, sizeof(s->raster.point_atten));
        restore_enables(s, r, EN_POINT);
    }
    if (m & GL_LINE_BIT) {
        s->raster.line_width   = r->raster.line_width;
        s->raster.line_pattern = r->raster.line_pattern;
        s->raster.line_repeat  = r->raster.line_repeat;
        restore_enables(s, r, EN_LINE);
    }
    if (m & GL_POLYGON_BIT) {
        s->raster.cull_mode       = r->raster.cull_mode;
        s->raster.front_face      = r->raster.front_face;
        s->raster.polygon_mode[0] = r->raster.polygon_mode[0];
        s->raster.polygon_mode[1] = r->raster.polygon_mode[1];
        s->raster.offset_factor   = r->raster.offset_factor;
        s->raster.offset_units    = r->raster.offset_units;
        restore_enables(s, r, EN_POLYGON);
    }
    if (m & GL_POLYGON_STIPPLE_BIT)
        memcpy(s->stipple, r->stipple, sizeof(s->stipple));
    if (m & GL_PIXEL_MODE_BIT)
        s->pixel_mode = r->pixel_mode;
    if (m & GL_LIGHTING_BIT) {
        s->lighting = r->lighting;
        restore_enables(s, r, EN_LIGHTING);
    }
    if (m & GL_FOG_BIT) {
        s->fog = r->fog;
        restore_enables(s, r, EN_FOG);
    }
    if (m & GL_DEPTH_BUFFER_BIT) {
        s->buffers.depth_func  = r->buffers.depth_func;
        s->buffers.depth_mask  = r->buffers.depth_mask;
        s->buffers.depth_clear = r->buffers.depth_clear;
        restore_enables(s, r, EN_DEPTH);
    }
    if (m & GL_ACCUM_BUFFER_BIT)
        memcpy(s->buffers.accum_clear, r->buffers.accum_clear, sizeof(s->buffers.accum_clear));
    if (m & GL_STENCIL_BUFFER_BIT) {
        s->buffers.stencil_func       = r->buffers.stencil_func;
        s->buffers.stencil_ref        = r->buffers.stencil_ref;
        s->buffers.stencil_value_mask = r->buffers.stencil_value_mask;
        s->buffers.stencil_write_mask = r->buffers.stencil_write_mask;
        s->buffers.stencil_fail       = r->buffers.stencil_fail;
        s->buffers.stencil_zfail      = r->buffers.stencil_zfail;
        s->buffers.stencil_zpass      = r->buffers.stencil_zpass;
        s->buffers.stencil_clear      = r->buffers.stencil_clear;
        restore_enables(s, r, EN_STENCIL);
    }
    if (m & GL_VIEWPORT_BIT)
        s->viewport = r->viewport;
    if (m & GL_TRANSFORM_BIT) {
        s->transform = r->transform;
        restore_enables(s, r, EN_TRANSFORM);
    }
    if (m & GL_ENABLE_BIT) {
        int u;

        s->enables = r->enables;
        for (u = 0; u < OGL_UNITS; u++)
            s->texture.unit[u].en = r->texture.unit[u].en;
    }
    if (m & GL_COLOR_BUFFER_BIT) {
        s->color_buffer = r->color_buffer;
        restore_enables(s, r, EN_COLOR);
    }
    if (m & GL_HINT_BIT)
        s->hints = r->hints;
    if (m & GL_EVAL_BIT) {
        s->eval = r->eval;
        restore_enables(s, r, EN_EVAL);
    }
    if (m & GL_LIST_BIT)
        s->list_base = r->list_base;
    if (m & GL_TEXTURE_BIT) {
        int i;

        /* Both units (their enables with them), the active unit, the shared
           palette's enable (EXT_shared_texture_palette) */
        s->texture = r->texture;
        restore_enables(s, r, OGL_E_SHARED_PALETTE);
        for (i = 0; i < 2 * OGL_UNITS; i++) {
            ogl_tex_t *t = ogl_tex_bound_unit(c, i / 2, (i & 1) ? GL_TEXTURE_2D : GL_TEXTURE_1D);

            /* A texture deleted since is not there to take them (the
               binding fell back to the default). */
            if (t && (t->name == r->texture.unit[i / 2].bound[i & 1]))
                ogl_tex_set_params(c, t, &r->tex_params[i / 2][i & 1]);
        }
    }
    if (m & GL_SCISSOR_BIT) {
        s->scissor = r->scissor;
        restore_enables(s, r, EN_SCISSOR);
    }
    ogl_free(r);
    ogl_regs_all(c);
}

void STDCALL
ogl_PushAttrib(GLbitfield mask)
{
    ogl_state_t      *s;
    ogl_attrib_rec_t *r;
    OGL_CTX();

    OGL_DL(OP_PUSHATTRIB, mask);
    if (ogl_inside(c))
        return;
    s = c->gl;
    if (s->attrib_depth >= OGL_ATTRIB_DEPTH) {
        ogl_error(c, GL_STACK_OVERFLOW);
        return;
    }
    r = save(c, mask);
    if (!r) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    s->attrib[s->attrib_depth++] = r;
}

void STDCALL
ogl_PopAttrib(void)
{
    ogl_state_t      *s;
    ogl_attrib_rec_t *r;
    OGL_CTX();

    OGL_DL0(OP_POPATTRIB);
    if (ogl_inside(c))
        return;
    s = c->gl;
    if (s->attrib_depth <= 0) {
        ogl_error(c, GL_STACK_UNDERFLOW);
        return;
    }
    r                          = s->attrib[--s->attrib_depth];
    s->attrib[s->attrib_depth] = NULL;
    restore(c, r);
}

int
ogl_attrib_copy(ogl_ctx_t *src, ogl_ctx_t *dst, GLbitfield mask)
{
    ogl_attrib_rec_t *r = save(src, mask);

    if (!r)
        return 0;
    restore(dst, r);
    return 1;
}

void STDCALL
ogl_PushClientAttrib(GLbitfield mask)
{
    ogl_state_t      *s;
    ogl_client_rec_t *r;
    OGL_CTX();

    if (ogl_inside(c))
        return;
    s = c->gl;
    if (s->client_depth >= OGL_CLIENT_DEPTH) {
        ogl_error(c, GL_STACK_OVERFLOW);
        return;
    }
    r = ogl_alloc(sizeof(ogl_client_rec_t));
    if (!r) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    r->mask       = mask;
    r->pack       = s->client.pack;
    r->unpack     = s->client.unpack;
    r->lock_first    = s->client.lock_first;
    r->lock_count    = s->client.lock_count;
    r->client_active = s->client.client_active;
    r->coord_set     = s->client.coord_set;
    memcpy(r->array, s->client.array, sizeof(r->array));
    s->client_attrib[s->client_depth++] = r;
}

void STDCALL
ogl_PopClientAttrib(void)
{
    ogl_state_t      *s;
    ogl_client_rec_t *r;
    OGL_CTX();

    if (ogl_inside(c))
        return;
    s = c->gl;
    if (s->client_depth <= 0) {
        ogl_error(c, GL_STACK_UNDERFLOW);
        return;
    }
    r = s->client_attrib[--s->client_depth];
    s->client_attrib[s->client_depth] = NULL;
    if (r->mask & GL_CLIENT_PIXEL_STORE_BIT) {
        s->client.pack   = r->pack;
        s->client.unpack = r->unpack;
    }
    if (r->mask & GL_CLIENT_VERTEX_ARRAY_BIT) {
        memcpy(s->client.array, r->array, sizeof(r->array));
        /* The lock as it was, its arrays taken as they are now. */
        s->client.lock_first    = r->lock_first;
        s->client.lock_count    = r->lock_count;
        s->client.client_active = r->client_active;
        s->client.coord_set     = r->coord_set;
        ogl_arrays_lock_again(c);
    }
    ogl_free(r);
}

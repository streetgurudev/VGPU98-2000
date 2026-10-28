/*
 * GL's state as the OpenGL device's registers (regs.h). The ICD's state is
 * the truth; these put it in the device's terms: GL's enums as the
 * device's small numbers, its enables as the device's bits, the matrices
 * at the top of their stacks.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "regs.h"
#include "glmath.h"

static DWORD
fbits(float f)
{
    DWORD v;

    memcpy(&v, &f, 4);
    return v;
}

static void
regs4(ogl_ctx_t *c, DWORD index, const float *v, int n)
{
    int i;

    for (i = 0; i < n; i++)
        ogl_reg(c, index + (DWORD) i, fbits(v[i]));
}

/* The modelview's inverse transpose (3x3, for normals): GL takes normals
   through the inverse transpose of the modelview's upper-left 3x3. */
void
ogl_normal_matrix(ogl_state_t *s)
{
    const float *mv = s->modelview[s->modelview_depth - 1].m;
    int          r, k;

    if (!s->normal_stale)
        return;
    ogl_mat_invert(s->modelview_inverse, mv);
    /* (M^-1)^T: element (row r, column k) is the inverse's (k, r). */
    for (k = 0; k < 3; k++) {
        for (r = 0; r < 3; r++)
            s->normal[k * 3 + r] = s->modelview_inverse[r * 4 + k];
    }
    s->normal_stale = 0;
}

void
ogl_regs_matrices(ogl_ctx_t *c)
{
    ogl_state_t *s = c->gl;
    int          k;

    regs4(c, VGPU98_OGL_R_MODELVIEW, s->modelview[s->modelview_depth - 1].m, 16);
    regs4(c, VGPU98_OGL_R_PROJECTION, s->projection[s->projection_depth - 1].m, 16);
    regs4(c, VGPU98_OGL_R_TEXTURE, s->texmatrix[0][s->texture_depth[0] - 1].m, 16);
    regs4(c, VGPU98_OGL_R_UNIT1 + VGPU98_OGL_U_TEXTURE, s->texmatrix[1][s->texture_depth[1] - 1].m, 16);
    ogl_normal_matrix(s);
    for (k = 0; k < 3; k++) {
        regs4(c, VGPU98_OGL_R_NORMAL + 4 * (DWORD) k, &s->normal[k * 3], 3);
        ogl_reg(c, VGPU98_OGL_R_NORMAL + 4 * (DWORD) k + 3, 0);
    }
}

void
ogl_regs_enables(ogl_ctx_t *c)
{
    static const struct {
        uint64_t gl;
        DWORD    dev;
    } map[] = {
        { OGL_E_LIGHTING, VGPU98_OGL_EN_LIGHTING },         { OGL_E_COLOR_MATERIAL, VGPU98_OGL_EN_COLOR_MATERIAL },
        { OGL_E_NORMALIZE, VGPU98_OGL_EN_NORMALIZE },       { OGL_E_FOG, VGPU98_OGL_EN_FOG },
        { OGL_E_ALPHA_TEST, VGPU98_OGL_EN_ALPHA_TEST },     { OGL_E_DEPTH_TEST, VGPU98_OGL_EN_DEPTH_TEST },
        { OGL_E_STENCIL_TEST, VGPU98_OGL_EN_STENCIL_TEST }, { OGL_E_BLEND, VGPU98_OGL_EN_BLEND },
        { OGL_E_COLOR_LOGIC_OP, VGPU98_OGL_EN_LOGIC_OP },   { OGL_E_SCISSOR_TEST, VGPU98_OGL_EN_SCISSOR },
        { OGL_E_CULL_FACE, VGPU98_OGL_EN_CULL_FACE },       { OGL_E_OFFSET_FILL, VGPU98_OGL_EN_OFFSET_FILL },
        { OGL_E_OFFSET_LINE, VGPU98_OGL_EN_OFFSET_LINE },   { OGL_E_OFFSET_POINT, VGPU98_OGL_EN_OFFSET_POINT },
        { OGL_E_POLYGON_STIPPLE, VGPU98_OGL_EN_POLY_STIPPLE }, { OGL_E_LINE_STIPPLE, VGPU98_OGL_EN_LINE_STIPPLE },
        { OGL_E_POINT_SMOOTH, VGPU98_OGL_EN_POINT_SMOOTH }, { OGL_E_LINE_SMOOTH, VGPU98_OGL_EN_LINE_SMOOTH },
        { OGL_E_POLYGON_SMOOTH, VGPU98_OGL_EN_POLY_SMOOTH }, { OGL_E_DITHER, VGPU98_OGL_EN_DITHER },
        { OGL_E_SHARED_PALETTE, VGPU98_OGL_EN_SHARED_PALETTE },
    };
    const ogl_state_t *s = c->gl;
    DWORD              en = 0;
    unsigned           i;
    int                one_d = 0;

    for (i = 0; i < ARRAY_SIZE(map); i++) {
        if (s->enables & map[i].gl)
            en |= map[i].dev;
    }
    if (ogl_texture_effective(c, 0, &one_d) && one_d)
        en |= VGPU98_OGL_EN_TEXTURE_1D;
    if (ogl_texture_effective(c, 1, &one_d) && one_d)
        en |= VGPU98_OGL_EN_TEXTURE1_1D;
    ogl_reg(c, VGPU98_OGL_R_ENABLES, en);
    ogl_reg(c, VGPU98_OGL_R_LIGHTS_ON, (DWORD) ((s->enables & OGL_E_LIGHT_ALL) >> 46));
    ogl_reg(c, VGPU98_OGL_R_CLIPS_ON, (DWORD) ((s->enables & OGL_E_CLIP_ALL) >> 54));
}

static DWORD
face_bits(GLenum face)
{
    return (face == GL_FRONT) ? 1u : (face == GL_BACK) ? 2u : 3u;
}

void
ogl_regs_lighting(ogl_ctx_t *c)
{
    const ogl_lighting_t *l = &c->gl->lighting;
    DWORD                 cm;
    int                   f, i;

    ogl_reg(c, VGPU98_OGL_R_SHADE, (l->shade_model == GL_SMOOTH) ? 1u : 0u);
    regs4(c, VGPU98_OGL_R_MODEL_AMBIENT, l->model_ambient, 4);
    ogl_reg(c, VGPU98_OGL_R_MODEL_FLAGS, (l->local_viewer ? 1u : 0u) | (l->two_side ? 2u : 0u));
    switch (l->cm_mode) {
        case GL_EMISSION:
            cm = VGPU98_OGL_CM_EMISSION;
            break;
        case GL_AMBIENT:
            cm = VGPU98_OGL_CM_AMBIENT;
            break;
        case GL_DIFFUSE:
            cm = VGPU98_OGL_CM_DIFFUSE;
            break;
        case GL_SPECULAR:
            cm = VGPU98_OGL_CM_SPECULAR;
            break;
        default:
            cm = VGPU98_OGL_CM_AMBIENT_DIFFUSE;
            break;
    }
    ogl_reg(c, VGPU98_OGL_R_COLOR_MATERIAL, face_bits(l->cm_face) | (cm << 4));
    for (f = 0; f < 2; f++) {
        const ogl_material_t *m    = &l->material[f];
        const DWORD           base = VGPU98_OGL_R_MATERIAL(f);

        regs4(c, base + VGPU98_OGL_M_AMBIENT, m->ambient, 4);
        regs4(c, base + VGPU98_OGL_M_DIFFUSE, m->diffuse, 4);
        regs4(c, base + VGPU98_OGL_M_SPECULAR, m->specular, 4);
        regs4(c, base + VGPU98_OGL_M_EMISSION, m->emission, 4);
        ogl_reg(c, base + VGPU98_OGL_M_SHININESS, fbits(m->shininess));
    }
    for (i = 0; i < OGL_MAX_LIGHTS; i++) {
        const ogl_light_t *lt   = &l->light[i];
        const DWORD        base = VGPU98_OGL_R_LIGHT(i);

        regs4(c, base + VGPU98_OGL_L_AMBIENT, lt->ambient, 4);
        regs4(c, base + VGPU98_OGL_L_DIFFUSE, lt->diffuse, 4);
        regs4(c, base + VGPU98_OGL_L_SPECULAR, lt->specular, 4);
        regs4(c, base + VGPU98_OGL_L_POSITION, lt->position, 4);
        regs4(c, base + VGPU98_OGL_L_SPOT_DIRECTION, lt->spot_dir, 3);
        ogl_reg(c, base + VGPU98_OGL_L_SPOT_EXPONENT, fbits(lt->spot_exponent));
        ogl_reg(c, base + VGPU98_OGL_L_SPOT_CUTOFF, fbits(lt->spot_cutoff));
        regs4(c, base + VGPU98_OGL_L_ATTENUATION, lt->attenuation, 3);
    }
}

/* A unit's registers of a kind: unit 0's where 2.5 has them, unit 1's in
   its block (vid_vgpu98_oglcmd.h). */
static DWORD
unit_reg(int unit, DWORD reg0, DWORD reg1)
{
    return unit ? VGPU98_OGL_R_UNIT1 + reg1 : reg0;
}

void
ogl_regs_texgen(ogl_ctx_t *c)
{
    const ogl_state_t *s = c->gl;
    int                u, i, k;

    for (u = 0; u < OGL_UNITS; u++) {
        const ogl_texunit_t *t    = &s->texture.unit[u];
        const DWORD          obj  = unit_reg(u, VGPU98_OGL_R_TEXGEN_OBJECT, VGPU98_OGL_U_TEXGEN_OBJECT);
        const DWORD          eye  = unit_reg(u, VGPU98_OGL_R_TEXGEN_EYE, VGPU98_OGL_U_TEXGEN_EYE);
        DWORD                mode = 0;

        for (i = 0; i < 4; i++) {
            DWORD m = VGPU98_OGL_TG_OFF;

            if (t->en & (OGL_TE_GEN_S << i)) {
                switch (t->gen_mode[i]) {
                    case GL_OBJECT_LINEAR:
                        m = VGPU98_OGL_TG_OBJECT;
                        break;
                    case GL_SPHERE_MAP:
                        m = VGPU98_OGL_TG_SPHERE;
                        break;
                    default:
                        m = VGPU98_OGL_TG_EYE;
                        break;
                }
            }
            mode |= m << (4 * i);
            for (k = 0; k < 4; k++) {
                ogl_reg(c, obj + 4 * (DWORD) i + (DWORD) k, fbits((float) t->obj_plane[i][k]));
                ogl_reg(c, eye + 4 * (DWORD) i + (DWORD) k, fbits((float) t->eye_plane[i][k]));
            }
        }
        ogl_reg(c, unit_reg(u, VGPU98_OGL_R_TEXGEN_MODE, VGPU98_OGL_U_TEXGEN_MODE), mode);
    }
}

void
ogl_regs_clip(ogl_ctx_t *c)
{
    const ogl_state_t *s = c->gl;
    int                i, k;

    for (i = 0; i < OGL_MAX_CLIP_PLANES; i++) {
        for (k = 0; k < 4; k++)
            ogl_reg(c, VGPU98_OGL_R_CLIP_PLANE + 4 * (DWORD) i + (DWORD) k, fbits((float) s->transform.clip_plane[i][k]));
    }
}

void
ogl_regs_fog(ogl_ctx_t *c)
{
    const ogl_fog_t *f = &c->gl->fog;

    ogl_reg(c, VGPU98_OGL_R_FOG_MODE, (f->mode == GL_LINEAR) ? 0u : (f->mode == GL_EXP) ? 1u : 2u);
    ogl_reg(c, VGPU98_OGL_R_FOG_DENSITY, fbits(f->density));
    ogl_reg(c, VGPU98_OGL_R_FOG_START, fbits(f->start));
    ogl_reg(c, VGPU98_OGL_R_FOG_END, fbits(f->end));
    regs4(c, VGPU98_OGL_R_FOG_COLOR, f->color, 4);
}

void
ogl_regs_texture(ogl_ctx_t *c)
{
    int u;

    for (u = 0; u < OGL_UNITS; u++) {
        const ogl_texunit_t *t     = &c->gl->texture.unit[u];
        int                  one_d = 0;
        DWORD                env;

        switch (t->env_mode) {
            case GL_DECAL:
                env = VGPU98_OGL_ENV_DECAL;
                break;
            case GL_BLEND:
                env = VGPU98_OGL_ENV_BLEND;
                break;
            case GL_REPLACE:
                env = VGPU98_OGL_ENV_REPLACE;
                break;
            default:
                env = VGPU98_OGL_ENV_MODULATE;
                break;
        }
        ogl_reg(c, unit_reg(u, VGPU98_OGL_R_TEXTURE_ID, VGPU98_OGL_U_TEXTURE_ID), ogl_texture_effective(c, u, &one_d));
        ogl_reg(c, unit_reg(u, VGPU98_OGL_R_TEX_ENV_MODE, VGPU98_OGL_U_TEX_ENV_MODE), env);
        regs4(c, unit_reg(u, VGPU98_OGL_R_TEX_ENV_COLOR, VGPU98_OGL_U_TEX_ENV_COLOR), t->env_color, 4);
    }
    ogl_regs_enables(c); /* the 1D bits follow the textures drawn with */
}

DWORD
ogl_draw_mask(GLenum buffer)
{
    switch (buffer) {
        case GL_FRONT:
        case GL_FRONT_LEFT:
            return 1;
        case GL_BACK:
        case GL_BACK_LEFT:
            return 2;
        case GL_FRONT_AND_BACK:
        case GL_LEFT:
            return 3;
        default:
            return 0;
    }
}

static DWORD
blend_factor(GLenum f)
{
    switch (f) {
        case GL_ZERO:
            return VGPU98_OGL_BF_ZERO;
        case GL_SRC_COLOR:
            return VGPU98_OGL_BF_SRC_COLOR;
        case GL_ONE_MINUS_SRC_COLOR:
            return VGPU98_OGL_BF_ONE_MINUS_SRC_COLOR;
        case GL_SRC_ALPHA:
            return VGPU98_OGL_BF_SRC_ALPHA;
        case GL_ONE_MINUS_SRC_ALPHA:
            return VGPU98_OGL_BF_ONE_MINUS_SRC_ALPHA;
        case GL_DST_ALPHA:
            return VGPU98_OGL_BF_DST_ALPHA;
        case GL_ONE_MINUS_DST_ALPHA:
            return VGPU98_OGL_BF_ONE_MINUS_DST_ALPHA;
        case GL_DST_COLOR:
            return VGPU98_OGL_BF_DST_COLOR;
        case GL_ONE_MINUS_DST_COLOR:
            return VGPU98_OGL_BF_ONE_MINUS_DST_COLOR;
        case GL_SRC_ALPHA_SATURATE:
            return VGPU98_OGL_BF_SRC_ALPHA_SATURATE;
        default:
            return VGPU98_OGL_BF_ONE;
    }
}

static DWORD
stencil_op(GLenum op)
{
    switch (op) {
        case GL_ZERO:
            return VGPU98_OGL_SOP_ZERO;
        case GL_REPLACE:
            return VGPU98_OGL_SOP_REPLACE;
        case GL_INCR:
            return VGPU98_OGL_SOP_INCR;
        case GL_DECR:
            return VGPU98_OGL_SOP_DECR;
        case GL_INVERT:
            return VGPU98_OGL_SOP_INVERT;
        default:
            return VGPU98_OGL_SOP_KEEP;
    }
}

void
ogl_regs_fragment(ogl_ctx_t *c)
{
    const ogl_state_t *s = c->gl;
    const ogl_buffers_t *b = &s->buffers;
    const int            stencil_bits = ogl_format_bits(c->format, OGL_BITS_STENCIL);
    const DWORD          smask = stencil_bits ? ((1u << stencil_bits) - 1u) : 0u;
    GLint                ref   = b->stencil_ref;
    DWORD                cmask = 0;
    int                  i;

    ogl_reg(c, VGPU98_OGL_R_ALPHA_FUNC, (s->color_buffer.alpha_func - GL_NEVER) & 7u);
    ogl_reg(c, VGPU98_OGL_R_ALPHA_REF, fbits(s->color_buffer.alpha_ref));
    ogl_reg(c, VGPU98_OGL_R_DEPTH, ((b->depth_func - GL_NEVER) & 7u) | (b->depth_mask ? 0x100u : 0u));
    /* The reference is clamped to the stencil buffer's range (GL 1.1). */
    if (ref < 0)
        ref = 0;
    if ((DWORD) ref > smask)
        ref = (GLint) smask;
    ogl_reg(c, VGPU98_OGL_R_STENCIL, ((b->stencil_func - GL_NEVER) & 7u) | (((DWORD) ref & 0xffu) << 8) |
                                         ((b->stencil_value_mask & smask & 0xffu) << 16) |
                                         ((b->stencil_write_mask & smask & 0xffu) << 24));
    ogl_reg(c, VGPU98_OGL_R_STENCIL_OPS,
            stencil_op(b->stencil_fail) | (stencil_op(b->stencil_zfail) << 4) | (stencil_op(b->stencil_zpass) << 8));
    ogl_reg(c, VGPU98_OGL_R_BLEND, blend_factor(s->color_buffer.blend_src) | (blend_factor(s->color_buffer.blend_dst) << 8));
    ogl_reg(c, VGPU98_OGL_R_LOGIC_OP, (s->color_buffer.logic_op - GL_CLEAR) & 15u);
    for (i = 0; i < 4; i++) {
        if (s->color_buffer.color_mask[i])
            cmask |= 1u << i;
    }
    ogl_reg(c, VGPU98_OGL_R_COLOR_MASK, cmask);
    ogl_reg(c, VGPU98_OGL_R_SCISSOR, (DWORD) s->scissor.x);
    ogl_reg(c, VGPU98_OGL_R_SCISSOR + 1, (DWORD) s->scissor.y);
    ogl_reg(c, VGPU98_OGL_R_SCISSOR + 2, (DWORD) s->scissor.w);
    ogl_reg(c, VGPU98_OGL_R_SCISSOR + 3, (DWORD) s->scissor.h);
    ogl_reg(c, VGPU98_OGL_R_DRAW_BUFFERS, ogl_draw_mask(s->color_buffer.draw_buffer));
    ogl_reg(c, VGPU98_OGL_R_READ_BUFFER, (ogl_draw_mask(s->pixel_mode.read_buffer) & 1u) ? 0u : 1u);
}

void
ogl_regs_raster(ogl_ctx_t *c)
{
    const ogl_state_t  *s = c->gl;
    const ogl_raster_t *r = &s->raster;
    DWORD               face;
    int                 i;

    ogl_reg(c, VGPU98_OGL_R_VIEWPORT, (DWORD) s->viewport.x);
    ogl_reg(c, VGPU98_OGL_R_VIEWPORT + 1, (DWORD) s->viewport.y);
    ogl_reg(c, VGPU98_OGL_R_VIEWPORT + 2, (DWORD) s->viewport.w);
    ogl_reg(c, VGPU98_OGL_R_VIEWPORT + 3, (DWORD) s->viewport.h);
    ogl_reg(c, VGPU98_OGL_R_DEPTH_RANGE, fbits((float) s->viewport.near_val));
    ogl_reg(c, VGPU98_OGL_R_DEPTH_RANGE + 1, fbits((float) s->viewport.far_val));
    ogl_reg(c, VGPU98_OGL_R_POINT_SIZE, fbits(r->point_size));
    ogl_reg(c, VGPU98_OGL_R_POINT_PARAMS, ogl_point_params_on(r) ? 1u : 0u);
    ogl_reg(c, VGPU98_OGL_R_POINT_MIN, fbits(r->point_min));
    ogl_reg(c, VGPU98_OGL_R_POINT_MAX, fbits(r->point_max));
    ogl_reg(c, VGPU98_OGL_R_POINT_FADE, fbits(r->point_fade));
    regs4(c, VGPU98_OGL_R_POINT_ATTEN, r->point_atten, 3);
    ogl_reg(c, VGPU98_OGL_R_LINE_WIDTH, fbits(r->line_width));
    ogl_reg(c, VGPU98_OGL_R_LINE_STIPPLE, (DWORD) r->line_pattern | ((DWORD) r->line_repeat << 16));
    ogl_reg(c, VGPU98_OGL_R_POLYGON_OFFSET, fbits(r->offset_factor));
    ogl_reg(c, VGPU98_OGL_R_POLYGON_OFFSET + 1, fbits(r->offset_units));
    face = (r->front_face == GL_CW) ? 1u : 0u;
    face |= face_bits(r->cull_mode) << 4;
    ogl_reg(c, VGPU98_OGL_R_FACE, face);
    for (i = 0; i < 32; i++)
        ogl_reg(c, VGPU98_OGL_R_POLYGON_STIPPLE + (DWORD) i, s->stipple[i]);
}

void
ogl_regs_all(ogl_ctx_t *c)
{
    ogl_regs_matrices(c);
    ogl_regs_lighting(c);
    ogl_regs_texgen(c);
    ogl_regs_clip(c);
    ogl_regs_fog(c);
    ogl_regs_texture(c);
    ogl_regs_fragment(c);
    ogl_regs_raster(c);
    ogl_regs_enables(c);
    ogl_reg(c, VGPU98_OGL_R_FOG_PER_PIXEL, 1);
}

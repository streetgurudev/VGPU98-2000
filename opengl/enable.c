/*
 * glEnable, glDisable, glIsEnabled and the client arrays' enables (GL 1.1,
 * sections 2.8 and 6.1.1): each capability one bit of the context's mask
 * (state.h), its change turned into the device's registers.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "lists.h"
#include "prim.h"
#include "regs.h"

uint64_t
ogl_cap_bit(GLenum cap)
{
    switch (cap) {
        case GL_ALPHA_TEST:
            return OGL_E_ALPHA_TEST;
        case GL_AUTO_NORMAL:
            return OGL_E_AUTO_NORMAL;
        case GL_BLEND:
            return OGL_E_BLEND;
        case GL_COLOR_MATERIAL:
            return OGL_E_COLOR_MATERIAL;
        case GL_CULL_FACE:
            return OGL_E_CULL_FACE;
        case GL_DEPTH_TEST:
            return OGL_E_DEPTH_TEST;
        case GL_DITHER:
            return OGL_E_DITHER;
        case GL_FOG:
            return OGL_E_FOG;
        case GL_LIGHTING:
            return OGL_E_LIGHTING;
        case GL_LINE_SMOOTH:
            return OGL_E_LINE_SMOOTH;
        case GL_LINE_STIPPLE:
            return OGL_E_LINE_STIPPLE;
        case GL_INDEX_LOGIC_OP:
            return OGL_E_INDEX_LOGIC_OP;
        case GL_COLOR_LOGIC_OP:
            return OGL_E_COLOR_LOGIC_OP;
        case GL_NORMALIZE:
            return OGL_E_NORMALIZE;
        case GL_POINT_SMOOTH:
            return OGL_E_POINT_SMOOTH;
        case GL_POLYGON_OFFSET_FILL:
            return OGL_E_OFFSET_FILL;
        case GL_POLYGON_OFFSET_LINE:
            return OGL_E_OFFSET_LINE;
        case GL_POLYGON_OFFSET_POINT:
            return OGL_E_OFFSET_POINT;
        case GL_POLYGON_SMOOTH:
            return OGL_E_POLYGON_SMOOTH;
        case GL_POLYGON_STIPPLE:
            return OGL_E_POLYGON_STIPPLE;
        case GL_SCISSOR_TEST:
            return OGL_E_SCISSOR_TEST;
        case GL_STENCIL_TEST:
            return OGL_E_STENCIL_TEST;
        case GL_SHARED_TEXTURE_PALETTE_EXT:
            return ogl_ext_on(OGL_EXT_SHARED_TEXTURE_PALETTE) ? OGL_E_SHARED_PALETTE : 0;
        default:
            break;
    }
    if ((cap >= GL_MAP1_COLOR_4) && (cap <= GL_MAP1_VERTEX_4))
        return OGL_E_MAP1(cap - GL_MAP1_COLOR_4);
    if ((cap >= GL_MAP2_COLOR_4) && (cap <= GL_MAP2_VERTEX_4))
        return OGL_E_MAP2(cap - GL_MAP2_COLOR_4);
    if ((cap >= GL_LIGHT0) && (cap < GL_LIGHT0 + OGL_MAX_LIGHTS))
        return OGL_E_LIGHT(cap - GL_LIGHT0);
    if ((cap >= GL_CLIP_PLANE0) && (cap < GL_CLIP_PLANE0 + OGL_MAX_CLIP_PLANES))
        return OGL_E_CLIP(cap - GL_CLIP_PLANE0);
    return 0;
}

/* A texture unit's capability: its bit of the active unit's enables
   (OGL_TE_*), 0 for another name. */
static uint32_t
tex_cap_bit(GLenum cap)
{
    switch (cap) {
        case GL_TEXTURE_1D:
            return OGL_TE_1D;
        case GL_TEXTURE_2D:
            return OGL_TE_2D;
        case GL_TEXTURE_GEN_S:
        case GL_TEXTURE_GEN_T:
        case GL_TEXTURE_GEN_R:
        case GL_TEXTURE_GEN_Q:
            return (uint32_t) OGL_TE_GEN_S << (cap - GL_TEXTURE_GEN_S);
        default:
            return 0;
    }
}

int
ogl_cap_query(const ogl_state_t *s, GLenum cap, int *on)
{
    const uint32_t tb = tex_cap_bit(cap);
    uint64_t       bit;

    if (tb) {
        *on = (s->texture.unit[s->texture.active].en & tb) != 0;
        return 1;
    }
    bit = ogl_cap_bit(cap);
    if (!bit)
        return 0;
    *on = (s->enables & bit) != 0;
    return 1;
}

void
ogl_cap_regs(ogl_ctx_t *c, uint64_t bits)
{
    /* Colour material on: the material tracks the current colour from now
       (GL 1.1, 2.13.3); off: the device draws with the material as the
       tracking left it. */
    if (bits & OGL_E_COLOR_MATERIAL) {
        if (c->gl->enables & OGL_E_COLOR_MATERIAL)
            ogl_color_material(c);
        ogl_regs_lighting(c);
    }
    ogl_regs_enables(c);
}

static void
set_cap(GLenum cap, int on)
{
    uint64_t bit;
    uint32_t tb;
    OGL_CTX();

    if (on)
        OGL_DL(OP_ENABLE, cap);
    else
        OGL_DL(OP_DISABLE, cap);
    tb  = tex_cap_bit(cap);
    bit = tb ? 0 : ogl_cap_bit(cap);
    if (!tb && !bit) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    if (tb) {
        /* The active unit's */
        ogl_texunit_t *u = &c->gl->texture.unit[c->gl->texture.active];

        if (on)
            u->en |= tb;
        else
            u->en &= ~tb;
        if (tb & (OGL_TE_1D | OGL_TE_2D))
            ogl_regs_texture(c);
        else
            ogl_regs_texgen(c);
        return;
    }
    if (on)
        c->gl->enables |= bit;
    else
        c->gl->enables &= ~bit;
    ogl_cap_regs(c, bit);
}

void STDCALL
ogl_Enable(GLenum cap)
{
    set_cap(cap, 1);
}

void STDCALL
ogl_Disable(GLenum cap)
{
    set_cap(cap, 0);
}

/* The client arrays' capabilities (glEnableClientState), their index in
   state.h's arrays (the texture coordinates' of the client unit); -1 for
   another name. */
static int
array_of(const ogl_state_t *s, GLenum cap)
{
    switch (cap) {
        case GL_VERTEX_ARRAY:
            return OGL_ARRAY_VERTEX;
        case GL_NORMAL_ARRAY:
            return OGL_ARRAY_NORMAL;
        case GL_COLOR_ARRAY:
            return OGL_ARRAY_COLOR;
        case GL_INDEX_ARRAY:
            return OGL_ARRAY_INDEX;
        case GL_TEXTURE_COORD_ARRAY:
            return OGL_ARRAY_TEX(s->client.client_active);
        case GL_EDGE_FLAG_ARRAY:
            return OGL_ARRAY_EDGEFLAG;
        default:
            return -1;
    }
}

GLboolean STDCALL
ogl_IsEnabled(GLenum cap)
{
    int a, on;
    OGL_CTX(GL_FALSE);

    if (ogl_inside(c))
        return GL_FALSE;
    a = array_of(c->gl, cap);
    if (a >= 0)
        return c->gl->client.array[a].enabled;
    if (!ogl_cap_query(c->gl, cap, &on)) {
        ogl_error(c, GL_INVALID_ENUM);
        return GL_FALSE;
    }
    return on ? GL_TRUE : GL_FALSE;
}

/* Client state: never compiled into a display list (GL runs it at once),
   and an error between glBegin and glEnd. */
static void
set_client(GLenum cap, GLboolean on)
{
    int a;
    OGL_CTX();

    a = array_of(c->gl, cap);
    if (a < 0) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->client.array[a].enabled = on;
}

void STDCALL
ogl_EnableClientState(GLenum cap)
{
    set_client(cap, GL_TRUE);
}

void STDCALL
ogl_DisableClientState(GLenum cap)
{
    set_client(cap, GL_FALSE);
}

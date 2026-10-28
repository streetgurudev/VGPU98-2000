/*
 * Texture objects (GL 1.1, section 3.8): names in the share group (the
 * default textures, name 0, are the context's own), their parameters,
 * their levels as defined, and the device's copy.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_ICD_TEXOBJ_H
#define VGPU98_ICD_TEXOBJ_H

#include "state.h"

#define OGL_LEVELS 12 /* 2048 down to 1 */

typedef struct ogl_texparams {
    GLenum min_filter, mag_filter;
    GLenum wrap_s, wrap_t;
    float  border[4];
    float  priority;
} ogl_texparams_t;

typedef struct ogl_level {
    GLsizei w, h;      /* border included */
    GLint   border;
    GLint   ifmt;      /* the internal format asked for */
    int     base;      /* VGPU98_OGL_BASE_*, 0: not defined */
} ogl_level_t;

typedef struct ogl_tex {
    GLuint          name;
    GLenum          target; /* GL_TEXTURE_1D or GL_TEXTURE_2D once bound; 0 before */
    DWORD           dev;    /* the device's ID in the group, 0 before its first use there */
    ogl_texparams_t p;
    ogl_level_t     level[OGL_LEVELS]; /* an index texture's levels: base VGPU98_OGL_BASE_INDEX */
    ogl_palette_t   pal;               /* its palette (palette.c) */
    struct ogl_tex *next;
} ogl_tex_t;

/* The texture bound to a target of the active unit, or of a unit (the
   context's default one for name 0). */
ogl_tex_t *ogl_tex_bound(ogl_ctx_t *c, GLenum target);
ogl_tex_t *ogl_tex_bound_unit(ogl_ctx_t *c, int unit, GLenum target);
/* Its parameters (attrib.c saves and restores the bound textures'). */
void       ogl_tex_set_params(ogl_ctx_t *c, ogl_tex_t *t, const ogl_texparams_t *p);
/* Is it complete for its filters (GL 1.1, section 3.8.9)? */
int        ogl_tex_complete(const ogl_tex_t *t);
/* The context's proxy of 1D (two 0) or 2D textures, made when needed. */
ogl_tex_t *ogl_tex_proxy(ogl_ctx_t *c, int two);
/* The texture's device ID, made (and its parameters sent) on first need. */
DWORD      ogl_tex_dev(ogl_ctx_t *c, ogl_tex_t *t);
/* The group's textures, freed with it. */
void       ogl_tex_group_free(ogl_group_t *g);
/* The context's default textures, freed with it. */
void       ogl_tex_ctx_free(ogl_ctx_t *c);
/* Their IDs given back to the group at the context's close, the device
   told (`send`) while the group is open there. */
void       ogl_tex_ctx_release(ogl_ctx_t *c, int send);

/* Palettes (palette.c): a palette as it starts (one white entry of RGBA);
   whether one is still so; the device told of a texture's palette (t) or
   the context's shared one (t NULL); the palette an index texture draws
   with (the shared one while EXT_shared_texture_palette is on). */
void                 ogl_palette_init(ogl_palette_t *p);
int                  ogl_palette_default(const ogl_palette_t *p);
void                 ogl_palette_send(ogl_ctx_t *c, const ogl_tex_t *t);
const ogl_palette_t *ogl_palette_of(ogl_ctx_t *c, const ogl_tex_t *t);

/* Diagnostics "OpenGL" (diag.c): a unit's texturing on with a texture that
   cannot draw (NULL: none), as last chosen for the registers; a draw with
   one noted is icd.h's ogl_diag_untextured_draw. */
void ogl_diag_untextured(int unit, const ogl_tex_t *t);

/* A pixel's RGBA (0-1) as a texel of a base format, and back, as the
   device keeps texels (teximage.c). */
DWORD ogl_texel(int base, const float *rgba);
void  ogl_texel_rgba(int base, DWORD t, float *rgba);

#endif /* VGPU98_ICD_TEXOBJ_H */

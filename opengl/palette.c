/*
 * Palettes: EXT_paletted_texture's
 * colour tables (glColorTableEXT, glColorSubTableEXT, glGetColorTableEXT,
 * glGetColorTableParameter*EXT) and EXT_shared_texture_palette's shared
 * palette. A texture's palette belongs to it (its share group's); the
 * shared palette to the context. Each goes to the device whole (PALETTE),
 * up to 256 entries: a palette change never sends the texels again.
 *
 * A table's data is unpacked and taken through the pixel transfer path as a
 * texture image's (pixel.c), and kept as texels of its internal format's
 * base format. Its width is a power of two up to 256: wider is refused (a
 * proxy is left with width 0).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "lists.h"
#include "pixel.h"
#include "texobj.h"

void
ogl_palette_init(ogl_palette_t *p)
{
    memset(p, 0, sizeof(*p));
    p->n    = 1;
    p->ifmt = GL_RGBA;
    p->base = VGPU98_OGL_BASE_RGBA;
    p->e[0] = 0xffffffffu;
}

int
ogl_palette_default(const ogl_palette_t *p)
{
    return (p->n == 1) && (p->base == VGPU98_OGL_BASE_RGBA) && (p->e[0] == 0xffffffffu);
}

const ogl_palette_t *
ogl_palette_of(ogl_ctx_t *c, const ogl_tex_t *t)
{
    return (c->gl->enables & OGL_E_SHARED_PALETTE) ? &c->gl->shared_pal : &t->pal;
}

void
ogl_palette_send(ogl_ctx_t *c, const ogl_tex_t *t)
{
    const ogl_palette_t *p = t ? &t->pal : &c->gl->shared_pal;
    DWORD               *w;

    if ((t && !t->dev) || (p->n < 1) || !ogl_objects(c))
        return;
    w = ogl_cmd(c, VGPU98_OGL_PALETTE, VGPU98_OGL_LEN_PALETTE + (DWORD) p->n);
    if (!w)
        return;
    w[0] = c->id;
    w[1] = t ? t->dev : 0;
    w[2] = (DWORD) p->base;
    w[3] = (DWORD) p->n;
    memcpy(&w[VGPU98_OGL_LEN_PALETTE], p->e, (size_t) p->n * 4);
}

/* A table's internal format's base format (a texture's, without the index
   formats); 0 for another name. */
static int
table_base(GLint ifmt)
{
    switch (ifmt) {
        case GL_ALPHA:
        case GL_ALPHA4:
        case GL_ALPHA8:
        case GL_ALPHA12:
        case GL_ALPHA16:
            return VGPU98_OGL_BASE_ALPHA;
        case 1:
        case GL_LUMINANCE:
        case GL_LUMINANCE4:
        case GL_LUMINANCE8:
        case GL_LUMINANCE12:
        case GL_LUMINANCE16:
            return VGPU98_OGL_BASE_LUMINANCE;
        case 2:
        case GL_LUMINANCE_ALPHA:
        case GL_LUMINANCE4_ALPHA4:
        case GL_LUMINANCE6_ALPHA2:
        case GL_LUMINANCE8_ALPHA8:
        case GL_LUMINANCE12_ALPHA4:
        case GL_LUMINANCE12_ALPHA12:
        case GL_LUMINANCE16_ALPHA16:
            return VGPU98_OGL_BASE_LUM_ALPHA;
        case GL_INTENSITY:
        case GL_INTENSITY4:
        case GL_INTENSITY8:
        case GL_INTENSITY12:
        case GL_INTENSITY16:
            return VGPU98_OGL_BASE_INTENSITY;
        case 3:
        case GL_RGB:
        case GL_R3_G3_B2:
        case GL_RGB4:
        case GL_RGB5:
        case GL_RGB8:
        case GL_RGB10:
        case GL_RGB12:
        case GL_RGB16:
            return VGPU98_OGL_BASE_RGB;
        case 4:
        case GL_RGBA:
        case GL_RGBA2:
        case GL_RGBA4:
        case GL_RGB5_A1:
        case GL_RGBA8:
        case GL_RGB10_A2:
        case GL_RGBA12:
        case GL_RGBA16:
            return VGPU98_OGL_BASE_RGBA;
        default:
            return 0;
    }
}

/* The palette a target names (the active unit's bound texture's, a proxy's,
   the shared one) and its texture (NULL for the shared one); NULL for a
   target that names none. */
static ogl_palette_t *
target_palette(ogl_ctx_t *c, GLenum target, ogl_tex_t **tex, int proxies)
{
    ogl_tex_t *t = NULL;

    switch (target) {
        case GL_TEXTURE_1D:
        case GL_TEXTURE_2D:
            t = ogl_tex_bound(c, target);
            break;
        case GL_PROXY_TEXTURE_1D:
        case GL_PROXY_TEXTURE_2D:
            if (!proxies)
                return NULL;
            t = ogl_tex_proxy(c, target == GL_PROXY_TEXTURE_2D);
            break;
        case GL_SHARED_TEXTURE_PALETTE_EXT:
            *tex = NULL;
            return &c->gl->shared_pal;
        default:
            return NULL;
    }
    *tex = t;
    return t ? &t->pal : NULL;
}

static int
proxy_target(GLenum target)
{
    return (target == GL_PROXY_TEXTURE_1D) || (target == GL_PROXY_TEXTURE_2D);
}

/* The colour formats a table can be given in (a texture image's, its
   colour index format included: through the I_TO_* maps). */
static int
table_format(GLenum format, GLenum type)
{
    return (format != GL_STENCIL_INDEX) && (format != GL_DEPTH_COMPONENT) && ogl_format_comps(format) && ogl_type_ok(type) &&
           (type != GL_BITMAP);
}

/* Entries first to first + n - 1 from an unpacked image (n pixels), as
   texels of the palette's base format; zero without data. */
static void
fill(ogl_ctx_t *c, ogl_palette_t *pal, int first, int n, const ogl_pixels_t *p)
{
    int i;

    for (i = 0; i < n; i++) {
        float rgba[4];

        if (!p || !p->v || !p->comps) {
            pal->e[first + i] = 0;
            continue;
        }
        ogl_transfer_rgba(c->gl, p, (DWORD) i, rgba);
        pal->e[first + i] = ogl_texel(pal->base, rgba);
    }
}

void
ogl_colortable_apply(ogl_ctx_t *c, GLenum target, GLint ifmt, GLsizei width, const ogl_pixels_t *p)
{
    ogl_tex_t     *t   = NULL;
    ogl_palette_t *pal = target_palette(c, target, &t, 1);

    if (!pal)
        return;
    pal->n    = width;
    pal->ifmt = ifmt;
    pal->base = table_base(ifmt);
    if (proxy_target(target))
        return;
    fill(c, pal, 0, width, p);
    ogl_palette_send(c, t);
}

void
ogl_colorsubtable_apply(ogl_ctx_t *c, GLenum target, GLsizei start, GLsizei count, const ogl_pixels_t *p)
{
    ogl_tex_t     *t   = NULL;
    ogl_palette_t *pal = target_palette(c, target, &t, 0);

    if (!pal || (start < 0) || (count < 0) || ((int64_t) start + count > pal->n)) {
        ogl_error(c, pal ? GL_INVALID_VALUE : GL_INVALID_ENUM);
        return;
    }
    if (!count)
        return;
    fill(c, pal, start, count, p);
    ogl_palette_send(c, t);
}

void STDCALL
ogl_ColorTableEXT(GLenum target, GLenum internalformat, GLsizei width, GLenum format, GLenum type, const GLvoid *table)
{
    const int      proxy = proxy_target(target);
    ogl_tex_t     *t     = NULL;
    ogl_palette_t *pal;
    ogl_pixels_t   px;
    OGL_CTX();

    memset(&px, 0, sizeof(px));
    pal = target_palette(c, target, &t, 1);
    if (!pal || !table_base((GLint) internalformat) || !table_format(format, type)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    /* A power of two, 256 at most: a proxy's refusal is its width 0 */
    if ((width < 1) || (width & (width - 1)) || (width > VGPU98_OGL_PALETTE_MAX)) {
        if (proxy) {
            pal->n    = 0;
            pal->ifmt = 0;
            pal->base = 0;
            if ((width >= 1) && !(width & (width - 1)))
                return; /* too wide: no error for a proxy */
        }
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c))
        return;
    if (!proxy && table && !ogl_unpack(c, format, type, width, 1, table, &px))
        return;
    if (!proxy && ogl_dl_on(c)) {
        DWORD w5[5];
        int   only;

        w5[0] = target;
        w5[1] = internalformat;
        w5[2] = (DWORD) width;
        w5[3] = format;
        w5[4] = px.v ? (DWORD) px.comps : 0;
        only  = ogl_dl_data(c, OP_COLORTABLE, w5, 5, px.v, px.v ? (DWORD) width * (DWORD) px.comps * 4 : 0);
        if (only) {
            ogl_pixels_free(&px);
            return;
        }
    }
    ogl_colortable_apply(c, target, (GLint) internalformat, width, &px);
    ogl_pixels_free(&px);
}

void STDCALL
ogl_ColorSubTableEXT(GLenum target, GLsizei start, GLsizei count, GLenum format, GLenum type, const GLvoid *data)
{
    ogl_tex_t    *t = NULL;
    ogl_pixels_t  px;
    OGL_CTX();

    memset(&px, 0, sizeof(px));
    if (!target_palette(c, target, &t, 0) || !table_format(format, type)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (count < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c))
        return;
    if (count && data && !ogl_unpack(c, format, type, count, 1, data, &px))
        return;
    if (ogl_dl_on(c)) {
        DWORD w5[5];
        int   only;

        w5[0] = target;
        w5[1] = (DWORD) start;
        w5[2] = (DWORD) count;
        w5[3] = format;
        w5[4] = px.v ? (DWORD) px.comps : 0;
        only  = ogl_dl_data(c, OP_COLORSUBTABLE, w5, 5, px.v, px.v ? (DWORD) count * (DWORD) px.comps * 4 : 0);
        if (only) {
            ogl_pixels_free(&px);
            return;
        }
    }
    ogl_colorsubtable_apply(c, target, start, count, &px);
    ogl_pixels_free(&px);
}

void STDCALL
ogl_GetColorTableEXT(GLenum target, GLenum format, GLenum type, GLvoid *data)
{
    ogl_tex_t           *t = NULL;
    const ogl_palette_t *pal;
    float               *v;
    int                  i;
    OGL_CTX();

    pal = target_palette(c, target, &t, 0);
    if (!pal || !table_format(format, type) || (format == GL_COLOR_INDEX)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c) || !data || (pal->n < 1))
        return;
    v = ogl_alloc((DWORD) pal->n * 16);
    if (!v) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    for (i = 0; i < pal->n; i++)
        ogl_texel_rgba(pal->base, pal->e[i], &v[i * 4]);
    ogl_pack(&c->gl->client.pack, format, type, pal->n, 1, v, data);
    ogl_free(v);
}

/* A base format's components: bits 0-3 red, green, blue, alpha; 4
   luminance; 5 intensity. */
static int
components(int base)
{
    switch (base) {
        case VGPU98_OGL_BASE_ALPHA:
            return 8;
        case VGPU98_OGL_BASE_LUMINANCE:
            return 16;
        case VGPU98_OGL_BASE_LUM_ALPHA:
            return 16 | 8;
        case VGPU98_OGL_BASE_INTENSITY:
            return 32;
        case VGPU98_OGL_BASE_RGB:
            return 7;
        case VGPU98_OGL_BASE_RGBA:
            return 15;
        default:
            return 0;
    }
}

static int
table_param(ogl_ctx_t *c, GLenum target, GLenum pname, GLint *out)
{
    ogl_tex_t           *t = NULL;
    const ogl_palette_t *pal = target_palette(c, target, &t, 1);
    const int            comps = pal ? components(pal->base) : 0;

    if (!pal) {
        ogl_error(c, GL_INVALID_ENUM);
        return 0;
    }
    switch (pname) {
        case GL_COLOR_TABLE_FORMAT_EXT:
            *out = pal->ifmt;
            break;
        case GL_COLOR_TABLE_WIDTH_EXT:
            *out = pal->n;
            break;
        case GL_COLOR_TABLE_RED_SIZE_EXT:
            *out = (comps & 1) ? 8 : 0;
            break;
        case GL_COLOR_TABLE_GREEN_SIZE_EXT:
            *out = (comps & 2) ? 8 : 0;
            break;
        case GL_COLOR_TABLE_BLUE_SIZE_EXT:
            *out = (comps & 4) ? 8 : 0;
            break;
        case GL_COLOR_TABLE_ALPHA_SIZE_EXT:
            *out = (comps & 8) ? 8 : 0;
            break;
        case GL_COLOR_TABLE_LUMINANCE_SIZE_EXT:
            *out = (comps & 16) ? 8 : 0;
            break;
        case GL_COLOR_TABLE_INTENSITY_SIZE_EXT:
            *out = (comps & 32) ? 8 : 0;
            break;
        default:
            ogl_error(c, GL_INVALID_ENUM);
            return 0;
    }
    return !ogl_inside(c);
}

void STDCALL
ogl_GetColorTableParameterivEXT(GLenum target, GLenum pname, GLint *params)
{
    GLint v;
    OGL_CTX();

    if (table_param(c, target, pname, &v) && params)
        params[0] = v;
}

void STDCALL
ogl_GetColorTableParameterfvEXT(GLenum target, GLenum pname, GLfloat *params)
{
    GLint v;
    OGL_CTX();

    if (table_param(c, target, pname, &v) && params)
        params[0] = (GLfloat) v;
}

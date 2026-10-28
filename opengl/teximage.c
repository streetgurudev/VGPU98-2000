/*
 * Texture images (GL 1.1, sections 3.8.1-3.8.2 and 6.1.4): glTexImage,
 * glTexSubImage, glCopyTexImage, glCopyTexSubImage and glGetTexImage, the
 * proxy targets. Each level's image is unpacked and through the pixel
 * transfer path (pixel.c), then made RGBA8 with its base format's
 * components and its border (8 bits a component, as
 * glGetTexLevelParameter reports), and sent to the device, which keeps the
 * texels (TEXIMAGE). glGetTexImage reads them back (TEXREAD).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "glmath.h"
#include "lists.h"
#include "pixel.h"
#include "regs.h"
#include "texobj.h"

/* The texels one TEXIMAGE carries at most */
#define TEXELS_MAX (VGPU98_OGL_PACKET_BYTES / 4 - 1 - VGPU98_OGL_LEN_TEXIMAGE)

/* An internal format's base format (VGPU98_OGL_BASE_*), 0 for a name that
   is not one (GL 1.1, table 3.15). */
static int
base_of(GLint ifmt)
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
        /* EXT_paletted_texture's:
           kept as 8-bit indices, whatever the size asked for */
        case GL_COLOR_INDEX1_EXT:
        case GL_COLOR_INDEX2_EXT:
        case GL_COLOR_INDEX4_EXT:
        case GL_COLOR_INDEX8_EXT:
        case GL_COLOR_INDEX12_EXT:
        case GL_COLOR_INDEX16_EXT:
            return ogl_ext_on(OGL_EXT_PALETTED_TEXTURE) ? VGPU98_OGL_BASE_INDEX : 0;
        default:
            return 0;
    }
}

/* An index image's pixels (GL_COLOR_INDEX, unpacked) through the index
   transfer path (shift, offset, I_TO_I with MAP_COLOR) as 8-bit indices;
   NULL with the error recorded when there is no room. */
static uint8_t *
indices_of(ogl_ctx_t *c, const ogl_pixels_t *p)
{
    const DWORD n = (DWORD) p->w * (DWORD) p->h;
    uint8_t    *b = ogl_alloc(n ? n : 1);
    DWORD       i;

    if (!b) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return NULL;
    }
    for (i = 0; i < n; i++)
        b[i] = (uint8_t) (ogl_transfer_index(c->gl, p->v[i * (DWORD) p->comps], 0) & 0xff);
    return b;
}

/* The direct path for indices: unsigned bytes of GL_COLOR_INDEX with no
   index transfer, taken by the unpack state as they are (Quake 2's
   textures and cinematics). */
static int
indices_direct_ok(const ogl_ctx_t *c, GLenum format, GLenum type)
{
    const ogl_pixel_mode_t *pm = &c->gl->pixel_mode;

    return (format == GL_COLOR_INDEX) && (type == GL_UNSIGNED_BYTE) && !pm->index_shift && !pm->index_offset && !pm->map_color;
}

static uint8_t *
indices_direct(ogl_ctx_t *c, GLsizei w, GLsizei h, const GLubyte *data)
{
    const ogl_store_t *st  = &c->gl->client.unpack;
    const DWORD        row = ogl_row_bytes(st, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, w);
    uint8_t           *b;
    GLsizei            y;

    if ((uint64_t) w * (uint64_t) h > 0x1000000u) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return NULL;
    }
    b = ogl_alloc((DWORD) w * (DWORD) h);
    if (!b) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return NULL;
    }
    for (y = 0; y < h; y++)
        memcpy(b + (size_t) y * (size_t) w, data + (size_t) (st->skip_rows + y) * row + (size_t) st->skip_pixels, (size_t) w);
    return b;
}

/* A size GL takes: 2^n plus twice the border (0 allowed: no image). */
static int
size_ok(GLsizei s, GLint border)
{
    const GLsizei n = s - 2 * border;

    if (s == 0)
        return 1;
    return (n > 0) && !(n & (n - 1));
}

/* The colour formats an image of a texture can be given in */
static int
color_format(GLenum format)
{
    return (format != GL_STENCIL_INDEX) && (format != GL_DEPTH_COMPONENT) && ogl_format_comps(format);
}

static DWORD
byte_of(float v)
{
    return (DWORD) ogl_round((double) ogl_clampf(v, 0.0f, 1.0f) * 255.0);
}

/* A pixel's RGBA (after the transfer path) as a texel of the base format:
   luminance and intensity are red (GL 1.1, table 3.15), filled in as the
   device keeps them (vid_vgpu98_oglcmd.h). */
DWORD
ogl_texel(int base, const float *c)
{
    const DWORD r = byte_of(c[0]), g = byte_of(c[1]), b = byte_of(c[2]), a = byte_of(c[3]);

    switch (base) {
        case VGPU98_OGL_BASE_ALPHA:
            return a << 24;
        case VGPU98_OGL_BASE_LUMINANCE:
            return r | (r << 8) | (r << 16) | 0xff000000u;
        case VGPU98_OGL_BASE_LUM_ALPHA:
            return r | (r << 8) | (r << 16) | (a << 24);
        case VGPU98_OGL_BASE_INTENSITY:
            return r | (r << 8) | (r << 16) | (r << 24);
        case VGPU98_OGL_BASE_RGB:
            return r | (g << 8) | (b << 16) | 0xff000000u;
        default:
            return r | (g << 8) | (b << 16) | (a << 24);
    }
}

/* The direct path (audit 4, item 2): unsigned bytes of a colour format,
   with a transfer path that changes nothing, become texels byte for byte,
   without the float image (a texture's upload is most of a level load). */
static int
direct(const ogl_ctx_t *c, GLenum format, GLenum type)
{
    const ogl_pixel_mode_t *pm = &c->gl->pixel_mode;
    int                     k;

    if ((type != GL_UNSIGNED_BYTE) || pm->map_color)
        return 0;
    if ((format != GL_RGBA) && (format != GL_RGB) && (format != GL_LUMINANCE) && (format != GL_LUMINANCE_ALPHA) &&
        (format != GL_ALPHA))
        return 0;
    for (k = 0; k < 4; k++) {
        if ((pm->scale[k] != 1.0f) || (pm->bias[k] != 0.0f))
            return 0;
    }
    return 1;
}

static DWORD *
texels_direct(ogl_ctx_t *c, GLenum format, GLsizei w, GLsizei h, const GLubyte *data, int base)
{
    const ogl_store_t *st  = &c->gl->client.unpack;
    const int          n   = ogl_format_comps(format);
    const DWORD        row = ogl_row_bytes(st, format, GL_UNSIGNED_BYTE, w);
    DWORD             *t;
    GLsizei            x, y;

    /* At most 16M texels, counted in 64 bits, as ogl_unpack does (final
       audit, item 1) */
    if ((uint64_t) w * (uint64_t) h > 0x1000000u) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return NULL;
    }
    t = ogl_alloc((DWORD) w * (DWORD) h * 4);
    if (!t) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return NULL;
    }
    for (y = 0; y < h; y++) {
        const GLubyte *s = data + (size_t) (st->skip_rows + y) * row + (size_t) st->skip_pixels * (size_t) n;
        DWORD         *o = t + (size_t) y * (size_t) w;

        for (x = 0; x < w; x++, s += n) {
            DWORD r, g, b, a;

            switch (format) {
                case GL_RGBA:
                    r = s[0];
                    g = s[1];
                    b = s[2];
                    a = s[3];
                    break;
                case GL_RGB:
                    r = s[0];
                    g = s[1];
                    b = s[2];
                    a = 255;
                    break;
                case GL_LUMINANCE:
                    r = g = b = s[0];
                    a         = 255;
                    break;
                case GL_LUMINANCE_ALPHA:
                    r = g = b = s[0];
                    a         = s[1];
                    break;
                default: /* GL_ALPHA */
                    r = g = b = 0;
                    a         = s[0];
                    break;
            }
            /* As texel() makes them (luminance and intensity are red) */
            switch (base) {
                case VGPU98_OGL_BASE_ALPHA:
                    o[x] = a << 24;
                    break;
                case VGPU98_OGL_BASE_LUMINANCE:
                    o[x] = r | (r << 8) | (r << 16) | 0xff000000u;
                    break;
                case VGPU98_OGL_BASE_LUM_ALPHA:
                    o[x] = r | (r << 8) | (r << 16) | (a << 24);
                    break;
                case VGPU98_OGL_BASE_INTENSITY:
                    o[x] = r | (r << 8) | (r << 16) | (r << 24);
                    break;
                case VGPU98_OGL_BASE_RGB:
                    o[x] = r | (g << 8) | (b << 16) | 0xff000000u;
                    break;
                default:
                    o[x] = r | (g << 8) | (b << 16) | (a << 24);
                    break;
            }
        }
    }
    return t;
}

/* An image's pixels through the transfer path as texels (NULL with the
   error recorded when there is no room). */
static DWORD *
texels_of(ogl_ctx_t *c, const ogl_pixels_t *p, int base)
{
    const DWORD n = (DWORD) p->w * (DWORD) p->h;
    DWORD      *t = ogl_alloc(n * 4);
    DWORD       i;
    float       rgba[4];

    if (!t) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return NULL;
    }
    for (i = 0; i < n; i++) {
        ogl_transfer_rgba(c->gl, p, i, rgba);
        t[i] = ogl_texel(base, rgba);
    }
    return t;
}

/* k rows of w texels from row `row` on into a TEXIMAGE's payload: RGBA8
   dwords, or (an index level) bytes, four a dword, each row padded to a
   whole dword (vid_vgpu98_oglcmd.h). */
static void
put_rows(DWORD *dst, const void *texels, int index, GLsizei w, GLsizei row, GLsizei k)
{
    if (!index) {
        memcpy(dst, (const DWORD *) texels + (size_t) row * (size_t) w, (size_t) w * (size_t) k * 4);
        return;
    }
    {
        const DWORD rw = ((DWORD) w + 3) / 4;
        GLsizei     j;

        for (j = 0; j < k; j++) {
            dst[(size_t) j * rw + rw - 1] = 0;
            memcpy(&dst[(size_t) j * rw], (const uint8_t *) texels + (size_t) (row + j) * (size_t) w, (size_t) w);
        }
    }
}

/* Texels to the device: with `define` the level's size (lw by lh, border
   included) first; then the w by h texels (NULL: none) at x, y: RGBA8
   dwords, or bytes for an index level. */
static void
send_texels(ogl_ctx_t *c, ogl_tex_t *t, int dims, GLint level, int define, GLsizei lw, GLsizei lh, GLint x, GLint y,
            GLsizei w, GLsizei h, const void *texels)
{
    const ogl_level_t *l     = &t->level[level];
    const DWORD        ctl   = (DWORD) dims | ((DWORD) level << VGPU98_OGL_TEX_LEVEL_SHIFT) | (l->border ? VGPU98_OGL_TEX_BORDER : 0);
    const DWORD        dev   = ogl_tex_dev(c, t);
    const int          index = l->base == VGPU98_OGL_BASE_INDEX;
    const DWORD        rw    = index ? ((DWORD) w + 3) / 4 : (DWORD) w; /* a row's dwords */
    const DWORD        n     = rw * (DWORD) h;
    DWORD             *p;
    GLsizei            row, rows;

    /* No ID left in the group (16383 textures with images): it stays off
       the device, and is not drawn with (texobj.c). */
    if (!dev || !ogl_objects(c))
        return;
    if (define) {
        const int whole = texels && (n <= TEXELS_MAX);

        p    = ogl_cmd(c, VGPU98_OGL_TEXIMAGE, VGPU98_OGL_LEN_TEXIMAGE + (whole ? n : 0));
        p[0] = c->group->id;
        p[1] = dev;
        p[2] = ctl | VGPU98_OGL_TEX_DEFINE | (whole ? VGPU98_OGL_TEX_TEXELS : 0);
        p[3] = (DWORD) lw | ((DWORD) lh << 16);
        p[4] = 0;
        p[5] = (DWORD) l->base;
        if (whole)
            put_rows(&p[VGPU98_OGL_LEN_TEXIMAGE], texels, index, w, 0, h);
        if (whole || !texels)
            return;
    }
    if (!n || !texels)
        return;
    /* Rows a packet, as many as fit */
    rows = (GLsizei) (TEXELS_MAX / rw);
    for (row = 0; row < h; row += rows) {
        const GLsizei k = (h - row < rows) ? h - row : rows;

        p    = ogl_cmd(c, VGPU98_OGL_TEXIMAGE, VGPU98_OGL_LEN_TEXIMAGE + rw * (DWORD) k);
        p[0] = c->group->id;
        p[1] = dev;
        p[2] = ctl;
        p[3] = (DWORD) w | ((DWORD) k << 16);
        p[4] = ((DWORD) x & 0xffff) | ((DWORD) (y + row) << 16);
        p[5] = (DWORD) l->base;
        put_rows(&p[VGPU98_OGL_LEN_TEXIMAGE], texels, index, w, row, k);
    }
}

/* --- glTexImage ------------------------------------------------------------------------------ */

/* A level defined: its size checked (a proxy's taken or refused without
   an error), then its texels (NULL: undefined contents). */
static void
tex_define(ogl_ctx_t *c, int dims, GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border,
           const void *texels)
{
    const int proxy = (target == GL_PROXY_TEXTURE_1D) || (target == GL_PROXY_TEXTURE_2D);
    const int base  = base_of(ifmt);
    ogl_tex_t *t;
    int        fits;

    if (((dims == 1) && (target != GL_TEXTURE_1D) && (target != GL_PROXY_TEXTURE_1D)) ||
        ((dims == 2) && (target != GL_TEXTURE_2D) && (target != GL_PROXY_TEXTURE_2D))) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if ((level < 0) || (level > OGL_MAX_TEXTURE_LOG2) || !base || ((border != 0) && (border != 1)) || (w < 0) || (h < 0) ||
        !size_ok(w, border) || ((dims == 2) && !size_ok(h, border))) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c))
        return;
    /* The largest image; a proxy says whether it would fit. */
    fits = (w - 2 * border <= OGL_MAX_TEXTURE) && ((dims == 1) || (h - 2 * border <= OGL_MAX_TEXTURE));
    if (proxy) {
        ogl_tex_t   *pt = ogl_tex_proxy(c, dims == 2);
        ogl_level_t *l;

        if (!pt) {
            ogl_error(c, GL_OUT_OF_MEMORY);
            return;
        }
        l = &pt->level[level];
        memset(l, 0, sizeof(*l));
        if (fits) {
            l->w      = w;
            l->h      = (dims == 1) ? 1 : h;
            l->border = border;
            l->ifmt   = ifmt;
            l->base   = base;
        }
        return;
    }
    if (!fits) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    t = ogl_tex_bound(c, target);
    if (!t)
        return;
    {
        ogl_level_t *l = &t->level[level];

        if (!w || ((dims == 2) && !h)) {
            /* No image: the level undefined (the texture incomplete). */
            memset(l, 0, sizeof(*l));
            ogl_regs_texture(c);
            return;
        }
        l->w      = w;
        l->h      = (dims == 1) ? 1 : h;
        l->border = border;
        l->ifmt   = ifmt;
        l->base   = base;
        send_texels(c, t, dims, level, 1, l->w, l->h, 0, 0, l->w, l->h, texels);
    }
    ogl_regs_texture(c); /* completeness may have changed */
}

void
ogl_teximage_apply(ogl_ctx_t *c, int dims, GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border,
                   const ogl_pixels_t *p)
{
    void *texels = NULL;
    int   base   = base_of(ifmt);

    if (dims == 1)
        h = 1;
    /* The texels only for a level that will be defined (an index level's
       as indices). */
    if (p && p->v && p->comps && base && (w > 0) && (h > 0) && (target != GL_PROXY_TEXTURE_1D) &&
        (target != GL_PROXY_TEXTURE_2D) && !c->gl->in_begin) {
        texels = (base == VGPU98_OGL_BASE_INDEX) ? (void *) indices_of(c, p) : (void *) texels_of(c, p, base);
        if (!texels)
            return;
    }
    tex_define(c, dims, target, level, ifmt, w, h, border, texels);
    ogl_free(texels);
}

/* glTexImage1D and 2D: unpacked now (compiled so), defined when run. A
   proxy's is never compiled (GL runs it at once). */
static void
tex_image(int dims, GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type,
          const GLvoid *pixels)
{
    const int    proxy = (target == GL_PROXY_TEXTURE_1D) || (target == GL_PROXY_TEXTURE_2D);
    ogl_pixels_t px;
    OGL_CTX();

    memset(&px, 0, sizeof(px));
    if (!color_format(format) || !ogl_type_ok(type) || ((type == GL_BITMAP) && (format != GL_COLOR_INDEX))) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    /* An index level takes GL_COLOR_INDEX images alone */
    if ((base_of(ifmt) == VGPU98_OGL_BASE_INDEX) && (format != GL_COLOR_INDEX)) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    if (pixels && !proxy && (w > 0) && (h > 0) && (base_of(ifmt) == VGPU98_OGL_BASE_INDEX) && !ogl_dl_on(c) &&
        !c->gl->in_begin && indices_direct_ok(c, format, type)) {
        uint8_t *idx = indices_direct(c, w, (dims == 1) ? 1 : h, pixels);

        if (idx)
            tex_define(c, dims, target, level, ifmt, w, (dims == 1) ? 1 : h, border, idx);
        ogl_free(idx);
        return;
    }
    if (pixels && !proxy && (w > 0) && (h > 0) && base_of(ifmt) && (base_of(ifmt) != VGPU98_OGL_BASE_INDEX) &&
        !ogl_dl_on(c) && !c->gl->in_begin && direct(c, format, type)) {
        DWORD *texels = texels_direct(c, format, w, (dims == 1) ? 1 : h, pixels, base_of(ifmt));

        if (texels)
            tex_define(c, dims, target, level, ifmt, w, (dims == 1) ? 1 : h, border, texels);
        ogl_free(texels);
        return;
    }
    if (pixels && !proxy && (w > 0) && (h > 0) && !ogl_unpack(c, format, type, w, h, pixels, &px))
        return;
    if (!proxy && ogl_dl_on(c)) {
        DWORD w9[9];
        int   only;

        w9[0] = (DWORD) dims;
        w9[1] = target;
        w9[2] = (DWORD) level;
        w9[3] = (DWORD) ifmt;
        w9[4] = (DWORD) w;
        w9[5] = (DWORD) h;
        w9[6] = (DWORD) border;
        w9[7] = format;
        w9[8] = px.v ? (DWORD) px.comps : 0;
        only  = ogl_dl_data(c, OP_TEXIMAGE, w9, 9, px.v, px.v ? (DWORD) w * (DWORD) h * (DWORD) px.comps * 4 : 0);
        if (only) {
            ogl_pixels_free(&px);
            return;
        }
    }
    ogl_teximage_apply(c, dims, target, level, ifmt, w, h, border, &px);
    ogl_pixels_free(&px);
}

void STDCALL
ogl_TexImage1D(GLenum target, GLint level, GLint ifmt, GLsizei width, GLint border, GLenum format, GLenum type,
               const GLvoid *pixels)
{
    tex_image(1, target, level, ifmt, width, 1, border, format, type, pixels);
}

void STDCALL
ogl_TexImage2D(GLenum target, GLint level, GLint ifmt, GLsizei width, GLsizei height, GLint border, GLenum format,
               GLenum type, const GLvoid *pixels)
{
    tex_image(2, target, level, ifmt, width, height, border, format, type, pixels);
}

/* --- glTexSubImage ---------------------------------------------------------------------------- */

/* The level's texels at xoff, yoff (GL's: the border at -1) replaced: from
   an unpacked image (p), a read-back's RGBA (rgba), unsigned bytes of a
   colour format (bytes, the direct path) or, for an index level, unsigned
   byte indices as given (ibytes, the direct path). */
static void
tex_sub(ogl_ctx_t *c, int dims, GLenum target, GLint level, GLint xoff, GLint yoff, GLsizei w, GLsizei h,
        const ogl_pixels_t *p, const float *rgba, const GLubyte *bytes, GLenum format, const GLubyte *ibytes)
{
    ogl_tex_t   *t;
    ogl_level_t *l;
    DWORD       *texels;
    DWORD        i, n;

    if (((dims == 1) && (target != GL_TEXTURE_1D)) || ((dims == 2) && (target != GL_TEXTURE_2D))) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if ((level < 0) || (level > OGL_MAX_TEXTURE_LOG2) || (w < 0) || (h < 0)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c))
        return;
    t = ogl_tex_bound(c, target);
    l = t ? &t->level[level] : NULL;
    if (!l || !l->base) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    if ((xoff < -l->border) || ((int64_t) xoff + w > l->w - l->border) ||
        ((dims == 2) && ((yoff < -l->border) || ((int64_t) yoff + h > l->h - l->border)))) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    /* An index level takes GL_COLOR_INDEX images alone; nothing is copied
       into one from the colour buffer */
    if ((l->base == VGPU98_OGL_BASE_INDEX) && (rgba || (!ibytes && (!p || (p->format != GL_COLOR_INDEX))))) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    n = (DWORD) w * (DWORD) h;
    if (!n)
        return;
    if (l->base == VGPU98_OGL_BASE_INDEX) {
        uint8_t *idx = ibytes ? indices_direct(c, w, h, ibytes) : (p && p->v) ? indices_of(c, p) : ogl_alloc(n);

        if (idx)
            send_texels(c, t, dims, level, 0, l->w, l->h, xoff + l->border, (dims == 2) ? yoff + l->border : 0, w, h, idx);
        ogl_free(idx);
        return;
    }
    if (bytes) {
        texels = texels_direct(c, format, w, h, bytes, l->base);
        if (texels)
            send_texels(c, t, dims, level, 0, l->w, l->h, xoff + l->border, (dims == 2) ? yoff + l->border : 0, w, h, texels);
        ogl_free(texels);
        return;
    }
    texels = ogl_alloc(n * 4);
    if (!texels) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    for (i = 0; i < n; i++) {
        float c4[4];

        if (rgba)
            memcpy(c4, &rgba[i * 4], sizeof(c4));
        else if (p && p->v && p->comps)
            ogl_transfer_rgba(c->gl, p, i, c4);
        else
            c4[0] = c4[1] = c4[2] = c4[3] = 0.0f;
        texels[i] = ogl_texel(l->base, c4);
    }
    send_texels(c, t, dims, level, 0, l->w, l->h, xoff + l->border, (dims == 2) ? yoff + l->border : 0, w, h, texels);
    ogl_free(texels);
}

void
ogl_texsubimage_apply(ogl_ctx_t *c, int dims, GLenum target, GLint level, GLint xoff, GLint yoff, const ogl_pixels_t *p)
{
    tex_sub(c, dims, target, level, xoff, yoff, p->w, (dims == 1) ? 1 : p->h, p, NULL, NULL, 0, NULL);
}

/* Is the active unit's level of the target an index level? */
static int
index_level(ogl_ctx_t *c, GLenum target, GLint level)
{
    const ogl_tex_t *t;

    if (((target != GL_TEXTURE_1D) && (target != GL_TEXTURE_2D)) || (level < 0) || (level > OGL_MAX_TEXTURE_LOG2))
        return 0;
    t = ogl_tex_bound(c, target);
    return t && (t->level[level].base == VGPU98_OGL_BASE_INDEX);
}

static void
tex_subimage(int dims, GLenum target, GLint level, GLint xoff, GLint yoff, GLsizei w, GLsizei h, GLenum format, GLenum type,
             const GLvoid *pixels)
{
    ogl_pixels_t px;
    OGL_CTX();

    memset(&px, 0, sizeof(px));
    if (!color_format(format) || !ogl_type_ok(type) || ((type == GL_BITMAP) && (format != GL_COLOR_INDEX))) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (pixels && (w > 0) && (h > 0) && !ogl_dl_on(c) && indices_direct_ok(c, format, type) && index_level(c, target, level)) {
        tex_sub(c, dims, target, level, xoff, yoff, w, (dims == 1) ? 1 : h, NULL, NULL, NULL, 0, pixels);
        return;
    }
    if (pixels && (w > 0) && (h > 0) && !ogl_dl_on(c) && direct(c, format, type)) {
        tex_sub(c, dims, target, level, xoff, yoff, w, (dims == 1) ? 1 : h, NULL, NULL, pixels, format, NULL);
        return;
    }
    if ((w > 0) && (h > 0) && !ogl_unpack(c, format, type, w, h, pixels, &px))
        return;
    px.format = format; /* also without data (an index level checks it) */
    px.w      = w;
    px.h      = h;
    if (ogl_dl_on(c)) {
        DWORD w9[9];
        int   only;

        w9[0] = (DWORD) dims;
        w9[1] = target;
        w9[2] = (DWORD) level;
        w9[3] = (DWORD) xoff;
        w9[4] = (DWORD) yoff;
        w9[5] = (DWORD) w;
        w9[6] = (DWORD) h;
        w9[7] = format;
        w9[8] = px.v ? (DWORD) px.comps : 0;
        only  = ogl_dl_data(c, OP_TEXSUBIMAGE, w9, 9, px.v, px.v ? (DWORD) w * (DWORD) h * (DWORD) px.comps * 4 : 0);
        if (only) {
            ogl_pixels_free(&px);
            return;
        }
    }
    tex_sub(c, dims, target, level, xoff, yoff, w, h, &px, NULL, NULL, 0, NULL);
    ogl_pixels_free(&px);
}

void STDCALL
ogl_TexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type, const GLvoid *pixels)
{
    tex_subimage(1, target, level, xoffset, 0, width, 1, format, type, pixels);
}

void STDCALL
ogl_TexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format,
                  GLenum type, const GLvoid *pixels)
{
    tex_subimage(2, target, level, xoffset, yoffset, width, height, format, type, pixels);
}

/* --- glCopyTexImage, glCopyTexSubImage --------------------------------------------------------- */

/* The read buffer's rectangle as RGBA, through the transfer path (once:
   GL 1.1 copies as glCopyPixels with GL_COLOR does). NULL on an error. */
static float *
read_rgba(ogl_ctx_t *c, GLint x, GLint y, GLsizei w, GLsizei h)
{
    float       *v = ogl_read_rect(c, c->reg[VGPU98_OGL_R_READ_BUFFER] ? VGPU98_OGL_READ_BACK : VGPU98_OGL_READ_FRONT, x, y, w, h);
    ogl_pixels_t p;
    DWORD        i;

    if (!v)
        return NULL;
    p.format = GL_RGBA;
    p.w      = w;
    p.h      = h;
    p.comps  = 4;
    p.v      = v;
    p.owned  = 0;
    for (i = 0; i < (DWORD) w * (DWORD) h; i++) {
        float t[4];

        ogl_transfer_rgba(c->gl, &p, i, t);
        memcpy(&v[i * 4], t, sizeof(t));
    }
    return v;
}

static void
copy_tex_image(int dims, GLenum target, GLint level, GLenum ifmt, GLint x, GLint y, GLsizei w, GLsizei h, GLint border)
{
    const int base = base_of((GLint) ifmt);
    float    *v;
    DWORD    *texels;
    DWORD     i;
    OGL_CTX();

    if (dims == 1)
        OGL_DL(OP_COPYTEXIMAGE1D, target, (DWORD) level, ifmt, (DWORD) x, (DWORD) y, (DWORD) w, (DWORD) border);
    else
        OGL_DL(OP_COPYTEXIMAGE2D, target, (DWORD) level, ifmt, (DWORD) x, (DWORD) y, (DWORD) w, (DWORD) h, (DWORD) border);
    if (dims == 1)
        h = 1;
    /* No proxies here (GL 1.1). */
    if (((dims == 1) && (target != GL_TEXTURE_1D)) || ((dims == 2) && (target != GL_TEXTURE_2D))) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (!base || (w < 0) || (h < 0) || ((border != 0) && (border != 1)) || !size_ok(w, border) ||
        ((dims == 2) && !size_ok(h, border))) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    /* No index level from the colour buffer */
    if (base == VGPU98_OGL_BASE_INDEX) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    if (ogl_inside(c))
        return;
    if (!w || !h) {
        tex_define(c, dims, target, level, (GLint) ifmt, w, h, border, NULL);
        return;
    }
    v = read_rgba(c, x, y, w, h);
    if (!v)
        return;
    texels = ogl_alloc((DWORD) w * (DWORD) h * 4);
    if (!texels) {
        ogl_free(v);
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    for (i = 0; i < (DWORD) w * (DWORD) h; i++)
        texels[i] = ogl_texel(base, &v[i * 4]);
    tex_define(c, dims, target, level, (GLint) ifmt, w, h, border, texels);
    ogl_free(texels);
    ogl_free(v);
}

void STDCALL
ogl_CopyTexImage1D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border)
{
    copy_tex_image(1, target, level, internalformat, x, y, width, 1, border);
}

void STDCALL
ogl_CopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height,
                   GLint border)
{
    copy_tex_image(2, target, level, internalformat, x, y, width, height, border);
}

static void
copy_tex_sub(int dims, GLenum target, GLint level, GLint xoff, GLint yoff, GLint x, GLint y, GLsizei w, GLsizei h)
{
    float *v;
    OGL_CTX();

    if (dims == 1)
        OGL_DL(OP_COPYTEXSUB1D, target, (DWORD) level, (DWORD) xoff, (DWORD) x, (DWORD) y, (DWORD) w);
    else
        OGL_DL(OP_COPYTEXSUB2D, target, (DWORD) level, (DWORD) xoff, (DWORD) yoff, (DWORD) x, (DWORD) y, (DWORD) w, (DWORD) h);
    if (dims == 1)
        h = 1;
    if ((w <= 0) || (h <= 0) || c->gl->in_begin) {
        /* The checks alone (an empty rectangle copies nothing). */
        tex_sub(c, dims, target, level, xoff, yoff, (w < 0) ? w : 0, (h < 0) ? h : 0, NULL, NULL, NULL, 0, NULL);
        return;
    }
    v = read_rgba(c, x, y, w, h);
    if (!v)
        return;
    tex_sub(c, dims, target, level, xoff, yoff, w, h, NULL, v, NULL, 0, NULL);
    ogl_free(v);
}

void STDCALL
ogl_CopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width)
{
    copy_tex_sub(1, target, level, xoffset, 0, x, y, width, 1);
}

void STDCALL
ogl_CopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width,
                      GLsizei height)
{
    copy_tex_sub(2, target, level, xoffset, yoffset, x, y, width, height);
}

/* --- glGetTexImage ------------------------------------------------------------------------------- */

/* A texel as GL gives it back: luminance and intensity as red (GL 1.1,
   6.1.4), no pixel transfer. */
void
ogl_texel_rgba(int base, DWORD t, float *c)
{
    const float r = (float) (t & 0xff) / 255.0f, g = (float) ((t >> 8) & 0xff) / 255.0f;
    const float b = (float) ((t >> 16) & 0xff) / 255.0f, a = (float) (t >> 24) / 255.0f;

    c[0] = c[1] = c[2] = 0.0f;
    c[3]               = 1.0f;
    switch (base) {
        case VGPU98_OGL_BASE_ALPHA:
            c[3] = a;
            break;
        case VGPU98_OGL_BASE_LUMINANCE:
        case VGPU98_OGL_BASE_INTENSITY:
            c[0] = r;
            break;
        case VGPU98_OGL_BASE_LUM_ALPHA:
            c[0] = r;
            c[3] = a;
            break;
        case VGPU98_OGL_BASE_RGB:
            c[0] = r;
            c[1] = g;
            c[2] = b;
            break;
        default:
            c[0] = r;
            c[1] = g;
            c[2] = b;
            c[3] = a;
            break;
    }
}

void STDCALL
ogl_GetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels)
{
    ogl_tex_t         *t;
    const ogl_level_t *l;
    float             *v;
    uint8_t           *st;
    DWORD              offset;
    GLsizei            row, y, x;
    /* An index level's indices themselves (EXT_paletted_texture) */
    const int          indices = format == GL_COLOR_INDEX;
    OGL_CTX();

    if (((target != GL_TEXTURE_1D) && (target != GL_TEXTURE_2D)) || !color_format(format) ||
        (indices && !ogl_ext_on(OGL_EXT_PALETTED_TEXTURE)) || !ogl_type_ok(type) || (type == GL_BITMAP)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if ((level < 0) || (level > OGL_MAX_TEXTURE_LOG2)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c) || !pixels)
        return;
    t = ogl_tex_bound(c, target);
    l = t ? &t->level[level] : NULL;
    if (indices && l && (l->base != VGPU98_OGL_BASE_INDEX)) {
        ogl_error(c, GL_INVALID_OPERATION); /* no indices in a colour level */
        return;
    }
    if (!l || !l->base || !t->dev || !ogl_objects(c))
        return;
    v = ogl_alloc((DWORD) l->w * (DWORD) l->h * 16);
    if (!v) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    st = ogl_staging(&offset);
    /* The level's rows through the staging surface, as many as it holds */
    for (row = 0; st && (row < l->h); row += OGL_STAGING_ROWS) {
        const GLsizei k = (l->h - row < OGL_STAGING_ROWS) ? l->h - row : OGL_STAGING_ROWS;
        DWORD        *p = ogl_cmd(c, VGPU98_OGL_TEXREAD, VGPU98_OGL_LEN_TEXREAD);

        p[0] = c->group->id;
        p[1] = t->dev;
        p[2] = (DWORD) level;
        p[3] = offset;
        p[4] = OGL_STAGING_STRIDE;
        p[5] = (DWORD) row | ((DWORD) k << 16);
        ogl_finish(c);
        for (y = 0; y < k; y++) {
            for (x = 0; x < l->w; x++) {
                float *out = &v[((size_t) (row + y) * (size_t) l->w + (size_t) x) * 4];
                DWORD  tx;

                if (l->base == VGPU98_OGL_BASE_INDEX) {
                    /* An index, a byte each: its colour through the palette
                       it draws with */
                    const ogl_palette_t *pal = ogl_palette_of(c, t);
                    const DWORD          i   = st[(size_t) y * OGL_STAGING_STRIDE + (size_t) x];

                    if (indices)
                        v[(size_t) (row + y) * (size_t) l->w + (size_t) x] = (float) i; /* one value a pixel */
                    else
                        ogl_texel_rgba(pal->base, (pal->n > 0) ? pal->e[i & (DWORD) (pal->n - 1)] : 0, out);
                    continue;
                }
                memcpy(&tx, st + (size_t) y * OGL_STAGING_STRIDE + (size_t) x * 4, 4);
                ogl_texel_rgba(l->base, tx, out);
            }
        }
    }
    ogl_pack(&c->gl->client.pack, format, type, l->w, l->h, v, pixels);
    ogl_free(v);
}

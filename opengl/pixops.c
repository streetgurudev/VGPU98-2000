/*
 * Pixel rectangles (GL 1.1, sections 3.6.3-3.6.4, 3.7 and 4.3): glDrawPixels,
 * glBitmap, glReadPixels and glCopyPixels.
 *
 * - Drawn images go through the pixel transfer path here, then to the
 *   device as PIXELS: their fragments at the raster position, zoomed,
 *   through the per-fragment operations. In the selection and feedback
 *   render modes they give their token and hit instead (raster.c); glBitmap
 *   moves the raster position in every mode.
 * - Read-backs: the device writes the rectangle into the staging surface in
 *   video memory (READ), a band of rows at a time; it is converted here,
 *   through the transfer path, and packed.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "glmath.h"
#include "lists.h"
#include "pixel.h"
#include "raster.h"

/* A PIXELS packet's data, at most (room left for unit 1's coordinates) */
#define PIXELS_MAX (VGPU98_OGL_PACKET_BYTES / 4 - 1 - VGPU98_OGL_LEN_PIXELS - 4)

static DWORD
fbits(float f)
{
    DWORD v;

    memcpy(&v, &f, 4);
    return v;
}

/* --- Drawing ------------------------------------------------------------------------------------- */

/* An image's fragments to the device: w by h pixels of `data` (a dword
   each, or a byte each for stencil and bitmaps, each row then a whole
   number of dwords), at x, y in window coordinates, zoomed; as many
   packets as it takes, each a rectangle of it at its own place. */
static void
send_pixels(ogl_ctx_t *c, DWORD kind, GLsizei w, GLsizei h, float x, float y, float zx, float zy, const void *data)
{
    const ogl_state_t *s     = c->gl;
    const int          bytes = (kind == VGPU98_OGL_PIXELS_STENCIL) || (kind == VGPU98_OGL_PIXELS_BITMAP);
    const DWORD        row_w = bytes ? ((DWORD) w + 3) / 4 : (DWORD) w; /* the source's row, in dwords */
    const GLsizei      cw    = (GLsizei) ((row_w <= PIXELS_MAX) ? (DWORD) w : (bytes ? 4 * PIXELS_MAX : PIXELS_MAX));
    const DWORD        crow  = bytes ? ((DWORD) cw + 3) / 4 : (DWORD) cw;
    const GLsizei      ch    = (GLsizei) (PIXELS_MAX / crow);
    GLsizei            x0, y0, j;
    DWORD              head;

    if (!w || !h || !ogl_objects(c))
        return;
    if (c->reg[VGPU98_OGL_R_DRAW_BUFFERS] & 1u)
        ogl_front_load(c);
    ogl_sync(c);
    /* Unit 1's raster coordinates while it draws with a texture (2.6) */
    head = VGPU98_OGL_LEN_PIXELS + (c->reg[VGPU98_OGL_R_UNIT1 + VGPU98_OGL_U_TEXTURE_ID] ? 4 : 0);
    for (y0 = 0; y0 < h; y0 += ch) {
        for (x0 = 0; x0 < w; x0 += cw) {
            const GLsizei kw = (w - x0 < cw) ? w - x0 : cw;
            const GLsizei kh = (h - y0 < ch) ? h - y0 : ch;
            const DWORD   kr = bytes ? ((DWORD) kw + 3) / 4 : (DWORD) kw;
            DWORD        *p  = ogl_cmd(c, VGPU98_OGL_PIXELS, head + kr * (DWORD) kh);
            int           k;

            if (!p)
                return;
            p[0] = c->id;
            p[1] = kind | (head > VGPU98_OGL_LEN_PIXELS ? VGPU98_OGL_PIXELS_TEX1 : 0);
            p[2] = (DWORD) kw | ((DWORD) kh << 16);
            p[3] = fbits(x + zx * (float) x0);
            p[4] = fbits(y + zy * (float) y0);
            p[5] = fbits(s->current.raster_pos[2]);
            p[6] = fbits(zx);
            p[7] = fbits(zy);
            for (k = 0; k < 4; k++) {
                p[8 + k]  = fbits(s->current.raster_color[k]);
                p[12 + k] = fbits(s->current.raster_texcoord[0][k]);
                if (head > VGPU98_OGL_LEN_PIXELS)
                    p[17 + k] = fbits(s->current.raster_texcoord[1][k]); /* PIXELS_TEX1 */
            }
            p[16] = fbits(s->current.raster_distance);
            p += head;
            for (j = 0; j < kh; j++) {
                if (bytes) {
                    const uint8_t *src = (const uint8_t *) data + (size_t) (y0 + j) * row_w * 4 + (size_t) x0;

                    p[(size_t) j * kr + kr - 1] = 0;
                    memcpy(&p[(size_t) j * kr], src, (size_t) kw);
                } else
                    memcpy(&p[(size_t) j * kr], (const DWORD *) data + (size_t) (y0 + j) * row_w + (size_t) x0, (size_t) kw * 4);
            }
        }
    }
    /* Drawn into the front buffer: shown soon. */
    if ((c->reg[VGPU98_OGL_R_DRAW_BUFFERS] & 1u) && c->win) {
        c->win->front_drawn = 1;
        ogl_front_show(c, 0);
    }
}

static DWORD
byte_of(float v)
{
    return (DWORD) ogl_round((double) ogl_clampf(v, 0.0f, 1.0f) * 255.0);
}

/* An image (unpacked, not yet transferred) drawn at the raster position:
   colour as RGBA8, depths as floats, stencil indices as bytes. */
static void
draw_image(ogl_ctx_t *c, const ogl_pixels_t *p)
{
    const ogl_state_t *s = c->gl;
    const DWORD        n = (DWORD) p->w * (DWORD) p->h;
    DWORD              i, kind;
    void              *data;

    if (!n || !p->v)
        return;
    if ((p->format == GL_STENCIL_INDEX) && !ogl_format_bits(c->format, OGL_BITS_STENCIL)) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    if ((p->format == GL_DEPTH_COMPONENT) && !ogl_format_bits(c->format, OGL_BITS_DEPTH)) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    if (p->format == GL_STENCIL_INDEX) {
        const DWORD row = ((DWORD) p->w + 3) & ~3u;
        uint8_t    *b   = ogl_alloc(row * (DWORD) p->h);
        GLsizei     x, y;

        if (!b) {
            ogl_error(c, GL_OUT_OF_MEMORY);
            return;
        }
        for (y = 0; y < p->h; y++) {
            for (x = 0; x < p->w; x++)
                b[(size_t) y * row + (size_t) x] =
                    (uint8_t) ogl_transfer_index(s, p->v[(size_t) y * (size_t) p->w + (size_t) x], 1);
        }
        data = b;
        kind = VGPU98_OGL_PIXELS_STENCIL;
    } else if (p->format == GL_DEPTH_COMPONENT) {
        float *f = ogl_alloc(n * 4);

        if (!f) {
            ogl_error(c, GL_OUT_OF_MEMORY);
            return;
        }
        for (i = 0; i < n; i++)
            f[i] = ogl_transfer_depth(s, p->v[i]);
        data = f;
        kind = VGPU98_OGL_PIXELS_DEPTH;
    } else {
        DWORD *t = ogl_alloc(n * 4);
        float  rgba[4];

        if (!t) {
            ogl_error(c, GL_OUT_OF_MEMORY);
            return;
        }
        for (i = 0; i < n; i++) {
            ogl_transfer_rgba(s, p, i, rgba);
            t[i] = byte_of(rgba[0]) | (byte_of(rgba[1]) << 8) | (byte_of(rgba[2]) << 16) | (byte_of(rgba[3]) << 24);
        }
        data = t;
        kind = VGPU98_OGL_PIXELS_COLOR;
    }
    send_pixels(c, kind, p->w, p->h, s->current.raster_pos[0], s->current.raster_pos[1], s->pixel_mode.zoom_x,
                s->pixel_mode.zoom_y, data);
    ogl_free(data);
}

void
ogl_drawpixels_apply(ogl_ctx_t *c, const ogl_pixels_t *p)
{
    if (ogl_inside(c) || !c->gl->current.raster_valid)
        return;
    if (c->gl->render_mode != GL_RENDER) {
        ogl_raster_token(c, GL_DRAW_PIXEL_TOKEN);
        return;
    }
    draw_image(c, p);
}

void STDCALL
ogl_DrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    ogl_pixels_t px;
    OGL_CTX();

    /* Unpacked now (compiled so); drawn when run. */
    if (!ogl_unpack(c, format, type, width, height, pixels, &px))
        return;
    if (ogl_dl_on(c)) {
        DWORD w4[4];
        int   only;

        w4[0] = (DWORD) width;
        w4[1] = (DWORD) height;
        w4[2] = format;
        w4[3] = (DWORD) px.comps;
        only  = ogl_dl_data(c, OP_DRAWPIXELS, w4, 4, px.v, px.v ? (DWORD) width * (DWORD) height * (DWORD) px.comps * 4 : 0);
        if (only) {
            ogl_pixels_free(&px);
            return;
        }
    }
    ogl_drawpixels_apply(c, &px);
    ogl_pixels_free(&px);
}

void
ogl_bitmap_apply(ogl_ctx_t *c, GLsizei w, GLsizei h, float xorig, float yorig, float xmove, float ymove, const uint8_t *bits)
{
    ogl_state_t *s = c->gl;

    if (ogl_inside(c) || !s->current.raster_valid)
        return;
    if (s->render_mode != GL_RENDER)
        ogl_raster_token(c, GL_BITMAP_TOKEN);
    else if (w && h && bits) {
        /* Its rows as whole dwords for the device */
        const DWORD row = ((DWORD) w + 3) & ~3u;
        uint8_t    *b   = ogl_alloc(row * (DWORD) h);
        GLsizei     y;

        if (!b) {
            ogl_error(c, GL_OUT_OF_MEMORY);
            return;
        }
        for (y = 0; y < h; y++)
            memcpy(b + (size_t) y * row, bits + (size_t) y * (size_t) w, (size_t) w);
        send_pixels(c, VGPU98_OGL_PIXELS_BITMAP, w, h, s->current.raster_pos[0] - xorig, s->current.raster_pos[1] - yorig, 1.0f,
                    1.0f, b);
        ogl_free(b);
    }
    s->current.raster_pos[0] += xmove;
    s->current.raster_pos[1] += ymove;
}

void STDCALL
ogl_Bitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove, const GLubyte *bitmap)
{
    uint8_t *bits = NULL;
    OGL_CTX();

    if ((width < 0) || (height < 0)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if ((uint64_t) width * (uint64_t) height > 0x1000000u) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    /* Unpacked now, one byte a pixel, the first row first. */
    if (width && height && bitmap) {
        bits = ogl_alloc((DWORD) width * (DWORD) height);
        if (!bits) {
            ogl_error(c, GL_OUT_OF_MEMORY);
            return;
        }
        ogl_unpack_bits(&c->gl->client.unpack, width, height, bitmap, bits);
    }
    if (ogl_dl_on(c)) {
        DWORD w6[6];
        int   only;

        w6[0] = (DWORD) (bits ? width : 0);
        w6[1] = (DWORD) (bits ? height : 0);
        w6[2] = ogl_fw(xorig);
        w6[3] = ogl_fw(yorig);
        w6[4] = ogl_fw(xmove);
        w6[5] = ogl_fw(ymove);
        only  = ogl_dl_data(c, OP_BITMAP, w6, 6, bits, bits ? (DWORD) width * (DWORD) height : 0);
        if (only) {
            ogl_free(bits);
            return;
        }
    }
    ogl_bitmap_apply(c, bits ? width : 0, bits ? height : 0, xorig, yorig, xmove, ymove, bits);
    ogl_free(bits);
}

/* --- Reading ------------------------------------------------------------------------------------- */

float *
ogl_read_rect(ogl_ctx_t *c, DWORD what, GLint x, GLint y, GLsizei w, GLsizei h)
{
    const int   comps = (what <= VGPU98_OGL_READ_BACK) ? 4 : 1;
    const DWORD color = c->format & VGPU98_OGL_COLOR_MASK;
    const int   depth = ogl_format_bits(c->format, OGL_BITS_DEPTH);
    DWORD       bpp, offset;
    uint8_t    *st;
    float      *out;
    GLsizei     x0, y0, cw, i, j;

    if ((uint64_t) w * (uint64_t) h > 0x1000000u) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return NULL;
    }
    out = ogl_alloc((DWORD) w * (DWORD) h * (DWORD) comps * 4);
    if (!out) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return NULL;
    }
    if (!w || !h || !ogl_objects(c))
        return out;
    if (what == VGPU98_OGL_READ_FRONT)
        ogl_front_load(c);
    st = ogl_staging(&offset);
    if (!st)
        return out; /* no room in video memory: zeros */
    switch (what) {
        case VGPU98_OGL_READ_DEPTH:
            bpp = 4;
            break;
        case VGPU98_OGL_READ_STENCIL:
            bpp = 1;
            break;
        default:
            bpp = (color == VGPU98_OGL_COLOR_565) ? 2 : 4;
            break;
    }
    cw = (GLsizei) (OGL_STAGING_STRIDE / bpp);
    if (cw > 0x7fff)
        cw = 0x7fff;
    for (y0 = 0; y0 < h; y0 += OGL_STAGING_ROWS) {
        for (x0 = 0; x0 < w; x0 += cw) {
            const GLsizei kw = (w - x0 < cw) ? w - x0 : cw;
            const GLsizei kh = (h - y0 < OGL_STAGING_ROWS) ? h - y0 : OGL_STAGING_ROWS;
            DWORD        *p  = ogl_cmd(c, VGPU98_OGL_READ, VGPU98_OGL_LEN_READ);

            p[0] = c->id;
            p[1] = what;
            p[2] = ((DWORD) (x + x0) & 0xffff) | ((DWORD) (y + y0) << 16);
            p[3] = (DWORD) kw | ((DWORD) kh << 16);
            p[4] = offset;
            p[5] = OGL_STAGING_STRIDE;
            ogl_finish(c);
            for (j = 0; j < kh; j++) {
                const uint8_t *r = st + (size_t) j * OGL_STAGING_STRIDE;

                for (i = 0; i < kw; i++) {
                    float *o = &out[((size_t) (y0 + j) * (size_t) w + (size_t) (x0 + i)) * (size_t) comps];

                    if (what == VGPU98_OGL_READ_STENCIL)
                        o[0] = (float) r[i];
                    else if (what == VGPU98_OGL_READ_DEPTH) {
                        DWORD d;

                        memcpy(&d, r + (size_t) i * 4, 4);
                        o[0] = (float) ((double) d / ((depth == 16) ? 65535.0 : 16777215.0));
                    } else if (bpp == 2) {
                        const DWORD v = (DWORD) r[i * 2] | ((DWORD) r[i * 2 + 1] << 8);

                        o[0] = (float) (v >> 11) / 31.0f;
                        o[1] = (float) ((v >> 5) & 63) / 63.0f;
                        o[2] = (float) (v & 31) / 31.0f;
                        o[3] = 1.0f;
                    } else {
                        /* BGRA in memory; alpha only an ARGB drawable keeps */
                        o[0] = (float) r[i * 4 + 2] / 255.0f;
                        o[1] = (float) r[i * 4 + 1] / 255.0f;
                        o[2] = (float) r[i * 4] / 255.0f;
                        o[3] = (color == VGPU98_OGL_COLOR_ARGB8888) ? (float) r[i * 4 + 3] / 255.0f : 1.0f;
                    }
                }
            }
        }
    }
    return out;
}

void STDCALL
ogl_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
    ogl_state_t *s;
    float       *v;
    DWORD        what, i, n;
    OGL_CTX();

    if (!ogl_format_comps(format) || !ogl_type_ok(type) ||
        ((type == GL_BITMAP) && (format != GL_STENCIL_INDEX) && (format != GL_COLOR_INDEX))) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if ((width < 0) || (height < 0)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    /* No colour indices in RGBA; no stencil or depth without the buffer */
    if ((format == GL_COLOR_INDEX) || ((format == GL_STENCIL_INDEX) && !ogl_format_bits(c->format, OGL_BITS_STENCIL)) ||
        ((format == GL_DEPTH_COMPONENT) && !ogl_format_bits(c->format, OGL_BITS_DEPTH))) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    if (ogl_inside(c) || !pixels)
        return;
    s = c->gl;
    if (format == GL_STENCIL_INDEX)
        what = VGPU98_OGL_READ_STENCIL;
    else if (format == GL_DEPTH_COMPONENT)
        what = VGPU98_OGL_READ_DEPTH;
    else
        what = c->reg[VGPU98_OGL_R_READ_BUFFER] ? VGPU98_OGL_READ_BACK : VGPU98_OGL_READ_FRONT;
    v = ogl_read_rect(c, what, x, y, width, height);
    if (!v)
        return;
    /* The transfer path, then packed */
    n = (DWORD) width * (DWORD) height;
    if (what == VGPU98_OGL_READ_STENCIL) {
        for (i = 0; i < n; i++)
            v[i] = (float) ogl_transfer_index(s, v[i], 1);
    } else if (what == VGPU98_OGL_READ_DEPTH) {
        for (i = 0; i < n; i++)
            v[i] = ogl_transfer_depth(s, v[i]);
    } else {
        ogl_pixels_t p;

        p.format = GL_RGBA;
        p.w      = width;
        p.h      = height;
        p.comps  = 4;
        p.v      = v;
        p.owned  = 0;
        for (i = 0; i < n; i++) {
            float t[4];

            ogl_transfer_rgba(s, &p, i, t);
            memcpy(&v[i * 4], t, sizeof(t));
        }
    }
    ogl_pack(&s->client.pack, format, type, width, height, v, pixels);
    ogl_free(v);
}

void STDCALL
ogl_CopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type)
{
    ogl_pixels_t p;
    DWORD        what;
    OGL_CTX();

    OGL_DL(OP_COPYPIXELS, (DWORD) x, (DWORD) y, (DWORD) width, (DWORD) height, type);
    if ((type != GL_COLOR) && (type != GL_DEPTH) && (type != GL_STENCIL)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if ((width < 0) || (height < 0)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (((type == GL_STENCIL) && !ogl_format_bits(c->format, OGL_BITS_STENCIL)) ||
        ((type == GL_DEPTH) && !ogl_format_bits(c->format, OGL_BITS_DEPTH))) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    if (ogl_inside(c) || !c->gl->current.raster_valid)
        return;
    if (c->gl->render_mode != GL_RENDER) {
        ogl_raster_token(c, GL_COPY_PIXEL_TOKEN);
        return;
    }
    /* Read as they are, then drawn through the transfer path (once). */
    if (type == GL_STENCIL)
        what = VGPU98_OGL_READ_STENCIL;
    else if (type == GL_DEPTH)
        what = VGPU98_OGL_READ_DEPTH;
    else
        what = c->reg[VGPU98_OGL_R_READ_BUFFER] ? VGPU98_OGL_READ_BACK : VGPU98_OGL_READ_FRONT;
    p.v = ogl_read_rect(c, what, x, y, width, height);
    if (!p.v)
        return;
    p.format = (type == GL_STENCIL) ? GL_STENCIL_INDEX : (type == GL_DEPTH) ? GL_DEPTH_COMPONENT : GL_RGBA;
    p.comps  = (type == GL_COLOR) ? 4 : 1;
    p.w      = width;
    p.h      = height;
    p.owned  = 1;
    draw_image(c, &p);
    ogl_pixels_free(&p);
}

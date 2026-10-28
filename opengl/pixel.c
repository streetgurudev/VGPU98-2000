/*
 * Pixel rectangles' client side (pixel.h; GL 1.1, sections 3.6 and 4.3):
 * the pixel store, transfer and map calls, unpacking every format and type
 * by the unpack state, and the transfer path.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "glmath.h"
#include "lists.h"
#include "pixel.h"

/* --- The pixel store ------------------------------------------------------------------ */

static void
store(ogl_ctx_t *c, GLenum pname, GLint v)
{
    ogl_store_t *st;
    int          pack;

    if (ogl_inside(c))
        return;
    pack = (pname >= GL_PACK_SWAP_BYTES) && (pname <= GL_PACK_ALIGNMENT);
    st   = pack ? &c->gl->client.pack : &c->gl->client.unpack;
    switch (pack ? pname - GL_PACK_SWAP_BYTES + GL_UNPACK_SWAP_BYTES : pname) {
        case GL_UNPACK_SWAP_BYTES:
            st->swap_bytes = v ? GL_TRUE : GL_FALSE;
            break;
        case GL_UNPACK_LSB_FIRST:
            st->lsb_first = v ? GL_TRUE : GL_FALSE;
            break;
        case GL_UNPACK_ROW_LENGTH:
        case GL_UNPACK_SKIP_ROWS:
        case GL_UNPACK_SKIP_PIXELS:
            if (v < 0) {
                ogl_error(c, GL_INVALID_VALUE);
                return;
            }
            if ((pname == GL_UNPACK_ROW_LENGTH) || (pname == GL_PACK_ROW_LENGTH))
                st->row_length = v;
            else if ((pname == GL_UNPACK_SKIP_ROWS) || (pname == GL_PACK_SKIP_ROWS))
                st->skip_rows = v;
            else
                st->skip_pixels = v;
            break;
        case GL_UNPACK_ALIGNMENT:
            if ((v != 1) && (v != 2) && (v != 4) && (v != 8)) {
                ogl_error(c, GL_INVALID_VALUE);
                return;
            }
            st->alignment = v;
            break;
        default:
            ogl_error(c, GL_INVALID_ENUM);
            break;
    }
}

void STDCALL
ogl_PixelStorei(GLenum pname, GLint param)
{
    OGL_CTX();
    store(c, pname, param);
}

void STDCALL
ogl_PixelStoref(GLenum pname, GLfloat param)
{
    OGL_CTX();
    /* The booleans take any nonzero value as TRUE; the others round. */
    if ((pname == GL_PACK_SWAP_BYTES) || (pname == GL_PACK_LSB_FIRST) || (pname == GL_UNPACK_SWAP_BYTES) ||
        (pname == GL_UNPACK_LSB_FIRST))
        store(c, pname, param != 0.0f);
    else
        store(c, pname, ogl_round(param));
}

/* --- The pixel transfer state ------------------------------------------------------------ */

void STDCALL
ogl_PixelTransferf(GLenum pname, GLfloat param)
{
    ogl_pixel_mode_t *pm;
    OGL_CTX();

    OGL_DL(OP_PIXELTRANSFER, pname, ogl_fw(param));
    if (ogl_inside(c))
        return;
    pm = &c->gl->pixel_mode;
    switch (pname) {
        case GL_MAP_COLOR:
            pm->map_color = param != 0.0f;
            break;
        case GL_MAP_STENCIL:
            pm->map_stencil = param != 0.0f;
            break;
        case GL_INDEX_SHIFT:
            pm->index_shift = ogl_round(param);
            break;
        case GL_INDEX_OFFSET:
            pm->index_offset = ogl_round(param);
            break;
        case GL_RED_SCALE:
            pm->scale[0] = param;
            break;
        case GL_GREEN_SCALE:
            pm->scale[1] = param;
            break;
        case GL_BLUE_SCALE:
            pm->scale[2] = param;
            break;
        case GL_ALPHA_SCALE:
            pm->scale[3] = param;
            break;
        case GL_RED_BIAS:
            pm->bias[0] = param;
            break;
        case GL_GREEN_BIAS:
            pm->bias[1] = param;
            break;
        case GL_BLUE_BIAS:
            pm->bias[2] = param;
            break;
        case GL_ALPHA_BIAS:
            pm->bias[3] = param;
            break;
        case GL_DEPTH_SCALE:
            pm->depth_scale = param;
            break;
        case GL_DEPTH_BIAS:
            pm->depth_bias = param;
            break;
        default:
            ogl_error(c, GL_INVALID_ENUM);
            break;
    }
}

void STDCALL
ogl_PixelTransferi(GLenum pname, GLint param)
{
    ogl_PixelTransferf(pname, (GLfloat) param);
}

void STDCALL
ogl_PixelZoom(GLfloat xfactor, GLfloat yfactor)
{
    OGL_CTX();
    OGL_DL(OP_PIXELZOOM, ogl_fw(xfactor), ogl_fw(yfactor));
    if (ogl_inside(c))
        return;
    c->gl->pixel_mode.zoom_x = xfactor;
    c->gl->pixel_mode.zoom_y = yfactor;
}

/* --- Pixel maps ---------------------------------------------------------------------------- */

/* An index map (I_TO_I, S_TO_S) keeps indices; the others colours, 0-1. */
static int
index_map(GLenum map)
{
    return (map == GL_PIXEL_MAP_I_TO_I) || (map == GL_PIXEL_MAP_S_TO_S);
}

/* The maps indexed by an index need a power of two's size. */
static int
indexed_by_index(GLenum map)
{
    return (map >= GL_PIXEL_MAP_I_TO_I) && (map <= GL_PIXEL_MAP_I_TO_A);
}

void STDCALL
ogl_PixelMapfv(GLenum map, GLsizei mapsize, const GLfloat *values)
{
    ogl_pixel_map_t *pm;
    GLsizei          i;
    OGL_CTX();

    /* Compiled with its values (none when its size is out of range: the
       list's run then gives the error). */
    if (ogl_dl_on(c)) {
        const GLsizei n = ((mapsize > 0) && (mapsize <= OGL_PIXEL_MAP_SIZE) && values) ? mapsize : 0;
        DWORD         w[2];

        w[0] = map;
        w[1] = (DWORD) (n ? mapsize : -1);
        if (ogl_dl_data(c, OP_PIXELMAP, w, 2, values, (DWORD) n * 4))
            return;
    }
    if ((map < GL_PIXEL_MAP_I_TO_I) || (map > GL_PIXEL_MAP_A_TO_A)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if ((mapsize < 1) || (mapsize > OGL_PIXEL_MAP_SIZE) || (indexed_by_index(map) && (mapsize & (mapsize - 1)))) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c) || !values)
        return;
    pm       = &c->gl->pixel_map[map - GL_PIXEL_MAP_I_TO_I];
    pm->size = mapsize;
    for (i = 0; i < mapsize; i++)
        pm->value[i] = index_map(map) ? values[i] : ogl_clampf(values[i], 0.0f, 1.0f);
}

void STDCALL
ogl_PixelMapuiv(GLenum map, GLsizei mapsize, const GLuint *values)
{
    float   v[OGL_PIXEL_MAP_SIZE];
    GLsizei i;

    for (i = 0; values && (i < mapsize) && (i < OGL_PIXEL_MAP_SIZE); i++)
        v[i] = index_map(map) ? (float) values[i] : (float) ((double) values[i] / 4294967295.0);
    ogl_PixelMapfv(map, mapsize, values ? v : NULL);
}

void STDCALL
ogl_PixelMapusv(GLenum map, GLsizei mapsize, const GLushort *values)
{
    float   v[OGL_PIXEL_MAP_SIZE];
    GLsizei i;

    for (i = 0; values && (i < mapsize) && (i < OGL_PIXEL_MAP_SIZE); i++)
        v[i] = index_map(map) ? (float) values[i] : (float) values[i] / 65535.0f;
    ogl_PixelMapfv(map, mapsize, values ? v : NULL);
}

static const ogl_pixel_map_t *
get_map(ogl_ctx_t *c, GLenum map)
{
    if ((map < GL_PIXEL_MAP_I_TO_I) || (map > GL_PIXEL_MAP_A_TO_A)) {
        ogl_error(c, GL_INVALID_ENUM);
        return NULL;
    }
    if (ogl_inside(c))
        return NULL;
    return &c->gl->pixel_map[map - GL_PIXEL_MAP_I_TO_I];
}

void STDCALL
ogl_GetPixelMapfv(GLenum map, GLfloat *values)
{
    const ogl_pixel_map_t *pm;
    OGL_CTX();

    pm = get_map(c, map);
    if (pm && values)
        memcpy(values, pm->value, (size_t) pm->size * sizeof(float));
}

void STDCALL
ogl_GetPixelMapuiv(GLenum map, GLuint *values)
{
    const ogl_pixel_map_t *pm;
    GLint                  i;
    OGL_CTX();

    pm = get_map(c, map);
    for (i = 0; pm && values && (i < pm->size); i++)
        values[i] = index_map(map) ? (GLuint) ogl_round(pm->value[i])
                                   : (GLuint) (ogl_floor((double) pm->value[i] * 4294967295.0 + 0.5));
}

void STDCALL
ogl_GetPixelMapusv(GLenum map, GLushort *values)
{
    const ogl_pixel_map_t *pm;
    GLint                  i;
    OGL_CTX();

    pm = get_map(c, map);
    for (i = 0; pm && values && (i < pm->size); i++)
        values[i] = index_map(map) ? (GLushort) ogl_round(pm->value[i]) : (GLushort) ogl_round(pm->value[i] * 65535.0f);
}

/* --- Unpacking -------------------------------------------------------------------------------- */

int
ogl_format_comps(GLenum format)
{
    switch (format) {
        case GL_COLOR_INDEX:
        case GL_STENCIL_INDEX:
        case GL_DEPTH_COMPONENT:
        case GL_RED:
        case GL_GREEN:
        case GL_BLUE:
        case GL_ALPHA:
        case GL_LUMINANCE:
            return 1;
        case GL_LUMINANCE_ALPHA:
            return 2;
        case GL_RGB:
            return 3;
        case GL_RGBA:
            return 4;
        default:
            return 0;
    }
}

int
ogl_type_ok(GLenum type)
{
    switch (type) {
        case GL_BITMAP:
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FLOAT:
            return 1;
        default:
            return 0;
    }
}

static int
elem_bytes(GLenum type)
{
    switch (type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            return 1;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
            return 2;
        default:
            return 4;
    }
}

static int
is_index(GLenum format)
{
    return (format == GL_COLOR_INDEX) || (format == GL_STENCIL_INDEX);
}

DWORD
ogl_row_bytes(const ogl_store_t *st, GLenum format, GLenum type, GLsizei w)
{
    const DWORD l = (DWORD) ((st->row_length > 0) ? st->row_length : w);
    const DWORD a = (DWORD) st->alignment;

    if (type == GL_BITMAP)
        return a * ((l + 8 * a - 1) / (8 * a));
    {
        const DWORD s = (DWORD) elem_bytes(type);
        const DWORD n = (DWORD) ogl_format_comps(format);

        /* GL 1.1's k: s >= a, exactly n * l elements; else rounded up to
           whole alignments. */
        if (s >= a)
            return s * n * l;
        return a * ((s * n * l + a - 1) / a);
    }
}

void
ogl_unpack_bits(const ogl_store_t *st, GLsizei w, GLsizei h, const GLubyte *data, uint8_t *out)
{
    const DWORD row = ogl_row_bytes(st, GL_COLOR_INDEX, GL_BITMAP, w);
    GLsizei     y, x;

    for (y = 0; y < h; y++) {
        const GLubyte *r = data + (size_t) (st->skip_rows + y) * row;

        for (x = 0; x < w; x++) {
            const DWORD bit = (DWORD) (st->skip_pixels + x);
            const int   sh  = st->lsb_first ? (int) (bit % 8) : 7 - (int) (bit % 8);

            out[(size_t) y * (size_t) w + (size_t) x] = (uint8_t) ((r[bit / 8] >> sh) & 1);
        }
    }
}

/* One element as a float: an index or a component (normalized by its
   type as GL converts colours and depths). */
static float
element(const uint8_t *p, GLenum type, int swap, int index)
{
    uint8_t b[4];
    int     n = elem_bytes(type);

    memcpy(b, p, (size_t) n);
    if (swap && (n > 1)) {
        uint8_t t = b[0];

        if (n == 2) {
            b[0] = b[1];
            b[1] = t;
        } else {
            b[0] = b[3];
            b[3] = t;
            t    = b[1];
            b[1] = b[2];
            b[2] = t;
        }
    }
    switch (type) {
        case GL_BYTE: {
            const GLbyte v = (GLbyte) b[0];

            return index ? (float) v : (float) ((2.0 * v + 1.0) / 255.0);
        }
        case GL_UNSIGNED_BYTE:
            return index ? (float) b[0] : (float) b[0] / 255.0f;
        case GL_SHORT: {
            GLshort v;

            memcpy(&v, b, 2);
            return index ? (float) v : (float) ((2.0 * v + 1.0) / 65535.0);
        }
        case GL_UNSIGNED_SHORT: {
            GLushort v;

            memcpy(&v, b, 2);
            return index ? (float) v : (float) v / 65535.0f;
        }
        case GL_INT: {
            GLint v;

            memcpy(&v, b, 4);
            return index ? (float) v : (float) ((2.0 * (double) v + 1.0) / 4294967295.0);
        }
        case GL_UNSIGNED_INT: {
            GLuint v;

            memcpy(&v, b, 4);
            return index ? (float) v : (float) ((double) v / 4294967295.0);
        }
        default: {
            GLfloat v;

            memcpy(&v, b, 4);
            return v;
        }
    }
}

int
ogl_unpack(ogl_ctx_t *c, GLenum format, GLenum type, GLsizei w, GLsizei h, const void *data, ogl_pixels_t *out)
{
    const ogl_store_t *st = &c->gl->client.unpack;
    const int          n  = ogl_format_comps(format);
    DWORD              count, row;
    GLsizei            y, x;
    int                k;

    memset(out, 0, sizeof(*out));
    if (!n || !ogl_type_ok(type)) {
        ogl_error(c, GL_INVALID_ENUM);
        return 0;
    }
    if ((type == GL_BITMAP) && !is_index(format)) {
        ogl_error(c, GL_INVALID_ENUM);
        return 0;
    }
    if ((w < 0) || (h < 0)) {
        ogl_error(c, GL_INVALID_VALUE);
        return 0;
    }
    out->format = format;
    out->w      = w;
    out->h      = h;
    out->comps  = n;
    /* At most 16M pixels, counted in 64 bits (a width times a height can
       wrap 32). */
    if ((uint64_t) w * (uint64_t) h > 0x1000000u) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return 0;
    }
    count = (DWORD) w * (DWORD) h * (DWORD) n;
    if (!count)
        return 1;
    out->v     = ogl_alloc(count * sizeof(float));
    out->owned = 1;
    if (!out->v) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return 0;
    }
    if (!data)
        return 1; /* no data: zeros (a program's mistake, not GL's) */
    if (type == GL_BITMAP) {
        uint8_t *bits = ogl_alloc((DWORD) w * (DWORD) h);

        if (!bits) {
            ogl_pixels_free(out);
            ogl_error(c, GL_OUT_OF_MEMORY);
            return 0;
        }
        ogl_unpack_bits(st, w, h, data, bits);
        for (x = 0; x < w * h; x++)
            out->v[x] = bits[x];
        ogl_free(bits);
        return 1;
    }
    row = ogl_row_bytes(st, format, type, w);
    for (y = 0; y < h; y++) {
        const uint8_t *r = (const uint8_t *) data + (size_t) (st->skip_rows + y) * row +
                           (size_t) st->skip_pixels * (size_t) n * (size_t) elem_bytes(type);

        for (x = 0; x < w; x++) {
            for (k = 0; k < n; k++)
                out->v[((size_t) y * (size_t) w + (size_t) x) * (size_t) n + (size_t) k] =
                    element(r + ((size_t) x * (size_t) n + (size_t) k) * (size_t) elem_bytes(type), type, st->swap_bytes,
                            is_index(format));
        }
    }
    return 1;
}

void
ogl_pixels_free(ogl_pixels_t *p)
{
    if (p->owned)
        ogl_free(p->v);
    p->v     = NULL;
    p->owned = 0;
}

/* --- Packing (glReadPixels, glGetTexImage) --------------------------------------------------- */

/* One element of the type from a component (0-1, GL 1.1 table 2.6
   reversed) or an index (its low bits). */
static void
put_element(uint8_t *p, GLenum type, int swap, float v, int index)
{
    uint8_t b[4];
    int     n = elem_bytes(type);

    /* An index into a signed type keeps the bits of that type's positive
       values (GL 1.1, 4.3.2) */
    if (index) {
        if (type == GL_BYTE)
            v = (float) ((int32_t) v & 0x7f);
        else if (type == GL_SHORT)
            v = (float) ((int32_t) v & 0x7fff);
        else if (type == GL_INT)
            v = (float) ((int32_t) v & 0x7fffffff);
    }
    switch (type) {
        case GL_UNSIGNED_BYTE:
            b[0] = index ? (uint8_t) (int32_t) v : (uint8_t) ogl_round((double) ogl_clampf(v, 0.0f, 1.0f) * 255.0);
            break;
        case GL_BYTE:
            b[0] = (uint8_t) (index ? (int32_t) v : (int32_t) ogl_floor(((double) ogl_clampf(v, -1.0f, 1.0f) * 255.0 - 1.0) / 2.0 + 0.5));
            break;
        case GL_UNSIGNED_SHORT: {
            const uint16_t u = index ? (uint16_t) (int32_t) v : (uint16_t) ogl_round((double) ogl_clampf(v, 0.0f, 1.0f) * 65535.0);

            memcpy(b, &u, 2);
            break;
        }
        case GL_SHORT: {
            const int16_t u = (int16_t) (index ? (int32_t) v : (int32_t) ogl_floor(((double) ogl_clampf(v, -1.0f, 1.0f) * 65535.0 - 1.0) / 2.0 + 0.5));

            memcpy(b, &u, 2);
            break;
        }
        case GL_UNSIGNED_INT: {
            const double d = (double) ogl_clampf(v, 0.0f, 1.0f) * 4294967295.0 + 0.5;
            const DWORD  u = index ? (DWORD) (int32_t) v : ((d >= 4294967295.0) ? 0xffffffffu : (DWORD) d);

            memcpy(b, &u, 4);
            break;
        }
        case GL_INT: {
            const int32_t u = index ? (int32_t) v : (int32_t) ogl_floor(((double) ogl_clampf(v, -1.0f, 1.0f) * 4294967295.0 - 1.0) / 2.0 + 0.5);

            memcpy(b, &u, 4);
            break;
        }
        default:
            memcpy(b, &v, 4);
            break;
    }
    if (swap && (n > 1)) {
        uint8_t t = b[0];

        if (n == 2) {
            b[0] = b[1];
            b[1] = t;
        } else {
            b[0] = b[3];
            b[3] = t;
            t    = b[1];
            b[1] = b[2];
            b[2] = t;
        }
    }
    memcpy(p, b, (size_t) n);
}

void
ogl_pack(const ogl_store_t *st, GLenum format, GLenum type, GLsizei w, GLsizei h, const float *src, void *data)
{
    const int   n     = ogl_format_comps(format);
    const int   index = is_index(format);
    const int   per   = (index || (format == GL_DEPTH_COMPONENT)) ? 1 : 4;
    const DWORD row   = ogl_row_bytes(st, format, type, w);
    GLsizei     y, x;
    int         k;

    if (type == GL_BITMAP) {
        for (y = 0; y < h; y++) {
            uint8_t *r = (uint8_t *) data + (size_t) (st->skip_rows + y) * row;

            for (x = 0; x < w; x++) {
                const DWORD bit = (DWORD) (st->skip_pixels + x);
                const int   sh  = st->lsb_first ? (int) (bit % 8) : 7 - (int) (bit % 8);
                const int   v   = (int) ((int32_t) src[(size_t) y * (size_t) w + (size_t) x] & 1);

                r[bit / 8] = (uint8_t) ((r[bit / 8] & ~(1 << sh)) | (v << sh));
            }
        }
        return;
    }
    for (y = 0; y < h; y++) {
        uint8_t *r = (uint8_t *) data + (size_t) (st->skip_rows + y) * row +
                     (size_t) st->skip_pixels * (size_t) n * (size_t) elem_bytes(type);

        for (x = 0; x < w; x++) {
            const float *s    = &src[((size_t) y * (size_t) w + (size_t) x) * (size_t) per];
            float        c[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

            /* The RGBA components as the format takes them; luminance is
               red, green and blue's sum (GL 1.1, 4.3.2). */
            switch (format) {
                case GL_RED:
                case GL_DEPTH_COMPONENT:
                case GL_COLOR_INDEX:
                case GL_STENCIL_INDEX:
                    c[0] = s[0];
                    break;
                case GL_GREEN:
                    c[0] = s[1];
                    break;
                case GL_BLUE:
                    c[0] = s[2];
                    break;
                case GL_ALPHA:
                    c[0] = s[3];
                    break;
                case GL_LUMINANCE:
                    c[0] = ogl_clampf(s[0] + s[1] + s[2], 0.0f, 1.0f);
                    break;
                case GL_LUMINANCE_ALPHA:
                    c[0] = ogl_clampf(s[0] + s[1] + s[2], 0.0f, 1.0f);
                    c[1] = s[3];
                    break;
                default:
                    memcpy(c, s, (size_t) n * sizeof(float));
                    break;
            }
            for (k = 0; k < n; k++)
                put_element(r + ((size_t) x * (size_t) n + (size_t) k) * (size_t) elem_bytes(type), type, st->swap_bytes, c[k], index);
        }
    }
}

/* --- The transfer path ----------------------------------------------------------------------- */

static float
map_lookup(const ogl_pixel_map_t *m, float v)
{
    /* A colour indexes the map by v (0-1) times its size less one, rounded. */
    const int32_t i = ogl_round((double) ogl_clampf(v, 0.0f, 1.0f) * (m->size - 1));

    return m->value[(i < 0) ? 0 : (i >= m->size) ? m->size - 1 : i];
}

static GLint
shift_offset(const ogl_pixel_mode_t *pm, GLint i)
{
    if (pm->index_shift > 0)
        i = (GLint) ((uint32_t) i << (pm->index_shift & 31));
    else if (pm->index_shift < 0)
        i >>= (-pm->index_shift & 31);
    return i + pm->index_offset;
}

void
ogl_transfer_rgba(const ogl_state_t *s, const ogl_pixels_t *p, DWORD i, float *rgba)
{
    const ogl_pixel_mode_t *pm = &s->pixel_mode;
    const float            *v  = &p->v[(size_t) i * (size_t) p->comps];
    int                     k;

    if (p->format == GL_COLOR_INDEX) {
        /* In RGBA mode an index becomes a colour through the I_TO_* maps
           (indexed by the index masked to their size). */
        const GLint idx = shift_offset(pm, ogl_round(v[0]));

        for (k = 0; k < 4; k++) {
            const ogl_pixel_map_t *m = &s->pixel_map[GL_PIXEL_MAP_I_TO_R - GL_PIXEL_MAP_I_TO_I + k];

            rgba[k] = m->value[(DWORD) idx & (DWORD) (m->size - 1)];
        }
        return;
    }
    /* To RGB, then RGBA (GL's conversions), then scale and bias, the maps,
       and the clamp. */
    rgba[0] = rgba[1] = rgba[2] = 0.0f;
    rgba[3]                     = 1.0f;
    switch (p->format) {
        case GL_RED:
            rgba[0] = v[0];
            break;
        case GL_GREEN:
            rgba[1] = v[0];
            break;
        case GL_BLUE:
            rgba[2] = v[0];
            break;
        case GL_ALPHA:
            rgba[3] = v[0];
            break;
        case GL_LUMINANCE:
            rgba[0] = rgba[1] = rgba[2] = v[0];
            break;
        case GL_LUMINANCE_ALPHA:
            rgba[0] = rgba[1] = rgba[2] = v[0];
            rgba[3]                     = v[1];
            break;
        case GL_RGB:
            memcpy(rgba, v, 3 * sizeof(float));
            break;
        default:
            memcpy(rgba, v, 4 * sizeof(float));
            break;
    }
    for (k = 0; k < 4; k++)
        rgba[k] = rgba[k] * pm->scale[k] + pm->bias[k];
    if (pm->map_color) {
        for (k = 0; k < 4; k++)
            rgba[k] = map_lookup(&s->pixel_map[GL_PIXEL_MAP_R_TO_R - GL_PIXEL_MAP_I_TO_I + k], rgba[k]);
    }
    for (k = 0; k < 4; k++)
        rgba[k] = ogl_clampf(rgba[k], 0.0f, 1.0f);
}

float
ogl_transfer_depth(const ogl_state_t *s, float d)
{
    return ogl_clampf(d * s->pixel_mode.depth_scale + s->pixel_mode.depth_bias, 0.0f, 1.0f);
}

GLint
ogl_transfer_index(const ogl_state_t *s, float index, int stencil)
{
    const ogl_pixel_mode_t *pm = &s->pixel_mode;
    GLint                   i  = shift_offset(pm, ogl_round(index));

    if (stencil ? pm->map_stencil : pm->map_color) {
        const ogl_pixel_map_t *m = &s->pixel_map[(stencil ? GL_PIXEL_MAP_S_TO_S : GL_PIXEL_MAP_I_TO_I) - GL_PIXEL_MAP_I_TO_I];

        i = ogl_round(m->value[(DWORD) i & (DWORD) (m->size - 1)]);
    }
    return i;
}

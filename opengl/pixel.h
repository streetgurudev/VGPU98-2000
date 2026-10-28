/*
 * Pixel rectangles' client side (GL 1.1, sections 3.6 and 4.3): unpacking
 * images from program memory by the pixel store state, and the pixel
 * transfer path (scale, bias, maps, index shift and offset) that turns
 * them into colours, depths and indices. Packing for reads goes the other
 * way.
 *
 * An unpacked image keeps each pixel's group as floats, before the transfer
 * path: colour and depth components as GL converts them (an unsigned type
 * to 0-1, a signed one to -1-1, a float as it is), indices as integers.
 * Display lists keep images in this form (unpacked when compiled, transferred
 * when run).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_ICD_PIXEL_H
#define VGPU98_ICD_PIXEL_H

#include "state.h"

typedef struct ogl_pixels {
    GLenum  format;
    GLsizei w, h;
    int     comps; /* floats a pixel */
    float  *v;     /* w * h * comps, the first row (GL's bottom) first */
    int     owned; /* v is the image's own (freed by ogl_pixels_free) */
} ogl_pixels_t;

/* The components of a pixel format's group; 0 for a name that is not a
   pixel format. */
int ogl_format_comps(GLenum format);
/* Is the type one of GL 1.1's (GL_BITMAP included)? */
int ogl_type_ok(GLenum type);

/* Unpacks an image (a format and type checked as glDrawPixels checks them;
   GL_BITMAP only with an index format). 1, or 0 with the error recorded
   (out->v NULL). w and h 0: an empty image. */
int  ogl_unpack(ogl_ctx_t *c, GLenum format, GLenum type, GLsizei w, GLsizei h, const void *data, ogl_pixels_t *out);
void ogl_pixels_free(ogl_pixels_t *p);
/* One bit a pixel, as glBitmap and glPolygonStipple unpack (GL_BITMAP by
   the unpack state): out gets w * h bytes, 0 or 1, the first row first. */
void ogl_unpack_bits(const ogl_store_t *st, GLsizei w, GLsizei h, const GLubyte *data, uint8_t *out);

/* The transfer path for colour: an unpacked image's pixel i as RGBA, each
   0-1 after scale, bias and the maps (indices through the I_TO_* maps). */
void ogl_transfer_rgba(const ogl_state_t *s, const ogl_pixels_t *p, DWORD i, float *rgba);
/* For a depth component: scale, bias, clamped to 0-1. */
float ogl_transfer_depth(const ogl_state_t *s, float d);
/* For an index (colour or stencil): shift, offset, and the map when on. */
GLint ogl_transfer_index(const ogl_state_t *s, float index, int stencil);

/* The bytes one row of a packed or unpacked image takes, by a store's
   alignment and row length. */
DWORD ogl_row_bytes(const ogl_store_t *st, GLenum format, GLenum type, GLsizei w);

/* Packs w * h pixels into program memory by the store (glReadPixels,
   glGetTexImage): src holds RGBA (4 floats a pixel, 0-1) for the colour
   formats, a depth (0-1) for GL_DEPTH_COMPONENT, an index for the index
   formats. The first row (GL's bottom) first. */
void  ogl_pack(const ogl_store_t *st, GLenum format, GLenum type, GLsizei w, GLsizei h, const float *src, void *data);

/* A rectangle of the drawable read back (pixops.c): RGBA floats (0-1),
   depths (0-1) or stencil indices, one value a pixel for the latter two;
   `what` VGPU98_OGL_READ_*. NULL (an error recorded) when it cannot be. */
float *ogl_read_rect(ogl_ctx_t *c, DWORD what, GLint x, GLint y, GLsizei w, GLsizei h);

/* What the images become (pixops.c and teximage.c): each takes an image
   already unpacked. */
void ogl_drawpixels_apply(ogl_ctx_t *c, const ogl_pixels_t *p);
void ogl_bitmap_apply(ogl_ctx_t *c, GLsizei w, GLsizei h, float xorig, float yorig, float xmove, float ymove,
                      const uint8_t *bits);
void ogl_teximage_apply(ogl_ctx_t *c, int dims, GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border,
                        const ogl_pixels_t *p);
void ogl_texsubimage_apply(ogl_ctx_t *c, int dims, GLenum target, GLint level, GLint xoff, GLint yoff,
                           const ogl_pixels_t *p);
/* EXT_paletted_texture's tables (palette.c): a table (a proxy's checked
   alone), entries of one. */
void ogl_colortable_apply(ogl_ctx_t *c, GLenum target, GLint ifmt, GLsizei width, const ogl_pixels_t *p);
void ogl_colorsubtable_apply(ogl_ctx_t *c, GLenum target, GLsizei start, GLsizei count, const ogl_pixels_t *p);

#endif /* VGPU98_ICD_PIXEL_H */

/*
 * GL's state as the OpenGL device's registers (vid_vgpu98_oglcmd.h,
 * VGPU98_OGL_R_*): each part of the state that changed is turned into its
 * registers, which go before the next command that draws (device.c).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_ICD_REGS_H
#define VGPU98_ICD_REGS_H

#include "state.h"

void ogl_regs_all(ogl_ctx_t *c);
void ogl_regs_matrices(ogl_ctx_t *c); /* modelview, projection, texture, normal */
void ogl_regs_enables(ogl_ctx_t *c);
void ogl_regs_lighting(ogl_ctx_t *c);
void ogl_regs_texgen(ogl_ctx_t *c);
void ogl_regs_clip(ogl_ctx_t *c);
void ogl_regs_fog(ogl_ctx_t *c);
void ogl_regs_texture(ogl_ctx_t *c);  /* the texture drawn with and its environment */
void ogl_regs_fragment(ogl_ctx_t *c); /* alpha, depth, stencil, blend, logic op, masks, scissor, buffers */
void ogl_regs_raster(ogl_ctx_t *c);   /* viewport, depth range, points, lines, polygon offset and stipple, faces */

/* The modelview's inverse transpose, kept up to date (state.h's normal). */
void ogl_normal_matrix(ogl_state_t *s);

/* The draw buffers' mask (front 1, back 2) for a GL draw buffer name. */
DWORD ogl_draw_mask(GLenum buffer);

/* A unit's effective texture (texobj.c): its device ID and whether it is
   one-dimensional; 0 when the unit's texturing is off or its texture
   incomplete. */
DWORD ogl_texture_effective(ogl_ctx_t *c, int unit, int *one_d);

#endif /* VGPU98_ICD_REGS_H */

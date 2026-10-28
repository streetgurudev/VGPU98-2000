/*
 * The framebuffer's control: clears and their values, glFlush, glFinish,
 * the viewport, the depth range and the scissor box (GL 1.1, sections 2.10
 * and 4.2).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "glmath.h"
#include "lists.h"
#include "regs.h"
#include "state.h"

static DWORD
fbits(float f)
{
    DWORD v;

    memcpy(&v, &f, 4);
    return v;
}

void STDCALL
ogl_ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
    OGL_CTX();
    OGL_DL(OP_CLEARCOLOR, ogl_fw(r), ogl_fw(g), ogl_fw(b), ogl_fw(a));
    if (c->gl->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    c->gl->color_buffer.clear_color[0] = ogl_clampf(r, 0.0f, 1.0f);
    c->gl->color_buffer.clear_color[1] = ogl_clampf(g, 0.0f, 1.0f);
    c->gl->color_buffer.clear_color[2] = ogl_clampf(b, 0.0f, 1.0f);
    c->gl->color_buffer.clear_color[3] = ogl_clampf(a, 0.0f, 1.0f);
}

void STDCALL
ogl_ClearDepth(GLclampd depth)
{
    OGL_CTX();
    if (ogl_dl_on(c)) {
        DWORD w[2];

        ogl_dw(w, depth);
        if (ogl_dl(c, OP_CLEARDEPTH, w, 2))
            return;
    }
    if (c->gl->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    c->gl->buffers.depth_clear = (depth > 0.0) ? ((depth < 1.0) ? depth : 1.0) : 0.0;
}

void STDCALL
ogl_ClearStencil(GLint s)
{
    OGL_CTX();
    OGL_DL(OP_CLEARSTENCIL, (DWORD) s);
    if (c->gl->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    c->gl->buffers.stencil_clear = s;
}

void STDCALL
ogl_Clear(GLbitfield mask)
{
    const GLbitfield all = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT | GL_ACCUM_BUFFER_BIT;
    ogl_state_t     *s;
    DWORD            dev = 0;
    DWORD           *p;
    OGL_CTX();

    OGL_DL(OP_CLEAR, mask);
    s = c->gl;
    if (mask & ~all) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (s->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    /* The render modes other than GL_RENDER clear nothing (no fragments). */
    if (s->render_mode != GL_RENDER)
        return;
    if (mask & GL_COLOR_BUFFER_BIT)
        dev |= VGPU98_OGL_CLEAR_COLOR;
    if ((mask & GL_DEPTH_BUFFER_BIT) && ogl_format_bits(c->format, OGL_BITS_DEPTH))
        dev |= VGPU98_OGL_CLEAR_DEPTH;
    if ((mask & GL_STENCIL_BUFFER_BIT) && ogl_format_bits(c->format, OGL_BITS_STENCIL))
        dev |= VGPU98_OGL_CLEAR_STENCIL;
    /* No accumulation buffer: its bit clears nothing. */
    if (!dev || !ogl_objects(c))
        return;
    /* The front buffer's pixels outside the scissor box or masks stay the
       window's: its image takes them first. */
    if ((dev & VGPU98_OGL_CLEAR_COLOR) && (c->reg[VGPU98_OGL_R_DRAW_BUFFERS] & 1u))
        ogl_front_load(c);
    ogl_sync(c);
    p = ogl_cmd(c, VGPU98_OGL_CLEAR, VGPU98_OGL_LEN_CLEAR);
    p[0] = c->id;
    p[1] = dev;
    p[2] = fbits(s->color_buffer.clear_color[0]);
    p[3] = fbits(s->color_buffer.clear_color[1]);
    p[4] = fbits(s->color_buffer.clear_color[2]);
    p[5] = fbits(s->color_buffer.clear_color[3]);
    p[6] = fbits((float) s->buffers.depth_clear);
    p[7] = (DWORD) s->buffers.stencil_clear;
    if ((dev & VGPU98_OGL_CLEAR_COLOR) && (c->reg[VGPU98_OGL_R_DRAW_BUFFERS] & 1u) && c->win) {
        c->win->front_drawn = 1;
        ogl_front_show(c, 0);
    }
}

void STDCALL
ogl_Flush(void)
{
    OGL_CTX();
    if (c->gl->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    ogl_front_show(c, 1);
    ogl_sync(c);
    ogl_flush(c);
}

void STDCALL
ogl_Finish(void)
{
    OGL_CTX();
    if (c->gl->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    ogl_front_show(c, 1);
    ogl_finish(c);
}

void STDCALL
ogl_Viewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    ogl_state_t *s;
    OGL_CTX();

    OGL_DL(OP_VIEWPORT, (DWORD) x, (DWORD) y, (DWORD) w, (DWORD) h);
    s = c->gl;
    if ((w < 0) || (h < 0)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (s->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    /* The width and height are clamped to the largest viewport. */
    s->viewport.x = x;
    s->viewport.y = y;
    s->viewport.w = (w < OGL_MAX_VIEWPORT) ? w : OGL_MAX_VIEWPORT;
    s->viewport.h = (h < OGL_MAX_VIEWPORT) ? h : OGL_MAX_VIEWPORT;
    ogl_regs_raster(c);
}

void STDCALL
ogl_DepthRange(GLclampd n, GLclampd f)
{
    ogl_state_t *s;
    OGL_CTX();

    if (ogl_dl_on(c)) {
        DWORD w[4];

        ogl_dw(&w[0], n);
        ogl_dw(&w[2], f);
        if (ogl_dl(c, OP_DEPTHRANGE, w, 4))
            return;
    }
    s = c->gl;
    if (s->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    s->viewport.near_val = (n > 0.0) ? ((n < 1.0) ? n : 1.0) : 0.0;
    s->viewport.far_val  = (f > 0.0) ? ((f < 1.0) ? f : 1.0) : 0.0;
    ogl_regs_raster(c);
}

void STDCALL
ogl_Scissor(GLint x, GLint y, GLsizei w, GLsizei h)
{
    ogl_state_t *s;
    OGL_CTX();

    OGL_DL(OP_SCISSOR, (DWORD) x, (DWORD) y, (DWORD) w, (DWORD) h);
    s = c->gl;
    if ((w < 0) || (h < 0)) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (s->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    s->scissor.x = x;
    s->scissor.y = y;
    s->scissor.w = w;
    s->scissor.h = h;
    ogl_regs_fragment(c);
}

/*
 * The Glide core: the frame buffer.
 *
 * The buffers live on the host GPU; a program reaches them through the
 * staging surface in video memory, in rows laid out as a Voodoo3's frame
 * buffer has them (2048 pixels a row: 4096 bytes of 16-bit pixels, 8192 of
 * 32-bit). A read lock has the device write the buffer there and waits for
 * it; a write lock is filled the same way first, in the program's format,
 * and at the unlock the device takes back the pixels the program changed.
 * grLfbWriteRegion sends its pixels without a read. The region calls count
 * rows from the top whatever grSstOrigin says, as a Voodoo3's Glide locks
 * for them.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcore.h"

/* The two locks a program may hold, a read lock and a write lock. */
typedef struct lfb_lock {
    int   held;
    FxI32 buffer;
    DWORD format; /* the format dword sent: write mode and flags */
    DWORD offset, stride;
} lfb_lock_t;

static lfb_lock_t locks[2]; /* [0] read, [1] write */
static DWORD      color_flags; /* grLfbWriteColorFormat and grLfbWriteColorSwizzle, as LFB flags */

static DWORD
pixel_bytes(DWORD mode)
{
    switch (mode) {
        case VGPU98_GL_LFB_565:
        case VGPU98_GL_LFB_555:
        case VGPU98_GL_LFB_1555:
        case VGPU98_GL_LFB_ZA16:
            return 2;
        case VGPU98_GL_LFB_888:
        case VGPU98_GL_LFB_8888:
        case VGPU98_GL_LFB_565_DEPTH:
        case VGPU98_GL_LFB_555_DEPTH:
        case VGPU98_GL_LFB_1555_DEPTH:
            return 4;
        default:
            return 0;
    }
}

/* Glide's depth and alpha buffers are the aux buffer. */
static FxI32
buffer_of(FxI32 buffer)
{
    return ((buffer == GR_BUFFER_DEPTHBUFFER) || (buffer == GR_BUFFER_ALPHABUFFER)) ? GR_BUFFER_AUXBUFFER : buffer;
}

static DWORD
origin_flag(FxI32 origin)
{
    if (origin == GR_ORIGIN_ANY)
        origin = (FxI32) glc_reg_get(VGPU98_GS_ORIGIN);
    return (origin == GR_ORIGIN_LOWER_LEFT) ? VGPU98_GL_LFB_LOWER_LEFT : 0;
}

/* A rectangle inside the window, whatever the numbers (no sum wraps). */
static int
inside(FxU32 x, FxU32 y, FxU32 w, FxU32 h)
{
    return w && h && (x < glc_width()) && (w <= glc_width() - x) && (y < glc_height()) && (h <= glc_height() - y);
}

/* LFB_READ or LFB_WRITE of a rectangle. */
static int
transfer(DWORD op, FxI32 buffer, FxU32 x, FxU32 y, FxU32 w, FxU32 h, DWORD format, DWORD offset, DWORD stride)
{
    DWORD *p = glc_cmd(op, VGPU98_GL_LEN_LFB);

    if (!p)
        return 0;
    p[0] = (DWORD) buffer;
    p[1] = x | (y << 16);
    p[2] = w | (h << 16);
    p[3] = format;
    p[4] = offset;
    p[5] = stride;
    return 1;
}

FxBool
glc_lfb_lock(FxU32 type, FxI32 buffer, FxI32 write_mode, FxI32 origin, FxBool pipeline, glc_lfb_info_t *info)
{
    const int   write = (type & GR_LFB_WRITE_ONLY) != 0;
    lfb_lock_t *l     = &locks[write];
    DWORD       mode, bytes, offset, stride, area;
    uint8_t    *base;
    DWORD       fence;

    if (!glc_is_open() || !info || (info->size < (int) sizeof(*info)) || l->held)
        return FXFALSE;
    buffer = buffer_of(buffer);
    if (!glc_has_buffer(buffer))
        return FXFALSE;
    /* Reads give the buffer's own pixels; writes the program's format, the
       buffer's own for "any". */
    if (!write || (write_mode == GR_LFBWRITEMODE_ANY))
        mode = (buffer == GR_BUFFER_AUXBUFFER) ? VGPU98_GL_LFB_ZA16 : VGPU98_GL_LFB_565;
    else
        mode = (DWORD) write_mode;
    bytes = pixel_bytes(mode);
    if (!bytes || ((buffer == GR_BUFFER_AUXBUFFER) != (mode == VGPU98_GL_LFB_ZA16)))
        return FXFALSE;
    base = glc_staging(!write, &offset, &stride, &area);
    if (!base)
        return FXFALSE;
    if (write && (bytes == 2))
        stride /= 2; /* 16-bit rows as a Voodoo3's: half the 32-bit rows' */
    if (write && pipeline)
        glc_deferred(GLC_DEFER_LFB_PIPELINE);
    l->buffer = buffer;
    l->format = mode | origin_flag(origin) | (write ? (color_flags | (pipeline ? VGPU98_GL_LFB_PIPELINE : 0)) : 0);
    l->offset = offset;
    l->stride = stride;
    /* The buffer's pixels into the rows, in the lock's format, and the
       program waits for them: what was drawn before is in them. With the
       GPU held (a DOS box), no pixels come: refused. */
    glc_sync();
    if (!transfer(VGPU98_GL_LFB_READ, buffer, 0, 0, glc_width(), glc_height(), l->format & ~VGPU98_GL_LFB_PIPELINE, offset,
                  stride))
        return FXFALSE;
    fence = glc_flush();
    glc_lfb_fence(fence);
    if (!fence || !glc_wait(fence))
        return FXFALSE;
    glc_count(GLC_LOCKS);
    l->held             = 1;
    info->lfbPtr        = base;
    info->strideInBytes = stride;
    info->writeMode     = (FxI32) mode;
    info->origin        = (l->format & VGPU98_GL_LFB_LOWER_LEFT) ? GR_ORIGIN_LOWER_LEFT : GR_ORIGIN_UPPER_LEFT;
    return FXTRUE;
}

FxBool
glc_lfb_unlock(FxU32 type, FxI32 buffer)
{
    const int   write = (type & GR_LFB_WRITE_ONLY) != 0;
    lfb_lock_t *l     = &locks[write];

    if (!l->held || (l->buffer != buffer_of(buffer)))
        return FXFALSE;
    l->held = 0;
    if (!write)
        return FXTRUE;
    /* The pixels the program changed since the lock's read. */
    if (transfer(VGPU98_GL_LFB_WRITE, l->buffer, 0, 0, glc_width(), glc_height(), l->format | VGPU98_GL_LFB_CHANGED,
                 l->offset, l->stride))
        glc_lfb_fence(glc_flush());
    return FXTRUE;
}

FxBool
glc_lfb_read(FxI32 buffer, FxU32 x, FxU32 y, FxU32 w, FxU32 h, FxU32 dst_stride, void *dst)
{
    const DWORD mode = (buffer_of(buffer) == GR_BUFFER_AUXBUFFER) ? VGPU98_GL_LFB_ZA16 : VGPU98_GL_LFB_565;
    DWORD       offset, stride, area, fence, r;
    uint8_t    *base;

    buffer = buffer_of(buffer);
    if (!glc_is_open() || !dst || !inside(x, y, w, h) || !glc_has_buffer(buffer))
        return FXFALSE;
    base = glc_staging(1, &offset, &stride, &area);
    if (!base || !glc_lfb_settle())
        return FXFALSE;
    glc_sync();
    if (!transfer(VGPU98_GL_LFB_READ, buffer, x, y, w, h, mode, offset, stride))
        return FXFALSE;
    fence = glc_flush();
    glc_lfb_fence(fence);
    if (!fence || !glc_wait(fence))
        return FXFALSE;
    for (r = 0; r < h; r++)
        memcpy((uint8_t *) dst + r * dst_stride, base + (y + r) * stride + x * 2, w * 2);
    return FXTRUE;
}

FxBool
glc_lfb_write(FxI32 buffer, FxU32 x, FxU32 y, FxI32 format, FxU32 w, FxU32 h, FxBool pipeline, FxI32 src_stride,
              const void *src)
{
    const DWORD bytes = pixel_bytes((DWORD) format);
    DWORD       offset, stride, area, r;
    uint8_t    *base;

    buffer = buffer_of(buffer);
    if (!glc_is_open() || !src || !inside(x, y, w, h) || !glc_has_buffer(buffer))
        return FXFALSE;
    if (format == GR_LFB_SRC_FMT_RLE16) {
        static int told;

        if (!told++)
            glide_event("grLfbWriteRegion: run-length pixels are not taken yet");
        return FXFALSE;
    }
    if (!bytes || ((buffer == GR_BUFFER_AUXBUFFER) != (format == VGPU98_GL_LFB_ZA16)))
        return FXFALSE;
    base = glc_staging(0, &offset, &stride, &area);
    if (!base)
        return FXFALSE;
    if (bytes == 2)
        stride /= 2;
    if (pipeline)
        glc_deferred(GLC_DEFER_LFB_PIPELINE);
    /* The rows are the program's again once the last transfer is done (the
       GPU held: not yet, so refused). */
    if (!glc_lfb_settle())
        return FXFALSE;
    for (r = 0; r < h; r++)
        memcpy(base + (y + r) * stride + x * bytes, (const uint8_t *) src + (int32_t) r * src_stride, w * bytes);
    glc_sync();
    if (!transfer(VGPU98_GL_LFB_WRITE, buffer, x, y, w, h,
                  (DWORD) format | color_flags | (pipeline ? VGPU98_GL_LFB_PIPELINE : 0), offset, stride))
        return FXFALSE;
    glc_lfb_fence(glc_flush());
    if ((buffer == GR_BUFFER_FRONTBUFFER) || (glc_color_buffers() == 1))
        glc_front_written();
    return FXTRUE;
}

void
glc_lfb_color_format(FxI32 format)
{
    if ((format < GR_COLORFORMAT_ARGB) || (format > GR_COLORFORMAT_BGRA))
        return;
    color_flags = (color_flags & ~(3u << VGPU98_GL_LFB_COLOR_SHIFT)) | ((DWORD) format << VGPU98_GL_LFB_COLOR_SHIFT);
}

/* No lock held: a window's open and close end them. */
void
glc_lfb_reset(void)
{
    memset(locks, 0, sizeof(locks));
}

DWORD
glc_lfb_flags(void)
{
    return color_flags;
}

void
glc_lfb_flags_set(DWORD flags)
{
    color_flags = flags & ((3u << VGPU98_GL_LFB_COLOR_SHIFT) | VGPU98_GL_LFB_SWAP_BYTES | VGPU98_GL_LFB_SWAP_WORDS);
}

void
glc_lfb_swizzle(FxBool bytes, FxBool words)
{
    color_flags = (color_flags & ~(VGPU98_GL_LFB_SWAP_BYTES | VGPU98_GL_LFB_SWAP_WORDS)) |
                  (bytes ? VGPU98_GL_LFB_SWAP_BYTES : 0) | (words ? VGPU98_GL_LFB_SWAP_WORDS : 0);
}

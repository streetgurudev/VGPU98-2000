/*
 * glide3x.dll: the screen and the frame buffer:
 * clears, swaps, the buffers drawn into, the clip window, the
 * origin, the masks, gamma, the frame buffer's locks and regions, and the
 * pipeline's flushes, on the Glide core (glcore.h).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glide3x.h"
#include "glcore.h"

/* --- The screen ------------------------------------------------------------ */

void FX_CALL
grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU32 depth)
{
    CALLED(grBufferClear);
    glc_clear(color, alpha, depth);
}

void FX_CALL
grBufferSwap(FxU32 swap_interval)
{
    CALLED(grBufferSwap);
    glc_swap(swap_interval);
}

/* Swaps the program lets wait before grBufferSwap waits for one. */
void FX_CALL
grSetNumPendingBuffers(FxI32 NumPendingBuffers)
{
    CALLED(grSetNumPendingBuffers);
    glc_set_pending_max(NumPendingBuffers);
}

void FX_CALL
grRenderBuffer(GrBuffer_t buffer)
{
    CALLED(grRenderBuffer);
    if ((buffer == GR_BUFFER_FRONTBUFFER) || (buffer == GR_BUFFER_BACKBUFFER))
        glc_reg(VGPU98_GS_RENDER_BUFFER, (DWORD) buffer);
}

/* Inclusive minimums, exclusive maximums; the screen's size at the open. */
void FX_CALL
grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy)
{
    CALLED(grClipWindow);
    minx = (minx > 0xffff) ? 0xffff : minx;
    miny = (miny > 0xffff) ? 0xffff : miny;
    maxx = (maxx > 0xffff) ? 0xffff : maxx;
    maxy = (maxy > 0xffff) ? 0xffff : maxy;
    glc_reg(VGPU98_GS_CLIP_MIN, minx | (miny << 16));
    glc_reg(VGPU98_GS_CLIP_MAX, maxx | (maxy << 16));
}

void FX_CALL
grSstOrigin(GrOriginLocation_t origin)
{
    CALLED(grSstOrigin);
    if ((origin == GR_ORIGIN_UPPER_LEFT) || (origin == GR_ORIGIN_LOWER_LEFT))
        glc_reg(VGPU98_GS_ORIGIN, (DWORD) origin);
}

void FX_CALL
grColorMask(FxBool rgb, FxBool a)
{
    DWORD m;

    CALLED(grColorMask);
    m = glc_reg_get(VGPU98_GS_MASKS) & ~(DWORD) (VGPU98_GS_MASK_RGB | VGPU98_GS_MASK_ALPHA);
    if (a && glc_is_open() && glc_aux_buffers() && !(glc_reg_get(VGPU98_GS_DEPTH) & 0xff))
        glc_deferred(GLC_DEFER_ALPHA_BUFFER);
    glc_reg(VGPU98_GS_MASKS, m | (rgb ? VGPU98_GS_MASK_RGB : 0) | (a ? VGPU98_GS_MASK_ALPHA : 0));
}

void FX_CALL
grDepthMask(FxBool mask)
{
    DWORD m = glc_reg_get(VGPU98_GS_MASKS) & ~(DWORD) VGPU98_GS_MASK_DEPTH;

    CALLED(grDepthMask);
    glc_reg(VGPU98_GS_MASKS, m | (mask ? VGPU98_GS_MASK_DEPTH : 0));
}

/* A table of nentries values a channel (8 bits each), spread over the 256
   levels of the video output. */
void FX_CALL
grLoadGammaTable(FxU32 nentries, FxU32 *red, FxU32 *green, FxU32 *blue)
{
    uint8_t tr[256], tg[256], tb[256];
    int     i;

    CALLED(grLoadGammaTable);
    if (!red || !green || !blue || (nentries < 2) || (nentries > 256))
        return;
    for (i = 0; i < 256; i++) {
        /* Between the two entries around level i, as the hardware's
           table interpolates. */
        const FxU32 at   = (FxU32) i * (nentries - 1);
        const FxU32 k    = at / 255;
        const FxU32 frac = at % 255;
        const FxU32 next = (k + 1 < nentries) ? k + 1 : k;

        tr[i] = (uint8_t) (((red[k] & 0xff) * (255 - frac) + (red[next] & 0xff) * frac + 127) / 255);
        tg[i] = (uint8_t) (((green[k] & 0xff) * (255 - frac) + (green[next] & 0xff) * frac + 127) / 255);
        tb[i] = (uint8_t) (((blue[k] & 0xff) * (255 - frac) + (blue[next] & 0xff) * frac + 127) / 255);
    }
    glc_gamma_table(tr, tg, tb);
}

void FX_CALL
guGammaCorrectionRGB(FxFloat red, FxFloat green, FxFloat blue)
{
    CALLED(guGammaCorrectionRGB);
    if ((red > 0.0f) && (green > 0.0f) && (blue > 0.0f))
        glc_gamma(red, green, blue);
}

/* --- The pipeline -------------------------------------------------------- */

void FX_CALL
grFlush(void)
{
    CALLED(grFlush);
    glc_flush();
}

void FX_CALL
grFinish(void)
{
    CALLED(grFinish);
    glc_finish();
}

/* --- The frame buffer ---------------------------------------------------- */

FxBool FX_CALL
grLfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode, GrOriginLocation_t origin, FxBool pixelPipeline,
          GrLfbInfo_t *info)
{
    CALLED(grLfbLock);
    return glc_lfb_lock(type, buffer, writeMode, origin, pixelPipeline, (glc_lfb_info_t *) info);
}

FxBool FX_CALL
grLfbUnlock(GrLock_t type, GrBuffer_t buffer)
{
    CALLED(grLfbUnlock);
    return glc_lfb_unlock(type, buffer);
}

void FX_CALL
grLfbConstantAlpha(GrAlpha_t alpha)
{
    CALLED(grLfbConstantAlpha);
    glc_reg(VGPU98_GS_LFB_CONSTANT, (glc_reg_get(VGPU98_GS_LFB_CONSTANT) & 0xffff0000u) | alpha);
}

void FX_CALL
grLfbConstantDepth(FxU32 depth)
{
    CALLED(grLfbConstantDepth);
    glc_reg(VGPU98_GS_LFB_CONSTANT, (glc_reg_get(VGPU98_GS_LFB_CONSTANT) & 0xffff) | ((depth & 0xffff) << 16));
}

void FX_CALL
grLfbWriteColorFormat(GrColorFormat_t colorFormat)
{
    CALLED(grLfbWriteColorFormat);
    glc_lfb_color_format(colorFormat);
}

void FX_CALL
grLfbWriteColorSwizzle(FxBool swizzleBytes, FxBool swapWords)
{
    CALLED(grLfbWriteColorSwizzle);
    glc_lfb_swizzle(swizzleBytes, swapWords);
}

FxBool FX_CALL
grLfbReadRegion(GrBuffer_t src_buffer, FxU32 src_x, FxU32 src_y, FxU32 src_width, FxU32 src_height, FxU32 dst_stride,
                void *dst_data)
{
    CALLED(grLfbReadRegion);
    return glc_lfb_read(src_buffer, src_x, src_y, src_width, src_height, dst_stride, dst_data);
}

FxBool FX_CALL
grLfbWriteRegion(GrBuffer_t dst_buffer, FxU32 dst_x, FxU32 dst_y, GrLfbSrcFmt_t src_format, FxU32 src_width,
                 FxU32 src_height, FxBool pixelPipeline, FxI32 src_stride, void *src_data)
{
    CALLED(grLfbWriteRegion);
    return glc_lfb_write(dst_buffer, dst_x, dst_y, src_format, src_width, src_height, pixelPipeline, src_stride, src_data);
}

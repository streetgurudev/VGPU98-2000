/*
 * glide2x.dll: the screen and the frame buffer:
 * clears, swaps, the buffers drawn into, the clip window, the
 * origin, the masks, gamma, the frame buffer's locks and regions, and the
 * board's status, on the Glide core (glcore.h).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glide2x.h"
#include "glcore.h"

/* --- The screen ------------------------------------------------------------ */

void FX_CALL
grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU16 depth)
{
    CALLED(grBufferClear);
    glc_clear(color, alpha, depth);
}

void FX_CALL
grBufferSwap(int swap_interval)
{
    CALLED(grBufferSwap);
    glc_swap((swap_interval > 0) ? (FxU32) swap_interval : 0);
}

/* At most 7, as a Voodoo counts them. */
int FX_CALL
grBufferNumPending(void)
{
    CALLED(grBufferNumPending);
    return glc_pending();
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
    /* Alpha with depth buffering off makes the aux buffer an alpha buffer
       on a Voodoo3 (deferred). */
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

/* The video output's curve, RGB = 255 (RGB / 255)^(1/gamma); 1.0 at the
   start, a straight line. */
void FX_CALL
grGammaCorrectionValue(float value)
{
    CALLED(grGammaCorrectionValue);
    if ((value > 0.0f) && (value <= 20.0f))
        glc_gamma(value, value, value);
}

/* --- The board's status --------------------------------------------------- */

void FX_CALL
grSstIdle(void)
{
    CALLED(grSstIdle);
    glc_finish();
}

FxBool FX_CALL
grSstIsBusy(void)
{
    CALLED(grSstIsBusy);
    return glc_busy() ? FXTRUE : FXFALSE;
}

/* The status register as a Voodoo3's Glide 2 gives it: the FIFO's room
   (bits 4:0, all free), bit 6 clear in vertical retrace, the FBI, the TMUs
   and the chip busy (bits 7-9), bits 27:12 all ones (a Voodoo's memory
   FIFO, empty), the swaps pending (bits 30:28). */
FxU32 FX_CALL
grSstStatus(void)
{
    const int pending = glc_pending();
    FxU32     v       = 0x0ffff01f;

    CALLED(grSstStatus);
    if (!(glc_vretrace() & VGPU98_SCANLINE_VBLANK))
        v |= 0x40;
    if (glc_busy())
        v |= 0x380;
    v |= (FxU32) ((pending > 7) ? 7 : pending) << 28;
    return v;
}

FxBool FX_CALL
grSstVRetraceOn(void)
{
    CALLED(grSstVRetraceOn);
    return (glc_vretrace() & VGPU98_SCANLINE_VBLANK) ? FXTRUE : FXFALSE;
}

/* Always 1, as a Voodoo3's Glide 2 answers. */
FxU32 FX_CALL
grSstVideoLine(void)
{
    CALLED(grSstVideoLine);
    return 1;
}

FxU32 FX_CALL
grSstScreenWidth(void)
{
    CALLED(grSstScreenWidth);
    return glc_width();
}

FxU32 FX_CALL
grSstScreenHeight(void)
{
    CALLED(grSstScreenHeight);
    return glc_height();
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
grLfbConstantDepth(FxU16 depth)
{
    CALLED(grLfbConstantDepth);
    glc_reg(VGPU98_GS_LFB_CONSTANT, (glc_reg_get(VGPU98_GS_LFB_CONSTANT) & 0xffff) | ((DWORD) depth << 16));
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
                 FxU32 src_height, FxI32 src_stride, void *src_data)
{
    CALLED(grLfbWriteRegion);
    return glc_lfb_write(dst_buffer, dst_x, dst_y, src_format, src_width, src_height, FXFALSE, src_stride, src_data);
}

/* Obsolete: a Voodoo3's Glide 2 does nothing for them either. */
void FX_CALL
guFbReadRegion(const int srcX, const int srcY, const int w, const int h, const void *dst, const int strideInBytes)
{
    CALLED(guFbReadRegion);
    (void) srcX;
    (void) srcY;
    (void) w;
    (void) h;
    (void) dst;
    (void) strideInBytes;
}

void FX_CALL
guFbWriteRegion(const int dstX, const int dstY, const int w, const int h, const void *src, const int strideInBytes)
{
    CALLED(guFbWriteRegion);
    (void) dstX;
    (void) dstY;
    (void) w;
    (void) h;
    (void) src;
    (void) strideInBytes;
}

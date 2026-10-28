/*
 * glide3x.dll: the pixel pipeline's state:
 * the combine units, the constant colour, blending, the alpha test, depth,
 * fog (Glide 3's modes turned into the device's), chroma key, culling and
 * dithering, on the Glide core (glcore.h, state.c).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glide3x.h"
#include "glcore.h"

void FX_CALL
grColorCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other,
               FxBool invert)
{
    CALLED(grColorCombine);
    glc_color_combine(function, factor, local, other, invert);
}

void FX_CALL
grAlphaCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other,
               FxBool invert)
{
    CALLED(grAlphaCombine);
    glc_alpha_combine(function, factor, local, other, invert);
}

void FX_CALL
grAlphaControlsITRGBLighting(FxBool enable)
{
    CALLED(grAlphaControlsITRGBLighting);
    glc_itrgb(enable);
}

void FX_CALL
grConstantColorValue(GrColor_t value)
{
    CALLED(grConstantColorValue);
    glc_constant(value);
}

void FX_CALL
grAlphaBlendFunction(GrAlphaBlendFnc_t rgb_sf, GrAlphaBlendFnc_t rgb_df, GrAlphaBlendFnc_t alpha_sf,
                     GrAlphaBlendFnc_t alpha_df)
{
    CALLED(grAlphaBlendFunction);
    glc_blend(rgb_sf, rgb_df, alpha_sf, alpha_df);
}

void FX_CALL
grAlphaTestFunction(GrCmpFnc_t function)
{
    CALLED(grAlphaTestFunction);
    glc_alpha_test(function);
}

void FX_CALL
grAlphaTestReferenceValue(GrAlpha_t value)
{
    CALLED(grAlphaTestReferenceValue);
    glc_alpha_ref(value);
}

void FX_CALL
grDepthBufferMode(GrDepthBufferMode_t mode)
{
    CALLED(grDepthBufferMode);
    glc_depth_mode(mode);
}

void FX_CALL
grDepthBufferFunction(GrCmpFnc_t function)
{
    CALLED(grDepthBufferFunction);
    glc_depth_func(function);
}

void FX_CALL
grDepthBiasLevel(FxI32 level)
{
    CALLED(grDepthBiasLevel);
    glc_depth_bias(level);
}

/* Glide 3's modes: 1 the table at the fog coordinate (an extension), 2
   the table at q (1/w), 3 iterated Z; 4 (iterated alpha, an extension a
   Voodoo3's Glide does not take) is off. MULT2 and ADD2 as Glide 2's. */
void FX_CALL
grFogMode(GrFogMode_t mode)
{
    static const DWORD device[] = { 0, 4, 2, 3 };
    const DWORD        base     = (DWORD) mode & 0xff;

    CALLED(grFogMode);
    glc_fog_mode((base <= 3) ? (device[base] | ((DWORD) mode & 0x300)) : 0);
}

void FX_CALL
grFogColorValue(GrColor_t fogcolor)
{
    CALLED(grFogColorValue);
    glc_fog_color(fogcolor);
}

void FX_CALL
grFogTable(const GrFog_t ft[])
{
    CALLED(grFogTable);
    glc_fog_table(ft);
}

void FX_CALL
grChromakeyMode(GrChromakeyMode_t mode)
{
    CALLED(grChromakeyMode);
    glc_chroma_mode(mode);
}

void FX_CALL
grChromakeyValue(GrColor_t value)
{
    CALLED(grChromakeyValue);
    glc_chroma_value(value);
}

void FX_CALL
grCullMode(GrCullMode_t mode)
{
    CALLED(grCullMode);
    glc_cull(mode);
}

void FX_CALL
grDitherMode(GrDitherMode_t mode)
{
    CALLED(grDitherMode);
    glc_dither(mode);
}

void FX_CALL
grDisableAllEffects(void)
{
    CALLED(grDisableAllEffects);
    glc_disable_all_effects();
}

/*
 * glide2x.dll: the pixel pipeline's state:
 * the combine units, the constant colour (and Glide 2's flat colour of
 * grConstantColorValue4), blending, the alpha test, depth, fog, chroma
 * key, culling and dithering, and the utility library's presets for them,
 * on the Glide core (glcore.h, state.c).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glide2x.h"
#include "glcore.h"

void FX_CALL
grColorCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local, GrCombineOther_t other,
               FxBool invert)
{
    CALLED(grColorCombine);
    /* Delta0 stays on, as a Voodoo3's Glide 2 keeps it: only a preset or
       the window's open turns it off. */
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

/* Stored for the delta0 presets alone (guColorCombineFunction): the flat
   colour their iterated colour takes instead of the vertices'. */
static float delta0[4];

void FX_CALL
grConstantColorValue4(float a, float r, float g, float b)
{
    CALLED(grConstantColorValue4);
    delta0[0] = r;
    delta0[1] = g;
    delta0[2] = b;
    delta0[3] = a;
    if (glc_delta0_on())
        glc_delta0(1, delta0);
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
grDepthBiasLevel(FxI16 level)
{
    CALLED(grDepthBiasLevel);
    glc_depth_bias(level);
}

/* Glide 2's modes are the device's: off, iterated alpha, the table, Z,
   with MULT2 and ADD2. */
void FX_CALL
grFogMode(GrFogMode_t mode)
{
    const DWORD base = (DWORD) mode & 0xff;

    CALLED(grFogMode);
    glc_fog_mode((base <= 3) ? ((DWORD) mode & 0x3ff) : 0);
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

/* --- The utility library's presets --------------------------------------- */

#define GR_COMBINE_FUNCTION_ZERO                        0x0
#define GR_COMBINE_FUNCTION_LOCAL                       0x1
#define GR_COMBINE_FUNCTION_SCALE_OTHER                 0x3
#define GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL       0x4
#define GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA 0x5
#define GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL     0x6
#define GR_COMBINE_FUNCTION_BLEND                       0x7
#define GR_COMBINE_FACTOR_NONE                          0x0
#define GR_COMBINE_FACTOR_LOCAL                         0x1
#define GR_COMBINE_FACTOR_LOCAL_ALPHA                   0x3
#define GR_COMBINE_FACTOR_TEXTURE_ALPHA                 0x4
#define GR_COMBINE_FACTOR_ONE                           0x8
#define GR_COMBINE_LOCAL_ITERATED                       0x0
#define GR_COMBINE_LOCAL_CONSTANT                       0x1
#define GR_COMBINE_LOCAL_NONE                           0x1
#define GR_COMBINE_OTHER_ITERATED                       0x0
#define GR_COMBINE_OTHER_TEXTURE                        0x1
#define GR_COMBINE_OTHER_NONE                           0x2

/* guColorCombineFunction's presets (GR_COLORCOMBINE_*): the grColorCombine
   call each makes, and whether it is a delta0 one (flat colour). */
static const struct {
    uint8_t fn, factor, local, other, invert, delta0;
} presets[] = {
    { GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_NONE, 0, 0 },
    { GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, 0, 0 },
    { GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, 0, 0 },
    { GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_NONE, 0, 1 },
    { GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, 0, 0 },
    { GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_TEXTURE, 0, 0 },
    { GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0, 0 },
    { GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, 0, 1 },
    { GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE, 0, 0 },
    { GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL_ALPHA, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, 0, 0 },
    { GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_LOCAL_ALPHA, GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE, 0, 0 },
    { GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE,
      0, 0 },
    { GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE, 0, 0 },
    { GR_COMBINE_FUNCTION_BLEND, GR_COMBINE_FACTOR_TEXTURE_ALPHA, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_ITERATED, 0,
      0 },
    { GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_LOCAL_ALPHA, GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE, 0, 0 },
    { GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED,
      GR_COMBINE_OTHER_TEXTURE, 0, 0 },
    { GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_NONE, 1, 0 },
};

void FX_CALL
guColorCombineFunction(GrColorCombineFnc_t fnc)
{
    CALLED(guColorCombineFunction);
    if ((fnc < 0) || (fnc >= (int) (sizeof(presets) / sizeof(presets[0]))))
        return;
    glc_color_combine(presets[fnc].fn, presets[fnc].factor, presets[fnc].local, presets[fnc].other, presets[fnc].invert);
    glc_delta0(presets[fnc].delta0, delta0);
}

/* guAlphaSource's presets: the grAlphaCombine call each makes. */
void FX_CALL
guAlphaSource(GrAlphaSource_t mode)
{
    CALLED(guAlphaSource);
    switch (mode) {
        case 0: /* GR_ALPHASOURCE_CC_ALPHA */
            glc_alpha_combine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT,
                              GR_COMBINE_OTHER_NONE, FXFALSE);
            break;
        case 1: /* ITERATED_ALPHA */
            glc_alpha_combine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                              GR_COMBINE_OTHER_NONE, FXFALSE);
            break;
        case 2: /* TEXTURE_ALPHA */
            glc_alpha_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE,
                              GR_COMBINE_OTHER_TEXTURE, FXFALSE);
            break;
        case 3: /* TEXTURE_ALPHA_TIMES_ITERATED_ALPHA */
            glc_alpha_combine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED,
                              GR_COMBINE_OTHER_TEXTURE, FXFALSE);
            break;
        default:
            break;
    }
}

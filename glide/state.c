/*
 * The Glide core: the pixel pipeline's state, kept as the device's
 * registers (vid_vgpu98_glcmd.h).
 * Each call checks its arguments as a Voodoo3's Glide does and stores
 * Glide's own numbers; the registers go to the device before the next
 * command that reads them.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcore.h"

#define GR_BLEND_ZERO 0x0
#define GR_BLEND_ONE  0x4
#define GR_CMP_LESS   1
#define GR_CMP_ALWAYS 7

static void
field(DWORD reg, DWORD mask, DWORD value)
{
    glc_reg(reg, (glc_reg_get(reg) & ~mask) | (value & mask));
}

/* The state a Voodoo3's Glide leaves after grSstWinOpen (its
   assertDefaultState), with nothing sent: the registers of a new
   session. */
void
glc_state_defaults(DWORD *r, int color_buffers)
{
    int t;

    r[VGPU98_GS_RENDER_BUFFER] = (color_buffers > 1) ? GR_BUFFER_BACKBUFFER : GR_BUFFER_FRONTBUFFER;
    r[VGPU98_GS_MASKS]         = VGPU98_GS_MASK_RGB; /* colour on; alpha and depth off */
    r[VGPU98_GS_COLOR_COMBINE] = VGPU98_GS_COMBINE(VGPU98_GL_FN_SCALE_OTHER, VGPU98_GL_FAC_ONE, VGPU98_GL_LOCAL_ITERATED,
                                                   VGPU98_GL_OTHER_ITERATED, 0);
    r[VGPU98_GS_ALPHA_COMBINE] = VGPU98_GS_COMBINE(VGPU98_GL_FN_SCALE_OTHER, VGPU98_GL_FAC_ONE, VGPU98_GL_LOCAL_CONSTANT,
                                                   VGPU98_GL_OTHER_CONSTANT, 0);
    r[VGPU98_GS_CONSTANT]      = 0xffffffffu;
    r[VGPU98_GS_BLEND]         = GR_BLEND_ONE | (GR_BLEND_ZERO << 8) | (GR_BLEND_ONE << 16) | (GR_BLEND_ZERO << 24);
    r[VGPU98_GS_ALPHA_TEST]    = GR_CMP_ALWAYS;
    r[VGPU98_GS_DEPTH]         = VGPU98_GL_DEPTH_DISABLE | (GR_CMP_LESS << 8);
    r[VGPU98_GS_DITHER]        = 2; /* 4x4 */
    r[VGPU98_GS_LFB_CONSTANT]  = 0xff; /* alpha FFh, depth 0 */
    for (t = 0; t < 2; t++) {
        DWORD *m = &r[VGPU98_GS_TMU(t)];

        m[VGPU98_GS_TEX_CLAMP]  = 1 | (1 << 8);            /* clamp both ways */
        m[VGPU98_GS_TEX_DETAIL] = 0 | (1 << 8) | (255 << 16); /* bias 0, scale 1, the most 1.0 */
    }
}

/* --- The combine units ------------------------------------------------ */

/* A TMU is active when its own combine uses its texel: a factor of LOCAL
   or LOCAL_ALPHA (their ONE_MINUS forms too: the low three bits), or a
   colour or alpha function that adds or subtracts the local or is it
   (LOCAL, LOCAL_ALPHA, 4-9, 10h; not ZERO, SCALE_OTHER, nor a value Glide
   has not), as 3dfx's grTexCombine marks it. A Voodoo3's Glide sends a TMU
   its own coordinates only when it is active. */
int
glc_tmu_active(int tmu)
{
    const DWORD c = glc_reg_get(VGPU98_GS_TMU(tmu) + VGPU98_GS_TEX_COMBINE);
    int         k;

    for (k = 0; k < 2; k++) {
        const DWORD fn  = (c >> (k * 16)) & 0xff;
        const DWORD fac = (c >> (k * 16 + 8)) & 7;

        if ((fac == VGPU98_GL_FAC_LOCAL) || (fac == VGPU98_GL_FAC_LOCAL_ALPHA) ||
            ((fn >= VGPU98_GL_FN_LOCAL) && (fn <= VGPU98_GL_FN_SCALE_MINUS_LOCAL_ADD_LOCAL) && (fn != VGPU98_GL_FN_SCALE_OTHER)) ||
            (fn == VGPU98_GL_FN_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA))
            return 1;
    }
    return 0;
}

void
glc_color_combine(FxI32 fn, FxI32 factor, FxI32 local, FxI32 other, FxBool invert)
{
    glc_reg(VGPU98_GS_COLOR_COMBINE,
            VGPU98_GS_COMBINE((DWORD) fn & 0xff, (DWORD) factor & 0xf, (DWORD) local & 3, (DWORD) other & 3, invert ? 1 : 0));
}

void
glc_alpha_combine(FxI32 fn, FxI32 factor, FxI32 local, FxI32 other, FxBool invert)
{
    glc_reg(VGPU98_GS_ALPHA_COMBINE,
            VGPU98_GS_COMBINE((DWORD) fn & 0xff, (DWORD) factor & 0xf, (DWORD) local & 3, (DWORD) other & 3, invert ? 1 : 0));
}

void
glc_constant(FxU32 color)
{
    glc_reg(VGPU98_GS_CONSTANT, glc_argb(color));
}

void
glc_itrgb(FxBool on)
{
    glc_reg(VGPU98_GS_ITRGB_LIGHTING, on ? 1 : 0);
}

/* --- Blending and the tests -------------------------------------------- */

/* The alpha channel's factors are ONE or ZERO alone: any other source
   factor is ONE, any other destination ZERO, as a Voodoo3's Glide makes
   them. */
void
glc_blend(FxI32 rgb_sf, FxI32 rgb_df, FxI32 alpha_sf, FxI32 alpha_df)
{
    const DWORD asf = ((alpha_sf == GR_BLEND_ZERO) || (alpha_sf == GR_BLEND_ONE)) ? (DWORD) alpha_sf : GR_BLEND_ONE;
    const DWORD adf = ((alpha_df == GR_BLEND_ZERO) || (alpha_df == GR_BLEND_ONE)) ? (DWORD) alpha_df : GR_BLEND_ZERO;

    glc_reg(VGPU98_GS_BLEND, ((DWORD) rgb_sf & 0xf) | (((DWORD) rgb_df & 0xf) << 8) | (asf << 16) | (adf << 24));
}

void
glc_alpha_test(FxI32 fn)
{
    field(VGPU98_GS_ALPHA_TEST, 0xff, (DWORD) fn & 7);
}

void
glc_alpha_ref(FxU32 ref)
{
    field(VGPU98_GS_ALPHA_TEST, 0xff00, (ref & 0xff) << 8);
}

void
glc_depth_mode(FxI32 mode)
{
    if ((mode >= VGPU98_GL_DEPTH_DISABLE) && (mode <= VGPU98_GL_DEPTH_WBUFFER_TO_BIAS))
        field(VGPU98_GS_DEPTH, 0xff, (DWORD) mode);
    if ((mode == VGPU98_GL_DEPTH_ZBUFFER_TO_BIAS) || (mode == VGPU98_GL_DEPTH_WBUFFER_TO_BIAS))
        glc_deferred(GLC_DEFER_COMPARE_TO_BIAS);
}

void
glc_depth_func(FxI32 fn)
{
    field(VGPU98_GS_DEPTH, 0xff00, ((DWORD) fn & 7) << 8);
}

void
glc_depth_bias(FxI32 level)
{
    field(VGPU98_GS_DEPTH, 0xffff0000u, ((DWORD) level & 0xffff) << 16);
}

void
glc_chroma_mode(FxI32 mode)
{
    glc_reg(VGPU98_GS_CHROMA, mode ? 1 : 0);
}

void
glc_chroma_value(FxU32 color)
{
    glc_reg(VGPU98_GS_CHROMA_VALUE, glc_argb(color) & 0xffffff);
}

void
glc_cull(FxI32 mode)
{
    if ((mode >= 0) && (mode <= 2))
        glc_reg(VGPU98_GS_CULL, (DWORD) mode);
}

void
glc_dither(FxI32 mode)
{
    if ((mode >= 0) && (mode <= 2))
        glc_reg(VGPU98_GS_DITHER, (DWORD) mode);
}

/* --- Fog ---------------------------------------------------------------- */

/* The device's fog modes: 0 off, 1 iterated alpha, 2 the table at 1/w,
   3 iterated Z, 4 the table at the fog coordinate; MULT2 100h and ADD2
   200h with them. */
void
glc_fog_mode(DWORD mode)
{
    glc_reg(VGPU98_GS_FOG, mode & 0x3ff);
}

void
glc_fog_color(FxU32 color)
{
    glc_reg(VGPU98_GS_FOG_COLOR, glc_argb(color) & 0xffffff);
}

void
glc_fog_table(const FxU8 *ft)
{
    int i;

    if (!ft)
        return;
    for (i = 0; i < VGPU98_GS_FOG_TABLE_REGS; i++)
        glc_reg(VGPU98_GS_FOG_TABLE + i,
                (DWORD) ft[i * 4] | ((DWORD) ft[i * 4 + 1] << 8) | ((DWORD) ft[i * 4 + 2] << 16) | ((DWORD) ft[i * 4 + 3] << 24));
}

/* grHints' GR_HINT_ALLOW_MIPMAP_DITHER (Glide 2) and grEnable's
   GR_ALLOW_MIPMAP_DITHER (Glide 3): GR_MIPMAP_NEAREST_DITHER may dither
   only when allowed, as a Voodoo3's Glide has it. */
static int mipmap_dither;

void
glc_mipmap_dither(int on)
{
    mipmap_dither = on;
}

int
glc_mipmap_dither_allowed(void)
{
    return mipmap_dither;
}

/* --- The saved state ------------------------------------------------------ */

/* The registers, the frame buffer writes' colour flags, mipmap dithering
   and delta0 (its flag and colour). */
void
glc_state_save(DWORD *out)
{
    float rgb[3];

    memcpy(out, glc_regs(), VGPU98_GS_COUNT * sizeof(DWORD));
    out[VGPU98_GS_COUNT]     = glc_lfb_flags();
    out[VGPU98_GS_COUNT + 1] = (glc_mipmap_dither_allowed() ? 1u : 0u) | (glc_delta0_on() ? 2u : 0u) |
                               (glc_palette_6666() ? 4u : 0u);
    glc_delta0_get(rgb);
    memcpy(&out[VGPU98_GS_COUNT + 2], rgb, sizeof(rgb));
}

void
glc_state_load(const DWORD *in)
{
    float rgb[3];

    glc_regs_load(in);
    glc_lfb_flags_set(in[VGPU98_GS_COUNT]);
    glc_mipmap_dither((in[VGPU98_GS_COUNT + 1] & 1) != 0);
    glc_palette_6666_set((in[VGPU98_GS_COUNT + 1] & 4) != 0);
    memcpy(rgb, &in[VGPU98_GS_COUNT + 2], sizeof(rgb));
    glc_delta0((in[VGPU98_GS_COUNT + 1] & 2) != 0, rgb);
}

/* grDisableAllEffects, as a Voodoo3's Glide does it: no blending, alpha
   test, chroma key, depth bias, depth buffering or fog. */
void
glc_disable_all_effects(void)
{
    glc_blend(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    glc_alpha_test(GR_CMP_ALWAYS);
    glc_chroma_mode(0);
    glc_depth_bias(0);
    glc_depth_mode(VGPU98_GL_DEPTH_DISABLE);
    glc_fog_mode(0);
}

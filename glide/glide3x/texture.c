/*
 * glide3x.dll: textures: the TMUs' memory,
 * downloads, the palette, a texture's source and its settings, and the
 * utility library's .3df files, on the Glide core (glcore.h, tex.c).
 * Glide 3 numbers levels and aspects as log2, the core's own numbers; it
 * has one palette for both TMUs, and the 6666 palette of its extension.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glide3x.h"
#include "glcore.h"

/* GrTexInfo, as Glide 3 programs pass it. */
struct GrTexInfo {
    GrLOD_t           smallLodLog2;
    GrLOD_t           largeLodLog2;
    GrAspectRatio_t   aspectRatioLog2;
    GrTextureFormat_t format;
    void             *data;
};

#define GR_TEXTABLE_NCC0         0
#define GR_TEXTABLE_NCC1         1
#define GR_TEXTABLE_PALETTE      2
#define GR_TEXTABLE_PALETTE_6666 3 /* GR_TEXTABLE_PALETTE_6666_EXT */

/* A Glide 3 description in the core's form; 0 if it is not one. */
static int
tex_of(const GrTexInfo *info, FxU32 even_odd, glc_tex_t *t)
{
    if (!info || (info->smallLodLog2 < 0) || (info->smallLodLog2 > 8) || (info->largeLodLog2 < 0) ||
        (info->largeLodLog2 > 8) || (info->aspectRatioLog2 < -3) || (info->aspectRatioLog2 > 3))
        return 0;
    t->format  = info->format;
    t->small   = info->smallLodLog2;
    t->large   = info->largeLodLog2;
    t->aspect  = info->aspectRatioLog2;
    t->evenodd = (int) even_odd;
    return 1;
}

FxU32 FX_CALL
grTexMinAddress(GrChipID_t tmu)
{
    CALLED(grTexMinAddress);
    (void) tmu;
    return 0;
}

FxU32 FX_CALL
grTexMaxAddress(GrChipID_t tmu)
{
    CALLED(grTexMaxAddress);
    (void) tmu;
    return (glc_tmu_bytes() > 16) ? glc_tmu_bytes() - 16 : 0;
}

FxU32 FX_CALL
grTexCalcMemRequired(GrLOD_t lodmin, GrLOD_t lodmax, GrAspectRatio_t aspect, GrTextureFormat_t fmt)
{
    GrTexInfo info;
    glc_tex_t t;

    CALLED(grTexCalcMemRequired);
    /* Either order: the smaller map has the smaller log2. */
    info.smallLodLog2    = (lodmin < lodmax) ? lodmin : lodmax;
    info.largeLodLog2    = (lodmin < lodmax) ? lodmax : lodmin;
    info.aspectRatioLog2 = aspect;
    info.format          = fmt;
    return tex_of(&info, 3, &t) ? glc_tex_mem_required(&t) : 0;
}

FxU32 FX_CALL
grTexTextureMemRequired(FxU32 evenOdd, GrTexInfo *info)
{
    glc_tex_t t;

    CALLED(grTexTextureMemRequired);
    return tex_of(info, evenOdd, &t) ? glc_tex_mem_required(&t) : 0;
}

void FX_CALL
grTexSource(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info)
{
    glc_tex_t t;

    CALLED(grTexSource);
    if (tex_of(info, evenOdd, &t) && glc_tex_source(tmu, startAddress, &t))
        glide3x_tex_scale(tmu, t.aspect);
}

void FX_CALL
grTexDownloadMipMap(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info)
{
    glc_tex_t t;

    CALLED(grTexDownloadMipMap);
    if (tex_of(info, evenOdd, &t))
        glc_tex_download_all(tmu, startAddress, &t, info->data);
}

void FX_CALL
grTexDownloadMipMapLevel(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod,
                         GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd, void *data)
{
    GrTexInfo info;
    glc_tex_t t;

    CALLED(grTexDownloadMipMapLevel);
    info.smallLodLog2    = 0;
    info.largeLodLog2    = largeLod;
    info.aspectRatioLog2 = aspectRatio;
    info.format          = format;
    if (tex_of(&info, evenOdd, &t) && (thisLod >= 0) && (thisLod <= 8))
        glc_tex_download(tmu, startAddress, &t, thisLod, 0, 255, data);
}

/* Rows start to end of the level; data at the first of them, as a
   Voodoo3's Glide reads it. */
FxBool FX_CALL
grTexDownloadMipMapLevelPartial(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod,
                                GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd, void *data,
                                int start, int end)
{
    GrTexInfo info;
    glc_tex_t t;

    CALLED(grTexDownloadMipMapLevelPartial);
    info.smallLodLog2    = 0;
    info.largeLodLog2    = largeLod;
    info.aspectRatioLog2 = aspectRatio;
    info.format          = format;
    if (!tex_of(&info, evenOdd, &t) || (thisLod < 0) || (thisLod > 8) || (start < 0) || (end < start))
        return FXFALSE;
    glc_tex_download(tmu, startAddress, &t, thisLod, (DWORD) start, (DWORD) end, data);
    return FXTRUE;
}

/* The palette, or the extension's 6666 palette, for both TMUs; NCC tables
   are kept for YIQ textures, which are not decoded yet. */
void FX_CALL
grTexDownloadTable(GrTexTable_t type, void *data)
{
    CALLED(grTexDownloadTable);
    if ((type == GR_TEXTABLE_PALETTE) || (type == GR_TEXTABLE_PALETTE_6666))
        glc_palette((const DWORD *) data, 0, 255, type == GR_TEXTABLE_PALETTE_6666);
    else if ((type == GR_TEXTABLE_NCC0) || (type == GR_TEXTABLE_NCC1)) {
        glc_palette_6666_set(0);
        glc_deferred(GLC_DEFER_YIQ);
    }
}

/* Entries start to end, read from data[start] on: the whole table's
   pointer. */
void FX_CALL
grTexDownloadTablePartial(GrTexTable_t type, void *data, int start, int end)
{
    CALLED(grTexDownloadTablePartial);
    if ((type == GR_TEXTABLE_PALETTE) || (type == GR_TEXTABLE_PALETTE_6666))
        glc_palette((const DWORD *) data, start, end, type == GR_TEXTABLE_PALETTE_6666);
    else if ((type == GR_TEXTABLE_NCC0) || (type == GR_TEXTABLE_NCC1))
        glc_deferred(GLC_DEFER_YIQ);
}

void FX_CALL
grTexNCCTable(GrNCCTable_t table)
{
    CALLED(grTexNCCTable);
    (void) table;
    glc_deferred(GLC_DEFER_YIQ);
}

/* Wrap, clamp, or the extension's mirror. */
void FX_CALL
grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s_clampmode, GrTextureClampMode_t t_clampmode)
{
    CALLED(grTexClampMode);
    glc_tex_clamp(tmu, ((s_clampmode >= 0) && (s_clampmode <= 2)) ? s_clampmode : 0,
                  ((t_clampmode >= 0) && (t_clampmode <= 2)) ? t_clampmode : 0);
}

void FX_CALL
grTexCombine(GrChipID_t tmu, GrCombineFunction_t rgb_function, GrCombineFactor_t rgb_factor,
             GrCombineFunction_t alpha_function, GrCombineFactor_t alpha_factor, FxBool rgb_invert,
             FxBool alpha_invert)
{
    CALLED(grTexCombine);
    glc_tex_combine(tmu, rgb_function, rgb_factor, alpha_function, alpha_factor, rgb_invert, alpha_invert);
}

void FX_CALL
grTexDetailControl(GrChipID_t tmu, int lod_bias, FxU8 detail_scale, float detail_max)
{
    CALLED(grTexDetailControl);
    glc_tex_detail(tmu, lod_bias, detail_scale, detail_max);
}

void FX_CALL
grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t minfilter_mode, GrTextureFilterMode_t magfilter_mode)
{
    CALLED(grTexFilterMode);
    glc_tex_filter(tmu, minfilter_mode, magfilter_mode);
}

void FX_CALL
grTexLodBiasValue(GrChipID_t tmu, float bias)
{
    CALLED(grTexLodBiasValue);
    glc_tex_lod_bias(tmu, bias);
}

void FX_CALL
grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend)
{
    CALLED(grTexMipMapMode);
    glc_tex_mipmap(tmu, mode, lodBlend);
}

/* Several base addresses for one texture (deferred: said once). */
void FX_CALL
grTexMultibase(GrChipID_t tmu, FxBool enable)
{
    CALLED(grTexMultibase);
    (void) tmu;
    if (enable)
        glc_deferred(GLC_DEFER_MULTIBASE);
}

void FX_CALL
grTexMultibaseAddress(GrChipID_t tmu, GrTexBaseRange_t range, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info)
{
    CALLED(grTexMultibaseAddress);
    (void) tmu;
    (void) range;
    (void) startAddress;
    (void) evenOdd;
    (void) info;
    glc_deferred(GLC_DEFER_MULTIBASE);
}

/* --- The utility library's .3df files ----------------------------------- */

/* Glide 3's numbers in the header: log2 levels and aspects. */
FxBool FX_CALL
gu3dfGetInfo(const char *filename, Gu3dfInfo *info)
{
    CALLED(gu3dfGetInfo);
    return glc_3df_info(filename, (glc_3df_info_t *) info, 1);
}

FxBool FX_CALL
gu3dfLoad(const char *filename, Gu3dfInfo *data)
{
    CALLED(gu3dfLoad);
    return glc_3df_load(filename, (glc_3df_info_t *) data, 1);
}

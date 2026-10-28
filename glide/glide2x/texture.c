/*
 * glide2x.dll: textures: the TMUs' memory,
 * downloads, palettes, a texture's source and its settings, and the
 * utility library's texture manager, on the Glide core (glcore.h, tex.c).
 * Glide 2 numbers levels 0 (256) to 8 (1) and aspects 0 (8:1) to 6 (1:8);
 * the core takes Glide 3's log2 forms.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glide2x.h"
#include "glcore.h"
#include "vid_vgpu98_glcpu.h"

/* GrTexInfo, as Glide 2 programs pass it. */
struct GrTexInfo {
    GrLOD_t           smallLod;
    GrLOD_t           largeLod;
    GrAspectRatio_t   aspectRatio;
    GrTextureFormat_t format;
    void             *data;
};

#define GR_TEXTABLE_NCC0    0
#define GR_TEXTABLE_NCC1    1
#define GR_TEXTABLE_PALETTE 2
#define GR_NULL_MIPMAP_HANDLE ((GrMipMapId_t) -1)

static int
lod2(GrLOD_t lod)
{
    return 8 - lod;
}

static int
aspect2(GrAspectRatio_t a)
{
    return 3 - a;
}

/* A Glide 2 description in the core's form; 0 if it is not one. */
static int
tex_of(const GrTexInfo *info, FxU32 even_odd, glc_tex_t *t)
{
    if (!info || (info->smallLod < 0) || (info->smallLod > 8) || (info->largeLod < 0) || (info->largeLod > 8) ||
        (info->aspectRatio < 0) || (info->aspectRatio > 6))
        return 0;
    t->format  = info->format;
    t->small   = lod2(info->smallLod);
    t->large   = lod2(info->largeLod);
    t->aspect  = aspect2(info->aspectRatio);
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
    /* Either order: the smaller map has the larger Glide 2 number. */
    info.smallLod    = (lodmin > lodmax) ? lodmin : lodmax;
    info.largeLod    = (lodmin > lodmax) ? lodmax : lodmin;
    info.aspectRatio = aspect;
    info.format      = fmt;
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
    if (tex_of(info, evenOdd, &t))
        glc_tex_source(tmu, startAddress, &t);
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
    info.smallLod    = 8;
    info.largeLod    = largeLod;
    info.aspectRatio = aspectRatio;
    info.format      = format;
    if (tex_of(&info, evenOdd, &t) && (thisLod >= 0) && (thisLod <= 8))
        glc_tex_download(tmu, startAddress, &t, lod2(thisLod), 0, 255, data);
}

/* Rows start to end of the level; data at the first of them, as a
   Voodoo3's Glide reads it. */
void FX_CALL
grTexDownloadMipMapLevelPartial(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod,
                                GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd, void *data,
                                int start, int end)
{
    GrTexInfo info;
    glc_tex_t t;

    CALLED(grTexDownloadMipMapLevelPartial);
    info.smallLod    = 8;
    info.largeLod    = largeLod;
    info.aspectRatio = aspectRatio;
    info.format      = format;
    if (tex_of(&info, evenOdd, &t) && (thisLod >= 0) && (thisLod <= 8) && (start >= 0) && (end >= start))
        glc_tex_download(tmu, startAddress, &t, lod2(thisLod), (DWORD) start, (DWORD) end, data);
}

/* The palette to both TMUs (a Voodoo3's Glide sends it so); NCC tables are
   kept for YIQ textures, which are not decoded yet. */
void FX_CALL
grTexDownloadTable(GrChipID_t tmu, GrTexTable_t type, void *data)
{
    CALLED(grTexDownloadTable);
    (void) tmu;
    if (type == GR_TEXTABLE_PALETTE)
        glc_palette((const DWORD *) data, 0, 255, 0);
    else if ((type == GR_TEXTABLE_NCC0) || (type == GR_TEXTABLE_NCC1)) {
        glc_palette_6666_set(0);
        glc_deferred(GLC_DEFER_YIQ);
    }
}

/* Entries start to end, read from data[start] on: the whole table's
   pointer. */
void FX_CALL
grTexDownloadTablePartial(GrChipID_t tmu, GrTexTable_t type, void *data, int start, int end)
{
    CALLED(grTexDownloadTablePartial);
    (void) tmu;
    if (type == GR_TEXTABLE_PALETTE)
        glc_palette((const DWORD *) data, start, end, 0);
    else if ((type == GR_TEXTABLE_NCC0) || (type == GR_TEXTABLE_NCC1))
        glc_deferred(GLC_DEFER_YIQ);
}

void FX_CALL
grTexNCCTable(GrChipID_t tmu, GrNCCTable_t table)
{
    CALLED(grTexNCCTable);
    (void) tmu;
    (void) table;
    glc_deferred(GLC_DEFER_YIQ);
}

void FX_CALL
grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s_clampmode, GrTextureClampMode_t t_clampmode)
{
    CALLED(grTexClampMode);
    /* Glide 2 has no mirroring: wrap or clamp. */
    glc_tex_clamp(tmu, s_clampmode & 1, t_clampmode & 1);
}

void FX_CALL
grTexCombine(GrChipID_t tmu, GrCombineFunction_t rgb_function, GrCombineFactor_t rgb_factor,
             GrCombineFunction_t alpha_function, GrCombineFactor_t alpha_factor, FxBool rgb_invert, FxBool alpha_invert)
{
    CALLED(grTexCombine);
    glc_tex_combine(tmu, rgb_function, rgb_factor, alpha_function, alpha_factor, rgb_invert, alpha_invert);
}

/* grTexCombineFunction's presets (GR_TEXTURECOMBINE_*): the function and
   factor each sets for colour and alpha alike, and inversion. */
static void
tex_preset(GrChipID_t tmu, GrTextureCombineFnc_t fnc)
{
    static const uint8_t presets[][3] = {
        { 0x0, 0x0, 0 }, /* ZERO */
        { 0x1, 0x0, 0 }, /* DECAL: this TMU's texel */
        { 0x3, 0x8, 0 }, /* OTHER: the upstream TMU's */
        { 0x4, 0x8, 0 }, /* ADD */
        { 0x3, 0x1, 0 }, /* MULTIPLY */
        { 0x6, 0x8, 0 }, /* SUBTRACT: upstream - this */
        { 0x7, 0xc, 0 }, /* DETAIL: blend by 1 - the detail factor */
        { 0x7, 0x4, 0 }, /* DETAIL_OTHER */
        { 0x7, 0xd, 0 }, /* TRILINEAR_ODD: by 1 - the LOD fraction */
        { 0x7, 0x5, 0 }, /* TRILINEAR_EVEN */
        { 0x0, 0x0, 1 }, /* ONE */
    };

    if ((fnc < 0) || (fnc >= (int) (sizeof(presets) / sizeof(presets[0]))))
        return;
    glc_tex_combine(tmu, presets[fnc][0], presets[fnc][1], presets[fnc][0], presets[fnc][1], presets[fnc][2],
                    presets[fnc][2]);
}

void FX_CALL
grTexCombineFunction(GrChipID_t tmu, GrTextureCombineFnc_t fnc)
{
    CALLED(grTexCombineFunction);
    tex_preset(tmu, fnc);
}

void FX_CALL
guTexCombineFunction(GrChipID_t tmu, GrTextureCombineFnc_t fnc)
{
    CALLED(guTexCombineFunction);
    tex_preset(tmu, fnc);
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

/* --- The utility library's texture manager --------------------------------- */

/* GrMipMapInfo (196 bytes), as programs read it through guTexGetMipMapInfo. */
struct GrMipMapInfo {
    int                   sst;
    FxBool                valid;
    int                   width, height;
    GrAspectRatio_t       aspect_ratio;
    void                 *data;
    GrTextureFormat_t     format;
    GrMipMapMode_t        mipmap_mode;
    GrTextureFilterMode_t magfilter_mode;
    GrTextureFilterMode_t minfilter_mode;
    GrTextureClampMode_t  s_clamp_mode;
    GrTextureClampMode_t  t_clamp_mode;
    FxU32                 tLOD;
    FxU32                 tTextureMode;
    FxU32                 lod_bias;
    GrLOD_t               lod_min, lod_max;
    int                   tmu;
    FxU32                 odd_even_mask;
    FxU32                 tmu_base_address;
    FxBool                trilinear;
    uint8_t               ncc_table[112];
};

_Static_assert((sizeof(void *) != 4) || (sizeof(GrMipMapInfo) == 196), "GrMipMapInfo is not 196 bytes");

#define MAX_MIPMAPS 1024

static GrMipMapInfo mipmaps[MAX_MIPMAPS];
static DWORD        n_mipmaps;
static DWORD        free_at[2];
static GrMipMapId_t current[2] = { GR_NULL_MIPMAP_HANDLE, GR_NULL_MIPMAP_HANDLE };

static GrMipMapInfo *
mipmap(GrMipMapId_t id)
{
    return ((id < n_mipmaps) && mipmaps[id].valid) ? &mipmaps[id] : NULL;
}

static void
describe(const GrMipMapInfo *m, GrTexInfo *info)
{
    info->smallLod    = m->lod_min;
    info->largeLod    = m->lod_max;
    info->aspectRatio = m->aspect_ratio;
    info->format      = m->format;
    info->data        = m->data;
}

/* The manager emptied: at guTexMemReset, and at every grSstWinOpen, as a
   Voodoo3's Glide resets it there. */
void
glide2x_tex_reset(void)
{
    memset(mipmaps, 0, sizeof(mipmaps));
    n_mipmaps  = 0;
    free_at[0] = free_at[1] = 0;
    current[0] = current[1] = GR_NULL_MIPMAP_HANDLE;
}

void FX_CALL
guTexMemReset(void)
{
    CALLED(guTexMemReset);
    glide2x_tex_reset();
}

/* A bump allocation on the TMU, moved to 2 MB if it would straddle it, as
   a Voodoo3's manager keeps the old rule. No texels are sent. */
GrMipMapId_t FX_CALL
guTexAllocateMemory(GrChipID_t tmu, FxU8 odd_even_mask, int width, int height, GrTextureFormat_t fmt,
                    GrMipMapMode_t mm_mode, GrLOD_t smallest_lod, GrLOD_t largest_lod, GrAspectRatio_t aspect,
                    GrTextureClampMode_t s_clamp_mode, GrTextureClampMode_t t_clamp_mode,
                    GrTextureFilterMode_t minfilter_mode, GrTextureFilterMode_t magfilter_mode, float lod_bias,
                    FxBool trilinear)
{
    GrTexInfo     info;
    glc_tex_t     t;
    GrMipMapInfo *m;
    DWORD         size, at;

    CALLED(guTexAllocateMemory);
    if (((tmu != 0) && (tmu != 1)) || (n_mipmaps == MAX_MIPMAPS))
        return GR_NULL_MIPMAP_HANDLE;
    info.smallLod    = smallest_lod;
    info.largeLod    = largest_lod;
    info.aspectRatio = aspect;
    info.format      = fmt;
    if (!tex_of(&info, odd_even_mask, &t))
        return GR_NULL_MIPMAP_HANDLE;
    size = glc_tex_mem_required(&t);
    at   = free_at[tmu];
    if ((at < 0x200000) && (at + size > 0x200000))
        at = 0x200000;
    if (at + size > glc_tmu_bytes())
        return GR_NULL_MIPMAP_HANDLE;
    free_at[tmu]        = at + size;
    m                   = &mipmaps[n_mipmaps];
    memset(m, 0, sizeof(*m));
    m->valid            = FXTRUE;
    m->width            = width;
    m->height           = height;
    m->aspect_ratio     = aspect;
    m->format           = fmt;
    m->mipmap_mode      = mm_mode;
    m->magfilter_mode   = magfilter_mode;
    m->minfilter_mode   = minfilter_mode;
    m->s_clamp_mode     = s_clamp_mode;
    m->t_clamp_mode     = t_clamp_mode;
    m->lod_bias         = (FxU32) glc_lod_steps(lod_bias); /* quarter steps, as a Voodoo3 keeps them */
    m->lod_min          = smallest_lod;
    m->lod_max          = largest_lod;
    m->tmu              = tmu;
    m->odd_even_mask    = odd_even_mask;
    m->tmu_base_address = at;
    m->trilinear        = trilinear;
    return n_mipmaps++;
}

/* The texture's settings on its TMU, and its source. */
static void
apply(const GrMipMapInfo *m)
{
    GrTexInfo info;
    glc_tex_t t;

    describe(m, &info);
    if (!tex_of(&info, m->odd_even_mask, &t))
        return;
    glc_tex_filter(m->tmu, m->minfilter_mode, m->magfilter_mode);
    glc_tex_clamp(m->tmu, m->s_clamp_mode & 1, m->t_clamp_mode & 1);
    glc_tex_mipmap(m->tmu, m->mipmap_mode, m->trilinear);
    glc_tex_lod_bias_steps(m->tmu, (int) m->lod_bias);
    glc_tex_source(m->tmu, m->tmu_base_address, &t);
}

void
glide2x_tex_source(GrMipMapId_t id)
{
    const GrMipMapInfo *m = mipmap(id);

    if (!m)
        return;
    apply(m);
    current[m->tmu] = id;
}

void FX_CALL
guTexSource(GrMipMapId_t id)
{
    CALLED(guTexSource);
    glide2x_tex_source(id);
}

void FX_CALL
guTexDownloadMipMap(GrMipMapId_t mmid, const void *src, const GuNccTable *table)
{
    GrMipMapInfo *m = mipmap(mmid);
    GrTexInfo     info;
    glc_tex_t     t;

    CALLED(guTexDownloadMipMap);
    (void) table;
    if (!m || !src)
        return;
    m->data = (void *) src;
    describe(m, &info);
    if (tex_of(&info, m->odd_even_mask, &t))
        glc_tex_download_all(m->tmu, m->tmu_base_address, &t, src);
}

/* One level, *src moved past it. */
void FX_CALL
guTexDownloadMipMapLevel(GrMipMapId_t mmid, GrLOD_t lod, const void **src)
{
    GrMipMapInfo *m = mipmap(mmid);
    GrTexInfo     info;
    glc_tex_t     t;
    uint32_t      w, h;

    CALLED(guTexDownloadMipMapLevel);
    if (!m || !src || !*src || (lod < 0) || (lod > 8))
        return;
    describe(m, &info);
    if (!tex_of(&info, m->odd_even_mask, &t))
        return;
    glc_tex_download(m->tmu, m->tmu_base_address, &t, lod2(lod), 0, 255, *src);
    vgpu98_gl_level_size(lod2(lod), t.aspect, &w, &h);
    *src = (const uint8_t *) *src + w * h * vgpu98_gl_texel_bytes(t.format);
}

GrMipMapId_t FX_CALL
guTexGetCurrentMipMap(GrChipID_t tmu)
{
    CALLED(guTexGetCurrentMipMap);
    return ((tmu == 0) || (tmu == 1)) ? current[tmu] : GR_NULL_MIPMAP_HANDLE;
}

/* Any argument -1 keeps its value; the memory stays where it is. */
FxBool FX_CALL
guTexChangeAttributes(GrMipMapId_t mmid, int width, int height, GrTextureFormat_t fmt, GrMipMapMode_t mm_mode,
                      GrLOD_t smallest_lod, GrLOD_t largest_lod, GrAspectRatio_t aspect,
                      GrTextureClampMode_t s_clamp_mode, GrTextureClampMode_t t_clamp_mode,
                      GrTextureFilterMode_t minFilterMode, GrTextureFilterMode_t magFilterMode)
{
    GrMipMapInfo *m = mipmap(mmid);

    CALLED(guTexChangeAttributes);
    if (!m)
        return FXFALSE;
    if (width != -1)
        m->width = width;
    if (height != -1)
        m->height = height;
    if (fmt != -1)
        m->format = fmt;
    if (mm_mode != -1)
        m->mipmap_mode = mm_mode;
    if (smallest_lod != -1)
        m->lod_min = smallest_lod;
    if (largest_lod != -1)
        m->lod_max = largest_lod;
    if (aspect != -1)
        m->aspect_ratio = aspect;
    if (s_clamp_mode != -1)
        m->s_clamp_mode = s_clamp_mode;
    if (t_clamp_mode != -1)
        m->t_clamp_mode = t_clamp_mode;
    if (minFilterMode != -1)
        m->minfilter_mode = minFilterMode;
    if (magFilterMode != -1)
        m->magfilter_mode = magFilterMode;
    /* The record alone, as a Voodoo3's: the TMU at the next guTexSource. */
    return FXTRUE;
}

FxU32 FX_CALL
guTexMemQueryAvail(GrChipID_t tmu)
{
    CALLED(guTexMemQueryAvail);
    if ((tmu != 0) && (tmu != 1))
        return 0;
    return (glc_tmu_bytes() > free_at[tmu]) ? glc_tmu_bytes() - free_at[tmu] : 0;
}

GrMipMapInfo *FX_CALL
guTexGetMipMapInfo(GrMipMapId_t mmid)
{
    CALLED(guTexGetMipMapInfo);
    return mipmap(mmid);
}

/* A Voodoo3's library gives a 256 mipmap of solid 565 levels from its C
   library's heap, which the caller frees with its own: this DLL has no C
   library, so the answer is a failed allocation's. */
FxU16 *FX_CALL
guTexCreateColorMipMap(void)
{
    CALLED(guTexCreateColorMipMap);
    return NULL;
}

/* --- The utility library's .3df files ----------------------------------- */

/* Glide 2's numbers in the header: levels 0 (256) to 8, aspects 0 (8:1) to
   6 (1:8). */
FxBool FX_CALL
gu3dfGetInfo(const char *filename, Gu3dfInfo *info)
{
    CALLED(gu3dfGetInfo);
    return glc_3df_info(filename, (glc_3df_info_t *) info, 0);
}

FxBool FX_CALL
gu3dfLoad(const char *filename, Gu3dfInfo *data)
{
    CALLED(gu3dfLoad);
    return glc_3df_load(filename, (glc_3df_info_t *) data, 0);
}

/* --- Run-length textures -------------------------------------------------- */

/* A Voodoo3's Glide 2 on Windows decodes no run-length data here (its line
   decoder does nothing there) and writes the level's rows from a line
   buffer that stays zero: so the rows asked for, and one more when the
   level is taller, become zero, as on a Voodoo3. */
void FX_CALL
ConvertAndDownloadRle(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod,
                      GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd, FxU8 *bm_data, long bm_h,
                      FxU32 u0, FxU32 v0, FxU32 width, FxU32 height, FxU32 dest_width, FxU32 dest_height,
                      FxU16 *tlut)
{
    static const uint8_t zeros[256 * 2]; /* a row */
    GrTexInfo            info;
    glc_tex_t            t;
    FxU32                rows, r;

    CALLED(ConvertAndDownloadRle);
    (void) format;
    (void) bm_h;
    (void) u0;
    (void) v0;
    (void) width;
    (void) dest_width;
    (void) tlut;
    if (!bm_data || !dest_height || (thisLod < 0) || (thisLod > 8))
        return;
    rows             = (height < dest_height) ? height + 1 : dest_height;
    info.smallLod    = 8;
    info.largeLod    = largeLod;
    info.aspectRatio = aspectRatio;
    info.format      = 0xa; /* 565, from the palette of 16-bit entries */
    if (tex_of(&info, evenOdd, &t)) {
        for (r = 0; r < rows; r++)
            glc_tex_download(tmu, startAddress, &t, lod2(thisLod), r, r, zeros);
    }
}

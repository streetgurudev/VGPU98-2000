/*
 * The Glide 2.x interface as glide2x.dll gives it (Glide 2.61, a Voodoo3's):
 * restated from the Glide 2.4 Reference Manual and the 2.43 SDK's headers,
 * with the Voodoo3's own additions, as facts. The
 * functions are the 133 a Voodoo3's glide2x.dll exports (glide2x.exports);
 * their argument types decide their stdcall decoration, which the link
 * checks against that list.
 *
 * Types the DLL only passes along are declared, not defined: each is
 * restated in full by the step that first looks inside it.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_GLIDE2X_H
#define VGPU98_GLIDE2X_H

#include "glidedll.h"

/* --- Types ---------------------------------------------------------------- */

typedef FxU32 GrColor_t;
typedef FxU8  GrAlpha_t;
typedef FxU32 GrMipMapId_t;
typedef FxU8  GrFog_t;

/* Glide's enumerations: each a 32-bit value. */
typedef FxI32 GrChipID_t;
typedef FxI32 GrLOD_t;
typedef FxI32 GrAspectRatio_t;
typedef FxI32 GrTextureFormat_t;
typedef FxI32 GrAlphaBlendFnc_t;
typedef FxI32 GrCombineFunction_t;
typedef FxI32 GrCombineFactor_t;
typedef FxI32 GrCombineLocal_t;
typedef FxI32 GrCombineOther_t;
typedef FxI32 GrCmpFnc_t;
typedef FxI32 GrChromakeyMode_t;
typedef FxI32 GrCullMode_t;
typedef FxI32 GrDepthBufferMode_t;
typedef FxI32 GrDitherMode_t;
typedef FxI32 GrFogMode_t;
typedef FxU32 GrHint_t;
typedef FxU32 GrLock_t;
typedef FxI32 GrBuffer_t;
typedef FxI32 GrLfbWriteMode_t;
typedef FxI32 GrOriginLocation_t;
typedef FxI32 GrLfbSrcFmt_t;
typedef FxI32 GrColorFormat_t;
typedef FxI32 GrScreenResolution_t;
typedef FxI32 GrScreenRefresh_t;
typedef FxI32 GrTextureClampMode_t;
typedef FxI32 GrTextureCombineFnc_t;
typedef FxU32 GrTexTable_t;
typedef FxI32 GrTextureFilterMode_t;
typedef FxI32 GrMipMapMode_t;
typedef FxU32 GrTexBaseRange_t;
typedef FxU32 GrNCCTable_t;
typedef FxI32 GrAlphaSource_t;
typedef FxI32 GrColorCombineFnc_t;
typedef FxI32 GrMPTextureCombineFnc_t;

typedef void (*GrErrorCallbackFnc_t)(const char *string, FxBool fatal);

typedef struct GrVertex           GrVertex;
typedef struct GrState            GrState;
typedef struct GrLfbInfo_t        GrLfbInfo_t;
typedef struct GrTexInfo          GrTexInfo;
typedef struct GuNccTable         GuNccTable;
typedef struct Gu3dfInfo          Gu3dfInfo;
typedef struct GrMipMapInfo       GrMipMapInfo;
typedef struct GrSstPerfStats_t   GrSstPerfStats_t;
typedef struct FxVideoTimingInfo  FxVideoTimingInfo;

/* --- The hardware: grSstQueryBoards and grSstQueryHardware --------------- */

#define MAX_NUM_SST   4 /* boards a configuration describes */
#define GLIDE_NUM_TMU 2 /* TMUs a board's description has room for */

#define GR_SSTTYPE_VOODOO  0 /* Voodoo Graphics, and the Banshee and Voodoo3 */
#define GR_SSTTYPE_SST96   1 /* Voodoo Rush */
#define GR_SSTTYPE_AT3D    2
#define GR_SSTTYPE_Voodoo2 3

typedef struct GrTMUConfig_t {
    FxI32 tmuRev; /* the texture unit's revision */
    FxI32 tmuRam; /* its memory, MB */
} GrTMUConfig_t;

typedef struct GrVoodooConfig_t {
    FxI32         fbRam;    /* the frame buffer's memory, MB */
    FxI32         fbiRev;   /* the pixel chip's revision */
    FxI32         nTexelfx; /* texture units */
    FxBool        sliDetect; /* two boards interleaving scan lines */
    GrTMUConfig_t tmuConfig[GLIDE_NUM_TMU];
} GrVoodooConfig_t;

typedef struct GrSst96Config_t {
    FxI32         fbRam;
    FxI32         nTexelfx;
    GrTMUConfig_t tmuConfig;
} GrSst96Config_t;

typedef struct GrAT3DConfig_t {
    FxI32 rev;
} GrAT3DConfig_t;

typedef struct GrHwConfiguration {
    FxI32 num_sst; /* boards */
    struct {
        FxI32 type; /* GR_SSTTYPE_* */
        union {
            GrVoodooConfig_t VoodooConfig;
            GrSst96Config_t  SST96Config;
            GrAT3DConfig_t   AT3DConfig;
            GrVoodooConfig_t Voodoo2Config;
        } sstBoard;
    } SSTs[MAX_NUM_SST];
} GrHwConfiguration;

_Static_assert(sizeof(GrVoodooConfig_t) == 32, "GrVoodooConfig_t is not 32 bytes");
_Static_assert(sizeof(GrHwConfiguration) == 148, "GrHwConfiguration is not 148 bytes");
_Static_assert(offsetof(GrHwConfiguration, SSTs[1]) == 40, "a board's description is not 36 bytes");

/* --- Initialization and the board ---------------------------------------- */

void FX_CALL   grGlideInit(void);
void FX_CALL   grGlideShutdown(void);
void FX_CALL   grGlideGetVersion(char version[80]);
void FX_CALL   grGlideGetState(GrState *state);
void FX_CALL   grGlideSetState(const GrState *state);
void FX_CALL   grGlideShamelessPlug(const FxBool on);
void FX_CALL   grErrorSetCallback(GrErrorCallbackFnc_t fnc);
void FX_CALL   grHints(GrHint_t hintType, FxU32 hintMask);
FxBool FX_CALL grSstQueryBoards(GrHwConfiguration *hwconfig);
FxBool FX_CALL grSstQueryHardware(GrHwConfiguration *hwconfig);
void FX_CALL   grSstSelect(int which_sst);
FxBool FX_CALL grSstWinOpen(FxU32 hWnd, GrScreenResolution_t screen_resolution, GrScreenRefresh_t refresh_rate,
                            GrColorFormat_t color_format, GrOriginLocation_t origin_location, int nColBuffers,
                            int nAuxBuffers);
void FX_CALL   grSstWinClose(void);
FxBool FX_CALL grSstControl(FxU32 code);
void FX_CALL   grSstOrigin(GrOriginLocation_t origin);
void FX_CALL   grSstVidMode(FxU32 whichSst, FxVideoTimingInfo *vidTimings);
void FX_CALL   grSstConfigPipeline(GrChipID_t chip, FxU32 reg, FxU32 value);
void FX_CALL   grSstIdle(void);
FxBool FX_CALL grSstIsBusy(void);
FxU32 FX_CALL  grSstStatus(void);
FxBool FX_CALL grSstVRetraceOn(void);
FxU32 FX_CALL  grSstVideoLine(void);
FxU32 FX_CALL  grSstScreenWidth(void);
FxU32 FX_CALL  grSstScreenHeight(void);
void FX_CALL   grSstPerfStats(GrSstPerfStats_t *pStats);
void FX_CALL   grSstResetPerfStats(void);
void FX_CALL   grTriStats(FxU32 *trisProcessed, FxU32 *trisDrawn);
void FX_CALL   grResetTriStats(void);
void FX_CALL   grCheckForRoom(FxI32 n);
void FX_CALL   grSplash(float x, float y, float width, float height, FxU32 frame);
/* An internal Voodoo3 export; no program is documented to call it. */
FxU32 FX_CALL  _grGetSipValues(FxU32 a, FxU32 b, FxU32 c);

/* --- Buffers --------------------------------------------------------------- */

void FX_CALL grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU16 depth);
void FX_CALL grBufferSwap(int swap_interval);
int FX_CALL  grBufferNumPending(void);
void FX_CALL grRenderBuffer(GrBuffer_t buffer);
void FX_CALL grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy);
void FX_CALL grColorMask(FxBool rgb, FxBool a);
void FX_CALL grGammaCorrectionValue(float value);

/* --- Drawing --------------------------------------------------------------- */

void FX_CALL grDrawPoint(const GrVertex *pt);
void FX_CALL grDrawLine(const GrVertex *v1, const GrVertex *v2);
void FX_CALL grDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c);
void FX_CALL grDrawPolygon(int nverts, const int ilist[], const GrVertex *vlist);
void FX_CALL grDrawPolygonVertexList(int nverts, const GrVertex *vlist);
void FX_CALL grDrawPlanarPolygon(int nverts, const int ilist[], const GrVertex *vlist);
void FX_CALL grDrawPlanarPolygonVertexList(int nverts, const GrVertex *vlist);
void FX_CALL grAADrawPoint(const GrVertex *pt);
void FX_CALL grAADrawLine(const GrVertex *v1, const GrVertex *v2);
void FX_CALL grAADrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c, FxBool ab_antialias,
                              FxBool bc_antialias, FxBool ca_antialias);
void FX_CALL grAADrawPolygon(const int nverts, const int ilist[], const GrVertex *vlist);
void FX_CALL grAADrawPolygonVertexList(const int nverts, const GrVertex *vlist);

/* --- The pixel pipeline ---------------------------------------------------- */

void FX_CALL grAlphaBlendFunction(GrAlphaBlendFnc_t rgb_sf, GrAlphaBlendFnc_t rgb_df, GrAlphaBlendFnc_t alpha_sf,
                                  GrAlphaBlendFnc_t alpha_df);
void FX_CALL grAlphaCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local,
                            GrCombineOther_t other, FxBool invert);
void FX_CALL grAlphaControlsITRGBLighting(FxBool enable);
void FX_CALL grAlphaTestFunction(GrCmpFnc_t function);
void FX_CALL grAlphaTestReferenceValue(GrAlpha_t value);
void FX_CALL grColorCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local,
                            GrCombineOther_t other, FxBool invert);
void FX_CALL grConstantColorValue(GrColor_t value);
void FX_CALL grConstantColorValue4(float a, float r, float g, float b);
void FX_CALL grChromakeyMode(GrChromakeyMode_t mode);
void FX_CALL grChromakeyValue(GrColor_t value);
void FX_CALL grCullMode(GrCullMode_t mode);
void FX_CALL grDepthBiasLevel(FxI16 level);
void FX_CALL grDepthBufferFunction(GrCmpFnc_t function);
void FX_CALL grDepthBufferMode(GrDepthBufferMode_t mode);
void FX_CALL grDepthMask(FxBool mask);
void FX_CALL grDisableAllEffects(void);
void FX_CALL grDitherMode(GrDitherMode_t mode);
void FX_CALL grFogColorValue(GrColor_t fogcolor);
void FX_CALL grFogMode(GrFogMode_t mode);
void FX_CALL grFogTable(const GrFog_t ft[]);

/* --- The linear frame buffer ----------------------------------------------- */

FxBool FX_CALL grLfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode, GrOriginLocation_t origin,
                         FxBool pixelPipeline, GrLfbInfo_t *info);
FxBool FX_CALL grLfbUnlock(GrLock_t type, GrBuffer_t buffer);
void FX_CALL   grLfbConstantAlpha(GrAlpha_t alpha);
void FX_CALL   grLfbConstantDepth(FxU16 depth);
void FX_CALL   grLfbWriteColorFormat(GrColorFormat_t colorFormat);
void FX_CALL   grLfbWriteColorSwizzle(FxBool swizzleBytes, FxBool swapWords);
FxBool FX_CALL grLfbReadRegion(GrBuffer_t src_buffer, FxU32 src_x, FxU32 src_y, FxU32 src_width, FxU32 src_height,
                               FxU32 dst_stride, void *dst_data);
FxBool FX_CALL grLfbWriteRegion(GrBuffer_t dst_buffer, FxU32 dst_x, FxU32 dst_y, GrLfbSrcFmt_t src_format,
                                FxU32 src_width, FxU32 src_height, FxI32 src_stride, void *src_data);
void FX_CALL   guFbReadRegion(const int srcX, const int srcY, const int w, const int h, const void *dst,
                              const int strideInBytes);
void FX_CALL   guFbWriteRegion(const int dstX, const int dstY, const int w, const int h, const void *src,
                               const int strideInBytes);

/* --- Textures -------------------------------------------------------------- */

FxU32 FX_CALL grTexMinAddress(GrChipID_t tmu);
FxU32 FX_CALL grTexMaxAddress(GrChipID_t tmu);
FxU32 FX_CALL grTexCalcMemRequired(GrLOD_t lodmin, GrLOD_t lodmax, GrAspectRatio_t aspect, GrTextureFormat_t fmt);
FxU32 FX_CALL grTexTextureMemRequired(FxU32 evenOdd, GrTexInfo *info);
void FX_CALL  grTexSource(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info);
void FX_CALL  grTexDownloadMipMap(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info);
void FX_CALL  grTexDownloadMipMapLevel(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod,
                                       GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd,
                                       void *data);
void FX_CALL  grTexDownloadMipMapLevelPartial(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod,
                                              GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd,
                                              void *data, int start, int end);
void FX_CALL  ConvertAndDownloadRle(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod,
                                    GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd,
                                    FxU8 *bm_data, long bm_h, FxU32 u0, FxU32 v0, FxU32 width, FxU32 height,
                                    FxU32 dest_width, FxU32 dest_height, FxU16 *tlut);
void FX_CALL  grTexDownloadTable(GrChipID_t tmu, GrTexTable_t type, void *data);
void FX_CALL  grTexDownloadTablePartial(GrChipID_t tmu, GrTexTable_t type, void *data, int start, int end);
void FX_CALL  grTexNCCTable(GrChipID_t tmu, GrNCCTable_t table);
void FX_CALL  grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s_clampmode, GrTextureClampMode_t t_clampmode);
void FX_CALL  grTexCombine(GrChipID_t tmu, GrCombineFunction_t rgb_function, GrCombineFactor_t rgb_factor,
                           GrCombineFunction_t alpha_function, GrCombineFactor_t alpha_factor, FxBool rgb_invert,
                           FxBool alpha_invert);
void FX_CALL  grTexCombineFunction(GrChipID_t tmu, GrTextureCombineFnc_t fnc);
void FX_CALL  grTexDetailControl(GrChipID_t tmu, int lod_bias, FxU8 detail_scale, float detail_max);
void FX_CALL  grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t minfilter_mode,
                              GrTextureFilterMode_t magfilter_mode);
void FX_CALL  grTexLodBiasValue(GrChipID_t tmu, float bias);
void FX_CALL  grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend);
void FX_CALL  grTexMultibase(GrChipID_t tmu, FxBool enable);
void FX_CALL  grTexMultibaseAddress(GrChipID_t tmu, GrTexBaseRange_t range, FxU32 startAddress, FxU32 evenOdd,
                                    GrTexInfo *info);

/* --- The utility library (gu*) --------------------------------------------- */

void FX_CALL         guAADrawTriangleWithClip(const GrVertex *a, const GrVertex *b, const GrVertex *c);
void FX_CALL         guDrawTriangleWithClip(const GrVertex *a, const GrVertex *b, const GrVertex *c);
void FX_CALL         guDrawPolygonVertexListWithClip(int nverts, const GrVertex *vlist);
void FX_CALL         guAlphaSource(GrAlphaSource_t mode);
void FX_CALL         guColorCombineFunction(GrColorCombineFnc_t fnc);
int FX_CALL          guEncodeRLE16(void *dst, void *src, FxU32 width, FxU32 height);
FxU16 *FX_CALL       guTexCreateColorMipMap(void);
float FX_CALL        guFogTableIndexToW(int i);
void FX_CALL         guFogGenerateExp(GrFog_t fogtable[], float density);
void FX_CALL         guFogGenerateExp2(GrFog_t fogtable[], float density);
void FX_CALL         guFogGenerateLinear(GrFog_t fogtable[], float nearZ, float farZ);
FxU32 FX_CALL        guEndianSwapWords(FxU32 value);
FxU16 FX_CALL        guEndianSwapBytes(FxU16 value);
FxBool FX_CALL       gu3dfGetInfo(const char *filename, Gu3dfInfo *info);
FxBool FX_CALL       gu3dfLoad(const char *filename, Gu3dfInfo *data);
void FX_CALL         guMovieSetName(const char *name);
void FX_CALL         guMovieStart(void);
void FX_CALL         guMovieStop(void);
void FX_CALL         guMPInit(void);
void FX_CALL         guMPTexCombineFunction(GrMPTextureCombineFnc_t tc);
void FX_CALL         guMPTexSource(GrChipID_t virtual_tmu, GrMipMapId_t mmid);
void FX_CALL         guMPDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c);
GrMipMapId_t FX_CALL guTexAllocateMemory(GrChipID_t tmu, FxU8 odd_even_mask, int width, int height,
                                         GrTextureFormat_t fmt, GrMipMapMode_t mm_mode, GrLOD_t smallest_lod,
                                         GrLOD_t largest_lod, GrAspectRatio_t aspect,
                                         GrTextureClampMode_t s_clamp_mode, GrTextureClampMode_t t_clamp_mode,
                                         GrTextureFilterMode_t minfilter_mode, GrTextureFilterMode_t magfilter_mode,
                                         float lod_bias, FxBool trilinear);
FxBool FX_CALL       guTexChangeAttributes(GrMipMapId_t mmid, int width, int height, GrTextureFormat_t fmt,
                                           GrMipMapMode_t mm_mode, GrLOD_t smallest_lod, GrLOD_t largest_lod,
                                           GrAspectRatio_t aspect, GrTextureClampMode_t s_clamp_mode,
                                           GrTextureClampMode_t t_clamp_mode, GrTextureFilterMode_t minFilterMode,
                                           GrTextureFilterMode_t magFilterMode);
void FX_CALL         guTexCombineFunction(GrChipID_t tmu, GrTextureCombineFnc_t fnc);
void FX_CALL         guTexDownloadMipMap(GrMipMapId_t mmid, const void *src, const GuNccTable *table);
void FX_CALL         guTexDownloadMipMapLevel(GrMipMapId_t mmid, GrLOD_t lod, const void **src);
GrMipMapId_t FX_CALL guTexGetCurrentMipMap(GrChipID_t tmu);
GrMipMapInfo *FX_CALL guTexGetMipMapInfo(GrMipMapId_t mmid);
FxU32 FX_CALL        guTexMemQueryAvail(GrChipID_t tmu);
void FX_CALL         guTexMemReset(void);
void FX_CALL         guTexSource(GrMipMapId_t id);

/* The DLL's own: the utility library's texture memory emptied, at
   grSstWinOpen as at guTexMemReset, and a mipmap of it sourced
   (texture.c); grHints' STW hint, for the saved state (vertex.c). */
void  glide2x_tex_reset(void);
void  glide2x_tex_source(GrMipMapId_t mmid);
FxU32 glide2x_stw_hint(void);
void  glide2x_stw_hint_set(FxU32 hint);

#endif /* VGPU98_GLIDE2X_H */

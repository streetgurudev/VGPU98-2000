/*
 * The Glide 3.x interface as glide3x.dll gives it (Glide 3.10, a Voodoo3's):
 * restated from the Glide 3.0 Reference Manual and the 3.00 SDK's headers,
 * with the Voodoo3's own additions, as facts. The
 * functions are the 96 a Voodoo3's glide3x.dll exports (glide3x.exports);
 * their argument types decide their stdcall decoration, which the link
 * checks against that list. Glide 3 renumbers several of Glide 2's
 * enumerations, so the two DLLs share no tables of them.
 *
 * Types the DLL only passes along are declared, not defined: each is
 * restated in full by the step that first looks inside it.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_GLIDE3X_H
#define VGPU98_GLIDE3X_H

#include "glidedll.h"

/* --- Types ---------------------------------------------------------------- */

typedef FxU32 GrColor_t;
typedef FxU8  GrAlpha_t;
typedef FxU8  GrFog_t;
typedef FxU32 GrContext_t; /* a window grSstWinOpen opened; 0 for none */

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
typedef FxU32 GrLock_t;
typedef FxI32 GrBuffer_t;
typedef FxI32 GrLfbWriteMode_t;
typedef FxI32 GrOriginLocation_t;
typedef FxI32 GrLfbSrcFmt_t;
typedef FxI32 GrColorFormat_t;
typedef FxI32 GrScreenResolution_t;
typedef FxI32 GrScreenRefresh_t;
typedef FxI32 GrTextureClampMode_t;
typedef FxU32 GrTexTable_t;
typedef FxI32 GrTextureFilterMode_t;
typedef FxI32 GrMipMapMode_t;
typedef FxU32 GrTexBaseRange_t;
typedef FxU32 GrNCCTable_t;
typedef FxU32 GrCoordinateSpaceMode_t;
typedef FxU32 GrEnableMode_t;

typedef void (*GrErrorCallbackFnc_t)(const char *string, FxBool fatal);
typedef void (FX_CALL *GrProc)(void);

typedef struct GrLfbInfo_t       GrLfbInfo_t;
typedef struct GrTexInfo         GrTexInfo;
typedef struct GrResolution      GrResolution;
typedef struct Gu3dfInfo         Gu3dfInfo;
typedef struct FxVideoTimingInfo FxVideoTimingInfo;

/* --- grGet and grGetString ------------------------------------------------ */

/* The queries the DLL answers. */
#define GR_BITS_DEPTH                0x01
#define GR_BITS_RGBA                 0x02
#define GR_MAX_TEXTURE_SIZE          0x0a
#define GR_MAX_TEXTURE_ASPECT_RATIO  0x0b
#define GR_MEMORY_FB                 0x0c
#define GR_MEMORY_TMU                0x0d
#define GR_MEMORY_UMA                0x0e
#define GR_NUM_BOARDS                0x0f
#define GR_NON_POWER_OF_TWO_TEXTURES 0x10
#define GR_NUM_FB                    0x11
#define GR_NUM_TMU                   0x13
#define GR_REVISION_FB               0x15
#define GR_REVISION_TMU              0x16
#define GR_SUPPORTS_PASSTHRU         0x23
#define GR_BITS_GAMMA                0x2a
#define GR_FIFO_FULLNESS             0x03
#define GR_FOG_TABLE_ENTRIES         0x04
#define GR_GAMMA_TABLE_ENTRIES       0x05
#define GR_GLIDE_STATE_SIZE          0x06
#define GR_GLIDE_VERTEXLAYOUT_SIZE   0x07
#define GR_IS_BUSY                   0x08
#define GR_LFB_PIXEL_PIPE            0x09
#define GR_NUM_SWAP_HISTORY_BUFFER   0x12
#define GR_PENDING_BUFFERSWAPS       0x14
#define GR_STATS_LINES               0x17
#define GR_STATS_PIXELS_AFUNC_FAIL   0x18
#define GR_STATS_PIXELS_CHROMA_FAIL  0x19
#define GR_STATS_PIXELS_DEPTHFUNC_FAIL 0x1a
#define GR_STATS_PIXELS_IN           0x1b
#define GR_STATS_PIXELS_OUT          0x1c
#define GR_STATS_PIXELS              0x1d
#define GR_STATS_POINTS              0x1e
#define GR_STATS_TRIANGLES_IN        0x1f
#define GR_STATS_TRIANGLES_OUT       0x20
#define GR_STATS_TRIANGLES           0x21
#define GR_SWAP_HISTORY              0x22
#define GR_TEXTURE_ALIGN             0x24
#define GR_VIDEO_POSITION            0x25
#define GR_VIEWPORT                  0x26
#define GR_WDEPTH_MIN_MAX            0x27
#define GR_ZDEPTH_MIN_MAX            0x28
#define GR_VERTEX_PARAMETER          0x29

#define GR_EXTENSION 0xa0
#define GR_HARDWARE  0xa1
#define GR_RENDERER  0xa2
#define GR_VENDOR    0xa3
#define GR_VERSION   0xa4

/* --- Initialization and the board ---------------------------------------- */

void FX_CALL        grGlideInit(void);
void FX_CALL        grGlideShutdown(void);
void FX_CALL        grGlideGetState(void *state);
void FX_CALL        grGlideSetState(const void *state);
void FX_CALL        grGlideGetVertexLayout(void *layout);
void FX_CALL        grGlideSetVertexLayout(const void *layout);
FxU32 FX_CALL       grGet(FxU32 pname, FxU32 plength, FxI32 *params);
const char *FX_CALL grGetString(FxU32 pname);
GrProc FX_CALL      grGetProcAddress(char *procName);
FxBool FX_CALL      grReset(FxU32 what);
void FX_CALL        grEnable(GrEnableMode_t mode);
void FX_CALL        grDisable(GrEnableMode_t mode);
void FX_CALL        grErrorSetCallback(GrErrorCallbackFnc_t fnc);
void FX_CALL        grSstSelect(int which_sst);
GrContext_t FX_CALL grSstWinOpen(FxU32 hWnd, GrScreenResolution_t screen_resolution, GrScreenRefresh_t refresh_rate,
                                 GrColorFormat_t color_format, GrOriginLocation_t origin_location, int nColBuffers,
                                 int nAuxBuffers);
FxBool FX_CALL      grSstWinClose(GrContext_t context);
FxBool FX_CALL      grSelectContext(GrContext_t context);
FxI32 FX_CALL       grQueryResolutions(const GrResolution *resTemplate, GrResolution *output);
void FX_CALL        grSstOrigin(GrOriginLocation_t origin);
void FX_CALL        grSstVidMode(FxU32 whichSst, FxVideoTimingInfo *vidTimings);
void FX_CALL        grSstConfigPipeline(GrChipID_t chip, FxU32 reg, FxU32 value);
void FX_CALL        grSetNumPendingBuffers(FxI32 NumPendingBuffers);
void FX_CALL        grCheckForRoom(FxI32 n);
void FX_CALL        grFinish(void);
void FX_CALL        grFlush(void);
void FX_CALL        grSplash(float x, float y, float width, float height, FxU32 frame);

/* --- Coordinates and buffers ------------------------------------------------ */

void FX_CALL grCoordinateSpace(GrCoordinateSpaceMode_t mode);
void FX_CALL grViewport(FxI32 x, FxI32 y, FxI32 width, FxI32 height);
void FX_CALL grDepthRange(FxFloat n, FxFloat f);
void FX_CALL grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU32 depth);
void FX_CALL grBufferSwap(FxU32 swap_interval);
void FX_CALL grRenderBuffer(GrBuffer_t buffer);
void FX_CALL grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy);
void FX_CALL grColorMask(FxBool rgb, FxBool a);
void FX_CALL grLoadGammaTable(FxU32 nentries, FxU32 *red, FxU32 *green, FxU32 *blue);
void FX_CALL guGammaCorrectionRGB(FxFloat red, FxFloat green, FxFloat blue);

/* --- Drawing --------------------------------------------------------------- */

void FX_CALL grVertexLayout(FxU32 param, FxI32 offset, FxU32 mode);
void FX_CALL grDrawPoint(const void *pt);
void FX_CALL grDrawLine(const void *v1, const void *v2);
void FX_CALL grDrawTriangle(const void *a, const void *b, const void *c);
void FX_CALL grAADrawTriangle(const void *a, const void *b, const void *c, FxBool ab_antialias, FxBool bc_antialias,
                              FxBool ca_antialias);
void FX_CALL grDrawVertexArray(FxU32 mode, FxU32 Count, void *pointers);
void FX_CALL grDrawVertexArrayContiguous(FxU32 mode, FxU32 Count, void *pointers, FxU32 stride);

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
void FX_CALL grChromakeyMode(GrChromakeyMode_t mode);
void FX_CALL grChromakeyValue(GrColor_t value);
void FX_CALL grCullMode(GrCullMode_t mode);
void FX_CALL grDepthBiasLevel(FxI32 level);
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
void FX_CALL   grLfbConstantDepth(FxU32 depth);
void FX_CALL   grLfbWriteColorFormat(GrColorFormat_t colorFormat);
void FX_CALL   grLfbWriteColorSwizzle(FxBool swizzleBytes, FxBool swapWords);
FxBool FX_CALL grLfbReadRegion(GrBuffer_t src_buffer, FxU32 src_x, FxU32 src_y, FxU32 src_width, FxU32 src_height,
                               FxU32 dst_stride, void *dst_data);
FxBool FX_CALL grLfbWriteRegion(GrBuffer_t dst_buffer, FxU32 dst_x, FxU32 dst_y, GrLfbSrcFmt_t src_format,
                                FxU32 src_width, FxU32 src_height, FxBool pixelPipeline, FxI32 src_stride,
                                void *src_data);

/* --- Textures -------------------------------------------------------------- */

FxU32 FX_CALL  grTexMinAddress(GrChipID_t tmu);
FxU32 FX_CALL  grTexMaxAddress(GrChipID_t tmu);
FxU32 FX_CALL  grTexCalcMemRequired(GrLOD_t lodmin, GrLOD_t lodmax, GrAspectRatio_t aspect, GrTextureFormat_t fmt);
FxU32 FX_CALL  grTexTextureMemRequired(FxU32 evenOdd, GrTexInfo *info);
void FX_CALL   grTexSource(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info);
void FX_CALL   grTexDownloadMipMap(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info);
void FX_CALL   grTexDownloadMipMapLevel(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod,
                                        GrAspectRatio_t aspectRatio, GrTextureFormat_t format, FxU32 evenOdd,
                                        void *data);
FxBool FX_CALL grTexDownloadMipMapLevelPartial(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod,
                                               GrLOD_t largeLod, GrAspectRatio_t aspectRatio,
                                               GrTextureFormat_t format, FxU32 evenOdd, void *data, int start,
                                               int end);
void FX_CALL   grTexDownloadTable(GrTexTable_t type, void *data);
void FX_CALL   grTexDownloadTablePartial(GrTexTable_t type, void *data, int start, int end);
void FX_CALL   grTexNCCTable(GrNCCTable_t table);
void FX_CALL   grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s_clampmode, GrTextureClampMode_t t_clampmode);
void FX_CALL   grTexCombine(GrChipID_t tmu, GrCombineFunction_t rgb_function, GrCombineFactor_t rgb_factor,
                            GrCombineFunction_t alpha_function, GrCombineFactor_t alpha_factor, FxBool rgb_invert,
                            FxBool alpha_invert);
void FX_CALL   grTexDetailControl(GrChipID_t tmu, int lod_bias, FxU8 detail_scale, float detail_max);
void FX_CALL   grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t minfilter_mode,
                               GrTextureFilterMode_t magfilter_mode);
void FX_CALL   grTexLodBiasValue(GrChipID_t tmu, float bias);
void FX_CALL   grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend);
void FX_CALL   grTexMultibase(GrChipID_t tmu, FxBool enable);
void FX_CALL   grTexMultibaseAddress(GrChipID_t tmu, GrTexBaseRange_t range, FxU32 startAddress, FxU32 evenOdd,
                                     GrTexInfo *info);

/* --- The utility library (gu*) --------------------------------------------- */

int FX_CALL    guEncodeRLE16(void *dst, void *src, FxU32 width, FxU32 height);
float FX_CALL  guFogTableIndexToW(int i);
void FX_CALL   guFogGenerateExp(GrFog_t *fogtable, float density);
void FX_CALL   guFogGenerateExp2(GrFog_t *fogtable, float density);
void FX_CALL   guFogGenerateLinear(GrFog_t *fogtable, float nearZ, float farZ);
FxBool FX_CALL gu3dfGetInfo(const char *filename, Gu3dfInfo *info);
FxBool FX_CALL gu3dfLoad(const char *filename, Gu3dfInfo *data);

/* The DLL's own (vertex.c, texture.c): drawing's state at the open,
   grTexSource's scales for clip coordinates, the viewport, the layout's
   reset, and drawing's part of the saved state. */
void  glide3x_draw_open(void);
void  glide3x_tex_scale(FxI32 tmu, int aspect);
void  glide3x_viewport(FxI32 *out);
void  glide3x_layout_reset(void);
DWORD glide3x_draw_state_bytes(void);
void  glide3x_draw_state_get(void *out);
void  glide3x_draw_state_set(const void *in);

#endif /* VGPU98_GLIDE3X_H */

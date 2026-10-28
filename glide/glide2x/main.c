/*
 * glide2x.dll: the Glide 2.x interface on the VGPU98 2000, answering as a
 * Voodoo3 (spec/glide.md):
 * the board, its window, and the init and shutdown around them; the screen
 * and the frame buffer in frame.c, on the Glide core (glcore.h). Exports
 * that do nothing yet are in stubs.c.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glide2x.h"
#include "glcore.h"

const char glide_tag[] = "VGPU98 glide2x " VGPU98_BUILD_STAMP;
const char glide_api[] = "Glide 2.61";

/* The one board, as a Voodoo3's Glide 2 describes it; none without the card. */
static void
describe(GrHwConfiguration *hwc)
{
    GrVoodooConfig_t *v = &hwc->SSTs[0].sstBoard.VoodooConfig;
    int               k;

    memset(hwc, 0, sizeof(*hwc));
    if (!glide_card())
        return;
    hwc->num_sst      = 1;
    hwc->SSTs[0].type = GR_SSTTYPE_VOODOO;
    v->fbRam          = VOODOO3_FB_MB;
    v->fbiRev         = VOODOO3_FBI_REV;
    v->nTexelfx       = VOODOO3_TMUS;
    v->sliDetect      = FXFALSE;
    for (k = 0; k < VOODOO3_TMUS; k++) {
        v->tmuConfig[k].tmuRev = VOODOO3_TMU_REV;
        v->tmuConfig[k].tmuRam = VOODOO3_TMU_MB;
    }
}

/* Allowed before grGlideInit: the number of boards only, as Glide fills it. */
FxBool FX_CALL
grSstQueryBoards(GrHwConfiguration *hwconfig)
{
    CALLED(grSstQueryBoards);
    hwconfig->num_sst = glide_card() ? 1 : 0;
    return FXTRUE;
}

FxBool FX_CALL
grSstQueryHardware(GrHwConfiguration *hwconfig)
{
    CALLED(grSstQueryHardware);
    describe(hwconfig);
    return hwconfig->num_sst > 0;
}

/* Allowed before grGlideInit. */
void FX_CALL
grGlideGetVersion(char version[80])
{
    CALLED(grGlideGetVersion);
    memcpy(version, VOODOO3_GLIDE2, sizeof(VOODOO3_GLIDE2));
}

void FX_CALL
grGlideInit(void)
{
    CALLED(grGlideInit);
}

void FX_CALL
grGlideShutdown(void)
{
    CALLED(grGlideShutdown);
    glc_close();
    glide_shutdown_seen();
    glide_record(); /* at Diagnostics Glide */
}

void FX_CALL
grSstSelect(int which_sst)
{
    CALLED(grSstSelect);
    (void) which_sst;
}

/* The screen and a session on the card's Glide device (glcore.h). */
FxBool FX_CALL
grSstWinOpen(FxU32 hWnd, GrScreenResolution_t screen_resolution, GrScreenRefresh_t refresh_rate,
             GrColorFormat_t color_format, GrOriginLocation_t origin_location, int nColBuffers, int nAuxBuffers)
{
    CALLED(grSstWinOpen);
    if (!glc_open(hWnd, (FxU32) screen_resolution, (FxU32) refresh_rate, (FxU32) color_format, (FxU32) origin_location,
                  nColBuffers, nAuxBuffers))
        return FXFALSE;
    /* The utility library's texture memory starts empty, as a Voodoo3's
       Glide resets it at the open. */
    glide2x_tex_reset();
    return FXTRUE;
}

void FX_CALL
grSstWinClose(void)
{
    CALLED(grSstWinClose);
    glc_close();
}

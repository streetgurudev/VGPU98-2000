/*
 * glide3x.dll: the Glide 3.x interface on the VGPU98 2000, answering as a
 * Voodoo3 (spec/glide.md):
 * grGet and grGetString for the board, its window and context, the saved
 * state and the init and shutdown around them; the screen and the frame
 * buffer in frame.c, drawing in vertex.c, the pixel pipeline in pixel.c,
 * textures in texture.c, on the Glide core (glcore.h).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glide3x.h"
#include "glcore.h"

const char glide_tag[] = "VGPU98 glide3x " VGPU98_BUILD_STAMP;
const char glide_api[] = "Glide 3.10";

/* Before a window is open, a Voodoo3's Glide 3 counts the frame buffer's
   memory 2 MB short of what Glide 2 reports. */
#define VOODOO3_GLIDE3_FB_MB (VOODOO3_FB_MB - 2)

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

/* One value of a 4-byte query. */
static FxU32
answer(FxU32 plength, FxI32 *params, FxI32 value)
{
    if (plength != 4)
        return 0;
    *params = value;
    return plength;
}

/* Two values of an 8-byte query. */
static FxU32
answer2(FxU32 plength, FxI32 *params, FxI32 a, FxI32 b)
{
    if (plength != 8)
        return 0;
    params[0] = a;
    params[1] = b;
    return plength;
}

/* The saved state: the core's, then drawing's. */
static DWORD
state_bytes(void)
{
    return GLC_STATE_DWORDS * 4 + glide3x_draw_state_bytes();
}

/* The board's numbers, as a Voodoo3's Glide 3 gives them: the bytes
   written, or 0 (Glide's failure) for a query it does not know, a wrong
   length or no params. Without the card, no board. */
FxU32 FX_CALL
grGet(FxU32 pname, FxU32 plength, FxI32 *params)
{
    CALLED(grGet);
    if (!params)
        return 0;
    if (!glide_card())
        return (pname == GR_NUM_BOARDS) ? answer(plength, params, 0) : 0;
    switch (pname) {
        case GR_FIFO_FULLNESS: /* the FIFO empty: its room, and the status */
            return answer2(plength, params, 0xffff, 0x0ffff01f);
        case GR_FOG_TABLE_ENTRIES:
            return answer(plength, params, 64);
        case GR_GAMMA_TABLE_ENTRIES:
            return answer(plength, params, 256);
        case GR_GLIDE_STATE_SIZE:
            return answer(plength, params, (FxI32) state_bytes());
        case GR_GLIDE_VERTEXLAYOUT_SIZE:
            return answer(plength, params, 108);
        case GR_IS_BUSY:
            return answer(plength, params, glc_busy() ? 1 : 0);
        case GR_LFB_PIXEL_PIPE:
            return answer(plength, params, 1);
        case GR_NUM_SWAP_HISTORY_BUFFER:
            return answer(plength, params, 8);
        case GR_PENDING_BUFFERSWAPS:
            return answer(plength, params, glc_pending());
        case GR_STATS_LINES:
            return answer(plength, params, (FxI32) glc_counter(GLC_LINES));
        case GR_STATS_POINTS:
            return answer(plength, params, (FxI32) glc_counter(GLC_POINTS));
        case GR_STATS_TRIANGLES_IN:
            return answer(plength, params, (FxI32) glc_counter(GLC_TRIANGLES_IN));
        case GR_STATS_TRIANGLES_OUT:
            return answer(plength, params, (FxI32) glc_counter(GLC_TRIANGLES));
        case GR_STATS_PIXELS_AFUNC_FAIL: /* the pixel counters: none kept */
        case GR_STATS_PIXELS_CHROMA_FAIL:
        case GR_STATS_PIXELS_DEPTHFUNC_FAIL:
        case GR_STATS_PIXELS_IN:
        case GR_STATS_PIXELS_OUT:
            return answer(plength, params, 0);
        case GR_SWAP_HISTORY:
            if (plength != 32)
                return 0;
            memset(params, 0, 32);
            return plength;
        case GR_TEXTURE_ALIGN:
            return answer(plength, params, 16);
        case GR_VIDEO_POSITION:
            return answer2(plength, params, 0, 0);
        case GR_VIEWPORT:
            if (plength != 16)
                return 0;
            glide3x_viewport(params);
            return plength;
        case GR_WDEPTH_MIN_MAX:
            return answer2(plength, params, 0, 0xffff);
        case GR_ZDEPTH_MIN_MAX:
            return answer2(plength, params, 0xffff, 0);
        case GR_BITS_DEPTH:
            return answer(plength, params, 16);
        case GR_BITS_RGBA: /* the frame buffer's 5-6-5 */
            if (plength != 16)
                return 0;
            params[0] = 5;
            params[1] = 6;
            params[2] = 5;
            params[3] = 0;
            return plength;
        case GR_BITS_GAMMA:
            return answer(plength, params, 8);
        case GR_MAX_TEXTURE_SIZE:
            return answer(plength, params, 256);
        case GR_MAX_TEXTURE_ASPECT_RATIO: /* log2: 8:1 */
            return answer(plength, params, 3);
        case GR_MEMORY_FB: /* the window's buffers, or before one Glide 3's count */
            return answer(plength, params, glc_is_open() ? (FxI32) glc_fb_bytes() : (VOODOO3_GLIDE3_FB_MB << 20));
        case GR_MEMORY_TMU:
            return answer(plength, params, (FxI32) glc_tmu_bytes());
        case GR_MEMORY_UMA:
            return answer(plength, params, VOODOO3_MEMORY_MB << 20);
        case GR_NUM_BOARDS:
        case GR_NUM_FB:
            return answer(plength, params, 1);
        case GR_NUM_TMU:
            return answer(plength, params, VOODOO3_TMUS);
        case GR_REVISION_FB:
        case GR_REVISION_TMU:
            return answer(plength, params, VOODOO3_REVISION);
        case GR_NON_POWER_OF_TWO_TEXTURES:
        case GR_SUPPORTS_PASSTHRU:
            return answer(plength, params, FXFALSE);
        default: {
            /* The first such queries are noted. */
            static volatile DWORD unanswered;

            if (__atomic_add_fetch(&unanswered, 1, __ATOMIC_ACQ_REL) <= 16)
                glide_dbg("grGet %x: not a query a Voodoo3 answers", pname);
            return 0;
        }
    }
}

/* The library's strings, as a Voodoo3's Glide 3 gives them; the board's
   name only with the card. The extension list is empty (leading and
   trailing spaces kept, so a program searching it for " NAME " finds
   nothing): no extension is implemented yet. */
const char *FX_CALL
grGetString(FxU32 pname)
{
    CALLED(grGetString);
    switch (pname) {
        case GR_EXTENSION:
            return " ";
        case GR_HARDWARE:
            return glide_card() ? "Voodoo3 (tm)" : "ERROR";
        case GR_RENDERER:
            return "Glide";
        case GR_VENDOR:
            return "3Dfx Interactive";
        case GR_VERSION:
            return VOODOO3_GLIDE3;
        default:
            return "ERROR";
    }
}

/* No extension is offered, so no extension's function: NULL for every
   name. The first names asked for are noted for the steps to come. */
GrProc FX_CALL
grGetProcAddress(char *procName)
{
    static volatile DWORD asked;

    CALLED(grGetProcAddress);
    if (__atomic_add_fetch(&asked, 1, __ATOMIC_ACQ_REL) <= 16) {
        const char *s = procName ? procName : "(null)";
        char        name[65]; /* at most 64 characters of it, so the line fits */
        int         n;

        for (n = 0; s[n] && (n < 64); n++)
            name[n] = s[n];
        name[n] = 0;
        glide_dbg("grGetProcAddress %s: none", name);
    }
    return NULL;
}

void FX_CALL
grSstSelect(int which_sst)
{
    CALLED(grSstSelect);
    (void) which_sst;
}

/* The screen and a session on the card's Glide device (glcore.h): the
   window's context is 1, the process's one. */
#define CONTEXT 1

GrContext_t FX_CALL
grSstWinOpen(FxU32 hWnd, GrScreenResolution_t screen_resolution, GrScreenRefresh_t refresh_rate,
             GrColorFormat_t color_format, GrOriginLocation_t origin_location, int nColBuffers, int nAuxBuffers)
{
    CALLED(grSstWinOpen);
    if (!glc_open(hWnd, (FxU32) screen_resolution, (FxU32) refresh_rate, (FxU32) color_format, (FxU32) origin_location,
                  nColBuffers, nAuxBuffers))
        return 0;
    glide3x_draw_open();
    return CONTEXT;
}

FxBool FX_CALL
grSstWinClose(GrContext_t context)
{
    CALLED(grSstWinClose);
    if (context != CONTEXT)
        return FXFALSE;
    /* A lost context: FXFALSE, as a Voodoo3's Glide answers (the desktop
       is given back all the same). */
    if (!glc_is_open()) {
        glc_close();
        return FXFALSE;
    }
    glc_close();
    return FXTRUE;
}

/* The context is the program's while its window is open and its screen
   kept; lost (a mode change: the program then opens its window again, as
   after a Voodoo3 lost it), FXFALSE. */
FxBool FX_CALL
grSelectContext(GrContext_t context)
{
    CALLED(grSelectContext);
    return ((context == CONTEXT) && glc_is_open()) ? FXTRUE : FXFALSE;
}

FxBool FX_CALL
grReset(FxU32 what)
{
    CALLED(grReset);
    switch (what) {
        case GR_STATS_POINTS:
            glc_counter_reset(GLC_POINTS);
            return FXTRUE;
        case GR_STATS_LINES:
            glc_counter_reset(GLC_LINES);
            return FXTRUE;
        case GR_STATS_TRIANGLES:
            glc_counter_reset(GLC_TRIANGLES_IN);
            glc_counter_reset(GLC_TRIANGLES);
            return FXTRUE;
        case GR_STATS_PIXELS:
            return FXTRUE;
        case GR_VERTEX_PARAMETER:
            glide3x_layout_reset();
            return FXTRUE;
        default:
            return FXFALSE;
    }
}

/* The whole state: the registers and what the core keeps, then the
   layout, the coordinate space, the viewport and the depth range. */
void FX_CALL
grGlideGetState(void *state)
{
    DWORD core[GLC_STATE_DWORDS];

    CALLED(grGlideGetState);
    if (!state)
        return;
    glc_sync();
    glc_state_save(core);
    memcpy(state, core, sizeof(core));
    glide3x_draw_state_get((uint8_t *) state + sizeof(core));
}

void FX_CALL
grGlideSetState(const void *state)
{
    DWORD core[GLC_STATE_DWORDS];

    CALLED(grGlideSetState);
    if (!state)
        return;
    memcpy(core, state, sizeof(core));
    glc_state_load(core);
    glide3x_draw_state_set((const uint8_t *) state + sizeof(core));
}

/* GrResolution, as grQueryResolutions fills them. */
typedef struct resolution {
    FxI32 resolution, refresh, color_buffers, aux_buffers;
} resolution_t;

#define GR_QUERY_ANY   0xffffffffu
#define GR_REFRESH_NONE 0xff

/* The resolutions' sizes (GR_RESOLUTION_320x200 to 2048x2048). */
static const uint16_t res_sizes[][2] = {
    { 320, 200 },   { 320, 240 },   { 400, 256 },   { 512, 384 },   { 640, 200 },   { 640, 350 },
    { 640, 400 },   { 640, 480 },   { 800, 600 },   { 960, 720 },   { 856, 480 },   { 512, 256 },
    { 1024, 768 },  { 1280, 1024 }, { 1600, 1200 }, { 400, 300 },   { 1152, 864 },  { 1280, 960 },
    { 1600, 1024 }, { 1792, 1344 }, { 1856, 1392 }, { 1920, 1440 }, { 2048, 1536 }, { 2048, 2048 },
};

/* Does Windows have the size at 16 bpp (the display driver's list)? With
   no list, every size. */
static int
mode_listed(DWORD w, DWORD h)
{
    DWORD i;

    if (!sb || !sb->num_modes)
        return 1;
    for (i = 0; (i < sb->num_modes) && (i < SB_MAX_MODES); i++) {
        if ((sb->modes[i].dwWidth == w) && (sb->modes[i].dwHeight == h) && (sb->modes[i].dwBPP == 16))
            return 1;
    }
    return 0;
}

/* Every resolution, refresh rate and buffer count the template allows that
   Windows has at 16 bpp and that leaves a Voodoo3 its smallest texture
   memory: their bytes (16 each), written to output unless it is NULL. */
FxI32 FX_CALL
grQueryResolutions(const GrResolution *resTemplate, GrResolution *output)
{
    const resolution_t *t   = (const resolution_t *) resTemplate;
    resolution_t       *out = (resolution_t *) output;
    FxU32               r, hz, c, a, n = 0;

    CALLED(grQueryResolutions);
    if (!t || !glide_card())
        return 0;
    if (((FxU32) t->resolution != GR_QUERY_ANY) && ((FxU32) t->resolution > 0x17))
        return 0;
    if (((FxU32) t->refresh != GR_QUERY_ANY) && ((FxU32) t->refresh != GR_REFRESH_NONE) && ((FxU32) t->refresh > 8))
        return 0;
    if (((FxU32) t->color_buffers != GR_QUERY_ANY) && ((t->color_buffers < 1) || (t->color_buffers > 3)))
        return 0;
    if (((FxU32) t->aux_buffers != GR_QUERY_ANY) && ((t->aux_buffers < 0) || (t->aux_buffers > 1)))
        return 0;
    for (r = 0; r <= 0x17; r++) {
        const DWORD w = res_sizes[r][0], h = res_sizes[r][1];

        if ((((FxU32) t->resolution != GR_QUERY_ANY) && ((FxU32) t->resolution != r)) || !mode_listed(w, h))
            continue;
        for (hz = 0; hz <= 8; hz++) {
            if (((FxU32) t->refresh != GR_QUERY_ANY) && ((FxU32) t->refresh != GR_REFRESH_NONE) && ((FxU32) t->refresh != hz))
                continue;
            for (c = 1; c <= 3; c++) {
                if (((FxU32) t->color_buffers != GR_QUERY_ANY) && ((FxU32) t->color_buffers != c))
                    continue;
                for (a = 0; a <= 1; a++) {
                    if (((FxU32) t->aux_buffers != GR_QUERY_ANY) && ((FxU32) t->aux_buffers != a))
                        continue;
                    if ((uint64_t) w * h * 2 * (c + a) >= ((uint64_t) VOODOO3_MEMORY_MB << 20) - (4u << 20) - 65536)
                        continue;
                    if (out) {
                        out[n].resolution    = (FxI32) r;
                        out[n].refresh       = (FxI32) hz;
                        out[n].color_buffers = (FxI32) c;
                        out[n].aux_buffers   = (FxI32) a;
                    }
                    n++;
                }
            }
        }
    }
    return (FxI32) (n * sizeof(resolution_t));
}

/* Kept, never called: the device reports nothing to it. */
static GrErrorCallbackFnc_t error_callback;

void FX_CALL
grErrorSetCallback(GrErrorCallbackFnc_t fnc)
{
    CALLED(grErrorSetCallback);
    error_callback = fnc;
    (void) error_callback;
}

/* What a Voodoo3's Glide does for these needs nothing of the device. */
void FX_CALL
grCheckForRoom(FxI32 n)
{
    CALLED(grCheckForRoom);
    (void) n;
}

void FX_CALL
grSplash(float x, float y, float width, float height, FxU32 frame)
{
    CALLED(grSplash);
    (void) x;
    (void) y;
    (void) width;
    (void) height;
    (void) frame;
}

void FX_CALL
grSstConfigPipeline(GrChipID_t chip, FxU32 reg, FxU32 value)
{
    CALLED(grSstConfigPipeline);
    (void) chip;
    (void) reg;
    (void) value;
}

void FX_CALL
grSstVidMode(FxU32 whichSst, FxVideoTimingInfo *vidTimings)
{
    CALLED(grSstVidMode);
    (void) whichSst;
    (void) vidTimings;
}

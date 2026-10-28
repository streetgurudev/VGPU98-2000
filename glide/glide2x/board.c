/*
 * glide2x.dll: the board's other calls: the
 * saved state, the statistics, and the calls a Voodoo3's Glide 2 answers
 * without the hardware, on the Glide core (glcore.h).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glide2x.h"
#include "glcore.h"

/* GrState: 312 bytes, the core's state and the STW hint. */
#define STATE_DWORDS (GLC_STATE_DWORDS + 1)

_Static_assert(STATE_DWORDS * 4 == 312, "Glide 2's GrState is not 312 bytes");

void FX_CALL
grGlideGetState(GrState *state)
{
    DWORD s[STATE_DWORDS];

    CALLED(grGlideGetState);
    if (!state)
        return;
    glc_sync();
    glc_state_save(s);
    s[GLC_STATE_DWORDS] = glide2x_stw_hint();
    memcpy(state, s, sizeof(s));
}

void FX_CALL
grGlideSetState(const GrState *state)
{
    DWORD s[STATE_DWORDS];

    CALLED(grGlideSetState);
    if (!state)
        return;
    memcpy(s, state, sizeof(s));
    glc_state_load(s);
    glide2x_stw_hint_set(s[GLC_STATE_DWORDS]);
}

/* A Voodoo3's Glide 2 does nothing for it and answers TRUE. */
FxBool FX_CALL
grSstControl(FxU32 code)
{
    CALLED(grSstControl);
    (void) code;
    return FXTRUE;
}

/* GrSstPerfStats_t: the pixel counters, none of them kept by the device. */
struct GrSstPerfStats_t {
    FxU32 pixelsIn, chromaFail, zFuncFail, aFuncFail, pixelsOut;
};

void FX_CALL
grSstPerfStats(GrSstPerfStats_t *pStats)
{
    CALLED(grSstPerfStats);
    if (pStats)
        memset(pStats, 0, sizeof(*pStats));
}

void FX_CALL
grSstResetPerfStats(void)
{
    CALLED(grSstResetPerfStats);
}

/* Triangles asked for, and drawn (sent to the device). */
void FX_CALL
grTriStats(FxU32 *trisProcessed, FxU32 *trisDrawn)
{
    CALLED(grTriStats);
    if (trisProcessed)
        *trisProcessed = glc_counter(GLC_TRIANGLES_IN);
    if (trisDrawn)
        *trisDrawn = glc_counter(GLC_TRIANGLES);
}

void FX_CALL
grResetTriStats(void)
{
    CALLED(grResetTriStats);
    glc_counter_reset(GLC_TRIANGLES_IN);
    glc_counter_reset(GLC_TRIANGLES);
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
grGlideShamelessPlug(const FxBool on)
{
    CALLED(grGlideShamelessPlug);
    (void) on;
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

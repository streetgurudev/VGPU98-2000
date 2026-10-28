/*
 * glide2x.dll: the exports that do nothing:
 * each notes its first call, touches
 * none of its arguments and returns 0. A Voodoo3's Glide 2 records no
 * movies either, and its _grGetSipValues is the library's own.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glide2x.h"

/* The arguments are named for the prototypes alone. */
#pragma GCC diagnostic ignored "-Wunused-parameter"

FxU32 FX_CALL
_grGetSipValues(FxU32 a, FxU32 b, FxU32 c)
{
    NOT_YET(_grGetSipValues);
    return 0;
}

void FX_CALL
guMovieSetName(const char *name)
{
    NOT_YET(guMovieSetName);
}

void FX_CALL
guMovieStart(void)
{
    NOT_YET(guMovieStart);
}

void FX_CALL
guMovieStop(void)
{
    NOT_YET(guMovieStop);
}

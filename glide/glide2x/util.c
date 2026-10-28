/*
 * glide2x.dll: the utility library's byte swaps, which need no card and are
 * exactly defined. guFogTableIndexToW is both
 * DLLs' (glide/gu.c).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glide2x.h"

/* The two 16-bit halves exchanged. */
FxU32 FX_CALL
guEndianSwapWords(FxU32 value)
{
    CALLED(guEndianSwapWords);
    return (value >> 16) | (value << 16);
}

/* The two bytes exchanged. */
FxU16 FX_CALL
guEndianSwapBytes(FxU16 value)
{
    CALLED(guEndianSwapBytes);
    return (FxU16) ((value >> 8) | (value << 8));
}

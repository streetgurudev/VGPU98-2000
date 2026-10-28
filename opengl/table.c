/*
 * The table of GL 1.1's 336 functions DrvSetContext returns (glfuncs.h's
 * order, the one Windows' opengl32.dll calls through): each entry is the
 * ICD's ogl_<Name>, a stdcall function taking GL's arguments.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "icd.h"
#include "glcalls.h"

#define GLF(index, ret, name, params, args, stack) [index] = (void *) ogl_##name,

const ogl_proc_table_t ogl_table = {
    OGL_TABLE_ENTRIES,
    {
#include "glfuncs.h"
    },
};

_Static_assert(GLF_COUNT == OGL_TABLE_ENTRIES, "the table's size is GL 1.1's 336 functions");

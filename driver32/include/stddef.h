/*
 * The DLL's freestanding stddef.h (it uses no host headers): from gcc's
 * predefined macros, for the 32-bit target.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_STDDEF_H
#define VGPU98_STDDEF_H

typedef __SIZE_TYPE__    size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;

#define NULL ((void *) 0)
#define offsetof(type, member) __builtin_offsetof(type, member)

#endif

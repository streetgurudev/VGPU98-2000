/*
 * The Glide DLLs' host test (test_glide.c): memory below 4 GB, where the
 * shared block and the register window go, since the DLLs hold their
 * addresses in 32 bits as on Windows 98. Apart from test_glide.c because
 * windows.h and the DLLs' own declarations of KERNEL32 do not mix.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stddef.h>
#include <stdint.h>

#include <windows.h>

void *test_low_alloc(size_t size);

void *
test_low_alloc(size_t size)
{
    uintptr_t at;

    for (at = 0x20000000u; at < 0xf0000000u; at += 0x01000000u) {
        void *p = VirtualAlloc((void *) at, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

        if (p)
            return p;
    }
    return NULL;
}

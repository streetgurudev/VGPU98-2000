/*
 * Minimal test helpers for the VGPU98 tool tests.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_CHECK_H
#define VGPU98_CHECK_H

#include <stdio.h>

static int check_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            check_failures++;                                                \
        }                                                                    \
    } while (0)

#define CHECK_DONE()                                                \
    do {                                                            \
        if (check_failures) {                                       \
            fprintf(stderr, "%d check(s) failed\n", check_failures); \
            return 1;                                               \
        }                                                           \
        return 0;                                                   \
    } while (0)

#endif /* VGPU98_CHECK_H */

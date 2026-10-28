/*
 * VGPU98 authoring tools: SHA-256 (FIPS 180-4).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_SHA256_H
#define VGPU98_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct sha256_t {
    uint32_t state[8];
    uint64_t length; /* bytes hashed so far */
    uint8_t  block[64];
    size_t   used;
} sha256_t;

void sha256_init(sha256_t *ctx);
void sha256_update(sha256_t *ctx, const void *data, size_t len);
void sha256_final(sha256_t *ctx, uint8_t digest[32]);

/* Hashes a buffer and writes 64 lower-case hex digits plus a NUL to `hex`. */
void sha256_hex(const void *data, size_t len, char hex[65]);

#endif /* VGPU98_SHA256_H */

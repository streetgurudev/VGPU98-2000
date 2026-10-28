/*
 * The Glide device's arithmetic, which the DLLs and 86Box share
 * (card/vid_vgpu98_glcpu.h): level sizes, offsets and
 * memory as a Voodoo3's Glide lays a texture out, the texel formats'
 * decoding, and the W-buffer float and fog values the pixel shader
 * computes, against Glide's manuals' and the Voodoo specs' numbers.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vid_vgpu98_glcpu.h"

static int failures;

#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (!(cond)) {                                    \
            fprintf(stderr, "test_gltex:%d: ", __LINE__); \
            fprintf(stderr, __VA_ARGS__);                 \
            fputc('\n', stderr);                          \
            failures++;                                   \
        }                                                 \
    } while (0)

static void
sizes(void)
{
    static const struct {
        int      lod, aspect;
        uint32_t w, h;
    } t[] = {
        { 8, 0, 256, 256 }, { 8, 1, 256, 128 }, { 8, 3, 256, 32 }, { 2, 3, 4, 1 }, { 1, 1, 2, 1 },
        { 0, 3, 1, 1 },     { 8, -1, 128, 256 }, { 4, -2, 4, 16 },  { 3, 3, 8, 1 },
    };
    size_t i;

    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        uint32_t w, h;

        vgpu98_gl_level_size(t[i].lod, t[i].aspect, &w, &h);
        CHECK((w == t[i].w) && (h == t[i].h), "level %d aspect %d: %ux%u, expected %ux%u", t[i].lod, t[i].aspect, w, h,
              t[i].w, t[i].h);
    }
}

static void
memory(void)
{
    /* The Programming Guide's example: 128x64 down to 8x4, 8-bit. */
    vgpu98_gltex_t t = { 0x3, 3, 7, 1, 3 };

    CHECK(vgpu98_gl_mem_required(&t) == 10912, "both: %u", vgpu98_gl_mem_required(&t));
    t.evenodd = 2;
    CHECK(vgpu98_gl_mem_required(&t) == 8736, "odd: %u", vgpu98_gl_mem_required(&t));
    t.evenodd = 1;
    CHECK(vgpu98_gl_mem_required(&t) == 2176, "even: %u", vgpu98_gl_mem_required(&t));
    /* A split texture's small levels count padded: an 8:1 texture from 8x1
       down, 16-bit, odd levels (8x1 is level 3, 2x1 level 1). */
    {
        vgpu98_gltex_t s = { 0xa, 0, 3, 3, 2 };

        CHECK(vgpu98_gl_mem_required(&s) == ((16 + 4) * 2 + 15) / 16 * 16, "padded: %u", vgpu98_gl_mem_required(&s));
    }
    /* Offsets: a 256 texture from 0, the next level after it; a 128 one at
       its start; 1x1 alone at start + 8 (the levels above it end 8 bytes
       past a 16-byte line). */
    {
        vgpu98_gltex_t a = { 0xa, 0, 8, 0, 3 };
        vgpu98_gltex_t b = { 0xa, 0, 7, 0, 3 };
        vgpu98_gltex_t c = { 0xa, 0, 0, 0, 3 };

        CHECK(vgpu98_gl_level_offset(&a, 8) == 0, "256's offset %u", vgpu98_gl_level_offset(&a, 8));
        CHECK(vgpu98_gl_level_offset(&a, 7) == 131072, "256's 128 level at %u", vgpu98_gl_level_offset(&a, 7));
        CHECK(vgpu98_gl_level_offset(&b, 7) == 0, "128's offset %u", vgpu98_gl_level_offset(&b, 7));
        CHECK(vgpu98_gl_level_offset(&b, 6) == 32768, "128's 64 level at %u", vgpu98_gl_level_offset(&b, 6));
        CHECK(vgpu98_gl_level_offset(&c, 0) == 8, "1x1 at %u", vgpu98_gl_level_offset(&c, 0));
        CHECK(vgpu98_gl_has_level(&b, 7) && !vgpu98_gl_has_level(&b, 8), "the 128 texture's levels");
    }
    /* A split texture: its parity's levels alone, one after another. */
    {
        vgpu98_gltex_t e = { 0x3, 0, 8, 0, 1 };

        CHECK(vgpu98_gl_has_level(&e, 8) && !vgpu98_gl_has_level(&e, 7) && vgpu98_gl_has_level(&e, 6),
              "the even levels");
        CHECK(vgpu98_gl_level_offset(&e, 6) == 65536, "the even texture's 64 level at %u", vgpu98_gl_level_offset(&e, 6));
    }
}

static void
decode(void)
{
    uint8_t  src[4];
    uint32_t out[2];
    uint32_t palette[256];

    src[0] = 0x00;
    src[1] = 0xf8; /* 565 red */
    src[2] = 0xe0;
    src[3] = 0x07; /* 565 green */
    CHECK(vgpu98_gl_decode(0xa, src, 2, 1, NULL, out) && (out[0] == 0xff0000ffu) && (out[1] == 0xff00ff00u), "565: %08X %08X",
          out[0], out[1]);
    src[0] = 0x1f;
    src[1] = 0x80; /* 1555: alpha, blue */
    CHECK(vgpu98_gl_decode(0xb, src, 1, 1, NULL, out) && (out[0] == 0xffff0000u), "1555: %08X", out[0]);
    src[0] = 0x21;
    src[1] = 0x43; /* 4444: a 4, r 3, g 2, b 1 */
    CHECK(vgpu98_gl_decode(0xc, src, 1, 1, NULL, out) && (out[0] == 0x44112233u), "4444: %08X", out[0]);
    src[0] = 0xe0; /* 332 red */
    src[1] = 0x1f; /* 332 green and blue */
    CHECK(vgpu98_gl_decode(0x0, src, 2, 1, NULL, out) && (out[0] == 0xff0000ffu) && (out[1] == 0xffffff00u), "332: %08X %08X",
          out[0], out[1]);
    src[0] = 0xf0; /* AI44: alpha F, intensity 0 */
    CHECK(vgpu98_gl_decode(0x4, src, 1, 1, NULL, out) && (out[0] == 0xff000000u), "AI44: %08X", out[0]);
    memset(palette, 0, sizeof(palette));
    palette[7] = 0x00123456;
    src[0]     = 7;
    src[1]     = 0x80; /* AP88: index 7, alpha 80h */
    CHECK(vgpu98_gl_decode(0x5, src, 1, 1, palette, out) && (out[0] == 0xff563412u), "P8: %08X", out[0]);
    CHECK(vgpu98_gl_decode(0xe, src, 1, 1, palette, out) && (out[0] == 0x80563412u), "AP88: %08X", out[0]);
    CHECK(!vgpu98_gl_decode(0x7, src, 1, 1, palette, out), "a reserved format decoded");
}

static void
depth_fog(void)
{
    uint8_t table[64];
    int     i;

    /* The Voodoo2 spec's float, by the facts' reading. */
    CHECK(vgpu98_gl_wfloat(0.5f) == 0x1000, "1/w 0.5: %X", vgpu98_gl_wfloat(0.5f));
    CHECK(vgpu98_gl_wfloat(1.0f) == 0, "1/w 1: %X", vgpu98_gl_wfloat(1.0f));
    CHECK(vgpu98_gl_wfloat(0.99999f) == 1, "1/w just under 1: %X", vgpu98_gl_wfloat(0.99999f));
    CHECK(vgpu98_gl_wfloat(1.0f / 65536.0f) == 0xffff, "1/w 2^-16: %X", vgpu98_gl_wfloat(1.0f / 65536.0f));
    CHECK(vgpu98_gl_wfloat(1e-9f) == 0xffff, "1/w tiny: %X", vgpu98_gl_wfloat(1e-9f));
    CHECK(vgpu98_gl_wfloat(0.25f) == 0x2000, "1/w 0.25: %X", vgpu98_gl_wfloat(0.25f));
    CHECK(vgpu98_gl_wfloat(0.3f) > vgpu98_gl_wfloat(0.4f), "the float grows with w");
    for (i = 0; i < 64; i++)
        table[i] = (uint8_t) (i * 4);
    /* 1/w 0.5 is w 2, the table's entry 4. */
    CHECK(vgpu98_gl_fog(table, vgpu98_gl_wfloat(0.5f)) == 16, "fog at w 2: %d", vgpu98_gl_fog(table, vgpu98_gl_wfloat(0.5f)));
    CHECK(vgpu98_gl_fog(table, 0x1000 + 0x200) == 16 + 2, "fog halfway to entry 5: %d", vgpu98_gl_fog(table, 0x1200));
    CHECK(vgpu98_gl_fog(table, 0xffff) == 252, "fog at the far end: %d", vgpu98_gl_fog(table, 0xffff));
}

int
main(void)
{
    sizes();
    memory();
    decode();
    depth_fog();
    printf("test_gltex: %d failed\n", failures);
    return failures ? 1 : 0;
}

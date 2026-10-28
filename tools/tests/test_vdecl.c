/*
 * The DLL's vertex shader declarations (driver32/vdecl.c) on the host:
 * DirectX 8 declaration tokens built by hand, as the GPU's DECLARE elements
 * for the fixed function and for vertex programs (INPUT elements), the
 * constants they load, and what they refuse.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "vid_vgpu98_gpu.h"
#include "vdecl.h"

/* DirectX 8's declaration tokens */
#define TSTREAM(s)      ((1u << 29) | (uint32_t) (s))
#define TREG(r, t)      ((2u << 29) | ((uint32_t) (t) << 16) | (uint32_t) (r))
#define TSKIP(n)        ((2u << 29) | 0x10000000u | ((uint32_t) (n) << 16))
#define TTESS           (3u << 29)
#define TCONST(a, n)    ((4u << 29) | ((uint32_t) (n) << 25) | (uint32_t) (a))
#define TEXT(n)         ((5u << 29) | ((uint32_t) (n) << 24))
#define TEND            0xFFFFFFFFu

/* DirectX 8's types and registers */
#define F1  0
#define F2  1
#define F3  2
#define F4  3
#define COL 4
#define UB4 5
#define SH2 6
#define SH4 7
#define R_POSITION 0
#define R_NORMAL   3
#define R_DIFFUSE  5
#define R_TEX0     7

#define N(a) ((uint32_t) sizeof(a))

static uint32_t decl[2 * VGPU98_DECL_MAX];
static uint32_t n;
static uint32_t streams;

static int
parse(const uint32_t *tok, uint32_t bytes, int program)
{
    memset(decl, 0xA5, sizeof(decl));
    return vd_parse((const uint8_t *) tok, bytes, program, decl, &n, &streams);
}

static int
element(uint32_t i, uint32_t stream, uint32_t type, uint32_t use, uint32_t index, uint32_t offset)
{
    if ((decl[2 * i] != VGPU98_DECL(stream, type, use, index)) || (decl[2 * i + 1] != offset)) {
        fprintf(stderr, "element %u: %08x at %u, %08x at %u expected\n", i, decl[2 * i], decl[2 * i + 1],
                VGPU98_DECL(stream, type, use, index), offset);
        return 0;
    }
    return 1;
}

static void
test_fixed(void)
{
    static const uint32_t a[] = { TSTREAM(0), TREG(R_POSITION, F3), TREG(R_NORMAL, F3), TREG(R_DIFFUSE, COL), TREG(R_TEX0, F2),
                                  TEND };
    static const uint32_t nopos[]  = { TSTREAM(0), TREG(R_NORMAL, F3), TEND };
    static const uint32_t twice[]  = { TSTREAM(0), TREG(R_POSITION, F3), TREG(R_TEX0, F2), TREG(R_TEX0, F2), TEND };
    static const uint32_t badtype[] = { TSTREAM(0), TREG(R_POSITION, F3), TREG(R_NORMAL, F2), TEND };
    static const uint32_t noend[]  = { TSTREAM(0), TREG(R_POSITION, F3) };

    CHECK(parse(a, N(a), 0) == VD_OK);
    CHECK(n == 4);
    CHECK(streams == 1);
    CHECK(element(0, 0, VGPU98_DT_FLOAT3, VGPU98_DU_POSITION, 0, 0));
    CHECK(element(1, 0, VGPU98_DT_FLOAT3, VGPU98_DU_NORMAL, 0, 12));
    CHECK(element(2, 0, VGPU98_DT_D3DCOLOR, VGPU98_DU_COLOR, 0, 24));
    CHECK(element(3, 0, VGPU98_DT_FLOAT2, VGPU98_DU_TEXCOORD, 0, 28));
    CHECK(parse(nopos, N(nopos), 0) == VD_BAD);
    CHECK(parse(twice, N(twice), 0) == VD_BAD);
    CHECK(parse(badtype, N(badtype), 0) == VD_BAD);
    CHECK(parse(noend, N(noend), 0) == VD_OK); /* it runs to its size */
    CHECK(n == 1);
}

static void
test_program(void)
{
    static const uint32_t a[] = {
        TSTREAM(0), TREG(0, F3), TSKIP(2), TREG(3, COL),
        TSTREAM(1), TREG(5, UB4), TREG(7, SH2), TREG(8, SH4),
        TCONST(10, 2), 0x3F800000u, 0, 0, 0x3F800000u, 1, 2, 3, 4,
        TEXT(1), 0x12345678u,
        TEND,
    };
    static const uint32_t twice[]  = { TSTREAM(0), TREG(0, F3), TREG(0, F2), TEND };
    static const uint32_t r16[]    = { TSTREAM(0), TREG(16, F3), TEND };
    static const uint32_t none[]   = { TSTREAM(0), TEND };
    static const uint32_t tstrm[]  = { TSTREAM(0) | 0x10000000u, TREG(0, F3), TEND };
    static const uint32_t ttok[]   = { TSTREAM(0), TREG(0, F3), TTESS, TEND };
    static const uint32_t nostrm[] = { TREG(0, F3), TEND };
    static const uint32_t type8[]  = { TSTREAM(0), TREG(0, 8), TEND };
    static const uint32_t nop1[]   = { TSTREAM(0), 1, TEND };
    static const uint32_t badend[] = { TSTREAM(0), TREG(0, F3), 7u << 29 };
    static const uint32_t exact[]  = { TSTREAM(0), TSKIP(15), TSKIP(15), TSKIP(15), TSKIP(15), TREG(0, F4), TEND };
    static const uint32_t past[]   = { TSTREAM(0), TSKIP(15), TSKIP(15), TSKIP(15), TSKIP(15), TREG(0, F4), TREG(1, F1), TEND };
    static const uint32_t ffpast[] = { TSTREAM(0), TREG(R_POSITION, F3), TSKIP(15), TSKIP(15), TSKIP(15), TSKIP(15),
                                       TREG(R_TEX0, F2), TEND };
    uint32_t              consts[2 * 5];

    CHECK(parse(a, N(a), 1) == VD_OK);
    CHECK(n == 5);
    CHECK(streams == 3);
    CHECK(element(0, 0, VGPU98_DT_FLOAT3, VGPU98_DU_INPUT, 0, 0));
    CHECK(element(1, 0, VGPU98_DT_D3DCOLOR, VGPU98_DU_INPUT, 3, 20));
    CHECK(element(2, 1, VGPU98_DT_UBYTE4, VGPU98_DU_INPUT, 5, 0));
    CHECK(element(3, 1, VGPU98_DT_SHORT2, VGPU98_DU_INPUT, 7, 4));
    CHECK(element(4, 1, VGPU98_DT_SHORT4, VGPU98_DU_INPUT, 8, 8));
    CHECK(vd_consts((const uint8_t *) a, N(a), NULL) == 2);
    CHECK(vd_consts((const uint8_t *) a, N(a), consts) == 2);
    CHECK((consts[0] == 10) && (consts[1] == 0x3F800000u) && (consts[4] == 0x3F800000u));
    CHECK((consts[5] == 11) && (consts[6] == 1) && (consts[9] == 4));
    /* The fixed function reads none of these registers as uses. */
    CHECK(parse(a, N(a), 0) == VD_BAD);
    CHECK(parse(twice, N(twice), 1) == VD_BAD);
    CHECK(parse(r16, N(r16), 1) == VD_BAD);
    CHECK(parse(none, N(none), 1) == VD_OK); /* a program may load nothing */
    CHECK(n == 0);
    CHECK(parse(tstrm, N(tstrm), 1) == VD_BAD);
    CHECK(parse(ttok, N(ttok), 1) == VD_BAD);
    CHECK(parse(nostrm, N(nostrm), 1) == VD_BAD);
    CHECK(parse(type8, N(type8), 1) == VD_BAD);
    CHECK(parse(nop1, N(nop1), 1) == VD_BAD);
    CHECK(parse(badend, N(badend), 1) == VD_BAD);
    CHECK(parse(exact, N(exact), 1) == VD_OK);
    CHECK(element(0, 0, VGPU98_DT_FLOAT4, VGPU98_DU_INPUT, 0, 240));
    CHECK(parse(past, N(past), 1) == VD_STRIDE);
    CHECK(parse(ffpast, N(ffpast), 0) == VD_STRIDE);
    /* A CONSTMEM cut short loads the registers whose four dwords are there. */
    {
        static const uint32_t cut[] = { TCONST(0, 3), 1, 2, 3, 4, 5, 6 };

        CHECK(vd_consts((const uint8_t *) cut, N(cut), NULL) == 1);
    }
}

int
main(void)
{
    test_fixed();
    test_program();
    CHECK_DONE();
}

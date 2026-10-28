/*
 * The DLL's shader translator (driver32/xlate.c) on the host: DirectX 8
 * token streams built by hand, and the one 3DMark2001 SE makes, translated
 * and compared with the GPU programs spec/directx-hal.md says they become;
 * every program it writes passed through the GPU's own check
 * (card/vid_vgpu98_prog.h), and that check's refusals.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "vid_vgpu98_prog.h"
#include "xlate.h"

/* DirectX 8's tokens */
#define PSV(n)   (0xFFFF0100u | (n))
#define VSV(n)   (0xFFFE0100u | (n))
#define END      0x0000FFFFu
#define PHASE    0x0000FFFDu
#define CO       0x40000000u
#define PARAM    0x80000000u
#define COMMENT(n) (0xFFFEu | ((uint32_t) (n) << 16))
#define DT(type, reg, mask) (PARAM | ((uint32_t) (type) << 28) | (uint32_t) (reg) | ((uint32_t) (mask) << 16))
#define SAT      (1u << 20)
#define SH(s)    (((uint32_t) (s) & 15u) << 24)
#define ST(type, reg, swz, mod) \
    (PARAM | ((uint32_t) (type) << 28) | (uint32_t) (reg) | ((uint32_t) (swz) << 16) | ((uint32_t) (mod) << 24))
#define REL      (1u << 13)
#define F1       0x3F800000u /* 1.0f */

/* Register types */
#define R  0
#define V  1
#define C  2
#define T  3
#define A  3
#define RO 4 /* oPos 0, oFog 1, oPts 2 */
#define AO 5 /* oD0, oD1 */
#define TO 6 /* oT0-oT7 */

/* Swizzles */
#define XYZW 0xE4u
#define XXXX 0x00u
#define YYYY 0x55u
#define ZZZZ 0xAAu
#define WWWW 0xFFu

/* The GPU's */
#define I(op, f, r, m) VGPU98_INS(VGPU98_PI_##op, VGPU98_PF_##f, r, m)
#define G(f, r)        VGPU98_SRC(VGPU98_PF_##f, r, XYZW, 0)
#define GS(f, r, s)    VGPU98_SRC(VGPU98_PF_##f, r, s, 0)
#define GM(f, r, s, m) VGPU98_SRC(VGPU98_PF_##f, r, s, m)
#define STAGE(s)       G(STAGE, s)
#define NC             VGPU98_INS_NOCLAMP
#define PROJ           VGPU98_INS_PROJECT
#define WSAT           VGPU98_INS_SAT

static xl_out_t out;

/* Compares the program with the instructions expected, naming the first that differs. */
static int
same(const char *name, const xl_out_t *o, const uint32_t *want, uint32_t n)
{
    uint32_t i;

    if (o->n != n) {
        fprintf(stderr, "%s: %u instructions, %u expected\n", name, o->n, n);
        return 0;
    }
    for (i = 0; i < 4 * n; i++) {
        if (o->ins[i] != want[i]) {
            fprintf(stderr, "%s: instruction %u dword %u is %08x, %08x expected\n", name, i / 4, i % 4, o->ins[i], want[i]);
            return 0;
        }
    }
    return 1;
}

static uint32_t range1 = F1;

static int
pixel(const uint32_t *tok, uint32_t n)
{
    memset(&out, 0xA5, sizeof(out));
    return xl_pixel(tok, n, range1, &out);
}

static int
vertex(const uint32_t *tok, uint32_t n)
{
    memset(&out, 0xA5, sizeof(out));
    return xl_vertex(tok, n, &out);
}

#define N(a) ((uint32_t) (sizeof(a) / sizeof((a)[0])))

/* --- 3DMark2001 SE's shader (Nature's water, the Pixel Shader test) ------- */

static void
test_3dmark(void)
{
    static const uint32_t tok[] = {
        PSV(0),
        66, DT(T, 0, 15),                         /* tex t0 */
        73, DT(T, 1, 15), ST(T, 0, XYZW, 0),      /* texm3x3pad t1, t0 */
        73, DT(T, 2, 15), ST(T, 0, XYZW, 0),      /* texm3x3pad t2, t0 */
        77, DT(T, 3, 15), ST(T, 0, XYZW, 0),      /* texm3x3vspec t3, t0 */
        1, DT(R, 0, 7), ST(T, 3, XYZW, 0),        /* mov r0.rgb, t3 */
        1, DT(R, 0, 8), ST(V, 0, WWWW, 0),        /* mov r0.a, v0.a */
        END,
    };
    static const uint32_t want[] = {
        I(SAMPLE, TEMP, 2, 15) | PROJ, G(COORD, 0), STAGE(0), 0,
        I(DP3, TEMP, 3, 1) | NC, G(COORD, 1), G(TEMP, 2), 0,
        I(DP3, TEMP, 4, 1) | NC, G(COORD, 2), G(TEMP, 2), 0,
        I(DP3, TEMP, 6, 4) | NC, G(COORD, 3), G(TEMP, 2), 0,
        I(MOV, TEMP, 6, 1) | NC, GS(TEMP, 3, XXXX), 0, 0,
        I(MOV, TEMP, 6, 2) | NC, GS(TEMP, 4, XXXX), 0, 0,
        I(MOV, TEMP, 7, 1) | NC, GS(COORD, 1, WWWW), 0, 0,
        I(MOV, TEMP, 7, 2) | NC, GS(COORD, 2, WWWW), 0, 0,
        I(MOV, TEMP, 7, 4) | NC, GS(COORD, 3, WWWW), 0, 0,
        I(RFL, TEMP, 7, 15) | NC, G(TEMP, 6), G(TEMP, 7), 0,
        I(SAMPLE, TEMP, 5, 15), G(TEMP, 7), STAGE(3), 0,
        I(MOV, TEMP, 0, 7), G(TEMP, 5), 0, 0,
        I(MOV, TEMP, 0, 8), GS(INPUT, 0, WWWW), 0, 0,
    };

    CHECK(pixel(tok, N(tok)));
    CHECK(same("3dmark", &out, want, N(want) / 4));
    CHECK(out.kind == VGPU98_PROG_PIXEL);
    CHECK(out.version == PSV(0));
    CHECK(out.info.stages == 0x9);
    CHECK(out.info.coords == 0xF);
    CHECK(out.info.inputs == 0x1);
    CHECK(out.n_def == 0);
    CHECK(out.n_in == 6);
    /* The same without END: the code ends at its size. */
    CHECK(pixel(tok, N(tok) - 1));
    CHECK(out.n == N(want) / 4);
}

/* --- Pixel shaders 1.0-1.3: arithmetic, modifiers, co-issue, def ---------- */

static void
test_ps_arith(void)
{
    static const uint32_t tok[] = {
        PSV(1),
        81, DT(C, 1, 15), F1, 0x3F000000u, 0, 0xBF800000u, /* def c1, 1, 0.5, 0, -1 */
        COMMENT(2), 0x12345678u, 0x80000001u,              /* a comment, a param-like dword inside */
        66, DT(T, 0, 15),                                   /* tex t0 */
        5, DT(R, 0, 7), ST(T, 0, XYZW, 0), ST(V, 0, XYZW, 0),            /* mul r0.rgb, t0, v0 */
        CO | 2, DT(R, 0, 8), ST(R, 0, WWWW, 0), ST(C, 0, WWWW, 0),       /* +add r0.a, r0.a, c0.a */
        4, DT(R, 1, 15) | SAT | SH(1), ST(T, 0, XYZW, 4), ST(C, 1, XYZW, 2), ST(V, 1, XYZW, 6), /* mad_x2_sat r1, t0_bx2, c1_bias, 1-v1 */
        5, DT(R, 1, 15) | SH(15), ST(R, 1, XYZW, 0), ST(C, 0, XYZW, 1),  /* mul_d2 r1, r1, -c0 */
        8, DT(R, 0, 7), ST(R, 1, XYZW, 5), ST(T, 0, XYZW, 3),            /* dp3 r0.rgb, -r1_bx2, -t0_bias */
        80, DT(R, 0, 15), ST(R, 0, WWWW, 0), ST(R, 1, XYZW, 0), ST(T, 0, XYZW, 0), /* cnd r0, r0.a, r1, t0 */
        18, DT(R, 0, 15), ST(C, 1, XYZW, 0), ST(R, 0, XYZW, 0), ST(V, 0, XYZW, 0), /* lrp r0, c1, r0, v0 */
        81, DT(C, 1, 15), 0, 0, 0, 0,                       /* def c1 again: the last counts */
        0,                                                  /* nop */
        END,
    };
    static const uint32_t want[] = {
        I(SAMPLE, TEMP, 2, 15) | PROJ, G(COORD, 0), STAGE(0), 0,
        I(MOV, TEMP, 8, 15) | NC, G(TEMP, 0), 0, 0,
        I(MUL, TEMP, 0, 7), G(TEMP, 2), G(INPUT, 0), 0,
        I(ADD, TEMP, 0, 8), GS(TEMP, 8, WWWW), GS(CONST, 0, WWWW), 0,
        I(MAD, TEMP, 1, 15) | WSAT | VGPU98_INS_SHIFT(1), GM(TEMP, 2, XYZW, 4), GM(CONST, 1, XYZW, 2), GM(INPUT, 1, XYZW, 6),
        I(MUL, TEMP, 1, 15) | VGPU98_INS_SHIFT(-1), G(TEMP, 1), GM(CONST, 0, XYZW, 1), 0,
        I(DP3, TEMP, 0, 7), GM(TEMP, 1, XYZW, 5), GM(TEMP, 2, XYZW, 3), 0,
        I(CND, TEMP, 0, 15), GS(TEMP, 0, WWWW), G(TEMP, 1), G(TEMP, 2),
        I(LRP, TEMP, 0, 15), G(CONST, 1), G(TEMP, 0), G(INPUT, 0),
    };

    CHECK(pixel(tok, N(tok)));
    CHECK(same("ps arith", &out, want, N(want) / 4));
    CHECK(out.n_in == 8); /* a pair counts two; def and nop none */
    CHECK(out.n_def == 1);
    CHECK((out.def[0][0] == 1) && !out.def[0][1] && !out.def[0][4]);
    CHECK(VGPU98_INS_SHIFTOF(out.ins[4 * 5]) == -1);
    CHECK(VGPU98_INS_SHIFTOF(out.ins[4 * 4]) == 1);
}

/* A co-issued pair whose second reads nothing the first writes needs no copy;
   ps 1.2-1.3's cmp and dp4. */
static void
test_ps_pairs(void)
{
    static const uint32_t tok[] = {
        PSV(3),
        1, DT(R, 0, 7), ST(V, 0, XYZW, 0),                                 /* mov r0.rgb, v0 */
        CO | 1, DT(R, 0, 8), ST(V, 1, WWWW, 0),                            /* +mov r0.a, v1.a */
        88, DT(R, 1, 15), ST(R, 0, XYZW, 1), ST(C, 0, XYZW, 0), ST(C, 1, XYZW, 0), /* cmp r1, -r0, c0, c1 */
        9, DT(R, 0, 15), ST(R, 1, XYZW, 0), ST(C, 2, XYZW, 0),             /* dp4 r0, r1, c2 */
        3, DT(R, 0, 8) | SAT, ST(R, 0, ZZZZ, 0), ST(C, 3, ZZZZ, 0),        /* sub_sat r0.a, r0.b, c3.b */
        END,
    };
    static const uint32_t want[] = {
        I(MOV, TEMP, 0, 7), G(INPUT, 0), 0, 0,
        I(MOV, TEMP, 0, 8), GS(INPUT, 1, WWWW), 0, 0,
        I(CMP, TEMP, 1, 15), GM(TEMP, 0, XYZW, 1), G(CONST, 0), G(CONST, 1),
        I(DP4, TEMP, 0, 15), G(TEMP, 1), G(CONST, 2), 0,
        I(SUB, TEMP, 0, 8) | WSAT, GS(TEMP, 0, ZZZZ), GS(CONST, 3, ZZZZ), 0,
    };

    CHECK(pixel(tok, N(tok)));
    CHECK(same("ps pairs", &out, want, N(want) / 4));
    CHECK(out.info.stages == 0);
    CHECK(out.info.inputs == 3);
}

/* --- Pixel shaders 1.0-1.3: every texture instruction -------------------- */

static void
test_ps_tex(void)
{
    static const uint32_t a[] = {
        PSV(1),
        64, DT(T, 0, 15),                           /* texcoord t0 */
        65, DT(T, 1, 15),                           /* texkill t1 */
        66, DT(T, 1, 15),                           /* tex t1 */
        67, DT(T, 2, 15), ST(T, 1, XYZW, 0),        /* texbem t2, t1 */
        68, DT(T, 3, 15), ST(T, 1, XYZW, 0),        /* texbeml t3, t1 */
        5, DT(R, 0, 15), ST(T, 2, XYZW, 0), ST(T, 3, XYZW, 0),
        END,
    };
    static const uint32_t wa[] = {
        I(MOV, TEMP, 2, 7) | WSAT, G(COORD, 0), 0, 0,
        I(MOV, TEMP, 2, 8), G(FIXED, VGPU98_PX_ONE), 0, 0,
        I(KILL, NONE, 0, 0), G(COORD, 1), 0, 0,
        I(SAMPLE, TEMP, 3, 15) | PROJ, G(COORD, 1), STAGE(1), 0,
        I(BEM, TEMP, 6, 3) | NC | PROJ, G(COORD, 2), G(TEMP, 3), STAGE(2),
        I(SAMPLE, TEMP, 4, 15), G(TEMP, 6), STAGE(2), 0,
        I(BEM, TEMP, 6, 3) | NC | PROJ, G(COORD, 3), G(TEMP, 3), STAGE(3),
        I(SAMPLE, TEMP, 5, 15), G(TEMP, 6), STAGE(3), 0,
        I(LUM, TEMP, 5, 15), G(TEMP, 5), G(TEMP, 3), STAGE(3),
        I(MUL, TEMP, 0, 15), G(TEMP, 4), G(TEMP, 5), 0,
    };
    static const uint32_t b[] = {
        PSV(2),
        66, DT(T, 0, 15),                                           /* tex t0 */
        69, DT(T, 1, 15), ST(T, 0, XYZW, 0),                        /* texreg2ar t1, t0 */
        70, DT(T, 2, 15), ST(T, 0, XYZW, 0),                        /* texreg2gb t2, t0 */
        82, DT(T, 3, 15), ST(T, 0, XYZW, 0),                        /* texreg2rgb t3, t0 */
        1, DT(R, 0, 15), ST(T, 3, XYZW, 0),
        END,
    };
    static const uint32_t wb[] = {
        I(SAMPLE, TEMP, 2, 15) | PROJ, G(COORD, 0), STAGE(0), 0,
        I(SAMPLE, TEMP, 3, 15), GS(TEMP, 2, VGPU98_SWZ(3, 0, 0, 0)), STAGE(1), 0,
        I(SAMPLE, TEMP, 4, 15), GS(TEMP, 2, VGPU98_SWZ(1, 2, 2, 2)), STAGE(2), 0,
        I(SAMPLE, TEMP, 5, 15), G(TEMP, 2), STAGE(3), 0,
        I(MOV, TEMP, 0, 15), G(TEMP, 5), 0, 0,
    };
    static const uint32_t c[] = {
        PSV(3),
        66, DT(T, 0, 15),                                           /* tex t0 */
        71, DT(T, 1, 15), ST(T, 0, XYZW, 4),                        /* texm3x2pad t1, t0_bx2 */
        72, DT(T, 2, 15), ST(T, 0, XYZW, 4),                        /* texm3x2tex t2, t0_bx2 */
        1, DT(R, 0, 15), ST(T, 2, XYZW, 0),
        END,
    };
    static const uint32_t wc[] = {
        I(SAMPLE, TEMP, 2, 15) | PROJ, G(COORD, 0), STAGE(0), 0,
        I(DP3, TEMP, 3, 1) | NC, G(COORD, 1), GM(TEMP, 2, XYZW, 4), 0,
        I(DP3, TEMP, 6, 2) | NC, G(COORD, 2), GM(TEMP, 2, XYZW, 4), 0,
        I(MOV, TEMP, 6, 1) | NC, GS(TEMP, 3, XXXX), 0, 0,
        I(SAMPLE, TEMP, 4, 15), G(TEMP, 6), STAGE(2), 0,
        I(MOV, TEMP, 0, 15), G(TEMP, 4), 0, 0,
    };
    static const uint32_t d[] = {
        PSV(3),
        66, DT(T, 0, 15),
        71, DT(T, 1, 15), ST(T, 0, XYZW, 0),                        /* texm3x2pad t1, t0 */
        84, DT(T, 2, 15), ST(T, 0, XYZW, 0),                        /* texm3x2depth t2, t0 */
        1, DT(R, 0, 15), ST(T, 0, XYZW, 0),
        END,
    };
    static const uint32_t wd[] = {
        I(SAMPLE, TEMP, 2, 15) | PROJ, G(COORD, 0), STAGE(0), 0,
        I(DP3, TEMP, 3, 1) | NC, G(COORD, 1), G(TEMP, 2), 0,
        I(DP3, TEMP, 6, 2) | NC, G(COORD, 2), G(TEMP, 2), 0,
        I(MOV, TEMP, 6, 1) | NC, GS(TEMP, 3, XXXX), 0, 0,
        I(DEPTH, NONE, 0, 0), G(TEMP, 6), 0, 0,
        I(MOV, TEMP, 0, 15), G(TEMP, 2), 0, 0,
    };
    static const uint32_t e[] = {
        PSV(1),
        66, DT(T, 0, 15),
        73, DT(T, 1, 15), ST(T, 0, XYZW, 0),
        73, DT(T, 2, 15), ST(T, 0, XYZW, 0),
        74, DT(T, 3, 15), ST(T, 0, XYZW, 0),                        /* texm3x3tex t3, t0 */
        1, DT(R, 0, 15), ST(T, 3, XYZW, 0),
        END,
    };
    static const uint32_t we[] = {
        I(SAMPLE, TEMP, 2, 15) | PROJ, G(COORD, 0), STAGE(0), 0,
        I(DP3, TEMP, 3, 1) | NC, G(COORD, 1), G(TEMP, 2), 0,
        I(DP3, TEMP, 4, 1) | NC, G(COORD, 2), G(TEMP, 2), 0,
        I(DP3, TEMP, 6, 4) | NC, G(COORD, 3), G(TEMP, 2), 0,
        I(MOV, TEMP, 6, 1) | NC, GS(TEMP, 3, XXXX), 0, 0,
        I(MOV, TEMP, 6, 2) | NC, GS(TEMP, 4, XXXX), 0, 0,
        I(SAMPLE, TEMP, 5, 15), G(TEMP, 6), STAGE(3), 0,
        I(MOV, TEMP, 0, 15), G(TEMP, 5), 0, 0,
    };
    static const uint32_t f[] = {
        PSV(3),
        66, DT(T, 0, 15),
        73, DT(T, 1, 15), ST(T, 0, XYZW, 0),
        73, DT(T, 2, 15), ST(T, 0, XYZW, 0),
        76, DT(T, 3, 15), ST(T, 0, XYZW, 0), ST(C, 5, XYZW, 0),     /* texm3x3spec t3, t0, c5 */
        1, DT(R, 0, 15), ST(T, 3, XYZW, 0),
        END,
    };
    static const uint32_t wf[] = {
        I(SAMPLE, TEMP, 2, 15) | PROJ, G(COORD, 0), STAGE(0), 0,
        I(DP3, TEMP, 3, 1) | NC, G(COORD, 1), G(TEMP, 2), 0,
        I(DP3, TEMP, 4, 1) | NC, G(COORD, 2), G(TEMP, 2), 0,
        I(DP3, TEMP, 6, 4) | NC, G(COORD, 3), G(TEMP, 2), 0,
        I(MOV, TEMP, 6, 1) | NC, GS(TEMP, 3, XXXX), 0, 0,
        I(MOV, TEMP, 6, 2) | NC, GS(TEMP, 4, XXXX), 0, 0,
        I(RFL, TEMP, 7, 15) | NC, G(TEMP, 6), G(CONST, 5), 0,
        I(SAMPLE, TEMP, 5, 15), G(TEMP, 7), STAGE(3), 0,
        I(MOV, TEMP, 0, 15), G(TEMP, 5), 0, 0,
    };
    static const uint32_t g[] = {
        PSV(2),
        66, DT(T, 0, 15),
        73, DT(T, 1, 15), ST(T, 0, XYZW, 0),
        73, DT(T, 2, 15), ST(T, 0, XYZW, 0),
        86, DT(T, 3, 15), ST(T, 0, XYZW, 0),                        /* texm3x3 t3, t0 */
        1, DT(R, 0, 15), ST(T, 3, XYZW, 0),
        END,
    };
    static const uint32_t wg[] = {
        I(SAMPLE, TEMP, 2, 15) | PROJ, G(COORD, 0), STAGE(0), 0,
        I(DP3, TEMP, 3, 1) | NC, G(COORD, 1), G(TEMP, 2), 0,
        I(DP3, TEMP, 4, 1) | NC, G(COORD, 2), G(TEMP, 2), 0,
        I(DP3, TEMP, 6, 4) | NC, G(COORD, 3), G(TEMP, 2), 0,
        I(MOV, TEMP, 6, 1) | NC, GS(TEMP, 3, XXXX), 0, 0,
        I(MOV, TEMP, 6, 2) | NC, GS(TEMP, 4, XXXX), 0, 0,
        I(MOV, TEMP, 5, 7), G(TEMP, 6), 0, 0,
        I(MOV, TEMP, 5, 8), G(FIXED, VGPU98_PX_ONE), 0, 0,
        I(MOV, TEMP, 0, 15), G(TEMP, 5), 0, 0,
    };
    static const uint32_t h[] = {
        PSV(2),
        66, DT(T, 0, 15),
        85, DT(T, 1, 15), ST(T, 0, XYZW, 0),                        /* texdp3 t1, t0 */
        83, DT(T, 2, 15), ST(T, 0, XYZW, 0),                        /* texdp3tex t2, t0 */
        5, DT(R, 0, 15), ST(T, 1, XYZW, 0), ST(T, 2, XYZW, 0),
        END,
    };
    static const uint32_t wh[] = {
        I(SAMPLE, TEMP, 2, 15) | PROJ, G(COORD, 0), STAGE(0), 0,
        I(DP3, TEMP, 3, 15), G(COORD, 1), G(TEMP, 2), 0,
        I(DP3, TEMP, 6, 1) | NC, G(COORD, 2), G(TEMP, 2), 0,
        I(MOV, TEMP, 6, 6) | NC, G(FIXED, VGPU98_PX_ZERO), 0, 0,
        I(SAMPLE, TEMP, 4, 15), G(TEMP, 6), STAGE(2), 0,
        I(MUL, TEMP, 0, 15), G(TEMP, 3), G(TEMP, 4), 0,
    };

    CHECK(pixel(a, N(a)));
    CHECK(same("texcoord/texkill/texbem/texbeml", &out, wa, N(wa) / 4));
    CHECK(out.info.stages == 0xE);
    CHECK(out.info.flags & VGPU98_PROGF_KILL);
    CHECK(pixel(b, N(b)));
    CHECK(same("texreg2*", &out, wb, N(wb) / 4));
    CHECK(pixel(c, N(c)));
    CHECK(same("texm3x2tex", &out, wc, N(wc) / 4));
    CHECK(out.info.stages == 0x5); /* the pad's stage carries a row only */
    CHECK(pixel(d, N(d)));
    CHECK(same("texm3x2depth", &out, wd, N(wd) / 4));
    CHECK(out.info.flags & VGPU98_PROGF_DEPTH);
    CHECK(pixel(e, N(e)));
    CHECK(same("texm3x3tex", &out, we, N(we) / 4));
    CHECK(pixel(f, N(f)));
    CHECK(same("texm3x3spec", &out, wf, N(wf) / 4));
    CHECK(out.info.stages == 0x9);
    CHECK(pixel(g, N(g)));
    CHECK(same("texm3x3", &out, wg, N(wg) / 4));
    CHECK(out.info.stages == 0x1); /* texm3x3 samples nothing */
    CHECK(pixel(h, N(h)));
    CHECK(same("texdp3/texdp3tex", &out, wh, N(wh) / 4));
}

/* --- Pixel shaders 1.4 ------------------------------------------------------ */

static void
test_ps14(void)
{
    static const uint32_t tok[] = {
        PSV(4),
        81, DT(C, 7, 15), F1, F1, F1, F1,                                /* def c7, 1, 1, 1, 1 */
        66, DT(R, 0, 15), ST(T, 0, XYZW, 0),                             /* texld r0, t0 */
        64, DT(R, 1, 7), ST(T, 1, VGPU98_SWZ(0, 1, 3, 3), 10),           /* texcrd r1.rgb, t1.xyw_dw */
        89, DT(R, 2, 3), ST(R, 1, XYZW, 0), ST(R, 0, XYZW, 0),           /* bem r2.rg, r1, r0 */
        PHASE,
        66, DT(R, 2, 15), ST(R, 2, XYZW, 9),                             /* texld r2, r2_dz */
        66, DT(R, 0, 15), ST(R, 1, XYZW, 0),                             /* texld r0, r1 */
        66, DT(R, 3, 15), ST(R, 0, XYZW, 0),                             /* texld r3, r0: r0 as the phase began */
        65, DT(T, 4, 15),                                                /* texkill t4 */
        5, DT(R, 0, 15), ST(R, 2, XYZW, 0), ST(R, 3, XYZW, 7),           /* mul r0, r2, r3_x2 */
        2, DT(R, 0, 8) | SH(2), ST(R, 0, XXXX, 0), ST(C, 7, YYYY, 0),    /* add_x4 r0.a, r0.r, c7.g */
        5, DT(R, 4, 15) | SH(3), ST(R, 0, XYZW, 8), ST(C, 7, XYZW, 0),   /* mul_x8 r4, -r0_x2, c7 */
        5, DT(R, 5, 15) | SH(13), ST(R, 4, XYZW, 0), ST(C, 7, XYZW, 0),  /* mul_d8 r5, r4, c7 */
        END,
    };
    static const uint32_t want[] = {
        I(SAMPLE, TEMP, 0, 15), G(COORD, 0), STAGE(0), 0,
        I(MOV, TEMP, 1, 7), GM(COORD, 1, VGPU98_SWZ(0, 1, 3, 3), VGPU98_PM_DW), 0, 0,
        I(BEM, TEMP, 2, 3) | NC, G(TEMP, 1), G(TEMP, 0), STAGE(2),
        I(MOV, TEMP, 8, 15) | NC, G(TEMP, 0), 0, 0,
        I(SAMPLE, TEMP, 2, 15), GM(TEMP, 2, XYZW, VGPU98_PM_DZ), STAGE(2), 0,
        I(SAMPLE, TEMP, 0, 15), G(TEMP, 1), STAGE(0), 0,
        I(SAMPLE, TEMP, 3, 15), G(TEMP, 8), STAGE(3), 0,
        I(KILL, NONE, 0, 0), G(COORD, 4), 0, 0,
        I(MUL, TEMP, 0, 15), G(TEMP, 2), GM(TEMP, 3, XYZW, VGPU98_PM_X2), 0,
        I(ADD, TEMP, 0, 8) | VGPU98_INS_SHIFT(2), GS(TEMP, 0, XXXX), GS(CONST, 7, YYYY), 0,
        I(MUL, TEMP, 4, 15) | VGPU98_INS_SHIFT(3), GM(TEMP, 0, XYZW, VGPU98_PM_X2NEG), G(CONST, 7), 0,
        I(MUL, TEMP, 5, 15) | VGPU98_INS_SHIFT(-3), G(TEMP, 4), G(CONST, 7), 0,
    };
    static const uint32_t depth[] = {
        PSV(4),
        64, DT(R, 5, 3), ST(T, 0, XYZW, 0),                              /* texcrd r5.rg, t0 */
        PHASE,
        87, DT(R, 5, 15),                                                /* texdepth r5 */
        65, DT(R, 5, 15),                                                /* texkill r5 */
        1, DT(R, 0, 15), ST(V, 0, XYZW, 0),
        END,
    };
    static const uint32_t wdepth[] = {
        I(MOV, TEMP, 5, 3), G(COORD, 0), 0, 0,
        I(DEPTH, NONE, 0, 0), G(TEMP, 5), 0, 0,
        I(KILL, NONE, 0, 0), G(TEMP, 5), 0, 0,
        I(MOV, TEMP, 0, 15), G(INPUT, 0), 0, 0,
    };
    uint32_t r8 = 0x41000000u; /* 8.0f */

    CHECK(pixel(tok, N(tok)));
    CHECK(same("ps 1.4", &out, want, N(want) / 4));
    CHECK(out.n_in == 11); /* def and the phase marker not counted */
    CHECK(out.info.stages == 0xD);
    CHECK(out.n_def == 1);
    CHECK((out.def[0][0] == 7) && (out.def[0][1] == F1));
    CHECK(pixel(depth, N(depth)));
    CHECK(same("ps 1.4 depth", &out, wdepth, N(wdepth) / 4));
    CHECK(out.info.flags == (VGPU98_PROGF_DEPTH | VGPU98_PROGF_KILL));
    /* The range is PROGRAM's: 8 checks, 9 does not. */
    memset(&out, 0, sizeof(out));
    CHECK(xl_pixel(tok, N(tok), r8, &out));
    memset(&out, 0, sizeof(out));
    CHECK(!xl_pixel(tok, N(tok), 0x41100000u, &out) && (out.why == XL_E_CHECK));
}

/* ps 1.4's phase rule through texkill, its cnd, cmp and lrp, and a co-issued
   pair whose second reads what the first writes. */
static void
test_ps14_more(void)
{
    static const uint32_t tok[] = {
        PSV(4),
        64, DT(R, 1, 7), ST(T, 0, XYZW, 0),                                         /* texcrd r1.rgb, t0 */
        PHASE,
        66, DT(R, 1, 15), ST(T, 1, XYZW, 0),                                        /* texld r1, t1 */
        65, DT(R, 1, 15),                                                           /* texkill r1: as the phase began */
        80, DT(R, 0, 15), ST(R, 1, XYZW, 0), ST(C, 0, XYZW, 0), ST(C, 1, XYZW, 0), /* cnd r0, r1, c0, c1 */
        88, DT(R, 2, 15), ST(R, 1, XYZW, 0), ST(C, 0, XYZW, 0), ST(C, 1, XYZW, 0), /* cmp r2, r1, c0, c1 */
        18, DT(R, 3, 15), ST(C, 2, XYZW, 0), ST(R, 0, XYZW, 0), ST(R, 2, XYZW, 0), /* lrp r3, c2, r0, r2 */
        2, DT(R, 0, 7), ST(R, 3, XYZW, 0), ST(C, 3, XYZW, 0),                       /* add r0.rgb, r3, c3 */
        CO | 5, DT(R, 0, 8), ST(R, 0, XXXX, 0), ST(C, 0, WWWW, 0),                  /* +mul r0.a, r0.r, c0.a */
        END,
    };
    static const uint32_t want[] = {
        I(MOV, TEMP, 1, 7), G(COORD, 0), 0, 0,
        I(MOV, TEMP, 8, 15) | NC, G(TEMP, 1), 0, 0,
        I(SAMPLE, TEMP, 1, 15), G(COORD, 1), STAGE(1), 0,
        I(KILL, NONE, 0, 0), G(TEMP, 8), 0, 0,
        I(CND, TEMP, 0, 15), G(TEMP, 1), G(CONST, 0), G(CONST, 1),
        I(CMP, TEMP, 2, 15), G(TEMP, 1), G(CONST, 0), G(CONST, 1),
        I(LRP, TEMP, 3, 15), G(CONST, 2), G(TEMP, 0), G(TEMP, 2),
        I(MOV, TEMP, 8, 15) | NC, G(TEMP, 0), 0, 0,
        I(ADD, TEMP, 0, 7), G(TEMP, 3), G(CONST, 3), 0,
        I(MUL, TEMP, 0, 8), GS(TEMP, 8, XXXX), GS(CONST, 0, WWWW), 0,
    };

    static const uint32_t depth[] = {
        PSV(4),
        66, DT(R, 5, 15), ST(T, 0, XYZW, 0),                      /* texld r5, t0 */
        64, DT(R, 2, 7), ST(T, 1, VGPU98_SWZ(0, 1, 3, 3), 0),     /* texcrd r2.rgb, t1.xyw: no divide */
        PHASE,
        66, DT(R, 5, 15), ST(T, 1, XYZW, 0),                      /* texld r5, t1 */
        87, DT(R, 5, 15),                                         /* texdepth r5: r5 as the phase began */
        1, DT(R, 0, 15), ST(R, 2, XYZW, 0),
        END,
    };
    static const uint32_t wdepth[] = {
        I(SAMPLE, TEMP, 5, 15), G(COORD, 0), STAGE(5), 0,
        I(MOV, TEMP, 2, 7), GS(COORD, 1, VGPU98_SWZ(0, 1, 3, 3)), 0, 0,
        I(MOV, TEMP, 8, 15) | NC, G(TEMP, 5), 0, 0,
        I(SAMPLE, TEMP, 5, 15), G(COORD, 1), STAGE(5), 0,
        I(DEPTH, NONE, 0, 0), G(TEMP, 8), 0, 0,
        I(MOV, TEMP, 0, 15), G(TEMP, 2), 0, 0,
    };

    CHECK(pixel(tok, N(tok)));
    CHECK(same("ps 1.4 phases and pairs", &out, want, N(want) / 4));
    CHECK(out.info.stages == 0x2);
    CHECK(out.info.flags == VGPU98_PROGF_KILL);
    CHECK(pixel(depth, N(depth)));
    CHECK(same("ps 1.4 texdepth after texld r5", &out, wdepth, N(wdepth) / 4));
    CHECK(out.info.stages == 0x20);
    CHECK(out.info.flags == VGPU98_PROGF_DEPTH);
}

/* --- Vertex shaders ------------------------------------------------------------ */

/* vs 1.0: add, mul, dp4 and a def (a function's constants come in its
   declaration, but a def is kept the same way). */
static void
test_vs10(void)
{
    static const uint32_t tok[] = {
        VSV(0),
        81, DT(C, 5, 15), F1, 0, 0, F1,                       /* def c5, 1, 0, 0, 1 */
        2, DT(R, 0, 15), ST(V, 0, XYZW, 0), ST(C, 5, XYZW, 0), /* add r0, v0, c5 */
        5, DT(R, 1, 15), ST(R, 0, XYZW, 0), ST(V, 1, XYZW, 1), /* mul r1, r0, -v1 */
        9, DT(RO, 0, 15), ST(R, 1, XYZW, 0), ST(C, 0, XYZW, 0), /* dp4 oPos, r1, c0 */
        END,
    };
    static const uint32_t want[] = {
        I(ADD, TEMP, 0, 15), G(INPUT, 0), G(CONST, 5), 0,
        I(MUL, TEMP, 1, 15), G(TEMP, 0), GM(INPUT, 1, XYZW, 1), 0,
        I(DP4, OUTPUT, 0, 15), G(TEMP, 1), G(CONST, 0), 0,
    };

    CHECK(vertex(tok, N(tok)));
    CHECK(same("vs 1.0", &out, want, N(want) / 4));
    CHECK(out.version == VSV(0));
    CHECK((out.n_def == 1) && (out.def[0][0] == 5) && (out.def[0][1] == F1) && (out.def[0][4] == F1));
}

static void
test_vs(void)
{
    static const uint32_t tok[] = {
        VSV(1),
        20, DT(RO, 0, 15), ST(V, 0, XYZW, 0), ST(C, 0, XYZW, 0),         /* m4x4 oPos, v0, c0 */
        1, DT(AO, 0, 15), ST(V, 5, XYZW, 0),                             /* mov oD0, v5 */
        8, DT(R, 0, 1), ST(V, 3, XYZW, 0), ST(C, 4, XYZW, 0),            /* dp3 r0.x, v3, c4 */
        1, DT(A, 0, 1), ST(R, 0, XXXX, 0),                               /* mov a0.x, r0.x */
        1, DT(TO, 0, 15), ST(C, 10, XYZW, 0) | REL,                      /* mov oT0, c[a0.x + 10] */
        6, DT(R, 1, 8), ST(R, 0, YYYY, 0),                               /* rcp r1.w, r0.y */
        23, DT(R, 2, 7), ST(R, 2, XYZW, 0), ST(C, 5, XYZW, 0),           /* m3x3 r2.xyz, r2, c5: through scratch */
        3, DT(R, 3, 15), ST(R, 1, XYZW, 0), ST(C, 1, XYZW, 1),           /* sub r3, r1, -c1 */
        78, DT(R, 4, 15), ST(R, 0, ZZZZ, 0),                             /* expp r4, r0.z */
        79, DT(R, 5, 15), ST(R, 0, WWWW, 1),                             /* logp r5, -r0.w */
        16, DT(R, 6, 15), ST(R, 0, XYZW, 0),                             /* lit r6, r0 */
        17, DT(R, 7, 15), ST(R, 0, XYZW, 0), ST(R, 1, XYZW, 0),          /* dst r7, r0, r1 */
        19, DT(R, 8, 3), ST(R, 0, XYZW, 0),                              /* frc r8.xy, r0 */
        11, DT(R, 9, 15), ST(R, 0, XYZW, 0), ST(C, 0, XYZW, 0),          /* max r9, r0, c0 */
        12, DT(R, 10, 15), ST(R, 0, XYZW, 0), ST(C, 0, XYZW, 0),         /* slt r10, r0, c0 */
        13, DT(R, 11, 15), ST(R, 0, XYZW, 0), ST(C, 0, XYZW, 0),         /* sge r11, r0, c0 */
        4, DT(RO, 1, 1), ST(R, 0, XXXX, 0), ST(C, 2, XXXX, 0), ST(C, 2, YYYY, 0), /* mad oFog.x, r0.x, c2.x, c2.y */
        1, DT(RO, 2, 1), ST(C, 3, XXXX, 0),                              /* mov oPts.x, c3.x */
        21, DT(TO, 1, 7), ST(V, 0, XYZW, 0), ST(C, 20, XYZW, 0) | REL,   /* m4x3 oT1.xyz, v0, c[a0.x + 20] */
        22, DT(R, 0, 15), ST(V, 0, XYZW, 0), ST(R, 0, XYZW, 0),          /* m3x4 r0, v0, r0: a row is the destination */
        24, DT(AO, 1, 3), ST(V, 0, XYZW, 0), ST(C, 30, XYZW, 0),         /* m3x2 oD1.xy, v0, c30 */
        14, DT(R, 1, 15), ST(R, 0, XXXX, 0),                             /* exp r1, r0.x */
        15, DT(R, 1, 15), ST(R, 0, VGPU98_SWZ(0, 1, 2, 1), 0),           /* log r1, r0 (w selector y) */
        7, DT(R, 1, 15), ST(R, 0, WWWW, 0),                              /* rsq r1, r0.w */
        10, DT(R, 1, 15), ST(R, 0, XYZW, 0), ST(V, 15, XYZW, 0),         /* min r1, r0, v15 */
        0,
        END,
    };
    static const uint32_t want[] = {
        I(DP4, OUTPUT, 0, 1), G(INPUT, 0), G(CONST, 0), 0,
        I(DP4, OUTPUT, 0, 2), G(INPUT, 0), G(CONST, 1), 0,
        I(DP4, OUTPUT, 0, 4), G(INPUT, 0), G(CONST, 2), 0,
        I(DP4, OUTPUT, 0, 8), G(INPUT, 0), G(CONST, 3), 0,
        I(MOV, OUTPUT, 1, 15), G(INPUT, 5), 0, 0,
        I(DP3, TEMP, 0, 1), G(INPUT, 3), G(CONST, 4), 0,
        I(MOVA, ADDR, 0, 1), GS(TEMP, 0, XXXX), 0, 0,
        I(MOV, OUTPUT, 5, 15), G(CONST, 10) | VGPU98_SRC_RELATIVE, 0, 0,
        I(RCP, TEMP, 1, 8), GS(TEMP, 0, YYYY), 0, 0,
        I(DP3, TEMP, 12, 1), G(TEMP, 2), G(CONST, 5), 0,
        I(DP3, TEMP, 12, 2), G(TEMP, 2), G(CONST, 6), 0,
        I(DP3, TEMP, 12, 4), G(TEMP, 2), G(CONST, 7), 0,
        I(MOV, TEMP, 2, 7), G(TEMP, 12), 0, 0,
        I(SUB, TEMP, 3, 15), G(TEMP, 1), GM(CONST, 1, XYZW, 1), 0,
        I(EXPP, TEMP, 4, 15), GS(TEMP, 0, ZZZZ), 0, 0,
        I(LOGP, TEMP, 5, 15), GM(TEMP, 0, WWWW, 1), 0, 0,
        I(LIT, TEMP, 6, 15), G(TEMP, 0), 0, 0,
        I(DST, TEMP, 7, 15), G(TEMP, 0), G(TEMP, 1), 0,
        I(FRC, TEMP, 8, 3), G(TEMP, 0), 0, 0,
        I(MAX, TEMP, 9, 15), G(TEMP, 0), G(CONST, 0), 0,
        I(SLT, TEMP, 10, 15), G(TEMP, 0), G(CONST, 0), 0,
        I(SGE, TEMP, 11, 15), G(TEMP, 0), G(CONST, 0), 0,
        I(MAD, OUTPUT, 3, 1), GS(TEMP, 0, XXXX), GS(CONST, 2, XXXX), GS(CONST, 2, YYYY),
        I(MOV, OUTPUT, 4, 1), GS(CONST, 3, XXXX), 0, 0,
        I(DP4, OUTPUT, 6, 1), G(INPUT, 0), G(CONST, 20) | VGPU98_SRC_RELATIVE, 0,
        I(DP4, OUTPUT, 6, 2), G(INPUT, 0), G(CONST, 21) | VGPU98_SRC_RELATIVE, 0,
        I(DP4, OUTPUT, 6, 4), G(INPUT, 0), G(CONST, 22) | VGPU98_SRC_RELATIVE, 0,
        I(DP3, TEMP, 12, 1), G(INPUT, 0), G(TEMP, 0), 0,
        I(DP3, TEMP, 12, 2), G(INPUT, 0), G(TEMP, 1), 0,
        I(DP3, TEMP, 12, 4), G(INPUT, 0), G(TEMP, 2), 0,
        I(DP3, TEMP, 12, 8), G(INPUT, 0), G(TEMP, 3), 0,
        I(MOV, TEMP, 0, 15), G(TEMP, 12), 0, 0,
        I(DP3, OUTPUT, 2, 1), G(INPUT, 0), G(CONST, 30), 0,
        I(DP3, OUTPUT, 2, 2), G(INPUT, 0), G(CONST, 31), 0,
        I(EXP, TEMP, 1, 15), GS(TEMP, 0, XXXX), 0, 0,
        I(LOG, TEMP, 1, 15), GS(TEMP, 0, YYYY), 0, 0,
        I(RSQ, TEMP, 1, 15), GS(TEMP, 0, WWWW), 0, 0,
        I(MIN, TEMP, 1, 15), G(TEMP, 0), G(INPUT, 15), 0,
    };

    CHECK(vertex(tok, N(tok)));
    CHECK(same("vs 1.1", &out, want, N(want) / 4));
    CHECK(out.n_in == 25); /* nop not counted */
    CHECK(out.kind == VGPU98_PROG_VERTEX);
    CHECK(out.info.outputs == ((1u << VGPU98_PO_POS) | (1u << VGPU98_PO_D0) | (1u << VGPU98_PO_D1) | (1u << VGPU98_PO_FOG) |
                               (1u << VGPU98_PO_PTS) | (1u << VGPU98_PO_T0) | (1u << (VGPU98_PO_T0 + 1))));
    CHECK(out.info.inputs == ((1u << 0) | (1u << 3) | (1u << 5) | (1u << 15)));
    CHECK(out.info.flags & VGPU98_PROGF_RELATIVE);
}

/* A shader of nothing but def still makes a program (one that does nothing). */
static void
test_empty(void)
{
    static const uint32_t tok[] = { PSV(1), 81, DT(C, 0, 15), F1, 0, 0, F1, END };

    CHECK(pixel(tok, N(tok)));
    CHECK(out.n == 1);
    CHECK(out.n_def == 1);
}

/* Comments stripped in place, a def value that looks like one kept. */
static void
test_strip(void)
{
    uint32_t tok[] = {
        PSV(1),
        COMMENT(2), 0x11111111u, 0x22222222u,
        81, DT(C, 0, 15), 0x0000FFFEu, 0, 0, F1, /* def c0 with a comment-like raw value */
        66, DT(T, 0, 15),
        COMMENT(1), 0x33333333u,
        1, DT(R, 0, 15), ST(T, 0, XYZW, 0),
        END,
    };
    static const uint32_t want[] = {
        PSV(1), 81, DT(C, 0, 15), 0x0000FFFEu, 0, 0, F1, 66, DT(T, 0, 15), 1, DT(R, 0, 15), ST(T, 0, XYZW, 0),
    };
    const uint32_t k = xl_strip(tok, N(tok));

    CHECK(k == N(want));
    CHECK(!memcmp(tok, want, sizeof(want)));
    CHECK(pixel(tok, k));
    CHECK((out.n == 2) && (out.n_def == 1) && (out.def[0][1] == 0x0000FFFEu));
    CHECK(xl_strip(tok, 0) == 0);
}

/* --- What the translator refuses ------------------------------------------------- */

static void
refused(const char *name, int is_pixel, const uint32_t *tok, uint32_t n, uint32_t why, uint32_t at)
{
    const int ok = is_pixel ? pixel(tok, n) : vertex(tok, n);

    if (ok || (out.why != why) || (out.at != at)) {
        fprintf(stderr, "%s: %s, why %u at %u (%u at %u expected)\n", name, ok ? "taken" : "refused", out.why, out.at, why, at);
        check_failures++;
    }
}

static void
test_refusals(void)
{
    static const uint32_t ps20[]   = { 0xFFFF0200u, END };
    static const uint32_t vs20[]   = { 0xFFFE0200u, END };
    static const uint32_t vsps[]   = { PSV(1), END };
    static const uint32_t rcp[]    = { PSV(1), 6, DT(R, 0, 15), ST(R, 0, WWWW, 0), END };
    static const uint32_t r2[]     = { PSV(1), 1, DT(R, 0, 15), ST(R, 2, XYZW, 0), END };
    static const uint32_t t4[]     = { PSV(3), 66, DT(T, 4, 15), END };
    static const uint32_t c8[]     = { PSV(1), 1, DT(R, 0, 15), ST(C, 8, XYZW, 0), END };
    static const uint32_t dep13[]  = { PSV(3), 87, DT(R, 5, 15), END };
    static const uint32_t bem13[]  = { PSV(3), 89, DT(R, 0, 3), ST(R, 0, XYZW, 0), ST(R, 1, XYZW, 0), END };
    static const uint32_t bem14t[] = { PSV(4), 67, DT(R, 0, 15), ST(R, 1, XYZW, 0), END };
    static const uint32_t ar14[]   = { PSV(4), 1, DT(R, 0, 15), ST(T, 0, XYZW, 0), END };
    static const uint32_t three[]  = { PSV(1), 1, DT(R, 0, 15), ST(V, 0, XYZW, 0), ST(V, 1, XYZW, 0), END };
    static const uint32_t lone[]   = { PSV(1), ST(V, 0, XYZW, 0), END };
    static const uint32_t shift4[] = { PSV(4), 1, DT(R, 0, 15) | SH(4), ST(V, 0, XYZW, 0), END };
    static const uint32_t pp[]     = { PSV(1), 1, DT(R, 0, 15) | (2u << 20), ST(V, 0, XYZW, 0), END };
    static const uint32_t dzadd[]  = { PSV(4), 2, DT(R, 0, 15), ST(R, 0, XYZW, 9), ST(R, 1, XYZW, 0), END };
    static const uint32_t smod11[] = { PSV(1), 1, DT(R, 0, 15), ST(V, 0, XYZW, 11), END };
    static const uint32_t pad0[]   = { PSV(1), 72, DT(T, 0, 15), ST(T, 0, XYZW, 0), END };
    static const uint32_t def9[]   = { PSV(1), 81, DT(C, 9, 15), 0, 0, 0, 0, END };
    static const uint32_t shortdef[] = { PSV(1), 81, DT(C, 1, 15), 0, 0 };
    static const uint32_t spec_r[] = { PSV(1), 66, DT(T, 0, 15), 73, DT(T, 1, 15), ST(T, 0, XYZW, 0), 73, DT(T, 2, 15),
                                       ST(T, 0, XYZW, 0), 76, DT(T, 3, 15), ST(T, 0, XYZW, 0), ST(R, 0, XYZW, 0), END };
    static const uint32_t pair_tex[] = { PSV(1), 1, DT(R, 0, 7), ST(V, 0, XYZW, 0), CO | 66, DT(T, 0, 15), END };
    static const uint32_t vsat[]   = { VSV(1), 1, DT(R, 0, 15) | SAT, ST(V, 0, XYZW, 0), END };
    static const uint32_t vrelr[]  = { VSV(1), 1, DT(R, 0, 15), ST(R, 1, XYZW, 0) | REL, END };
    static const uint32_t vr12[]   = { VSV(1), 1, DT(R, 12, 15), ST(V, 0, XYZW, 0), END };
    static const uint32_t vv16[]   = { VSV(1), 1, DT(R, 0, 15), ST(V, 16, XYZW, 0), END };
    static const uint32_t vc256[]  = { VSV(1), 1, DT(R, 0, 15), ST(C, 256, XYZW, 0), END };
    static const uint32_t voread[] = { VSV(1), 1, DT(R, 0, 15), ST(RO, 0, XYZW, 0), END };
    static const uint32_t vtex[]   = { VSV(1), 66, DT(R, 0, 15), END };
    static const uint32_t va0y[]   = { VSV(1), 1, DT(A, 1, 1), ST(R, 0, XXXX, 0), END };
    static const uint32_t vlrp[]   = { VSV(1), 18, DT(R, 0, 15), ST(R, 0, XYZW, 0), ST(R, 0, XYZW, 0), ST(R, 0, XYZW, 0), END };
    static const uint32_t vrows[]  = { VSV(1), 20, DT(R, 0, 15), ST(V, 0, XYZW, 0), ST(C, 253, XYZW, 0), END };
    static const uint32_t vdz[]    = { VSV(1), 1, DT(R, 0, 15), ST(V, 0, XYZW, 9), END };
    static const uint32_t vo3[]    = { VSV(1), 1, DT(RO, 3, 1), ST(V, 0, XYZW, 0), END };
    static uint32_t       vlong[1 + 70 * 4 + 1];
    static uint32_t       defs[1 + 33 * 6 + 1];
    uint32_t              i;

    refused("ps 2.0", 1, ps20, N(ps20), XL_E_VERSION, 0);
    refused("vs 2.0", 0, vs20, N(vs20), XL_E_VERSION, 0);
    refused("a pixel shader as a vertex one", 0, vsps, N(vsps), XL_E_VERSION, 0);
    refused("no tokens", 1, ps20, 0, XL_E_VERSION, 0);
    refused("rcp in a pixel shader", 1, rcp, N(rcp), XL_E_OPCODE, 1);
    refused("r2 in 1.1", 1, r2, N(r2), XL_E_REGISTER, 1);
    refused("t4 in 1.3", 1, t4, N(t4), XL_E_REGISTER, 1);
    refused("c8", 1, c8, N(c8), XL_E_REGISTER, 1);
    refused("texdepth in 1.3", 1, dep13, N(dep13), XL_E_OPCODE, 1);
    refused("bem in 1.3", 1, bem13, N(bem13), XL_E_OPCODE, 1);
    refused("texbem in 1.4", 1, bem14t, N(bem14t), XL_E_OPCODE, 1);
    refused("1.4 arithmetic reading t0", 1, ar14, N(ar14), XL_E_REGISTER, 1);
    refused("mov with three parameters", 1, three, N(three), XL_E_TOKENS, 1);
    refused("a parameter alone", 1, lone, N(lone), XL_E_TOKENS, 1);
    refused("shift 4", 1, shift4, N(shift4), XL_E_MODIFIER, 1);
    refused("a result modifier besides saturate", 1, pp, N(pp), XL_E_MODIFIER, 1);
    refused("_dz on add", 1, dzadd, N(dzadd), XL_E_MODIFIER, 1);
    refused("source modifier 11", 1, smod11, N(smod11), XL_E_MODIFIER, 1);
    refused("texm3x2tex t0", 1, pad0, N(pad0), XL_E_REGISTER, 1);
    refused("def c9", 1, def9, N(def9), XL_E_REGISTER, 1);
    refused("def cut short", 1, shortdef, N(shortdef), XL_E_TOKENS, 1);
    refused("texm3x3spec's eye in r0", 1, spec_r, N(spec_r), XL_E_REGISTER, 9);
    refused("tex co-issued", 1, pair_tex, N(pair_tex), XL_E_OPCODE, 4);
    refused("vs saturate", 0, vsat, N(vsat), XL_E_MODIFIER, 1);
    refused("vs relative r1", 0, vrelr, N(vrelr), XL_E_MODIFIER, 1);
    refused("vs r12", 0, vr12, N(vr12), XL_E_REGISTER, 1);
    refused("vs v16", 0, vv16, N(vv16), XL_E_REGISTER, 1);
    refused("vs c256", 0, vc256, N(vc256), XL_E_REGISTER, 1);
    refused("vs reading oPos", 0, voread, N(voread), XL_E_REGISTER, 1);
    refused("vs tex", 0, vtex, N(vtex), XL_E_OPCODE, 1);
    refused("vs a1", 0, va0y, N(va0y), XL_E_REGISTER, 1);
    refused("vs lrp", 0, vlrp, N(vlrp), XL_E_OPCODE, 1);
    refused("vs m4x4 past c255", 0, vrows, N(vrows), XL_E_REGISTER, 1);
    refused("vs _dz", 0, vdz, N(vdz), XL_E_MODIFIER, 1);
    refused("vs output 3 of RASTOUT", 0, vo3, N(vo3), XL_E_REGISTER, 1);
    /* 70 m4x4 are 280 instructions */
    vlong[0] = VSV(1);
    for (i = 0; i < 70; i++) {
        vlong[1 + 4 * i]     = 20;
        vlong[1 + 4 * i + 1] = DT(R, 0, 15);
        vlong[1 + 4 * i + 2] = ST(V, 0, XYZW, 0);
        vlong[1 + 4 * i + 3] = ST(C, 0, XYZW, 0);
    }
    vlong[N(vlong) - 1] = END;
    refused("vs too long", 0, vlong, N(vlong), XL_E_LENGTH, 1 + 4 * 64);
    /* 33 def constants */
    defs[0] = VSV(1);
    for (i = 0; i < 33; i++) {
        defs[1 + 6 * i]     = 81;
        defs[1 + 6 * i + 1] = DT(C, i, 15);
        defs[1 + 6 * i + 2] = defs[1 + 6 * i + 3] = defs[1 + 6 * i + 4] = defs[1 + 6 * i + 5] = 0;
    }
    defs[N(defs) - 1] = END;
    refused("33 defs", 0, defs, N(defs), XL_E_DEFS, 1 + 6 * 32);
    CHECK(strcmp(xl_why(XL_E_LENGTH), "too long lowered") == 0);
    CHECK(strcmp(xl_why(99), "?") == 0);
}

/* --- The GPU's check --------------------------------------------------------------- */

static int
check1(uint32_t kind, uint32_t range, uint32_t d, uint32_t a, uint32_t b, uint32_t c, uint32_t *bad)
{
    const uint32_t     ins[4] = { d, a, b, c };
    vgpu98_prog_info_t info;
    const int          r = vgpu98_prog_check(kind, range, ins, 1, &info);

    *bad = info.bad;
    return r;
}

static void
test_check(void)
{
    const uint32_t     P = VGPU98_PROG_PIXEL;
    const uint32_t     Vk = VGPU98_PROG_VERTEX;
    uint32_t           bad;
    uint32_t           two[8] = { 0 };
    vgpu98_prog_info_t info;

    CHECK(check1(P, F1, I(MOV, TEMP, 0, 15), G(INPUT, 0), 0, 0, &bad) == 0 && bad == 0);
    CHECK(check1(Vk, 0, I(MOV, OUTPUT, 0, 15), G(INPUT, 0), 0, 0, &bad) == 0);
    CHECK(check1(3, F1, I(MOV, TEMP, 0, 15), G(INPUT, 0), 0, 0, &bad) == 3 && bad == 1);
    CHECK(check1(P, 0x3F000000u, I(MOV, TEMP, 0, 15), G(INPUT, 0), 0, 0, &bad) == 3 && bad == 2);
    CHECK(check1(Vk, F1, I(MOV, OUTPUT, 0, 15), G(INPUT, 0), 0, 0, &bad) == 3 && bad == 2);
    CHECK(vgpu98_prog_check(P, F1, two, 0, &info) == 3 && info.bad == 3);
    CHECK(check1(P, F1, I(KILL, NONE, 0, 1), G(TEMP, 0), 0, 0, &bad) == 3 && bad == 3);
    CHECK(check1(P, F1, I(MOV, TEMP, 0, 15) | PROJ, G(TEMP, 0), 0, 0, &bad) == 3);
    CHECK(check1(P, F1, I(BEM, TEMP, 0, 3) | PROJ, G(COORD, 0), G(TEMP, 1), STAGE(0), &bad) == 0);
    CHECK(check1(P, F1, I(LUM, TEMP, 0, 15) | PROJ, G(TEMP, 0), G(TEMP, 1), STAGE(0), &bad) == 3);
    CHECK(check1(Vk, 0, I(MOV, TEMP, 0, 15) | NC, G(TEMP, 0), 0, 0, &bad) == 3);
    CHECK(check1(P, F1, I(KILL, NONE, 0, 0) | NC, G(TEMP, 0), 0, 0, &bad) == 3);
    CHECK(check1(P, F1, I(MOV, TEMP, 0, 15) | (4u << 25), G(TEMP, 0), 0, 0, &bad) == 3);
    CHECK(check1(Vk, 0, I(MOV, TEMP, 0, 15) | VGPU98_INS_SHIFT(1), G(TEMP, 0), 0, 0, &bad) == 3);
    CHECK(check1(Vk, 0, I(MOV, TEMP, 0, 15), G(TEMP, 0) | VGPU98_SRC_RELATIVE, 0, 0, &bad) == 3 && bad == 4);
    CHECK(check1(P, F1, I(MOV, TEMP, 0, 15), G(CONST, 0) | VGPU98_SRC_RELATIVE, 0, 0, &bad) == 3);
    CHECK(check1(P, F1, I(ADD, TEMP, 0, 15), GM(TEMP, 0, XYZW, 9), G(TEMP, 1), 0, &bad) == 3 && bad == 4);
    CHECK(check1(P, F1, I(SAMPLE, TEMP, 0, 15), G(COORD, 0), GM(TEMP, 0, XYZW, 10), 0, &bad) == 3 && bad == 5);
    CHECK(check1(P, F1, I(SAMPLE, TEMP, 0, 15), GM(COORD, 0, XYZW, 9), STAGE(0), 0, &bad) == 0);
    CHECK(check1(P, F1, I(SAMPLE, TEMP, 0, 15), G(COORD, 0), G(TEMP, 0), 0, &bad) == 3 && bad == 5);
    CHECK(check1(P, F1, I(SAMPLE, TEMP, 0, 15), G(COORD, 0), STAGE(8), 0, &bad) == 3);
    CHECK(check1(P, F1, I(SAMPLE, TEMP, 0, 15), G(COORD, 0), GS(STAGE, 0, XXXX), 0, &bad) == 3);
    CHECK(check1(P, F1, I(MOV, TEMP, 0, 15), STAGE(0), 0, 0, &bad) == 3);
    CHECK(check1(Vk, 0, I(MOV, TEMP, 0, 15), G(COORD, 0), 0, 0, &bad) == 3);
    CHECK(check1(P, F1, I(MOV, TEMP, 0, 15), G(INPUT, 2), 0, 0, &bad) == 3);
    CHECK(check1(P, F1, I(MOV, TEMP, 0, 15), G(CONST, 8), 0, 0, &bad) == 3);
    CHECK(check1(Vk, 0, I(MOV, TEMP, 0, 15), G(CONST, 255), 0, 0, &bad) == 0);
    CHECK(check1(P, F1, I(MOV, TEMP, 16, 15), G(TEMP, 0), 0, 0, &bad) == 3);
    CHECK(check1(P, F1, I(MOV, OUTPUT, 0, 15), G(TEMP, 0), 0, 0, &bad) == 3);
    CHECK(check1(P, F1, I(MOV, TEMP, 0, 0), G(TEMP, 0), 0, 0, &bad) == 3);
    CHECK(check1(Vk, 0, I(MOV, OUTPUT, 13, 15), G(TEMP, 0), 0, 0, &bad) == 3);
    CHECK(check1(Vk, 0, I(MOVA, ADDR, 0, 2), G(TEMP, 0), 0, 0, &bad) == 3);
    CHECK(check1(Vk, 0, I(MOVA, ADDR, 0, 1), G(TEMP, 0), 0, 0, &bad) == 0);
    CHECK(check1(P, F1, I(MOVA, ADDR, 0, 1), G(TEMP, 0), 0, 0, &bad) == 3);
    CHECK(check1(Vk, 0, I(SAMPLE, TEMP, 0, 15), G(TEMP, 0), STAGE(0), 0, &bad) == 3);
    CHECK(check1(P, F1, I(MOV, TEMP, 0, 15), G(TEMP, 0), G(TEMP, 1), 0, &bad) == 3 && bad == 5);
    CHECK(check1(P, F1, 0x30, 0, 0, 0, &bad) == 3);
    CHECK(check1(P, F1, I(MOV, TEMP, 0, 15) | 0x40000000u, G(TEMP, 0), 0, 0, &bad) == 3);
    CHECK(check1(P, F1, I(MOV, TEMP, 0, 15), G(TEMP, 0) | 0x02000000u, 0, 0, &bad) == 3);
    CHECK(check1(P, F1, I(MOV, TEMP, 0, 15), G(FIXED, 4), 0, 0, &bad) == 3);
    CHECK(check1(P, F1, I(KILL, NONE, 0, 0) | WSAT, G(TEMP, 0), 0, 0, &bad) == 3);
    CHECK(check1(P, F1, I(DEPTH, NONE, 0, 0) | WSAT, G(TEMP, 0), 0, 0, &bad) == 3);
    CHECK(check1(Vk, 0, I(MOVA, ADDR, 0, 1) | WSAT, G(TEMP, 0), 0, 0, &bad) == 3);
    two[0] = I(DEPTH, NONE, 0, 0);
    two[1] = G(TEMP, 0);
    two[2] = two[3] = 0;
    two[4] = two[0];
    two[5] = two[1];
    two[6] = two[7] = 0;
    CHECK(vgpu98_prog_check(P, F1, two, 2, &info) == 3 && info.bad == 7);
    CHECK(vgpu98_prog_check(P, F1, two, 1, &info) == 0 && info.flags == VGPU98_PROGF_DEPTH);
}

int
main(void)
{
    test_3dmark();
    test_ps_arith();
    test_ps_pairs();
    test_ps_tex();
    test_ps14();
    test_ps14_more();
    test_vs();
    test_vs10();
    test_empty();
    test_strip();
    test_refusals();
    test_check();
    CHECK_DONE();
}

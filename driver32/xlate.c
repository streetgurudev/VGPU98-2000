/*
 * vgpu98dx.dll: DirectX 8's shader tokens into the GPU's programs, as
 * spec/directx-hal.md (Shaders) has them: pixel shaders 1.0-1.4 and vertex
 * shaders 1.0-1.1, each instruction checked, its registers mapped, the
 * texture instructions and macros lowered into the GPU's operations, and
 * the result passed through the GPU's own check (vid_vgpu98_prog.h).
 *
 * The token format is DirectX 8's (the DDKs' and SDKs' d3d8types.h layout,
 * read for its facts only): a version
 * token, instruction tokens (the opcode in the low word, bit 30 co-issue),
 * each followed by its parameter tokens (bit 31 set: the register number in
 * bits 0-12, bit 13 relative addressing, the type in bits 28-30; a
 * destination's write mask in bits 16-19, its saturation in bits 20-23 and
 * its shift in bits 24-27; a source's swizzle in bits 16-23 and its
 * modifier in bits 24-27), def's four raw floats, comments (FFFEh, their
 * length in bits 16-30) and END (FFFFh).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include "vid_vgpu98_prog.h"
#include "xlate.h"

#define TK_PARAM    0x80000000u
#define TK_COISSUE  0x40000000u
#define TK_RELATIVE 0x00002000u
#define TK_REG(t)   ((t) & 0x1FFFu)
#define TK_TYPE(t)  (((t) >> 28) & 7u)
#define TK_MASK(t)  (((t) >> 16) & 0xFu)
#define TK_DMOD(t)  (((t) >> 20) & 0xFu)
#define TK_SHIFT(t) (((t) >> 24) & 0xFu)
#define TK_SWZ(t)   (((t) >> 16) & 0xFFu)
#define TK_SMOD(t)  (((t) >> 24) & 0xFu)

/* Register types */
#define RT_TEMP    0
#define RT_INPUT   1
#define RT_CONST   2
#define RT_TEXTURE 3 /* a pixel shader's t registers; a vertex shader's a0 */
#define RT_ADDR    3
#define RT_RASTOUT 4 /* 0 oPos, 1 oFog, 2 oPts */
#define RT_ATTROUT 5 /* oD0, oD1 */
#define RT_TEXCRD  6 /* oT0-oT7 */

/* Opcodes */
#define OP_NOP          0
#define OP_MOV          1
#define OP_ADD          2
#define OP_SUB          3
#define OP_MAD          4
#define OP_MUL          5
#define OP_RCP          6
#define OP_RSQ          7
#define OP_DP3          8
#define OP_DP4          9
#define OP_MIN          10
#define OP_MAX          11
#define OP_SLT          12
#define OP_SGE          13
#define OP_EXP          14
#define OP_LOG          15
#define OP_LIT          16
#define OP_DST          17
#define OP_LRP          18
#define OP_FRC          19
#define OP_M4X4         20
#define OP_M4X3         21
#define OP_M3X4         22
#define OP_M3X3         23
#define OP_M3X2         24
#define OP_TEXCOORD     64 /* 1.4: texcrd */
#define OP_TEXKILL      65
#define OP_TEX          66 /* 1.4: texld */
#define OP_TEXBEM       67
#define OP_TEXBEML      68
#define OP_TEXREG2AR    69
#define OP_TEXREG2GB    70
#define OP_TEXM3X2PAD   71
#define OP_TEXM3X2TEX   72
#define OP_TEXM3X3PAD   73
#define OP_TEXM3X3TEX   74
#define OP_TEXM3X3SPEC  76
#define OP_TEXM3X3VSPEC 77
#define OP_EXPP         78
#define OP_LOGP         79
#define OP_CND          80
#define OP_DEF          81
#define OP_TEXREG2RGB   82
#define OP_TEXDP3TEX    83
#define OP_TEXM3X2DEPTH 84
#define OP_TEXDP3       85
#define OP_TEXM3X3      86
#define OP_TEXDEPTH     87
#define OP_CMP          88
#define OP_BEM          89
#define OP_PHASE        0xFFFD
#define OP_COMMENT      0xFFFE
#define OP_END          0xFFFF

/* The GPU's temporaries: a pixel shader 1.0-1.3's t(n) is TEMP 2 + n; 6 and
   7 the lowering's scratch S and S2, 8-15 copies (co-issue, ps 1.4's
   phases); a vertex shader's 12-15 are scratch. */
#define PS_T(n)  (2u + (n))
#define PS_S     6u
#define PS_S2    7u
#define PS_COPY  8u
#define VS_TEMPS 12u
#define VS_S     12u

#define WM_XYZ (VGPU98_WM_X | VGPU98_WM_Y | VGPU98_WM_Z)
#define NC     VGPU98_INS_NOCLAMP

typedef struct xl {
    const uint32_t *tok;
    uint32_t        n;
    xl_out_t       *o;
    int             v14;    /* a pixel shader 1.4 */
    uint32_t        at;     /* the instruction being translated */
    int             failed;
    /* A temporary read through its copy (its scratch register plus one; 0:
       read as it is): the second of a co-issued pair's, a ps 1.4 phase's
       texture instructions'. */
    uint32_t        copy[VGPU98_PROG_TEMPS];
} xl_t;

static int
fail(xl_t *x, uint32_t why)
{
    if (!x->failed) {
        x->failed = 1;
        x->o->why = why;
        x->o->at  = x->at;
    }
    return 0;
}

static void
emit(xl_t *x, uint32_t d, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t *w;

    if (x->failed)
        return;
    if (x->o->n >= VGPU98_PROG_MAX_INS) {
        fail(x, XL_E_LENGTH);
        return;
    }
    w    = x->o->ins + VGPU98_PROG_INS_DWORDS * x->o->n++;
    w[0] = d;
    w[1] = a;
    w[2] = b;
    w[3] = c;
}

static uint32_t
src_temp(uint32_t r, uint32_t swz)
{
    return VGPU98_SRC(VGPU98_PF_TEMP, r, swz, VGPU98_PM_NONE);
}

static uint32_t
src_coord(uint32_t m, uint32_t swz)
{
    return VGPU98_SRC(VGPU98_PF_COORD, m, swz, VGPU98_PM_NONE);
}

static uint32_t
src_stage(uint32_t s)
{
    return VGPU98_SRC(VGPU98_PF_STAGE, s, VGPU98_SWZ_XYZW, VGPU98_PM_NONE);
}

static uint32_t
src_fixed(uint32_t v)
{
    return VGPU98_SRC(VGPU98_PF_FIXED, v, VGPU98_SWZ_XYZW, VGPU98_PM_NONE);
}

/* The swizzle `outer` applied after `inner`: component k reads the one
   inner selects at outer's selector k. */
static uint32_t
swz_compose(uint32_t inner, uint32_t outer)
{
    uint32_t r = 0;
    uint32_t k;

    for (k = 0; k < 4; k++)
        r |= ((inner >> (2 * ((outer >> (2 * k)) & 3u))) & 3u) << (2 * k);
    return r;
}

/* A destination's shift (bits 24-27: 1-3 x2-x8, 13-15 d8-d2) as the GPU's
   -3 to 3; 4 for one DirectX 8 does not have. */
static int
shift_of(uint32_t s)
{
    if (s <= 3)
        return (int) s;
    if (s >= 13)
        return (int) s - 16;
    return 4;
}

/* --- Tokens ----------------------------------------------------------------------- */

/* The instruction at or after token i (comments stepped over); n at END or
   past the end. */
static uint32_t
xl_next(const xl_t *x, uint32_t i)
{
    while (i < x->n) {
        const uint32_t t = x->tok[i];

        if (t & TK_PARAM)
            return i; /* refused where it is read */
        if ((t & 0xFFFFu) == OP_END)
            return x->n;
        if ((t & 0xFFFFu) != OP_COMMENT)
            return i;
        i += 1 + ((t >> 16) & 0x7FFFu);
    }
    return x->n;
}

/* The parameter tokens after the instruction at token i: def's register
   and four raw values, else those with bit 31 set. */
static uint32_t
xl_params(const xl_t *x, uint32_t i)
{
    uint32_t j;

    if ((x->tok[i] & 0xFFFFu) == OP_DEF)
        return (x->n - i - 1 >= 5) ? 5 : x->n - i - 1;
    for (j = i + 1; (j < x->n) && (x->tok[j] & TK_PARAM); j++)
        ;
    return j - i - 1;
}

/* def: the register's four values, loaded whenever the shader is set. */
static void
xl_def(xl_t *x, const uint32_t *p, uint32_t consts)
{
    xl_out_t      *o   = x->o;
    const uint32_t reg = TK_REG(p[0]);
    uint32_t       i;
    uint32_t       k;

    if ((TK_TYPE(p[0]) != RT_CONST) || (reg >= consts)) {
        fail(x, XL_E_REGISTER);
        return;
    }
    for (i = 0; (i < o->n_def) && (o->def[i][0] != reg); i++)
        ;
    if (i == o->n_def) {
        if (o->n_def == XL_DEFS_MAX) {
            fail(x, XL_E_DEFS);
            return;
        }
        o->n_def++;
    }
    o->def[i][0] = reg;
    for (k = 0; k < 4; k++)
        o->def[i][1 + k] = p[1 + k];
}

static void
xl_start(xl_t *x, const uint32_t *tok, uint32_t n, xl_out_t *o, uint32_t kind)
{
    uint32_t i;

    x->tok    = tok;
    x->n      = n;
    x->o      = o;
    x->v14    = 0;
    x->at     = 0;
    x->failed = 0;
    for (i = 0; i < VGPU98_PROG_TEMPS; i++)
        x->copy[i] = 0;
    o->kind    = kind;
    o->version = n ? tok[0] : 0;
    o->n       = 0;
    o->n_def   = 0;
    o->n_in    = 0;
    o->why     = XL_OK;
    o->at      = 0;
    o->info.count = o->info.stages = o->info.coords = o->info.inputs = o->info.outputs = o->info.flags = o->info.bad = 0;
}

/* The program passes the GPU's check, or the translator is wrong. An empty
   one (def alone) gets an instruction that changes nothing it outputs. */
static int
xl_finish(xl_t *x, uint32_t range)
{
    xl_out_t *o = x->o;

    if (x->failed)
        return 0;
    if (!o->n)
        emit(x, VGPU98_INS(VGPU98_PI_MOV, VGPU98_PF_TEMP, 15, VGPU98_WM_ALL), src_fixed(VGPU98_PX_ZERO), 0, 0);
    if (vgpu98_prog_check(o->kind, range, o->ins, o->n, &o->info))
        return fail(x, XL_E_CHECK);
    return 1;
}

/* --- Pixel shaders ------------------------------------------------------------------ */

static int
ps_tex_op(uint32_t op)
{
    return (op >= OP_TEXCOORD) && (op <= OP_TEXDEPTH) && (op != 75) && (op != OP_EXPP) && (op != OP_LOGP) && (op != OP_CND) &&
           (op != OP_DEF);
}

/* The parameters an opcode takes in the version; -1 for one it lacks. */
static int
ps_nparams(const xl_t *x, uint32_t op)
{
    switch (op) {
        case OP_NOP:
            return 0;
        case OP_MOV:
            return 2;
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DP3:
        case OP_DP4:
            return 3;
        case OP_MAD:
        case OP_LRP:
        case OP_CND:
        case OP_CMP:
            return 4;
        case OP_DEF:
            return 5;
        case OP_BEM:
            return x->v14 ? 3 : -1;
        case OP_PHASE:
            return x->v14 ? 0 : -1;
        case OP_TEX:
        case OP_TEXCOORD:
            return x->v14 ? 2 : 1;
        case OP_TEXKILL:
            return 1;
        case OP_TEXDEPTH:
            return x->v14 ? 1 : -1;
        case OP_TEXBEM:
        case OP_TEXBEML:
        case OP_TEXREG2AR:
        case OP_TEXREG2GB:
        case OP_TEXREG2RGB:
        case OP_TEXM3X2PAD:
        case OP_TEXM3X2TEX:
        case OP_TEXM3X2DEPTH:
        case OP_TEXM3X3PAD:
        case OP_TEXM3X3TEX:
        case OP_TEXM3X3:
        case OP_TEXM3X3VSPEC:
        case OP_TEXDP3:
        case OP_TEXDP3TEX:
            return x->v14 ? -1 : 2;
        case OP_TEXM3X3SPEC:
            return x->v14 ? -1 : 3;
        default:
            return -1;
    }
}

/* A source token's GPU temporary (r, and 1.0-1.3's t), ~0 if it names
   none. */
static uint32_t
ps_temp_of(const xl_t *x, uint32_t t)
{
    if (TK_TYPE(t) == RT_TEMP)
        return TK_REG(t);
    if ((TK_TYPE(t) == RT_TEXTURE) && !x->v14)
        return PS_T(TK_REG(t));
    return ~0u;
}

/* An arithmetic source as the GPU's. */
static int
ps_src(xl_t *x, uint32_t t, uint32_t *s)
{
    const uint32_t reg = TK_REG(t);
    uint32_t       file;
    uint32_t       r = reg;

    if ((t & TK_RELATIVE) || (TK_SMOD(t) > VGPU98_PM_X2NEG))
        return fail(x, XL_E_MODIFIER);
    switch (TK_TYPE(t)) {
        case RT_TEMP:
            if (reg >= (x->v14 ? 6u : 2u))
                return fail(x, XL_E_REGISTER);
            file = VGPU98_PF_TEMP;
            break;
        case RT_INPUT:
            if (reg >= 2)
                return fail(x, XL_E_REGISTER);
            file = VGPU98_PF_INPUT;
            break;
        case RT_CONST:
            if (reg >= VGPU98_PROG_PCONSTS)
                return fail(x, XL_E_REGISTER);
            file = VGPU98_PF_CONST;
            break;
        case RT_TEXTURE:
            /* 1.4's t registers are coordinates, read by texld and texcrd only. */
            if (x->v14 || (reg >= 4))
                return fail(x, XL_E_REGISTER);
            file = VGPU98_PF_TEMP;
            r    = PS_T(reg);
            break;
        default:
            return fail(x, XL_E_REGISTER);
    }
    if ((file == VGPU98_PF_TEMP) && x->copy[r])
        r = x->copy[r] - 1;
    *s = VGPU98_SRC(file, r, TK_SWZ(t), TK_SMOD(t));
    return 1;
}

/* An arithmetic destination as the GPU's dword 0, for operation `gop`. */
static int
ps_dst(xl_t *x, uint32_t t, uint32_t gop, uint32_t *d)
{
    const uint32_t reg   = TK_REG(t);
    const int      shift = shift_of(TK_SHIFT(t));
    uint32_t       r;

    switch (TK_TYPE(t)) {
        case RT_TEMP:
            if (reg >= (x->v14 ? 6u : 2u))
                return fail(x, XL_E_REGISTER);
            r = reg;
            break;
        case RT_TEXTURE:
            if (x->v14 || (reg >= 4))
                return fail(x, XL_E_REGISTER);
            r = PS_T(reg);
            break;
        default:
            return fail(x, XL_E_REGISTER);
    }
    if (!TK_MASK(t) || (TK_DMOD(t) > 1) || (shift == 4))
        return fail(x, XL_E_MODIFIER);
    *d = VGPU98_INS(gop, VGPU98_PF_TEMP, r, TK_MASK(t)) | (TK_DMOD(t) ? VGPU98_INS_SAT : 0) | VGPU98_INS_SHIFT(shift);
    return 1;
}

static uint32_t
ps_gop(uint32_t op)
{
    switch (op) {
        case OP_MOV:
            return VGPU98_PI_MOV;
        case OP_ADD:
            return VGPU98_PI_ADD;
        case OP_SUB:
            return VGPU98_PI_SUB;
        case OP_MAD:
            return VGPU98_PI_MAD;
        case OP_MUL:
            return VGPU98_PI_MUL;
        case OP_DP3:
            return VGPU98_PI_DP3;
        case OP_DP4:
            return VGPU98_PI_DP4;
        case OP_LRP:
            return VGPU98_PI_LRP;
        case OP_CND:
            return VGPU98_PI_CND;
        case OP_CMP:
            return VGPU98_PI_CMP;
        case OP_BEM:
            return VGPU98_PI_BEM;
        default:
            return 0;
    }
}

/* An arithmetic instruction: its operation, destination and sources as
   they are; bem's stage is its destination register's number, its result a
   coordinate (not clamped). */
static void
ps_arith(xl_t *x, uint32_t op, const uint32_t *p, uint32_t np)
{
    const uint32_t gop = ps_gop(op);
    uint32_t       s[3] = { 0, 0, 0 };
    uint32_t       d;
    uint32_t       k;

    if (!gop) {
        fail(x, XL_E_OPCODE);
        return;
    }
    if (!ps_dst(x, p[0], gop, &d))
        return;
    for (k = 1; k < np; k++) {
        if (!ps_src(x, p[k], &s[k - 1]))
            return;
    }
    if (op == OP_BEM)
        emit(x, d | NC, s[0], s[1], src_stage(VGPU98_INS_DREG(d)));
    else
        emit(x, d, s[0], s[1], s[2]);
}

/* A co-issued pair (the second's bit 30): read together, so where the
   second reads the register the first writes, it reads a copy taken before
   the first. */
static void
ps_pair(xl_t *x, uint32_t i, uint32_t j)
{
    const uint32_t *p1  = x->tok + i + 1;
    const uint32_t *p2  = x->tok + j + 1;
    const uint32_t  np1 = xl_params(x, i);
    const uint32_t  np2 = xl_params(x, j);
    const uint32_t  op2 = x->tok[j] & 0xFFFFu;
    uint32_t        d1;
    uint32_t        r1;
    uint32_t        k;
    int             need = 0;

    if (!ps_dst(x, p1[0], VGPU98_PI_MOV, &d1))
        return;
    r1 = VGPU98_INS_DREG(d1);
    x->at = j;
    if ((ps_nparams(x, op2) < 0) || ps_tex_op(op2) || (op2 == OP_DEF) || (op2 == OP_PHASE) || (op2 == OP_NOP)) {
        fail(x, XL_E_OPCODE);
        return;
    }
    if ((uint32_t) ps_nparams(x, op2) != np2) {
        fail(x, XL_E_TOKENS);
        return;
    }
    for (k = 1; k < np2; k++)
        need |= ps_temp_of(x, p2[k]) == r1;
    x->at = i;
    if (need) {
        emit(x, VGPU98_INS(VGPU98_PI_MOV, VGPU98_PF_TEMP, PS_COPY, VGPU98_WM_ALL) | NC, src_temp(r1, VGPU98_SWZ_XYZW), 0, 0);
        x->copy[r1] = PS_COPY + 1;
    }
    ps_arith(x, x->tok[i] & 0xFFFFu, p1, np1);
    x->at = j;
    ps_arith(x, op2, p2, np2);
    x->copy[r1] = 0;
}

/* --- Pixel shaders 1.0-1.3's texture instructions ---------------------------------- */

/* Its destination t(m): m, ~0 refused. */
static uint32_t
ps_treg(xl_t *x, uint32_t t)
{
    if ((TK_TYPE(t) != RT_TEXTURE) || (TK_REG(t) >= 4)) {
        fail(x, XL_E_REGISTER);
        return ~0u;
    }
    return TK_REG(t);
}

/* Its source t(n), its modifier kept and its swizzle followed by `swz`. */
static int
ps_tsrc(xl_t *x, uint32_t t, uint32_t swz, uint32_t *s)
{
    if ((TK_TYPE(t) != RT_TEXTURE) || (TK_REG(t) >= 4))
        return fail(x, XL_E_REGISTER);
    if ((t & TK_RELATIVE) || (TK_SMOD(t) > VGPU98_PM_X2NEG))
        return fail(x, XL_E_MODIFIER);
    *s = VGPU98_SRC(VGPU98_PF_TEMP, PS_T(TK_REG(t)), swz_compose(TK_SWZ(t), swz), TK_SMOD(t));
    return 1;
}

/* The 3x3 matrix's rows m-2, m-1 (the pads' dot products) and m: its
   result vector in S. */
static void
ps_m3x3(xl_t *x, uint32_t m, uint32_t s)
{
    emit(x, VGPU98_INS(VGPU98_PI_DP3, VGPU98_PF_TEMP, PS_S, VGPU98_WM_Z) | NC, src_coord(m, VGPU98_SWZ_XYZW), s, 0);
    emit(x, VGPU98_INS(VGPU98_PI_MOV, VGPU98_PF_TEMP, PS_S, VGPU98_WM_X) | NC, src_temp(PS_T(m - 2), VGPU98_SWZ_REP(0)), 0, 0);
    emit(x, VGPU98_INS(VGPU98_PI_MOV, VGPU98_PF_TEMP, PS_S, VGPU98_WM_Y) | NC, src_temp(PS_T(m - 1), VGPU98_SWZ_REP(0)), 0, 0);
}

static void
ps_tex13(xl_t *x, uint32_t op, const uint32_t *p, uint32_t np)
{
    const uint32_t m = ps_treg(x, p[0]);
    const uint32_t T = PS_T(m);
    uint32_t       s = 0;
    uint32_t       c;

    if (x->failed)
        return;
    /* t(n), as the instruction reads it */
    if (np >= 2) {
        const uint32_t swz = (op == OP_TEXREG2AR) ? VGPU98_SWZ(3, 0, 0, 0)
                             : (op == OP_TEXREG2GB) ? VGPU98_SWZ(1, 2, 2, 2)
                                                    : VGPU98_SWZ_XYZW;

        if (!ps_tsrc(x, p[1], swz, &s))
            return;
    }
    switch (op) {
        case OP_TEX:
            emit(x, VGPU98_INS(VGPU98_PI_SAMPLE, VGPU98_PF_TEMP, T, VGPU98_WM_ALL) | VGPU98_INS_PROJECT,
                 src_coord(m, VGPU98_SWZ_XYZW), src_stage(m), 0);
            break;
        case OP_TEXCOORD:
            emit(x, VGPU98_INS(VGPU98_PI_MOV, VGPU98_PF_TEMP, T, WM_XYZ) | VGPU98_INS_SAT, src_coord(m, VGPU98_SWZ_XYZW), 0, 0);
            emit(x, VGPU98_INS(VGPU98_PI_MOV, VGPU98_PF_TEMP, T, VGPU98_WM_W), src_fixed(VGPU98_PX_ONE), 0, 0);
            break;
        case OP_TEXKILL:
            emit(x, VGPU98_INS(VGPU98_PI_KILL, VGPU98_PF_NONE, 0, 0), src_coord(m, VGPU98_SWZ_XYZW), 0, 0);
            break;
        case OP_TEXBEM:
        case OP_TEXBEML:
            /* The coordinates projected by the stage's PROJECTED, as the fixed
               function's bump stage projects them, then bumped */
            emit(x, VGPU98_INS(VGPU98_PI_BEM, VGPU98_PF_TEMP, PS_S, VGPU98_WM_X | VGPU98_WM_Y) | NC | VGPU98_INS_PROJECT,
                 src_coord(m, VGPU98_SWZ_XYZW), s, src_stage(m));
            emit(x, VGPU98_INS(VGPU98_PI_SAMPLE, VGPU98_PF_TEMP, T, VGPU98_WM_ALL), src_temp(PS_S, VGPU98_SWZ_XYZW), src_stage(m),
                 0);
            if (op == OP_TEXBEML)
                emit(x, VGPU98_INS(VGPU98_PI_LUM, VGPU98_PF_TEMP, T, VGPU98_WM_ALL), src_temp(T, VGPU98_SWZ_XYZW), s, src_stage(m));
            break;
        case OP_TEXREG2AR:
        case OP_TEXREG2GB:
        case OP_TEXREG2RGB:
            emit(x, VGPU98_INS(VGPU98_PI_SAMPLE, VGPU98_PF_TEMP, T, VGPU98_WM_ALL), s, src_stage(m), 0);
            break;
        case OP_TEXM3X2PAD:
        case OP_TEXM3X3PAD:
            emit(x, VGPU98_INS(VGPU98_PI_DP3, VGPU98_PF_TEMP, T, VGPU98_WM_X) | NC, src_coord(m, VGPU98_SWZ_XYZW), s, 0);
            break;
        case OP_TEXM3X2TEX:
        case OP_TEXM3X2DEPTH:
            if (m < 1) {
                fail(x, XL_E_REGISTER);
                return;
            }
            emit(x, VGPU98_INS(VGPU98_PI_DP3, VGPU98_PF_TEMP, PS_S, VGPU98_WM_Y) | NC, src_coord(m, VGPU98_SWZ_XYZW), s, 0);
            emit(x, VGPU98_INS(VGPU98_PI_MOV, VGPU98_PF_TEMP, PS_S, VGPU98_WM_X) | NC, src_temp(PS_T(m - 1), VGPU98_SWZ_REP(0)), 0,
                 0);
            if (op == OP_TEXM3X2TEX)
                emit(x, VGPU98_INS(VGPU98_PI_SAMPLE, VGPU98_PF_TEMP, T, VGPU98_WM_ALL), src_temp(PS_S, VGPU98_SWZ_XYZW),
                     src_stage(m), 0);
            else
                emit(x, VGPU98_INS(VGPU98_PI_DEPTH, VGPU98_PF_NONE, 0, 0), src_temp(PS_S, VGPU98_SWZ_XYZW), 0, 0);
            break;
        case OP_TEXM3X3TEX:
        case OP_TEXM3X3:
        case OP_TEXM3X3SPEC:
        case OP_TEXM3X3VSPEC:
            if (m < 2) {
                fail(x, XL_E_REGISTER);
                return;
            }
            ps_m3x3(x, m, s);
            if (op == OP_TEXM3X3TEX)
                emit(x, VGPU98_INS(VGPU98_PI_SAMPLE, VGPU98_PF_TEMP, T, VGPU98_WM_ALL), src_temp(PS_S, VGPU98_SWZ_XYZW),
                     src_stage(m), 0);
            else if (op == OP_TEXM3X3) {
                emit(x, VGPU98_INS(VGPU98_PI_MOV, VGPU98_PF_TEMP, T, WM_XYZ), src_temp(PS_S, VGPU98_SWZ_XYZW), 0, 0);
                emit(x, VGPU98_INS(VGPU98_PI_MOV, VGPU98_PF_TEMP, T, VGPU98_WM_W), src_fixed(VGPU98_PX_ONE), 0, 0);
            } else {
                if (op == OP_TEXM3X3SPEC) {
                    /* The eye vector a constant's */
                    if ((TK_TYPE(p[2]) != RT_CONST) || (TK_REG(p[2]) >= VGPU98_PROG_PCONSTS)) {
                        fail(x, XL_E_REGISTER);
                        return;
                    }
                    if ((p[2] & TK_RELATIVE) || (TK_SMOD(p[2]) > VGPU98_PM_X2NEG)) {
                        fail(x, XL_E_MODIFIER);
                        return;
                    }
                    c = VGPU98_SRC(VGPU98_PF_CONST, TK_REG(p[2]), TK_SWZ(p[2]), TK_SMOD(p[2]));
                } else {
                    /* ... or the three coordinate sets' fourth components */
                    emit(x, VGPU98_INS(VGPU98_PI_MOV, VGPU98_PF_TEMP, PS_S2, VGPU98_WM_X) | NC, src_coord(m - 2, VGPU98_SWZ_REP(3)),
                         0, 0);
                    emit(x, VGPU98_INS(VGPU98_PI_MOV, VGPU98_PF_TEMP, PS_S2, VGPU98_WM_Y) | NC, src_coord(m - 1, VGPU98_SWZ_REP(3)),
                         0, 0);
                    emit(x, VGPU98_INS(VGPU98_PI_MOV, VGPU98_PF_TEMP, PS_S2, VGPU98_WM_Z) | NC, src_coord(m, VGPU98_SWZ_REP(3)), 0,
                         0);
                    c = src_temp(PS_S2, VGPU98_SWZ_XYZW);
                }
                emit(x, VGPU98_INS(VGPU98_PI_RFL, VGPU98_PF_TEMP, PS_S2, VGPU98_WM_ALL) | NC, src_temp(PS_S, VGPU98_SWZ_XYZW), c,
                     0);
                emit(x, VGPU98_INS(VGPU98_PI_SAMPLE, VGPU98_PF_TEMP, T, VGPU98_WM_ALL), src_temp(PS_S2, VGPU98_SWZ_XYZW),
                     src_stage(m), 0);
            }
            break;
        case OP_TEXDP3:
            emit(x, VGPU98_INS(VGPU98_PI_DP3, VGPU98_PF_TEMP, T, VGPU98_WM_ALL), src_coord(m, VGPU98_SWZ_XYZW), s, 0);
            break;
        case OP_TEXDP3TEX:
            emit(x, VGPU98_INS(VGPU98_PI_DP3, VGPU98_PF_TEMP, PS_S, VGPU98_WM_X) | NC, src_coord(m, VGPU98_SWZ_XYZW), s, 0);
            emit(x, VGPU98_INS(VGPU98_PI_MOV, VGPU98_PF_TEMP, PS_S, VGPU98_WM_Y | VGPU98_WM_Z) | NC, src_fixed(VGPU98_PX_ZERO), 0, 0);
            emit(x, VGPU98_INS(VGPU98_PI_SAMPLE, VGPU98_PF_TEMP, T, VGPU98_WM_ALL), src_temp(PS_S, VGPU98_SWZ_XYZW), src_stage(m),
                 0);
            break;
        default:
            fail(x, XL_E_OPCODE);
            break;
    }
}

/* --- Pixel shaders 1.4's texture instructions ------------------------------------- */

static int
ps14_tex(uint32_t op)
{
    return (op == OP_TEX) || (op == OP_TEXCOORD) || (op == OP_TEXKILL) || (op == OP_TEXDEPTH);
}

/* A run of texture instructions from token i (a phase's): those reading an
   r register an earlier one of the run wrote read it as the phase began,
   through a copy taken before the run. */
static void
ps14_run(xl_t *x, uint32_t i)
{
    uint32_t written = 0;
    uint32_t need    = 0;
    uint32_t next    = PS_COPY;
    uint32_t k;

    for (; i < x->n; i = xl_next(x, i + 1 + xl_params(x, i))) {
        const uint32_t t  = x->tok[i];
        const uint32_t op = t & 0xFFFFu;
        const uint32_t np = xl_params(x, i);
        uint32_t       rd = ~0u;
        uint32_t       wr = ~0u;

        if ((t & TK_PARAM) || !ps14_tex(op) || !np)
            break;
        if ((op == OP_TEX) || (op == OP_TEXCOORD)) {
            if (TK_TYPE(x->tok[i + 1]) == RT_TEMP)
                wr = TK_REG(x->tok[i + 1]);
            if ((np >= 2) && (TK_TYPE(x->tok[i + 2]) == RT_TEMP))
                rd = TK_REG(x->tok[i + 2]);
        } else if (TK_TYPE(x->tok[i + 1]) == RT_TEMP)
            rd = TK_REG(x->tok[i + 1]);
        if ((rd < 6) && ((written >> rd) & 1))
            need |= 1u << rd;
        if (wr < 6)
            written |= 1u << wr;
    }
    for (k = 0; k < 6; k++) {
        if ((need >> k) & 1) {
            emit(x, VGPU98_INS(VGPU98_PI_MOV, VGPU98_PF_TEMP, next, VGPU98_WM_ALL) | NC, src_temp(k, VGPU98_SWZ_XYZW), 0, 0);
            x->copy[k] = next + 1;
            next++;
        }
    }
}

/* texld's and texcrd's source: t(n), a coordinate set, or (texld) r(n),
   with its selector and divide. */
static int
ps14_coords(xl_t *x, uint32_t t, int texld, uint32_t *s)
{
    const uint32_t reg = TK_REG(t);
    const uint32_t mod = TK_SMOD(t);
    uint32_t       file;
    uint32_t       r = reg;

    if ((t & TK_RELATIVE) || ((mod != VGPU98_PM_NONE) && (mod != VGPU98_PM_DZ) && (mod != VGPU98_PM_DW)))
        return fail(x, XL_E_MODIFIER);
    if (reg >= 6)
        return fail(x, XL_E_REGISTER);
    if (TK_TYPE(t) == RT_TEXTURE)
        file = VGPU98_PF_COORD;
    else if ((TK_TYPE(t) == RT_TEMP) && texld) {
        file = VGPU98_PF_TEMP;
        if (x->copy[r])
            r = x->copy[r] - 1;
    } else
        return fail(x, XL_E_REGISTER);
    *s = VGPU98_SRC(file, r, TK_SWZ(t), mod);
    return 1;
}

/* texkill's and texdepth's register: t(n) or r(n). */
static int
ps14_reg(xl_t *x, uint32_t t, int coords_too, uint32_t *s)
{
    const uint32_t reg = TK_REG(t);

    if (reg >= 6)
        return fail(x, XL_E_REGISTER);
    if ((TK_TYPE(t) == RT_TEXTURE) && coords_too)
        *s = src_coord(reg, VGPU98_SWZ_XYZW);
    else if (TK_TYPE(t) == RT_TEMP)
        *s = src_temp(x->copy[reg] ? x->copy[reg] - 1 : reg, VGPU98_SWZ_XYZW);
    else
        return fail(x, XL_E_REGISTER);
    return 1;
}

static void
ps14_texop(xl_t *x, uint32_t op, const uint32_t *p)
{
    uint32_t s;
    uint32_t mask;

    if ((op == OP_TEXKILL) || (op == OP_TEXDEPTH)) {
        if (ps14_reg(x, p[0], op == OP_TEXKILL, &s))
            emit(x, VGPU98_INS((op == OP_TEXKILL) ? VGPU98_PI_KILL : VGPU98_PI_DEPTH, VGPU98_PF_NONE, 0, 0), s, 0, 0);
        return;
    }
    /* texld r(m), src: stage m sampled (no projection: 1.4 ignores the
       stage's PROJECTED); texcrd r(m), t(n): the coordinates copied */
    if ((TK_TYPE(p[0]) != RT_TEMP) || (TK_REG(p[0]) >= 6)) {
        fail(x, XL_E_REGISTER);
        return;
    }
    mask = TK_MASK(p[0]) ? TK_MASK(p[0]) : VGPU98_WM_ALL;
    if (!ps14_coords(x, p[1], op == OP_TEX, &s))
        return;
    if (op == OP_TEX)
        emit(x, VGPU98_INS(VGPU98_PI_SAMPLE, VGPU98_PF_TEMP, TK_REG(p[0]), mask), s, src_stage(TK_REG(p[0])), 0);
    else
        emit(x, VGPU98_INS(VGPU98_PI_MOV, VGPU98_PF_TEMP, TK_REG(p[0]), mask), s, 0, 0);
}

int
xl_pixel(const uint32_t *tok, uint32_t n, uint32_t range, xl_out_t *o)
{
    xl_t     x;
    uint32_t i;
    uint32_t next;
    uint32_t k;
    int      in_run = 0;

    xl_start(&x, tok, n, o, VGPU98_PROG_PIXEL);
    if (!n || ((tok[0] & 0xFFFFFF00u) != 0xFFFF0100u) || ((tok[0] & 0xFFu) > 4))
        return fail(&x, XL_E_VERSION);
    x.v14 = (tok[0] & 0xFFu) == 4;
    for (i = xl_next(&x, 1); (i < n) && !x.failed; i = next) {
        const uint32_t  t  = tok[i];
        const uint32_t  op = t & 0xFFFFu;
        const uint32_t *p  = tok + i + 1;
        uint32_t        np;
        int             want;

        x.at = i;
        if (t & TK_PARAM)
            return fail(&x, XL_E_TOKENS);
        np   = xl_params(&x, i);
        next = xl_next(&x, i + 1 + np);
        want = ps_nparams(&x, op);
        if (want < 0)
            return fail(&x, XL_E_OPCODE);
        if ((uint32_t) want != np)
            return fail(&x, XL_E_TOKENS);
        if ((op != OP_DEF) && (op != OP_NOP) && (op != OP_PHASE))
            o->n_in++;
        if (x.v14) {
            if (ps14_tex(op)) {
                if (!in_run) {
                    ps14_run(&x, i);
                    in_run = 1;
                }
                ps14_texop(&x, op, p);
                continue;
            }
            /* The run's over: the registers are read as they are. */
            in_run = 0;
            for (k = 0; k < VGPU98_PROG_TEMPS; k++)
                x.copy[k] = 0;
        }
        switch (op) {
            case OP_NOP:
            case OP_PHASE: /* the temporaries' alpha DirectX 8 calls lost there is kept */
                break;
            case OP_DEF:
                xl_def(&x, p, VGPU98_PROG_PCONSTS);
                break;
            default:
                if (ps_tex_op(op))
                    ps_tex13(&x, op, p, np);
                else if ((next < n) && (tok[next] & TK_COISSUE) && !(tok[next] & TK_PARAM)) {
                    const uint32_t j = next;

                    ps_pair(&x, i, j);
                    o->n_in++; /* the second of the pair */
                    next = xl_next(&x, j + 1 + xl_params(&x, j));
                } else
                    ps_arith(&x, op, p, np);
                break;
        }
    }
    return xl_finish(&x, range);
}

/* --- Vertex shaders ----------------------------------------------------------------- */

static int
vs_nparams(uint32_t op)
{
    switch (op) {
        case OP_NOP:
            return 0;
        case OP_MOV:
        case OP_RCP:
        case OP_RSQ:
        case OP_EXP:
        case OP_LOG:
        case OP_LIT:
        case OP_FRC:
        case OP_EXPP:
        case OP_LOGP:
            return 2;
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_DP3:
        case OP_DP4:
        case OP_MIN:
        case OP_MAX:
        case OP_SLT:
        case OP_SGE:
        case OP_DST:
        case OP_M4X4:
        case OP_M4X3:
        case OP_M3X4:
        case OP_M3X3:
        case OP_M3X2:
            return 3;
        case OP_MAD:
            return 4;
        case OP_DEF:
            return 5;
        default:
            return -1;
    }
}

static uint32_t
vs_gop(uint32_t op)
{
    switch (op) {
        case OP_MOV:
            return VGPU98_PI_MOV;
        case OP_ADD:
            return VGPU98_PI_ADD;
        case OP_SUB:
            return VGPU98_PI_SUB;
        case OP_MAD:
            return VGPU98_PI_MAD;
        case OP_MUL:
            return VGPU98_PI_MUL;
        case OP_RCP:
            return VGPU98_PI_RCP;
        case OP_RSQ:
            return VGPU98_PI_RSQ;
        case OP_DP3:
            return VGPU98_PI_DP3;
        case OP_DP4:
            return VGPU98_PI_DP4;
        case OP_MIN:
            return VGPU98_PI_MIN;
        case OP_MAX:
            return VGPU98_PI_MAX;
        case OP_SLT:
            return VGPU98_PI_SLT;
        case OP_SGE:
            return VGPU98_PI_SGE;
        case OP_EXP:
            return VGPU98_PI_EXP;
        case OP_LOG:
            return VGPU98_PI_LOG;
        case OP_LIT:
            return VGPU98_PI_LIT;
        case OP_DST:
            return VGPU98_PI_DST;
        case OP_FRC:
            return VGPU98_PI_FRC;
        case OP_EXPP:
            return VGPU98_PI_EXPP;
        case OP_LOGP:
            return VGPU98_PI_LOGP;
        default:
            return 0;
    }
}

/* A destination: a temporary or an output (a0 is MOV's: vs_shader). */
static int
vs_dst(xl_t *x, uint32_t t, uint32_t gop, uint32_t *d)
{
    const uint32_t reg = TK_REG(t);
    uint32_t       file = VGPU98_PF_OUTPUT;
    uint32_t       r;

    if (TK_DMOD(t) || TK_SHIFT(t) || !TK_MASK(t))
        return fail(x, XL_E_MODIFIER);
    switch (TK_TYPE(t)) {
        case RT_TEMP:
            if (reg >= VS_TEMPS)
                return fail(x, XL_E_REGISTER);
            file = VGPU98_PF_TEMP;
            r    = reg;
            break;
        case RT_RASTOUT:
            if (reg > 2)
                return fail(x, XL_E_REGISTER);
            r = (reg == 0) ? VGPU98_PO_POS : (reg == 1) ? VGPU98_PO_FOG : VGPU98_PO_PTS;
            break;
        case RT_ATTROUT:
            if (reg >= 2)
                return fail(x, XL_E_REGISTER);
            r = VGPU98_PO_D0 + reg;
            break;
        case RT_TEXCRD:
            if (reg >= 8)
                return fail(x, XL_E_REGISTER);
            r = VGPU98_PO_T0 + reg;
            break;
        default:
            return fail(x, XL_E_REGISTER);
    }
    *d = VGPU98_INS(gop, file, r, TK_MASK(t));
    return 1;
}

/* A source: a temporary, an input, a constant (c[a0.x + n] relative). */
static int
vs_src(xl_t *x, uint32_t t, uint32_t *s)
{
    const uint32_t reg = TK_REG(t);
    uint32_t       file;

    if (TK_SMOD(t) > VGPU98_PM_X2NEG)
        return fail(x, XL_E_MODIFIER);
    switch (TK_TYPE(t)) {
        case RT_TEMP:
            if (reg >= VS_TEMPS)
                return fail(x, XL_E_REGISTER);
            file = VGPU98_PF_TEMP;
            break;
        case RT_INPUT:
            if (reg >= VGPU98_PROG_INPUTS)
                return fail(x, XL_E_REGISTER);
            file = VGPU98_PF_INPUT;
            break;
        case RT_CONST:
            if (reg >= VGPU98_PROG_VCONSTS)
                return fail(x, XL_E_REGISTER);
            file = VGPU98_PF_CONST;
            break;
        default:
            return fail(x, XL_E_REGISTER);
    }
    if ((t & TK_RELATIVE) && (file != VGPU98_PF_CONST))
        return fail(x, XL_E_MODIFIER);
    *s = VGPU98_SRC(file, reg, TK_SWZ(t), TK_SMOD(t)) | ((t & TK_RELATIVE) ? VGPU98_SRC_RELATIVE : 0);
    return 1;
}

/* m4x4, m4x3, m3x4, m3x3, m3x2: a dot product a row, each into one
   component of the destination as its mask allows, the rows the second
   source's register and those after it; through scratch when the
   destination is the first source or a row (no row may read what an
   earlier one wrote). */
static void
vs_macro(xl_t *x, uint32_t op, const uint32_t *p)
{
    const uint32_t rows = ((op == OP_M4X4) || (op == OP_M3X4)) ? 4u : (op == OP_M3X2) ? 2u : 3u;
    const uint32_t gop  = ((op == OP_M4X4) || (op == OP_M4X3)) ? VGPU98_PI_DP4 : VGPU98_PI_DP3;
    uint32_t       d;
    uint32_t       a;
    uint32_t       b;
    uint32_t       rf;
    uint32_t       rr;
    uint32_t       df;
    uint32_t       dr;
    uint32_t       mask;
    uint32_t       r;
    int            alias;

    if (!vs_dst(x, p[0], gop, &d) || !vs_src(x, p[1], &a) || !vs_src(x, p[2], &b))
        return;
    rf   = VGPU98_SRC_FILE(b);
    rr   = VGPU98_SRC_REG(b);
    df   = VGPU98_INS_DFILE(d);
    dr   = VGPU98_INS_DREG(d);
    mask = VGPU98_INS_MASK(d) & ((1u << rows) - 1);
    if (rr + rows > ((rf == VGPU98_PF_TEMP) ? VS_TEMPS : (rf == VGPU98_PF_INPUT) ? (uint32_t) VGPU98_PROG_INPUTS
                                                                                  : (uint32_t) VGPU98_PROG_VCONSTS)) {
        fail(x, XL_E_REGISTER);
        return;
    }
    alias = (df == VGPU98_PF_TEMP) &&
            (((VGPU98_SRC_FILE(a) == VGPU98_PF_TEMP) && (VGPU98_SRC_REG(a) == dr)) || ((rf == VGPU98_PF_TEMP) && (dr >= rr) && (dr < rr + rows)));
    for (r = 0; r < rows; r++) {
        if (!((mask >> r) & 1))
            continue;
        emit(x, alias ? VGPU98_INS(gop, VGPU98_PF_TEMP, VS_S, 1u << r) : VGPU98_INS(gop, df, dr, 1u << r), a,
             (b & ~(0xFFu << 4)) | ((rr + r) << 4), 0);
    }
    if (alias && mask)
        emit(x, VGPU98_INS(VGPU98_PI_MOV, df, dr, mask), src_temp(VS_S, VGPU98_SWZ_XYZW), 0, 0);
}

int
xl_vertex(const uint32_t *tok, uint32_t n, xl_out_t *o)
{
    xl_t     x;
    uint32_t i;
    uint32_t next;

    xl_start(&x, tok, n, o, VGPU98_PROG_VERTEX);
    if (!n || ((tok[0] & 0xFFFFFF00u) != 0xFFFE0100u) || ((tok[0] & 0xFFu) > 1))
        return fail(&x, XL_E_VERSION);
    for (i = xl_next(&x, 1); (i < n) && !x.failed; i = next) {
        const uint32_t  t  = tok[i];
        const uint32_t  op = t & 0xFFFFu;
        const uint32_t *p  = tok + i + 1;
        uint32_t        np;
        uint32_t        gop;
        uint32_t        d;
        uint32_t        s[3] = { 0, 0, 0 };
        uint32_t        k;
        int             want;

        x.at = i;
        if (t & (TK_PARAM | TK_COISSUE))
            return fail(&x, XL_E_TOKENS);
        np   = xl_params(&x, i);
        next = xl_next(&x, i + 1 + np);
        want = vs_nparams(op);
        if (want < 0)
            return fail(&x, XL_E_OPCODE);
        if ((uint32_t) want != np)
            return fail(&x, XL_E_TOKENS);
        if ((op != OP_DEF) && (op != OP_NOP))
            o->n_in++;
        switch (op) {
            case OP_NOP:
                break;
            case OP_DEF:
                xl_def(&x, p, VGPU98_PROG_VCONSTS);
                break;
            case OP_M4X4:
            case OP_M4X3:
            case OP_M3X4:
            case OP_M3X3:
            case OP_M3X2:
                vs_macro(&x, op, p);
                break;
            default:
                gop = vs_gop(op);
                for (k = 1; k < np; k++) {
                    if (!vs_src(&x, p[k], &s[k - 1]))
                        return 0;
                }
                if ((op == OP_MOV) && (TK_TYPE(p[0]) == RT_ADDR)) {
                    /* mov a0.x: the address register, floor(a.x) */
                    if (TK_REG(p[0]) || TK_DMOD(p[0]) || TK_SHIFT(p[0]))
                        return fail(&x, XL_E_REGISTER);
                    emit(&x, VGPU98_INS(VGPU98_PI_MOVA, VGPU98_PF_ADDR, 0, VGPU98_WM_X), s[0], 0, 0);
                    break;
                }
                if (!vs_dst(&x, p[0], gop, &d))
                    return 0;
                if ((op == OP_RCP) || (op == OP_RSQ) || (op == OP_EXP) || (op == OP_LOG) || (op == OP_EXPP) || (op == OP_LOGP)) {
                    /* The scalar operations read the component their
                       source's w selector names (a replicate, as the DDK
                       asks), into x. */
                    const uint32_t c = (VGPU98_SRC_SWZ(s[0]) >> 6) & 3u;

                    s[0] = (s[0] & ~(0xFFu << 12)) | (VGPU98_SWZ_REP(c) << 12);
                }
                emit(&x, d, s[0], s[1], s[2]);
                break;
        }
    }
    return xl_finish(&x, 0);
}

uint32_t
xl_strip(uint32_t *tok, uint32_t n)
{
    xl_t     x;
    uint32_t k = n ? 1 : 0;
    uint32_t i;
    uint32_t j;
    uint32_t np;

    /* Each instruction moves no later than it was, so the tokens read are
       never ones written. */
    x.tok = tok;
    x.n   = n;
    for (i = xl_next(&x, 1); i < n; i = xl_next(&x, i + 1 + np)) {
        np = xl_params(&x, i);
        for (j = 0; j <= np; j++)
            tok[k++] = tok[i + j];
    }
    return k;
}

const char *
xl_why(uint32_t why)
{
    static const char *const names[XL_E_COUNT] = { "translated",       "a version it does not take", "malformed tokens",
                                                   "an unknown opcode", "a register out of place",   "a modifier out of place",
                                                   "too long lowered",  "too many def constants",    "the GPU's check" };

    return (why < XL_E_COUNT) ? names[why] : "?";
}

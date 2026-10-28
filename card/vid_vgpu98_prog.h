/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VGPU98 command set 2.2: the instruction format of the GPU's
 *          vertex and pixel programs, and the check every program passes
 *          before the GPU takes it (PROGRAM). The meaning of every value
 *          is in the project's spec/gpu-commands.md, Command set 2.2.
 *
 *          Kept in the VGPU98 project as card/vid_vgpu98_prog.h and
 *          copied unchanged into 86Box, as vid_vgpu98_gpu.h is: the GPU
 *          validates programs with it, the Windows 98 driver DLL writes
 *          them with it, and the project's suite checks the DLL's
 *          translations against it. Include <stdint.h> (or the DLL's
 *          equivalent) first.
 *
 * Authors: VGPU98 Project contributors
 *
 *          Copyright 2026 VGPU98 Project contributors.
 */
#ifndef VIDEO_VGPU98_PROG_H
#define VIDEO_VGPU98_PROG_H

/* PROGRAM's kinds and limits */
#define VGPU98_PROG_VERTEX      1
#define VGPU98_PROG_PIXEL       2
#define VGPU98_PROG_MAX_ID      4095
#define VGPU98_PROG_MAX_INS     256
#define VGPU98_PROG_INS_DWORDS  4
#define VGPU98_PROG_TEMPS       16
#define VGPU98_PROG_INPUTS      16  /* a vertex program's input registers */
#define VGPU98_PROG_VCONSTS     256 /* the vertex programs' constants */
#define VGPU98_PROG_PCONSTS     8   /* the pixel programs' */
#define VGPU98_PROG_COORDS      8   /* a pixel program's coordinate sets */
#define VGPU98_PROG_STAGES      8

/* Instruction dword 0 */
#define VGPU98_INS(op, dfile, dreg, mask) \
    ((uint32_t) (op) | ((uint32_t) (dfile) << 8) | ((uint32_t) (dreg) << 12) | ((uint32_t) (mask) << 20))
#define VGPU98_INS_SAT          0x01000000u
#define VGPU98_INS_SHIFT(n)     (((uint32_t) (n) & 7u) << 25) /* -3 to 3 */
#define VGPU98_INS_PROJECT      0x10000000u /* SAMPLE, BEM: the coordinates divided as the stage's PROJECTED says */
#define VGPU98_INS_NOCLAMP      0x20000000u /* pixel programs: the result not clamped to the range (a coordinate) */
#define VGPU98_INS_OP(d)        ((d) & 0xffu)
#define VGPU98_INS_DFILE(d)     (((d) >> 8) & 0xfu)
#define VGPU98_INS_DREG(d)      (((d) >> 12) & 0xffu)
#define VGPU98_INS_MASK(d)      (((d) >> 20) & 0xfu)
#define VGPU98_INS_SHIFTOF(d)   ((int) (((d) >> 25) & 7u) - ((((d) >> 25) & 4u) ? 8 : 0))

/* Instruction dwords 1-3: a source */
#define VGPU98_SRC(file, reg, swz, mod) \
    ((uint32_t) (file) | ((uint32_t) (reg) << 4) | ((uint32_t) (swz) << 12) | ((uint32_t) (mod) << 20))
#define VGPU98_SRC_RELATIVE     0x01000000u /* a vertex constant: the register plus a0.x */
#define VGPU98_SRC_FILE(s)      ((s) & 0xfu)
#define VGPU98_SRC_REG(s)       (((s) >> 4) & 0xffu)
#define VGPU98_SRC_SWZ(s)       (((s) >> 12) & 0xffu)
#define VGPU98_SRC_MOD(s)       (((s) >> 20) & 0xfu)
#define VGPU98_SWZ(x, y, z, w)  ((uint32_t) (x) | ((uint32_t) (y) << 2) | ((uint32_t) (z) << 4) | ((uint32_t) (w) << 6))
#define VGPU98_SWZ_XYZW         0xe4u
#define VGPU98_SWZ_REP(c)       ((uint32_t) (c) * 0x55u) /* component c to all four */

/* Write mask bits */
#define VGPU98_WM_X             1u
#define VGPU98_WM_Y             2u
#define VGPU98_WM_Z             4u
#define VGPU98_WM_W             8u
#define VGPU98_WM_ALL           15u

/* Files */
#define VGPU98_PF_NONE          0
#define VGPU98_PF_TEMP          1
#define VGPU98_PF_INPUT         2 /* vertex: v0-v15; pixel: 0 diffuse, 1 specular */
#define VGPU98_PF_CONST         3
#define VGPU98_PF_ADDR          4 /* a0: MOVA's destination, RELATIVE's index */
#define VGPU98_PF_COORD         5 /* pixel: the coordinate sets as interpolated */
#define VGPU98_PF_OUTPUT        6 /* vertex: VGPU98_PO_* */
#define VGPU98_PF_STAGE         7 /* pixel: a texture stage, SAMPLE's, BEM's and LUM's */
#define VGPU98_PF_FIXED         8 /* VGPU98_PX_* */

/* Vertex outputs */
#define VGPU98_PO_POS           0
#define VGPU98_PO_D0            1
#define VGPU98_PO_D1            2
#define VGPU98_PO_FOG           3
#define VGPU98_PO_PTS           4
#define VGPU98_PO_T0            5 /* oT0-oT7: 5-12 */
#define VGPU98_PO_COUNT         13

/* Fixed vectors */
#define VGPU98_PX_ZERO          0 /* (0, 0, 0, 0) */
#define VGPU98_PX_ONE           1 /* (1, 1, 1, 1) */
#define VGPU98_PX_W1            2 /* (0, 0, 0, 1) */
#define VGPU98_PX_HALF          3 /* (0.5, 0.5, 0.5, 0.5) */
#define VGPU98_PX_COUNT         4

/* Source modifiers: DirectX 8's codes */
#define VGPU98_PM_NONE          0
#define VGPU98_PM_NEG           1  /* -x */
#define VGPU98_PM_BIAS          2  /* x - 0.5 */
#define VGPU98_PM_BIASNEG       3  /* -(x - 0.5) */
#define VGPU98_PM_BX2           4  /* 2x - 1 */
#define VGPU98_PM_BX2NEG        5  /* -(2x - 1) */
#define VGPU98_PM_COMP          6  /* 1 - x */
#define VGPU98_PM_X2            7  /* 2x */
#define VGPU98_PM_X2NEG         8  /* -2x */
#define VGPU98_PM_DZ            9  /* x and y over z (1 where z is 0) */
#define VGPU98_PM_DW            10 /* x and y over w */
#define VGPU98_PM_LAST          10

/* Operations */
#define VGPU98_PI_MOV           0x01
#define VGPU98_PI_ADD           0x02
#define VGPU98_PI_SUB           0x03
#define VGPU98_PI_MUL           0x04
#define VGPU98_PI_MAD           0x05
#define VGPU98_PI_DP3           0x06
#define VGPU98_PI_DP4           0x07
#define VGPU98_PI_LRP           0x08
#define VGPU98_PI_CND           0x09
#define VGPU98_PI_CMP           0x0a
#define VGPU98_PI_MIN           0x0b
#define VGPU98_PI_MAX           0x0c
#define VGPU98_PI_SLT           0x0d
#define VGPU98_PI_SGE           0x0e
#define VGPU98_PI_RCP           0x0f
#define VGPU98_PI_RSQ           0x10
#define VGPU98_PI_EXP           0x11
#define VGPU98_PI_LOG           0x12
#define VGPU98_PI_EXPP          0x13
#define VGPU98_PI_LOGP          0x14
#define VGPU98_PI_FRC           0x15
#define VGPU98_PI_LIT           0x16
#define VGPU98_PI_DST           0x17
#define VGPU98_PI_MOVA          0x18 /* vertex */
#define VGPU98_PI_SAMPLE        0x20 /* pixel, from here */
#define VGPU98_PI_BEM           0x21
#define VGPU98_PI_LUM           0x22
#define VGPU98_PI_RFL           0x23
#define VGPU98_PI_KILL          0x24
#define VGPU98_PI_DEPTH         0x25
#define VGPU98_PI_LAST          0x25

/* What a program checked holds (vgpu98_prog_check). */
#define VGPU98_PROGF_DEPTH      1u /* a DEPTH: the pixel's depth is the program's */
#define VGPU98_PROGF_KILL       2u /* a KILL */
#define VGPU98_PROGF_RELATIVE   4u /* a RELATIVE constant */

typedef struct vgpu98_prog_info_t {
    uint32_t count;   /* instructions */
    uint32_t stages;  /* pixel: bit s, stage s is a STAGE operand (its texture is bound for the program) */
    uint32_t coords;  /* pixel: bit n, COORD n is read */
    uint32_t inputs;  /* bit n, INPUT n is read */
    uint32_t outputs; /* vertex: bit n, OUTPUT n is written */
    uint32_t flags;   /* VGPU98_PROGF_* */
    uint32_t bad;     /* the refused program: the PROGRAM payload's dword that is wrong (1 kind, 2 range, 3 on the instructions) */
} vgpu98_prog_info_t;

/* An operation's kinds (bit 0 vertex, bit 1 pixel), its sources, the source
   that is its STAGE operand (3: none) and its destination (0 TEMP or OUTPUT,
   1 ADDR with mask x, 2 NONE with mask 0). 0 kinds: not an operation. */
static inline uint32_t
vgpu98_prog_op(uint32_t op)
{
#define VGPU98_OPD(kinds, nsrc, stage, dest) ((kinds) | ((nsrc) << 2) | ((stage) << 4) | ((dest) << 6))
    switch (op) {
        case VGPU98_PI_MOV:
        case VGPU98_PI_RCP:
        case VGPU98_PI_RSQ:
        case VGPU98_PI_EXP:
        case VGPU98_PI_LOG:
        case VGPU98_PI_EXPP:
        case VGPU98_PI_LOGP:
        case VGPU98_PI_FRC:
        case VGPU98_PI_LIT:
            return VGPU98_OPD(3u, 1u, 3u, 0u);
        case VGPU98_PI_ADD:
        case VGPU98_PI_SUB:
        case VGPU98_PI_MUL:
        case VGPU98_PI_DP3:
        case VGPU98_PI_DP4:
        case VGPU98_PI_MIN:
        case VGPU98_PI_MAX:
        case VGPU98_PI_SLT:
        case VGPU98_PI_SGE:
        case VGPU98_PI_DST:
            return VGPU98_OPD(3u, 2u, 3u, 0u);
        case VGPU98_PI_MAD:
        case VGPU98_PI_LRP:
        case VGPU98_PI_CND:
        case VGPU98_PI_CMP:
            return VGPU98_OPD(3u, 3u, 3u, 0u);
        case VGPU98_PI_MOVA:
            return VGPU98_OPD(1u, 1u, 3u, 1u);
        case VGPU98_PI_SAMPLE:
            return VGPU98_OPD(2u, 2u, 1u, 0u);
        case VGPU98_PI_BEM:
        case VGPU98_PI_LUM:
            return VGPU98_OPD(2u, 3u, 2u, 0u);
        case VGPU98_PI_RFL:
            return VGPU98_OPD(2u, 2u, 3u, 0u);
        case VGPU98_PI_KILL:
        case VGPU98_PI_DEPTH:
            return VGPU98_OPD(2u, 1u, 3u, 2u);
        default:
            return 0;
    }
#undef VGPU98_OPD
}

/* Is `range` (PROGRAM's third dword) right for the kind: a float 1-8 for a
   pixel program, 0 for a vertex program? */
static inline int
vgpu98_prog_range_ok(uint32_t kind, uint32_t range)
{
    union {
        uint32_t u;
        float    f;
    } r;

    r.u = range;
    if (kind == VGPU98_PROG_VERTEX)
        return range == 0;
    return (r.f >= 1.0f) && (r.f <= 8.0f);
}

/* A source of instruction `k`'s operation `op` (source `i`, the operation's
   STAGE operand when `is_stage`) for a program of `kind`: 0 if it is right. */
static inline int
vgpu98_prog_src_bad(uint32_t kind, uint32_t op, uint32_t i, int is_stage, uint32_t s, vgpu98_prog_info_t *info)
{
    const uint32_t file  = VGPU98_SRC_FILE(s);
    const uint32_t reg   = VGPU98_SRC_REG(s);
    const uint32_t mod   = VGPU98_SRC_MOD(s);
    const int      pixel = kind == VGPU98_PROG_PIXEL;

    if (s >> 25)
        return 1;
    if ((s & VGPU98_SRC_RELATIVE) && (pixel || (file != VGPU98_PF_CONST)))
        return 1;
    if (mod > VGPU98_PM_LAST)
        return 1;
    if (((mod == VGPU98_PM_DZ) || (mod == VGPU98_PM_DW)) &&
        (!pixel || !(((op == VGPU98_PI_SAMPLE) && (i == 0)) || (op == VGPU98_PI_MOV))))
        return 1;
    if (is_stage) {
        if ((file != VGPU98_PF_STAGE) || (reg >= VGPU98_PROG_STAGES) || (VGPU98_SRC_SWZ(s) != VGPU98_SWZ_XYZW) || mod)
            return 1;
        info->stages |= 1u << reg;
        return 0;
    }
    switch (file) {
        case VGPU98_PF_TEMP:
            return reg >= VGPU98_PROG_TEMPS;
        case VGPU98_PF_INPUT:
            if (reg >= (pixel ? 2u : (uint32_t) VGPU98_PROG_INPUTS))
                return 1;
            info->inputs |= 1u << reg;
            return 0;
        case VGPU98_PF_CONST:
            if (s & VGPU98_SRC_RELATIVE)
                info->flags |= VGPU98_PROGF_RELATIVE;
            return reg >= (pixel ? (uint32_t) VGPU98_PROG_PCONSTS : (uint32_t) VGPU98_PROG_VCONSTS);
        case VGPU98_PF_COORD:
            if (!pixel || (reg >= VGPU98_PROG_COORDS))
                return 1;
            info->coords |= 1u << reg;
            return 0;
        case VGPU98_PF_FIXED:
            return reg >= VGPU98_PX_COUNT;
        default: /* NONE, ADDR (RELATIVE's only), OUTPUT, STAGE out of place */
            return 1;
    }
}

/* Checks a program's `n` instructions (1-256, 4 dwords each) for a program
   of `kind` with `range` (PROGRAM's dwords 1 and 2; a free, with no
   instructions, is not a program to check): 0 when the GPU takes it, else 3
   (ERROR_CODE 3, the bad dword in info->bad, counted in PROGRAM's payload).
   Fills `info` either way. */
static inline int
vgpu98_prog_check(uint32_t kind, uint32_t range, const uint32_t *ins, uint32_t n, vgpu98_prog_info_t *info)
{
    uint32_t k;
    uint32_t depths = 0;

    info->count = n;
    info->stages = info->coords = info->inputs = info->outputs = info->flags = 0;
    info->bad = 1;
    if ((kind != VGPU98_PROG_VERTEX) && (kind != VGPU98_PROG_PIXEL))
        return 3;
    info->bad = 2;
    if (!vgpu98_prog_range_ok(kind, range))
        return 3;
    info->bad = 3;
    if (!n || (n > VGPU98_PROG_MAX_INS))
        return 3;
    for (k = 0; k < n; k++) {
        const uint32_t *in    = ins + VGPU98_PROG_INS_DWORDS * k;
        const uint32_t  d     = in[0];
        const uint32_t  op    = VGPU98_INS_OP(d);
        const uint32_t  desc  = vgpu98_prog_op(op);
        const uint32_t  nsrc  = (desc >> 2) & 3u;
        const uint32_t  stage = (desc >> 4) & 3u;
        const uint32_t  dest  = (desc >> 6) & 3u;
        const uint32_t  dfile = VGPU98_INS_DFILE(d);
        const uint32_t  dreg  = VGPU98_INS_DREG(d);
        const uint32_t  mask  = VGPU98_INS_MASK(d);
        uint32_t        i;

        info->bad = 3 + VGPU98_PROG_INS_DWORDS * k;
        if (!(desc & kind) || (d >> 30))
            return 3;
        if ((d & VGPU98_INS_PROJECT) && (op != VGPU98_PI_SAMPLE) && (op != VGPU98_PI_BEM))
            return 3;
        if ((d & VGPU98_INS_NOCLAMP) && ((kind != VGPU98_PROG_PIXEL) || (dest != 0)))
            return 3;
        if (((d >> 25) & 7u) == 4u)
            return 3;
        if ((kind == VGPU98_PROG_VERTEX) && ((d >> 25) & 7u))
            return 3; /* no shift in a vertex program */
        if (dest == 2) {
            if (dfile || dreg || mask || (d & VGPU98_INS_SAT) || ((d >> 25) & 7u))
                return 3;
            if ((op == VGPU98_PI_DEPTH) && depths++)
                return 3;
            info->flags |= (op == VGPU98_PI_DEPTH) ? VGPU98_PROGF_DEPTH : VGPU98_PROGF_KILL;
        } else if (dest == 1) {
            if ((dfile != VGPU98_PF_ADDR) || dreg || (mask != VGPU98_WM_X) || (d & VGPU98_INS_SAT))
                return 3;
        } else {
            if (!mask)
                return 3;
            if (dfile == VGPU98_PF_TEMP) {
                if (dreg >= VGPU98_PROG_TEMPS)
                    return 3;
            } else if ((dfile == VGPU98_PF_OUTPUT) && (kind == VGPU98_PROG_VERTEX)) {
                if (dreg >= VGPU98_PO_COUNT)
                    return 3;
                info->outputs |= 1u << dreg;
            } else
                return 3;
        }
        for (i = 0; i < 3; i++) {
            info->bad = 3 + VGPU98_PROG_INS_DWORDS * k + 1 + i;
            if (i >= nsrc) {
                if (in[1 + i])
                    return 3;
            } else if (vgpu98_prog_src_bad(kind, op, i, i == stage, in[1 + i], info))
                return 3;
        }
    }
    info->bad = 0;
    return 0;
}

#endif /* VIDEO_VGPU98_PROG_H */

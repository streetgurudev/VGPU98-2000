/*
 * vgpu98dx.dll: DirectX 8's shader tokens into the GPU's programs
 * (spec/directx-hal.md, Shaders; the program format is card/vid_vgpu98_prog.h,
 * spec/gpu-commands.md, Command set 2.2). Plain C with no Windows types, so
 * the suite compiles it on the host and checks what it writes. Include
 * <stdint.h> (or the DLL's dll.h) and vid_vgpu98_prog.h first.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_XLATE_H
#define VGPU98_XLATE_H

#define XL_DEFS_MAX 32 /* the constants a shader's def instructions give, at most */

/* Why a shader was refused (xl_out_t.why). */
#define XL_OK         0
#define XL_E_VERSION  1 /* a version token the translator does not take */
#define XL_E_TOKENS   2 /* a parameter where an instruction belongs, a parameter count wrong, a comment past the end */
#define XL_E_OPCODE   3 /* an opcode the shader's kind and version do not have */
#define XL_E_REGISTER 4 /* a register file or number out of place */
#define XL_E_MODIFIER 5 /* a modifier, shift, saturation or relative address out of place */
#define XL_E_LENGTH   6 /* more than the GPU's 256 instructions once lowered */
#define XL_E_DEFS     7 /* more def constants than XL_DEFS_MAX */
#define XL_E_CHECK    8 /* the program failed the GPU's own check (the translator's fault) */
#define XL_E_COUNT    9

typedef struct xl_out {
    uint32_t           kind;    /* VGPU98_PROG_VERTEX or VGPU98_PROG_PIXEL */
    uint32_t           version; /* the version token */
    uint32_t           n;       /* instructions written, 4 dwords each */
    uint32_t           n_in;    /* the shader's own instructions (def, nop and the phase marker not counted) */
    uint32_t           ins[VGPU98_PROG_INS_DWORDS * VGPU98_PROG_MAX_INS];
    uint32_t           n_def;   /* the constants def gives: the register, then its four values (raw floats) */
    uint32_t           def[XL_DEFS_MAX][5];
    uint32_t           why;     /* XL_E_*: refused */
    uint32_t           at;      /* refused: the token index of the instruction */
    vgpu98_prog_info_t info;    /* what the GPU's check found (stages sampled, coordinates, inputs, outputs) */
} xl_out_t;

/* A pixel shader 1.0-1.4 (tok[0] its version token, n dwords): 1 with *o
   the program, `range` (the bits of a float 1-8, PROGRAM's range) its
   registers' bound; 0 refused, o->why and o->at saying why and where. */
int         xl_pixel(const uint32_t *tok, uint32_t n, uint32_t range, xl_out_t *o);
/* A vertex shader 1.0-1.1's function, likewise. */
int         xl_vertex(const uint32_t *tok, uint32_t n, xl_out_t *o);
/* A shader's tokens without their comments, in place: the version, then
   every instruction with its parameters, up to END (not kept). Their
   number. */
uint32_t    xl_strip(uint32_t *tok, uint32_t n);
/* XL_E_* as a few words. */
const char *xl_why(uint32_t why);

#endif /* VGPU98_XLATE_H */

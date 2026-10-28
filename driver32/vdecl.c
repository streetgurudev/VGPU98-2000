/*
 * vgpu98dx.dll: DirectX 8's vertex shader declarations as the GPU's DECLARE
 * elements (vdecl.h; spec/directx-hal.md, "The DirectX 8 interface" and
 * Shaders). The token format is DirectX 8's (the DDKs' and SDKs'
 * d3d8types.h layout, restated): each token's type in bits 29-31; STREAM
 * (1) the stream in bits 0-3, bit 28 the tessellator's; STREAMDATA (2) a
 * register load (bit 28 clear: the type in bits 16-19, the register in bits
 * 0-4) or a skip (bit 28 set: the dwords in bits 16-19); TESSELLATOR (3);
 * CONSTMEM (4) the count in bits 25-28 and the address in bits 0-6, four
 * dwords each after it; EXT (5) the count in bits 24-28; END FFFFFFFFh.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include "vid_vgpu98_gpu.h"
#include "vdecl.h"

#define VD_TOKEN_NOP        0
#define VD_TOKEN_STREAM     1
#define VD_TOKEN_STREAMDATA 2
#define VD_TOKEN_CONSTMEM   4
#define VD_TOKEN_EXT        5
#define VD_TOKEN_END        7
#define VD_END              0xFFFFFFFFu
#define VD_TYPE_SHORT4      7  /* D3DVSDT_SHORT4, the last type */
#define VD_STREAMS          16
#define VD_INPUTS           16 /* a vertex program's input registers */

/* The fixed function's input registers (D3DVSDE_*) */
#define VDE_POSITION     0
#define VDE_BLENDWEIGHT  1
#define VDE_BLENDINDICES 2
#define VDE_NORMAL       3
#define VDE_PSIZE        4
#define VDE_DIFFUSE      5
#define VDE_SPECULAR     6
#define VDE_TEXCOORD0    7
#define VDE_TEXCOORD7    14
#define VDE_POSITION2    15
#define VDE_NORMAL2      16

static uint32_t
rd32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24);
}

/* A fixed-function input register as the GPU's use and index. */
static int
reg_use(uint32_t reg, uint32_t *use, uint32_t *index)
{
    *index = 0;
    switch (reg) {
        case VDE_POSITION:
            *use = VGPU98_DU_POSITION;
            return 1;
        case VDE_BLENDWEIGHT:
            *use = VGPU98_DU_BLENDWEIGHT;
            return 1;
        case VDE_BLENDINDICES:
            *use = VGPU98_DU_BLENDINDICES;
            return 1;
        case VDE_NORMAL:
            *use = VGPU98_DU_NORMAL;
            return 1;
        case VDE_PSIZE:
            *use = VGPU98_DU_PSIZE;
            return 1;
        case VDE_DIFFUSE:
        case VDE_SPECULAR:
            *use   = VGPU98_DU_COLOR;
            *index = reg - VDE_DIFFUSE;
            return 1;
        case VDE_POSITION2:
            *use   = VGPU98_DU_POSITION;
            *index = 1;
            return 1;
        case VDE_NORMAL2:
            *use   = VGPU98_DU_NORMAL;
            *index = 1;
            return 1;
        default:
            if ((reg >= VDE_TEXCOORD0) && (reg <= VDE_TEXCOORD7)) {
                *use   = VGPU98_DU_TEXCOORD;
                *index = reg - VDE_TEXCOORD0;
                return 1;
            }
            return 0;
    }
}

/* The element types the GPU's fixed function takes for each use (spec/
   gpu-commands.md, DECLARE). */
static int
type_ok(uint32_t use, uint32_t type)
{
    switch (use) {
        case VGPU98_DU_POSITION:
        case VGPU98_DU_NORMAL:
            return type == VGPU98_DT_FLOAT3;
        case VGPU98_DU_BLENDWEIGHT:
            return (type >= VGPU98_DT_FLOAT1) && (type <= VGPU98_DT_FLOAT4);
        case VGPU98_DU_BLENDINDICES:
            return type == VGPU98_DT_UBYTE4;
        case VGPU98_DU_PSIZE:
            return type == VGPU98_DT_FLOAT1;
        case VGPU98_DU_COLOR:
            return type == VGPU98_DT_D3DCOLOR;
        case VGPU98_DU_TEXCOORD:
            return ((type >= VGPU98_DT_FLOAT1) && (type <= VGPU98_DT_FLOAT4)) || (type == VGPU98_DT_SHORT2) ||
                   (type == VGPU98_DT_SHORT4);
        default:
            return 0;
    }
}

int
vd_parse(const uint8_t *p, uint32_t bytes, int program, uint32_t *decl, uint32_t *n_out, uint32_t *streams)
{
    static const uint8_t sizes[8] = { 4, 8, 12, 16, 4, 4, 4, 8 }; /* FLOAT1-4, D3DCOLOR, UBYTE4, SHORT2, SHORT4 */
    uint32_t             offset[VD_STREAMS];
    uint32_t             seen[2] = { 0, 0 }; /* use * 8 + index, declared */
    uint32_t             inputs  = 0;        /* a program's input registers declared */
    const uint32_t       n       = bytes / 4;
    uint32_t             i       = 0;
    uint32_t             k;
    int                  stream   = -1;
    int                  position = 0; /* an untransformed position (index 0): POSITION 1 and NORMAL 1 need it */
    int                  past     = 0; /* an element past VD_STRIDE_MAX */

    *n_out   = 0;
    *streams = 0;
    for (k = 0; k < VD_STREAMS; k++)
        offset[k] = 0;
    while (i < n) {
        const uint32_t t = rd32(p + 4 * i++);

        switch (t >> 29) {
            case VD_TOKEN_NOP:
                if (t)
                    return VD_BAD;
                break;
            case VD_TOKEN_STREAM:
                if (t & 0x10000000u)
                    return VD_BAD; /* the tessellator's stream */
                stream = (int) (t & 0xf);
                break;
            case VD_TOKEN_STREAMDATA:
                if (stream < 0)
                    return VD_BAD;
                if (t & 0x10000000u)
                    offset[stream] += 4 * ((t >> 16) & 0xf); /* skip dwords */
                else {
                    const uint32_t dt   = (t >> 16) & 0xf;
                    const uint32_t type = dt + 1;
                    const uint32_t reg  = t & 0x1f;
                    uint32_t       use;
                    uint32_t       index;
                    uint32_t       key;

                    if ((dt > VD_TYPE_SHORT4) || (*n_out == VGPU98_DECL_MAX))
                        return VD_BAD;
                    if (program) {
                        if ((reg >= VD_INPUTS) || ((inputs >> reg) & 1))
                            return VD_BAD;
                        inputs |= 1u << reg;
                        use   = VGPU98_DU_INPUT;
                        index = reg;
                    } else {
                        if (!reg_use(reg, &use, &index) || !type_ok(use, type))
                            return VD_BAD;
                        key = use * 8 + index;
                        if ((seen[key / 32] >> (key % 32)) & 1)
                            return VD_BAD;
                        seen[key / 32] |= 1u << (key % 32);
                        if ((use == VGPU98_DU_POSITION) && !index)
                            position = 1;
                    }
                    past |= offset[stream] + sizes[dt] > VD_STRIDE_MAX;
                    decl[2 * *n_out]     = VGPU98_DECL(stream, type, use, index);
                    decl[2 * *n_out + 1] = offset[stream];
                    (*n_out)++;
                    *streams |= 1u << stream;
                    offset[stream] += sizes[dt];
                }
                break;
            case VD_TOKEN_CONSTMEM:
                i += 4 * ((t >> 25) & 0xf);
                break;
            case VD_TOKEN_EXT:
                i += (t >> 24) & 0x1f;
                break;
            case VD_TOKEN_END:
                if (t != VD_END)
                    return VD_BAD;
                i = n;
                break;
            default: /* TESSELLATOR, and the reserved type */
                return VD_BAD;
        }
    }
    /* The end, or the declaration ran to its size */
    if (!program && !(position && *n_out))
        return VD_BAD;
    return past ? VD_STRIDE : VD_OK;
}

uint32_t
vd_consts(const uint8_t *p, uint32_t bytes, uint32_t *out)
{
    const uint32_t n = bytes / 4;
    uint32_t       i = 0;
    uint32_t       k = 0;

    while (i < n) {
        const uint32_t t = rd32(p + 4 * i++);
        uint32_t       r;
        uint32_t       j;

        switch (t >> 29) {
            case VD_TOKEN_CONSTMEM:
                for (r = 0; (r < ((t >> 25) & 0xf)) && (n - i >= 4); r++, i += 4) {
                    if (out) {
                        out[5 * k] = (t & 0x7f) + r;
                        for (j = 0; j < 4; j++)
                            out[5 * k + 1 + j] = rd32(p + 4 * (i + j));
                    }
                    k++;
                }
                break;
            case VD_TOKEN_EXT:
                i += (t >> 24) & 0x1f;
                break;
            case VD_TOKEN_END:
                return k;
            default:
                break;
        }
    }
    return k;
}

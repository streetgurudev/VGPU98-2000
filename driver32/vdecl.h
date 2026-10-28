/*
 * vgpu98dx.dll: DirectX 8's vertex shader declarations (CREATEVERTEXSHADER's
 * declaration tokens) as the GPU's DECLARE elements (spec/directx-hal.md).
 * Plain C with no Windows types, so the suite compiles it on the host.
 * Include <stdint.h> (or the DLL's dll.h) and vid_vgpu98_gpu.h first.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_VDECL_H
#define VGPU98_VDECL_H

#define VD_STRIDE_MAX 256 /* D3DCAPS8's MaxStreamStride */

/* vd_parse's answers */
#define VD_OK     1
#define VD_BAD    0  /* tokens it does not take: see vd_parse */
#define VD_STRIDE -1 /* an element reaches past VD_STRIDE_MAX: it lies past every vertex */

/* A declaration's tokens (`bytes` of them) as DECLARE elements, two dwords
   each (the element, its offset), into decl (VGPU98_DECL_MAX of them), with
   their number and the streams they read (a bit each). Each register a
   stream loads is an element at the offset the tokens before it in its
   stream reach (SKIP counted), the GPU's type one more than DirectX 8's.
   For the fixed function (`program` 0) the register is its use: VD_BAD
   without an untransformed position, for a use twice or a type the use does
   not take. For a vertex program the register is an INPUT element's index,
   of any type: VD_BAD for one loaded twice. Either: VD_BAD for a
   tessellator stream or token (the caps offer no patches) or a malformed
   token; constants and extensions are stepped over. */
int      vd_parse(const uint8_t *p, uint32_t bytes, int program, uint32_t *decl, uint32_t *n, uint32_t *streams);
/* Its constants (CONSTMEM: a count of registers from an address, four
   dwords each): into `out` (the register, then its four values) when it is
   not NULL; how many. */
uint32_t vd_consts(const uint8_t *p, uint32_t bytes, uint32_t *out);

#endif /* VGPU98_VDECL_H */

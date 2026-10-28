/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VGPU98 command set 2.4: the Glide device's commands
 *          (PROFILE.GLIDE), which the Glide DLLs write into the GPU's
 *          command ring: sessions, Glide's state as registers of the
 *          device's own, vertices, clears, swaps, texture memory,
 *          palettes, frame buffer transfers and gamma. A Glide command
 *          the device cannot take is dropped and counted; it never stops
 *          the ring. Glide's own enumerations go with Glide's own
 *          numbers, so the values below are Glide's (its manuals' and SDK
 *          headers', read for facts).
 *
 *          Kept in the VGPU98 project as card/vid_vgpu98_glcmd.h and
 *          copied unchanged into 86Box; the Glide DLLs include the same
 *          file. The meaning of every value is in the project's
 *          spec/gpu-commands.md (command set 2.4) and spec/glide.md.
 *
 * Authors: VGPU98 Project contributors
 *
 *          Copyright 2026 VGPU98 Project contributors.
 */
#ifndef VIDEO_VGPU98_GLCMD_H
#define VIDEO_VGPU98_GLCMD_H

/* --- Opcodes (40h-4Fh; every one's first payload dword is the session) ---- */

#define VGPU98_GL_FIRST              0x40
#define VGPU98_GL_LAST               0x4f
#define VGPU98_GL_OPEN               0x40 /* session, size, flags, staging offset, staging bytes */
#define VGPU98_GL_CLOSE              0x41 /* session */
#define VGPU98_GL_STATE              0x42 /* session, first register, values */
#define VGPU98_GL_DRAW               0x43 /* session, kind, count, vertices */
#define VGPU98_GL_CLEAR              0x44 /* session, colour, alpha, depth */
#define VGPU98_GL_SWAP               0x45 /* session, interval */
#define VGPU98_GL_TEXDATA            0x46 /* session, memory, address, bytes, data */
#define VGPU98_GL_PALETTE            0x47 /* session, TMU, first | count << 16, entries */
#define VGPU98_GL_LFB_READ           0x48 /* session, buffer, x | y << 16, w | h << 16, format | flags, offset, stride */
#define VGPU98_GL_LFB_WRITE          0x49 /* the same */
#define VGPU98_GL_GAMMA              0x4a /* session, 256 entries */
/* 4Bh-4Fh: reserved (dropped). */

/* Payload lengths in dwords (the minimum for the variable ones). */
#define VGPU98_GL_LEN_OPEN           5
#define VGPU98_GL_LEN_CLOSE          1
#define VGPU98_GL_LEN_STATE          3  /* session, first, one value at least */
#define VGPU98_GL_LEN_DRAW           3  /* + count vertices of VGPU98_GL_VERTEX_DWORDS */
#define VGPU98_GL_LEN_CLEAR          4
#define VGPU98_GL_LEN_SWAP           2
#define VGPU98_GL_LEN_TEXDATA        4  /* + (bytes + 3) / 4 */
#define VGPU98_GL_LEN_PALETTE        3  /* + count */
#define VGPU98_GL_LEN_LFB            7
#define VGPU98_GL_LEN_GAMMA          257

/* The most a DLL puts in one packet: the ring's largest packet (half the
   ring in video memory, 64 KB) less room to spare. */
#define VGPU98_GL_PACKET_BYTES       0xF000

/* --- Sessions --------------------------------------------------------------- */

#define VGPU98_GL_SESSIONS           4      /* open at once on the device */
#define VGPU98_GL_MAX_SIZE           2048   /* a session's width and height at most */

/* OPEN's size: width | height << 16. Its flags: */
#define VGPU98_GL_OPEN_BUFFERS_MASK  0x03   /* colour buffers, 1-3 */
#define VGPU98_GL_OPEN_AUX           0x04   /* an aux (depth) buffer */
#define VGPU98_GL_OPEN_LOWER_LEFT    0x08   /* GR_ORIGIN_LOWER_LEFT at the start */

/* --- State registers (STATE) -------------------------------------------- */

#define VGPU98_GS_CLIP_MIN           0  /* x | y << 16, in the origin's coordinates */
#define VGPU98_GS_CLIP_MAX           1  /* x | y << 16, exclusive */
#define VGPU98_GS_RENDER_BUFFER      2  /* GR_BUFFER_FRONTBUFFER 0 or GR_BUFFER_BACKBUFFER 1 */
#define VGPU98_GS_MASKS              3  /* VGPU98_GS_MASK_* */
#define VGPU98_GS_ORIGIN             4  /* GR_ORIGIN_UPPER_LEFT 0, GR_ORIGIN_LOWER_LEFT 1 */
#define VGPU98_GS_COLOR_COMBINE      5  /* VGPU98_GS_COMBINE() */
#define VGPU98_GS_ALPHA_COMBINE      6  /* VGPU98_GS_COMBINE() */
#define VGPU98_GS_CONSTANT           7  /* the constant colour, 0xAARRGGBB */
#define VGPU98_GS_BLEND              8  /* rgb src | rgb dst << 8 | alpha src << 16 | alpha dst << 24 (GR_BLEND_*) */
#define VGPU98_GS_ALPHA_TEST         9  /* function (GR_CMP_*) | reference << 8 */
#define VGPU98_GS_ITRGB_LIGHTING     10 /* grAlphaControlsITRGBLighting: 1 or 0 */
#define VGPU98_GS_DEPTH              11 /* mode (GR_DEPTHBUFFER_*) | function (GR_CMP_*) << 8 | bias (signed 16) << 16 */
#define VGPU98_GS_FOG                12 /* GR_FOG_* mode, Glide 2's numbers */
#define VGPU98_GS_FOG_COLOR          13 /* 0x00RRGGBB */
#define VGPU98_GS_CHROMA             14 /* GR_CHROMAKEY_DISABLE 0, GR_CHROMAKEY_ENABLE 1 */
#define VGPU98_GS_CHROMA_VALUE       15 /* 0x00RRGGBB */
#define VGPU98_GS_CULL               16 /* GR_CULL_DISABLE 0, NEGATIVE 1, POSITIVE 2 */
#define VGPU98_GS_DITHER             17 /* GR_DITHER_DISABLE 0, 2x2 1, 4x4 2 */
#define VGPU98_GS_LFB_CONSTANT       18 /* grLfbConstantAlpha | grLfbConstantDepth << 16 */
#define VGPU98_GS_FOG_TABLE          20 /* 16 registers: the 64 entries, four a register, the first in the low byte */
#define VGPU98_GS_FOG_TABLE_REGS     16
#define VGPU98_GS_TMU(t)             (40 + 16 * (t)) /* each TMU's registers, t 0 or 1: */
#define VGPU98_GS_TEX_ADDRESS        0  /* the texture's start address in its memory */
#define VGPU98_GS_TEX_INFO           1  /* VGPU98_GS_TEXINFO() */
#define VGPU98_GS_TEX_COMBINE        2  /* rgb function | rgb factor << 8 | alpha function << 16 | alpha factor << 24 */
#define VGPU98_GS_TEX_INVERT         3  /* rgb 1 | alpha 2 */
#define VGPU98_GS_TEX_FILTER         4  /* min (GR_TEXTUREFILTER_*) | mag << 8 */
#define VGPU98_GS_TEX_CLAMP          5  /* s (GR_TEXTURECLAMP_*) | t << 8 */
#define VGPU98_GS_TEX_MIPMAP         6  /* mode (GR_MIPMAP_*) | trilinear (lodBlend) << 8 */
#define VGPU98_GS_TEX_LOD_BIAS       7  /* a float */
#define VGPU98_GS_TEX_DETAIL         8  /* grTexDetailControl: lod bias (signed 8) | scale << 8 | max (0-255) << 16 */
/* 9: reserved (0). */
#define VGPU98_GS_TEX_SOURCE         10 /* 1 while a texture is set on the TMU (grTexSource) */
#define VGPU98_GS_COUNT              72

#define VGPU98_GS_MASK_RGB           0x01
#define VGPU98_GS_MASK_ALPHA         0x02
#define VGPU98_GS_MASK_DEPTH         0x04

/* A combine register: function | factor << 8 | local << 16 | other << 20 | invert << 24. */
#define VGPU98_GS_COMBINE(fn, fac, loc, oth, inv) \
    ((uint32_t) (fn) | ((uint32_t) (fac) << 8) | ((uint32_t) (loc) << 16) | ((uint32_t) (oth) << 20) | ((uint32_t) (inv) << 24))
#define VGPU98_GS_COMBINE_FN(v)      ((v) & 0xffu)
#define VGPU98_GS_COMBINE_FACTOR(v)  (((v) >> 8) & 0xffu)
#define VGPU98_GS_COMBINE_LOCAL(v)   (((v) >> 16) & 0xfu)
#define VGPU98_GS_COMBINE_OTHER(v)   (((v) >> 20) & 0xfu)
#define VGPU98_GS_COMBINE_INVERT(v)  (((v) >> 24) & 1u)

/* A texture's description: its format (GR_TEXFMT_*), its smallest and
   largest levels as log2 of their long side (0 for 1 to 8 for 256: Glide
   3's numbers), its aspect as log2 of width over height (-3 to 3: Glide 3's,
   two's complement in 4 bits), its levels' split between TMUs
   (GR_MIPMAPLEVELMASK_*) and the memory it is in (VGPU98_GL_MEM_*). */
#define VGPU98_GS_TEXINFO(fmt, small, large, aspect, evenodd, mem)                                              \
    ((uint32_t) (fmt) | ((uint32_t) (small) << 8) | ((uint32_t) (large) << 12) | (((uint32_t) (aspect) & 0xfu) << 16) | \
     ((uint32_t) (evenodd) << 20) | ((uint32_t) (mem) << 24))
#define VGPU98_GS_TEXINFO_FORMAT(v)  ((v) & 0xffu)
#define VGPU98_GS_TEXINFO_SMALL(v)   (((v) >> 8) & 0xfu)
#define VGPU98_GS_TEXINFO_LARGE(v)   (((v) >> 12) & 0xfu)
#define VGPU98_GS_TEXINFO_ASPECT(v)  ((int32_t) ((((v) >> 16) & 0xfu) ^ 8u) - 8)
#define VGPU98_GS_TEXINFO_EVENODD(v) (((v) >> 20) & 0xfu)
#define VGPU98_GS_TEXINFO_MEM(v)     (((v) >> 24) & 0xfu)

/* --- Texture memory (TEXDATA, the texture registers) ----------------------- */

#define VGPU98_GL_MEM_TMU0           0
#define VGPU98_GL_MEM_TMU1           1
#define VGPU98_GL_MEM_UNIFIED        2 /* Glide 3's unified memory, both TMUs' */
#define VGPU98_GL_TMU_BYTES          0x800000  /* each TMU's own memory at most: 8 MB (a 16 MB Voodoo3 gives each about 7) */
#define VGPU98_GL_UNIFIED_BYTES      0x1000000 /* the unified memory at most */

/* --- Vertices (DRAW) -------------------------------------------------------- */

/* DRAW's kind: what its vertices make. The DLL sends triangles alone (a
   Voodoo3's Glide makes its points and lines triangles; strips, fans and
   polygons come as lists, in Glide's order); the device drops the others. */
#define VGPU98_GL_POINTS             0 /* reserved */
#define VGPU98_GL_LINES              1 /* reserved */
#define VGPU98_GL_TRIANGLES          2 /* threes */

/* A vertex: 16 floats, Glide's own values. */
#define VGPU98_GL_VERTEX_DWORDS      16
#define VGPU98_GLV_X                 0  /* screen x and y, in the origin's coordinates */
#define VGPU98_GLV_Y                 1
#define VGPU98_GLV_OOZ               2  /* 65535/z: the Z buffer's value */
#define VGPU98_GLV_OOW               3  /* 1/w: the W buffer's, fog's, and the TMUs' unless they have their own */
#define VGPU98_GLV_R                 4  /* the iterated colour, 0-255 each */
#define VGPU98_GLV_G                 5
#define VGPU98_GLV_B                 6
#define VGPU98_GLV_A                 7
#define VGPU98_GLV_SOW0              8  /* TMU0's s/w, t/w (Glide's texel space: 256 the long side) and 1/w */
#define VGPU98_GLV_TOW0              9
#define VGPU98_GLV_OOW0              10
#define VGPU98_GLV_SOW1              11 /* TMU1's */
#define VGPU98_GLV_TOW1              12
#define VGPU98_GLV_OOW1              13
#define VGPU98_GLV_FOG               14 /* Glide 3's fog coordinate (GR_PARAM_FOG_EXT), else 0 */
#define VGPU98_GLV_PAD               15

/* --- The frame buffer (LFB_READ, LFB_WRITE, CLEAR) ------------------------- */

/* Buffers, Glide's numbers. */
#define VGPU98_GL_FRONTBUFFER        0
#define VGPU98_GL_BACKBUFFER         1
#define VGPU98_GL_AUXBUFFER          2
#define VGPU98_GL_TRIPLEBUFFER       5

/* The format dword: a GR_LFBWRITEMODE_* (or, for a read, the buffer's own
   format: 565 for colour, ZA16 for the aux buffer) | flags: */
#define VGPU98_GL_LFB_FORMAT_MASK    0xff
#define VGPU98_GL_LFB_LOWER_LEFT     0x100  /* row 0 is the bottom */
#define VGPU98_GL_LFB_CHANGED        0x200  /* WRITE: only the pixels that differ from the last READ into the staging rows */
#define VGPU98_GL_LFB_PIPELINE       0x400  /* WRITE: through the pixel pipeline (taken as a plain write) */
#define VGPU98_GL_LFB_SWAP_BYTES     0x1000 /* grLfbWriteColorSwizzle's byte swap of 32-bit pixels */
#define VGPU98_GL_LFB_SWAP_WORDS     0x2000 /* and its word swap */
#define VGPU98_GL_LFB_COLOR_SHIFT    16     /* grLfbWriteColorFormat (GR_COLORFORMAT_*) of 32-bit pixels */

/* Glide's write modes (GR_LFBWRITEMODE_*). */
#define VGPU98_GL_LFB_565            0x0
#define VGPU98_GL_LFB_555            0x1
#define VGPU98_GL_LFB_1555           0x2
#define VGPU98_GL_LFB_888            0x4
#define VGPU98_GL_LFB_8888           0x5
#define VGPU98_GL_LFB_565_DEPTH      0xc
#define VGPU98_GL_LFB_555_DEPTH      0xd
#define VGPU98_GL_LFB_1555_DEPTH     0xe
#define VGPU98_GL_LFB_ZA16           0xf

/* --- Glide's enumerations, as the device reads them ------------------------ */

/* grColorCombine, grAlphaCombine, grTexCombine: functions */
#define VGPU98_GL_FN_ZERO                        0x0
#define VGPU98_GL_FN_LOCAL                       0x1
#define VGPU98_GL_FN_LOCAL_ALPHA                 0x2
#define VGPU98_GL_FN_SCALE_OTHER                 0x3
#define VGPU98_GL_FN_SCALE_OTHER_ADD_LOCAL       0x4
#define VGPU98_GL_FN_SCALE_OTHER_ADD_LOCAL_ALPHA 0x5
#define VGPU98_GL_FN_SCALE_OTHER_MINUS_LOCAL     0x6
#define VGPU98_GL_FN_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL       0x7
#define VGPU98_GL_FN_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA 0x8
#define VGPU98_GL_FN_SCALE_MINUS_LOCAL_ADD_LOCAL             0x9
#define VGPU98_GL_FN_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA       0x10
/* factors */
#define VGPU98_GL_FAC_ZERO                0x0
#define VGPU98_GL_FAC_LOCAL               0x1
#define VGPU98_GL_FAC_OTHER_ALPHA         0x2
#define VGPU98_GL_FAC_LOCAL_ALPHA         0x3
#define VGPU98_GL_FAC_TEXTURE_ALPHA       0x4 /* grTexCombine: the detail factor */
#define VGPU98_GL_FAC_TEXTURE_RGB         0x5 /* grTexCombine: the LOD fraction */
#define VGPU98_GL_FAC_ONE                 0x8
#define VGPU98_GL_FAC_ONE_MINUS_LOCAL     0x9
#define VGPU98_GL_FAC_ONE_MINUS_OTHER_ALPHA 0xa
#define VGPU98_GL_FAC_ONE_MINUS_LOCAL_ALPHA 0xb
#define VGPU98_GL_FAC_ONE_MINUS_TEXTURE_ALPHA 0xc /* grTexCombine: one minus the detail factor */
#define VGPU98_GL_FAC_ONE_MINUS_TEXTURE_RGB   0xd /* grTexCombine: one minus the LOD fraction */
/* local and other */
#define VGPU98_GL_LOCAL_ITERATED          0x0
#define VGPU98_GL_LOCAL_CONSTANT          0x1
#define VGPU98_GL_LOCAL_DEPTH             0x2 /* grAlphaCombine only */
#define VGPU98_GL_OTHER_ITERATED          0x0
#define VGPU98_GL_OTHER_TEXTURE           0x1
#define VGPU98_GL_OTHER_CONSTANT          0x2

/* grAlphaBlendFunction */
#define VGPU98_GL_BLEND_ZERO              0x0
#define VGPU98_GL_BLEND_SRC_ALPHA         0x1
#define VGPU98_GL_BLEND_SRC_COLOR         0x2 /* the source colour as a destination factor; as a source factor, the destination's (DST_COLOR) */
#define VGPU98_GL_BLEND_DST_ALPHA         0x3
#define VGPU98_GL_BLEND_ONE               0x4
#define VGPU98_GL_BLEND_ONE_MINUS_SRC_ALPHA 0x5
#define VGPU98_GL_BLEND_ONE_MINUS_SRC_COLOR 0x6 /* the same, one minus it (as a source factor ONE_MINUS_DST_COLOR) */
#define VGPU98_GL_BLEND_ONE_MINUS_DST_ALPHA 0x7
#define VGPU98_GL_BLEND_ALPHA_SATURATE    0xf /* as a destination factor: PREFOG_COLOR */

/* Compare functions (GR_CMP_*) */
#define VGPU98_GL_CMP_NEVER               0
#define VGPU98_GL_CMP_LESS                1
#define VGPU98_GL_CMP_EQUAL               2
#define VGPU98_GL_CMP_LEQUAL              3
#define VGPU98_GL_CMP_GREATER             4
#define VGPU98_GL_CMP_NOTEQUAL            5
#define VGPU98_GL_CMP_GEQUAL              6
#define VGPU98_GL_CMP_ALWAYS              7

/* grDepthBufferMode */
#define VGPU98_GL_DEPTH_DISABLE           0
#define VGPU98_GL_DEPTH_ZBUFFER           1
#define VGPU98_GL_DEPTH_WBUFFER           2
#define VGPU98_GL_DEPTH_ZBUFFER_TO_BIAS   3
#define VGPU98_GL_DEPTH_WBUFFER_TO_BIAS   4

/* Texture formats (GR_TEXFMT_*) */
#define VGPU98_GL_TEX_RGB_332             0x0
#define VGPU98_GL_TEX_YIQ_422             0x1
#define VGPU98_GL_TEX_ALPHA_8             0x2
#define VGPU98_GL_TEX_INTENSITY_8         0x3
#define VGPU98_GL_TEX_ALPHA_INTENSITY_44  0x4
#define VGPU98_GL_TEX_P_8                 0x5
#define VGPU98_GL_TEX_ARGB_8332           0x8
#define VGPU98_GL_TEX_AYIQ_8422           0x9
#define VGPU98_GL_TEX_RGB_565             0xa
#define VGPU98_GL_TEX_ARGB_1555           0xb
#define VGPU98_GL_TEX_ARGB_4444           0xc
#define VGPU98_GL_TEX_ALPHA_INTENSITY_88  0xd
#define VGPU98_GL_TEX_AP_88               0xe

#endif /* VIDEO_VGPU98_GLCMD_H */

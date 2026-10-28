/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VGPU98 command set 2.3: the packets the driver writes into
 *          the GPU's command ring, their flags, the surface formats, the
 *          vertex declaration's types and uses, and the render state
 *          numbers. Command set 2.1 adds DirectX 8's fixed function to
 *          2.0 (PROFILE.FIXED8): its values are marked "2.1", and a GPU
 *          without that bit refuses them. Command set 2.2 adds programs
 *          (PROFILE.PROGRAMS), marked "2.2"; their instruction format is
 *          vid_vgpu98_prog.h. Command set 2.3 adds DOWNLOAD
 *          (PROFILE.DOWNLOAD), marked "2.3".
 *
 *          Kept in the VGPU98 project as card/vid_vgpu98_gpu.h and
 *          copied unchanged into 86Box; the Windows 98 driver DLL
 *          includes the same file. The meaning of every value is in
 *          the project's spec/gpu-commands.md and spec/gpu-formats.md.
 *
 * Authors: VGPU98 Project contributors
 *
 *          Copyright 2026 VGPU98 Project contributors.
 */
#ifndef VIDEO_VGPU98_GPU_H
#define VIDEO_VGPU98_GPU_H

/* --- Packets ------------------------------------------------------------ */

/* Header: opcode in bits 7:0, payload length in dwords in bits 23:8. */
#define VGPU98_PKT(op, len)          ((uint32_t) (op) | ((uint32_t) (len) << 8))
#define VGPU98_PKT_OP(h)             ((h) & 0xffu)
#define VGPU98_PKT_LEN(h)            (((h) >> 8) & 0xffffu)
#define VGPU98_PKT_MAX_LEN           0xffffu

#define VGPU98_OP_NOP                0x00
#define VGPU98_OP_FENCE              0x01 /* value */
#define VGPU98_OP_SYNC               0x02 /* offset, length */
#define VGPU98_OP_FLIP               0x03 /* start, flags */
#define VGPU98_OP_DISCARD            0x04 /* offset, length */
#define VGPU98_OP_FILL               0x08 /* SURF dst, RECT, value */
#define VGPU98_OP_BLT                0x09 /* SURF src, RECT, SURF dst, RECT, flags, srckey, dstkey */
#define VGPU98_OP_UPLOAD             0x0a /* SURF dst, RECT, source, pitch, address, data */
#define VGPU98_OP_DOWNLOAD           0x0b /* 2.3: SURF src, RECT, pitch, address */
#define VGPU98_OP_TARGET             0x10 /* SURF colour, SURF depth */
#define VGPU98_OP_VIEWPORT           0x11 /* x, y, width, height */
#define VGPU98_OP_WRANGE             0x12 /* wnear, wfar (floats) */
#define VGPU98_OP_STATE              0x13 /* id, value, ... */
#define VGPU98_OP_TEXTURE            0x14 /* control, size, format, colorkey, then offset, pitch per level per face */
#define VGPU98_OP_PALETTE            0x15 /* stage, first | count << 16, colours */
#define VGPU98_OP_CLEAR              0x18 /* flags, colour, depth, stencil, count, rectangles */
#define VGPU98_OP_ZRANGE             0x19 /* minz, maxz (floats) */
#define VGPU98_OP_DECLARE            0x1a /* count, then two dwords per element */
#define VGPU98_OP_STREAM             0x1b /* stream, source, address, size, stride, version */
#define VGPU98_OP_INDICES            0x1c /* format, address, size, version */
#define VGPU98_OP_INLINE             0x1d /* offset, data */
#define VGPU98_OP_DRAW               0x1e /* control, start, count, base, edge flags */
#define VGPU98_OP_TRANSFORM          0x1f /* which, 16 floats */
#define VGPU98_OP_LIGHT              0x20 /* slot, D3DLIGHT7 (26 dwords) */
#define VGPU98_OP_LIGHTENABLE        0x21 /* mask */
#define VGPU98_OP_MATERIAL           0x22 /* D3DMATERIAL7 (17 dwords) */
#define VGPU98_OP_CLIPPLANE          0x23 /* index, a, b, c, d */
#define VGPU98_OP_PROGRAM            0x30 /* 2.2: id, kind, range, instructions (vid_vgpu98_prog.h) */
#define VGPU98_OP_BIND               0x31 /* 2.2: vertex id, pixel id */
#define VGPU98_OP_CONSTANTS          0x32 /* 2.2: kind, first, four floats a register */
/* 33h-3Fh: reserved. */

/* Fixed payload lengths in dwords (the minimum for the variable ones). */
#define VGPU98_LEN_FENCE             1
#define VGPU98_LEN_SYNC              2
#define VGPU98_LEN_FLIP              2
#define VGPU98_LEN_DISCARD           2
#define VGPU98_LEN_FILL              6
#define VGPU98_LEN_BLT               13
#define VGPU98_LEN_UPLOAD            8
#define VGPU98_LEN_DOWNLOAD          7 /* 2.3 */
#define VGPU98_LEN_TARGET            6
#define VGPU98_LEN_VIEWPORT          4
#define VGPU98_LEN_WRANGE            2
#define VGPU98_LEN_TEXTURE           4
#define VGPU98_LEN_PALETTE           2
#define VGPU98_LEN_CLEAR             5
#define VGPU98_LEN_ZRANGE            2
#define VGPU98_LEN_DECLARE           1
#define VGPU98_LEN_STREAM            6
#define VGPU98_LEN_INDICES           4
#define VGPU98_LEN_INLINE            1
#define VGPU98_LEN_DRAW              4
#define VGPU98_LEN_TRANSFORM         17
#define VGPU98_LEN_LIGHT             27
#define VGPU98_LEN_LIGHTENABLE       1
#define VGPU98_LEN_MATERIAL          17
#define VGPU98_LEN_CLIPPLANE         5
#define VGPU98_LEN_PROGRAM           3 /* 2.2: 3 + 4 per instruction */
#define VGPU98_LEN_BIND              2
#define VGPU98_LEN_CONSTANTS         6 /* 2.2: 2 + 4 per register, one at least */

/* Audit notes: with the texture audit, a NOP
   whose payload is one of these is the DirectX driver's note to the GPU.
   Its last dword checks the ones before it (VGPU98_NOTE_HASH over them),
   and the GPU takes notes in increasing sequence only: a NOP padding the
   ring to its end may hold an old one. A GPU without the audit skips them,
   as it skips every NOP's payload. */
#define VGPU98_NOTE_VSUM             0x4D555356 /* 'VSUM': tag, sequence, stream, offset, length, stride, lock, 8 sums, check */
#define VGPU98_NOTE_PROC             0x434F5250 /* 'PROC': tag, sequence, 16 characters of name in 4 dwords, check */
#define VGPU98_LEN_NOTE_VSUM         16
#define VGPU98_LEN_NOTE_PROC         7
#define VGPU98_NOTE_VSUM_PARTS       8          /* the window in this many vertex ranges, a sum each */
#define VGPU98_NOTE_VSUM_MAX         0x10000    /* bytes of a window at most */
/* VSUM's stream word: the stream, and the buffer's audit id (the DirectX
   driver's for the buffer, 0 for none), which a rename keeps. */
#define VGPU98_NOTE_VSUM_STREAM      0x000000FF
#define VGPU98_NOTE_VSUM_INDEX       0xFF       /* the stream: the index buffer (stride, the index size; offset from INDICES' address) */
#define VGPU98_NOTE_VSUM_BUF_SHIFT   8
/* The sums: FNV-1a over little-endian dwords, then each byte past the last
   whole dword as a dword of its own; from VGPU98_NOTE_HASH_SEED. */
#define VGPU98_NOTE_HASH_SEED        0x811C9DC5
#define VGPU98_NOTE_HASH_PRIME       0x01000193
/* VSUM's lock word: the buffer's Locks the DirectX driver gave since the
   buffer's last note (those after a draw's note come in the next one: a
   write that changed a draw's window shows there). */
#define VGPU98_NOTE_LOCK_FLAGS       0x0000FFFF /* their DDLOCK flags together (WAIT 1, READONLY 10h, NOOVERWRITE 1000h, DISCARDCONTENTS 2000h) */
#define VGPU98_NOTE_LOCK_SEEN        0x00010000 /* one at least */
#define VGPU98_NOTE_LOCK_UNWAITED    0x00020000 /* one given while a draw that read the buffer was pending, without waiting or new memory (not READONLY) */
#define VGPU98_NOTE_LOCK_WAITED      0x00040000 /* one waited for such a draw */
#define VGPU98_NOTE_LOCK_RENAMED     0x00080000 /* one gave the buffer new memory (DISCARDCONTENTS, or a copy) */
#define VGPU98_NOTE_LOCK_OPEN        0x00100000 /* a Lock was still open (no Unlock) when the draw was sent */
#define VGPU98_NOTE_LOCK_IDLE        0x00200000 /* one given with no draw that read the buffer pending */
#define VGPU98_NOTE_LOCK_NOOW_BUSY   0x00400000 /* an UNWAITED one was a NOOVERWRITE */
#define VGPU98_NOTE_LOCK_COUNT       0xFF000000 /* how many (255 at most) */
#define VGPU98_NOTE_LOCK_COUNT_SHIFT 24

/* ERROR_CODE */
#define VGPU98_ERR_OPCODE            1
#define VGPU98_ERR_LENGTH            2
#define VGPU98_ERR_ARGUMENT          3
#define VGPU98_ERR_RING_END          4
#define VGPU98_ERR_PROFILE           5
#define VGPU98_ERR_TAIL              6
#define VGPU98_ERR_UNMAPPED          7 /* a GPU address the page table does not map */
#define VGPU98_ERR_BAD_PAGE          8 /* a page table entry outside guest RAM, or in the adapter area */

/* Surface descriptor (3 dwords) and rectangle (2 dwords) */
#define VGPU98_SURF_SIZE(w, h)       ((uint32_t) (w) | ((uint32_t) (h) << 16))
#define VGPU98_SURF_PITCH(p, fmt)    ((uint32_t) (p) | ((uint32_t) (fmt) << 16))
#define VGPU98_XY(x, y)              ((uint32_t) (x) | ((uint32_t) (y) << 16))

/* FLIP flags */
#define VGPU98_FLIP_IMMEDIATE        0x01
#define VGPU98_FLIP_INTERVAL_SHIFT   1
#define VGPU98_FLIP_INTERVAL_MASK    0x0e

/* BLT flags */
#define VGPU98_BLT_SRCKEY            0x01
#define VGPU98_BLT_DSTKEY            0x02
#define VGPU98_BLT_MIRROR_X          0x04
#define VGPU98_BLT_MIRROR_Y          0x08

/* UPLOAD source */
#define VGPU98_UPLOAD_GPUVM          0x01

/* TEXTURE control */
#define VGPU98_TEX_STAGE_MASK        0x07
#define VGPU98_TEX_COLORKEY          0x100
#define VGPU98_TEX_PALETTE_ALPHA     0x200
#define VGPU98_TEX_TYPE_SHIFT        12
#define VGPU98_TEX_TYPE_MASK         0x3000
#define VGPU98_TEX_TYPE_2D           0
#define VGPU98_TEX_TYPE_CUBE         1
#define VGPU98_TEX_TYPE_VOLUME       2 /* 2.1: a volume texture */
#define VGPU98_TEX_MAX_LEVELS        12
#define VGPU98_TEX_MAX_SIZE          2048
#define VGPU98_CUBE_FACES            6
#define VGPU98_TEX_VOLUME_MAX        256 /* 2.1: a volume's width, height and depth at most */
#define VGPU98_TEX_VOLUME_LEVELS     9   /* 2.1: its levels at most (256 down to 1) */
#define VGPU98_TEX_DEPTH_SHIFT       16  /* 2.1: a volume's depth, in the format dword's bits 31:16 */

/* The vertex declaration (DECLARE): element dword 0 */
#define VGPU98_DECL(stream, type, use, index) \
    ((uint32_t) (stream) | ((uint32_t) (type) << 8) | ((uint32_t) (use) << 16) | ((uint32_t) (index) << 24))
#define VGPU98_DECL_STREAM(d)        ((d) & 0x0fu)
#define VGPU98_DECL_TYPE(d)          (((d) >> 8) & 0xffu)
#define VGPU98_DECL_USE(d)           (((d) >> 16) & 0xffu)
#define VGPU98_DECL_INDEX(d)         (((d) >> 24) & 0xffu)
#define VGPU98_DECL_MAX              32
#define VGPU98_MAX_STREAMS           16
#define VGPU98_MAX_STRIDE            1024

/* Element types */
#define VGPU98_DT_FLOAT1             1
#define VGPU98_DT_FLOAT2             2
#define VGPU98_DT_FLOAT3             3
#define VGPU98_DT_FLOAT4             4
#define VGPU98_DT_D3DCOLOR           5
#define VGPU98_DT_UBYTE4             6 /* 2.1: four unsigned bytes, 0-255 */
#define VGPU98_DT_SHORT2             7 /* 2.1: two signed 16-bit integers */
#define VGPU98_DT_SHORT4             8 /* 2.1: four */
#define VGPU98_DT_LAST7              VGPU98_DT_D3DCOLOR
#define VGPU98_DT_LAST8              VGPU98_DT_SHORT4

/* Element uses */
#define VGPU98_DU_POSITION           0 /* untransformed (x, y, z) */
#define VGPU98_DU_POSITIONT          1 /* pre-transformed (x, y, z, rhw) */
#define VGPU98_DU_BLENDWEIGHT        2
#define VGPU98_DU_NORMAL             3
#define VGPU98_DU_COLOR              4 /* index 0 diffuse, 1 specular */
#define VGPU98_DU_TEXCOORD           5 /* index 0-7 */
#define VGPU98_DU_PSIZE              6 /* 2.1: the point size */
#define VGPU98_DU_BLENDINDICES       7 /* 2.1: the matrix indices of indexed vertex blending */
#define VGPU98_DU_INPUT              8 /* 2.2: a vertex program's input register, index 0-15 */
#define VGPU98_DU_LAST7              VGPU98_DU_TEXCOORD
#define VGPU98_DU_LAST8              VGPU98_DU_BLENDINDICES
#define VGPU98_DU_LAST22             VGPU98_DU_INPUT

/* STREAM and INDICES sources */
#define VGPU98_SRC_INLINE            0
#define VGPU98_SRC_GPUVM             1
#define VGPU98_IDX_SIZE_MASK         0xff
#define VGPU98_IDX_GPUVM             0x100
#define VGPU98_INLINE_BYTES          (16u << 20)

/* DRAW control */
#define VGPU98_PRIM_POINTS           1
#define VGPU98_PRIM_LINES            2
#define VGPU98_PRIM_LINE_STRIP       3
#define VGPU98_PRIM_TRIANGLES        4
#define VGPU98_PRIM_TRIANGLE_STRIP   5
#define VGPU98_PRIM_TRIANGLE_FAN     6
#define VGPU98_DRAW_INDEXED          0x100
#define VGPU98_DRAW_EDGES            0x200
#define VGPU98_DRAW_MAX_COUNT        0x100000 /* vertices or indices one DRAW uses at most */
#define VGPU98_VERTEX_LIMIT          0xffffff /* vertex numbers from here up, and below 0, read as 0 */

/* TRANSFORM matrices */
#define VGPU98_XFORM_WORLD           0 /* 0-3: world matrices 0-3 */
#define VGPU98_XFORM_VIEW            4
#define VGPU98_XFORM_PROJECTION      5
#define VGPU98_XFORM_TEXTURE         8 /* 8-15: the texture matrices of stages 0-7 */
#define VGPU98_XFORM_COUNT           16
/* The slots TRANSFORM takes (6 and 7 are reserved): the driver sends these
   and no others, the GPU refuses the others. */
#define VGPU98_XFORM_VALID(n) \
    (((uint32_t) (n) <= VGPU98_XFORM_PROJECTION) || \
     (((uint32_t) (n) >= VGPU98_XFORM_TEXTURE) && ((uint32_t) (n) < VGPU98_XFORM_COUNT)))
/* 2.1: the world matrix palette of indexed vertex blending, world matrices
   0-255 by slot 256 + n (its matrices 0-3 are TRANSFORM's world matrices
   0-3: the same four). */
#define VGPU98_XFORM_PALETTE         256
#define VGPU98_PALETTE_SIZE          256
#define VGPU98_XFORM_IN_PALETTE(n) \
    (((uint32_t) (n) >= VGPU98_XFORM_PALETTE) && ((uint32_t) (n) < VGPU98_XFORM_PALETTE + VGPU98_PALETTE_SIZE))

/* LIGHT */
#define VGPU98_MAX_LIGHTS            8
#define VGPU98_LIGHT_POINT           1
#define VGPU98_LIGHT_SPOT            2
#define VGPU98_LIGHT_DIRECTIONAL     3
#define VGPU98_MAX_CLIPPLANES        6

/* CLEAR flags */
#define VGPU98_CLEAR_COLOR           0x01
#define VGPU98_CLEAR_DEPTH           0x02
#define VGPU98_CLEAR_STENCIL         0x04

/* --- Surface formats (spec/gpu-formats.md) ------------------------------ */

#define VGPU98_FMT_NONE              0x00
#define VGPU98_FMT_R5G6B5            0x01
#define VGPU98_FMT_X1R5G5B5          0x02
#define VGPU98_FMT_A1R5G5B5          0x03
#define VGPU98_FMT_A4R4G4B4          0x04
#define VGPU98_FMT_X8R8G8B8          0x05
#define VGPU98_FMT_A8R8G8B8          0x06
#define VGPU98_FMT_P8                0x07
#define VGPU98_FMT_L8                0x08
#define VGPU98_FMT_A8L8              0x09
#define VGPU98_FMT_V8U8              0x0a
#define VGPU98_FMT_L6V5U5            0x0b
#define VGPU98_FMT_X8L8V8U8          0x0c
#define VGPU98_FMT_A8                0x0d /* 2.1 */
#define VGPU98_FMT_X4R4G4B4          0x0e /* 2.1 */
#define VGPU98_FMT_A4L4              0x0f /* 2.1 */
#define VGPU98_FMT_DXT1              0x10
#define VGPU98_FMT_DXT2              0x11
#define VGPU98_FMT_DXT3              0x12
#define VGPU98_FMT_DXT4              0x13
#define VGPU98_FMT_DXT5              0x14
#define VGPU98_FMT_Q8W8V8U8          0x18 /* 2.1 */
#define VGPU98_FMT_V16U16            0x19 /* 2.2: V 31:16, U 15:0, signed */
#define VGPU98_FMT_G16R16            0x1a /* 2.2: G 31:16, R 15:0 */
#define VGPU98_FMT_A2W10V10U10       0x1b /* 2.2: A 31:30 (unsigned), W 29:20, V 19:10, U 9:0 (signed) */
#define VGPU98_FMT_D16               0x20
#define VGPU98_FMT_D24X8             0x21
#define VGPU98_FMT_D24S8             0x22

/* --- Render states (spec/gpu-commands.md): Direct3D 7's numbers --------- */

#define VGPU98_RS_TEXTUREPERSPECTIVE 4
#define VGPU98_RS_ZENABLE            7
#define VGPU98_RS_FILLMODE           8
#define VGPU98_RS_SHADEMODE          9
#define VGPU98_RS_ZWRITEENABLE       14
#define VGPU98_RS_ALPHATESTENABLE    15
#define VGPU98_RS_LASTPIXEL          16
#define VGPU98_RS_SRCBLEND           19
#define VGPU98_RS_DESTBLEND          20
#define VGPU98_RS_CULLMODE           22
#define VGPU98_RS_ZFUNC              23
#define VGPU98_RS_ALPHAREF           24
#define VGPU98_RS_ALPHAFUNC          25
#define VGPU98_RS_DITHERENABLE       26
#define VGPU98_RS_ALPHABLENDENABLE   27
#define VGPU98_RS_FOGENABLE          28
#define VGPU98_RS_SPECULARENABLE     29
#define VGPU98_RS_FOGCOLOR           34
#define VGPU98_RS_FOGTABLEMODE       35
#define VGPU98_RS_FOGTABLESTART      36 /* FOGSTART: table and vertex fog */
#define VGPU98_RS_FOGTABLEEND        37 /* FOGEND */
#define VGPU98_RS_FOGTABLEDENSITY    38 /* FOGDENSITY */
#define VGPU98_RS_COLORKEYENABLE     41
#define VGPU98_RS_ZBIAS              47
#define VGPU98_RS_RANGEFOGENABLE     48
#define VGPU98_RS_STENCILENABLE      52
#define VGPU98_RS_STENCILFAIL        53
#define VGPU98_RS_STENCILZFAIL       54
#define VGPU98_RS_STENCILPASS        55
#define VGPU98_RS_STENCILFUNC        56
#define VGPU98_RS_STENCILREF         57
#define VGPU98_RS_STENCILMASK        58
#define VGPU98_RS_STENCILWRITEMASK   59
#define VGPU98_RS_TEXTUREFACTOR      60
#define VGPU98_RS_WRAP0              128 /* WRAP0-WRAP7: 128-135 */
#define VGPU98_RS_CLIPPING           136
#define VGPU98_RS_LIGHTING           137
#define VGPU98_RS_EXTENTS            138
#define VGPU98_RS_AMBIENT            139
#define VGPU98_RS_FOGVERTEXMODE      140
#define VGPU98_RS_COLORVERTEX        141
#define VGPU98_RS_LOCALVIEWER        142
#define VGPU98_RS_NORMALIZENORMALS   143
#define VGPU98_RS_COLORKEYBLENDENABLE 144
#define VGPU98_RS_DIFFUSEMATERIALSOURCE  145
#define VGPU98_RS_SPECULARMATERIALSOURCE 146
#define VGPU98_RS_AMBIENTMATERIALSOURCE  147
#define VGPU98_RS_EMISSIVEMATERIALSOURCE 148
#define VGPU98_RS_VERTEXBLEND        151
#define VGPU98_RS_CLIPPLANEENABLE    152
#define VGPU98_RS_GLOBAL_COUNT       153 /* command set 2.0's global ids are below this */
/* 2.1: DirectX 8's fixed-function states */
#define VGPU98_RS_POINTSIZE          154 /* float */
#define VGPU98_RS_POINTSIZE_MIN      155 /* float */
#define VGPU98_RS_POINTSPRITEENABLE  156
#define VGPU98_RS_POINTSCALEENABLE   157
#define VGPU98_RS_POINTSCALE_A       158 /* float */
#define VGPU98_RS_POINTSCALE_B       159 /* float */
#define VGPU98_RS_POINTSCALE_C       160 /* float */
#define VGPU98_RS_MULTISAMPLEANTIALIAS 161 /* accepted, no effect: single-sample targets */
#define VGPU98_RS_MULTISAMPLEMASK    162 /* likewise */
#define VGPU98_RS_PATCHEDGESTYLE     163 /* accepted, no effect: no patches */
#define VGPU98_RS_PATCHSEGMENTS      164 /* likewise */
#define VGPU98_RS_POINTSIZE_MAX      166 /* float */
#define VGPU98_RS_INDEXEDVERTEXBLENDENABLE 167
#define VGPU98_RS_COLORWRITEENABLE   168 /* VGPU98_CWE_* */
#define VGPU98_RS_TWEENFACTOR        170 /* float */
#define VGPU98_RS_BLENDOP            171 /* VGPU98_BLENDOP_* */
#define VGPU98_RS_POSITIONORDER      172 /* accepted, no effect: no patches */
#define VGPU98_RS_NORMALORDER        173 /* likewise */
#define VGPU98_RS_GLOBAL_COUNT8      174 /* command set 2.1's global ids are below this */
#define VGPU98_POINT_SIZE_MAX        256 /* 2.1: the largest point size, in pixels */

/* VERTEXBLEND: 0 off, 1-3 weights; 2.1 adds these */
#define VGPU98_VBF_TWEENING          255 /* tweening between two positions and normals */
#define VGPU98_VBF_0WEIGHTS          256 /* indexed: one matrix by index, weight 1 */

/* BLENDOP (2.1) */
#define VGPU98_BLENDOP_ADD           1
#define VGPU98_BLENDOP_SUBTRACT      2 /* source - destination */
#define VGPU98_BLENDOP_REVSUBTRACT   3 /* destination - source */
#define VGPU98_BLENDOP_MIN           4
#define VGPU98_BLENDOP_MAX           5

/* COLORWRITEENABLE (2.1) */
#define VGPU98_CWE_RED               0x1
#define VGPU98_CWE_GREEN             0x2
#define VGPU98_CWE_BLUE              0x4
#define VGPU98_CWE_ALPHA             0x8

/* Material sources */
#define VGPU98_MCS_MATERIAL          0
#define VGPU98_MCS_COLOR1            1
#define VGPU98_MCS_COLOR2            2

/* Texture stage states: id = VGPU98_TSS(stage, n). */
#define VGPU98_TSS(stage, n)         (0x100u + 0x20u * (uint32_t) (stage) + (uint32_t) (n))
#define VGPU98_TSS_FIRST             0x100u
#define VGPU98_TSS_LAST              0x1ffu
#define VGPU98_TSS_COLOROP           1
#define VGPU98_TSS_COLORARG1         2
#define VGPU98_TSS_COLORARG2         3
#define VGPU98_TSS_ALPHAOP           4
#define VGPU98_TSS_ALPHAARG1         5
#define VGPU98_TSS_ALPHAARG2         6
#define VGPU98_TSS_BUMPENVMAT00      7
#define VGPU98_TSS_BUMPENVMAT01      8
#define VGPU98_TSS_BUMPENVMAT10      9
#define VGPU98_TSS_BUMPENVMAT11      10
#define VGPU98_TSS_TEXCOORDINDEX     11
#define VGPU98_TSS_ADDRESSU          13
#define VGPU98_TSS_ADDRESSV          14
#define VGPU98_TSS_BORDERCOLOR       15
#define VGPU98_TSS_MAGFILTER         16
#define VGPU98_TSS_MINFILTER         17
#define VGPU98_TSS_MIPFILTER         18
#define VGPU98_TSS_MIPMAPLODBIAS     19
#define VGPU98_TSS_MAXMIPLEVEL       20
#define VGPU98_TSS_MAXANISOTROPY     21
#define VGPU98_TSS_BUMPENVLSCALE     22
#define VGPU98_TSS_BUMPENVLOFFSET    23
#define VGPU98_TSS_TEXTURETRANSFORMFLAGS 24
#define VGPU98_TSS_COUNT             25 /* command set 2.0's stage states are below this */
#define VGPU98_TSS_ADDRESSW          25 /* 2.1: a volume's third coordinate */
#define VGPU98_TSS_COLORARG0         26 /* 2.1: the third argument of MULTIPLYADD and LERP */
#define VGPU98_TSS_ALPHAARG0         27
#define VGPU98_TSS_RESULTARG         28 /* 2.1: CURRENT or TEMP */
#define VGPU98_TSS_COUNT8            29 /* command set 2.1's are below this */
#define VGPU98_MAX_STAGES            8
#define VGPU98_MAX_TEXSETS           8

/* TEXCOORDINDEX: the set in bits 2:0, generation in bits 17:16 */
#define VGPU98_TCI_SET_MASK          0x7
#define VGPU98_TCI_GEN_SHIFT         16
#define VGPU98_TCI_GEN_MASK          0x30000
#define VGPU98_TCI_PASSTHRU          0
#define VGPU98_TCI_NORMAL            1
#define VGPU98_TCI_POSITION          2
#define VGPU98_TCI_REFLECTION        3

/* TEXTURETRANSFORMFLAGS */
#define VGPU98_TTFF_COUNT_MASK       0xff
#define VGPU98_TTFF_PROJECTED        0x100

/* Texture stage arguments */
#define VGPU98_TA_DIFFUSE            0
#define VGPU98_TA_CURRENT            1
#define VGPU98_TA_TEXTURE            2
#define VGPU98_TA_TFACTOR            3
#define VGPU98_TA_SPECULAR           4
#define VGPU98_TA_TEMP               5 /* 2.1: the temporary register */
#define VGPU98_TA_SELECTMASK         0x0f
#define VGPU98_TA_COMPLEMENT         0x10
#define VGPU98_TA_ALPHAREPLICATE     0x20

/* Texture operations */
#define VGPU98_TOP_DISABLE                   1
#define VGPU98_TOP_SELECTARG1                2
#define VGPU98_TOP_SELECTARG2                3
#define VGPU98_TOP_MODULATE                  4
#define VGPU98_TOP_MODULATE2X                5
#define VGPU98_TOP_MODULATE4X                6
#define VGPU98_TOP_ADD                       7
#define VGPU98_TOP_ADDSIGNED                 8
#define VGPU98_TOP_ADDSIGNED2X               9
#define VGPU98_TOP_SUBTRACT                  10
#define VGPU98_TOP_ADDSMOOTH                 11
#define VGPU98_TOP_BLENDDIFFUSEALPHA         12
#define VGPU98_TOP_BLENDTEXTUREALPHA         13
#define VGPU98_TOP_BLENDFACTORALPHA          14
#define VGPU98_TOP_BLENDTEXTUREALPHAPM       15
#define VGPU98_TOP_BLENDCURRENTALPHA         16
#define VGPU98_TOP_PREMODULATE               17
#define VGPU98_TOP_MODULATEALPHA_ADDCOLOR    18
#define VGPU98_TOP_MODULATECOLOR_ADDALPHA    19
#define VGPU98_TOP_MODULATEINVALPHA_ADDCOLOR 20
#define VGPU98_TOP_MODULATEINVCOLOR_ADDALPHA 21
#define VGPU98_TOP_BUMPENVMAP                22
#define VGPU98_TOP_BUMPENVMAPLUMINANCE       23
#define VGPU98_TOP_DOTPRODUCT3               24
#define VGPU98_TOP_LAST                      24 /* command set 2.0's */
#define VGPU98_TOP_MULTIPLYADD               25 /* 2.1: A0 + A1 * A2 */
#define VGPU98_TOP_LERP                      26 /* 2.1: A0 * A1 + (1 - A0) * A2 */
#define VGPU98_TOP_LAST8                     26

#endif /* VIDEO_VGPU98_GPU_H */

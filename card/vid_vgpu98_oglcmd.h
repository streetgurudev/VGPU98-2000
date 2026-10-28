/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VGPU98 command set 2.7: the OpenGL device's commands
 *          (PROFILE.OGL), which the OpenGL ICD (vgpu98gl.dll) writes into
 *          the GPU's command ring: share groups, contexts and their state
 *          registers, drawables (a window's buffers), vertices, clears,
 *          swaps, textures and their palettes, pixel rectangles and
 *          read-backs (2.5: GL 1.1; 2.6: multitexture, compiled vertex
 *          arrays, paletted textures, point parameters and swap control;
 *          2.7: clamp-to-edge). A command the device cannot take is
 *          dropped and counted; it never stops the ring.
 *
 *          Kept in the VGPU98 project as card/vid_vgpu98_oglcmd.h and
 *          copied unchanged into 86Box; the ICD includes the same file.
 *          The meaning of every value is in the project's
 *          spec/gpu-commands.md (command sets 2.5 to 2.7) and
 *          spec/opengl.md.
 *
 * Authors: VGPU98 Project contributors
 *
 *          Copyright 2026 VGPU98 Project contributors.
 */
#ifndef VIDEO_VGPU98_OGLCMD_H
#define VIDEO_VGPU98_OGLCMD_H

/* --- Opcodes (50h-5Fh) ------------------------------------------------------ */

#define VGPU98_OGL_FIRST             0x50
#define VGPU98_OGL_LAST              0x5f
#define VGPU98_OGL_GROUP             0x50 /* group, op */
#define VGPU98_OGL_CONTEXT           0x51 /* context, op, group */
#define VGPU98_OGL_DRAWABLE          0x52 /* drawable, op, width | height << 16, format */
#define VGPU98_OGL_BIND              0x53 /* context, drawable (0: none) */
#define VGPU98_OGL_STATE             0x54 /* context, first register, values */
#define VGPU98_OGL_DRAW              0x55 /* context, control, vertices, indices, then the vertices and the indices */
#define VGPU98_OGL_CLEAR             0x56 /* context, mask, red, green, blue, alpha, depth (floats), stencil */
#define VGPU98_OGL_SWAP              0x57 /* drawable, flags, offset, pitch, bpp, x | y << 16, rectangles, then each */
#define VGPU98_OGL_TEXIMAGE          0x58 /* group, texture, control, size, place, base format, texels */
#define VGPU98_OGL_TEXPARAM          0x59 /* group, texture, wraps, filters, border colour (4 floats) */
#define VGPU98_OGL_TEXDELETE         0x5a /* group, texture */
#define VGPU98_OGL_PIXELS            0x5b /* context, kind, size, x, y, z, zoom x, zoom y, colour (4), texture (4), fog, data */
#define VGPU98_OGL_READ              0x5c /* context, what, x | y << 16, width | height << 16, offset, stride */
#define VGPU98_OGL_TEXREAD           0x5d /* group, texture, level, offset, stride, first row | rows << 16 */
#define VGPU98_OGL_PALETTE           0x5e /* 2.6: context, texture (0: the context's shared palette), base format, width, then the entries */
/* 5Fh: reserved (dropped). */

/* Payload lengths in dwords (the minimum for the variable ones). */
#define VGPU98_OGL_LEN_GROUP         2
#define VGPU98_OGL_LEN_CONTEXT       3
#define VGPU98_OGL_LEN_DRAWABLE      4
#define VGPU98_OGL_LEN_BIND          2
#define VGPU98_OGL_LEN_STATE         3  /* context, first, one value at least */
#define VGPU98_OGL_LEN_DRAW          4  /* + vertices * VGPU98_OGL_VERTEX_DWORDS + (indices + 1) / 2 */
#define VGPU98_OGL_LEN_CLEAR         8
#define VGPU98_OGL_LEN_SWAP          7  /* + 2 a rectangle */
#define VGPU98_OGL_LEN_TEXIMAGE      6  /* + width * height texels (DEFINE alone: none) */
#define VGPU98_OGL_LEN_TEXPARAM      8
#define VGPU98_OGL_LEN_TEXDELETE     2
#define VGPU98_OGL_LEN_PIXELS        17 /* + the data (VGPU98_OGL_PIXELS_*) */
#define VGPU98_OGL_LEN_READ          6
#define VGPU98_OGL_LEN_TEXREAD       6
#define VGPU98_OGL_LEN_PALETTE       4  /* + width entries */

/* The most the ICD puts in one packet: the ring's largest packet (half the
   ring in video memory, 64 KB) less room to spare, as Glide's. */
#define VGPU98_OGL_PACKET_BYTES      0xF000

/* --- Objects (GROUP, CONTEXT, DRAWABLE) -------------------------------------- */

/* Their IDs are the ICD's: nonzero, unique among the live objects of their
   kind. */
#define VGPU98_OGL_GROUPS            32
#define VGPU98_OGL_CONTEXTS          64
#define VGPU98_OGL_DRAWABLES         32
#define VGPU98_OGL_TEXTURES          16384 /* on the device, every group's */
#define VGPU98_OGL_MAX_SIZE          2048  /* a drawable's width and height, a texture level's (without its border) */
#define VGPU98_OGL_LEVELS            12    /* a texture's levels: 2048 down to 1 */

/* GROUP's, CONTEXT's and DRAWABLE's op. */
#define VGPU98_OGL_CLOSE             0
#define VGPU98_OGL_OPEN              1
#define VGPU98_OGL_RESIZE            2 /* DRAWABLE: a new size, its contents undefined (cleared) */

/* A drawable's format: its colour, its depth and stencil, and whether it
   has a back buffer. */
#define VGPU98_OGL_COLOR_MASK        0x0f
#define VGPU98_OGL_COLOR_565         1 /* RGB 5:6:5, as a 16-bit desktop's pixels */
#define VGPU98_OGL_COLOR_XRGB8888    2 /* XRGB 8:8:8:8, as a 32-bit desktop's (alpha reads 1) */
#define VGPU98_OGL_COLOR_ARGB8888    3 /* the same with 8 alpha bits kept */
#define VGPU98_OGL_DEPTH_SHIFT       4
#define VGPU98_OGL_DEPTH_MASK        0xf0
#define VGPU98_OGL_DEPTH_NONE        0
#define VGPU98_OGL_DEPTH_16          1 /* 16 bits, no stencil */
#define VGPU98_OGL_DEPTH_24S8        2 /* 24 bits and 8 stencil bits */
#define VGPU98_OGL_DOUBLE            0x100

/* --- State registers (STATE) ---------------------------------------------- */

/* Each a dword: a float as its bits where it says so. Matrices are GL's
   column-major order. What GL computes when a command is called (the
   modelview's inverse transpose, positions and planes in eye coordinates),
   the ICD has computed. */
#define VGPU98_OGL_R_MODELVIEW       0   /* 16 floats */
#define VGPU98_OGL_R_PROJECTION      16  /* 16 floats */
#define VGPU98_OGL_R_TEXTURE         32  /* 16 floats */
#define VGPU98_OGL_R_NORMAL          48  /* 12 floats: the modelview's inverse transpose, three columns of x, y, z, 0 */
#define VGPU98_OGL_R_ENABLES         60  /* VGPU98_OGL_EN_* */
#define VGPU98_OGL_R_LIGHTS_ON       61  /* a bit a light, 0-7 */
#define VGPU98_OGL_R_CLIPS_ON        62  /* a bit a clip plane, 0-5 */
#define VGPU98_OGL_R_SHADE           63  /* 0 flat, 1 smooth */
#define VGPU98_OGL_R_FACE            64  /* front face (0 CCW, 1 CW) | culled face << 4 (1 front, 2 back, 3 both) */
#define VGPU98_OGL_R_MODEL_AMBIENT   65  /* 4 floats */
#define VGPU98_OGL_R_MODEL_FLAGS     69  /* 1 local viewer | 2 two-sided */
#define VGPU98_OGL_R_COLOR_MATERIAL  70  /* face (1 front, 2 back, 3 both) | which << 4 (VGPU98_OGL_CM_*) */
#define VGPU98_OGL_R_MATERIAL(f)     (72 + 20 * (f)) /* f 0 front, 1 back: */
#define VGPU98_OGL_M_AMBIENT         0   /* 4 floats */
#define VGPU98_OGL_M_DIFFUSE         4   /* 4 floats */
#define VGPU98_OGL_M_SPECULAR        8   /* 4 floats */
#define VGPU98_OGL_M_EMISSION        12  /* 4 floats */
#define VGPU98_OGL_M_SHININESS       16  /* a float */
#define VGPU98_OGL_R_LIGHT(i)        (112 + 24 * (i)) /* i 0-7: */
#define VGPU98_OGL_L_AMBIENT         0   /* 4 floats */
#define VGPU98_OGL_L_DIFFUSE         4   /* 4 floats */
#define VGPU98_OGL_L_SPECULAR        8   /* 4 floats */
#define VGPU98_OGL_L_POSITION        12  /* 4 floats, eye coordinates */
#define VGPU98_OGL_L_SPOT_DIRECTION  16  /* 3 floats, eye coordinates */
#define VGPU98_OGL_L_SPOT_EXPONENT   19  /* a float */
#define VGPU98_OGL_L_SPOT_CUTOFF     20  /* a float, degrees (180: not a spotlight) */
#define VGPU98_OGL_L_ATTENUATION     21  /* 3 floats: constant, linear, quadratic */
#define VGPU98_OGL_R_TEXGEN_MODE     304 /* s | t << 4 | r << 8 | q << 12: VGPU98_OGL_TG_* (0: not generated) */
#define VGPU98_OGL_R_TEXGEN_OBJECT   305 /* 16 floats: the object planes of s, t, r and q */
#define VGPU98_OGL_R_TEXGEN_EYE      321 /* 16 floats: the eye planes, in eye coordinates */
#define VGPU98_OGL_R_CLIP_PLANE      337 /* 24 floats: six planes, in eye coordinates */
#define VGPU98_OGL_R_FOG_MODE        361 /* 0 linear, 1 exp, 2 exp2 */
#define VGPU98_OGL_R_FOG_DENSITY     362 /* a float */
#define VGPU98_OGL_R_FOG_START       363 /* a float */
#define VGPU98_OGL_R_FOG_END         364 /* a float */
#define VGPU98_OGL_R_FOG_COLOR       365 /* 4 floats */
#define VGPU98_OGL_R_TEXTURE_ID      369 /* the texture drawn with: its ID in the group, 0 for none (texturing off or incomplete) */
#define VGPU98_OGL_R_TEX_ENV_MODE    370 /* VGPU98_OGL_ENV_* */
#define VGPU98_OGL_R_TEX_ENV_COLOR   371 /* 4 floats */
#define VGPU98_OGL_R_ALPHA_FUNC      375 /* VGPU98_OGL_CMP_* */
#define VGPU98_OGL_R_ALPHA_REF       376 /* a float, 0-1 */
#define VGPU98_OGL_R_DEPTH           377 /* function (VGPU98_OGL_CMP_*) | writes << 8 */
#define VGPU98_OGL_R_DEPTH_RANGE     378 /* 2 floats: near, far */
#define VGPU98_OGL_R_STENCIL         380 /* function | reference << 8 | value mask << 16 | write mask << 24 */
#define VGPU98_OGL_R_STENCIL_OPS     381 /* fail | depth fail << 4 | pass << 8: VGPU98_OGL_SOP_* */
#define VGPU98_OGL_R_BLEND           382 /* source | destination << 8: VGPU98_OGL_BF_* */
#define VGPU98_OGL_R_LOGIC_OP        383 /* 0-15, GL's order (Vulkan's too) */
#define VGPU98_OGL_R_COLOR_MASK      384 /* red 1 | green 2 | blue 4 | alpha 8 */
#define VGPU98_OGL_R_SCISSOR         385 /* x, y, width, height: window coordinates (signed x and y) */
#define VGPU98_OGL_R_VIEWPORT        389 /* x, y, width, height */
#define VGPU98_OGL_R_POINT_SIZE      393 /* a float */
#define VGPU98_OGL_R_LINE_WIDTH      394 /* a float */
#define VGPU98_OGL_R_LINE_STIPPLE    395 /* pattern | factor << 16 */
#define VGPU98_OGL_R_POLYGON_OFFSET  396 /* 2 floats: factor, units */
#define VGPU98_OGL_R_DRAW_BUFFERS    398 /* front 1 | back 2 */
#define VGPU98_OGL_R_READ_BUFFER     399 /* 0 front, 1 back */
#define VGPU98_OGL_R_FOG_PER_PIXEL   400 /* 1: fog computed per fragment (always, here) */
#define VGPU98_OGL_R_POLYGON_STIPPLE 401 /* 32 rows of 32 bits, the bottom row first, bit 31 the leftmost pixel */
#define VGPU98_OGL_R_COUNT_25        433 /* command set 2.5's registers */
/* 2.6: texture unit 1 (ARB_multitexture), each register as unit 0's of the
   same name above; unit 0's stay where they were. */
#define VGPU98_OGL_R_UNIT1           433
#define VGPU98_OGL_U_TEXTURE         0   /* 16 floats: the unit's texture matrix */
#define VGPU98_OGL_U_TEXGEN_MODE     16  /* as VGPU98_OGL_R_TEXGEN_MODE */
#define VGPU98_OGL_U_TEXGEN_OBJECT   17  /* 16 floats */
#define VGPU98_OGL_U_TEXGEN_EYE      33  /* 16 floats, eye coordinates */
#define VGPU98_OGL_U_TEXTURE_ID      49  /* the texture the unit draws with, 0 for none */
#define VGPU98_OGL_U_TEX_ENV_MODE    50  /* VGPU98_OGL_ENV_* */
#define VGPU98_OGL_U_TEX_ENV_COLOR   51  /* 4 floats */
#define VGPU98_OGL_UNIT_REGS         55
/* 2.6: EXT_point_parameters. With R_POINT_PARAMS 1, a point's size is
   POINT_SIZE * sqrt(1 / (a + b d + c d^2)), d its distance from the eye,
   clamped to MIN and MAX; below FADE, FADE's size and its alpha times
   (size / FADE)^2; then the device's 1-64. 0: POINT_SIZE, as 2.5. */
#define VGPU98_OGL_R_POINT_PARAMS    488
#define VGPU98_OGL_R_POINT_MIN       489 /* a float */
#define VGPU98_OGL_R_POINT_MAX       490 /* a float */
#define VGPU98_OGL_R_POINT_FADE      491 /* a float: the fade threshold */
#define VGPU98_OGL_R_POINT_ATTEN     492 /* 3 floats: a, b, c */
#define VGPU98_OGL_R_COUNT           495

/* VGPU98_OGL_R_ENABLES */
#define VGPU98_OGL_EN_LIGHTING       0x00000001
#define VGPU98_OGL_EN_COLOR_MATERIAL 0x00000002
#define VGPU98_OGL_EN_NORMALIZE      0x00000004
#define VGPU98_OGL_EN_FOG            0x00000008
#define VGPU98_OGL_EN_ALPHA_TEST     0x00000010
#define VGPU98_OGL_EN_DEPTH_TEST     0x00000020
#define VGPU98_OGL_EN_STENCIL_TEST   0x00000040
#define VGPU98_OGL_EN_BLEND          0x00000080
#define VGPU98_OGL_EN_LOGIC_OP       0x00000100
#define VGPU98_OGL_EN_SCISSOR        0x00000200
#define VGPU98_OGL_EN_CULL_FACE      0x00000400
#define VGPU98_OGL_EN_OFFSET_FILL    0x00000800
#define VGPU98_OGL_EN_OFFSET_LINE    0x00001000
#define VGPU98_OGL_EN_OFFSET_POINT   0x00002000
#define VGPU98_OGL_EN_POLY_STIPPLE   0x00004000
#define VGPU98_OGL_EN_LINE_STIPPLE   0x00008000
#define VGPU98_OGL_EN_POINT_SMOOTH   0x00010000
#define VGPU98_OGL_EN_LINE_SMOOTH    0x00020000
#define VGPU98_OGL_EN_POLY_SMOOTH    0x00040000
#define VGPU98_OGL_EN_DITHER         0x00080000
#define VGPU98_OGL_EN_TEXTURE_1D     0x00100000 /* the texture drawn with is one-dimensional */
#define VGPU98_OGL_EN_TEXTURE1_1D    0x00200000 /* 2.6: unit 1's likewise */
#define VGPU98_OGL_EN_SHARED_PALETTE 0x00400000 /* 2.6: index textures look up the context's shared palette */

/* Colour material (VGPU98_OGL_R_COLOR_MATERIAL's which) */
#define VGPU98_OGL_CM_EMISSION       0
#define VGPU98_OGL_CM_AMBIENT        1
#define VGPU98_OGL_CM_DIFFUSE        2
#define VGPU98_OGL_CM_SPECULAR       3
#define VGPU98_OGL_CM_AMBIENT_DIFFUSE 4

/* Texture coordinate generation */
#define VGPU98_OGL_TG_OFF            0
#define VGPU98_OGL_TG_OBJECT         1
#define VGPU98_OGL_TG_EYE            2
#define VGPU98_OGL_TG_SPHERE         3

/* Texture environment */
#define VGPU98_OGL_ENV_MODULATE      0
#define VGPU98_OGL_ENV_DECAL         1
#define VGPU98_OGL_ENV_BLEND         2
#define VGPU98_OGL_ENV_REPLACE       3

/* Compare functions: GL's NEVER (200h) to ALWAYS (207h) less 200h */
#define VGPU98_OGL_CMP_NEVER         0
#define VGPU98_OGL_CMP_LESS          1
#define VGPU98_OGL_CMP_EQUAL         2
#define VGPU98_OGL_CMP_LEQUAL        3
#define VGPU98_OGL_CMP_GREATER       4
#define VGPU98_OGL_CMP_NOTEQUAL      5
#define VGPU98_OGL_CMP_GEQUAL        6
#define VGPU98_OGL_CMP_ALWAYS        7

/* Stencil operations */
#define VGPU98_OGL_SOP_KEEP          0
#define VGPU98_OGL_SOP_ZERO          1
#define VGPU98_OGL_SOP_REPLACE       2
#define VGPU98_OGL_SOP_INCR          3 /* clamped */
#define VGPU98_OGL_SOP_DECR          4 /* clamped */
#define VGPU98_OGL_SOP_INVERT        5

/* Blend factors */
#define VGPU98_OGL_BF_ZERO                0
#define VGPU98_OGL_BF_ONE                 1
#define VGPU98_OGL_BF_SRC_COLOR           2  /* destination factor only */
#define VGPU98_OGL_BF_ONE_MINUS_SRC_COLOR 3  /* destination factor only */
#define VGPU98_OGL_BF_SRC_ALPHA           4
#define VGPU98_OGL_BF_ONE_MINUS_SRC_ALPHA 5
#define VGPU98_OGL_BF_DST_ALPHA           6
#define VGPU98_OGL_BF_ONE_MINUS_DST_ALPHA 7
#define VGPU98_OGL_BF_DST_COLOR           8  /* source factor only */
#define VGPU98_OGL_BF_ONE_MINUS_DST_COLOR 9  /* source factor only */
#define VGPU98_OGL_BF_SRC_ALPHA_SATURATE  10 /* source factor only */

/* --- Vertices (DRAW) -------------------------------------------------------- */

/* DRAW's control: what its indices make, and how its vertices are given.
   The indices are 16 bits each, two a dword, the first in the low half. */
#define VGPU98_OGL_DRAW_KIND_MASK    0x0f
#define VGPU98_OGL_POINTS            0 /* one index a point */
#define VGPU98_OGL_LINES             1 /* two a line, the provoking vertex second (GL's) */
#define VGPU98_OGL_TRIANGLES         2 /* three a triangle, the provoking vertex first */
#define VGPU98_OGL_DRAW_CLIP         0x100 /* the vertices are clip coordinates, clipped, lit and textured already (the ICD's own path) */
#define VGPU98_OGL_DRAW_STIPPLE_ON   0x200 /* LINES: each line's stipple counter starts at its first vertex's STIPPLE (else 0) */
#define VGPU98_OGL_DRAW_POLYGON      0x400 /* LINES, POINTS: a polygon's edges or vertices (its polygon mode): polygon offset applies */
#define VGPU98_OGL_DRAW_TEX1         0x800 /* 2.6: each vertex is 20 floats, unit 1's texture coordinates last */

/* A vertex: 16 floats (20 with DRAW_TEX1). */
#define VGPU98_OGL_VERTEX_DWORDS     16
#define VGPU98_OGL_VERTEX1_DWORDS    20
#define VGPU98_OGLV_X                0  /* object coordinates (with DRAW_CLIP: clip coordinates) */
#define VGPU98_OGLV_Y                1
#define VGPU98_OGLV_Z                2
#define VGPU98_OGLV_W                3
#define VGPU98_OGLV_R                4  /* the current colour as given (with DRAW_CLIP: the final colour, 0-1) */
#define VGPU98_OGLV_G                5
#define VGPU98_OGLV_B                6
#define VGPU98_OGLV_A                7
#define VGPU98_OGLV_NX               8  /* the current normal (with DRAW_CLIP: NX is the fog distance) */
#define VGPU98_OGLV_NY               9
#define VGPU98_OGLV_NZ               10
#define VGPU98_OGLV_STIPPLE          11 /* with DRAW_CLIP and lines: the stipple counter at the vertex; with DRAW_CLIP and points
                                           (2.6): the point's size when above 0 (R_POINT_SIZE's for 0); else 0 */
#define VGPU98_OGLV_S                12 /* the current texture coordinates (with DRAW_CLIP: the final ones) */
#define VGPU98_OGLV_T                13
#define VGPU98_OGLV_TR               14
#define VGPU98_OGLV_Q                15
#define VGPU98_OGLV_S1               16 /* DRAW_TEX1: unit 1's (with DRAW_CLIP: its final ones) */
#define VGPU98_OGLV_T1               17
#define VGPU98_OGLV_R1               18
#define VGPU98_OGLV_Q1               19

/* --- Clears, swaps --------------------------------------------------------- */

/* CLEAR's mask */
#define VGPU98_OGL_CLEAR_COLOR       1
#define VGPU98_OGL_CLEAR_DEPTH       2
#define VGPU98_OGL_CLEAR_STENCIL     4

/* SWAP's flags: in this order, those given. */
#define VGPU98_OGL_SWAP_LOAD         1 /* the front image takes the window's visible pixels from video memory first */
#define VGPU98_OGL_SWAP_COPY         2 /* the back image is copied into the front image (SwapBuffers) */
#define VGPU98_OGL_SWAP_SHOW         4 /* the front image is written to video memory through the rectangles */
/* 2.6: SHOW's vertical blanks (WGL_EXT_swap_control): 0 none; n, the n-th
   since the drawable's last SHOW and one at least from now, the image read
   back before the wait and written after it (at most 4). */
#define VGPU98_OGL_SWAP_INTERVAL_SHIFT 4
#define VGPU98_OGL_SWAP_INTERVAL_MASK  0x70
#define VGPU98_OGL_SWAP_INTERVAL_MAX   4
/* Interface 3.6, sent only to a card with PROFILE.VSYNC:
   the V-sync overrides may change this SHOW's wait. A SwapBuffers whose
   window is the whole screen, all of it visible, with the cursor not kept
   out of it: the SHOWs the interval is sent for. Without it the SHOW waits
   as asked (a window on the desktop, a front-buffer show). */
#define VGPU98_OGL_SWAP_OVERRIDE       8
/* SWAP's place: the window's client area's top-left corner on the screen,
   x | y << 16, each a signed 16-bit value; a rectangle: x0 | y0 << 16,
   x1 | y1 << 16, exclusive, on the screen, inside the client area. */

/* --- Textures (TEXIMAGE, TEXPARAM, TEXREAD) ------------------------------- */

/* TEXIMAGE's control: dimensions | level << 4 | border << 8 | DEFINE. A
   DEFINE gives the level's size (border included) and no texels, or texels
   for all of it; without it, the texels are for a rectangle of a level
   defined before, at its place (x | y << 16, border included). */
#define VGPU98_OGL_TEX_DIMS_MASK     0x0f /* 1 or 2 */
#define VGPU98_OGL_TEX_LEVEL_SHIFT   4
#define VGPU98_OGL_TEX_BORDER        0x100 /* a border of one texel */
#define VGPU98_OGL_TEX_DEFINE        0x200
#define VGPU98_OGL_TEX_TEXELS        0x400 /* DEFINE: the texels follow */

/* Base formats: which components a texel has. Texels are
   RGBA8 (red in the low byte), the missing components as GL fills them in:
   luminance as red, green and blue, intensity as all four. */
#define VGPU98_OGL_BASE_ALPHA        1
#define VGPU98_OGL_BASE_LUMINANCE    2
#define VGPU98_OGL_BASE_LUM_ALPHA    3
#define VGPU98_OGL_BASE_INTENSITY    4
#define VGPU98_OGL_BASE_RGB          5
#define VGPU98_OGL_BASE_RGBA         6
/* 2.6 (EXT_paletted_texture): an index texture's levels. Its texels are
   8-bit indices, a byte each, four a dword in TEXIMAGE (the first in the
   low byte), each row padded to a whole dword; TEXREAD writes them a byte
   each. They are looked up in a palette (PALETTE: the texture's own, or
   the context's shared one with EN_SHARED_PALETTE), whose base format the
   texture takes, each texel within the palette's width, before filtering.
   A palette is up to 256 entries, a power of two; its entries are texels
   of its base format (RGBA8, filled in as above). A texture or a context
   starts with one entry, white, of RGBA. */
#define VGPU98_OGL_BASE_INDEX        7
#define VGPU98_OGL_PALETTE_MAX       256

/* TEXPARAM's wraps: s | t << 4; its filters: min | mag << 4. */
#define VGPU98_OGL_WRAP_REPEAT       0
#define VGPU98_OGL_WRAP_CLAMP        1
#define VGPU98_OGL_WRAP_CLAMP_EDGE   2 /* 2.7: clamp to edge, the border never sampled (GL_CLAMP_TO_EDGE) */
#define VGPU98_OGL_FILTER_NEAREST    0
#define VGPU98_OGL_FILTER_LINEAR     1
#define VGPU98_OGL_FILTER_N_MIP_N    2 /* NEAREST_MIPMAP_NEAREST */
#define VGPU98_OGL_FILTER_L_MIP_N    3 /* LINEAR_MIPMAP_NEAREST */
#define VGPU98_OGL_FILTER_N_MIP_L    4 /* NEAREST_MIPMAP_LINEAR */
#define VGPU98_OGL_FILTER_L_MIP_L    5 /* LINEAR_MIPMAP_LINEAR */

/* --- Pixel rectangles (PIXELS, READ) ---------------------------------------- */

/* PIXELS: an image's fragments (glDrawPixels, glCopyPixels, glBitmap), its
   pixel (i, j) covering the pixel centres from (x + zoom x * i, y + zoom y
   * j) to (x + zoom x * (i + 1), y + zoom y * (j + 1)) in window
   coordinates (GL 1.1, 3.6.4); a bitmap's at (floor(x) + i, floor(y) + j).
   z, the colour, the texture coordinates and the fog distance are the
   raster position's (a colour image's own colour in place of its colour,
   a depth image's own depths in place of z). Its kind; its data, a row
   after another from the bottom: */
#define VGPU98_OGL_PIXELS_COLOR      0 /* RGBA8, red in the low byte */
#define VGPU98_OGL_PIXELS_DEPTH      1 /* floats, 0-1 */
#define VGPU98_OGL_PIXELS_STENCIL    2 /* bytes, four a dword: written through the stencil write mask, no colour, no tests */
#define VGPU98_OGL_PIXELS_BITMAP     3 /* bytes (1: a fragment), four a dword */
/* 2.6: with this in the kind, unit 1's raster texture coordinates (4)
   follow the fog distance, before the data. */
#define VGPU98_OGL_PIXELS_TEX1       0x100
#define VGPU98_OGL_PIXELS_KIND_MASK  0xff

/* READ's what; the rows written from the bottom (GL's), each a stride
   apart from video memory's offset: */
#define VGPU98_OGL_READ_FRONT        0 /* colour as the drawable keeps it: 565 (2 bytes) or BGRA 8888 (4) */
#define VGPU98_OGL_READ_BACK         1
#define VGPU98_OGL_READ_DEPTH        2 /* 32-bit integers: the 16- or 24-bit value */
#define VGPU98_OGL_READ_STENCIL      3 /* bytes */
/* TEXREAD writes rows of a level's texels, RGBA8, its border included, the
   first row (t 0) first: rows from the first given. */

#endif /* VIDEO_VGPU98_OGLCMD_H */

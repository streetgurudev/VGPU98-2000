/*
 * OpenGL 1.1's types and values, for the ICD (vgpu98gl.dll)
 * and its host tests. The ICD includes no system
 * header: it is built freestanding for the guest, and natively on a 64-bit
 * host for the tests, so its types are fixed-width ones, at the sizes a
 * 32-bit Windows gl.h gives them.
 *
 * The values are every one an OpenGL 1.1 header defines: facts from the XP
 * DDK's gl.h, checked against Wine's GL header (both read for facts). The
 * grouping and the words are ours. A 1.1 header also carries a few
 * extensions' values; they come after the core, kept apart. Then the values
 * of the extensions the ICD offers: facts from the
 * games that use them and from Wine's generated GL header, which follows the
 * Khronos registry.
 *
 * The last part is what WGL's pixel formats need: PIXELFORMATDESCRIPTOR's
 * layout and its flags, facts from the same DDK's wingdi.h and Wine's.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_GLENUM_H
#define VGPU98_GLENUM_H

#include <stdint.h>

/* --- The types ------------------------------------------------------------ */

/* GL's own names for C's types. GLboolean is a byte, GLenum and GLbitfield
   unsigned dwords; GLclampf and GLclampd hold values GL clamps to [0, 1]. */
typedef uint32_t GLenum;
typedef uint8_t  GLboolean;
typedef uint32_t GLbitfield;
typedef int8_t   GLbyte;
typedef int16_t  GLshort;
typedef int32_t  GLint;
typedef int32_t  GLsizei;
typedef uint8_t  GLubyte;
typedef uint16_t GLushort;
typedef uint32_t GLuint;
typedef float    GLfloat;
typedef float    GLclampf;
typedef double   GLdouble;
typedef double   GLclampd;
typedef void     GLvoid;

/* --- OpenGL 1.1 ----------------------------------------------------------- */

/* The version this header describes. */
#define GL_VERSION_1_1 1

/* Truth values: GLboolean's two. */
#define GL_FALSE 0x0000
#define GL_TRUE  0x0001

/* Zero, one and none. GL_ZERO and GL_ONE are blend factors (GL_ZERO also a
   stencil operation); GL_NONE is a draw buffer. */
#define GL_ZERO 0x0000
#define GL_ONE  0x0001
#define GL_NONE 0x0000

/* glGetError's answers. */
#define GL_NO_ERROR          0x0000
#define GL_INVALID_ENUM      0x0500
#define GL_INVALID_VALUE     0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_STACK_OVERFLOW    0x0503
#define GL_STACK_UNDERFLOW   0x0504
#define GL_OUT_OF_MEMORY     0x0505

/* Primitives: glBegin's modes, and glDrawArrays' and glDrawElements'. */
#define GL_POINTS         0x0000
#define GL_LINES          0x0001
#define GL_LINE_LOOP      0x0002
#define GL_LINE_STRIP     0x0003
#define GL_TRIANGLES      0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN   0x0006
#define GL_QUADS          0x0007
#define GL_QUAD_STRIP     0x0008
#define GL_POLYGON        0x0009

/* glPushAttrib's groups, one bit each; glClear takes four of them (colour,
   depth, stencil, accumulation). GL_ALL_ATTRIB_BITS is a 1.1 header's
   twenty bits; later headers make it 0xFFFFFFFF, and the bits past the
   twenty name no 1.1 group. */
#define GL_CURRENT_BIT         0x00000001
#define GL_POINT_BIT           0x00000002
#define GL_LINE_BIT            0x00000004
#define GL_POLYGON_BIT         0x00000008
#define GL_POLYGON_STIPPLE_BIT 0x00000010
#define GL_PIXEL_MODE_BIT      0x00000020
#define GL_LIGHTING_BIT        0x00000040
#define GL_FOG_BIT             0x00000080
#define GL_DEPTH_BUFFER_BIT    0x00000100
#define GL_ACCUM_BUFFER_BIT    0x00000200
#define GL_STENCIL_BUFFER_BIT  0x00000400
#define GL_VIEWPORT_BIT        0x00000800
#define GL_TRANSFORM_BIT       0x00001000
#define GL_ENABLE_BIT          0x00002000
#define GL_COLOR_BUFFER_BIT    0x00004000
#define GL_HINT_BIT            0x00008000
#define GL_EVAL_BIT            0x00010000
#define GL_LIST_BIT            0x00020000
#define GL_TEXTURE_BIT         0x00040000
#define GL_SCISSOR_BIT         0x00080000
#define GL_ALL_ATTRIB_BITS     0x000FFFFF

/* glPushClientAttrib's groups. */
#define GL_CLIENT_PIXEL_STORE_BIT  0x00000001
#define GL_CLIENT_VERTEX_ARRAY_BIT 0x00000002
#define GL_CLIENT_ALL_ATTRIB_BITS  0xFFFFFFFF

/* glAccum's operations. */
#define GL_ACCUM  0x0100
#define GL_LOAD   0x0101
#define GL_RETURN 0x0102
#define GL_MULT   0x0103
#define GL_ADD    0x0104

/* Comparisons, for the alpha, depth and stencil tests. */
#define GL_NEVER    0x0200
#define GL_LESS     0x0201
#define GL_EQUAL    0x0202
#define GL_LEQUAL   0x0203
#define GL_GREATER  0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL   0x0206
#define GL_ALWAYS   0x0207

/* Blend factors, beside GL_ZERO and GL_ONE. The DST_COLOR pair and
   SRC_ALPHA_SATURATE are source factors only; the SRC_COLOR pair
   destination factors only. */
#define GL_SRC_COLOR           0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA           0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA           0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR           0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE  0x0308

/* Colour buffers, for glDrawBuffer and glReadBuffer; GL_FRONT, GL_BACK and
   GL_FRONT_AND_BACK also name faces (culling, materials, polygon modes). */
#define GL_FRONT_LEFT     0x0400
#define GL_FRONT_RIGHT    0x0401
#define GL_BACK_LEFT      0x0402
#define GL_BACK_RIGHT     0x0403
#define GL_FRONT          0x0404
#define GL_BACK           0x0405
#define GL_LEFT           0x0406
#define GL_RIGHT          0x0407
#define GL_FRONT_AND_BACK 0x0408
#define GL_AUX0           0x0409
#define GL_AUX1           0x040A
#define GL_AUX2           0x040B
#define GL_AUX3           0x040C

/* Feedback: the vertex types glFeedbackBuffer takes. */
#define GL_2D               0x0600
#define GL_3D               0x0601
#define GL_3D_COLOR         0x0602
#define GL_3D_COLOR_TEXTURE 0x0603
#define GL_4D_COLOR_TEXTURE 0x0604

/* Feedback: the tokens written into the buffer, before each record. */
#define GL_PASS_THROUGH_TOKEN 0x0700
#define GL_POINT_TOKEN        0x0701
#define GL_LINE_TOKEN         0x0702
#define GL_POLYGON_TOKEN      0x0703
#define GL_BITMAP_TOKEN       0x0704
#define GL_DRAW_PIXEL_TOKEN   0x0705
#define GL_COPY_PIXEL_TOKEN   0x0706
#define GL_LINE_RESET_TOKEN   0x0707

/* Fog modes, beside GL_LINEAR. */
#define GL_EXP  0x0800
#define GL_EXP2 0x0801

/* Which winding faces front. */
#define GL_CW  0x0900
#define GL_CCW 0x0901

/* glGetMap's queries. */
#define GL_COEFF  0x0A00
#define GL_ORDER  0x0A01
#define GL_DOMAIN 0x0A02

/* State, for glGet: the current vertex values and the raster position. */
#define GL_CURRENT_COLOR                 0x0B00
#define GL_CURRENT_INDEX                 0x0B01
#define GL_CURRENT_NORMAL                0x0B02
#define GL_CURRENT_TEXTURE_COORDS        0x0B03
#define GL_CURRENT_RASTER_COLOR          0x0B04
#define GL_CURRENT_RASTER_INDEX          0x0B05
#define GL_CURRENT_RASTER_TEXTURE_COORDS 0x0B06
#define GL_CURRENT_RASTER_POSITION       0x0B07
#define GL_CURRENT_RASTER_POSITION_VALID 0x0B08
#define GL_CURRENT_RASTER_DISTANCE       0x0B09

/* State: points and lines. The _SMOOTH and GL_LINE_STIPPLE names are also
   glEnable's. */
#define GL_POINT_SMOOTH           0x0B10
#define GL_POINT_SIZE             0x0B11
#define GL_POINT_SIZE_RANGE       0x0B12
#define GL_POINT_SIZE_GRANULARITY 0x0B13
#define GL_LINE_SMOOTH            0x0B20
#define GL_LINE_WIDTH             0x0B21
#define GL_LINE_WIDTH_RANGE       0x0B22
#define GL_LINE_WIDTH_GRANULARITY 0x0B23
#define GL_LINE_STIPPLE           0x0B24
#define GL_LINE_STIPPLE_PATTERN   0x0B25
#define GL_LINE_STIPPLE_REPEAT    0x0B26

/* State: display lists. */
#define GL_LIST_MODE        0x0B30
#define GL_MAX_LIST_NESTING 0x0B31
#define GL_LIST_BASE        0x0B32
#define GL_LIST_INDEX       0x0B33

/* State: polygons. */
#define GL_POLYGON_MODE    0x0B40
#define GL_POLYGON_SMOOTH  0x0B41
#define GL_POLYGON_STIPPLE 0x0B42
#define GL_EDGE_FLAG       0x0B43
#define GL_CULL_FACE       0x0B44
#define GL_CULL_FACE_MODE  0x0B45
#define GL_FRONT_FACE      0x0B46

/* State: lighting. The GL_LIGHT_MODEL_ names are also glLightModel's. */
#define GL_LIGHTING                 0x0B50
#define GL_LIGHT_MODEL_LOCAL_VIEWER 0x0B51
#define GL_LIGHT_MODEL_TWO_SIDE     0x0B52
#define GL_LIGHT_MODEL_AMBIENT      0x0B53
#define GL_SHADE_MODEL              0x0B54
#define GL_COLOR_MATERIAL_FACE      0x0B55
#define GL_COLOR_MATERIAL_PARAMETER 0x0B56
#define GL_COLOR_MATERIAL           0x0B57

/* State: fog. All but GL_FOG are also glFog's. */
#define GL_FOG         0x0B60
#define GL_FOG_INDEX   0x0B61
#define GL_FOG_DENSITY 0x0B62
#define GL_FOG_START   0x0B63
#define GL_FOG_END     0x0B64
#define GL_FOG_MODE    0x0B65
#define GL_FOG_COLOR   0x0B66

/* State: depth and accumulation buffers. */
#define GL_DEPTH_RANGE       0x0B70
#define GL_DEPTH_TEST        0x0B71
#define GL_DEPTH_WRITEMASK   0x0B72
#define GL_DEPTH_CLEAR_VALUE 0x0B73
#define GL_DEPTH_FUNC        0x0B74
#define GL_ACCUM_CLEAR_VALUE 0x0B80

/* State: stencil. */
#define GL_STENCIL_TEST            0x0B90
#define GL_STENCIL_CLEAR_VALUE     0x0B91
#define GL_STENCIL_FUNC            0x0B92
#define GL_STENCIL_VALUE_MASK      0x0B93
#define GL_STENCIL_FAIL            0x0B94
#define GL_STENCIL_PASS_DEPTH_FAIL 0x0B95
#define GL_STENCIL_PASS_DEPTH_PASS 0x0B96
#define GL_STENCIL_REF             0x0B97
#define GL_STENCIL_WRITEMASK       0x0B98

/* State: transformation, and the attribute stacks' depths. */
#define GL_MATRIX_MODE               0x0BA0
#define GL_NORMALIZE                 0x0BA1
#define GL_VIEWPORT                  0x0BA2
#define GL_MODELVIEW_STACK_DEPTH     0x0BA3
#define GL_PROJECTION_STACK_DEPTH    0x0BA4
#define GL_TEXTURE_STACK_DEPTH       0x0BA5
#define GL_MODELVIEW_MATRIX          0x0BA6
#define GL_PROJECTION_MATRIX         0x0BA7
#define GL_TEXTURE_MATRIX            0x0BA8
#define GL_ATTRIB_STACK_DEPTH        0x0BB0
#define GL_CLIENT_ATTRIB_STACK_DEPTH 0x0BB1

/* State: alpha test, dithering, blending, logic op. */
#define GL_ALPHA_TEST      0x0BC0
#define GL_ALPHA_TEST_FUNC 0x0BC1
#define GL_ALPHA_TEST_REF  0x0BC2
#define GL_DITHER          0x0BD0
#define GL_BLEND_DST       0x0BE0
#define GL_BLEND_SRC       0x0BE1
#define GL_BLEND           0x0BE2
#define GL_LOGIC_OP_MODE   0x0BF0
#define GL_INDEX_LOGIC_OP  0x0BF1
#define GL_COLOR_LOGIC_OP  0x0BF2

/* State: the buffers drawn and read, the scissor, clear values and write
   masks, and what kind of frame buffer the context has. */
#define GL_AUX_BUFFERS       0x0C00
#define GL_DRAW_BUFFER       0x0C01
#define GL_READ_BUFFER       0x0C02
#define GL_SCISSOR_BOX       0x0C10
#define GL_SCISSOR_TEST      0x0C11
#define GL_INDEX_CLEAR_VALUE 0x0C20
#define GL_INDEX_WRITEMASK   0x0C21
#define GL_COLOR_CLEAR_VALUE 0x0C22
#define GL_COLOR_WRITEMASK   0x0C23
#define GL_INDEX_MODE        0x0C30
#define GL_RGBA_MODE         0x0C31
#define GL_DOUBLEBUFFER      0x0C32
#define GL_STEREO            0x0C33
#define GL_RENDER_MODE       0x0C40

/* Hints: glHint's targets (also glGet names). */
#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#define GL_POINT_SMOOTH_HINT           0x0C51
#define GL_LINE_SMOOTH_HINT            0x0C52
#define GL_POLYGON_SMOOTH_HINT         0x0C53
#define GL_FOG_HINT                    0x0C54

/* Texture coordinate generation, enabled one coordinate at a time. */
#define GL_TEXTURE_GEN_S 0x0C60
#define GL_TEXTURE_GEN_T 0x0C61
#define GL_TEXTURE_GEN_R 0x0C62
#define GL_TEXTURE_GEN_Q 0x0C63

/* Pixel maps (glPixelMap, glGetPixelMap), then their sizes (glGet). */
#define GL_PIXEL_MAP_I_TO_I      0x0C70
#define GL_PIXEL_MAP_S_TO_S      0x0C71
#define GL_PIXEL_MAP_I_TO_R      0x0C72
#define GL_PIXEL_MAP_I_TO_G      0x0C73
#define GL_PIXEL_MAP_I_TO_B      0x0C74
#define GL_PIXEL_MAP_I_TO_A      0x0C75
#define GL_PIXEL_MAP_R_TO_R      0x0C76
#define GL_PIXEL_MAP_G_TO_G      0x0C77
#define GL_PIXEL_MAP_B_TO_B      0x0C78
#define GL_PIXEL_MAP_A_TO_A      0x0C79
#define GL_PIXEL_MAP_I_TO_I_SIZE 0x0CB0
#define GL_PIXEL_MAP_S_TO_S_SIZE 0x0CB1
#define GL_PIXEL_MAP_I_TO_R_SIZE 0x0CB2
#define GL_PIXEL_MAP_I_TO_G_SIZE 0x0CB3
#define GL_PIXEL_MAP_I_TO_B_SIZE 0x0CB4
#define GL_PIXEL_MAP_I_TO_A_SIZE 0x0CB5
#define GL_PIXEL_MAP_R_TO_R_SIZE 0x0CB6
#define GL_PIXEL_MAP_G_TO_G_SIZE 0x0CB7
#define GL_PIXEL_MAP_B_TO_B_SIZE 0x0CB8
#define GL_PIXEL_MAP_A_TO_A_SIZE 0x0CB9

/* Pixel storage, for glPixelStore: how images are read from the program's
   memory (unpack) and written to it (pack). */
#define GL_UNPACK_SWAP_BYTES  0x0CF0
#define GL_UNPACK_LSB_FIRST   0x0CF1
#define GL_UNPACK_ROW_LENGTH  0x0CF2
#define GL_UNPACK_SKIP_ROWS   0x0CF3
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#define GL_UNPACK_ALIGNMENT   0x0CF5
#define GL_PACK_SWAP_BYTES    0x0D00
#define GL_PACK_LSB_FIRST     0x0D01
#define GL_PACK_ROW_LENGTH    0x0D02
#define GL_PACK_SKIP_ROWS     0x0D03
#define GL_PACK_SKIP_PIXELS   0x0D04
#define GL_PACK_ALIGNMENT     0x0D05

/* Pixel transfer, for glPixelTransfer. GL_ZOOM_X and GL_ZOOM_Y sit among
   them but are glPixelZoom's, read with glGet. */
#define GL_MAP_COLOR    0x0D10
#define GL_MAP_STENCIL  0x0D11
#define GL_INDEX_SHIFT  0x0D12
#define GL_INDEX_OFFSET 0x0D13
#define GL_RED_SCALE    0x0D14
#define GL_RED_BIAS     0x0D15
#define GL_ZOOM_X       0x0D16
#define GL_ZOOM_Y       0x0D17
#define GL_GREEN_SCALE  0x0D18
#define GL_GREEN_BIAS   0x0D19
#define GL_BLUE_SCALE   0x0D1A
#define GL_BLUE_BIAS    0x0D1B
#define GL_ALPHA_SCALE  0x0D1C
#define GL_ALPHA_BIAS   0x0D1D
#define GL_DEPTH_SCALE  0x0D1E
#define GL_DEPTH_BIAS   0x0D1F

/* The implementation's limits. */
#define GL_MAX_EVAL_ORDER                0x0D30
#define GL_MAX_LIGHTS                    0x0D31
#define GL_MAX_CLIP_PLANES               0x0D32
#define GL_MAX_TEXTURE_SIZE              0x0D33
#define GL_MAX_PIXEL_MAP_TABLE           0x0D34
#define GL_MAX_ATTRIB_STACK_DEPTH        0x0D35
#define GL_MAX_MODELVIEW_STACK_DEPTH     0x0D36
#define GL_MAX_NAME_STACK_DEPTH          0x0D37
#define GL_MAX_PROJECTION_STACK_DEPTH    0x0D38
#define GL_MAX_TEXTURE_STACK_DEPTH       0x0D39
#define GL_MAX_VIEWPORT_DIMS             0x0D3A
#define GL_MAX_CLIENT_ATTRIB_STACK_DEPTH 0x0D3B
#define GL_SUBPIXEL_BITS                 0x0D50

/* The frame buffer's bits per component, and the name stack's depth. */
#define GL_INDEX_BITS       0x0D51
#define GL_RED_BITS         0x0D52
#define GL_GREEN_BITS       0x0D53
#define GL_BLUE_BITS        0x0D54
#define GL_ALPHA_BITS       0x0D55
#define GL_DEPTH_BITS       0x0D56
#define GL_STENCIL_BITS     0x0D57
#define GL_ACCUM_RED_BITS   0x0D58
#define GL_ACCUM_GREEN_BITS 0x0D59
#define GL_ACCUM_BLUE_BITS  0x0D5A
#define GL_ACCUM_ALPHA_BITS 0x0D5B
#define GL_NAME_STACK_DEPTH 0x0D70

/* Evaluators: automatic normals, the one- and two-dimensional maps (each
   also glEnable's), and the grids glMapGrid sets. */
#define GL_AUTO_NORMAL          0x0D80
#define GL_MAP1_COLOR_4         0x0D90
#define GL_MAP1_INDEX           0x0D91
#define GL_MAP1_NORMAL          0x0D92
#define GL_MAP1_TEXTURE_COORD_1 0x0D93
#define GL_MAP1_TEXTURE_COORD_2 0x0D94
#define GL_MAP1_TEXTURE_COORD_3 0x0D95
#define GL_MAP1_TEXTURE_COORD_4 0x0D96
#define GL_MAP1_VERTEX_3        0x0D97
#define GL_MAP1_VERTEX_4        0x0D98
#define GL_MAP2_COLOR_4         0x0DB0
#define GL_MAP2_INDEX           0x0DB1
#define GL_MAP2_NORMAL          0x0DB2
#define GL_MAP2_TEXTURE_COORD_1 0x0DB3
#define GL_MAP2_TEXTURE_COORD_2 0x0DB4
#define GL_MAP2_TEXTURE_COORD_3 0x0DB5
#define GL_MAP2_TEXTURE_COORD_4 0x0DB6
#define GL_MAP2_VERTEX_3        0x0DB7
#define GL_MAP2_VERTEX_4        0x0DB8
#define GL_MAP1_GRID_DOMAIN     0x0DD0
#define GL_MAP1_GRID_SEGMENTS   0x0DD1
#define GL_MAP2_GRID_DOMAIN     0x0DD2
#define GL_MAP2_GRID_SEGMENTS   0x0DD3

/* Texture targets (also glEnable's), and their proxies, which check a
   glTexImage without keeping it. */
#define GL_TEXTURE_1D       0x0DE0
#define GL_TEXTURE_2D       0x0DE1
#define GL_PROXY_TEXTURE_1D 0x8063
#define GL_PROXY_TEXTURE_2D 0x8064

/* The feedback and selection buffers. */
#define GL_FEEDBACK_BUFFER_POINTER  0x0DF0
#define GL_FEEDBACK_BUFFER_SIZE     0x0DF1
#define GL_FEEDBACK_BUFFER_TYPE     0x0DF2
#define GL_SELECTION_BUFFER_POINTER 0x0DF3
#define GL_SELECTION_BUFFER_SIZE    0x0DF4

/* A texture level's description, for glGetTexLevelParameter (the border
   colour is a texture parameter). */
#define GL_TEXTURE_WIDTH           0x1000
#define GL_TEXTURE_HEIGHT          0x1001
#define GL_TEXTURE_INTERNAL_FORMAT 0x1003
#define GL_TEXTURE_BORDER_COLOR    0x1004
#define GL_TEXTURE_BORDER          0x1005
#define GL_TEXTURE_RED_SIZE        0x805C
#define GL_TEXTURE_GREEN_SIZE      0x805D
#define GL_TEXTURE_BLUE_SIZE       0x805E
#define GL_TEXTURE_ALPHA_SIZE      0x805F
#define GL_TEXTURE_LUMINANCE_SIZE  0x8060
#define GL_TEXTURE_INTENSITY_SIZE  0x8061

/* Hint modes. */
#define GL_DONT_CARE 0x1100
#define GL_FASTEST   0x1101
#define GL_NICEST    0x1102

/* Lights, and their parameters (glLight). GL_AMBIENT, GL_DIFFUSE and
   GL_SPECULAR are also material parameters. */
#define GL_LIGHT0                0x4000
#define GL_LIGHT1                0x4001
#define GL_LIGHT2                0x4002
#define GL_LIGHT3                0x4003
#define GL_LIGHT4                0x4004
#define GL_LIGHT5                0x4005
#define GL_LIGHT6                0x4006
#define GL_LIGHT7                0x4007
#define GL_AMBIENT               0x1200
#define GL_DIFFUSE               0x1201
#define GL_SPECULAR              0x1202
#define GL_POSITION              0x1203
#define GL_SPOT_DIRECTION        0x1204
#define GL_SPOT_EXPONENT         0x1205
#define GL_SPOT_CUTOFF           0x1206
#define GL_CONSTANT_ATTENUATION  0x1207
#define GL_LINEAR_ATTENUATION    0x1208
#define GL_QUADRATIC_ATTENUATION 0x1209

/* Material parameters of their own (glMaterial). */
#define GL_EMISSION            0x1600
#define GL_SHININESS           0x1601
#define GL_AMBIENT_AND_DIFFUSE 0x1602
#define GL_COLOR_INDEXES       0x1603

/* glNewList's modes. */
#define GL_COMPILE             0x1300
#define GL_COMPILE_AND_EXECUTE 0x1301

/* Data types: of arrays, pixels and glCallLists' names (the 2-, 3- and
   4-byte ones only there). */
#define GL_BYTE           0x1400
#define GL_UNSIGNED_BYTE  0x1401
#define GL_SHORT          0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT            0x1404
#define GL_UNSIGNED_INT   0x1405
#define GL_FLOAT          0x1406
#define GL_2_BYTES        0x1407
#define GL_3_BYTES        0x1408
#define GL_4_BYTES        0x1409
#define GL_DOUBLE         0x140A

/* glLogicOp's sixteen operations. GL_INVERT is also a stencil operation. */
#define GL_CLEAR         0x1500
#define GL_AND           0x1501
#define GL_AND_REVERSE   0x1502
#define GL_COPY          0x1503
#define GL_AND_INVERTED  0x1504
#define GL_NOOP          0x1505
#define GL_XOR           0x1506
#define GL_OR            0x1507
#define GL_NOR           0x1508
#define GL_EQUIV         0x1509
#define GL_INVERT        0x150A
#define GL_OR_REVERSE    0x150B
#define GL_COPY_INVERTED 0x150C
#define GL_OR_INVERTED   0x150D
#define GL_NAND          0x150E
#define GL_SET           0x150F

/* glMatrixMode's stacks. */
#define GL_MODELVIEW  0x1700
#define GL_PROJECTION 0x1701
#define GL_TEXTURE    0x1702

/* glCopyPixels' types. */
#define GL_COLOR   0x1800
#define GL_DEPTH   0x1801
#define GL_STENCIL 0x1802

/* Pixel formats, of images drawn, read and given to textures. */
#define GL_COLOR_INDEX     0x1900
#define GL_STENCIL_INDEX   0x1901
#define GL_DEPTH_COMPONENT 0x1902
#define GL_RED             0x1903
#define GL_GREEN           0x1904
#define GL_BLUE            0x1905
#define GL_ALPHA           0x1906
#define GL_RGB             0x1907
#define GL_RGBA            0x1908
#define GL_LUMINANCE       0x1909
#define GL_LUMINANCE_ALPHA 0x190A

/* A pixel type beside the data types: one bit per pixel. */
#define GL_BITMAP 0x1A00

/* Polygon modes; glEvalMesh's too. */
#define GL_POINT 0x1B00
#define GL_LINE  0x1B01
#define GL_FILL  0x1B02

/* glRenderMode's modes. */
#define GL_RENDER   0x1C00
#define GL_FEEDBACK 0x1C01
#define GL_SELECT   0x1C02

/* Shading models. */
#define GL_FLAT   0x1D00
#define GL_SMOOTH 0x1D01

/* Stencil operations, beside GL_ZERO and GL_INVERT. GL_REPLACE is also a
   texture environment mode. */
#define GL_KEEP    0x1E00
#define GL_REPLACE 0x1E01
#define GL_INCR    0x1E02
#define GL_DECR    0x1E03

/* glGetString's names. */
#define GL_VENDOR     0x1F00
#define GL_RENDERER   0x1F01
#define GL_VERSION    0x1F02
#define GL_EXTENSIONS 0x1F03

/* Texture coordinates, for glTexGen. */
#define GL_S 0x2000
#define GL_T 0x2001
#define GL_R 0x2002
#define GL_Q 0x2003

/* The texture environment: its modes (with GL_BLEND and GL_REPLACE), its
   parameters and its target. */
#define GL_MODULATE          0x2100
#define GL_DECAL             0x2101
#define GL_TEXTURE_ENV_MODE  0x2200
#define GL_TEXTURE_ENV_COLOR 0x2201
#define GL_TEXTURE_ENV       0x2300

/* Texture coordinate generation: its modes and parameters. */
#define GL_EYE_LINEAR       0x2400
#define GL_OBJECT_LINEAR    0x2401
#define GL_SPHERE_MAP       0x2402
#define GL_TEXTURE_GEN_MODE 0x2500
#define GL_OBJECT_PLANE     0x2501
#define GL_EYE_PLANE        0x2502

/* Texture filters: the first two magnify, all six minify. GL_LINEAR is also a
   fog mode. */
#define GL_NEAREST                0x2600
#define GL_LINEAR                 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST  0x2701
#define GL_NEAREST_MIPMAP_LINEAR  0x2702
#define GL_LINEAR_MIPMAP_LINEAR   0x2703

/* Texture parameters (glTexParameter), beside the border colour and the
   priority; then the wrap modes. */
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S     0x2802
#define GL_TEXTURE_WRAP_T     0x2803
#define GL_CLAMP              0x2900
#define GL_REPEAT             0x2901

/* Polygon offset: its two values, and its three enables (one per polygon
   mode). */
#define GL_POLYGON_OFFSET_FACTOR 0x8038
#define GL_POLYGON_OFFSET_UNITS  0x2A00
#define GL_POLYGON_OFFSET_POINT  0x2A01
#define GL_POLYGON_OFFSET_LINE   0x2A02
#define GL_POLYGON_OFFSET_FILL   0x8037

/* User clip planes (also glEnable's). */
#define GL_CLIP_PLANE0 0x3000
#define GL_CLIP_PLANE1 0x3001
#define GL_CLIP_PLANE2 0x3002
#define GL_CLIP_PLANE3 0x3003
#define GL_CLIP_PLANE4 0x3004
#define GL_CLIP_PLANE5 0x3005

/* Internal formats a texture may ask for, beside 1-4 and the base pixel
   formats. Any may be kept at other sizes; glGetTexLevelParameter says what
   was. */
#define GL_R3_G3_B2            0x2A10
#define GL_ALPHA4              0x803B
#define GL_ALPHA8              0x803C
#define GL_ALPHA12             0x803D
#define GL_ALPHA16             0x803E
#define GL_LUMINANCE4          0x803F
#define GL_LUMINANCE8          0x8040
#define GL_LUMINANCE12         0x8041
#define GL_LUMINANCE16         0x8042
#define GL_LUMINANCE4_ALPHA4   0x8043
#define GL_LUMINANCE6_ALPHA2   0x8044
#define GL_LUMINANCE8_ALPHA8   0x8045
#define GL_LUMINANCE12_ALPHA4  0x8046
#define GL_LUMINANCE12_ALPHA12 0x8047
#define GL_LUMINANCE16_ALPHA16 0x8048
#define GL_INTENSITY           0x8049
#define GL_INTENSITY4          0x804A
#define GL_INTENSITY8          0x804B
#define GL_INTENSITY12         0x804C
#define GL_INTENSITY16         0x804D
#define GL_RGB4                0x804F
#define GL_RGB5                0x8050
#define GL_RGB8                0x8051
#define GL_RGB10               0x8052
#define GL_RGB12               0x8053
#define GL_RGB16               0x8054
#define GL_RGBA2               0x8055
#define GL_RGBA4               0x8056
#define GL_RGB5_A1             0x8057
#define GL_RGBA8               0x8058
#define GL_RGB10_A2            0x8059
#define GL_RGBA12              0x805A
#define GL_RGBA16              0x805B

/* Texture objects: a texture's priority and residency, and the bindings. */
#define GL_TEXTURE_PRIORITY   0x8066
#define GL_TEXTURE_RESIDENT   0x8067
#define GL_TEXTURE_BINDING_1D 0x8068
#define GL_TEXTURE_BINDING_2D 0x8069

/* Client vertex arrays: the six arrays (glEnableClientState), their sizes,
   types and strides (glGet), and their pointers (glGetPointerv). */
#define GL_VERTEX_ARRAY                0x8074
#define GL_NORMAL_ARRAY                0x8075
#define GL_COLOR_ARRAY                 0x8076
#define GL_INDEX_ARRAY                 0x8077
#define GL_TEXTURE_COORD_ARRAY         0x8078
#define GL_EDGE_FLAG_ARRAY             0x8079
#define GL_VERTEX_ARRAY_SIZE           0x807A
#define GL_VERTEX_ARRAY_TYPE           0x807B
#define GL_VERTEX_ARRAY_STRIDE         0x807C
#define GL_NORMAL_ARRAY_TYPE           0x807E
#define GL_NORMAL_ARRAY_STRIDE         0x807F
#define GL_COLOR_ARRAY_SIZE            0x8081
#define GL_COLOR_ARRAY_TYPE            0x8082
#define GL_COLOR_ARRAY_STRIDE          0x8083
#define GL_INDEX_ARRAY_TYPE            0x8085
#define GL_INDEX_ARRAY_STRIDE          0x8086
#define GL_TEXTURE_COORD_ARRAY_SIZE    0x8088
#define GL_TEXTURE_COORD_ARRAY_TYPE    0x8089
#define GL_TEXTURE_COORD_ARRAY_STRIDE  0x808A
#define GL_EDGE_FLAG_ARRAY_STRIDE      0x808C
#define GL_VERTEX_ARRAY_POINTER        0x808E
#define GL_NORMAL_ARRAY_POINTER        0x808F
#define GL_COLOR_ARRAY_POINTER         0x8090
#define GL_INDEX_ARRAY_POINTER         0x8091
#define GL_TEXTURE_COORD_ARRAY_POINTER 0x8092
#define GL_EDGE_FLAG_ARRAY_POINTER     0x8093

/* glInterleavedArrays' layouts: T texture coordinates, C colour, N normal,
   V vertex; the count of components, F float or UB unsigned byte. */
#define GL_V2F             0x2A20
#define GL_V3F             0x2A21
#define GL_C4UB_V2F        0x2A22
#define GL_C4UB_V3F        0x2A23
#define GL_C3F_V3F         0x2A24
#define GL_N3F_V3F         0x2A25
#define GL_C4F_N3F_V3F     0x2A26
#define GL_T2F_V3F         0x2A27
#define GL_T4F_V4F         0x2A28
#define GL_T2F_C4UB_V3F    0x2A29
#define GL_T2F_C3F_V3F     0x2A2A
#define GL_T2F_N3F_V3F     0x2A2B
#define GL_T2F_C4F_N3F_V3F 0x2A2C
#define GL_T4F_C4F_N3F_V4F 0x2A2D

/* OpenGL 1.0's names for two that 1.1 renamed, still in its headers:
   GL_INDEX_LOGIC_OP and GL_TEXTURE_INTERNAL_FORMAT. */
#define GL_LOGIC_OP           0x0BF1
#define GL_TEXTURE_COMPONENTS 0x1003

/* --- Extensions a 1.1 header carries ------------------------------------- */

/* The ICD offers none of these but EXT_paletted_texture. The values are
   here to name what
   a program may pass; most of EXT_vertex_array's are 1.1's own. */

/* The markers by which a 1.1 header says it has these extensions' names.
   They say nothing about what an implementation offers. */
#define GL_EXT_vertex_array        1
#define GL_EXT_bgra                1
#define GL_EXT_paletted_texture    1
#define GL_WIN_swap_hint           1
#define GL_WIN_draw_range_elements 1

/* EXT_vertex_array: the 1.1 vertex arrays' values under their _EXT names,
   and the element counts that only the extension has. */
#define GL_VERTEX_ARRAY_EXT                0x8074
#define GL_NORMAL_ARRAY_EXT                0x8075
#define GL_COLOR_ARRAY_EXT                 0x8076
#define GL_INDEX_ARRAY_EXT                 0x8077
#define GL_TEXTURE_COORD_ARRAY_EXT         0x8078
#define GL_EDGE_FLAG_ARRAY_EXT             0x8079
#define GL_VERTEX_ARRAY_SIZE_EXT           0x807A
#define GL_VERTEX_ARRAY_TYPE_EXT           0x807B
#define GL_VERTEX_ARRAY_STRIDE_EXT         0x807C
#define GL_VERTEX_ARRAY_COUNT_EXT          0x807D
#define GL_NORMAL_ARRAY_TYPE_EXT           0x807E
#define GL_NORMAL_ARRAY_STRIDE_EXT         0x807F
#define GL_NORMAL_ARRAY_COUNT_EXT          0x8080
#define GL_COLOR_ARRAY_SIZE_EXT            0x8081
#define GL_COLOR_ARRAY_TYPE_EXT            0x8082
#define GL_COLOR_ARRAY_STRIDE_EXT          0x8083
#define GL_COLOR_ARRAY_COUNT_EXT           0x8084
#define GL_INDEX_ARRAY_TYPE_EXT            0x8085
#define GL_INDEX_ARRAY_STRIDE_EXT          0x8086
#define GL_INDEX_ARRAY_COUNT_EXT           0x8087
#define GL_TEXTURE_COORD_ARRAY_SIZE_EXT    0x8088
#define GL_TEXTURE_COORD_ARRAY_TYPE_EXT    0x8089
#define GL_TEXTURE_COORD_ARRAY_STRIDE_EXT  0x808A
#define GL_TEXTURE_COORD_ARRAY_COUNT_EXT   0x808B
#define GL_EDGE_FLAG_ARRAY_STRIDE_EXT      0x808C
#define GL_EDGE_FLAG_ARRAY_COUNT_EXT       0x808D
#define GL_VERTEX_ARRAY_POINTER_EXT        0x808E
#define GL_NORMAL_ARRAY_POINTER_EXT        0x808F
#define GL_COLOR_ARRAY_POINTER_EXT         0x8090
#define GL_INDEX_ARRAY_POINTER_EXT         0x8091
#define GL_TEXTURE_COORD_ARRAY_POINTER_EXT 0x8092
#define GL_EDGE_FLAG_ARRAY_POINTER_EXT     0x8093
#define GL_DOUBLE_EXT                      0x140A

/* EXT_bgra: pixel formats with blue first. */
#define GL_BGR_EXT  0x80E0
#define GL_BGRA_EXT 0x80E1

/* EXT_paletted_texture: the colour table's queries (the same values as
   SGI's colour tables), then the indexed internal formats. */
#define GL_COLOR_TABLE_FORMAT_EXT         0x80D8
#define GL_COLOR_TABLE_WIDTH_EXT          0x80D9
#define GL_COLOR_TABLE_RED_SIZE_EXT       0x80DA
#define GL_COLOR_TABLE_GREEN_SIZE_EXT     0x80DB
#define GL_COLOR_TABLE_BLUE_SIZE_EXT      0x80DC
#define GL_COLOR_TABLE_ALPHA_SIZE_EXT     0x80DD
#define GL_COLOR_TABLE_LUMINANCE_SIZE_EXT 0x80DE
#define GL_COLOR_TABLE_INTENSITY_SIZE_EXT 0x80DF
#define GL_COLOR_INDEX1_EXT               0x80E2
#define GL_COLOR_INDEX2_EXT               0x80E3
#define GL_COLOR_INDEX4_EXT               0x80E4
#define GL_COLOR_INDEX8_EXT               0x80E5
#define GL_COLOR_INDEX12_EXT              0x80E6
#define GL_COLOR_INDEX16_EXT              0x80E7

/* WIN_draw_range_elements: its two limits. (WIN_swap_hint has a function,
   glAddSwapHintRectWIN, and no values.) */
#define GL_MAX_ELEMENTS_VERTICES_WIN 0x80E8
#define GL_MAX_ELEMENTS_INDICES_WIN  0x80E9

/* WIN_phong_shading and WIN_specular_fog: values the header defines
   though it leaves both extensions' markers out. */
#define GL_PHONG_WIN                0x80EA
#define GL_PHONG_HINT_WIN           0x80EB
#define GL_FOG_SPECULAR_TEXTURE_WIN 0x80EC

/* --- The extensions the ICD offers ------------------- */

/* EXT_paletted_texture's level query (its other values are above). */
#define GL_TEXTURE_INDEX_SIZE_EXT 0x80ED

/* EXT_shared_texture_palette: the shared palette's target and enable. */
#define GL_SHARED_TEXTURE_PALETTE_EXT 0x81FB

/* EXT_compiled_vertex_array: the locked range. */
#define GL_ARRAY_ELEMENT_LOCK_FIRST_EXT 0x81A8
#define GL_ARRAY_ELEMENT_LOCK_COUNT_EXT 0x81A9

/* EXT_point_parameters. */
#define GL_POINT_SIZE_MIN_EXT            0x8126
#define GL_POINT_SIZE_MAX_EXT            0x8127
#define GL_POINT_FADE_THRESHOLD_SIZE_EXT 0x8128
#define GL_DISTANCE_ATTENUATION_EXT      0x8129

/* EXT_texture_edge_clamp and SGIS_texture_edge_clamp (one token, GL 1.2's
   value). */
#define GL_CLAMP_TO_EDGE 0x812F

/* SGIS_multitexture's units, as Quake 2 and GLQuake pass them (the
   registry's later revision numbers them otherwise: the games decide). */
#define GL_TEXTURE0_SGIS 0x835E
#define GL_TEXTURE1_SGIS 0x835F

/* ARB_multitexture: units from TEXTURE0_ARB on, one value each. */
#define GL_TEXTURE0_ARB               0x84C0
#define GL_TEXTURE1_ARB               0x84C1
#define GL_ACTIVE_TEXTURE_ARB         0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE_ARB  0x84E1
#define GL_MAX_TEXTURE_UNITS_ARB      0x84E2 /* Quake 3's GL_MAX_ACTIVE_TEXTURES_ARB */

/* --- WGL's pixel formats ------------------------------------------------- */

/* A pixel format's kind of colour (iPixelType). The ICD's formats are all
   RGBA. */
#define PFD_TYPE_RGBA       0
#define PFD_TYPE_COLORINDEX 1

/* Layer planes (iLayerType, and the layer-plane exports' plane numbers): the
   main plane, overlays above it, underlays below. The ICD has only the main
   plane. */
#define PFD_MAIN_PLANE     0
#define PFD_OVERLAY_PLANE  1
#define PFD_UNDERLAY_PLANE (-1)

/* dwFlags. The ICD's formats draw to windows with OpenGL, double-buffered
   ones swapping by copy, and are never Windows' own generic ones. */
#define PFD_DOUBLEBUFFER        0x00000001
#define PFD_STEREO              0x00000002
#define PFD_DRAW_TO_WINDOW      0x00000004
#define PFD_DRAW_TO_BITMAP      0x00000008
#define PFD_SUPPORT_GDI         0x00000010
#define PFD_SUPPORT_OPENGL      0x00000020
#define PFD_GENERIC_FORMAT      0x00000040
#define PFD_NEED_PALETTE        0x00000080
#define PFD_NEED_SYSTEM_PALETTE 0x00000100
#define PFD_SWAP_EXCHANGE       0x00000200
#define PFD_SWAP_COPY           0x00000400
#define PFD_SWAP_LAYER_BUFFERS  0x00000800
#define PFD_GENERIC_ACCELERATED 0x00001000
#define PFD_SUPPORT_DIRECTDRAW  0x00002000 /* the XP DDK has it; Wine's header does not */
#define PFD_SUPPORT_COMPOSITION 0x00008000 /* Vista's; Wine's and ReactOS's headers have it */

/* Flags only ChoosePixelFormat reads: the program does not mind either way. */
#define PFD_DEPTH_DONTCARE        0x20000000
#define PFD_DOUBLEBUFFER_DONTCARE 0x40000000
#define PFD_STEREO_DONTCARE       0x80000000

/* PIXELFORMATDESCRIPTOR, field for field in fixed-width types: 40 bytes,
   every field at its natural alignment, so no padding on either compiler.
   DrvDescribePixelFormat fills one. */
typedef struct ogl_pfd {
    uint16_t nSize;    /* the structure's size, 40 */
    uint16_t nVersion; /* 1 */
    uint32_t dwFlags;  /* PFD_ flags */
    uint8_t  iPixelType;
    uint8_t  cColorBits; /* the colour's bits, alpha left out */
    uint8_t  cRedBits;
    uint8_t  cRedShift;
    uint8_t  cGreenBits;
    uint8_t  cGreenShift;
    uint8_t  cBlueBits;
    uint8_t  cBlueShift;
    uint8_t  cAlphaBits;
    uint8_t  cAlphaShift;
    uint8_t  cAccumBits;
    uint8_t  cAccumRedBits;
    uint8_t  cAccumGreenBits;
    uint8_t  cAccumBlueBits;
    uint8_t  cAccumAlphaBits;
    uint8_t  cDepthBits;
    uint8_t  cStencilBits;
    uint8_t  cAuxBuffers;
    uint8_t  iLayerType; /* PFD_MAIN_PLANE */
    uint8_t  bReserved;  /* overlay planes (low 4 bits) and underlay planes (high 4) */
    uint32_t dwLayerMask;
    uint32_t dwVisibleMask;
    uint32_t dwDamageMask;
} ogl_pfd_t;

_Static_assert(sizeof(ogl_pfd_t) == 40, "PIXELFORMATDESCRIPTOR is 40 bytes");

#endif /* VGPU98_GLENUM_H */

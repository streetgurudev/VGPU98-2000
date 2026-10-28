/*
 * GL 1.1's state as the ICD keeps it:
 * everything glGet* answers, the attribute stacks save and display lists
 * change. It is laid out by GL's attribute groups, so glPushAttrib and
 * glPopAttrib copy whole parts; the enables are one mask (each group saves
 * and restores its own bits of it). What no attribute group holds (the
 * matrix stacks, the pixel maps, the evaluators' maps, texture objects,
 * display lists, render modes, errors) is apart.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_ICD_STATE_H
#define VGPU98_ICD_STATE_H

#include "icd.h"

/* --- Limits -------------------------------------------------------------------- */

#define OGL_MAX_LIGHTS        8
#define OGL_MAX_CLIP_PLANES   6
#define OGL_MODELVIEW_DEPTH   32
#define OGL_PROJECTION_DEPTH  2
#define OGL_TEXTURE_DEPTH     2
#define OGL_NAME_DEPTH        64
#define OGL_ATTRIB_DEPTH      16
#define OGL_CLIENT_DEPTH      16
#define OGL_LIST_NESTING      64
#define OGL_EVAL_ORDER        30
#define OGL_PIXEL_MAP_SIZE    256
#define OGL_MAX_TEXTURE       2048
#define OGL_MAX_TEXTURE_LOG2  11
#define OGL_MAX_VIEWPORT      2048
#define OGL_SUBPIXEL_BITS     4
#define OGL_UNITS             2 /* texture units (ARB_multitexture, SGIS_multitexture) */
#define OGL_POINT_MIN         1.0f
#define OGL_POINT_MAX         64.0f
#define OGL_LINE_MIN          1.0f
#define OGL_LINE_MAX          16.0f
#define OGL_GRANULARITY       0.125f

/* --- The enables: one bit each ------------------------------------------------- */

#define OGL_E_ALPHA_TEST       (1ull << 0)
#define OGL_E_AUTO_NORMAL      (1ull << 1)
#define OGL_E_BLEND            (1ull << 2)
#define OGL_E_COLOR_MATERIAL   (1ull << 3)
#define OGL_E_CULL_FACE        (1ull << 4)
#define OGL_E_DEPTH_TEST       (1ull << 5)
#define OGL_E_DITHER           (1ull << 6)
#define OGL_E_FOG              (1ull << 7)
#define OGL_E_LIGHTING         (1ull << 8)
#define OGL_E_LINE_SMOOTH      (1ull << 9)
#define OGL_E_LINE_STIPPLE     (1ull << 10)
#define OGL_E_INDEX_LOGIC_OP   (1ull << 11)
#define OGL_E_COLOR_LOGIC_OP   (1ull << 12)
#define OGL_E_NORMALIZE        (1ull << 13)
#define OGL_E_POINT_SMOOTH     (1ull << 14)
#define OGL_E_OFFSET_FILL      (1ull << 15)
#define OGL_E_OFFSET_LINE      (1ull << 16)
#define OGL_E_OFFSET_POINT     (1ull << 17)
#define OGL_E_POLYGON_SMOOTH   (1ull << 18)
#define OGL_E_POLYGON_STIPPLE  (1ull << 19)
#define OGL_E_SCISSOR_TEST     (1ull << 20)
#define OGL_E_STENCIL_TEST     (1ull << 21)
#define OGL_E_SHARED_PALETTE   (1ull << 22) /* EXT_shared_texture_palette */
/* 23-27: free (the texture enables are each unit's: OGL_TE_*) */
#define OGL_E_MAP1(i)          (1ull << (28 + (i))) /* i 0-8: GL_MAP1_COLOR_4 to GL_MAP1_VERTEX_4 */
#define OGL_E_MAP2(i)          (1ull << (37 + (i))) /* likewise */
#define OGL_E_LIGHT(i)         (1ull << (46 + (i))) /* i 0-7 */
#define OGL_E_CLIP(i)          (1ull << (54 + (i))) /* i 0-5 */

#define OGL_E_MAP1_ALL    (0x1ffull << 28)
#define OGL_E_MAP2_ALL    (0x1ffull << 37)
#define OGL_E_LIGHT_ALL   (0xffull << 46)
#define OGL_E_CLIP_ALL    (0x3full << 54)
#define OGL_E_ALL         ((1ull << 60) - 1)

/* A texture unit's enables (ogl_texunit_t's en): its 1D and 2D textures,
   the generation of s, t, r and q. */
#define OGL_TE_1D      0x01
#define OGL_TE_2D      0x02
#define OGL_TE_GEN_S   0x04 /* << 0-3: s, t, r, q */
#define OGL_TE_GEN_ALL 0x3c

/* --- The attribute groups ----------------------------------------------------- */

typedef struct ogl_matrix {
    float m[16]; /* column-major, as GL gives it */
} ogl_matrix_t;

/* GL_CURRENT_BIT (texture coordinates each unit's) */
typedef struct ogl_current {
    float     color[4];
    float     index;
    float     texcoord[OGL_UNITS][4];
    float     normal[3];
    float     raster_pos[4];      /* window x, y, z and clip w */
    float     raster_distance;
    float     raster_color[4];
    float     raster_index;
    float     raster_texcoord[OGL_UNITS][4];
    GLboolean raster_valid;
    GLboolean edge_flag;
} ogl_current_t;

/* GL_POINT_BIT, GL_LINE_BIT, GL_POLYGON_BIT */
typedef struct ogl_raster {
    float    point_size;
    /* EXT_point_parameters (GL_POINT_BIT):
       the size's bounds, the fade threshold, the distance attenuation */
    float    point_min, point_max, point_fade;
    float    point_atten[3];
    float    line_width;
    GLushort line_pattern;
    GLint    line_repeat;
    GLenum   cull_mode;
    GLenum   front_face;
    GLenum   polygon_mode[2]; /* front, back */
    float    offset_factor;
    float    offset_units;
} ogl_raster_t;

/* GL_PIXEL_MODE_BIT */
typedef struct ogl_pixel_mode {
    GLboolean map_color;
    GLboolean map_stencil;
    GLint     index_shift;
    GLint     index_offset;
    float     scale[4]; /* red, green, blue, alpha */
    float     bias[4];
    float     depth_scale;
    float     depth_bias;
    float     zoom_x;
    float     zoom_y;
    GLenum    read_buffer;
} ogl_pixel_mode_t;

typedef struct ogl_light {
    float ambient[4];
    float diffuse[4];
    float specular[4];
    float position[4];  /* eye coordinates */
    float spot_dir[3];  /* eye coordinates */
    float spot_exponent;
    float spot_cutoff;
    float attenuation[3]; /* constant, linear, quadratic */
} ogl_light_t;

typedef struct ogl_material {
    float ambient[4];
    float diffuse[4];
    float specular[4];
    float emission[4];
    float shininess;
    float indexes[3]; /* ambient, diffuse, specular colour indexes */
} ogl_material_t;

/* GL_LIGHTING_BIT */
typedef struct ogl_lighting {
    GLenum         shade_model;
    ogl_material_t material[2]; /* front, back */
    float          model_ambient[4];
    GLboolean      local_viewer;
    GLboolean      two_side;
    GLenum         cm_face;
    GLenum         cm_mode;
    ogl_light_t    light[OGL_MAX_LIGHTS];
} ogl_lighting_t;

/* GL_FOG_BIT */
typedef struct ogl_fog {
    GLenum mode;
    float  color[4];
    float  density;
    float  start;
    float  end;
    float  index;
} ogl_fog_t;

/* GL_DEPTH_BUFFER_BIT, GL_ACCUM_BUFFER_BIT, GL_STENCIL_BUFFER_BIT */
typedef struct ogl_buffers {
    GLenum    depth_func;
    GLboolean depth_mask;
    double    depth_clear;
    float     accum_clear[4];
    GLenum    stencil_func;
    GLint     stencil_ref;
    GLuint    stencil_value_mask;
    GLuint    stencil_write_mask;
    GLenum    stencil_fail;
    GLenum    stencil_zfail;
    GLenum    stencil_zpass;
    GLint     stencil_clear;
} ogl_buffers_t;

/* GL_VIEWPORT_BIT */
typedef struct ogl_viewport {
    GLint   x, y;
    GLsizei w, h;
    double  near_val, far_val;
} ogl_viewport_t;

/* GL_TRANSFORM_BIT */
typedef struct ogl_transform {
    GLenum matrix_mode;
    double clip_plane[OGL_MAX_CLIP_PLANES][4]; /* eye coordinates */
} ogl_transform_t;

/* GL_COLOR_BUFFER_BIT */
typedef struct ogl_color_buffer {
    GLenum    alpha_func;
    float     alpha_ref;
    GLenum    blend_src;
    GLenum    blend_dst;
    GLenum    logic_op;
    GLenum    draw_buffer;
    GLuint    index_mask;
    GLboolean color_mask[4];
    float     clear_color[4];
    float     clear_index;
} ogl_color_buffer_t;

/* GL_HINT_BIT */
typedef struct ogl_hints {
    GLenum perspective;
    GLenum point_smooth;
    GLenum line_smooth;
    GLenum polygon_smooth;
    GLenum fog;
} ogl_hints_t;

/* GL_EVAL_BIT (with its enables) */
typedef struct ogl_eval_attr {
    GLint grid1_n;
    float grid1_u1, grid1_u2;
    GLint grid2_un, grid2_vn;
    float grid2_u1, grid2_u2, grid2_v1, grid2_v2;
} ogl_eval_attr_t;

/* GL_TEXTURE_BIT: each unit's enables, environment, generation and
   bindings (the bound textures' parameters are saved with them,
   attrib.c), and the active unit. */
typedef struct ogl_texunit {
    uint32_t en;               /* OGL_TE_* (GL_ENABLE_BIT saves them too) */
    GLenum   env_mode;
    float    env_color[4];
    GLenum   gen_mode[4];      /* s, t, r, q */
    double   obj_plane[4][4];
    double   eye_plane[4][4];  /* eye coordinates */
    GLuint   bound[2];         /* the 1D and 2D textures' names */
} ogl_texunit_t;

typedef struct ogl_texture_attr {
    ogl_texunit_t unit[OGL_UNITS];
    int           active; /* ACTIVE_TEXTURE_ARB: the unit texture state calls address */
} ogl_texture_attr_t;

/* GL_SCISSOR_BIT */
typedef struct ogl_scissor {
    GLint   x, y;
    GLsizei w, h;
} ogl_scissor_t;

/* The pixel store's packing or unpacking (GL_CLIENT_PIXEL_STORE_BIT). */
typedef struct ogl_store {
    GLboolean swap_bytes;
    GLboolean lsb_first;
    GLint     row_length;
    GLint     skip_rows;
    GLint     skip_pixels;
    GLint     alignment;
} ogl_store_t;

/* A client vertex array (GL_CLIENT_VERTEX_ARRAY_BIT). */
typedef struct ogl_array {
    GLboolean     enabled;
    GLint         size;
    GLenum        type;
    GLsizei       stride;
    const GLvoid *ptr;
} ogl_array_t;

#define OGL_ARRAY_VERTEX   0
#define OGL_ARRAY_NORMAL   1
#define OGL_ARRAY_COLOR    2
#define OGL_ARRAY_INDEX    3
#define OGL_ARRAY_EDGEFLAG 4
#define OGL_ARRAY_TEX(u)   (5 + (u)) /* each unit's texture coordinates */
#define OGL_ARRAY_TEXCOORD OGL_ARRAY_TEX(0)
#define OGL_ARRAYS         (5 + OGL_UNITS)

typedef struct ogl_client {
    ogl_store_t pack, unpack;
    ogl_array_t array[OGL_ARRAYS];
    /* EXT_compiled_vertex_array's locked range (count 0: none), client
       vertex-array state */
    GLint   lock_first;
    GLsizei lock_count;
    /* CLIENT_ACTIVE_TEXTURE_ARB: the unit whose texture coordinate array
       the array calls address; SGIS's coordinate set, which glTexCoord
       addresses too */
    int client_active;
    int coord_set;
} ogl_client_t;

/* --- What no attribute group holds ------------------------------------------- */

/* The pixel maps: I_TO_I, S_TO_S, I_TO_R, I_TO_G, I_TO_B, I_TO_A, R_TO_R,
   G_TO_G, B_TO_B, A_TO_A (GL_PIXEL_MAP_I_TO_I + n). */
#define OGL_PIXEL_MAPS 10
typedef struct ogl_pixel_map {
    GLint size;
    float value[OGL_PIXEL_MAP_SIZE];
} ogl_pixel_map_t;

/* An evaluator's map (GL_MAP1_* or GL_MAP2_*): its domain and control
   points (k floats a point, k by its target). */
typedef struct ogl_map {
    GLint  uorder, vorder; /* vorder 0 for a one-dimensional map */
    float  u1, u2, v1, v2;
    float *points;         /* uorder * vorder * k floats */
} ogl_map_t;

/* A palette (EXT_paletted_texture, EXT_shared_texture_palette):
   a texture's own, or the context's shared one. Its entries are
   texels of its base format, RGBA8 filled in as the device keeps texels
   (vid_vgpu98_oglcmd.h); it starts as one white entry of RGBA. Not
   attribute state (a texture image). */
typedef struct ogl_palette {
    int   n;    /* entries: a power of two up to VGPU98_OGL_PALETTE_MAX (0: a proxy's refusal) */
    GLint ifmt; /* the internal format asked for */
    int   base; /* its entries' VGPU98_OGL_BASE_* */
    DWORD e[VGPU98_OGL_PALETTE_MAX];
} ogl_palette_t;

/* glPushAttrib's record (attrib.c). */
typedef struct ogl_attrib_rec ogl_attrib_rec_t;
typedef struct ogl_client_rec ogl_client_rec_t;

/* --- The context's whole state --------------------------------------------- */

struct ogl_state {
    uint64_t enables;

    ogl_current_t      current;
    ogl_raster_t       raster;
    uint32_t           stipple[32]; /* GL_POLYGON_STIPPLE_BIT: rows from the bottom, bit 31 the leftmost */
    ogl_pixel_mode_t   pixel_mode;
    ogl_lighting_t     lighting;
    ogl_fog_t          fog;
    ogl_buffers_t      buffers;
    ogl_viewport_t     viewport;
    ogl_transform_t    transform;
    ogl_color_buffer_t color_buffer;
    ogl_hints_t        hints;
    ogl_eval_attr_t    eval;
    GLuint             list_base; /* GL_LIST_BIT */
    ogl_texture_attr_t texture;
    ogl_scissor_t      scissor;
    ogl_client_t       client;

    /* The matrix stacks: their tops are [depth - 1] */
    ogl_matrix_t modelview[OGL_MODELVIEW_DEPTH];
    ogl_matrix_t projection[OGL_PROJECTION_DEPTH];
    ogl_matrix_t texmatrix[OGL_UNITS][OGL_TEXTURE_DEPTH];
    int          modelview_depth, projection_depth, texture_depth[OGL_UNITS];
    int          normal_stale; /* the modelview changed: its inverse transpose is computed again before it is needed */
    float        normal[9];    /* the modelview's inverse transpose, 3x3 column-major */
    float        modelview_inverse[16];

    ogl_pixel_map_t pixel_map[OGL_PIXEL_MAPS];
    ogl_map_t       map1[9], map2[9];

    /* The attribute stacks */
    ogl_attrib_rec_t *attrib[OGL_ATTRIB_DEPTH];
    int               attrib_depth;
    ogl_client_rec_t *client_attrib[OGL_CLIENT_DEPTH];
    int               client_depth;

    /* Errors: a flag each, the oldest set first out (glGetError) */
    uint32_t error_flags;

    /* Between glBegin and glEnd: the primitive's mode and its vertices
       (prim.h) */
    GLenum           begin_mode;
    int              in_begin;
    struct ogl_prim *prim;
    struct ogl_draw *draw; /* the DRAW being built (draw.c) */
    struct ogl_lock *lock; /* the locked arrays' conversion (arrays.c), while locked */
    /* Work space of the context's own, made when first needed: three
       polygons' clipped vertices (raster.c), a map's values (eval.c) */
    struct ogl_cvtx *clip_buf;
    float           *map_buf;

    /* Display lists (lists.c): the list being compiled (0 for none) and
       its mode; the nesting of glCallList */
    GLuint list_compiling;
    GLenum list_mode;
    void  *list_open;
    int    list_nesting;
    int    list_replaying; /* a list runs: its commands are not recorded again */

    /* Render modes (raster.c): GL_RENDER, GL_SELECT, GL_FEEDBACK */
    GLenum   render_mode;
    GLuint  *select_buffer;
    GLsizei  select_size;
    GLuint   select_count; /* dwords written */
    GLuint   select_hits;
    int      select_overflow;
    int      select_hit;         /* a primitive drew since the name stack last changed */
    float    select_min, select_max;
    GLuint   name_stack[OGL_NAME_DEPTH];
    int      name_depth;
    GLfloat *feedback_buffer;
    GLsizei  feedback_size;
    GLenum   feedback_type;
    GLuint   feedback_count;
    int      feedback_overflow;

    /* The window's size at the last MakeCurrent (the first sets the
       viewport and the scissor box) */
    GLsizei win_w, win_h;

    /* The context's own textures (texobj.h): the defaults (name 0) of 1D
       and 2D, and the proxies */
    struct ogl_tex *default_tex[2];
    struct ogl_tex *proxy_tex[2];

    /* EXT_shared_texture_palette's palette (palette.c) */
    ogl_palette_t shared_pal;
};

/* The active texture unit's state (ACTIVE_TEXTURE_ARB's). */
#define OGL_UNIT(c) (&(c)->gl->texture.unit[(c)->gl->texture.active])

/* Do EXT_point_parameters' values change a point's size: any of them not
   its default (then GL 1.1's points stay exactly as they were)? */
static inline int
ogl_point_params_on(const ogl_raster_t *r)
{
    return (r->point_min != 0.0f) || (r->point_max != OGL_POINT_MAX) || (r->point_fade != 1.0f) ||
           (r->point_atten[0] != 1.0f) || (r->point_atten[1] != 0.0f) || (r->point_atten[2] != 0.0f);
}

/* The size of a point at eye distance d by EXT_point_parameters,
   and its alpha's factor (the fade below the threshold). */
float ogl_point_size(const ogl_raster_t *r, float d, float *fade);

/* --- Errors (state.c) --------------------------------------------------------- */

/* Records an error (its flag set if clear), named by the function that
   raised it at Diagnostics "OpenGL":
   ogl_error passes its caller's name. */
void ogl_error_at(ogl_ctx_t *c, GLenum error, const char *func);
#define ogl_error(c, error) ogl_error_at((c), (error), __func__)

/* GL's state for a new context, as the specification starts it. */
int  ogl_state_init(ogl_ctx_t *c);
void ogl_state_free(ogl_ctx_t *c);

/* Between glBegin and glEnd: most commands are an error there. Records
   GL_INVALID_OPERATION (named by the caller) and returns 1 when inside. */
static inline int
ogl_inside_at(ogl_ctx_t *c, const char *func)
{
    if (c->gl->in_begin) {
        ogl_error_at(c, GL_INVALID_OPERATION, func);
        return 1;
    }
    return 0;
}
#define ogl_inside(c) ogl_inside_at((c), __func__)

/* --- Across the ICD's files --------------------------------------------------- */

/* An evaluator target's components (GL_MAP1_* or GL_MAP2_*: k of its
   points), 0 for another name; its index 0-8 (eval.c). */
GLint ogl_map_components_of(GLenum target);
int   ogl_map_index(GLenum target, int *two);
int   ogl_map_components(int target_index);
/* The polygon stipple from 32 unpacked rows (misc.c). */
void  ogl_stipple_apply(ogl_ctx_t *c, const DWORD *rows);
/* The enable bit of a capability, 0 for none and for a texture unit's
   (enable.c). */
uint64_t ogl_cap_bit(GLenum cap);
/* Whether a capability is on (a texture unit's: the active unit's): 1, or
   0 for a name that is none (enable.c). */
int   ogl_cap_query(const ogl_state_t *s, GLenum cap, int *on);
/* The device registers an enable's change touches (enable.c). */
void  ogl_cap_regs(ogl_ctx_t *c, uint64_t bits);
/* wglCopyContext: the attribute groups of the mask from one context's
   state into another's (attrib.c). 0 without memory. */
int   ogl_attrib_copy(ogl_ctx_t *src, ogl_ctx_t *dst, GLbitfield mask);
/* The locked arrays' conversion dropped (glUnlockArraysEXT, the context's
   end); taken again for the lock the client state now has (its pop)
   (arrays.c). */
void  ogl_arrays_unlock(ogl_ctx_t *c);
void  ogl_arrays_lock_again(ogl_ctx_t *c);

#endif /* VGPU98_ICD_STATE_H */

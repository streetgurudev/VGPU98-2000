/*
 * glGetBooleanv, glGetIntegerv, glGetFloatv, glGetDoublev and glGetString
 * (GL 1.1, section 6.1): every state value the tables of section 6.2 list
 * for these four, each found once (query) and converted to the type asked
 * for by section 6.1.2's rules.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "glmath.h"
#include "state.h"

/* A value's kind: how the integer query converts it. */
#define K_BOOL   0 /* GL_TRUE or GL_FALSE */
#define K_INT    1 /* an integer or a name */
#define K_FLOAT  2 /* a float: rounded to the nearest integer */
#define K_MAPPED 3 /* a colour component, a normal or a depth value: -1 to 1 onto the integers' range */

typedef struct query {
    int    kind;
    int    n;
    double v[16];
} query_t;

static void
put(query_t *q, int kind, int n, ...)
{
    va_list ap;
    int     i;

    q->kind = kind;
    q->n    = n;
    va_start(ap, n);
    for (i = 0; i < n; i++)
        q->v[i] = va_arg(ap, double);
    va_end(ap);
}

static void
put_floats(query_t *q, int kind, int n, const float *v)
{
    int i;

    q->kind = kind;
    q->n    = n;
    for (i = 0; i < n; i++)
        q->v[i] = v[i];
}

#define B(x)       put(q, K_BOOL, 1, (double) ((x) ? 1 : 0))
#define I(x)       put(q, K_INT, 1, (double) (x))
#define F(x)       put(q, K_FLOAT, 1, (double) (x))
#define M4(v)      put_floats(q, K_MAPPED, 4, (v))
#define F4(v)      put_floats(q, K_FLOAT, 4, (v))

static const ogl_array_t *
array(const ogl_state_t *s, int a)
{
    return &s->client.array[a];
}

/* The value of pname: 1, or 0 when GL has no such value to get. */
static int
query(ogl_ctx_t *c, GLenum pname, query_t *q)
{
    const ogl_state_t *s = c->gl;
    const ogl_matrix_t *m;

    switch (pname) {
        /* --- Current values (table 6.5) --- */
        case GL_CURRENT_COLOR:
            M4(s->current.color);
            return 1;
        case GL_CURRENT_INDEX:
            F(s->current.index);
            return 1;
        case GL_CURRENT_TEXTURE_COORDS:
            F4(s->current.texcoord[s->texture.active]); /* the active unit's */
            return 1;
        case GL_CURRENT_NORMAL:
            put_floats(q, K_MAPPED, 3, s->current.normal);
            return 1;
        case GL_CURRENT_RASTER_POSITION:
            F4(s->current.raster_pos);
            return 1;
        case GL_CURRENT_RASTER_DISTANCE:
            F(s->current.raster_distance);
            return 1;
        case GL_CURRENT_RASTER_COLOR:
            M4(s->current.raster_color);
            return 1;
        case GL_CURRENT_RASTER_INDEX:
            F(s->current.raster_index);
            return 1;
        case GL_CURRENT_RASTER_TEXTURE_COORDS:
            F4(s->current.raster_texcoord[s->texture.active]);
            return 1;
        case GL_CURRENT_RASTER_POSITION_VALID:
            B(s->current.raster_valid);
            return 1;
        case GL_EDGE_FLAG:
            B(s->current.edge_flag);
            return 1;

        /* --- Vertex arrays (table 6.6) --- */
        case GL_VERTEX_ARRAY_SIZE:
            I(array(s, OGL_ARRAY_VERTEX)->size);
            return 1;
        case GL_VERTEX_ARRAY_TYPE:
            I(array(s, OGL_ARRAY_VERTEX)->type);
            return 1;
        case GL_VERTEX_ARRAY_STRIDE:
            I(array(s, OGL_ARRAY_VERTEX)->stride);
            return 1;
        case GL_NORMAL_ARRAY_TYPE:
            I(array(s, OGL_ARRAY_NORMAL)->type);
            return 1;
        case GL_NORMAL_ARRAY_STRIDE:
            I(array(s, OGL_ARRAY_NORMAL)->stride);
            return 1;
        case GL_COLOR_ARRAY_SIZE:
            I(array(s, OGL_ARRAY_COLOR)->size);
            return 1;
        case GL_COLOR_ARRAY_TYPE:
            I(array(s, OGL_ARRAY_COLOR)->type);
            return 1;
        case GL_COLOR_ARRAY_STRIDE:
            I(array(s, OGL_ARRAY_COLOR)->stride);
            return 1;
        case GL_INDEX_ARRAY_TYPE:
            I(array(s, OGL_ARRAY_INDEX)->type);
            return 1;
        case GL_INDEX_ARRAY_STRIDE:
            I(array(s, OGL_ARRAY_INDEX)->stride);
            return 1;
        case GL_TEXTURE_COORD_ARRAY_SIZE:
            I(array(s, OGL_ARRAY_TEX(s->client.client_active))->size);
            return 1;
        case GL_TEXTURE_COORD_ARRAY_TYPE:
            I(array(s, OGL_ARRAY_TEX(s->client.client_active))->type);
            return 1;
        case GL_TEXTURE_COORD_ARRAY_STRIDE:
            I(array(s, OGL_ARRAY_TEX(s->client.client_active))->stride);
            return 1;
        case GL_EDGE_FLAG_ARRAY_STRIDE:
            I(array(s, OGL_ARRAY_EDGEFLAG)->stride);
            return 1;
        case GL_VERTEX_ARRAY:
            B(array(s, OGL_ARRAY_VERTEX)->enabled);
            return 1;
        case GL_NORMAL_ARRAY:
            B(array(s, OGL_ARRAY_NORMAL)->enabled);
            return 1;
        case GL_COLOR_ARRAY:
            B(array(s, OGL_ARRAY_COLOR)->enabled);
            return 1;
        case GL_INDEX_ARRAY:
            B(array(s, OGL_ARRAY_INDEX)->enabled);
            return 1;
        case GL_TEXTURE_COORD_ARRAY:
            B(array(s, OGL_ARRAY_TEX(s->client.client_active))->enabled);
            return 1;
        case GL_EDGE_FLAG_ARRAY:
            B(array(s, OGL_ARRAY_EDGEFLAG)->enabled);
            return 1;
        case GL_ARRAY_ELEMENT_LOCK_FIRST_EXT:
            if (!ogl_ext_on(OGL_EXT_COMPILED_VERTEX_ARRAY))
                return 0;
            I(s->client.lock_first);
            return 1;
        case GL_ARRAY_ELEMENT_LOCK_COUNT_EXT:
            if (!ogl_ext_on(OGL_EXT_COMPILED_VERTEX_ARRAY))
                return 0;
            I(s->client.lock_count);
            return 1;

        /* --- Transformation (table 6.7) --- */
        case GL_MODELVIEW_MATRIX:
            m = &s->modelview[s->modelview_depth - 1];
            put_floats(q, K_FLOAT, 16, m->m);
            return 1;
        case GL_PROJECTION_MATRIX:
            m = &s->projection[s->projection_depth - 1];
            put_floats(q, K_FLOAT, 16, m->m);
            return 1;
        case GL_TEXTURE_MATRIX:
            m = &s->texmatrix[s->texture.active][s->texture_depth[s->texture.active] - 1];
            put_floats(q, K_FLOAT, 16, m->m);
            return 1;
        case GL_VIEWPORT:
            put(q, K_INT, 4, (double) s->viewport.x, (double) s->viewport.y, (double) s->viewport.w, (double) s->viewport.h);
            return 1;
        case GL_DEPTH_RANGE:
            put(q, K_MAPPED, 2, s->viewport.near_val, s->viewport.far_val);
            return 1;
        case GL_MODELVIEW_STACK_DEPTH:
            I(s->modelview_depth);
            return 1;
        case GL_PROJECTION_STACK_DEPTH:
            I(s->projection_depth);
            return 1;
        case GL_TEXTURE_STACK_DEPTH:
            I(s->texture_depth[s->texture.active]);
            return 1;
        case GL_MATRIX_MODE:
            I(s->transform.matrix_mode);
            return 1;

        /* --- Colouring (table 6.8) and lighting (table 6.9) --- */
        case GL_FOG_COLOR:
            M4(s->fog.color);
            return 1;
        case GL_FOG_INDEX:
            F(s->fog.index);
            return 1;
        case GL_FOG_DENSITY:
            F(s->fog.density);
            return 1;
        case GL_FOG_START:
            F(s->fog.start);
            return 1;
        case GL_FOG_END:
            F(s->fog.end);
            return 1;
        case GL_FOG_MODE:
            I(s->fog.mode);
            return 1;
        case GL_SHADE_MODEL:
            I(s->lighting.shade_model);
            return 1;
        case GL_COLOR_MATERIAL_PARAMETER:
            I(s->lighting.cm_mode);
            return 1;
        case GL_COLOR_MATERIAL_FACE:
            I(s->lighting.cm_face);
            return 1;
        case GL_LIGHT_MODEL_AMBIENT:
            M4(s->lighting.model_ambient);
            return 1;
        case GL_LIGHT_MODEL_LOCAL_VIEWER:
            B(s->lighting.local_viewer);
            return 1;
        case GL_LIGHT_MODEL_TWO_SIDE:
            B(s->lighting.two_side);
            return 1;

        /* --- Rasterization (table 6.10) --- */
        case GL_POINT_SIZE:
            F(s->raster.point_size);
            return 1;
        /* EXT_point_parameters' */
        case GL_POINT_SIZE_MIN_EXT:
        case GL_POINT_SIZE_MAX_EXT:
        case GL_POINT_FADE_THRESHOLD_SIZE_EXT:
            if (!ogl_ext_on(OGL_EXT_POINT_PARAMETERS))
                return 0;
            F((pname == GL_POINT_SIZE_MIN_EXT)   ? s->raster.point_min
              : (pname == GL_POINT_SIZE_MAX_EXT) ? s->raster.point_max
                                                 : s->raster.point_fade);
            return 1;
        case GL_DISTANCE_ATTENUATION_EXT:
            if (!ogl_ext_on(OGL_EXT_POINT_PARAMETERS))
                return 0;
            put_floats(q, K_FLOAT, 3, s->raster.point_atten);
            return 1;
        case GL_LINE_WIDTH:
            F(s->raster.line_width);
            return 1;
        case GL_LINE_STIPPLE_PATTERN:
            I(s->raster.line_pattern);
            return 1;
        case GL_LINE_STIPPLE_REPEAT:
            I(s->raster.line_repeat);
            return 1;
        case GL_CULL_FACE_MODE:
            I(s->raster.cull_mode);
            return 1;
        case GL_FRONT_FACE:
            I(s->raster.front_face);
            return 1;
        case GL_POLYGON_MODE:
            put(q, K_INT, 2, (double) s->raster.polygon_mode[0], (double) s->raster.polygon_mode[1]);
            return 1;
        case GL_POLYGON_OFFSET_FACTOR:
            F(s->raster.offset_factor);
            return 1;
        case GL_POLYGON_OFFSET_UNITS:
            F(s->raster.offset_units);
            return 1;

        /* --- Texturing (table 6.11) --- */
        case GL_TEXTURE_BINDING_1D:
            I(s->texture.unit[s->texture.active].bound[0]);
            return 1;
        case GL_TEXTURE_BINDING_2D:
            I(s->texture.unit[s->texture.active].bound[1]);
            return 1;
        /* The multitexture extensions' */
        case GL_ACTIVE_TEXTURE_ARB:
            if (!ogl_ext_on(OGL_EXT_ARB_MULTITEXTURE))
                return 0;
            I(GL_TEXTURE0_ARB + (GLenum) s->texture.active);
            return 1;
        case GL_CLIENT_ACTIVE_TEXTURE_ARB:
            if (!ogl_ext_on(OGL_EXT_ARB_MULTITEXTURE))
                return 0;
            I(GL_TEXTURE0_ARB + (GLenum) s->client.client_active);
            return 1;
        case GL_MAX_TEXTURE_UNITS_ARB:
            if (!ogl_ext_on(OGL_EXT_ARB_MULTITEXTURE))
                return 0;
            I(OGL_UNITS);
            return 1;

        /* --- Pixel operations (table 6.12) --- */
        case GL_SCISSOR_BOX:
            put(q, K_INT, 4, (double) s->scissor.x, (double) s->scissor.y, (double) s->scissor.w, (double) s->scissor.h);
            return 1;
        case GL_ALPHA_TEST_FUNC:
            I(s->color_buffer.alpha_func);
            return 1;
        case GL_ALPHA_TEST_REF:
            put(q, K_MAPPED, 1, (double) s->color_buffer.alpha_ref);
            return 1;
        case GL_STENCIL_FUNC:
            I(s->buffers.stencil_func);
            return 1;
        case GL_STENCIL_VALUE_MASK:
            I(s->buffers.stencil_value_mask);
            return 1;
        case GL_STENCIL_REF:
            I(s->buffers.stencil_ref);
            return 1;
        case GL_STENCIL_FAIL:
            I(s->buffers.stencil_fail);
            return 1;
        case GL_STENCIL_PASS_DEPTH_FAIL:
            I(s->buffers.stencil_zfail);
            return 1;
        case GL_STENCIL_PASS_DEPTH_PASS:
            I(s->buffers.stencil_zpass);
            return 1;
        case GL_DEPTH_FUNC:
            I(s->buffers.depth_func);
            return 1;
        case GL_BLEND_SRC:
            I(s->color_buffer.blend_src);
            return 1;
        case GL_BLEND_DST:
            I(s->color_buffer.blend_dst);
            return 1;
        case GL_LOGIC_OP_MODE:
            I(s->color_buffer.logic_op);
            return 1;

        /* --- Framebuffer control (table 6.13) --- */
        case GL_DRAW_BUFFER:
            I(s->color_buffer.draw_buffer);
            return 1;
        case GL_INDEX_WRITEMASK:
            I(s->color_buffer.index_mask);
            return 1;
        case GL_COLOR_WRITEMASK:
            put(q, K_BOOL, 4, (double) (s->color_buffer.color_mask[0] != 0), (double) (s->color_buffer.color_mask[1] != 0),
                (double) (s->color_buffer.color_mask[2] != 0), (double) (s->color_buffer.color_mask[3] != 0));
            return 1;
        case GL_DEPTH_WRITEMASK:
            B(s->buffers.depth_mask);
            return 1;
        case GL_STENCIL_WRITEMASK:
            I(s->buffers.stencil_write_mask);
            return 1;
        case GL_COLOR_CLEAR_VALUE:
            M4(s->color_buffer.clear_color);
            return 1;
        case GL_INDEX_CLEAR_VALUE:
            F(s->color_buffer.clear_index);
            return 1;
        case GL_DEPTH_CLEAR_VALUE:
            put(q, K_MAPPED, 1, s->buffers.depth_clear);
            return 1;
        case GL_STENCIL_CLEAR_VALUE:
            I(s->buffers.stencil_clear);
            return 1;
        case GL_ACCUM_CLEAR_VALUE:
            M4(s->buffers.accum_clear);
            return 1;

        /* --- Pixels (tables 6.14 and 6.15) --- */
        case GL_UNPACK_SWAP_BYTES:
            B(s->client.unpack.swap_bytes);
            return 1;
        case GL_UNPACK_LSB_FIRST:
            B(s->client.unpack.lsb_first);
            return 1;
        case GL_UNPACK_ROW_LENGTH:
            I(s->client.unpack.row_length);
            return 1;
        case GL_UNPACK_SKIP_ROWS:
            I(s->client.unpack.skip_rows);
            return 1;
        case GL_UNPACK_SKIP_PIXELS:
            I(s->client.unpack.skip_pixels);
            return 1;
        case GL_UNPACK_ALIGNMENT:
            I(s->client.unpack.alignment);
            return 1;
        case GL_PACK_SWAP_BYTES:
            B(s->client.pack.swap_bytes);
            return 1;
        case GL_PACK_LSB_FIRST:
            B(s->client.pack.lsb_first);
            return 1;
        case GL_PACK_ROW_LENGTH:
            I(s->client.pack.row_length);
            return 1;
        case GL_PACK_SKIP_ROWS:
            I(s->client.pack.skip_rows);
            return 1;
        case GL_PACK_SKIP_PIXELS:
            I(s->client.pack.skip_pixels);
            return 1;
        case GL_PACK_ALIGNMENT:
            I(s->client.pack.alignment);
            return 1;
        case GL_MAP_COLOR:
            B(s->pixel_mode.map_color);
            return 1;
        case GL_MAP_STENCIL:
            B(s->pixel_mode.map_stencil);
            return 1;
        case GL_INDEX_SHIFT:
            I(s->pixel_mode.index_shift);
            return 1;
        case GL_INDEX_OFFSET:
            I(s->pixel_mode.index_offset);
            return 1;
        case GL_RED_SCALE:
        case GL_GREEN_SCALE:
        case GL_BLUE_SCALE:
        case GL_ALPHA_SCALE:
            F(s->pixel_mode.scale[(pname == GL_RED_SCALE) ? 0 : (pname == GL_GREEN_SCALE) ? 1 : (pname == GL_BLUE_SCALE) ? 2 : 3]);
            return 1;
        case GL_RED_BIAS:
        case GL_GREEN_BIAS:
        case GL_BLUE_BIAS:
        case GL_ALPHA_BIAS:
            F(s->pixel_mode.bias[(pname == GL_RED_BIAS) ? 0 : (pname == GL_GREEN_BIAS) ? 1 : (pname == GL_BLUE_BIAS) ? 2 : 3]);
            return 1;
        case GL_DEPTH_SCALE:
            F(s->pixel_mode.depth_scale);
            return 1;
        case GL_DEPTH_BIAS:
            F(s->pixel_mode.depth_bias);
            return 1;
        case GL_ZOOM_X:
            F(s->pixel_mode.zoom_x);
            return 1;
        case GL_ZOOM_Y:
            F(s->pixel_mode.zoom_y);
            return 1;
        case GL_READ_BUFFER:
            I(s->pixel_mode.read_buffer);
            return 1;
        case GL_PIXEL_MAP_I_TO_I_SIZE:
        case GL_PIXEL_MAP_S_TO_S_SIZE:
        case GL_PIXEL_MAP_I_TO_R_SIZE:
        case GL_PIXEL_MAP_I_TO_G_SIZE:
        case GL_PIXEL_MAP_I_TO_B_SIZE:
        case GL_PIXEL_MAP_I_TO_A_SIZE:
        case GL_PIXEL_MAP_R_TO_R_SIZE:
        case GL_PIXEL_MAP_G_TO_G_SIZE:
        case GL_PIXEL_MAP_B_TO_B_SIZE:
        case GL_PIXEL_MAP_A_TO_A_SIZE:
            I(s->pixel_map[pname - GL_PIXEL_MAP_I_TO_I_SIZE].size);
            return 1;

        /* --- Evaluators (table 6.16) --- */
        case GL_MAP1_GRID_DOMAIN:
            put(q, K_FLOAT, 2, (double) s->eval.grid1_u1, (double) s->eval.grid1_u2);
            return 1;
        case GL_MAP2_GRID_DOMAIN:
            put(q, K_FLOAT, 4, (double) s->eval.grid2_u1, (double) s->eval.grid2_u2, (double) s->eval.grid2_v1,
                (double) s->eval.grid2_v2);
            return 1;
        case GL_MAP1_GRID_SEGMENTS:
            I(s->eval.grid1_n);
            return 1;
        case GL_MAP2_GRID_SEGMENTS:
            put(q, K_INT, 2, (double) s->eval.grid2_un, (double) s->eval.grid2_vn);
            return 1;

        /* --- Hints (table 6.17) --- */
        case GL_PERSPECTIVE_CORRECTION_HINT:
            I(s->hints.perspective);
            return 1;
        case GL_POINT_SMOOTH_HINT:
            I(s->hints.point_smooth);
            return 1;
        case GL_LINE_SMOOTH_HINT:
            I(s->hints.line_smooth);
            return 1;
        case GL_POLYGON_SMOOTH_HINT:
            I(s->hints.polygon_smooth);
            return 1;
        case GL_FOG_HINT:
            I(s->hints.fog);
            return 1;

        /* --- Implementation limits (tables 6.18 and 6.19) --- */
        case GL_MAX_LIGHTS:
            I(OGL_MAX_LIGHTS);
            return 1;
        case GL_MAX_CLIP_PLANES:
            I(OGL_MAX_CLIP_PLANES);
            return 1;
        case GL_MAX_MODELVIEW_STACK_DEPTH:
            I(OGL_MODELVIEW_DEPTH);
            return 1;
        case GL_MAX_PROJECTION_STACK_DEPTH:
            I(OGL_PROJECTION_DEPTH);
            return 1;
        case GL_MAX_TEXTURE_STACK_DEPTH:
            I(OGL_TEXTURE_DEPTH);
            return 1;
        case GL_SUBPIXEL_BITS:
            I(OGL_SUBPIXEL_BITS);
            return 1;
        case GL_MAX_TEXTURE_SIZE:
            I(OGL_MAX_TEXTURE);
            return 1;
        case GL_MAX_PIXEL_MAP_TABLE:
            I(OGL_PIXEL_MAP_SIZE);
            return 1;
        case GL_MAX_NAME_STACK_DEPTH:
            I(OGL_NAME_DEPTH);
            return 1;
        case GL_MAX_LIST_NESTING:
            I(OGL_LIST_NESTING);
            return 1;
        case GL_MAX_EVAL_ORDER:
            I(OGL_EVAL_ORDER);
            return 1;
        case GL_MAX_VIEWPORT_DIMS:
            put(q, K_INT, 2, (double) OGL_MAX_VIEWPORT, (double) OGL_MAX_VIEWPORT);
            return 1;
        case GL_MAX_ATTRIB_STACK_DEPTH:
            I(OGL_ATTRIB_DEPTH);
            return 1;
        case GL_MAX_CLIENT_ATTRIB_STACK_DEPTH:
            I(OGL_CLIENT_DEPTH);
            return 1;
        case GL_AUX_BUFFERS:
            I(0);
            return 1;
        case GL_RGBA_MODE:
            B(1);
            return 1;
        case GL_INDEX_MODE:
            B(0);
            return 1;
        case GL_DOUBLEBUFFER:
            B(c->format & VGPU98_OGL_DOUBLE);
            return 1;
        case GL_STEREO:
            B(0);
            return 1;
        case GL_POINT_SIZE_RANGE:
            put(q, K_FLOAT, 2, (double) OGL_POINT_MIN, (double) OGL_POINT_MAX);
            return 1;
        case GL_POINT_SIZE_GRANULARITY:
            F(OGL_GRANULARITY);
            return 1;
        case GL_LINE_WIDTH_RANGE:
            put(q, K_FLOAT, 2, (double) OGL_LINE_MIN, (double) OGL_LINE_MAX);
            return 1;
        case GL_LINE_WIDTH_GRANULARITY:
            F(OGL_GRANULARITY);
            return 1;
        case GL_RED_BITS:
            I(ogl_format_bits(c->format, OGL_BITS_RED));
            return 1;
        case GL_GREEN_BITS:
            I(ogl_format_bits(c->format, OGL_BITS_GREEN));
            return 1;
        case GL_BLUE_BITS:
            I(ogl_format_bits(c->format, OGL_BITS_BLUE));
            return 1;
        case GL_ALPHA_BITS:
            I(ogl_format_bits(c->format, OGL_BITS_ALPHA));
            return 1;
        case GL_INDEX_BITS:
            I(0);
            return 1;
        case GL_DEPTH_BITS:
            I(ogl_format_bits(c->format, OGL_BITS_DEPTH));
            return 1;
        case GL_STENCIL_BITS:
            I(ogl_format_bits(c->format, OGL_BITS_STENCIL));
            return 1;
        case GL_ACCUM_RED_BITS:
        case GL_ACCUM_GREEN_BITS:
        case GL_ACCUM_BLUE_BITS:
        case GL_ACCUM_ALPHA_BITS:
            I(0);
            return 1;

        /* --- Miscellaneous (table 6.20) --- */
        case GL_LIST_BASE:
            I(s->list_base);
            return 1;
        case GL_LIST_INDEX:
            I(s->list_compiling);
            return 1;
        case GL_LIST_MODE:
            I(s->list_mode);
            return 1;
        case GL_ATTRIB_STACK_DEPTH:
            I(s->attrib_depth);
            return 1;
        case GL_CLIENT_ATTRIB_STACK_DEPTH:
            I(s->client_depth);
            return 1;
        case GL_NAME_STACK_DEPTH:
            I(s->name_depth);
            return 1;
        case GL_RENDER_MODE:
            I(s->render_mode);
            return 1;
        case GL_SELECTION_BUFFER_SIZE:
            I(s->select_size);
            return 1;
        case GL_FEEDBACK_BUFFER_SIZE:
            I(s->feedback_size);
            return 1;
        case GL_FEEDBACK_BUFFER_TYPE:
            I(s->feedback_type);
            return 1;

        default: {
            /* Every enable is a value too (a texture unit's: the active
               unit's). */
            int on;

            if (!ogl_cap_query(s, pname, &on))
                return 0;
            B(on);
            return 1;
        }
    }
}

/* The query, with the errors: 0 when nothing is to be written. */
static int
get(ogl_ctx_t *c, GLenum pname, query_t *q)
{
    if (ogl_inside(c))
        return 0;
    if (!query(c, pname, q)) {
        ogl_error(c, GL_INVALID_ENUM);
        return 0;
    }
    return 1;
}

/* A mapped value as an integer: 1.0 to the largest, -1.0 to the smallest,
   0.0 to 0. */
static GLint
mapped_int(double v)
{
    if (v >= 1.0)
        return 0x7fffffff;
    if (v <= -1.0)
        return (GLint) 0x80000000u;
    return (GLint) ogl_round((v >= 0.0) ? v * 2147483647.0 : v * 2147483648.0);
}

void STDCALL
ogl_GetBooleanv(GLenum pname, GLboolean *params)
{
    query_t q;
    int     i;
    OGL_CTX();

    if (!get(c, pname, &q) || !params)
        return;
    for (i = 0; i < q.n; i++)
        params[i] = (q.v[i] != 0.0) ? GL_TRUE : GL_FALSE;
}

void STDCALL
ogl_GetIntegerv(GLenum pname, GLint *params)
{
    query_t q;
    int     i;
    OGL_CTX();

    if (!get(c, pname, &q) || !params)
        return;
    for (i = 0; i < q.n; i++) {
        switch (q.kind) {
            case K_MAPPED:
                params[i] = mapped_int(q.v[i]);
                break;
            case K_FLOAT:
                params[i] = ogl_round(q.v[i]);
                break;
            default:
                /* Names and masks above 2^31 keep their bits. */
                params[i] = (GLint) (DWORD) (int64_t) q.v[i];
                break;
        }
    }
}

void STDCALL
ogl_GetFloatv(GLenum pname, GLfloat *params)
{
    query_t q;
    int     i;
    OGL_CTX();

    if (!get(c, pname, &q) || !params)
        return;
    for (i = 0; i < q.n; i++)
        params[i] = (float) q.v[i];
}

void STDCALL
ogl_GetDoublev(GLenum pname, GLdouble *params)
{
    query_t q;
    int     i;
    OGL_CTX();

    if (!get(c, pname, &q) || !params)
        return;
    for (i = 0; i < q.n; i++)
        params[i] = q.v[i];
}

/* --- glGetString (section 6.1.11, GL_EXTENSIONS from ext.c) ----------------------------------- */

const GLubyte *STDCALL
ogl_GetString(GLenum name)
{
    OGL_CTX(NULL);

    if (ogl_inside(c))
        return NULL;
    switch (name) {
        case GL_VENDOR:
            return (const GLubyte *) "VGPU98";
        case GL_RENDERER:
            return (const GLubyte *) "VGPU98 2000/PCI";
        case GL_VERSION:
            return (const GLubyte *) "1.1.0";
        case GL_EXTENSIONS:
            return (const GLubyte *) ogl_ext_string();
        default:
            ogl_error(c, GL_INVALID_ENUM);
            return NULL;
    }
}

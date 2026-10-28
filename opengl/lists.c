/*
 * Display lists (lists.h): their names in the share group, compiling,
 * running, and GL's list calls (GL 1.1, section 5.4).
 *
 * A list is a run of words: each operation a header (its number and its
 * words' count) and its words, its client data after them. A list being
 * compiled is apart until glEndList, which puts it in place of the name's
 * old one.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "lists.h"
#include "pixel.h"

#define BUCKETS 256

typedef struct ogl_list {
    GLuint           name;
    DWORD           *w;
    DWORD            n, cap;
    struct ogl_list *next;
} ogl_list_t;

typedef struct ogl_lists {
    ogl_list_t *bucket[BUCKETS];
    GLuint      top; /* the highest name in use */
} ogl_lists_t;

static ogl_lists_t *
lists_of(ogl_ctx_t *c, int make)
{
    ogl_group_t *g = c->group;

    if (!g->lists && make)
        g->lists = ogl_alloc(sizeof(ogl_lists_t));
    return (ogl_lists_t *) g->lists;
}

static ogl_list_t *
find(ogl_lists_t *ls, GLuint name)
{
    ogl_list_t *l;

    if (!ls)
        return NULL;
    for (l = ls->bucket[name % BUCKETS]; l; l = l->next) {
        if (l->name == name)
            return l;
    }
    return NULL;
}

static ogl_list_t *
insert(ogl_lists_t *ls, GLuint name)
{
    ogl_list_t *l = ogl_alloc(sizeof(ogl_list_t));

    if (!l)
        return NULL;
    l->name                   = name;
    l->next                   = ls->bucket[name % BUCKETS];
    ls->bucket[name % BUCKETS] = l;
    if (name > ls->top)
        ls->top = name;
    return l;
}

static void
delete_one(ogl_lists_t *ls, GLuint name)
{
    ogl_list_t **at = &ls->bucket[name % BUCKETS];

    while (*at) {
        if ((*at)->name == name) {
            ogl_list_t *l = *at;

            *at = l->next;
            ogl_free(l->w);
            ogl_free(l);
            return;
        }
        at = &(*at)->next;
    }
}

void
ogl_dl_group_free(ogl_group_t *g)
{
    ogl_lists_t *ls = g->lists;
    int          i;

    if (!ls)
        return;
    for (i = 0; i < BUCKETS; i++) {
        while (ls->bucket[i]) {
            ogl_list_t *l = ls->bucket[i];

            ls->bucket[i] = l->next;
            ogl_free(l->w);
            ogl_free(l);
        }
    }
    ogl_free(ls);
    g->lists = NULL;
}

void
ogl_dl_ctx_free(ogl_ctx_t *c)
{
    ogl_list_t *open = c->gl->list_open;

    if (!open)
        return;
    ogl_free(open->w);
    ogl_free(open);
    c->gl->list_open      = NULL;
    c->gl->list_compiling = 0;
}

/* --- Recording ---------------------------------------------------------------------- */

static int
grow(ogl_ctx_t *c, ogl_list_t *l, DWORD more)
{
    DWORD cap = l->cap ? l->cap : 64;
    DWORD *w;

    if (l->n + more <= l->cap)
        return 1;
    while (cap < l->n + more) {
        if (cap > 0x10000000u) {
            ogl_error(c, GL_OUT_OF_MEMORY);
            return 0;
        }
        cap *= 2;
    }
    w = ogl_realloc(l->w, cap * 4);
    if (!w) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return 0;
    }
    l->w   = w;
    l->cap = cap;
    return 1;
}

int
ogl_dl_data(ogl_ctx_t *c, int op, const DWORD *words, DWORD n, const void *data, DWORD bytes)
{
    ogl_list_t *l     = c->gl->list_open;
    const DWORD extra = (bytes + 3) / 4;

    if (l && (n + extra < 0xffff) && grow(c, l, 1 + n + extra)) {
        l->w[l->n] = (DWORD) op | ((n + extra) << 16);
        if (n)
            memcpy(&l->w[l->n + 1], words, n * 4);
        if (bytes) {
            l->w[l->n + n + extra] = 0;
            memcpy(&l->w[l->n + 1 + n], data, bytes);
        }
        l->n += 1 + n + extra;
    } else if (l && (n + extra >= 0xffff)) {
        /* Data past one operation's 65535 words: a chained header gives the
           length in the next word. */
        if (grow(c, l, 2 + n + extra)) {
            l->w[l->n]     = (DWORD) op | 0xffff0000u;
            l->w[l->n + 1] = n + extra;
            if (n)
                memcpy(&l->w[l->n + 2], words, n * 4);
            if (bytes) {
                l->w[l->n + 1 + n + extra] = 0;
                memcpy(&l->w[l->n + 2 + n], data, bytes);
            }
            l->n += 2 + n + extra;
        }
    }
    return c->gl->list_mode == GL_COMPILE;
}

int
ogl_dl(ogl_ctx_t *c, int op, const DWORD *words, DWORD n)
{
    return ogl_dl_data(c, op, words, n, NULL, 0);
}

/* --- Running --------------------------------------------------------------------- */

static float
F(const DWORD *w, int i)
{
    return ogl_wf(w[i]);
}

static void
run_op(ogl_ctx_t *c, DWORD op, const DWORD *w, DWORD n)
{
    (void) n;
    switch (op) {
        case OP_ACCUM:
            ogl_Accum(w[0], F(w, 1));
            break;
        case OP_ALPHAFUNC:
            ogl_AlphaFunc(w[0], F(w, 1));
            break;
        case OP_BEGIN:
            ogl_Begin(w[0]);
            break;
        case OP_END:
            ogl_End();
            break;
        case OP_BINDTEXTURE:
            ogl_BindTexture(w[0], w[1]);
            break;
        case OP_BITMAP:
            ogl_bitmap_apply(c, (GLsizei) w[0], (GLsizei) w[1], F(w, 2), F(w, 3), F(w, 4), F(w, 5), (const uint8_t *) &w[6]);
            break;
        case OP_BLENDFUNC:
            ogl_BlendFunc(w[0], w[1]);
            break;
        case OP_CALLLIST:
            ogl_dl_call(c, w[0]);
            break;
        case OP_CALLLISTS: {
            DWORD i;

            for (i = 0; i < w[0]; i++)
                ogl_dl_call(c, c->gl->list_base + w[1 + i]);
            break;
        }
        case OP_CLEAR:
            ogl_Clear(w[0]);
            break;
        case OP_CLEARACCUM:
            ogl_ClearAccum(F(w, 0), F(w, 1), F(w, 2), F(w, 3));
            break;
        case OP_CLEARCOLOR:
            ogl_ClearColor(F(w, 0), F(w, 1), F(w, 2), F(w, 3));
            break;
        case OP_CLEARDEPTH:
            ogl_ClearDepth(ogl_wd(&w[0]));
            break;
        case OP_CLEARINDEX:
            ogl_ClearIndex(F(w, 0));
            break;
        case OP_CLEARSTENCIL:
            ogl_ClearStencil((GLint) w[0]);
            break;
        case OP_CLIPPLANE: {
            double eq[4];

            eq[0] = ogl_wd(&w[1]);
            eq[1] = ogl_wd(&w[3]);
            eq[2] = ogl_wd(&w[5]);
            eq[3] = ogl_wd(&w[7]);
            ogl_ClipPlane(w[0], eq);
            break;
        }
        case OP_COLOR:
            ogl_Color4f(F(w, 0), F(w, 1), F(w, 2), F(w, 3));
            break;
        case OP_COLORMASK:
            ogl_ColorMask((GLboolean) w[0], (GLboolean) w[1], (GLboolean) w[2], (GLboolean) w[3]);
            break;
        case OP_COLORMATERIAL:
            ogl_ColorMaterial(w[0], w[1]);
            break;
        case OP_COPYPIXELS:
            ogl_CopyPixels((GLint) w[0], (GLint) w[1], (GLsizei) w[2], (GLsizei) w[3], w[4]);
            break;
        case OP_COPYTEXIMAGE1D:
            ogl_CopyTexImage1D(w[0], (GLint) w[1], w[2], (GLint) w[3], (GLint) w[4], (GLsizei) w[5], (GLint) w[6]);
            break;
        case OP_COPYTEXIMAGE2D:
            ogl_CopyTexImage2D(w[0], (GLint) w[1], w[2], (GLint) w[3], (GLint) w[4], (GLsizei) w[5], (GLsizei) w[6],
                               (GLint) w[7]);
            break;
        case OP_COPYTEXSUB1D:
            ogl_CopyTexSubImage1D(w[0], (GLint) w[1], (GLint) w[2], (GLint) w[3], (GLint) w[4], (GLsizei) w[5]);
            break;
        case OP_COPYTEXSUB2D:
            ogl_CopyTexSubImage2D(w[0], (GLint) w[1], (GLint) w[2], (GLint) w[3], (GLint) w[4], (GLint) w[5],
                                  (GLsizei) w[6], (GLsizei) w[7]);
            break;
        case OP_CULLFACE:
            ogl_CullFace(w[0]);
            break;
        case OP_DEPTHFUNC:
            ogl_DepthFunc(w[0]);
            break;
        case OP_DEPTHMASK:
            ogl_DepthMask((GLboolean) w[0]);
            break;
        case OP_DEPTHRANGE:
            ogl_DepthRange(ogl_wd(&w[0]), ogl_wd(&w[2]));
            break;
        case OP_DISABLE:
            ogl_Disable(w[0]);
            break;
        case OP_ENABLE:
            ogl_Enable(w[0]);
            break;
        case OP_DRAWBUFFER:
            ogl_DrawBuffer(w[0]);
            break;
        case OP_DRAWPIXELS: {
            ogl_pixels_t p;

            p.w      = (GLsizei) w[0];
            p.h      = (GLsizei) w[1];
            p.format = w[2];
            p.comps  = (int) w[3];
            p.v      = (float *) &w[4];
            p.owned  = 0;
            ogl_drawpixels_apply(c, &p);
            break;
        }
        case OP_EDGEFLAG:
            ogl_EdgeFlag((GLboolean) w[0]);
            break;
        case OP_EVALCOORD1:
            ogl_EvalCoord1f(F(w, 0));
            break;
        case OP_EVALCOORD2:
            ogl_EvalCoord2f(F(w, 0), F(w, 1));
            break;
        case OP_EVALMESH1:
            ogl_EvalMesh1(w[0], (GLint) w[1], (GLint) w[2]);
            break;
        case OP_EVALMESH2:
            ogl_EvalMesh2(w[0], (GLint) w[1], (GLint) w[2], (GLint) w[3], (GLint) w[4]);
            break;
        case OP_EVALPOINT1:
            ogl_EvalPoint1((GLint) w[0]);
            break;
        case OP_EVALPOINT2:
            ogl_EvalPoint2((GLint) w[0], (GLint) w[1]);
            break;
        case OP_FOG:
            ogl_Fogfv(w[0], (const GLfloat *) &w[1]);
            break;
        case OP_FRONTFACE:
            ogl_FrontFace(w[0]);
            break;
        case OP_FRUSTUM:
            ogl_Frustum(ogl_wd(&w[0]), ogl_wd(&w[2]), ogl_wd(&w[4]), ogl_wd(&w[6]), ogl_wd(&w[8]), ogl_wd(&w[10]));
            break;
        case OP_HINT:
            ogl_Hint(w[0], w[1]);
            break;
        case OP_INDEX:
            ogl_Indexf(F(w, 0));
            break;
        case OP_INDEXMASK:
            ogl_IndexMask(w[0]);
            break;
        case OP_INITNAMES:
            ogl_InitNames();
            break;
        case OP_LIGHT:
            ogl_Lightfv(w[0], w[1], (const GLfloat *) &w[2]);
            break;
        case OP_LIGHTMODEL:
            ogl_LightModelfv(w[0], (const GLfloat *) &w[1]);
            break;
        case OP_LINESTIPPLE:
            ogl_LineStipple((GLint) w[0], (GLushort) w[1]);
            break;
        case OP_LINEWIDTH:
            ogl_LineWidth(F(w, 0));
            break;
        case OP_LISTBASE:
            ogl_ListBase(w[0]);
            break;
        case OP_LOADIDENTITY:
            ogl_LoadIdentity();
            break;
        case OP_LOADMATRIX:
            ogl_LoadMatrixf((const GLfloat *) w);
            break;
        case OP_LOADNAME:
            ogl_LoadName(w[0]);
            break;
        case OP_LOGICOP:
            ogl_LogicOp(w[0]);
            break;
        case OP_MAP1:
            ogl_Map1f(w[0], F(w, 1), F(w, 2), ogl_map_components_of(w[0]), (GLint) w[3], (const GLfloat *) &w[4]);
            break;
        case OP_MAP2: {
            const GLint k = ogl_map_components_of(w[0]);

            ogl_Map2f(w[0], F(w, 1), F(w, 2), k * (GLint) w[6], (GLint) w[3], F(w, 4), F(w, 5), k, (GLint) w[6],
                      (const GLfloat *) &w[7]);
            break;
        }
        case OP_MAPGRID1:
            ogl_MapGrid1f((GLint) w[0], F(w, 1), F(w, 2));
            break;
        case OP_MAPGRID2:
            ogl_MapGrid2f((GLint) w[0], F(w, 1), F(w, 2), (GLint) w[3], F(w, 4), F(w, 5));
            break;
        case OP_MATERIAL:
            ogl_Materialfv(w[0], w[1], (const GLfloat *) &w[2]);
            break;
        case OP_MATRIXMODE:
            ogl_MatrixMode(w[0]);
            break;
        case OP_MULTMATRIX:
            ogl_MultMatrixf((const GLfloat *) w);
            break;
        case OP_NORMAL:
            ogl_Normal3f(F(w, 0), F(w, 1), F(w, 2));
            break;
        case OP_ORTHO:
            ogl_Ortho(ogl_wd(&w[0]), ogl_wd(&w[2]), ogl_wd(&w[4]), ogl_wd(&w[6]), ogl_wd(&w[8]), ogl_wd(&w[10]));
            break;
        case OP_PASSTHROUGH:
            ogl_PassThrough(F(w, 0));
            break;
        case OP_PIXELMAP:
            ogl_PixelMapfv(w[0], (GLsizei) w[1], (const GLfloat *) &w[2]);
            break;
        case OP_PIXELTRANSFER:
            ogl_PixelTransferf(w[0], F(w, 1));
            break;
        case OP_PIXELZOOM:
            ogl_PixelZoom(F(w, 0), F(w, 1));
            break;
        case OP_POINTSIZE:
            ogl_PointSize(F(w, 0));
            break;
        case OP_POLYGONMODE:
            ogl_PolygonMode(w[0], w[1]);
            break;
        case OP_POLYGONOFFSET:
            ogl_PolygonOffset(F(w, 0), F(w, 1));
            break;
        case OP_POLYGONSTIPPLE:
            ogl_stipple_apply(c, w);
            break;
        case OP_POPATTRIB:
            ogl_PopAttrib();
            break;
        case OP_POPMATRIX:
            ogl_PopMatrix();
            break;
        case OP_POPNAME:
            ogl_PopName();
            break;
        case OP_PRIORITIZE:
            ogl_PrioritizeTextures((GLsizei) w[0], &w[1], (const GLclampf *) &w[1 + w[0]]);
            break;
        case OP_PUSHATTRIB:
            ogl_PushAttrib(w[0]);
            break;
        case OP_PUSHMATRIX:
            ogl_PushMatrix();
            break;
        case OP_PUSHNAME:
            ogl_PushName(w[0]);
            break;
        case OP_RASTERPOS:
            ogl_RasterPos4f(F(w, 0), F(w, 1), F(w, 2), F(w, 3));
            break;
        case OP_READBUFFER:
            ogl_ReadBuffer(w[0]);
            break;
        case OP_RECT:
            ogl_Rectf(F(w, 0), F(w, 1), F(w, 2), F(w, 3));
            break;
        case OP_ROTATE:
            ogl_Rotatef(F(w, 0), F(w, 1), F(w, 2), F(w, 3));
            break;
        case OP_SCALE:
            ogl_Scalef(F(w, 0), F(w, 1), F(w, 2));
            break;
        case OP_SCISSOR:
            ogl_Scissor((GLint) w[0], (GLint) w[1], (GLsizei) w[2], (GLsizei) w[3]);
            break;
        case OP_SHADEMODEL:
            ogl_ShadeModel(w[0]);
            break;
        case OP_STENCILFUNC:
            ogl_StencilFunc(w[0], (GLint) w[1], w[2]);
            break;
        case OP_STENCILMASK:
            ogl_StencilMask(w[0]);
            break;
        case OP_STENCILOP:
            ogl_StencilOp(w[0], w[1], w[2]);
            break;
        case OP_TEXCOORD:
            ogl_TexCoord4f(F(w, 0), F(w, 1), F(w, 2), F(w, 3));
            break;
        case OP_TEXENV:
            ogl_TexEnvfv(w[0], w[1], (const GLfloat *) &w[2]);
            break;
        case OP_TEXGEN: {
            double v[4];

            v[0] = ogl_wd(&w[2]);
            v[1] = ogl_wd(&w[4]);
            v[2] = ogl_wd(&w[6]);
            v[3] = ogl_wd(&w[8]);
            ogl_TexGendv(w[0], w[1], v);
            break;
        }
        case OP_TEXIMAGE: {
            ogl_pixels_t p;

            p.w      = (GLsizei) w[4];
            p.h      = (GLsizei) w[5];
            p.format = w[7];
            p.comps  = (int) w[8];
            p.v      = (float *) &w[9];
            p.owned  = 0;
            ogl_teximage_apply(c, (int) w[0], w[1], (GLint) w[2], (GLint) w[3], p.w, p.h, (GLint) w[6], &p);
            break;
        }
        case OP_TEXSUBIMAGE: {
            ogl_pixels_t p;

            p.w      = (GLsizei) w[5];
            p.h      = (GLsizei) w[6];
            p.format = w[7];
            p.comps  = (int) w[8];
            p.v      = (float *) &w[9];
            p.owned  = 0;
            ogl_texsubimage_apply(c, (int) w[0], w[1], (GLint) w[2], (GLint) w[3], (GLint) w[4], &p);
            break;
        }
        case OP_TEXPARAMETER:
            ogl_TexParameterfv(w[0], w[1], (const GLfloat *) &w[2]);
            break;
        case OP_TRANSLATE:
            ogl_Translatef(F(w, 0), F(w, 1), F(w, 2));
            break;
        case OP_VERTEX:
            ogl_Vertex4f(F(w, 0), F(w, 1), F(w, 2), F(w, 3));
            break;
        case OP_VIEWPORT:
            ogl_Viewport((GLint) w[0], (GLint) w[1], (GLsizei) w[2], (GLsizei) w[3]);
            break;
        case OP_ACTIVETEXTURE:
            ogl_active_texture(c, (int) w[0]);
            break;
        case OP_MULTITEXCOORD:
            ogl_multitexcoord(c, (int) w[0], F(w, 1), F(w, 2), F(w, 3), F(w, 4));
            break;
        case OP_COLORTABLE:
        case OP_COLORSUBTABLE: {
            ogl_pixels_t p;

            p.w      = (GLsizei) w[2];
            p.h      = 1;
            p.format = w[3];
            p.comps  = (int) w[4];
            p.v      = (float *) &w[5];
            p.owned  = 0;
            if (op == OP_COLORTABLE)
                ogl_colortable_apply(c, w[0], (GLint) w[1], (GLsizei) w[2], &p);
            else
                ogl_colorsubtable_apply(c, w[0], (GLsizei) w[1], (GLsizei) w[2], &p);
            break;
        }
        case OP_POINTPARAM:
            if (w[4])
                ogl_PointParameterfEXT(w[0], F(w, 1));
            else
                ogl_PointParameterfvEXT(w[0], (const GLfloat *) &w[1]);
            break;
        default:
            break;
    }
}

void
ogl_dl_call(ogl_ctx_t *c, GLuint name)
{
    ogl_state_t *s = c->gl;
    ogl_list_t  *l = find(lists_of(c, 0), name);
    DWORD        i;

    if (!l || (s->list_nesting >= OGL_LIST_NESTING))
        return;
    ogl_dg.lists++;
    s->list_nesting++;
    s->list_replaying++;
    for (i = 0; i < l->n;) {
        const DWORD op = l->w[i] & 0xffff;
        DWORD       n  = l->w[i] >> 16;
        DWORD       at = i + 1;

        if (n == 0xffff) {
            n  = l->w[i + 1];
            at = i + 2;
        }
        run_op(c, op, &l->w[at], n);
        /* The list may have been replaced or deleted meanwhile (a list's
           own commands cannot do that: glNewList and glDeleteLists are not
           compiled), but its group's table may have been: look again. */
        if (find(lists_of(c, 0), name) != l)
            break;
        i = at + n;
    }
    s->list_replaying--;
    s->list_nesting--;
}

/* --- GL's list calls --------------------------------------------------------------- */

void STDCALL
ogl_NewList(GLuint list, GLenum mode)
{
    ogl_state_t *s;
    ogl_list_t  *l;
    OGL_CTX();

    s = c->gl;
    if (!list) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if ((mode != GL_COMPILE) && (mode != GL_COMPILE_AND_EXECUTE)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (s->in_begin || s->list_compiling) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    l = ogl_alloc(sizeof(ogl_list_t));
    if (!l) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    l->name           = list;
    s->list_open      = l;
    s->list_compiling = list;
    s->list_mode      = mode;
}

void STDCALL
ogl_EndList(void)
{
    ogl_state_t *s;
    ogl_lists_t *ls;
    ogl_list_t  *open, *old;
    OGL_CTX();

    s = c->gl;
    if (!s->list_compiling || s->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    open              = s->list_open;
    s->list_open      = NULL;
    s->list_compiling = 0;
    ls                = lists_of(c, 1);
    if (!ls) {
        ogl_free(open->w);
        ogl_free(open);
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    /* In place of the name's old list. */
    old = find(ls, open->name);
    if (old) {
        ogl_free(old->w);
        old->w   = open->w;
        old->n   = open->n;
        old->cap = open->cap;
        ogl_free(open);
    } else {
        ogl_list_t *l = insert(ls, open->name);

        if (!l) {
            ogl_free(open->w);
            ogl_free(open);
            ogl_error(c, GL_OUT_OF_MEMORY);
            return;
        }
        l->w   = open->w;
        l->n   = open->n;
        l->cap = open->cap;
        ogl_free(open);
    }
}

void STDCALL
ogl_CallList(GLuint list)
{
    OGL_CTX();
    OGL_DL(OP_CALLLIST, list);
    ogl_dl_call(c, list);
}

/* glCallLists' names, each by its type; 0 for a type GL does not have. */
static int
list_names(GLsizei n, GLenum type, const GLvoid *lists, DWORD *out)
{
    const uint8_t *b = lists;
    GLsizei        i;

    for (i = 0; i < n; i++) {
        switch (type) {
            case GL_BYTE:
                out[i] = (DWORD) (int32_t) ((const GLbyte *) lists)[i];
                break;
            case GL_UNSIGNED_BYTE:
                out[i] = b[i];
                break;
            case GL_SHORT: {
                GLshort v;

                memcpy(&v, b + 2 * i, 2);
                out[i] = (DWORD) (int32_t) v;
                break;
            }
            case GL_UNSIGNED_SHORT: {
                GLushort v;

                memcpy(&v, b + 2 * i, 2);
                out[i] = v;
                break;
            }
            case GL_INT:
            case GL_UNSIGNED_INT:
                memcpy(&out[i], b + 4 * i, 4);
                break;
            case GL_FLOAT: {
                GLfloat v;

                memcpy(&v, b + 4 * i, 4);
                out[i] = (DWORD) (int32_t) v;
                break;
            }
            case GL_2_BYTES:
                out[i] = ((DWORD) b[2 * i] << 8) | b[2 * i + 1];
                break;
            case GL_3_BYTES:
                out[i] = ((DWORD) b[3 * i] << 16) | ((DWORD) b[3 * i + 1] << 8) | b[3 * i + 2];
                break;
            case GL_4_BYTES:
                out[i] = ((DWORD) b[4 * i] << 24) | ((DWORD) b[4 * i + 1] << 16) | ((DWORD) b[4 * i + 2] << 8) | b[4 * i + 3];
                break;
            default:
                return 0;
        }
    }
    return 1;
}

void STDCALL
ogl_CallLists(GLsizei n, GLenum type, const GLvoid *lists)
{
    DWORD  *names;
    GLsizei i;
    OGL_CTX();

    if (n < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (!n)
        return;
    names = ogl_alloc((DWORD) (n + 1) * 4);
    if (!names) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    if (!lists || !list_names(n, type, lists, names + 1)) {
        ogl_free(names);
        ogl_error(c, lists ? GL_INVALID_ENUM : GL_INVALID_VALUE);
        return;
    }
    names[0] = (DWORD) n;
    if (ogl_dl_on(c) && ogl_dl_data(c, OP_CALLLISTS, names, 1, names + 1, (DWORD) n * 4)) {
        ogl_free(names);
        return;
    }
    for (i = 0; i < n; i++)
        ogl_dl_call(c, c->gl->list_base + names[1 + i]);
    ogl_free(names);
}

void STDCALL
ogl_DeleteLists(GLuint list, GLsizei range)
{
    ogl_lists_t *ls;
    GLsizei      i;
    OGL_CTX();

    if (range < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (c->gl->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    ls = lists_of(c, 0);
    if (!ls)
        return;
    /* A long range: the lists there, found through the table. */
    if (range > 4096) {
        int b;

        for (b = 0; b < BUCKETS; b++) {
            ogl_list_t **at = &ls->bucket[b];

            while (*at) {
                if ((*at)->name - list < (GLuint) range) {
                    ogl_list_t *l = *at;

                    *at = l->next;
                    ogl_free(l->w);
                    ogl_free(l);
                } else
                    at = &(*at)->next;
            }
        }
        return;
    }
    for (i = 0; (i < range) && (list + (GLuint) i >= list); i++)
        delete_one(ls, list + (GLuint) i);
}

GLuint STDCALL
ogl_GenLists(GLsizei range)
{
    ogl_lists_t *ls;
    GLuint       first;
    GLsizei      i;
    OGL_CTX(0);

    if (range < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return 0;
    }
    if (c->gl->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return 0;
    }
    /* No run of more than a million names is given (GL answers 0 when it
       has no run that long). */
    if (!range || (range > 0x100000))
        return 0;
    ls = lists_of(c, 1);
    if (!ls) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return 0;
    }
    /* Past the highest name in use, else the first free run from 1. */
    first = ls->top + 1;
    if (!first || (first + (GLuint) range - 1 < first)) {
        GLuint run = 0;

        for (first = 1; first; first++) {
            run = find(ls, first) ? 0 : run + 1;
            if (run == (GLuint) range) {
                first -= (GLuint) range - 1;
                break;
            }
        }
        if (!first)
            return 0;
    }
    /* Each name of the run an empty list (GL 1.1: IsList answers TRUE). */
    for (i = 0; i < range; i++) {
        if (!insert(ls, first + (GLuint) i)) {
            ogl_error(c, GL_OUT_OF_MEMORY);
            return 0;
        }
    }
    return first;
}

void STDCALL
ogl_ListBase(GLuint base)
{
    OGL_CTX();
    OGL_DL(OP_LISTBASE, base);
    if (c->gl->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    c->gl->list_base = base;
}

GLboolean STDCALL
ogl_IsList(GLuint list)
{
    OGL_CTX(GL_FALSE);
    if (c->gl->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return GL_FALSE;
    }
    return find(lists_of(c, 0), list) ? GL_TRUE : GL_FALSE;
}

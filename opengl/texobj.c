/*
 * Texture objects (texobj.h): names and bindings, parameters, priorities
 * and residency, the level queries, completeness, and the texture the
 * device draws with (GL 1.1, section 3.8).
 *
 * A texture's image lives on the device (teximage.c sends it); the ICD
 * keeps its levels' sizes and formats, what the queries and completeness
 * need. Every texture is resident: the device keeps them all.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "glmath.h"
#include "lists.h"
#include "regs.h"
#include "texobj.h"

#define BUCKETS 256

typedef struct ogl_textures {
    ogl_tex_t *bucket[BUCKETS];
    GLuint     next; /* where glGenTextures looks next */
} ogl_textures_t;

static void
params_default(ogl_texparams_t *p)
{
    memset(p, 0, sizeof(*p));
    p->min_filter = GL_NEAREST_MIPMAP_LINEAR;
    p->mag_filter = GL_LINEAR;
    p->wrap_s     = GL_REPEAT;
    p->wrap_t     = GL_REPEAT;
    p->priority   = 1.0f;
}

static ogl_tex_t *
tex_new(GLuint name, GLenum target)
{
    ogl_tex_t *t = ogl_alloc(sizeof(ogl_tex_t));

    if (!t)
        return NULL;
    t->name   = name;
    t->target = target;
    params_default(&t->p);
    ogl_palette_init(&t->pal);
    return t;
}

static ogl_textures_t *
table(ogl_ctx_t *c, int make)
{
    if (!c->group->textures && make)
        c->group->textures = ogl_alloc(sizeof(ogl_textures_t));
    return c->group->textures;
}

static ogl_tex_t *
find(ogl_ctx_t *c, GLuint name)
{
    ogl_textures_t *ts = table(c, 0);
    ogl_tex_t      *t;

    if (!ts)
        return NULL;
    for (t = ts->bucket[name % BUCKETS]; t; t = t->next) {
        if (t->name == name)
            return t;
    }
    return NULL;
}

static int
target_index(GLenum target)
{
    return (target == GL_TEXTURE_1D) ? 0 : (target == GL_TEXTURE_2D) ? 1 : -1;
}

ogl_tex_t *
ogl_tex_bound_unit(ogl_ctx_t *c, int unit, GLenum target)
{
    const int      i = target_index(target);
    ogl_state_t   *s = c->gl;
    ogl_texunit_t *u = &s->texture.unit[unit];
    ogl_tex_t     *t;

    if (i < 0)
        return NULL;
    if (u->bound[i]) {
        t = find(c, u->bound[i]);
        if (t)
            return t;
        u->bound[i] = 0; /* deleted meanwhile (by another context of the group) */
    }
    /* Name 0: the context's default texture of the target, every unit's */
    if (!s->default_tex[i])
        s->default_tex[i] = tex_new(0, target);
    return s->default_tex[i];
}

ogl_tex_t *
ogl_tex_bound(ogl_ctx_t *c, GLenum target)
{
    return ogl_tex_bound_unit(c, c->gl->texture.active, target);
}

static void
tex_free(ogl_tex_t *t)
{
    ogl_free(t);
}

void
ogl_tex_group_free(ogl_group_t *g)
{
    ogl_textures_t *ts = g->textures;
    int             i;

    if (!ts)
        return;
    for (i = 0; i < BUCKETS; i++) {
        while (ts->bucket[i]) {
            ogl_tex_t *t = ts->bucket[i];

            ts->bucket[i] = t->next;
            tex_free(t);
        }
    }
    ogl_free(ts);
    g->textures = NULL;
}

static void tex_id_free(ogl_group_t *g, DWORD id);

void
ogl_tex_ctx_release(ogl_ctx_t *c, int send)
{
    int i;

    for (i = 0; i < 2; i++) {
        ogl_tex_t *t = c->gl->default_tex[i];

        if (!t || !t->dev)
            continue;
        if (send && c->group->opened) {
            DWORD *p = ogl_cmd(c, VGPU98_OGL_TEXDELETE, VGPU98_OGL_LEN_TEXDELETE);

            if (p) {
                p[0] = c->group->id;
                p[1] = t->dev;
            }
        }
        tex_id_free(c->group, t->dev);
        t->dev = 0;
    }
}

void
ogl_tex_ctx_free(ogl_ctx_t *c)
{
    int i;

    for (i = 0; i < 2; i++) {
        tex_free(c->gl->default_tex[i]);
        tex_free(c->gl->proxy_tex[i]);
        c->gl->default_tex[i] = NULL;
        c->gl->proxy_tex[i]   = NULL;
    }
}

/* --- The device's copy ---------------------------------------------------------------- */

static DWORD
wrap_dev(GLenum w)
{
    return (w == GL_CLAMP)           ? VGPU98_OGL_WRAP_CLAMP
           : (w == GL_CLAMP_TO_EDGE) ? VGPU98_OGL_WRAP_CLAMP_EDGE /* only set on a 2.7 device */
                                     : VGPU98_OGL_WRAP_REPEAT;
}

static DWORD
filter_dev(GLenum f)
{
    switch (f) {
        case GL_LINEAR:
            return VGPU98_OGL_FILTER_LINEAR;
        case GL_NEAREST_MIPMAP_NEAREST:
            return VGPU98_OGL_FILTER_N_MIP_N;
        case GL_LINEAR_MIPMAP_NEAREST:
            return VGPU98_OGL_FILTER_L_MIP_N;
        case GL_NEAREST_MIPMAP_LINEAR:
            return VGPU98_OGL_FILTER_N_MIP_L;
        case GL_LINEAR_MIPMAP_LINEAR:
            return VGPU98_OGL_FILTER_L_MIP_L;
        default:
            return VGPU98_OGL_FILTER_NEAREST;
    }
}

static void
send_params(ogl_ctx_t *c, const ogl_tex_t *t)
{
    DWORD *p;
    int    i;

    if (!t->dev || !ogl_objects(c))
        return;
    p = ogl_cmd(c, VGPU98_OGL_TEXPARAM, VGPU98_OGL_LEN_TEXPARAM);
    p[0] = c->group->id;
    p[1] = t->dev;
    p[2] = wrap_dev(t->p.wrap_s) | (wrap_dev(t->p.wrap_t) << 4);
    p[3] = filter_dev(t->p.min_filter) | (filter_dev(t->p.mag_filter) << 4);
    for (i = 0; i < 4; i++)
        p[4 + i] = ogl_fw(t->p.border[i]);
}

/* A free texture ID of the group, the lowest; 0 with all in use. */
static DWORD
tex_id_new(ogl_group_t *g)
{
    DWORD i;

    for (i = 0; i < VGPU98_OGL_TEXTURES / 32; i++) {
        const DWORD free_bits = ~g->tex_ids[i] & ((i == 0) ? ~1u : ~0u); /* ID 0 is none */

        if (free_bits) {
            const DWORD bit = (DWORD) __builtin_ctz(free_bits);

            g->tex_ids[i] |= 1u << bit;
            return i * 32 + bit;
        }
    }
    return 0;
}

static void
tex_id_free(ogl_group_t *g, DWORD id)
{
    if (id && (id < VGPU98_OGL_TEXTURES))
        g->tex_ids[id / 32] &= ~(1u << (id % 32));
}

DWORD
ogl_tex_dev(ogl_ctx_t *c, ogl_tex_t *t)
{
    if (!t->dev) {
        t->dev = tex_id_new(c->group);
        if (t->dev)
            send_params(c, t);
        /* A palette given before its first image (palette.c) */
        if (t->dev && !ogl_palette_default(&t->pal))
            ogl_palette_send(c, t);
    }
    return t->dev;
}

void
ogl_tex_set_params(ogl_ctx_t *c, ogl_tex_t *t, const ogl_texparams_t *p)
{
    t->p = *p;
    send_params(c, t);
    ogl_regs_texture(c); /* completeness may have changed */
}

/* --- Completeness and the texture drawn with ------------------------------------------- */

static int
uses_mipmaps(GLenum min)
{
    return (min != GL_NEAREST) && (min != GL_LINEAR);
}

int
ogl_tex_complete(const ogl_tex_t *t)
{
    const ogl_level_t *l0   = &t->level[0];
    const int          one_d = t->target == GL_TEXTURE_1D; /* its levels one row high, the border at the ends alone */
    GLsizei            w, h;
    int                i;

    if (!l0->base)
        return 0;
    w = l0->w - 2 * l0->border;
    h = one_d ? l0->h : l0->h - 2 * l0->border;
    if ((w <= 0) || (h <= 0))
        return 0;
    if (!uses_mipmaps(t->p.min_filter))
        return 1;
    /* Every level down to 1x1, each half the last (at least 1), with the
       same border and internal format (GL 1.1). */
    for (i = 1; (w > 1) || (h > 1); i++) {
        const ogl_level_t *l = &t->level[i];

        w = (w > 1) ? w / 2 : 1;
        h = (h > 1) ? h / 2 : 1;
        if ((i >= OGL_LEVELS) || !l->base || (l->border != l0->border) || (l->ifmt != l0->ifmt) ||
            (l->w - 2 * l->border != w) || ((one_d ? l->h : l->h - 2 * l->border) != h))
            return 0;
    }
    return 1;
}

ogl_tex_t *
ogl_tex_proxy(ogl_ctx_t *c, int two)
{
    if (!c->gl->proxy_tex[two])
        c->gl->proxy_tex[two] = tex_new(0, two ? GL_PROXY_TEXTURE_2D : GL_PROXY_TEXTURE_1D);
    return c->gl->proxy_tex[two];
}

DWORD
ogl_texture_effective(ogl_ctx_t *c, int unit, int *one_d)
{
    const uint32_t en = c->gl->texture.unit[unit].en;
    ogl_tex_t     *t;

    *one_d = 0;
    if (en & OGL_TE_2D)
        t = ogl_tex_bound_unit(c, unit, GL_TEXTURE_2D);
    else if (en & OGL_TE_1D) {
        t      = ogl_tex_bound_unit(c, unit, GL_TEXTURE_1D);
        *one_d = 1;
    } else {
        if (diag_on)
            ogl_diag_untextured(unit, NULL);
        return 0;
    }
    /* Diagnostics "OpenGL": texturing on with a texture that cannot draw,
       noted for the incomplete texture's finding (draw.c) */
    if (diag_on)
        ogl_diag_untextured(unit, (t && ogl_tex_complete(t) && t->dev) ? NULL : t);
    if (!t || !ogl_tex_complete(t) || !t->dev)
        return 0;
    return t->dev;
}

/* --- Names ---------------------------------------------------------------------------- */

void STDCALL
ogl_GenTextures(GLsizei n, GLuint *textures)
{
    ogl_textures_t *ts;
    GLuint          name;
    GLsizei         i;
    OGL_CTX();

    if (n < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c) || !textures)
        return;
    ts = table(c, 1);
    if (!ts) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    /* Names in use by no texture: each made a texture with no target yet
       (bound later), so a second glGenTextures does not give it again. The
       search goes on from the last name given (names freed below it come
       round again after 2^32). */
    name = ts->next;
    for (i = 0; i < n; i++) {
        ogl_tex_t *t;

        while (!name || find(c, name))
            name++;
        t = tex_new(name, 0);
        if (!t) {
            ogl_error(c, GL_OUT_OF_MEMORY);
            return;
        }
        t->next                    = ts->bucket[name % BUCKETS];
        ts->bucket[name % BUCKETS] = t;
        textures[i]                = name++;
    }
    ts->next = name;
}

void STDCALL
ogl_DeleteTextures(GLsizei n, const GLuint *textures)
{
    ogl_textures_t *ts;
    GLsizei         i;
    OGL_CTX();

    if (n < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (ogl_inside(c) || !textures)
        return;
    ts = table(c, 0);
    if (!ts)
        return;
    for (i = 0; i < n; i++) {
        ogl_tex_t **at = &ts->bucket[textures[i] % BUCKETS];
        int         k;

        if (!textures[i])
            continue; /* the defaults cannot be deleted */
        while (*at && ((*at)->name != textures[i]))
            at = &(*at)->next;
        if (!*at)
            continue;
        {
            ogl_tex_t *t = *at;

            *at = t->next;
            /* Bound in this context (to any unit): the binding reverts to
               the default. */
            for (k = 0; k < 2 * OGL_UNITS; k++) {
                if (c->gl->texture.unit[k / 2].bound[k & 1] == t->name)
                    c->gl->texture.unit[k / 2].bound[k & 1] = 0;
            }
            if (t->dev) {
                if (c->group->opened) {
                    DWORD *p = ogl_cmd(c, VGPU98_OGL_TEXDELETE, VGPU98_OGL_LEN_TEXDELETE);

                    p[0] = c->group->id;
                    p[1] = t->dev;
                }
                tex_id_free(c->group, t->dev);
            }
            tex_free(t);
        }
    }
    ogl_regs_texture(c);
}

void STDCALL
ogl_BindTexture(GLenum target, GLuint texture)
{
    const int  i = target_index(target);
    ogl_tex_t *t;
    OGL_CTX();

    OGL_DL(OP_BINDTEXTURE, target, texture);
    if (i < 0) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    if (texture) {
        t = find(c, texture);
        if (t && t->target && (t->target != target)) {
            ogl_error(c, GL_INVALID_OPERATION); /* made with another dimensionality */
            return;
        }
        if (!t) {
            ogl_textures_t *ts = table(c, 1);

            t = ts ? tex_new(texture, target) : NULL;
            if (!t) {
                ogl_error(c, GL_OUT_OF_MEMORY);
                return;
            }
            t->next                       = ts->bucket[texture % BUCKETS];
            ts->bucket[texture % BUCKETS] = t;
        }
        t->target = target;
    }
    c->gl->texture.unit[c->gl->texture.active].bound[i] = texture;
    ogl_regs_texture(c);
}

GLboolean STDCALL
ogl_IsTexture(GLuint texture)
{
    const ogl_tex_t *t;
    OGL_CTX(GL_FALSE);

    if (ogl_inside(c) || !texture)
        return GL_FALSE;
    /* A name glGenTextures gave but never bound is not yet a texture. */
    t = find(c, texture);
    return (t && t->target) ? GL_TRUE : GL_FALSE;
}

GLboolean STDCALL
ogl_AreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences)
{
    GLsizei i;
    OGL_CTX(GL_FALSE);

    if (n < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return GL_FALSE;
    }
    if (ogl_inside(c) || !textures)
        return GL_FALSE;
    for (i = 0; i < n; i++) {
        const ogl_tex_t *t = find(c, textures[i]);

        if (!textures[i] || !t || !t->target) {
            ogl_error(c, GL_INVALID_VALUE);
            return GL_FALSE;
        }
    }
    /* Every texture is resident: GL then leaves residences as they are. */
    (void) residences;
    return GL_TRUE;
}

void STDCALL
ogl_PrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
    GLsizei i;
    OGL_CTX();

    if (n < 0) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    if (!textures || !priorities)
        return;
    if (ogl_dl_on(c)) {
        DWORD *w = ogl_alloc((DWORD) (2 * n + 1) * 4);
        int    only;

        if (!w) {
            ogl_error(c, GL_OUT_OF_MEMORY);
            return;
        }
        w[0] = (DWORD) n;
        memcpy(&w[1], textures, (size_t) n * 4);
        memcpy(&w[1 + n], priorities, (size_t) n * 4);
        only = ogl_dl(c, OP_PRIORITIZE, w, (DWORD) (2 * n + 1));
        ogl_free(w);
        if (only)
            return;
    }
    if (ogl_inside(c))
        return;
    for (i = 0; i < n; i++) {
        ogl_tex_t *t = textures[i] ? find(c, textures[i]) : NULL;

        if (t)
            t->p.priority = ogl_clampf(priorities[i], 0.0f, 1.0f);
    }
}

/* --- Parameters ------------------------------------------------------------------------- */

static int
param_ok(GLenum pname, const float *v)
{
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER: {
            const GLenum f = (GLenum) v[0];

            return (f == GL_NEAREST) || (f == GL_LINEAR) || (f == GL_NEAREST_MIPMAP_NEAREST) ||
                   (f == GL_LINEAR_MIPMAP_NEAREST) || (f == GL_NEAREST_MIPMAP_LINEAR) || (f == GL_LINEAR_MIPMAP_LINEAR);
        }
        case GL_TEXTURE_MAG_FILTER:
            return ((GLenum) v[0] == GL_NEAREST) || ((GLenum) v[0] == GL_LINEAR);
        case GL_TEXTURE_WRAP_S:
        case GL_TEXTURE_WRAP_T:
            /* GL_CLAMP_TO_EDGE with the edge-clamp extensions (a 2.7 device) */
            return ((GLenum) v[0] == GL_CLAMP) || ((GLenum) v[0] == GL_REPEAT) ||
                   (((GLenum) v[0] == GL_CLAMP_TO_EDGE) && ogl_ext_on(OGL_EXT_EDGE_CLAMP));
        case GL_TEXTURE_BORDER_COLOR:
        case GL_TEXTURE_PRIORITY:
            return 1;
        default:
            return 0;
    }
}

void STDCALL
ogl_TexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
    ogl_tex_t      *t;
    ogl_texparams_t p;
    float           v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int             i;
    OGL_CTX();

    if (params)
        memcpy(v, params, ((pname == GL_TEXTURE_BORDER_COLOR) ? 4 : 1) * sizeof(float));
    OGL_DL(OP_TEXPARAMETER, target, pname, ogl_fw(v[0]), ogl_fw(v[1]), ogl_fw(v[2]), ogl_fw(v[3]));
    if ((target_index(target) < 0) || !param_ok(pname, v)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    t = ogl_tex_bound(c, target);
    if (!t) {
        ogl_error(c, GL_OUT_OF_MEMORY);
        return;
    }
    p = t->p;
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:
            p.min_filter = (GLenum) v[0];
            break;
        case GL_TEXTURE_MAG_FILTER:
            p.mag_filter = (GLenum) v[0];
            break;
        case GL_TEXTURE_WRAP_S:
            p.wrap_s = (GLenum) v[0];
            break;
        case GL_TEXTURE_WRAP_T:
            p.wrap_t = (GLenum) v[0];
            break;
        case GL_TEXTURE_BORDER_COLOR:
            for (i = 0; i < 4; i++)
                p.border[i] = ogl_clampf(v[i], 0.0f, 1.0f);
            break;
        default:
            p.priority = ogl_clampf(v[0], 0.0f, 1.0f);
            break;
    }
    ogl_tex_set_params(c, t, &p);
}

void STDCALL
ogl_TexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    OGL_CTX();
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    ogl_TexParameterfv(target, pname, &param);
}

void STDCALL
ogl_TexParameteri(GLenum target, GLenum pname, GLint param)
{
    ogl_TexParameterf(target, pname, (GLfloat) param);
}

void STDCALL
ogl_TexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
    float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int   i;

    if (params) {
        if (pname == GL_TEXTURE_BORDER_COLOR) {
            for (i = 0; i < 4; i++)
                v[i] = (float) ((2.0 * (double) params[i] + 1.0) / 4294967295.0);
        } else if (pname == GL_TEXTURE_PRIORITY)
            v[0] = (float) ((2.0 * (double) params[0] + 1.0) / 4294967295.0);
        else
            v[0] = (float) params[0];
    }
    ogl_TexParameterfv(target, pname, v);
}

static int
get_param(ogl_ctx_t *c, GLenum target, GLenum pname, float *v)
{
    const ogl_tex_t *t;

    if ((target_index(target) < 0) ||
        ((pname != GL_TEXTURE_MIN_FILTER) && (pname != GL_TEXTURE_MAG_FILTER) && (pname != GL_TEXTURE_WRAP_S) &&
         (pname != GL_TEXTURE_WRAP_T) && (pname != GL_TEXTURE_BORDER_COLOR) && (pname != GL_TEXTURE_PRIORITY) &&
         (pname != GL_TEXTURE_RESIDENT))) {
        ogl_error(c, GL_INVALID_ENUM);
        return 0;
    }
    if (ogl_inside(c))
        return 0;
    t = ogl_tex_bound(c, target);
    if (!t)
        return 0;
    switch (pname) {
        case GL_TEXTURE_MIN_FILTER:
            v[0] = (float) t->p.min_filter;
            break;
        case GL_TEXTURE_MAG_FILTER:
            v[0] = (float) t->p.mag_filter;
            break;
        case GL_TEXTURE_WRAP_S:
            v[0] = (float) t->p.wrap_s;
            break;
        case GL_TEXTURE_WRAP_T:
            v[0] = (float) t->p.wrap_t;
            break;
        case GL_TEXTURE_BORDER_COLOR:
            memcpy(v, t->p.border, 4 * sizeof(float));
            break;
        case GL_TEXTURE_PRIORITY:
            v[0] = t->p.priority;
            break;
        default:
            v[0] = 1.0f; /* GL_TEXTURE_RESIDENT: every texture */
            break;
    }
    return 1;
}

void STDCALL
ogl_GetTexParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
    float v[4];
    OGL_CTX();

    if (get_param(c, target, pname, v) && params)
        memcpy(params, v, ((pname == GL_TEXTURE_BORDER_COLOR) ? 4 : 1) * sizeof(float));
}

void STDCALL
ogl_GetTexParameteriv(GLenum target, GLenum pname, GLint *params)
{
    float v[4];
    int   i;
    OGL_CTX();

    if (!get_param(c, target, pname, v) || !params)
        return;
    if (pname == GL_TEXTURE_BORDER_COLOR) {
        for (i = 0; i < 4; i++) {
            const double d = ((double) v[i] * 4294967295.0 - 1.0) / 2.0;

            params[i] = (d >= 2147483647.0) ? 2147483647 : (GLint) (d + ((d >= 0.0) ? 0.5 : -0.5));
        }
    } else if (pname == GL_TEXTURE_PRIORITY) {
        const double d = ((double) v[0] * 4294967295.0 - 1.0) / 2.0;

        params[0] = (d >= 2147483647.0) ? 2147483647 : (GLint) (d + ((d >= 0.0) ? 0.5 : -0.5));
    } else
        params[0] = (GLint) v[0];
}

/* --- Level queries ------------------------------------------------------------------------ */

/* A base format's components: bits 0-3 red, green, blue, alpha; 4
   luminance; 5 intensity. */
static int
base_components(int base)
{
    switch (base) {
        case VGPU98_OGL_BASE_ALPHA:
            return 8;
        case VGPU98_OGL_BASE_LUMINANCE:
            return 16;
        case VGPU98_OGL_BASE_LUM_ALPHA:
            return 16 | 8;
        case VGPU98_OGL_BASE_INTENSITY:
            return 32;
        case VGPU98_OGL_BASE_RGB:
            return 7;
        case VGPU98_OGL_BASE_RGBA:
            return 15;
        default:
            return 0;
    }
}

static int
level_param(ogl_ctx_t *c, GLenum target, GLint level, GLenum pname, GLint *out)
{
    const ogl_tex_t   *t;
    const ogl_level_t *l;
    int                comps;

    switch (target) {
        case GL_TEXTURE_1D:
        case GL_TEXTURE_2D:
            t = ogl_tex_bound(c, target);
            break;
        case GL_PROXY_TEXTURE_1D:
            t = c->gl->proxy_tex[0];
            break;
        case GL_PROXY_TEXTURE_2D:
            t = c->gl->proxy_tex[1];
            break;
        default:
            ogl_error(c, GL_INVALID_ENUM);
            return 0;
    }
    if ((level < 0) || (level > OGL_MAX_TEXTURE_LOG2)) {
        ogl_error(c, GL_INVALID_VALUE);
        return 0;
    }
    if (ogl_inside(c))
        return 0;
    l     = t ? &t->level[level] : NULL;
    comps = (l && l->base) ? base_components(l->base) : 0;
    switch (pname) {
        case GL_TEXTURE_WIDTH:
            *out = (l && l->base) ? l->w : 0;
            break;
        case GL_TEXTURE_HEIGHT:
            *out = (l && l->base) ? l->h : 0;
            break;
        case GL_TEXTURE_BORDER:
            *out = (l && l->base) ? l->border : 0;
            break;
        case GL_TEXTURE_INTERNAL_FORMAT: /* GL_TEXTURE_COMPONENTS */
            *out = (l && l->base) ? l->ifmt : 1;
            break;
        case GL_TEXTURE_RED_SIZE:
            *out = (comps & 1) ? 8 : 0;
            break;
        case GL_TEXTURE_GREEN_SIZE:
            *out = (comps & 2) ? 8 : 0;
            break;
        case GL_TEXTURE_BLUE_SIZE:
            *out = (comps & 4) ? 8 : 0;
            break;
        case GL_TEXTURE_ALPHA_SIZE:
            *out = (comps & 8) ? 8 : 0;
            break;
        case GL_TEXTURE_LUMINANCE_SIZE:
            *out = (comps & 16) ? 8 : 0;
            break;
        case GL_TEXTURE_INTENSITY_SIZE:
            *out = (comps & 32) ? 8 : 0;
            break;
        case GL_TEXTURE_INDEX_SIZE_EXT: /* EXT_paletted_texture's: 8 bits, every index format's */
            if (!ogl_ext_on(OGL_EXT_PALETTED_TEXTURE)) {
                ogl_error(c, GL_INVALID_ENUM);
                return 0;
            }
            *out = (l && (l->base == VGPU98_OGL_BASE_INDEX)) ? 8 : 0;
            break;
        default:
            ogl_error(c, GL_INVALID_ENUM);
            return 0;
    }
    return 1;
}

void STDCALL
ogl_GetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params)
{
    GLint v;
    OGL_CTX();

    if (level_param(c, target, level, pname, &v) && params)
        params[0] = v;
}

void STDCALL
ogl_GetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params)
{
    GLint v;
    OGL_CTX();

    if (level_param(c, target, level, pname, &v) && params)
        params[0] = (GLfloat) v;
}

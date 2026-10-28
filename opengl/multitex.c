/*
 * The multitexture extensions:
 * GL_ARB_multitexture and GL_SGIS_multitexture, one mechanism with two
 * texture units.
 *
 * - The active unit (ACTIVE_TEXTURE_ARB; glSelectTextureSGIS) is the one
 *   every texture state call addresses: enables, bindings, images and
 *   parameters, the environment, coordinate generation, the texture
 *   matrix, and their queries.
 * - The client unit (CLIENT_ACTIVE_TEXTURE_ARB) is the one the texture
 *   coordinate array calls address. SGIS's coordinate set
 *   (glSelectTextureCoordSetSGIS) is the client unit and the unit
 *   glTexCoord sets; with ARB alone glTexCoord sets unit 0.
 * - glMultiTexCoord and glMTexCoord set a unit's current coordinates.
 *
 * ARB's units are TEXTURE0_ARB on; SGIS's TEXTURE0_SGIS on, at the values
 * Quake 2 and GLQuake pass (835Eh, 835Fh). A unit past the last is
 * INVALID_ENUM.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "lists.h"
#include "regs.h"

/* A unit's number from its token (base TEXTURE0_ARB or TEXTURE0_SGIS); -1
   for none. */
static int
unit_of(GLenum target, GLenum base)
{
    return (target - base < OGL_UNITS) ? (int) (target - base) : -1;
}

/* --- The active unit ----------------------------------------------------------------------- */

void
ogl_active_texture(ogl_ctx_t *c, int unit)
{
    OGL_DL(OP_ACTIVETEXTURE, (DWORD) unit);
    if (unit < 0) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->texture.active = unit;
}

void STDCALL
ogl_ActiveTextureARB(GLenum texture)
{
    OGL_CTX();
    ogl_active_texture(c, unit_of(texture, GL_TEXTURE0_ARB));
}

void STDCALL
ogl_SelectTextureSGIS(GLenum target)
{
    OGL_CTX();
    ogl_active_texture(c, unit_of(target, GL_TEXTURE0_SGIS));
}

/* --- The client unit and the coordinate set (client state: run at once) ----------------- */

void STDCALL
ogl_ClientActiveTextureARB(GLenum texture)
{
    const int unit = unit_of(texture, GL_TEXTURE0_ARB);
    OGL_CTX();

    if (unit < 0) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->client.client_active = unit;
}

void STDCALL
ogl_SelectTextureCoordSetSGIS(GLenum target)
{
    const int unit = unit_of(target, GL_TEXTURE0_SGIS);
    OGL_CTX();

    if (unit < 0) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->client.client_active = unit;
    c->gl->client.coord_set     = unit;
}

/* SGIS's array of a unit, as glTexCoordPointer's (arrays.c) for that unit. */
void STDCALL
ogl_MultiTexCoordPointerSGIS(GLenum target, GLint size, GLenum type, GLsizei stride, GLvoid *pointer)
{
    const int unit = unit_of(target, GL_TEXTURE0_SGIS);
    int       old;
    OGL_CTX();

    if (unit < 0) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    old                         = c->gl->client.client_active;
    c->gl->client.client_active = unit;
    ogl_TexCoordPointer(size, type, stride, pointer);
    c->gl->client.client_active = old;
}

/* --- A unit's current coordinates ------------------------------------------------------ */

void
ogl_multitexcoord(ogl_ctx_t *c, int unit, float s, float t, float r, float q)
{
    float *tc;

    OGL_DL(OP_MULTITEXCOORD, (DWORD) unit, ogl_fw(s), ogl_fw(t), ogl_fw(r), ogl_fw(q));
    if (unit < 0) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    tc    = c->gl->current.texcoord[unit];
    tc[0] = s;
    tc[1] = t;
    tc[2] = r;
    tc[3] = q;
}

static void
mtc(GLenum target, GLenum base, float s, float t, float r, float q)
{
    OGL_CTX();
    ogl_multitexcoord(c, unit_of(target, base), s, t, r, q);
}

/* Each count and type of glMultiTexCoord, for both extensions: the
   missing coordinates 0, 0 and 1. */
#define MTC_FUNCS(ext, base, n, sfx, T)                                                                          \
    void STDCALL ogl_MultiTexCoord##n##sfx##ext(GLenum target, OGL_MTC_PARAMS##n(T))                               \
    {                                                                                                          \
        OGL_MTC_CALL##n(target, base);                                                                         \
    }                                                                                                          \
    void STDCALL ogl_MultiTexCoord##n##sfx##v##ext(GLenum target, const T *v)                                     \
    {                                                                                                          \
        OGL_MTC_CALLV##n(target, base);                                                                        \
    }

#define OGL_MTC_CALL1(target, base)  mtc(target, base, (float) s, 0.0f, 0.0f, 1.0f)
#define OGL_MTC_CALL2(target, base)  mtc(target, base, (float) s, (float) t, 0.0f, 1.0f)
#define OGL_MTC_CALL3(target, base)  mtc(target, base, (float) s, (float) t, (float) r, 1.0f)
#define OGL_MTC_CALL4(target, base)  mtc(target, base, (float) s, (float) t, (float) r, (float) q)
#define OGL_MTC_CALLV1(target, base) mtc(target, base, (float) v[0], 0.0f, 0.0f, 1.0f)
#define OGL_MTC_CALLV2(target, base) mtc(target, base, (float) v[0], (float) v[1], 0.0f, 1.0f)
#define OGL_MTC_CALLV3(target, base) mtc(target, base, (float) v[0], (float) v[1], (float) v[2], 1.0f)
#define OGL_MTC_CALLV4(target, base) mtc(target, base, (float) v[0], (float) v[1], (float) v[2], (float) v[3])

#define MTC_ARB(n, sfx, T)  MTC_FUNCS(ARB, GL_TEXTURE0_ARB, n, sfx, T)
#define MTC_SGIS(n, sfx, T) MTC_FUNCS(SGIS, GL_TEXTURE0_SGIS, n, sfx, T)

OGL_MTC_LIST(MTC_ARB)
OGL_MTC_LIST(MTC_SGIS)

/* GLQuake's and Quake 2's names for SGIS's two-float form. */
void STDCALL
ogl_MTexCoord2fSGIS(GLenum target, GLfloat s, GLfloat t)
{
    mtc(target, GL_TEXTURE0_SGIS, s, t, 0.0f, 1.0f);
}

void STDCALL
ogl_MTexCoord2fvSGIS(GLenum target, const GLfloat *v)
{
    mtc(target, GL_TEXTURE0_SGIS, v[0], v[1], 0.0f, 1.0f);
}

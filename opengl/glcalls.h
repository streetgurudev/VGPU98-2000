/*
 * The ICD's GL entry points: ogl_<Name> for each of GL 1.1's 336 functions,
 * stdcall, with GL's arguments (glfuncs.h). Each finds the calling thread's
 * current context (main.c), and does nothing without one.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_ICD_GLCALLS_H
#define VGPU98_ICD_GLCALLS_H

#include "icd.h"

#define GLF(index, ret, name, params, args, stack) ret STDCALL ogl_##name params;
#include "glfuncs.h"

/* The extensions' entry points (ext.c's table). */
BOOL STDCALL ogl_wglSwapIntervalEXT(int interval);
int STDCALL  ogl_wglGetSwapIntervalEXT(void);
void STDCALL ogl_LockArraysEXT(GLint first, GLsizei count);
void STDCALL ogl_UnlockArraysEXT(void);
void STDCALL ogl_PointParameterfEXT(GLenum pname, GLfloat param);
void STDCALL ogl_PointParameterfvEXT(GLenum pname, const GLfloat *params);
void STDCALL ogl_ColorTableEXT(GLenum target, GLenum internalformat, GLsizei width, GLenum format, GLenum type,
                               const GLvoid *table);
void STDCALL ogl_ColorSubTableEXT(GLenum target, GLsizei start, GLsizei count, GLenum format, GLenum type, const GLvoid *data);
void STDCALL ogl_GetColorTableEXT(GLenum target, GLenum format, GLenum type, GLvoid *data);
void STDCALL ogl_GetColorTableParameterivEXT(GLenum target, GLenum pname, GLint *params);
void STDCALL ogl_GetColorTableParameterfvEXT(GLenum target, GLenum pname, GLfloat *params);

/* The multitexture extensions (multitex.c). glMultiTexCoord's counts and
   types, as X(count, suffix, type): */
#define OGL_MTC_LIST(X)                                                                                \
    X(1, d, GLdouble) X(1, f, GLfloat) X(1, i, GLint) X(1, s, GLshort) X(2, d, GLdouble) X(2, f, GLfloat) \
    X(2, i, GLint) X(2, s, GLshort) X(3, d, GLdouble) X(3, f, GLfloat) X(3, i, GLint) X(3, s, GLshort)     \
    X(4, d, GLdouble) X(4, f, GLfloat) X(4, i, GLint) X(4, s, GLshort)
#define OGL_MTC_PARAMS1(T) T s
#define OGL_MTC_PARAMS2(T) T s, T t
#define OGL_MTC_PARAMS3(T) T s, T t, T r
#define OGL_MTC_PARAMS4(T) T s, T t, T r, T q
#define OGL_MTC_DECLS(n, sfx, T)                                                                  \
    void STDCALL ogl_MultiTexCoord##n##sfx##ARB(GLenum target, OGL_MTC_PARAMS##n(T));               \
    void STDCALL ogl_MultiTexCoord##n##sfx##v##ARB(GLenum target, const T *v);                      \
    void STDCALL ogl_MultiTexCoord##n##sfx##SGIS(GLenum target, OGL_MTC_PARAMS##n(T));              \
    void STDCALL ogl_MultiTexCoord##n##sfx##v##SGIS(GLenum target, const T *v);
OGL_MTC_LIST(OGL_MTC_DECLS)
void STDCALL ogl_ActiveTextureARB(GLenum texture);
void STDCALL ogl_ClientActiveTextureARB(GLenum texture);
void STDCALL ogl_SelectTextureSGIS(GLenum target);
void STDCALL ogl_SelectTextureCoordSetSGIS(GLenum target);
void STDCALL ogl_MultiTexCoordPointerSGIS(GLenum target, GLint size, GLenum type, GLsizei stride, GLvoid *pointer);
void STDCALL ogl_MTexCoord2fSGIS(GLenum target, GLfloat s, GLfloat t);
void STDCALL ogl_MTexCoord2fvSGIS(GLenum target, const GLfloat *v);
/* The active unit set, a unit's current coordinates set (-1: a token that
   names no unit, INVALID_ENUM when run); each compiled into a list. */
void ogl_active_texture(ogl_ctx_t *c, int unit);
void ogl_multitexcoord(ogl_ctx_t *c, int unit, float s, float t, float r, float q);

/* The entry point's context, or a return without one. */
#define OGL_CTX(...)                    \
    ogl_ctx_t *c = ogl_current();       \
    if (!c)                             \
        return __VA_ARGS__

#endif /* VGPU98_ICD_GLCALLS_H */

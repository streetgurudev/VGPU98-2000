/*
 * The extensions the ICD offers: their
 * names in GL_EXTENSIONS, their functions through DrvGetProcAddress, and
 * WGL_EXT_swap_control's two calls.
 *
 * An extension is offered only on a device whose command set draws what it
 * adds: 2.6 for multitexture, compiled vertex arrays, paletted textures,
 * point parameters and swap control, 2.7 for clamp-to-edge;
 * an older 86Box gets GL 1.1 alone, as
 * before, or 2.6's alone. Every name in GL_EXTENSIONS is
 * followed by a space (GLQuake looks for "GL_SGIS_multitexture " with
 * one), WGL's among them (Quake 2 and 3 look for WGL_EXT_swap_control
 * there), and the string stays far below the 1 KB some games copy it into.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "state.h"

/* The extensions, by the least command set of a device that draws them:
   2.6's, 2.7's. */
#define OGL_EXT_26                                                                                     \
    (OGL_EXT_SWAP_CONTROL | OGL_EXT_COMPILED_VERTEX_ARRAY | OGL_EXT_SGIS_MULTITEXTURE | OGL_EXT_ARB_MULTITEXTURE | \
     OGL_EXT_POINT_PARAMETERS | OGL_EXT_PALETTED_TEXTURE | OGL_EXT_SHARED_TEXTURE_PALETTE)
#define OGL_EXT_27 OGL_EXT_EDGE_CLAMP

#define OGL_COMMAND_SET_26 0x00020006u
#define OGL_COMMAND_SET_27 0x00020007u

static DWORD offered;
static int   device_26;
static char  names_offered[512];

/* Each extension's name, in GL_EXTENSIONS' order. */
static const struct {
    DWORD       ext;
    const char *name;
} names[] = {
    { OGL_EXT_ARB_MULTITEXTURE, "GL_ARB_multitexture" },
    { OGL_EXT_COMPILED_VERTEX_ARRAY, "GL_EXT_compiled_vertex_array" },
    { OGL_EXT_PALETTED_TEXTURE, "GL_EXT_paletted_texture" },
    { OGL_EXT_POINT_PARAMETERS, "GL_EXT_point_parameters" },
    { OGL_EXT_SHARED_TEXTURE_PALETTE, "GL_EXT_shared_texture_palette" },
    { OGL_EXT_EDGE_CLAMP, "GL_EXT_texture_edge_clamp" },
    { OGL_EXT_SGIS_MULTITEXTURE, "GL_SGIS_multitexture" },
    { OGL_EXT_EDGE_CLAMP, "GL_SGIS_texture_edge_clamp" },
    { OGL_EXT_SWAP_CONTROL, "WGL_EXT_swap_control" },
};

/* glMultiTexCoord's names and functions for both extensions. */
#define MTC_PROCS(n, sfx, T)                                                                                       \
    { "glMultiTexCoord" #n #sfx "ARB", (void *) ogl_MultiTexCoord##n##sfx##ARB, OGL_EXT_ARB_MULTITEXTURE },         \
        { "glMultiTexCoord" #n #sfx "vARB", (void *) ogl_MultiTexCoord##n##sfx##v##ARB, OGL_EXT_ARB_MULTITEXTURE }, \
        { "glMultiTexCoord" #n #sfx "SGIS", (void *) ogl_MultiTexCoord##n##sfx##SGIS, OGL_EXT_SGIS_MULTITEXTURE },  \
        { "glMultiTexCoord" #n #sfx "vSGIS", (void *) ogl_MultiTexCoord##n##sfx##v##SGIS, OGL_EXT_SGIS_MULTITEXTURE },

/* Each function an extension brings, by its name. */
static const struct {
    const char *name;
    void       *fn;
    DWORD       ext;
} procs[] = {
    { "wglGetSwapIntervalEXT", (void *) ogl_wglGetSwapIntervalEXT, OGL_EXT_SWAP_CONTROL },
    { "wglSwapIntervalEXT", (void *) ogl_wglSwapIntervalEXT, OGL_EXT_SWAP_CONTROL },
    { "glLockArraysEXT", (void *) ogl_LockArraysEXT, OGL_EXT_COMPILED_VERTEX_ARRAY },
    { "glUnlockArraysEXT", (void *) ogl_UnlockArraysEXT, OGL_EXT_COMPILED_VERTEX_ARRAY },
    { "glPointParameterfEXT", (void *) ogl_PointParameterfEXT, OGL_EXT_POINT_PARAMETERS },
    { "glPointParameterfvEXT", (void *) ogl_PointParameterfvEXT, OGL_EXT_POINT_PARAMETERS },
    { "glColorTableEXT", (void *) ogl_ColorTableEXT, OGL_EXT_PALETTED_TEXTURE },
    { "glColorSubTableEXT", (void *) ogl_ColorSubTableEXT, OGL_EXT_PALETTED_TEXTURE },
    { "glGetColorTableEXT", (void *) ogl_GetColorTableEXT, OGL_EXT_PALETTED_TEXTURE },
    { "glGetColorTableParameterivEXT", (void *) ogl_GetColorTableParameterivEXT, OGL_EXT_PALETTED_TEXTURE },
    { "glGetColorTableParameterfvEXT", (void *) ogl_GetColorTableParameterfvEXT, OGL_EXT_PALETTED_TEXTURE },
    { "glActiveTextureARB", (void *) ogl_ActiveTextureARB, OGL_EXT_ARB_MULTITEXTURE },
    { "glClientActiveTextureARB", (void *) ogl_ClientActiveTextureARB, OGL_EXT_ARB_MULTITEXTURE },
    { "glSelectTextureSGIS", (void *) ogl_SelectTextureSGIS, OGL_EXT_SGIS_MULTITEXTURE },
    { "glSelectTextureCoordSetSGIS", (void *) ogl_SelectTextureCoordSetSGIS, OGL_EXT_SGIS_MULTITEXTURE },
    { "glMultiTexCoordPointerSGIS", (void *) ogl_MultiTexCoordPointerSGIS, OGL_EXT_SGIS_MULTITEXTURE },
    { "glMTexCoord2fSGIS", (void *) ogl_MTexCoord2fSGIS, OGL_EXT_SGIS_MULTITEXTURE },
    { "glMTexCoord2fvSGIS", (void *) ogl_MTexCoord2fvSGIS, OGL_EXT_SGIS_MULTITEXTURE },
    OGL_MTC_LIST(MTC_PROCS)
};

void
ogl_ext_init(DWORD command_set)
{
    DWORD    n = 0;
    unsigned i;

    device_26 = command_set >= OGL_COMMAND_SET_26;
    offered   = (device_26 ? OGL_EXT_26 : 0) | ((command_set >= OGL_COMMAND_SET_27) ? OGL_EXT_27 : 0);
    for (i = 0; i < ARRAY_SIZE(names); i++) {
        const char *s = names[i].name;

        if (!(offered & names[i].ext))
            continue;
        while (*s && (n + 2 < sizeof(names_offered)))
            names_offered[n++] = *s++;
        if (n + 1 < sizeof(names_offered))
            names_offered[n++] = ' ';
    }
    names_offered[n] = 0;
}

int
ogl_ext_on(DWORD ext)
{
    return (offered & ext) != 0;
}

const char *
ogl_ext_string(void)
{
    return names_offered;
}

DWORD
ogl_reg_count(void)
{
    return device_26 ? VGPU98_OGL_R_COUNT : VGPU98_OGL_R_COUNT_25;
}

static int
same(const char *a, const char *b)
{
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *a == *b;
}

void *
ogl_ext_proc(const char *name)
{
    unsigned i;

    if (!name)
        return NULL;
    for (i = 0; i < ARRAY_SIZE(procs); i++) {
        if ((offered & procs[i].ext) && same(name, procs[i].name))
            return procs[i].fn;
    }
    return NULL;
}

/* --- WGL_EXT_swap_control ------------------------------------------------------------------- */

/* The window of the current context swaps after `interval` vertical blanks
   from now on (0: at once). A negative interval is refused. */
BOOL STDCALL
ogl_wglSwapIntervalEXT(int interval)
{
    ogl_ctx_t *c = ogl_current();

    if (!c || !c->win)
        return 0;
    if (interval < 0) {
        SetLastError(ERROR_INVALID_DATA);
        return 0;
    }
    c->win->swap_interval = interval;
    return 1;
}

int STDCALL
ogl_wglGetSwapIntervalEXT(void)
{
    ogl_ctx_t *c = ogl_current();

    return (c && c->win) ? c->win->swap_interval : 0;
}

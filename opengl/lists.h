/*
 * Display lists (GL 1.1, section 5.4): a list is the commands compiled into
 * it, each recorded as an operation with its arguments in canonical form
 * (every variant of a command recorded as its one float or integer form,
 * client data copied as it was unpacked at compile time). Executing a list
 * calls each command's entry point again with those arguments, recording
 * off, so a command's rules and errors apply when the list runs.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef VGPU98_ICD_LISTS_H
#define VGPU98_ICD_LISTS_H

#include "state.h"

enum {
    OP_END_OF_LIST = 0,
    OP_ACCUM,          /* op, value */
    OP_ALPHAFUNC,      /* func, ref */
    OP_BEGIN,          /* mode */
    OP_END,
    OP_BINDTEXTURE,    /* target, texture */
    OP_BITMAP,         /* w, h, xorig, yorig, xmove, ymove; data: w * h bytes (1: a fragment), bottom row first */
    OP_BLENDFUNC,      /* src, dst */
    OP_CALLLIST,       /* list */
    OP_CALLLISTS,      /* n; data: n names (the list base added when it runs) */
    OP_CLEAR,          /* mask */
    OP_CLEARACCUM,     /* r, g, b, a */
    OP_CLEARCOLOR,     /* r, g, b, a */
    OP_CLEARDEPTH,     /* depth (two words) */
    OP_CLEARINDEX,     /* c */
    OP_CLEARSTENCIL,   /* s */
    OP_CLIPPLANE,      /* plane, a, b, c, d (two words each) */
    OP_COLOR,          /* r, g, b, a */
    OP_COLORMASK,      /* r, g, b, a */
    OP_COLORMATERIAL,  /* face, mode */
    OP_COPYPIXELS,     /* x, y, w, h, type */
    OP_COPYTEXIMAGE1D, /* target, level, ifmt, x, y, w, border */
    OP_COPYTEXIMAGE2D, /* target, level, ifmt, x, y, w, h, border */
    OP_COPYTEXSUB1D,   /* target, level, xoff, x, y, w */
    OP_COPYTEXSUB2D,   /* target, level, xoff, yoff, x, y, w, h */
    OP_CULLFACE,       /* mode */
    OP_DEPTHFUNC,      /* func */
    OP_DEPTHMASK,      /* flag */
    OP_DEPTHRANGE,     /* near, far (two words each) */
    OP_DISABLE,        /* cap */
    OP_ENABLE,         /* cap */
    OP_DRAWBUFFER,     /* mode */
    OP_DRAWPIXELS,     /* w, h, format, kind; data: the unpacked image (pixel.h) */
    OP_EDGEFLAG,       /* flag */
    OP_EVALCOORD1,     /* u */
    OP_EVALCOORD2,     /* u, v */
    OP_EVALMESH1,      /* mode, i1, i2 */
    OP_EVALMESH2,      /* mode, i1, i2, j1, j2 */
    OP_EVALPOINT1,     /* i */
    OP_EVALPOINT2,     /* i, j */
    OP_FOG,            /* pname, four floats */
    OP_FRONTFACE,      /* mode */
    OP_FRUSTUM,        /* left, right, bottom, top, near, far (two words each) */
    OP_HINT,           /* target, mode */
    OP_INDEX,          /* c */
    OP_INDEXMASK,      /* mask */
    OP_INITNAMES,
    OP_LIGHT,          /* light, pname, four floats */
    OP_LIGHTMODEL,     /* pname, four floats */
    OP_LINESTIPPLE,    /* factor, pattern */
    OP_LINEWIDTH,      /* width */
    OP_LISTBASE,       /* base */
    OP_LOADIDENTITY,
    OP_LOADMATRIX,     /* 16 floats */
    OP_LOADNAME,       /* name */
    OP_LOGICOP,        /* opcode */
    OP_MAP1,           /* target, u1, u2, order; data: order * k floats */
    OP_MAP2,           /* target, u1, u2, uorder, v1, v2, vorder; data: uorder * vorder * k floats */
    OP_MAPGRID1,       /* n, u1, u2 */
    OP_MAPGRID2,       /* un, u1, u2, vn, v1, v2 */
    OP_MATERIAL,       /* face, pname, four floats */
    OP_MATRIXMODE,     /* mode */
    OP_MULTMATRIX,     /* 16 floats */
    OP_NORMAL,         /* x, y, z */
    OP_ORTHO,          /* left, right, bottom, top, near, far (two words each) */
    OP_PASSTHROUGH,    /* token */
    OP_PIXELMAP,       /* map, n; data: n floats */
    OP_PIXELTRANSFER,  /* pname, value (a float) */
    OP_PIXELZOOM,      /* x, y */
    OP_POINTSIZE,      /* size */
    OP_POLYGONMODE,    /* face, mode */
    OP_POLYGONOFFSET,  /* factor, units */
    OP_POLYGONSTIPPLE, /* data: 32 rows of 32 bits as glPolygonStipple unpacked them */
    OP_POPATTRIB,
    OP_POPMATRIX,
    OP_POPNAME,
    OP_PRIORITIZE,     /* n; data: n names, then n priorities */
    OP_PUSHATTRIB,     /* mask */
    OP_PUSHMATRIX,
    OP_PUSHNAME,       /* name */
    OP_RASTERPOS,      /* x, y, z, w */
    OP_READBUFFER,     /* mode */
    OP_RECT,           /* x1, y1, x2, y2 */
    OP_ROTATE,         /* angle, x, y, z */
    OP_SCALE,          /* x, y, z */
    OP_SCISSOR,        /* x, y, w, h */
    OP_SHADEMODEL,     /* mode */
    OP_STENCILFUNC,    /* func, ref, mask */
    OP_STENCILMASK,    /* mask */
    OP_STENCILOP,      /* fail, zfail, zpass */
    OP_TEXCOORD,       /* s, t, r, q */
    OP_TEXENV,         /* target, pname, four floats */
    OP_TEXGEN,         /* coord, pname, four values (two words each) */
    OP_TEXIMAGE,       /* dims, target, level, ifmt, w, h, border, format, kind; data: the unpacked image */
    OP_TEXSUBIMAGE,    /* dims, target, level, xoff, yoff, w, h, format, kind; data: the unpacked image */
    OP_TEXPARAMETER,   /* target, pname, four floats */
    OP_TRANSLATE,      /* x, y, z */
    OP_VERTEX,         /* x, y, z, w */
    OP_VIEWPORT,       /* x, y, w, h */
    /* The extensions' */
    OP_ACTIVETEXTURE,  /* unit (-1: none) */
    OP_MULTITEXCOORD,  /* unit (-1: none), s, t, r, q */
    OP_POINTPARAM,     /* pname, three floats, 1 for the scalar call */
    OP_COLORTABLE,     /* target, ifmt, width, format, kind; data: the unpacked table */
    OP_COLORSUBTABLE,  /* target, start, count, format, kind; data: the unpacked entries */
    OP_COUNT
};

/* A list is being compiled, and this is not a list running. */
static inline int
ogl_dl_on(const ogl_ctx_t *c)
{
    return c->gl->list_compiling && !c->gl->list_replaying;
}

/* Records an operation with its words (and data) into the list being
   compiled. Returns 1 if the command is not to run now (GL_COMPILE). A
   list that cannot grow records GL_OUT_OF_MEMORY and keeps what it has. */
int ogl_dl(ogl_ctx_t *c, int op, const DWORD *words, DWORD n);
int ogl_dl_data(ogl_ctx_t *c, int op, const DWORD *words, DWORD n, const void *data, DWORD bytes);

/* Runs a list (glCallList), as GL nests them (at most OGL_LIST_NESTING). */
void ogl_dl_call(ogl_ctx_t *c, GLuint list);

/* A share group's lists: made at its first list, freed with the group. */
void ogl_dl_group_free(ogl_group_t *g);
/* The list a context was compiling, dropped with the context. */
void ogl_dl_ctx_free(ogl_ctx_t *c);

/* A float or a double as list words. */
static inline DWORD
ogl_fw(float f)
{
    DWORD v;

    memcpy(&v, &f, 4);
    return v;
}

static inline float
ogl_wf(DWORD v)
{
    float f;

    memcpy(&f, &v, 4);
    return f;
}

static inline void
ogl_dw(DWORD *w, double d)
{
    memcpy(w, &d, 8);
}

static inline double
ogl_wd(const DWORD *w)
{
    double d;

    memcpy(&d, w, 8);
    return d;
}

/* An entry point's recording: its operation with its words; returns from
   the entry point when the command is only compiled. */
#define OGL_DL(op, ...)                                                   \
    do {                                                                  \
        if (ogl_dl_on(c)) {                                               \
            const DWORD w_[] = { __VA_ARGS__ };                           \
            if (ogl_dl(c, (op), w_, (DWORD) (sizeof(w_) / sizeof(w_[0])))) \
                return;                                                   \
        }                                                                 \
    } while (0)

#define OGL_DL0(op)                                    \
    do {                                               \
        if (ogl_dl_on(c) && ogl_dl(c, (op), NULL, 0))  \
            return;                                    \
    } while (0)

#endif /* VGPU98_ICD_LISTS_H */

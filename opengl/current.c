/*
 * The current values and the vertex calls (GL 1.1, section 2.7): colour,
 * index, normal, texture coordinates, edge flag and vertices, every variant
 * taken to its one float form (integer colours and normals by GL's
 * conversion: the largest positive integer to 1, the most negative to -1),
 * and glRect as GL defines it.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "lists.h"
#include "prim.h"

/* GL's integer conversions (Table 2.6). */
#define UB(v) ((float) (v) / 255.0f)
#define SB(v) ((float) ((2.0 * (v) + 1.0) / 255.0))
#define US(v) ((float) (v) / 65535.0f)
#define SS(v) ((float) ((2.0 * (v) + 1.0) / 65535.0))
#define UI(v) ((float) ((double) (v) / 4294967295.0))
#define SI(v) ((float) ((2.0 * (double) (v) + 1.0) / 4294967295.0))

/* --- Colour ------------------------------------------------------------------------- */

void STDCALL
ogl_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    float *cur;
    OGL_CTX();

    OGL_DL(OP_COLOR, ogl_fw(r), ogl_fw(g), ogl_fw(b), ogl_fw(a));
    cur    = c->gl->current.color;
    cur[0] = r;
    cur[1] = g;
    cur[2] = b;
    cur[3] = a;
    if (c->gl->enables & OGL_E_COLOR_MATERIAL)
        ogl_color_material(c);
}

void STDCALL ogl_Color3b(GLbyte r, GLbyte g, GLbyte b) { ogl_Color4f(SB(r), SB(g), SB(b), 1.0f); }
void STDCALL ogl_Color3bv(const GLbyte *v) { ogl_Color4f(SB(v[0]), SB(v[1]), SB(v[2]), 1.0f); }
void STDCALL ogl_Color3d(GLdouble r, GLdouble g, GLdouble b) { ogl_Color4f((float) r, (float) g, (float) b, 1.0f); }
void STDCALL ogl_Color3dv(const GLdouble *v) { ogl_Color4f((float) v[0], (float) v[1], (float) v[2], 1.0f); }
void STDCALL ogl_Color3f(GLfloat r, GLfloat g, GLfloat b) { ogl_Color4f(r, g, b, 1.0f); }
void STDCALL ogl_Color3fv(const GLfloat *v) { ogl_Color4f(v[0], v[1], v[2], 1.0f); }
void STDCALL ogl_Color3i(GLint r, GLint g, GLint b) { ogl_Color4f(SI(r), SI(g), SI(b), 1.0f); }
void STDCALL ogl_Color3iv(const GLint *v) { ogl_Color4f(SI(v[0]), SI(v[1]), SI(v[2]), 1.0f); }
void STDCALL ogl_Color3s(GLshort r, GLshort g, GLshort b) { ogl_Color4f(SS(r), SS(g), SS(b), 1.0f); }
void STDCALL ogl_Color3sv(const GLshort *v) { ogl_Color4f(SS(v[0]), SS(v[1]), SS(v[2]), 1.0f); }
void STDCALL ogl_Color3ub(GLubyte r, GLubyte g, GLubyte b) { ogl_Color4f(UB(r), UB(g), UB(b), 1.0f); }
void STDCALL ogl_Color3ubv(const GLubyte *v) { ogl_Color4f(UB(v[0]), UB(v[1]), UB(v[2]), 1.0f); }
void STDCALL ogl_Color3ui(GLuint r, GLuint g, GLuint b) { ogl_Color4f(UI(r), UI(g), UI(b), 1.0f); }
void STDCALL ogl_Color3uiv(const GLuint *v) { ogl_Color4f(UI(v[0]), UI(v[1]), UI(v[2]), 1.0f); }
void STDCALL ogl_Color3us(GLushort r, GLushort g, GLushort b) { ogl_Color4f(US(r), US(g), US(b), 1.0f); }
void STDCALL ogl_Color3usv(const GLushort *v) { ogl_Color4f(US(v[0]), US(v[1]), US(v[2]), 1.0f); }
void STDCALL ogl_Color4b(GLbyte r, GLbyte g, GLbyte b, GLbyte a) { ogl_Color4f(SB(r), SB(g), SB(b), SB(a)); }
void STDCALL ogl_Color4bv(const GLbyte *v) { ogl_Color4f(SB(v[0]), SB(v[1]), SB(v[2]), SB(v[3])); }
void STDCALL ogl_Color4d(GLdouble r, GLdouble g, GLdouble b, GLdouble a) { ogl_Color4f((float) r, (float) g, (float) b, (float) a); }
void STDCALL ogl_Color4dv(const GLdouble *v) { ogl_Color4f((float) v[0], (float) v[1], (float) v[2], (float) v[3]); }
void STDCALL ogl_Color4fv(const GLfloat *v) { ogl_Color4f(v[0], v[1], v[2], v[3]); }
void STDCALL ogl_Color4i(GLint r, GLint g, GLint b, GLint a) { ogl_Color4f(SI(r), SI(g), SI(b), SI(a)); }
void STDCALL ogl_Color4iv(const GLint *v) { ogl_Color4f(SI(v[0]), SI(v[1]), SI(v[2]), SI(v[3])); }
void STDCALL ogl_Color4s(GLshort r, GLshort g, GLshort b, GLshort a) { ogl_Color4f(SS(r), SS(g), SS(b), SS(a)); }
void STDCALL ogl_Color4sv(const GLshort *v) { ogl_Color4f(SS(v[0]), SS(v[1]), SS(v[2]), SS(v[3])); }
void STDCALL ogl_Color4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { ogl_Color4f(UB(r), UB(g), UB(b), UB(a)); }
void STDCALL ogl_Color4ubv(const GLubyte *v) { ogl_Color4f(UB(v[0]), UB(v[1]), UB(v[2]), UB(v[3])); }
void STDCALL ogl_Color4ui(GLuint r, GLuint g, GLuint b, GLuint a) { ogl_Color4f(UI(r), UI(g), UI(b), UI(a)); }
void STDCALL ogl_Color4uiv(const GLuint *v) { ogl_Color4f(UI(v[0]), UI(v[1]), UI(v[2]), UI(v[3])); }
void STDCALL ogl_Color4us(GLushort r, GLushort g, GLushort b, GLushort a) { ogl_Color4f(US(r), US(g), US(b), US(a)); }
void STDCALL ogl_Color4usv(const GLushort *v) { ogl_Color4f(US(v[0]), US(v[1]), US(v[2]), US(v[3])); }

/* --- Index (colour-index mode's: kept, as GL keeps it, and unused) ------------------- */

void STDCALL
ogl_Indexf(GLfloat i)
{
    OGL_CTX();
    OGL_DL(OP_INDEX, ogl_fw(i));
    c->gl->current.index = i;
}

void STDCALL ogl_Indexd(GLdouble i) { ogl_Indexf((float) i); }
void STDCALL ogl_Indexdv(const GLdouble *v) { ogl_Indexf((float) v[0]); }
void STDCALL ogl_Indexfv(const GLfloat *v) { ogl_Indexf(v[0]); }
void STDCALL ogl_Indexi(GLint i) { ogl_Indexf((float) i); }
void STDCALL ogl_Indexiv(const GLint *v) { ogl_Indexf((float) v[0]); }
void STDCALL ogl_Indexs(GLshort i) { ogl_Indexf((float) i); }
void STDCALL ogl_Indexsv(const GLshort *v) { ogl_Indexf((float) v[0]); }
void STDCALL ogl_Indexub(GLubyte i) { ogl_Indexf((float) i); }
void STDCALL ogl_Indexubv(const GLubyte *v) { ogl_Indexf((float) v[0]); }

/* --- Normal -------------------------------------------------------------------------- */

void STDCALL
ogl_Normal3f(GLfloat x, GLfloat y, GLfloat z)
{
    float *n;
    OGL_CTX();

    OGL_DL(OP_NORMAL, ogl_fw(x), ogl_fw(y), ogl_fw(z));
    n    = c->gl->current.normal;
    n[0] = x;
    n[1] = y;
    n[2] = z;
}

void STDCALL ogl_Normal3b(GLbyte x, GLbyte y, GLbyte z) { ogl_Normal3f(SB(x), SB(y), SB(z)); }
void STDCALL ogl_Normal3bv(const GLbyte *v) { ogl_Normal3f(SB(v[0]), SB(v[1]), SB(v[2])); }
void STDCALL ogl_Normal3d(GLdouble x, GLdouble y, GLdouble z) { ogl_Normal3f((float) x, (float) y, (float) z); }
void STDCALL ogl_Normal3dv(const GLdouble *v) { ogl_Normal3f((float) v[0], (float) v[1], (float) v[2]); }
void STDCALL ogl_Normal3fv(const GLfloat *v) { ogl_Normal3f(v[0], v[1], v[2]); }
void STDCALL ogl_Normal3i(GLint x, GLint y, GLint z) { ogl_Normal3f(SI(x), SI(y), SI(z)); }
void STDCALL ogl_Normal3iv(const GLint *v) { ogl_Normal3f(SI(v[0]), SI(v[1]), SI(v[2])); }
void STDCALL ogl_Normal3s(GLshort x, GLshort y, GLshort z) { ogl_Normal3f(SS(x), SS(y), SS(z)); }
void STDCALL ogl_Normal3sv(const GLshort *v) { ogl_Normal3f(SS(v[0]), SS(v[1]), SS(v[2])); }

/* --- Texture coordinates ---------------------------------------------------------------- */

void STDCALL
ogl_TexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    float *tc;
    OGL_CTX();

    OGL_DL(OP_TEXCOORD, ogl_fw(s), ogl_fw(t), ogl_fw(r), ogl_fw(q));
    /* Unit 0's, or SGIS's coordinate set's (multitex.c) */
    tc    = c->gl->current.texcoord[c->gl->client.coord_set];
    tc[0] = s;
    tc[1] = t;
    tc[2] = r;
    tc[3] = q;
}

void STDCALL ogl_TexCoord1d(GLdouble s) { ogl_TexCoord4f((float) s, 0.0f, 0.0f, 1.0f); }
void STDCALL ogl_TexCoord1dv(const GLdouble *v) { ogl_TexCoord4f((float) v[0], 0.0f, 0.0f, 1.0f); }
void STDCALL ogl_TexCoord1f(GLfloat s) { ogl_TexCoord4f(s, 0.0f, 0.0f, 1.0f); }
void STDCALL ogl_TexCoord1fv(const GLfloat *v) { ogl_TexCoord4f(v[0], 0.0f, 0.0f, 1.0f); }
void STDCALL ogl_TexCoord1i(GLint s) { ogl_TexCoord4f((float) s, 0.0f, 0.0f, 1.0f); }
void STDCALL ogl_TexCoord1iv(const GLint *v) { ogl_TexCoord4f((float) v[0], 0.0f, 0.0f, 1.0f); }
void STDCALL ogl_TexCoord1s(GLshort s) { ogl_TexCoord4f((float) s, 0.0f, 0.0f, 1.0f); }
void STDCALL ogl_TexCoord1sv(const GLshort *v) { ogl_TexCoord4f((float) v[0], 0.0f, 0.0f, 1.0f); }
void STDCALL ogl_TexCoord2d(GLdouble s, GLdouble t) { ogl_TexCoord4f((float) s, (float) t, 0.0f, 1.0f); }
void STDCALL ogl_TexCoord2dv(const GLdouble *v) { ogl_TexCoord4f((float) v[0], (float) v[1], 0.0f, 1.0f); }
void STDCALL ogl_TexCoord2f(GLfloat s, GLfloat t) { ogl_TexCoord4f(s, t, 0.0f, 1.0f); }
void STDCALL ogl_TexCoord2fv(const GLfloat *v) { ogl_TexCoord4f(v[0], v[1], 0.0f, 1.0f); }
void STDCALL ogl_TexCoord2i(GLint s, GLint t) { ogl_TexCoord4f((float) s, (float) t, 0.0f, 1.0f); }
void STDCALL ogl_TexCoord2iv(const GLint *v) { ogl_TexCoord4f((float) v[0], (float) v[1], 0.0f, 1.0f); }
void STDCALL ogl_TexCoord2s(GLshort s, GLshort t) { ogl_TexCoord4f((float) s, (float) t, 0.0f, 1.0f); }
void STDCALL ogl_TexCoord2sv(const GLshort *v) { ogl_TexCoord4f((float) v[0], (float) v[1], 0.0f, 1.0f); }
void STDCALL ogl_TexCoord3d(GLdouble s, GLdouble t, GLdouble r) { ogl_TexCoord4f((float) s, (float) t, (float) r, 1.0f); }
void STDCALL ogl_TexCoord3dv(const GLdouble *v) { ogl_TexCoord4f((float) v[0], (float) v[1], (float) v[2], 1.0f); }
void STDCALL ogl_TexCoord3f(GLfloat s, GLfloat t, GLfloat r) { ogl_TexCoord4f(s, t, r, 1.0f); }
void STDCALL ogl_TexCoord3fv(const GLfloat *v) { ogl_TexCoord4f(v[0], v[1], v[2], 1.0f); }
void STDCALL ogl_TexCoord3i(GLint s, GLint t, GLint r) { ogl_TexCoord4f((float) s, (float) t, (float) r, 1.0f); }
void STDCALL ogl_TexCoord3iv(const GLint *v) { ogl_TexCoord4f((float) v[0], (float) v[1], (float) v[2], 1.0f); }
void STDCALL ogl_TexCoord3s(GLshort s, GLshort t, GLshort r) { ogl_TexCoord4f((float) s, (float) t, (float) r, 1.0f); }
void STDCALL ogl_TexCoord3sv(const GLshort *v) { ogl_TexCoord4f((float) v[0], (float) v[1], (float) v[2], 1.0f); }
void STDCALL ogl_TexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q) { ogl_TexCoord4f((float) s, (float) t, (float) r, (float) q); }
void STDCALL ogl_TexCoord4dv(const GLdouble *v) { ogl_TexCoord4f((float) v[0], (float) v[1], (float) v[2], (float) v[3]); }
void STDCALL ogl_TexCoord4fv(const GLfloat *v) { ogl_TexCoord4f(v[0], v[1], v[2], v[3]); }
void STDCALL ogl_TexCoord4i(GLint s, GLint t, GLint r, GLint q) { ogl_TexCoord4f((float) s, (float) t, (float) r, (float) q); }
void STDCALL ogl_TexCoord4iv(const GLint *v) { ogl_TexCoord4f((float) v[0], (float) v[1], (float) v[2], (float) v[3]); }
void STDCALL ogl_TexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q) { ogl_TexCoord4f((float) s, (float) t, (float) r, (float) q); }
void STDCALL ogl_TexCoord4sv(const GLshort *v) { ogl_TexCoord4f((float) v[0], (float) v[1], (float) v[2], (float) v[3]); }

/* --- Edge flag ------------------------------------------------------------------------ */

void STDCALL
ogl_EdgeFlag(GLboolean flag)
{
    OGL_CTX();
    OGL_DL(OP_EDGEFLAG, flag);
    c->gl->current.edge_flag = flag ? GL_TRUE : GL_FALSE;
}

void STDCALL
ogl_EdgeFlagv(const GLboolean *flag)
{
    ogl_EdgeFlag(flag[0]);
}

/* --- Vertices --------------------------------------------------------------------------- */

void STDCALL
ogl_Vertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    float v[4];
    OGL_CTX();

    OGL_DL(OP_VERTEX, ogl_fw(x), ogl_fw(y), ogl_fw(z), ogl_fw(w));
    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = w;
    ogl_prim_vertex(c, v);
}

void STDCALL ogl_Vertex2d(GLdouble x, GLdouble y) { ogl_Vertex4f((float) x, (float) y, 0.0f, 1.0f); }
void STDCALL ogl_Vertex2dv(const GLdouble *v) { ogl_Vertex4f((float) v[0], (float) v[1], 0.0f, 1.0f); }
void STDCALL ogl_Vertex2f(GLfloat x, GLfloat y) { ogl_Vertex4f(x, y, 0.0f, 1.0f); }
void STDCALL ogl_Vertex2fv(const GLfloat *v) { ogl_Vertex4f(v[0], v[1], 0.0f, 1.0f); }
void STDCALL ogl_Vertex2i(GLint x, GLint y) { ogl_Vertex4f((float) x, (float) y, 0.0f, 1.0f); }
void STDCALL ogl_Vertex2iv(const GLint *v) { ogl_Vertex4f((float) v[0], (float) v[1], 0.0f, 1.0f); }
void STDCALL ogl_Vertex2s(GLshort x, GLshort y) { ogl_Vertex4f((float) x, (float) y, 0.0f, 1.0f); }
void STDCALL ogl_Vertex2sv(const GLshort *v) { ogl_Vertex4f((float) v[0], (float) v[1], 0.0f, 1.0f); }
void STDCALL ogl_Vertex3d(GLdouble x, GLdouble y, GLdouble z) { ogl_Vertex4f((float) x, (float) y, (float) z, 1.0f); }
void STDCALL ogl_Vertex3dv(const GLdouble *v) { ogl_Vertex4f((float) v[0], (float) v[1], (float) v[2], 1.0f); }
void STDCALL ogl_Vertex3f(GLfloat x, GLfloat y, GLfloat z) { ogl_Vertex4f(x, y, z, 1.0f); }
void STDCALL ogl_Vertex3fv(const GLfloat *v) { ogl_Vertex4f(v[0], v[1], v[2], 1.0f); }
void STDCALL ogl_Vertex3i(GLint x, GLint y, GLint z) { ogl_Vertex4f((float) x, (float) y, (float) z, 1.0f); }
void STDCALL ogl_Vertex3iv(const GLint *v) { ogl_Vertex4f((float) v[0], (float) v[1], (float) v[2], 1.0f); }
void STDCALL ogl_Vertex3s(GLshort x, GLshort y, GLshort z) { ogl_Vertex4f((float) x, (float) y, (float) z, 1.0f); }
void STDCALL ogl_Vertex3sv(const GLshort *v) { ogl_Vertex4f((float) v[0], (float) v[1], (float) v[2], 1.0f); }
void STDCALL ogl_Vertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w) { ogl_Vertex4f((float) x, (float) y, (float) z, (float) w); }
void STDCALL ogl_Vertex4dv(const GLdouble *v) { ogl_Vertex4f((float) v[0], (float) v[1], (float) v[2], (float) v[3]); }
void STDCALL ogl_Vertex4fv(const GLfloat *v) { ogl_Vertex4f(v[0], v[1], v[2], v[3]); }
void STDCALL ogl_Vertex4i(GLint x, GLint y, GLint z, GLint w) { ogl_Vertex4f((float) x, (float) y, (float) z, (float) w); }
void STDCALL ogl_Vertex4iv(const GLint *v) { ogl_Vertex4f((float) v[0], (float) v[1], (float) v[2], (float) v[3]); }
void STDCALL ogl_Vertex4s(GLshort x, GLshort y, GLshort z, GLshort w) { ogl_Vertex4f((float) x, (float) y, (float) z, (float) w); }
void STDCALL ogl_Vertex4sv(const GLshort *v) { ogl_Vertex4f((float) v[0], (float) v[1], (float) v[2], (float) v[3]); }

void STDCALL
ogl_Begin(GLenum mode)
{
    OGL_CTX();
    OGL_DL(OP_BEGIN, mode);
    ogl_prim_begin(c, mode);
}

void STDCALL
ogl_End(void)
{
    OGL_CTX();
    OGL_DL0(OP_END);
    ogl_prim_end(c);
}

/* glRect: exactly glBegin(GL_POLYGON), the four corners, glEnd. */
void STDCALL
ogl_Rectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
    float v[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    OGL_CTX();

    OGL_DL(OP_RECT, ogl_fw(x1), ogl_fw(y1), ogl_fw(x2), ogl_fw(y2));
    if (ogl_inside(c))
        return;
    ogl_prim_begin(c, GL_POLYGON);
    v[0] = x1;
    v[1] = y1;
    ogl_prim_vertex(c, v);
    v[0] = x2;
    ogl_prim_vertex(c, v);
    v[1] = y2;
    ogl_prim_vertex(c, v);
    v[0] = x1;
    ogl_prim_vertex(c, v);
    ogl_prim_end(c);
}

void STDCALL ogl_Rectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2) { ogl_Rectf((float) x1, (float) y1, (float) x2, (float) y2); }
void STDCALL ogl_Rectdv(const GLdouble *a, const GLdouble *b) { ogl_Rectf((float) a[0], (float) a[1], (float) b[0], (float) b[1]); }
void STDCALL ogl_Rectfv(const GLfloat *a, const GLfloat *b) { ogl_Rectf(a[0], a[1], b[0], b[1]); }
void STDCALL ogl_Recti(GLint x1, GLint y1, GLint x2, GLint y2) { ogl_Rectf((float) x1, (float) y1, (float) x2, (float) y2); }
void STDCALL ogl_Rectiv(const GLint *a, const GLint *b) { ogl_Rectf((float) a[0], (float) a[1], (float) b[0], (float) b[1]); }
void STDCALL ogl_Rects(GLshort x1, GLshort y1, GLshort x2, GLshort y2) { ogl_Rectf((float) x1, (float) y1, (float) x2, (float) y2); }
void STDCALL ogl_Rectsv(const GLshort *a, const GLshort *b) { ogl_Rectf((float) a[0], (float) a[1], (float) b[0], (float) b[1]); }

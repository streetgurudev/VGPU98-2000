/*
 * Lighting's state (GL 1.1, section 2.13): lights, the light model,
 * materials, colour material, the shade model, and their queries. A
 * light's position and spot direction go into eye coordinates when they
 * are given, through the modelview then (its upper 3x3 for the direction).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcalls.h"
#include "glmath.h"
#include "lists.h"
#include "prim.h"
#include "regs.h"

/* GL's integer-to-float rule for colours (and normals): the largest
   positive integer to 1.0, the most negative to -1.0. */
static float
int_color(GLint v)
{
    return (float) ((2.0 * (double) v + 1.0) / 4294967295.0);
}

/* How many values a light's parameter takes; 0 for no such parameter. */
static int
light_count(GLenum pname)
{
    switch (pname) {
        case GL_AMBIENT:
        case GL_DIFFUSE:
        case GL_SPECULAR:
        case GL_POSITION:
            return 4;
        case GL_SPOT_DIRECTION:
            return 3;
        case GL_SPOT_EXPONENT:
        case GL_SPOT_CUTOFF:
        case GL_CONSTANT_ATTENUATION:
        case GL_LINEAR_ATTENUATION:
        case GL_QUADRATIC_ATTENUATION:
            return 1;
        default:
            return 0;
    }
}

void STDCALL
ogl_Lightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    ogl_light_t *l;
    const int    n = light_count(pname);
    float        v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    OGL_CTX();

    if (n && params)
        memcpy(v, params, (size_t) n * sizeof(float));
    OGL_DL(OP_LIGHT, light, pname, ogl_fw(v[0]), ogl_fw(v[1]), ogl_fw(v[2]), ogl_fw(v[3]));
    if ((light < GL_LIGHT0) || (light >= GL_LIGHT0 + OGL_MAX_LIGHTS) || !n) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    l = &c->gl->lighting.light[light - GL_LIGHT0];
    switch (pname) {
        case GL_AMBIENT:
            memcpy(l->ambient, v, sizeof(l->ambient));
            break;
        case GL_DIFFUSE:
            memcpy(l->diffuse, v, sizeof(l->diffuse));
            break;
        case GL_SPECULAR:
            memcpy(l->specular, v, sizeof(l->specular));
            break;
        case GL_POSITION:
            ogl_mat_vec(l->position, c->gl->modelview[c->gl->modelview_depth - 1].m, v);
            break;
        case GL_SPOT_DIRECTION: {
            const float *m = c->gl->modelview[c->gl->modelview_depth - 1].m;
            int          k;

            for (k = 0; k < 3; k++)
                l->spot_dir[k] = m[k] * v[0] + m[4 + k] * v[1] + m[8 + k] * v[2];
            break;
        }
        case GL_SPOT_EXPONENT:
            if ((v[0] < 0.0f) || (v[0] > 128.0f)) {
                ogl_error(c, GL_INVALID_VALUE);
                return;
            }
            l->spot_exponent = v[0];
            break;
        case GL_SPOT_CUTOFF:
            if (((v[0] < 0.0f) || (v[0] > 90.0f)) && (v[0] != 180.0f)) {
                ogl_error(c, GL_INVALID_VALUE);
                return;
            }
            l->spot_cutoff = v[0];
            break;
        default: /* the attenuations */
            if (v[0] < 0.0f) {
                ogl_error(c, GL_INVALID_VALUE);
                return;
            }
            l->attenuation[pname - GL_CONSTANT_ATTENUATION] = v[0];
            break;
    }
    ogl_regs_lighting(c);
}

void STDCALL
ogl_Lightf(GLenum light, GLenum pname, GLfloat param)
{
    OGL_CTX();
    if (light_count(pname) != 1) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    ogl_Lightfv(light, pname, &param);
}

void STDCALL
ogl_Lighti(GLenum light, GLenum pname, GLint param)
{
    ogl_Lightf(light, pname, (GLfloat) param);
}

void STDCALL
ogl_Lightiv(GLenum light, GLenum pname, const GLint *params)
{
    float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int   i, n = light_count(pname);

    for (i = 0; params && (i < n); i++) {
        if ((pname == GL_AMBIENT) || (pname == GL_DIFFUSE) || (pname == GL_SPECULAR))
            v[i] = int_color(params[i]);
        else
            v[i] = (float) params[i];
    }
    ogl_Lightfv(light, pname, v);
}

void STDCALL
ogl_LightModelfv(GLenum pname, const GLfloat *params)
{
    ogl_lighting_t *lt;
    float           v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    OGL_CTX();

    if (params)
        memcpy(v, params, ((pname == GL_LIGHT_MODEL_AMBIENT) ? 4 : 1) * sizeof(float));
    OGL_DL(OP_LIGHTMODEL, pname, ogl_fw(v[0]), ogl_fw(v[1]), ogl_fw(v[2]), ogl_fw(v[3]));
    if (ogl_inside(c))
        return;
    lt = &c->gl->lighting;
    switch (pname) {
        case GL_LIGHT_MODEL_AMBIENT:
            memcpy(lt->model_ambient, v, sizeof(lt->model_ambient));
            break;
        case GL_LIGHT_MODEL_LOCAL_VIEWER:
            lt->local_viewer = v[0] != 0.0f;
            break;
        case GL_LIGHT_MODEL_TWO_SIDE:
            lt->two_side = v[0] != 0.0f;
            break;
        default:
            ogl_error(c, GL_INVALID_ENUM);
            return;
    }
    ogl_regs_lighting(c);
}

void STDCALL
ogl_LightModelf(GLenum pname, GLfloat param)
{
    OGL_CTX();
    if (pname == GL_LIGHT_MODEL_AMBIENT) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    ogl_LightModelfv(pname, &param);
}

void STDCALL
ogl_LightModeli(GLenum pname, GLint param)
{
    ogl_LightModelf(pname, (GLfloat) param);
}

void STDCALL
ogl_LightModeliv(GLenum pname, const GLint *params)
{
    float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int   i;

    if (params) {
        if (pname == GL_LIGHT_MODEL_AMBIENT) {
            for (i = 0; i < 4; i++)
                v[i] = int_color(params[i]);
        } else
            v[0] = (float) params[0];
    }
    ogl_LightModelfv(pname, v);
}

/* A material's parameter: its values' count, 0 for none. */
static int
material_count(GLenum pname)
{
    switch (pname) {
        case GL_AMBIENT:
        case GL_DIFFUSE:
        case GL_SPECULAR:
        case GL_EMISSION:
        case GL_AMBIENT_AND_DIFFUSE:
            return 4;
        case GL_SHININESS:
            return 1;
        case GL_COLOR_INDEXES:
            return 3;
        default:
            return 0;
    }
}

/* Sets one face's material parameter (no checks). */
void
ogl_material_set(ogl_material_t *m, GLenum pname, const float *v)
{
    switch (pname) {
        case GL_AMBIENT:
            memcpy(m->ambient, v, 4 * sizeof(float));
            break;
        case GL_DIFFUSE:
            memcpy(m->diffuse, v, 4 * sizeof(float));
            break;
        case GL_AMBIENT_AND_DIFFUSE:
            memcpy(m->ambient, v, 4 * sizeof(float));
            memcpy(m->diffuse, v, 4 * sizeof(float));
            break;
        case GL_SPECULAR:
            memcpy(m->specular, v, 4 * sizeof(float));
            break;
        case GL_EMISSION:
            memcpy(m->emission, v, 4 * sizeof(float));
            break;
        case GL_SHININESS:
            m->shininess = v[0];
            break;
        case GL_COLOR_INDEXES:
            memcpy(m->indexes, v, 3 * sizeof(float));
            break;
        default:
            break;
    }
}

/* Colour material: the current colour into the materials it tracks, as GL
   has it change them while COLOR_MATERIAL is on. */
void
ogl_color_material(ogl_ctx_t *c)
{
    ogl_lighting_t *lt = &c->gl->lighting;
    int             f;

    if (!(c->gl->enables & OGL_E_COLOR_MATERIAL))
        return;
    for (f = 0; f < 2; f++) {
        if ((lt->cm_face == GL_FRONT_AND_BACK) || (lt->cm_face == (f ? GL_BACK : GL_FRONT)))
            ogl_material_set(&lt->material[f], lt->cm_mode, c->gl->current.color);
    }
}

/* Allowed between glBegin and glEnd: there, the vertices after it take the
   new material (prim.c keeps a copy for them). */
void STDCALL
ogl_Materialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    ogl_lighting_t *lt;
    const int       n = material_count(pname);
    float           v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int             f;
    OGL_CTX();

    if (n && params)
        memcpy(v, params, (size_t) n * sizeof(float));
    OGL_DL(OP_MATERIAL, face, pname, ogl_fw(v[0]), ogl_fw(v[1]), ogl_fw(v[2]), ogl_fw(v[3]));
    if (((face != GL_FRONT) && (face != GL_BACK) && (face != GL_FRONT_AND_BACK)) || !n) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if ((pname == GL_SHININESS) && ((v[0] < 0.0f) || (v[0] > 128.0f))) {
        ogl_error(c, GL_INVALID_VALUE);
        return;
    }
    lt = &c->gl->lighting;
    for (f = 0; f < 2; f++) {
        if ((face == GL_FRONT_AND_BACK) || (face == (f ? GL_BACK : GL_FRONT)))
            ogl_material_set(&lt->material[f], pname, v);
    }
    if (c->gl->in_begin)
        ogl_prim_material(c);
    else
        ogl_regs_lighting(c);
}

void STDCALL
ogl_Materialf(GLenum face, GLenum pname, GLfloat param)
{
    OGL_CTX();
    if (pname != GL_SHININESS) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    ogl_Materialfv(face, pname, &param);
}

void STDCALL
ogl_Materiali(GLenum face, GLenum pname, GLint param)
{
    ogl_Materialf(face, pname, (GLfloat) param);
}

void STDCALL
ogl_Materialiv(GLenum face, GLenum pname, const GLint *params)
{
    float v[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    int   i, n = material_count(pname);

    for (i = 0; params && (i < n); i++) {
        if ((pname == GL_SHININESS) || (pname == GL_COLOR_INDEXES))
            v[i] = (float) params[i];
        else
            v[i] = int_color(params[i]);
    }
    ogl_Materialfv(face, pname, v);
}

void STDCALL
ogl_ColorMaterial(GLenum face, GLenum mode)
{
    OGL_CTX();
    OGL_DL(OP_COLORMATERIAL, face, mode);
    if (((face != GL_FRONT) && (face != GL_BACK) && (face != GL_FRONT_AND_BACK)) ||
        ((mode != GL_EMISSION) && (mode != GL_AMBIENT) && (mode != GL_DIFFUSE) && (mode != GL_SPECULAR) &&
         (mode != GL_AMBIENT_AND_DIFFUSE))) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->lighting.cm_face = face;
    c->gl->lighting.cm_mode = mode;
    ogl_color_material(c);
    ogl_regs_lighting(c);
}

void STDCALL
ogl_ShadeModel(GLenum mode)
{
    OGL_CTX();
    OGL_DL(OP_SHADEMODEL, mode);
    if ((mode != GL_FLAT) && (mode != GL_SMOOTH)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c))
        return;
    c->gl->lighting.shade_model = mode;
    ogl_regs_lighting(c);
}

/* --- Queries ------------------------------------------------------------------------- */

/* A colour as GL gives it to an integer query: -1-1 mapped onto the
   integers, linearly. */
static GLint
color_int(float f)
{
    const double v = ((double) f * 4294967295.0 - 1.0) / 2.0;

    if (v >= 2147483647.0)
        return 2147483647;
    if (v <= -2147483648.0)
        return (GLint) 0x80000000u;
    return (GLint) (v + ((v >= 0.0) ? 0.5 : -0.5));
}

void STDCALL
ogl_GetLightfv(GLenum light, GLenum pname, GLfloat *params)
{
    const ogl_light_t *l;
    OGL_CTX();

    if ((light < GL_LIGHT0) || (light >= GL_LIGHT0 + OGL_MAX_LIGHTS) || !light_count(pname)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c) || !params)
        return;
    l = &c->gl->lighting.light[light - GL_LIGHT0];
    switch (pname) {
        case GL_AMBIENT:
            memcpy(params, l->ambient, 4 * sizeof(float));
            break;
        case GL_DIFFUSE:
            memcpy(params, l->diffuse, 4 * sizeof(float));
            break;
        case GL_SPECULAR:
            memcpy(params, l->specular, 4 * sizeof(float));
            break;
        case GL_POSITION:
            memcpy(params, l->position, 4 * sizeof(float));
            break;
        case GL_SPOT_DIRECTION:
            memcpy(params, l->spot_dir, 3 * sizeof(float));
            break;
        case GL_SPOT_EXPONENT:
            params[0] = l->spot_exponent;
            break;
        case GL_SPOT_CUTOFF:
            params[0] = l->spot_cutoff;
            break;
        default:
            params[0] = l->attenuation[pname - GL_CONSTANT_ATTENUATION];
            break;
    }
}

void STDCALL
ogl_GetLightiv(GLenum light, GLenum pname, GLint *params)
{
    float v[4];
    int   i, n = light_count(pname);
    OGL_CTX();

    if (!n || (light < GL_LIGHT0) || (light >= GL_LIGHT0 + OGL_MAX_LIGHTS)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (c->gl->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    ogl_GetLightfv(light, pname, v);
    for (i = 0; params && (i < n); i++) {
        if ((pname == GL_AMBIENT) || (pname == GL_DIFFUSE) || (pname == GL_SPECULAR))
            params[i] = color_int(v[i]);
        else
            params[i] = ogl_round(v[i]);
    }
}

void STDCALL
ogl_GetMaterialfv(GLenum face, GLenum pname, GLfloat *params)
{
    const ogl_material_t *m;
    OGL_CTX();

    if (((face != GL_FRONT) && (face != GL_BACK)) || !material_count(pname) || (pname == GL_AMBIENT_AND_DIFFUSE)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (ogl_inside(c) || !params)
        return;
    m = &c->gl->lighting.material[face == GL_BACK];
    switch (pname) {
        case GL_AMBIENT:
            memcpy(params, m->ambient, 4 * sizeof(float));
            break;
        case GL_DIFFUSE:
            memcpy(params, m->diffuse, 4 * sizeof(float));
            break;
        case GL_SPECULAR:
            memcpy(params, m->specular, 4 * sizeof(float));
            break;
        case GL_EMISSION:
            memcpy(params, m->emission, 4 * sizeof(float));
            break;
        case GL_SHININESS:
            params[0] = m->shininess;
            break;
        default:
            memcpy(params, m->indexes, 3 * sizeof(float));
            break;
    }
}

void STDCALL
ogl_GetMaterialiv(GLenum face, GLenum pname, GLint *params)
{
    float v[4];
    int   i, n = material_count(pname);
    OGL_CTX();

    if (((face != GL_FRONT) && (face != GL_BACK)) || !n || (pname == GL_AMBIENT_AND_DIFFUSE)) {
        ogl_error(c, GL_INVALID_ENUM);
        return;
    }
    if (c->gl->in_begin) {
        ogl_error(c, GL_INVALID_OPERATION);
        return;
    }
    ogl_GetMaterialfv(face, pname, v);
    for (i = 0; params && (i < n); i++)
        params[i] = ((pname == GL_SHININESS) || (pname == GL_COLOR_INDEXES)) ? ogl_round(v[i]) : color_int(v[i]);
}

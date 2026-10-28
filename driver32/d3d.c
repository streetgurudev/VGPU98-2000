/*
 * vgpu98dx.dll: the Direct3D HAL (spec/directx-hal.md): contexts, texture
 * handles, render and texture stage state, transform and lighting state
 * (DirectX 7), SetRenderTarget, Clear, Clear2, ValidateTextureStageState.
 * DrawPrimitives2 is in dp2.c, the DirectX 7 interface's handles in d3d7.c.
 *
 * Every context keeps its own Direct3D state. The GPU holds one state: a
 * context sends what the GPU lacks before it draws, all of it when another
 * context drew last or the GPU was reset.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3d.h"

d3d_ctx_t                 d3d_ctx[MAX_CONTEXTS];
static DWORD              ctx_serials; /* contexts ever created (ctx_token) */
static DWORD              ctx_live;    /* contexts that exist (d3d_contexts) */
d3d_tex_t                 d3d_tex[MAX_TEXTURES];
PFND3DPARSEUNKNOWNCOMMAND d3d_parse_unknown;

#define F1 0x3f800000 /* 1.0f */

/* --- Contexts ----------------------------------------------------------------- */

d3d_ctx_t *
ctx_get(DWORD handle)
{
    if ((handle < 1) || (handle > MAX_CONTEXTS) || (d3d_ctx[handle - 1].used != CTX_LIVE))
        return NULL;
    return &d3d_ctx[handle - 1];
}

DWORD
ctx_handle(const d3d_ctx_t *c)
{
    return (DWORD) (c - d3d_ctx) + 1;
}

_Static_assert(MAX_CONTEXTS < (1u << CTX_TOKEN_BITS), "a context's handle fits its token");

DWORD
ctx_token(const d3d_ctx_t *c)
{
    return (c->serial << CTX_TOKEN_BITS) | ctx_handle(c);
}

static void
ctx_viewport_whole(d3d_ctx_t *c)
{
    c->vp[0] = 0;
    c->vp[1] = 0;
    c->vp[2] = c->target ? c->target->lpGbl->wWidth : 0;
    c->vp[3] = c->target ? c->target->lpGbl->wHeight : 0;
    c->dirty |= DIRTY_VIEW;
}

/* Every state of stage s is translated again at the next flush (a legacy
   render state mapped onto it). */
static void
ctx_stage_all(d3d_ctx_t *c, DWORD s)
{
    c->dirty |= DIRTY_STAGE(s);
    c->tss_dirty[s] = ~0u;
    c->tex_changed |= 1u << s;
}

/* Direct3D's defaults; the runtime sends its own on device creation. */
static void
ctx_defaults(d3d_ctx_t *c)
{
    int s;

    memset(c->rs, 0, sizeof(c->rs));
    memset(c->tss, 0, sizeof(c->tss));
    c->rs[D3DRS_TEXTUREADDRESS]     = 1;
    c->rs[D3DRS_TEXTUREPERSPECTIVE] = 1;
    c->rs[D3DRS_ZENABLE]            = c->zbuf ? 1 : 0;
    c->rs[D3DRS_FILLMODE]           = 3;
    c->rs[D3DRS_SHADEMODE]          = 2;
    c->rs[D3DRS_ROP2]               = 13;
    c->rs[D3DRS_PLANEMASK]          = 0xffffffff;
    c->rs[D3DRS_ZWRITEENABLE]       = 1;
    c->rs[D3DRS_LASTPIXEL]          = 1;
    c->rs[D3DRS_TEXTUREMAG]         = D3DFILTER_NEAREST;
    c->rs[D3DRS_TEXTUREMIN]         = D3DFILTER_NEAREST;
    c->rs[D3DRS_SRCBLEND]           = 2;
    c->rs[D3DRS_DESTBLEND]          = 1;
    c->rs[D3DRS_TEXTUREMAPBLEND]    = D3DTBLEND_MODULATE;
    c->rs[D3DRS_CULLMODE]           = 3;
    c->rs[D3DRS_ZFUNC]              = 4;
    c->rs[D3DRS_ALPHAFUNC]          = 8;
    c->rs[D3DRS_FOGTABLEEND]        = F1;
    c->rs[D3DRS_FOGTABLEDENSITY]    = F1;
    c->rs[D3DRS_TEXTUREADDRESSU]    = 1;
    c->rs[D3DRS_TEXTUREADDRESSV]    = 1;
    c->rs[D3DRS_ANISOTROPY]         = 1;
    c->rs[D3DRS_STENCILFAIL]        = 1;
    c->rs[D3DRS_STENCILZFAIL]       = 1;
    c->rs[D3DRS_STENCILPASS]        = 1;
    c->rs[D3DRS_STENCILFUNC]        = 8;
    c->rs[D3DRS_STENCILMASK]        = 0xffffffff;
    c->rs[D3DRS_STENCILWRITEMASK]   = 0xffffffff;
    c->rs[D3DRS_TEXTUREFACTOR]      = 0xffffffff;
    /* DirectX 7's transform and lighting, AMBIENTMATERIALSOURCE as DirectX 7
       documents it (COLOR2). */
    c->rs[VGPU98_RS_CLIPPING]               = 1;
    c->rs[VGPU98_RS_LIGHTING]               = 1;
    c->rs[VGPU98_RS_COLORVERTEX]            = 1;
    c->rs[VGPU98_RS_LOCALVIEWER]            = 1;
    c->rs[VGPU98_RS_DIFFUSEMATERIALSOURCE]  = 1;
    c->rs[VGPU98_RS_SPECULARMATERIALSOURCE] = 2;
    c->rs[VGPU98_RS_AMBIENTMATERIALSOURCE]  = 2;
    /* DirectX 8's states (command set 2.1), at their defaults in every
       context: the GPU holds one state, and a DirectX 6 or 7 context, which
       never sets them, must find them as a 2.0 GPU has them. */
    c->rs[VGPU98_RS_POINTSIZE]            = F1;
    c->rs[VGPU98_RS_POINTSCALE_A]         = F1;
    c->rs[VGPU98_RS_MULTISAMPLEANTIALIAS] = 1;
    c->rs[VGPU98_RS_MULTISAMPLEMASK]      = 0xffffffff;
    c->rs[VGPU98_RS_PATCHSEGMENTS]        = F1;
    c->rs[VGPU98_RS_POINTSIZE_MAX]        = 0x42800000; /* 64.0 */
    c->rs[VGPU98_RS_COLORWRITEENABLE]     = 0xf;
    c->rs[VGPU98_RS_BLENDOP]              = VGPU98_BLENDOP_ADD;
    c->rs[VGPU98_RS_POSITIONORDER]        = 3; /* cubic */
    c->rs[VGPU98_RS_NORMALORDER]          = 1; /* linear */
    for (s = 0; s < D3D_STAGES; s++) {
        c->tss[s][VGPU98_TSS_COLOROP]       = s ? VGPU98_TOP_DISABLE : VGPU98_TOP_MODULATE;
        c->tss[s][VGPU98_TSS_COLORARG1]     = VGPU98_TA_TEXTURE;
        c->tss[s][VGPU98_TSS_COLORARG2]     = VGPU98_TA_CURRENT;
        c->tss[s][VGPU98_TSS_ALPHAOP]       = s ? VGPU98_TOP_DISABLE : VGPU98_TOP_SELECTARG1;
        c->tss[s][VGPU98_TSS_ALPHAARG1]     = VGPU98_TA_TEXTURE;
        c->tss[s][VGPU98_TSS_ALPHAARG2]     = VGPU98_TA_CURRENT;
        c->tss[s][VGPU98_TSS_TEXCOORDINDEX] = (DWORD) s;
        c->tss[s][D3DTSS_ADDRESS]           = 1;
        c->tss[s][VGPU98_TSS_ADDRESSU]      = 1;
        c->tss[s][VGPU98_TSS_ADDRESSV]      = 1;
        c->tss[s][VGPU98_TSS_MAGFILTER]     = 1;
        c->tss[s][VGPU98_TSS_MINFILTER]     = 1;
        /* DirectX 8 numbers its filters anew: its MIPFILTER default (none)
           is 0 (gpu_tss_value). */
        c->tss[s][VGPU98_TSS_MIPFILTER]     = c->dx8 ? D3DTEXF_NONE : 1;
        c->tss[s][VGPU98_TSS_MAXANISOTROPY] = 1;
        c->tss[s][VGPU98_TSS_ADDRESSW]      = 1;
        c->tss[s][VGPU98_TSS_COLORARG0]     = VGPU98_TA_CURRENT;
        c->tss[s][VGPU98_TSS_ALPHAARG0]     = VGPU98_TA_CURRENT;
        c->tss[s][VGPU98_TSS_RESULTARG]     = VGPU98_TA_CURRENT;
    }
    ctx_viewport_whole(c);
    c->wnear      = 1.0f;
    c->wfar       = 1.0f;
    c->dirty      = DIRTY_ALL;
    memset(c->rs_dirty, 0xFF, sizeof(c->rs_dirty));
    memset(c->tss_dirty, 0xFF, sizeof(c->tss_dirty));
    c->tex_changed = ~0u;
    c->dep_valid   = 0;
    c->sent_epoch = sb->state_epoch - 1; /* everything goes to the GPU */

    /* Matrices are the identity, the rest zero, the Z range 0-1. */
    for (s = 0; s < VGPU98_XFORM_COUNT; s++) {
        int k;

        for (k = 0; k < 16; k++)
            c->xform[s][k] = ((k % 5) == 0) ? 1.0f : 0.0f;
    }
    c->zrange[0] = 0.0f;
    c->zrange[1] = 1.0f;
    c->tl_dirty  = ~0u;
}

/* --- Transform and lighting (DirectX 7) ----------------------------------------- */

/* Direct3D's transform types by the GPU's TRANSFORM numbers; -1 for others.
   DirectX 8 numbers world matrix n 256 + n: 0-3 are the GPU's 0-3 (world
   matrices from 4 up are the palette's, ctx_set_transform). */
static int
xform_gpu(DWORD type)
{
    if ((type >= D3DTS_WORLDMATRIX(0)) && (type <= D3DTS_WORLDMATRIX(3)))
        return VGPU98_XFORM_WORLD + (int) (type - D3DTS_WORLDMATRIX(0));
    switch (type) {
        case D3DTRANSFORMSTATE_WORLD:
            return VGPU98_XFORM_WORLD;
        case D3DTRANSFORMSTATE_WORLD1:
        case D3DTRANSFORMSTATE_WORLD2:
        case D3DTRANSFORMSTATE_WORLD3:
            return VGPU98_XFORM_WORLD + 1 + (int) (type - D3DTRANSFORMSTATE_WORLD1);
        case D3DTRANSFORMSTATE_VIEW:
            return VGPU98_XFORM_VIEW;
        case D3DTRANSFORMSTATE_PROJECTION:
            return VGPU98_XFORM_PROJECTION;
        default:
            if ((type >= D3DTRANSFORMSTATE_TEXTURE0) && (type < D3DTRANSFORMSTATE_TEXTURE0 + D3D_STAGES))
                return VGPU98_XFORM_TEXTURE + (int) (type - D3DTRANSFORMSTATE_TEXTURE0);
            return -1;
    }
}

/* DirectX 8's world matrices 4-255 (indexed vertex blending): kept in the
   DLL's heap from the first one set. */
static int
pal_index(const d3d_ctx_t *c, DWORD type)
{
    if (!c->dx8 || (type < D3DTS_WORLDMATRIX(4)) || (type > D3DTS_WORLDMATRIX(D3D_PALETTE - 1)))
        return -1;
    return (int) (type - D3DTS_WORLDMATRIX(0));
}

void
ctx_set_transform(d3d_ctx_t *c, DWORD type, const void *matrix)
{
    const int g = xform_gpu(type);
    const int k = pal_index(c, type);

    if (k >= 0) {
        if (!c->pal) {
            c->pal = heap_alloc(D3D_PALETTE * sizeof(c->pal[0]));
            if (!c->pal) {
                dbg_event(DLL_TAG ": no room for world matrix %u", (DWORD) k);
                return;
            }
        }
        memcpy(c->pal[k], matrix, sizeof(c->pal[k]));
        c->pal_set[k / 32] |= 1u << (k % 32);
        c->pal_dirty[k / 32] |= 1u << (k % 32);
        if ((DWORD) k + 1 > c->pal_hi)
            c->pal_hi = (DWORD) k + 1;
        c->tl_dirty |= TL_PALETTE;
        c->clean = 0;
        return;
    }
    if (g < 0)
        return;
    memcpy(c->xform[g], matrix, sizeof(c->xform[g]));
    c->tl_dirty |= TL_XFORM(g);
    c->clean = 0;
    if (diag_textures && (g >= VGPU98_XFORM_TEXTURE))
        tt_matrix(c, (DWORD) (g - VGPU98_XFORM_TEXTURE), c->xform[g]);
}

/* A matrix as set (a state block's CAPTURE, MULTIPLYTRANSFORM): a world
   matrix never set is the identity. */
void
ctx_get_transform(const d3d_ctx_t *c, DWORD type, void *matrix)
{
    const int g = xform_gpu(type);
    const int k = pal_index(c, type);

    if (k >= 0) {
        if (c->pal && ((c->pal_set[k / 32] >> (k % 32)) & 1))
            memcpy(matrix, c->pal[k], sizeof(c->pal[k]));
        else {
            float *m = matrix;
            int    i;

            for (i = 0; i < 16; i++)
                m[i] = ((i % 5) == 0) ? 1.0f : 0.0f;
        }
        return;
    }
    if (g >= 0)
        memcpy(matrix, c->xform[g], sizeof(c->xform[g]));
}

/* What of the palette the GPU holds (under the ring lock, for every
   process): the context whose matrices it last took, and past the highest
   slot any context sent since the last reset (state epoch pal_epoch). */
static DWORD gpu_pal_owner;
static DWORD gpu_pal_hi;
static DWORD gpu_pal_epoch;

/* The palette's matrices the GPU lacks (TRANSFORM 256 + n): those that
   changed, or, when another context's are there, every one up to the
   highest either set (never-set ones as the identity, DirectX 8's). */
static int
pal_flush(d3d_ctx_t *c)
{
    const DWORD     token = ctx_token(c); /* not its slot, which a later context reuses */
    DWORD           hi     = c->pal_hi;
    int             all;
    volatile DWORD *p;
    DWORD           k;
    DWORD           i;

    if (gpu_pal_epoch != sb->state_epoch) {
        gpu_pal_epoch = sb->state_epoch;
        gpu_pal_owner = 0;
        gpu_pal_hi    = 4;
    }
    all = gpu_pal_owner != token;
    if (all && (gpu_pal_hi > hi))
        hi = gpu_pal_hi;
    for (k = 4; k < hi; k++) {
        const int set = c->pal && ((c->pal_set[k / 32] >> (k % 32)) & 1);

        if (!all && !((c->pal_dirty[k / 32] >> (k % 32)) & 1))
            continue;
        if (!(p = gpu_packet(VGPU98_OP_TRANSFORM, VGPU98_LEN_TRANSFORM)))
            return 0;
        pkt_put(&p, VGPU98_XFORM_PALETTE + k);
        for (i = 0; i < 16; i++)
            pkt_putf(&p, set ? c->pal[k][i] : (((i % 5) == 0) ? 1.0f : 0.0f));
    }
    memset(c->pal_dirty, 0, sizeof(c->pal_dirty));
    gpu_pal_owner = token;
    if (hi > gpu_pal_hi)
        gpu_pal_hi = hi;
    return 1;
}

d3d_light_t *
ctx_light(const d3d_ctx_t *c, DWORD index)
{
    DWORD lo = 0;
    DWORD hi = c->n_lights;

    while (lo < hi) {
        const DWORD mid = (lo + hi) / 2;

        if (c->lights[mid].index < index)
            lo = mid + 1;
        else
            hi = mid;
    }
    return ((lo < c->n_lights) && (c->lights[lo].index == index)) ? &c->lights[lo] : NULL;
}

/* CREATELIGHT: the light joins the table, in index order. 0 if there is no
   room in the DLL's heap. */
int
ctx_create_light(d3d_ctx_t *c, DWORD index)
{
    DWORD at;

    if (ctx_light(c, index))
        return 1;
    if (c->n_lights == c->cap_lights) {
        const DWORD  cap = c->cap_lights ? 2 * c->cap_lights : 8;
        d3d_light_t *t   = heap_grow(c->lights, c->n_lights * sizeof(d3d_light_t), cap * sizeof(d3d_light_t));

        if (!t)
            return 0;
        c->lights     = t;
        c->cap_lights = cap;
    }
    for (at = c->n_lights; at && (c->lights[at - 1].index > index); at--)
        c->lights[at] = c->lights[at - 1];
    memset(&c->lights[at], 0, sizeof(c->lights[at]));
    c->lights[at].index = index;
    c->n_lights++;
    return 1;
}

void
ctx_set_light(d3d_ctx_t *c, DWORD index, DWORD type, const void *data)
{
    d3d_light_t *l = ctx_light(c, index);

    if (!l)
        return;
    if (type == D3DHAL_SETLIGHT_ENABLE)
        l->enabled = 1;
    else if (type == D3DHAL_SETLIGHT_DISABLE)
        l->enabled = 0;
    else if (type == D3DHAL_SETLIGHT_DATA)
        memcpy(l->data, data, sizeof(l->data));
    else
        return;
    c->tl_dirty |= TL_LIGHTS;
    c->clean = 0;
}

void
ctx_set_material(d3d_ctx_t *c, const void *material)
{
    memcpy(c->material, material, sizeof(c->material));
    c->tl_dirty |= TL_MATERIAL;
    c->clean = 0;
}

void
ctx_set_clipplane(d3d_ctx_t *c, DWORD index, const void *plane)
{
    if (index >= VGPU98_MAX_CLIPPLANES)
        return;
    memcpy(c->plane[index], plane, sizeof(c->plane[index]));
    c->tl_dirty |= TL_PLANES;
    c->clean = 0;
}

void
ctx_set_zrange(d3d_ctx_t *c, float minz, float maxz)
{
    c->zrange[0] = minz;
    c->zrange[1] = maxz;
    c->tl_dirty |= TL_ZRANGE;
    c->clean = 0;
}

/* Sends what of the transform and lighting state the GPU lacks: matrices;
   the enabled lights in the GPU's 8 slots, in table order (PARALLELPOINT
   lights, which DirectX 7 still defines but nothing draws, and any past the
   eighth stay out, as the caps say); the material; the clip planes; the Z
   range. */
static int
tl_flush(d3d_ctx_t *c, int full)
{
    volatile DWORD *p;
    DWORD           slot[VGPU98_MAX_LIGHTS];
    DWORD           n = 0;
    DWORD           i;
    DWORD           k;

    for (i = 0; i < VGPU98_XFORM_COUNT; i++) {
        if (!VGPU98_XFORM_VALID(i))
            continue; /* reserved slots: the GPU refuses them */
        if (!full && (!(c->tl_dirty & TL_XFORM(i)) || !dwords_differ(c->xform[i], c->sent_xform[i], 16)))
            continue;
        if (!(p = gpu_packet(VGPU98_OP_TRANSFORM, VGPU98_LEN_TRANSFORM)))
            return 0;
        pkt_put(&p, i);
        for (k = 0; k < 16; k++)
            pkt_putf(&p, c->xform[i][k]);
        memcpy(c->sent_xform[i], c->xform[i], sizeof(c->xform[i]));
    }
    if (full || (c->tl_dirty & TL_LIGHTS)) {
        DWORD mask;

        for (i = 0; (i < c->n_lights) && (n < VGPU98_MAX_LIGHTS); i++) {
            const d3d_light_t *l = &c->lights[i];

            if (l->enabled && (l->data[0] >= VGPU98_LIGHT_POINT) && (l->data[0] <= VGPU98_LIGHT_DIRECTIONAL))
                slot[n++] = i;
        }
        for (k = 0; k < n; k++) {
            const DWORD *data = c->lights[slot[k]].data;

            if (!full && (c->sent_light_mask & (1u << k)) && !dwords_differ(data, c->sent_light[k], D3DLIGHT7_DWORDS))
                continue;
            if (!(p = gpu_packet(VGPU98_OP_LIGHT, VGPU98_LEN_LIGHT)))
                return 0;
            pkt_put(&p, k);
            for (i = 0; i < D3DLIGHT7_DWORDS; i++)
                pkt_put(&p, data[i]);
            memcpy(c->sent_light[k], data, sizeof(c->sent_light[k]));
        }
        mask = (1u << n) - 1;
        if (full || (mask != c->sent_light_mask)) {
            if (!(p = gpu_packet(VGPU98_OP_LIGHTENABLE, VGPU98_LEN_LIGHTENABLE)))
                return 0;
            pkt_put(&p, mask);
            c->sent_light_mask = mask;
        }
    }
    if (full || ((c->tl_dirty & TL_MATERIAL) && dwords_differ(c->material, c->sent_material, D3DMATERIAL7_DWORDS))) {
        if (!(p = gpu_packet(VGPU98_OP_MATERIAL, VGPU98_LEN_MATERIAL)))
            return 0;
        for (i = 0; i < D3DMATERIAL7_DWORDS; i++)
            pkt_put(&p, c->material[i]);
        memcpy(c->sent_material, c->material, sizeof(c->material));
    }
    if (full || (c->tl_dirty & TL_PLANES)) {
        for (i = 0; i < VGPU98_MAX_CLIPPLANES; i++) {
            if (!full && !dwords_differ(c->plane[i], c->sent_plane[i], 4))
                continue;
            if (!(p = gpu_packet(VGPU98_OP_CLIPPLANE, VGPU98_LEN_CLIPPLANE)))
                return 0;
            pkt_put(&p, i);
            for (k = 0; k < 4; k++)
                pkt_putf(&p, c->plane[i][k]);
            memcpy(c->sent_plane[i], c->plane[i], sizeof(c->plane[i]));
        }
    }
    if (full || ((c->tl_dirty & TL_ZRANGE) && dwords_differ(c->zrange, c->sent_zrange, 2))) {
        if (!(p = gpu_packet(VGPU98_OP_ZRANGE, VGPU98_LEN_ZRANGE)))
            return 0;
        pkt_putf(&p, c->zrange[0]);
        pkt_putf(&p, c->zrange[1]);
        memcpy(c->sent_zrange, c->zrange, sizeof(c->zrange));
    }
    /* DirectX 8's palette: a DirectX 8 context's own whenever the GPU has
       another's (or after a reset), and what changed. */
    if (c->dx8 && (full || (c->tl_dirty & TL_PALETTE) || (gpu_pal_owner != ctx_token(c)) ||
                   (gpu_pal_epoch != sb->state_epoch)) &&
        !pal_flush(c))
        return 0;
    c->tl_dirty = 0;
    return 1;
}

/* --- Legacy states (the reference rasterizer's maplegcy.cpp) ---------------- */

static void
map_legacy_filter(d3d_ctx_t *c)
{
    const int aniso = c->rs[D3DRS_ANISOTROPY] > 1;
    DWORD    *t     = c->tss[0];

    t[VGPU98_TSS_MAGFILTER] = (c->rs[D3DRS_TEXTUREMAG] == D3DFILTER_LINEAR) ? (aniso ? 5 : 2) : 1;
    switch (c->rs[D3DRS_TEXTUREMIN]) {
        case D3DFILTER_NEAREST:
            t[VGPU98_TSS_MINFILTER] = 1, t[VGPU98_TSS_MIPFILTER] = 1;
            break;
        case D3DFILTER_MIPNEAREST:
            t[VGPU98_TSS_MINFILTER] = 1, t[VGPU98_TSS_MIPFILTER] = 2;
            break;
        case D3DFILTER_LINEARMIPNEAREST:
            t[VGPU98_TSS_MINFILTER] = 1, t[VGPU98_TSS_MIPFILTER] = 3;
            break;
        case D3DFILTER_LINEAR:
            t[VGPU98_TSS_MINFILTER] = aniso ? 3 : 2, t[VGPU98_TSS_MIPFILTER] = 1;
            break;
        case D3DFILTER_MIPLINEAR:
            t[VGPU98_TSS_MINFILTER] = aniso ? 3 : 2, t[VGPU98_TSS_MIPFILTER] = 2;
            break;
        case D3DFILTER_LINEARMIPLINEAR:
            t[VGPU98_TSS_MINFILTER] = aniso ? 3 : 2, t[VGPU98_TSS_MIPFILTER] = 3;
            break;
        default:
            break;
    }
}

static void
map_legacy_blend(d3d_ctx_t *c)
{
    DWORD *t = c->tss[0];

    c->tss[1][VGPU98_TSS_COLOROP] = VGPU98_TOP_DISABLE;
    switch (c->rs[D3DRS_TEXTUREMAPBLEND]) {
        case D3DTBLEND_MODULATEMASK:
        case D3DTBLEND_MODULATE:
            t[VGPU98_TSS_COLOROP]   = VGPU98_TOP_MODULATE;
            t[VGPU98_TSS_COLORARG1] = VGPU98_TA_TEXTURE;
            t[VGPU98_TSS_COLORARG2] = VGPU98_TA_DIFFUSE;
            t[VGPU98_TSS_ALPHAOP]   = TOP_LEGACY_ALPHAOVR;
            t[VGPU98_TSS_ALPHAARG1] = VGPU98_TA_TEXTURE;
            t[VGPU98_TSS_ALPHAARG2] = VGPU98_TA_DIFFUSE;
            break;
        case D3DTBLEND_MODULATEALPHA:
            t[VGPU98_TSS_COLOROP]   = VGPU98_TOP_MODULATE;
            t[VGPU98_TSS_COLORARG1] = VGPU98_TA_TEXTURE;
            t[VGPU98_TSS_COLORARG2] = VGPU98_TA_DIFFUSE;
            t[VGPU98_TSS_ALPHAOP]   = VGPU98_TOP_MODULATE;
            t[VGPU98_TSS_ALPHAARG1] = VGPU98_TA_TEXTURE;
            t[VGPU98_TSS_ALPHAARG2] = VGPU98_TA_DIFFUSE;
            break;
        case D3DTBLEND_DECALALPHA:
            t[VGPU98_TSS_COLOROP]   = VGPU98_TOP_BLENDTEXTUREALPHA;
            t[VGPU98_TSS_COLORARG1] = VGPU98_TA_TEXTURE;
            t[VGPU98_TSS_COLORARG2] = VGPU98_TA_DIFFUSE;
            t[VGPU98_TSS_ALPHAOP]   = VGPU98_TOP_SELECTARG2;
            t[VGPU98_TSS_ALPHAARG1] = VGPU98_TA_TEXTURE;
            t[VGPU98_TSS_ALPHAARG2] = VGPU98_TA_DIFFUSE;
            break;
        case D3DTBLEND_ADD:
            t[VGPU98_TSS_COLOROP]   = VGPU98_TOP_ADD;
            t[VGPU98_TSS_COLORARG1] = VGPU98_TA_TEXTURE;
            t[VGPU98_TSS_COLORARG2] = VGPU98_TA_DIFFUSE;
            t[VGPU98_TSS_ALPHAOP]   = VGPU98_TOP_SELECTARG2;
            t[VGPU98_TSS_ALPHAARG1] = VGPU98_TA_TEXTURE;
            t[VGPU98_TSS_ALPHAARG2] = VGPU98_TA_DIFFUSE;
            break;
        default: /* DECAL, COPY, DECALMASK */
            t[VGPU98_TSS_COLOROP]   = VGPU98_TOP_SELECTARG1;
            t[VGPU98_TSS_COLORARG1] = VGPU98_TA_TEXTURE;
            t[VGPU98_TSS_ALPHAOP]   = VGPU98_TOP_SELECTARG1;
            t[VGPU98_TSS_ALPHAARG1] = VGPU98_TA_TEXTURE;
            break;
    }
}

void
ctx_set_rs(d3d_ctx_t *c, DWORD state, DWORD value)
{
    if (state >= D3DRS_MAX)
        return;
    /* DirectX 8's states belong to DirectX 8 contexts. */
    if ((state >= VGPU98_RS_GLOBAL_COUNT) && (state < VGPU98_RS_GLOBAL_COUNT8) && !c->dx8)
        return;
    /* The point-sprite trace: a DirectX 8 context's point states as they
       change. */
    if (c->dx8 && diag_audit8 && (c->rs[state] != value) &&
        (((state >= VGPU98_RS_POINTSIZE) && (state <= VGPU98_RS_POINTSCALE_C)) || (state == VGPU98_RS_POINTSIZE_MAX)) &&
        TRACE(TRACE_POINTS))
        dbg(DLL_TAG " trace: context %u point state %u: %x to %x", ctx_handle(c), state, c->rs[state], value);
    c->rs[state] = value;
    c->dirty |= DIRTY_RS;
    c->rs_dirty[state >> 5] |= 1u << (state & 31);
    c->clean = 0;
    switch (state) {
        case D3DRS_TEXTUREHANDLE:
            c->tss[0][D3DTSS_TEXTUREMAP] = value;
            c->dirty |= DIRTY_STAGE(0);
            c->tss_dirty[0] |= 1u << D3DTSS_TEXTUREMAP;
            c->tex_changed |= 1u;
            break;
        case D3DRS_TEXTUREMAPBLEND:
            map_legacy_blend(c);
            ctx_stage_all(c, 0);
            ctx_stage_all(c, 1);
            break;
        case D3DRS_TEXTUREADDRESS:
            c->tss[0][D3DTSS_ADDRESS] = c->tss[0][VGPU98_TSS_ADDRESSU] = c->tss[0][VGPU98_TSS_ADDRESSV] = value;
            ctx_stage_all(c, 0);
            break;
        case D3DRS_TEXTUREADDRESSU:
            c->tss[0][VGPU98_TSS_ADDRESSU] = value;
            ctx_stage_all(c, 0);
            break;
        case D3DRS_TEXTUREADDRESSV:
            c->tss[0][VGPU98_TSS_ADDRESSV] = value;
            ctx_stage_all(c, 0);
            break;
        case D3DRS_MIPMAPLODBIAS:
            c->tss[0][VGPU98_TSS_MIPMAPLODBIAS] = value;
            ctx_stage_all(c, 0);
            break;
        case D3DRS_BORDERCOLOR:
            c->tss[0][VGPU98_TSS_BORDERCOLOR] = value;
            ctx_stage_all(c, 0);
            break;
        case D3DRS_ANISOTROPY:
            c->tss[0][VGPU98_TSS_MAXANISOTROPY] = value;
            map_legacy_filter(c);
            ctx_stage_all(c, 0);
            break;
        case D3DRS_TEXTUREMAG:
        case D3DRS_TEXTUREMIN:
            map_legacy_filter(c);
            ctx_stage_all(c, 0);
            break;
        case D3DRS_WRAPU:
            c->rs[D3DRS_WRAP0] = (c->rs[D3DRS_WRAP0] & ~1u) | (value ? 1u : 0u);
            c->rs_dirty[D3DRS_WRAP0 >> 5] |= 1u << (D3DRS_WRAP0 & 31);
            break;
        case D3DRS_WRAPV:
            c->rs[D3DRS_WRAP0] = (c->rs[D3DRS_WRAP0] & ~2u) | (value ? 2u : 0u);
            c->rs_dirty[D3DRS_WRAP0 >> 5] |= 1u << (D3DRS_WRAP0 & 31);
            break;
        default:
            break;
    }
}

void
ctx_set_tss(d3d_ctx_t *c, DWORD stage, DWORD n, DWORD value)
{
    /* Stage states 25-28 are DirectX 8's. */
    if ((stage >= D3D_STAGES) || (n >= (c->dx8 ? D3D_TSS_COUNT : VGPU98_TSS_COUNT)))
        return;
    c->tss[stage][n] = value;
    c->dirty |= DIRTY_STAGE(stage);
    c->tss_dirty[stage] |= 1u << n;
    if (n == D3DTSS_TEXTUREMAP)
        c->tex_changed |= 1u << stage;
    c->clean = 0;
    if (n == D3DTSS_ADDRESS) {
        c->tss[stage][VGPU98_TSS_ADDRESSU] = value;
        c->tss[stage][VGPU98_TSS_ADDRESSV] = value;
        c->tss_dirty[stage] |= (1u << VGPU98_TSS_ADDRESSU) | (1u << VGPU98_TSS_ADDRESSV);
    }
}

/* --- Textures -------------------------------------------------------------- */

/* A stage's texture: by its TextureCreate handle (DirectX 6), or by its
   surface handle (DirectX 7). */
static LPSURF
tex_surface(const d3d_ctx_t *c, DWORD handle)
{
    if (ctx_surface_handles(c))
        return d3d7_surface(c->ddl, handle);
    if ((handle < 1) || (handle >= MAX_TEXTURES) || !d3d_tex[handle].surf)
        return NULL;
    return d3d_tex[handle].surf->lpLcl;
}

static DWORD
caps2(LPSURF s)
{
    return s->lpSurfMore ? s->lpSurfMore->ddsCapsEx.dwCaps2 : 0;
}

/* The next level of a mip chain, and (if `implicit` is not NULL) whether it
   was attached when the chain was made (DDAL_IMPLICIT) rather than by
   AddAttachedSurface, which DeleteAttachedSurface can undo without telling
   the driver. A cube map's faces are attached to one another as well, so
   within a face only the attachment marked as a sublevel counts (DirectX
   7); other chains attach nothing else. */
static LPSURF
tex_mip_link(LPSURF s, int *implicit)
{
    const int   cube = (caps2(s) & DDSCAPS2_CUBEMAP) != 0;
    ATTACHLIST *al;

    for (al = s->lpAttachList; al; al = al->lpLink) {
        LPSURF a = al->lpAttached;

        if (!a || !(a->ddsCaps.dwCaps & DDSCAPS_MIPMAP) || !(a->ddsCaps.dwCaps & DDSCAPS_TEXTURE))
            continue;
        if (cube && !(caps2(a) & DDSCAPS2_MIPMAPSUBLEVEL))
            continue;
        if (implicit)
            *implicit = (al->dwFlags & DDAL_IMPLICIT) != 0;
        return a;
    }
    return NULL;
}

LPSURF
tex_mip_next(LPSURF s)
{
    return tex_mip_link(s, NULL);
}

/* A cube map's six faces in Direct3D's order (+X, -X, +Y, -Y, +Z, -Z), from
   its root (the first face it has): the root and the faces attached to it.
   0 unless all six are there. */
static int
cube_faces(LPSURF root, LPSURF *faces)
{
    ATTACHLIST *al;
    int         f;
    int         n = 0;

    memset(faces, 0, VGPU98_CUBE_FACES * sizeof(LPSURF));
    for (f = 0; f < VGPU98_CUBE_FACES; f++) {
        if (caps2(root) & (DDSCAPS2_CUBEMAP_POSITIVEX << f))
            faces[f] = root;
    }
    for (al = root->lpAttachList; al; al = al->lpLink) {
        LPSURF a = al->lpAttached;

        if (!a || (caps2(a) & DDSCAPS2_MIPMAPSUBLEVEL))
            continue;
        for (f = 0; f < VGPU98_CUBE_FACES; f++) {
            if ((caps2(a) & (DDSCAPS2_CUBEMAP_POSITIVEX << f)) && !faces[f])
                faces[f] = a;
        }
    }
    for (f = 0; f < VGPU98_CUBE_FACES; f++)
        n += faces[f] != NULL;
    return n == VGPU98_CUBE_FACES;
}

static int
fmt_has_alpha(DWORD f)
{
    return (f == VGPU98_FMT_A1R5G5B5) || (f == VGPU98_FMT_A4R4G4B4) || (f == VGPU98_FMT_A8R8G8B8) ||
           (f == VGPU98_FMT_A8L8) || ((f >= VGPU98_FMT_DXT1) && (f <= VGPU98_FMT_DXT5)) || (f == VGPU98_FMT_A8) ||
           (f == VGPU98_FMT_A4L4) || (f == VGPU98_FMT_A2W10V10U10);
}

static DDRAWI_DDRAWPALETTE_GBL *
tex_palette(LPSURF s)
{
    if (s->lpDDPalette && s->lpDDPalette->lpLcl && s->lpDDPalette->lpLcl->lpGbl)
        return s->lpDDPalette->lpLcl->lpGbl;
    return NULL;
}

/* The surfaces tex_build looked at, top first, and whether its walk ended
   at the end of the chain: what a description depends on below the top. */
typedef struct tex_chain {
    LPSURF lvl[VGPU98_TEX_MAX_LEVELS];
    DWORD  n;
    int    open;  /* a level attached at the end would change it */
    int    loose; /* a level of it was attached by AddAttachedSurface: checked at every call */
} tex_chain_t;

/* The palette alpha flag of a stage's texture: its own palette's
   (DirectX 6, DirectDraw's palette object) or the one SETPALETTE bound
   (DirectX 7). */
static int
tex_pal_alpha(const d3d_ctx_t *c, DWORD handle, LPSURF s)
{
    DDRAWI_DDRAWPALETTE_GBL *pal;
    int                      alpha;

    if (ctx_surface_handles(c))
        return d3d7_palette_of(c->ddl, handle, &alpha) && alpha;
    pal = tex_palette(s);
    return pal && (pal->dwFlags & DDRAWIPAL_ALPHA);
}

/* One face's levels into the payload from `at`: each half the size of the
   one before, down to 1x1 or the last the chain has. Returns the levels. */
static DWORD
tex_levels(LPSURF s, const gsurf_t *g, DWORD max, DWORD *at, tex_chain_t *ch)
{
    DWORD levels = 0;

    while (s && (levels < max)) {
        gsurf_t l;
        DWORD   w = g->width >> levels;
        DWORD   h = g->height >> levels;

        if (ch && (ch->n < VGPU98_TEX_MAX_LEVELS))
            ch->lvl[ch->n++] = s;
        if (!surf_describe(s, &l) || (l.format != g->format) || (l.width != (w ? w : 1)) || (l.height != (h ? h : 1)))
            break;
        at[2 * levels]     = l.offset;
        at[2 * levels + 1] = l.pitch;
        levels++;
        if ((w <= 1) && (h <= 1))
            break;
        if (ch) {
            int implicit = 1;

            s = tex_mip_link(s, &implicit);
            /* The DLL's copies: no AddAttachedSurface changes them. */
            ch->loose |= s && !implicit && !heap_owns(s);
        } else
            s = tex_mip_next(s);
    }
    if (ch)
        ch->open = !s;
    return levels;
}

/* A volume's levels (DirectX 8) into the payload from `at`: each half the
   one before each way, down to 1 x 1 x 1 or the last the chain has, with
   its offset, pitch and slice pitch, all of it within video memory. */
static DWORD
tex_volume_levels(LPSURF s, const gsurf_t *g, DWORD depth, DWORD *at, tex_chain_t *ch)
{
    DWORD levels = 0;

    while (s && (levels < VGPU98_TEX_VOLUME_LEVELS)) {
        gsurf_t     l;
        const DWORD w = (g->width >> levels) ? g->width >> levels : 1;
        const DWORD h = (g->height >> levels) ? g->height >> levels : 1;
        const DWORD d = (depth >> levels) ? depth >> levels : 1;
        DWORD       slice;

        if (ch && (ch->n < VGPU98_TEX_MAX_LEVELS))
            ch->lvl[ch->n++] = s;
        if (!(s->dwReserved1 & SURF_SLICES) || !surf_describe(s, &l) || (l.format != g->format) || (l.width != w) ||
            (l.height != h))
            break;
        slice = s->lpGbl->dwBlockSizeY;
        if ((slice < l.pitch * h) ||
            ((uint64_t) l.offset + (uint64_t) slice * (d - 1) + (uint64_t) l.pitch * (h - 1) + w * fmt_bytes(l.format) >
             sb->vram_size))
            break;
        at[3 * levels]     = l.offset;
        at[3 * levels + 1] = l.pitch;
        at[3 * levels + 2] = slice;
        levels++;
        if ((w <= 1) && (h <= 1) && (d <= 1))
            break;
        if (ch) {
            int implicit = 1;

            s = tex_mip_link(s, &implicit);
            ch->loose |= s && !implicit && !heap_owns(s);
        } else
            s = tex_mip_next(s);
    }
    if (ch)
        ch->open = !s;
    return levels;
}

/* The TEXTURE packet's payload for a stage's texture: a 2D texture's levels,
   a cube map's six faces of levels (square, every face with as many levels
   as the first), or (DirectX 8) a volume's levels. Returns its length; 4
   with format 0 when the stage has no usable texture. Records the chain it
   walked in `ch` (if not NULL; for a cube map none: its faces cannot be
   attached or detached alone). `s` is the stage's texture as the caller
   found it (NULL none): not looked up again, so the description and the
   batch's list name the same surface. */
static DWORD
tex_build(d3d_ctx_t *c, DWORD stage, LPSURF s, DWORD *out, int *has_alpha, LPSURF *top, tex_chain_t *ch)
{
    const DWORD handle  = c->tss[stage][D3DTSS_TEXTUREMAP];
    DWORD       control = stage;
    DWORD       levels;
    gsurf_t     g;

    *has_alpha = 0;
    *top       = NULL;
    if (ch) {
        ch->n     = 0;
        ch->open  = 0;
        ch->loose = 0;
    }
    memset(out, 0, 4 * sizeof(DWORD));
    out[0] = stage;
    if (!s || !surf_describe(s, &g) || (g.format >= VGPU98_FMT_D16) || (g.width > VGPU98_TEX_MAX_SIZE) ||
        (g.height > VGPU98_TEX_MAX_SIZE))
        return 4;
    if ((s->dwFlags & DDRAWISURF_HASCKEYSRCBLT) && !((g.format >= VGPU98_FMT_DXT1) && (g.format <= VGPU98_FMT_DXT5))) {
        control |= VGPU98_TEX_COLORKEY;
        out[3] = s->ddckCKSrcBlt.dwColorSpaceLowValue;
    }
    if (d3d8_on && (caps2(s) & DDSCAPS2_VOLUME)) {
        /* A volume (command set 2.1): no colour key, no P8 or DXT. */
        const DWORD depth = (s->lpSurfMore->ddsCapsEx.dwCaps4 & 0xffff) ? (s->lpSurfMore->ddsCapsEx.dwCaps4 & 0xffff) : 1;

        out[3] = 0;
        if ((g.format == VGPU98_FMT_P8) || fmt_is_dxt(g.format) || (g.width > VGPU98_TEX_VOLUME_MAX) ||
            (g.height > VGPU98_TEX_VOLUME_MAX) || (depth > VGPU98_TEX_VOLUME_MAX))
            return 4;
        levels = tex_volume_levels(s, &g, depth, out + 4, ch);
        if (!levels)
            return 4;
        *has_alpha = fmt_has_alpha(g.format);
        *top       = s;
        out[0]     = stage | (VGPU98_TEX_TYPE_VOLUME << VGPU98_TEX_TYPE_SHIFT);
        out[1]     = VGPU98_SURF_SIZE(g.width, g.height);
        out[2]     = g.format | (levels << 8) | (depth << VGPU98_TEX_DEPTH_SHIFT);
        return 4 + 3 * levels;
    }
    *has_alpha = fmt_has_alpha(g.format);
    if ((g.format == VGPU98_FMT_P8) && tex_pal_alpha(c, handle, s)) {
        control |= VGPU98_TEX_PALETTE_ALPHA;
        *has_alpha = 1;
    }
    if (caps2(s) & DDSCAPS2_CUBEMAP) {
        LPSURF faces[VGPU98_CUBE_FACES];
        DWORD  f;

        if ((g.width != g.height) || !cube_faces(s, faces))
            return 4;
        if (ch)
            ch->n = 0;
        levels = tex_levels(faces[0], &g, VGPU98_TEX_MAX_LEVELS, out + 4, NULL);
        for (f = 1; f < VGPU98_CUBE_FACES; f++) {
            if (tex_levels(faces[f], &g, levels, out + 4 + 2 * levels * f, NULL) != levels)
                return 4;
        }
        control |= VGPU98_TEX_TYPE_CUBE << VGPU98_TEX_TYPE_SHIFT;
        *top     = s;
        out[0]   = control;
        out[1]   = VGPU98_SURF_SIZE(g.width, g.height);
        out[2]   = g.format | (levels << 8);
        return 4 + 2 * levels * VGPU98_CUBE_FACES;
    }
    levels = tex_levels(s, &g, VGPU98_TEX_MAX_LEVELS, out + 4, ch);
    *top   = s;
    out[0] = control;
    out[1] = VGPU98_SURF_SIZE(g.width, g.height);
    out[2] = g.format | (levels << 8);
    return 4 + 2 * levels;
}

/* The palette as the GPU takes it: D3DCOLOR dwords, with a signature of
   them. PALETTEENTRY holds red, green, blue, flags (alpha) in that byte
   order. DirectX 6: the texture's DirectDraw palette; DirectX 7: the one
   SETPALETTE bound, as UPDATEPALETTE left it. */
static DWORD
palette_get(const d3d_ctx_t *c, DWORD handle, LPSURF s, DWORD *colors)
{
    DWORD sig = 0x811c9dc5u;
    int   alpha;
    int   i;

    if (ctx_surface_handles(c)) {
        if (!d3d7_palette(c->ddl, handle, colors, &alpha))
            memset(colors, 0, 1024);
    } else {
        DDRAWI_DDRAWPALETTE_GBL *pal = tex_palette(s);

        for (i = 0; i < 256; i++) {
            const PALETTEENTRY *e = (pal && pal->lpColorTable) ? &pal->lpColorTable[i] : NULL;

            colors[i] = e ? (((DWORD) e->peFlags << 24) | ((DWORD) e->peRed << 16) | ((DWORD) e->peGreen << 8) | e->peBlue)
                          : 0;
        }
    }
    for (i = 0; i < 256; i++)
        sig = (sig ^ colors[i]) * 16777619u;
    return sig | 1; /* never 0, the "nothing sent" value */
}

/* A DirectX 7 or 8 stage's palette as its version, which names its colours
   and flags (PAL_UNBOUND: none bound): what the flush compares with what
   it sent, instead of the colours. */
static DWORD
stage_pal_key(const d3d_ctx_t *c, DWORD s)
{
    DWORD handle;
    DWORD version;

    d3d7_palette_state(c->ddl, c->tss[s][D3DTSS_TEXTUREMAP], &handle, &version);
    return version ? version : PAL_UNBOUND;
}

/* Records a fence as the last use of every level of a texture (and every
   face of a cube map). */
void
tex_used(LPSURF t, DWORD fence)
{
    LPSURF faces[VGPU98_CUBE_FACES];
    DWORD  f;
    DWORD  n;

    if (t && (caps2(t) & DDSCAPS2_CUBEMAP) && cube_faces(t, faces)) {
        for (f = 0; f < VGPU98_CUBE_FACES; f++) {
            for (n = 0, t = faces[f]; t && (n < VGPU98_TEX_MAX_LEVELS); n++, t = tex_mip_next(t))
                surf_used(t, fence);
        }
        return;
    }
    for (n = 0; t && (n < VGPU98_TEX_MAX_LEVELS); n++, t = tex_mip_next(t))
        surf_used(t, fence);
}

/* --- Texture descriptions, cached --------------------------------------------- */

/* Moved by every surface created or destroyed and every texture handle
   created, destroyed or swapped (dll.h). */
DWORD surf_epoch;
/* Moved by everything a context's state is built from that changes without
   a state command (dll.h). */
DWORD world_epoch;

/* What a texture's description was built from, besides its mip chain
   (tex_chain_t). Every field is a dword, so keys compare whole. */
typedef struct tex_key {
    DWORD                    handle;
    LPSURF                   top;
    DWORD                    vidmem;      /* the top level's memory (a flip in the texture's chain moves it) */
    DWORD                    ckey_flags;  /* DDRAWISURF_HASCKEYSRCBLT: DirectDraw sets colour keys itself */
    DWORD                    ckey;
    DWORD                    palette;     /* its palette: DirectDraw's object (DirectX 6); 0 for DirectX 7 and 8, whose palettes go by
                                             version */
    DWORD                    pal_flags;   /* the palette's DDRAWIPAL_ALPHA */
    void                    *scope;       /* DirectX 7: the handle's DirectDraw local; DirectX 6: NULL */
    DWORD                    epoch;       /* DirectX 6: surf_epoch; DirectX 7: the handle's generation */
    DWORD                    state_epoch; /* every GPU reset and mode change */
} tex_key_t;

_Static_assert(sizeof(tex_key_t) == 10 * sizeof(DWORD), "a texture key is whole dwords");

/* Descriptions kept: sets of TEX_WAYS, chosen by the texture's handle and
   scope, the least recently used replaced (under the ring lock). A DirectX 8
   game's scene holds thousands of textures (GTA3's evicted 1,800 of 512 every
   5 seconds): 4,096, each a 2D or volume texture's description at most (a
   volume's three dwords a level, nine levels at most); a cube map's, six
   times a 2D texture's and rare, is built at each use and not kept, which
   holds the cache under 1 MB. */
#define TEX_CACHE       TEX_CACHE_SIZE
#define TEX_WAYS        8
#define TEX_SETS        (TEX_CACHE / TEX_WAYS)
#define TEX_CACHED_DESC (4 + 3 * VGPU98_TEX_VOLUME_LEVELS)

_Static_assert(TEX_CACHED_DESC >= 4 + 2 * VGPU98_TEX_MAX_LEVELS, "an entry holds a 2D texture's description");
_Static_assert((TEX_SETS & (TEX_SETS - 1)) == 0, "the sets are a power of two (tex_set's mask)");

typedef struct tex_cached {
    tex_key_t   key;
    tex_chain_t chain;
    DWORD       len; /* 0: empty */
    int       usable;
    int       has_alpha;
    DWORD     batch; /* the batch that last bound it */
    DWORD     used;  /* tex_clock at its last use */
    DWORD     desc[TEX_CACHED_DESC]; /* without the stage number */
} tex_cached_t;

static tex_cached_t tex_cache[TEX_CACHE];
static DWORD        tex_clock;

/* The textures this batch's flushes bound: a batch may change textures
   between its draws, and the batch's fence is the last use of each. Kept
   under the ring lock, from ctx_batch_begin to ctx_used. Past BATCH_TEX,
   every texture gets the fence. */
#define BATCH_TEX BATCH_TEX_SIZE
static LPSURF batch_tex[BATCH_TEX];
static LPSURF batch_lvl[BATCH_TEX][VGPU98_TEX_MAX_LEVELS]; /* the levels its cached description names ... */
static BYTE   batch_lvl_n[BATCH_TEX];                      /* ... how many; 0: its chain is walked (tex_used) */
static DWORD  batch_tex_n;
static int    batch_tex_all;
static DWORD  batch_serial;

void
ctx_batch_begin(void)
{
    batch_tex_n   = 0;
    batch_tex_all = 0;
    batch_serial++;
}

/* A texture into the batch's list, with the levels its cached description
   names (what the GPU reads), or without (`ch` NULL or empty: a cube map, a
   surface TEXBLT writes) its chain is walked when the batch ends. */
static void
batch_list(LPSURF s, const tex_chain_t *ch)
{
    const DWORD i = batch_tex_n;

    if (i >= BATCH_TEX) {
        batch_tex_all = 1;
        return;
    }
    batch_tex[i]   = s;
    batch_lvl_n[i] = 0;
    if (ch && ch->n) {
        memcpy(batch_lvl[i], ch->lvl, ch->n * sizeof(LPSURF));
        batch_lvl_n[i] = (BYTE) ch->n;
    }
    batch_tex_n++;
}

/* `palette` is a DirectX 7 handle's (d3d7_surface_gen's), unused for DirectX
   6. *pal_key: for DirectX 7 and 8, the palette's version (PAL_UNBOUND
   none) read with its alpha flag, so the flush sends the palette the
   description was built with; 0 for DirectX 6. */
static void
tex_key_of(tex_key_t *k, const d3d_ctx_t *c, DWORD handle, LPSURF s, DWORD epoch, DWORD palette, DWORD *pal_key)
{
    k->handle     = handle;
    k->top        = s;
    k->vidmem     = s->lpGbl->fpVidMem;
    k->ckey_flags = s->dwFlags & DDRAWISURF_HASCKEYSRCBLT;
    k->ckey       = s->ddckCKSrcBlt.dwColorSpaceLowValue;
    *pal_key      = 0;
    if (ctx_surface_handles(c)) {
        int   alpha;
        DWORD version;

        /* The description holds the palette's alpha flag alone: the palette
           goes by its version, so a
           texture bound to another palette keeps its description. */
        d3d7_palette_flags(c->ddl, palette, &alpha, &version);
        k->palette   = 0;
        k->pal_flags = alpha ? DDRAWIPAL_ALPHA : 0;
        *pal_key     = version ? version : PAL_UNBOUND;
    } else {
        DDRAWI_DDRAWPALETTE_GBL *pal = tex_palette(s);

        k->palette   = (DWORD) s->lpDDPalette;
        k->pal_flags = pal ? (pal->dwFlags & DDRAWIPAL_ALPHA) : 0;
    }
    k->scope       = ctx_surface_handles(c) ? c->ddl : NULL;
    k->epoch       = epoch;
    k->state_epoch = sb->state_epoch;
}

/* Is the mip chain below the top still the one walked? A level attached or
   detached anywhere in it changes what tex_mip_next finds. */
static int
tex_chain_same(const tex_chain_t *ch)
{
    LPSURF s;
    DWORD  i;

    if (!ch->n)
        return 1;
    s = ch->lvl[0];
    for (i = 1; i < ch->n; i++) {
        s = tex_mip_next(s);
        if (s != ch->lvl[i])
            return 0;
    }
    return !ch->open || !tex_mip_next(s);
}

/* The set of a texture's handle in its scope. */
static tex_cached_t *
tex_set(DWORD handle, const void *scope)
{
    DWORD x = (((DWORD) scope >> 4) * 0x9e3779b1u) ^ (handle * 0x85ebca6bu);

    x ^= x >> 15;
    return &tex_cache[(x & (TEX_SETS - 1)) * TEX_WAYS];
}

/* tex_build through the cache: the description built last for the handle
   while nothing it was built from changed, with this stage's number. The
   texture is recorded as bound in this batch. `loose`: its chain has a level
   AddAttachedSurface attached. *pal_key: tex_key_of's (0 when no key was
   made: no surface, a cube map). */
static DWORD
tex_desc(d3d_ctx_t *c, DWORD stage, DWORD *out, int *has_alpha, LPSURF *top, int *loose, DWORD *pal_key)
{
    const DWORD   handle  = c->tss[stage][D3DTSS_TEXTUREMAP];
    DWORD         epoch   = surf_epoch;
    DWORD         palette = 0;
    LPSURF        s;
    tex_cached_t *set;
    tex_cached_t *e = NULL;
    tex_cached_t *victim = NULL;
    tex_key_t     key;
    DWORD         w;

    GUARD_STEP(GUARD_STEP_TEXTURE, stage, handle);
    s = ctx_surface_handles(c) ? d3d7_surface_gen(c->ddl, handle, &epoch, &palette) : tex_surface(c, handle);
    if (!s && handle && c->dx8) {
        DIAG(diag.tex_missing++);
        if (TRACE(TRACE_LOOKUP))
            dbg(DLL_TAG " trace: context %u: stage %u's texture %x not found under %x", ctx_handle(c), stage, handle,
                (DWORD) c->ddl);
    }

    *loose   = 0;
    *pal_key = 0;
    if (!s)
        return tex_build(c, stage, NULL, out, has_alpha, top, NULL);
    /* A cube map's description is built at each use, not kept: it would not
       fit an entry. An entry its handle had (another texture
       before it) goes, so no lookup finds that texture's levels under the
       handle; the cube is listed for the batch once, its faces with it. */
    if (caps2(s) & DDSCAPS2_CUBEMAP) {
        tex_chain_t ch;
        const void *scope = ctx_surface_handles(c) ? c->ddl : NULL;
        DWORD       len;

        /* The palette's version before the build reads its alpha flag: a
           change between them shows as another version at the next flush. */
        if (ctx_surface_handles(c)) {
            int   alpha;
            DWORD version;

            d3d7_palette_flags(c->ddl, palette, &alpha, &version);
            *pal_key = version ? version : PAL_UNBOUND;
        }
        len = tex_build(c, stage, s, out, has_alpha, top, &ch);

        set = tex_set(handle, scope);
        for (w = 0; w < TEX_WAYS; w++) {
            if (set[w].len && (set[w].key.handle == handle) && (set[w].key.scope == scope))
                set[w].len = 0;
        }
        DIAG(diag.tex_cubes++);
        *loose = ch.loose;
        if (*top)
            ctx_batch_surface(s);
        return len;
    }
    tex_key_of(&key, c, handle, s, epoch, palette, pal_key);
    /* The handle's entry in its set, or the set's empty or least recently
       used one. */
    set = tex_set(handle, key.scope);
    for (w = 0; w < TEX_WAYS; w++) {
        tex_cached_t *t = &set[w];

        if (t->len && (t->key.handle == handle) && (t->key.scope == key.scope)) {
            e = t;
            break;
        }
        if (!victim || (victim->len && (!t->len || ((LONG) (t->used - victim->used) < 0))))
            victim = t;
    }
    if (!e) {
        e = victim;
        DIAG({
            if (e->len)
                diag.tex_evicted++;
            else
                diag.tex_new++;
        });
        e->len = 0;
    } else if (dwords_differ(&e->key, &key, sizeof(key) / sizeof(DWORD)) || !tex_chain_same(&e->chain)) {
        DIAG(diag.tex_changed++);
        e->len = 0;
    }
    e->used = ++tex_clock;
    if (!e->len) {
        /* Built where the caller wants it (TEX_DESC_MAX), then kept when it
           fits an entry: a description longer than a 2D or volume
           texture's is not. */
        const DWORD len = tex_build(c, stage, s, out, &e->has_alpha, top, &e->chain);

        DIAG(diag.tex_rebuilt++);
        if (len > TEX_CACHED_DESC) {
            *has_alpha = e->has_alpha;
            *loose     = e->chain.loose;
            if (*top)
                ctx_batch_surface(s); /* once a batch, as the cube path */
            return len;
        }
        memcpy(e->desc, out, len * sizeof(DWORD));
        e->desc[0] &= ~VGPU98_TEX_STAGE_MASK;
        e->len    = len;
        e->usable = *top != NULL;
        e->key    = key;
        e->batch  = batch_serial - 1;
    } else {
        memcpy(out, e->desc, e->len * sizeof(DWORD));
        out[0] |= stage;
    }
    *has_alpha = e->has_alpha;
    *loose     = e->chain.loose;
    *top       = e->usable ? s : NULL;
    if (*top && (e->batch != batch_serial)) {
        e->batch = batch_serial;
        batch_list(s, &e->chain);
    }
    return e->len;
}

/* The descriptions the cache holds (the report's, read without the lock:
   a count). */
DWORD
tex_cache_held(void)
{
    DWORD n = 0;
    DWORD i;

    for (i = 0; i < TEX_CACHE; i++)
        n += tex_cache[i].len != 0;
    return n;
}

/* The cached description of a texture (its handle, scope and top level),
   if it is there and usable. */
static tex_cached_t *
tex_find(DWORD handle, const void *scope, LPSURF top)
{
    tex_cached_t *set = tex_set(handle, scope);
    DWORD         w;

    for (w = 0; w < TEX_WAYS; w++) {
        tex_cached_t *t = &set[w];

        if (t->len && t->usable && (t->key.handle == handle) && (t->key.scope == scope) && (t->key.top == top))
            return t;
    }
    return NULL;
}

DWORD
ctx_tex_describe(d3d_ctx_t *c, DWORD stage, DWORD *out)
{
    LPSURF top;
    int    has_alpha;

    return tex_build(c, stage, tex_surface(c, c->tss[stage][D3DTSS_TEXTUREMAP]), out, &has_alpha, &top, NULL);
}

/* A stage whose description a flush kept: its texture into the batch's
   list, as ctx_batch_textures lists them. */
static void
tex_list_kept(const d3d_ctx_t *c, DWORD s)
{
    const LPSURF  t = c->dep_tex[s];
    tex_cached_t *e;

    if (!t)
        return;
    e = tex_find(c->tss[s][D3DTSS_TEXTUREMAP], ctx_surface_handles(c) ? c->ddl : NULL, t);
    if (!e)
        ctx_batch_surface(t);
    else {
        e->used = ++tex_clock; /* in use: not the set's next victim */
        if (e->batch != batch_serial) {
            e->batch = batch_serial;
            batch_list(t, &e->chain);
        }
    }
}

/* --- State translation ---------------------------------------------------------- */

enum { K_BOOL, K_ENUM, K_ENUM0, K_MASK8, K_CLAMP, K_RAW, K_WRAP, K_MASK4, K_VBLEND };

static const struct {
    BYTE id;
    BYTE kind;
    BYTE max;
    BYTE def;
} gstates[] = {
    { VGPU98_RS_TEXTUREPERSPECTIVE, K_BOOL, 1, 1 }, { VGPU98_RS_ZENABLE, K_ENUM0, 2, 1 },
    { VGPU98_RS_FILLMODE, K_ENUM, 3, 3 },           { VGPU98_RS_SHADEMODE, K_ENUM, 3, 2 },
    { VGPU98_RS_ZWRITEENABLE, K_BOOL, 1, 1 },       { VGPU98_RS_ALPHATESTENABLE, K_BOOL, 1, 0 },
    { VGPU98_RS_LASTPIXEL, K_BOOL, 1, 1 },          { VGPU98_RS_SRCBLEND, K_ENUM, 13, 2 },
    { VGPU98_RS_DESTBLEND, K_ENUM, 13, 1 },         { VGPU98_RS_CULLMODE, K_ENUM, 3, 3 },
    { VGPU98_RS_ZFUNC, K_ENUM, 8, 4 },              { VGPU98_RS_ALPHAREF, K_MASK8, 0, 0 },
    { VGPU98_RS_ALPHAFUNC, K_ENUM, 8, 8 },          { VGPU98_RS_DITHERENABLE, K_BOOL, 1, 0 },
    { VGPU98_RS_ALPHABLENDENABLE, K_BOOL, 1, 0 },   { VGPU98_RS_FOGENABLE, K_BOOL, 1, 0 },
    { VGPU98_RS_SPECULARENABLE, K_BOOL, 1, 0 },     { VGPU98_RS_FOGCOLOR, K_RAW, 0, 0 },
    { VGPU98_RS_FOGTABLEMODE, K_ENUM0, 3, 0 },      { VGPU98_RS_FOGTABLESTART, K_RAW, 0, 0 },
    { VGPU98_RS_FOGTABLEEND, K_RAW, 0, 0 },         { VGPU98_RS_FOGTABLEDENSITY, K_RAW, 0, 0 },
    { VGPU98_RS_COLORKEYENABLE, K_BOOL, 1, 0 },     { VGPU98_RS_ZBIAS, K_CLAMP, 16, 0 },
    { VGPU98_RS_STENCILENABLE, K_BOOL, 1, 0 },      { VGPU98_RS_STENCILFAIL, K_ENUM, 8, 1 },
    { VGPU98_RS_STENCILZFAIL, K_ENUM, 8, 1 },       { VGPU98_RS_STENCILPASS, K_ENUM, 8, 1 },
    { VGPU98_RS_STENCILFUNC, K_ENUM, 8, 8 },        { VGPU98_RS_STENCILREF, K_MASK8, 0, 0 },
    { VGPU98_RS_STENCILMASK, K_RAW, 0, 0 },         { VGPU98_RS_STENCILWRITEMASK, K_RAW, 0, 0 },
    { VGPU98_RS_TEXTUREFACTOR, K_RAW, 0, 0 },       { VGPU98_RS_WRAP0, K_WRAP, 0, 0 },
    { VGPU98_RS_WRAP0 + 1, K_WRAP, 0, 0 },          { VGPU98_RS_WRAP0 + 2, K_WRAP, 0, 0 },
    { VGPU98_RS_WRAP0 + 3, K_WRAP, 0, 0 },          { VGPU98_RS_WRAP0 + 4, K_WRAP, 0, 0 },
    { VGPU98_RS_WRAP0 + 5, K_WRAP, 0, 0 },          { VGPU98_RS_WRAP0 + 6, K_WRAP, 0, 0 },
    { VGPU98_RS_WRAP0 + 7, K_WRAP, 0, 0 },          { VGPU98_RS_RANGEFOGENABLE, K_BOOL, 1, 0 },
    /* DirectX 7: the transform and lighting */
    { VGPU98_RS_CLIPPING, K_BOOL, 1, 1 },           { VGPU98_RS_LIGHTING, K_BOOL, 1, 1 },
    { VGPU98_RS_EXTENTS, K_BOOL, 1, 0 },            { VGPU98_RS_AMBIENT, K_RAW, 0, 0 },
    { VGPU98_RS_FOGVERTEXMODE, K_ENUM0, 3, 0 },     { VGPU98_RS_COLORVERTEX, K_BOOL, 1, 1 },
    { VGPU98_RS_LOCALVIEWER, K_BOOL, 1, 1 },        { VGPU98_RS_NORMALIZENORMALS, K_BOOL, 1, 0 },
    { VGPU98_RS_COLORKEYBLENDENABLE, K_BOOL, 1, 0 }, { VGPU98_RS_DIFFUSEMATERIALSOURCE, K_ENUM0, 2, 1 },
    { VGPU98_RS_SPECULARMATERIALSOURCE, K_ENUM0, 2, 2 }, { VGPU98_RS_AMBIENTMATERIALSOURCE, K_ENUM0, 2, 2 },
    { VGPU98_RS_EMISSIVEMATERIALSOURCE, K_ENUM0, 2, 0 }, { VGPU98_RS_VERTEXBLEND, K_VBLEND, 3, 0 },
    { VGPU98_RS_CLIPPLANEENABLE, K_RAW, 0, 0 },
    /* Command set 2.1 (DirectX 8): sent only to a GPU with PROFILE.FIXED8,
       which a DirectX 6 or 7 context finds at their defaults. 153
       (SOFTWAREVERTEXPROCESSING), 165 (DEBUGMONITORTOKEN) and 169 (the
       DDI's DELETERTPATCH) are no GPU states. */
    { VGPU98_RS_POINTSIZE, K_RAW, 0, 0 },          { VGPU98_RS_POINTSIZE_MIN, K_RAW, 0, 0 },
    { VGPU98_RS_POINTSPRITEENABLE, K_BOOL, 1, 0 }, { VGPU98_RS_POINTSCALEENABLE, K_BOOL, 1, 0 },
    { VGPU98_RS_POINTSCALE_A, K_RAW, 0, 0 },       { VGPU98_RS_POINTSCALE_B, K_RAW, 0, 0 },
    { VGPU98_RS_POINTSCALE_C, K_RAW, 0, 0 },       { VGPU98_RS_MULTISAMPLEANTIALIAS, K_BOOL, 1, 1 },
    { VGPU98_RS_MULTISAMPLEMASK, K_RAW, 0, 0 },    { VGPU98_RS_PATCHEDGESTYLE, K_RAW, 0, 0 },
    { VGPU98_RS_PATCHSEGMENTS, K_RAW, 0, 0 },      { VGPU98_RS_POINTSIZE_MAX, K_RAW, 0, 0 },
    { VGPU98_RS_INDEXEDVERTEXBLENDENABLE, K_BOOL, 1, 0 }, { VGPU98_RS_COLORWRITEENABLE, K_MASK4, 0, 0 },
    { VGPU98_RS_TWEENFACTOR, K_RAW, 0, 0 },        { VGPU98_RS_BLENDOP, K_ENUM, 5, 1 },
    { VGPU98_RS_POSITIONORDER, K_RAW, 0, 0 },      { VGPU98_RS_NORMALORDER, K_RAW, 0, 0 },
};

/* The gstates entry of each render state (0xFF: none), made by d3d_init
   before any context: a flush that is not full translates only the
   states set since the last (rs_dirty). */
static BYTE gstate_of[D3DRS_MAX] = { [0 ... D3DRS_MAX - 1] = 0xFF };

/* Only the entries' own values are written, the same each time: a
   DriverInit again (a mode change) never shows a flush an empty map. */
static void
gstate_map(void)
{
    DWORD i;

    for (i = 0; i < ARRAY_SIZE(gstates); i++)
        gstate_of[gstates[i].id] = (BYTE) i;
}

_Static_assert(ARRAY_SIZE(gstates) < 0xFF, "a gstates entry fits gstate_of");

/* A render state's value as the GPU takes it: Direct3D's value, reduced as
   Direct3D defines; an undefined enumeration becomes the default, so a
   program can never stop the GPU. */
static DWORD
gpu_rs_value(const d3d_ctx_t *c, int i)
{
    DWORD v = c->rs[gstates[i].id];

    switch (gstates[i].kind) {
        case K_BOOL:
            return v ? 1 : 0;
        case K_ENUM:
            return ((v >= 1) && (v <= gstates[i].max)) ? v : gstates[i].def;
        case K_ENUM0:
            return (v <= gstates[i].max) ? v : gstates[i].def;
        case K_MASK8:
            return v & 0xff;
        case K_CLAMP:
            return (v > gstates[i].max) ? gstates[i].max : v;
        case K_WRAP:
            return v & 15; /* DirectX 7: four coordinates */
        case K_MASK4:
            return v & 15;
        case K_VBLEND:
            /* DirectX 8 adds tweening (255) and one matrix by index (256). */
            if (c->dx8 && ((v == VGPU98_VBF_TWEENING) || (v == VGPU98_VBF_0WEIGHTS)))
                return v;
            return (v <= 3) ? v : 0;
        default:
            return v;
    }
}

static DWORD
gpu_arg(const d3d_ctx_t *c, DWORD a)
{
    DWORD sel = a & VGPU98_TA_SELECTMASK;

    /* A selector Direct3D does not define: the current colour
       (ValidateTextureStageState reports it). DirectX 8 adds TEMP. */
    if ((sel > VGPU98_TA_SPECULAR) && !(c->dx8 && (sel == VGPU98_TA_TEMP)))
        sel = VGPU98_TA_CURRENT;
    return sel | (a & (VGPU98_TA_COMPLEMENT | VGPU98_TA_ALPHAREPLICATE));
}

/* DirectX 8's filters (D3DTEXF: none 0, point 1, linear 2, anisotropic 3,
   flat cubic 4, Gaussian cubic 5) as DirectX 7's, which the GPU takes. */
static DWORD
filter_dx7(DWORD n, DWORD v)
{
    switch (n) {
        case VGPU98_TSS_MAGFILTER:
            return (v <= D3DTEXF_POINT) ? 1 : (v == D3DTEXF_LINEAR) ? 2 : (v == D3DTEXF_ANISOTROPIC) ? 5 : v - 1;
        case VGPU98_TSS_MINFILTER:
            return (v <= D3DTEXF_POINT) ? 1 : (v == D3DTEXF_ANISOTROPIC) ? 3 : 2;
        default: /* MIPFILTER: none 1, point 2, linear 3 */
            return (v == D3DTEXF_NONE) ? 1 : (v == D3DTEXF_POINT) ? 2 : 3;
    }
}

/* The last texture operation a context may use: DirectX 8 adds
   MULTIPLYADD and LERP. */
static DWORD
top_last(const d3d_ctx_t *c)
{
    return c->dx8 ? VGPU98_TOP_LAST8 : VGPU98_TOP_LAST;
}

static int
colour_only_op(DWORD op)
{
    return ((op >= VGPU98_TOP_MODULATEALPHA_ADDCOLOR) && (op <= VGPU98_TOP_MODULATEINVCOLOR_ADDALPHA)) ||
           (op == VGPU98_TOP_DOTPRODUCT3);
}

static DWORD
gpu_tss_value(const d3d_ctx_t *c, DWORD s, DWORD n, int stage0_alpha)
{
    DWORD v = c->tss[s][n];

    if (c->dx8 && ((n == VGPU98_TSS_MAGFILTER) || (n == VGPU98_TSS_MINFILTER) || (n == VGPU98_TSS_MIPFILTER)))
        v = filter_dx7(n, v);
    switch (n) {
        case VGPU98_TSS_COLOROP:
            return ((v >= 1) && (v <= top_last(c))) ? v : VGPU98_TOP_DISABLE;
        case VGPU98_TSS_ALPHAOP:
            if (v == TOP_LEGACY_ALPHAOVR)
                return stage0_alpha ? VGPU98_TOP_SELECTARG1 : VGPU98_TOP_SELECTARG2;
            if ((v < 1) || (v > top_last(c)))
                return VGPU98_TOP_DISABLE;
            return colour_only_op(v) ? VGPU98_TOP_SELECTARG1 : v;
        case VGPU98_TSS_COLORARG1:
        case VGPU98_TSS_COLORARG2:
        case VGPU98_TSS_ALPHAARG1:
        case VGPU98_TSS_ALPHAARG2:
        case VGPU98_TSS_COLORARG0:
        case VGPU98_TSS_ALPHAARG0:
            return gpu_arg(c, v);
        case VGPU98_TSS_RESULTARG:
            return (c->dx8 && (v == VGPU98_TA_TEMP)) ? VGPU98_TA_TEMP : VGPU98_TA_CURRENT;
        case VGPU98_TSS_TEXCOORDINDEX:
            /* The set, and DirectX 7's generated coordinates. */
            return v & (VGPU98_TCI_SET_MASK | VGPU98_TCI_GEN_MASK);
        case VGPU98_TSS_ADDRESSU:
        case VGPU98_TSS_ADDRESSV:
        case VGPU98_TSS_ADDRESSW: /* DirectX 8: a volume's third coordinate */
            return ((v >= 1) && (v <= 4)) ? v : 1;
        case VGPU98_TSS_MAGFILTER:
            /* Cubic magnification is not advertised: linear. */
            return ((v == 1) || (v == 2) || (v == 5)) ? v : ((v == 3) || (v == 4)) ? 2 : 1;
        case VGPU98_TSS_MINFILTER:
        case VGPU98_TSS_MIPFILTER:
            return ((v >= 1) && (v <= 3)) ? v : 1;
        case VGPU98_TSS_MAXANISOTROPY:
            return (v < 1) ? 1 : (v > 16) ? 16 : v;
        case VGPU98_TSS_TEXTURETRANSFORMFLAGS:
            return v & (VGPU98_TTFF_COUNT_MASK | VGPU98_TTFF_PROJECTED);
        default:
            return v;
    }
}

/* The stage states the GPU takes: 2.1's (25-28) from a GPU with FIXED8. */
static int
tss_sent_id(DWORD n)
{
    return (n >= 1) && (n < (d3d8_on ? VGPU98_TSS_COUNT8 : VGPU98_TSS_COUNT)) && (n != D3DTSS_ADDRESS);
}

/* --- Sending the context's state ------------------------------------------------ */

/* The render target by role:
   Direct3D draws a flipping chain's frames into a back buffer, so a
   target that is the chain's member on display is drawn as the member the
   next flip shows, whatever made its name find it (a runtime naming the
   front buffer, a handle a flip moved). DirectX 8 draws into no front
   buffer at all; an older interface may (a loading screen before any
   flip), so its target moves only once a FLIP came since it was named.
   Every other target is left as named: a back buffer (the first or any
   other), one outside a chain, the DLL's own copy of a record. */
void
ctx_target_follow(d3d_ctx_t *c)
{
    LPSURF b;

    if (c->target != c->follow_named) {
        c->follow_named = c->target;
        c->follow_flip  = sb->fence_flip;
    }
    if (!c->target || heap_owns(c->target) || (!c->dx8 && (c->follow_flip == sb->fence_flip)))
        return;
    b = d3d7_flip_next(c->target);
    if (b) {
        c->target       = b;
        c->follow_named = b;
        c->clean        = 0;
    }
}

int
ctx_flush(d3d_ctx_t *c, int with_textures)
{
    const DWORD     handle = ctx_handle(c);
    const int       full   = (sb->state_owner != handle) || (c->sent_epoch != sb->state_epoch);
    const DWORD     world  = __atomic_load_n(&world_epoch, __ATOMIC_ACQUIRE);
    int             was_clean;
    gsurf_t         gt;
    gsurf_t         gz;
    DWORD           target[6];
    /* Large, so not on the stack; ctx_flush runs under the ring lock. */
    static DWORD    pairs[2 * (ARRAY_SIZE(gstates) + D3D_STAGES * VGPU98_TSS_COUNT8)];
    static DWORD    texdesc[D3D_STAGES][TEX_DESC_MAX];
    static DWORD    colors[256];
    DWORD           npairs = 0;
    DWORD           dirty  = full ? DIRTY_ALL : c->dirty;
    DWORD           limit;
    DWORD           pmask;
    DWORD           active = 0;
    DWORD           mem[2];
    int             reuse_ok;
    uint64_t        t_tex;
    int             alpha0 = 1;
    int             always = 0;
    DWORD           n_dep  = 0;
    volatile DWORD *p;
    DWORD           s;
    DWORD           i;

    DIAG(diag.flushes++);
    ctx_target_follow(c);
    was_clean = c->clean; /* after the target's follow: a move is a change */
    /* Clean again only once all of it went. */
    c->clean = 0;
    if (!c->target)
        return -1;
    /* The target and Z buffer: described again only when they, their
       memory (read before the description) or the world changed since the
       last flush described them (else the GPU holds them as sent). */
    mem[0] = c->target->lpGbl->fpVidMem;
    mem[1] = c->zbuf ? c->zbuf->lpGbl->fpVidMem : 0;
    if (full || (c->target != c->flush_target) || (c->zbuf != c->flush_zbuf) || (world != c->flush_world) ||
        (mem[0] != c->flush_mem[0]) || (mem[1] != c->flush_mem[1])) {
        c->flush_target = NULL;
        if (!surf_describe(c->target, &gt))
            return -1;
        if ((gt.format != VGPU98_FMT_R5G6B5) && (gt.format != VGPU98_FMT_X1R5G5B5) && (gt.format != VGPU98_FMT_X8R8G8B8) &&
            (gt.format != VGPU98_FMT_A8R8G8B8) && !(d3d8_on && (gt.format == VGPU98_FMT_A1R5G5B5)))
            return -1;
        target[0] = gt.offset;
        target[1] = VGPU98_SURF_SIZE(gt.width, gt.height);
        target[2] = VGPU98_SURF_PITCH(gt.pitch, gt.format);
        target[3] = target[4] = target[5] = 0;
        if (c->zbuf && surf_describe(c->zbuf, &gz) && (gz.format >= VGPU98_FMT_D16) && (gz.width >= gt.width) &&
            (gz.height >= gt.height)) {
            target[3] = gz.offset;
            target[4] = VGPU98_SURF_SIZE(gz.width, gz.height);
            target[5] = VGPU98_SURF_PITCH(gz.pitch, gz.format);
        }
        if (full || dwords_differ(target, c->sent_target, 6)) {
            if (!(p = gpu_packet(VGPU98_OP_TARGET, VGPU98_LEN_TARGET)))
                return 0;
            for (i = 0; i < 6; i++)
                pkt_put(&p, target[i]);
            memcpy(c->sent_target, target, sizeof(target));
        }
        c->flush_target = c->target;
        c->flush_zbuf   = c->zbuf;
        c->flush_world  = world;
        c->flush_mem[0] = mem[0];
        c->flush_mem[1] = mem[1];
    }
    /* At every flush, whatever cleared them: the GPU may hold newer pixels
       than video memory (Lock and Blt read these flags). */
    c->target->dwReserved1 |= SURF_GPU_WRITES;
    if (c->zbuf)
        c->zbuf->dwReserved1 |= SURF_GPU_WRITES;
    /* The viewport and W range: only after VIEWPORTINFO, WINFO or the
       defaults set them. */
    if ((full || (dirty & DIRTY_VIEW)) && (full || dwords_differ(c->vp, c->sent_vp, 4))) {
        if (!(p = gpu_packet(VGPU98_OP_VIEWPORT, VGPU98_LEN_VIEWPORT)))
            return 0;
        for (i = 0; i < 4; i++)
            pkt_put(&p, (c->vp[i] > 0xffff) ? 0xffff : c->vp[i]);
        memcpy(c->sent_vp, c->vp, sizeof(c->vp));
    }
    if ((full || (dirty & DIRTY_VIEW)) && (full || (c->wnear != c->sent_w[0]) || (c->wfar != c->sent_w[1]))) {
        if (!(p = gpu_packet(VGPU98_OP_WRANGE, VGPU98_LEN_WRANGE)))
            return 0;
        pkt_putf(&p, c->wnear);
        pkt_putf(&p, c->wfar);
        c->sent_w[0] = c->wnear;
        c->sent_w[1] = c->wfar;
    }
    if (c->dx7 && (full || c->tl_dirty) && !tl_flush(c, full))
        return 0;

    if (full) {
        /* The GPU's textures are not this context's: every stage that
           blends sends its own again, now or when it next blends, and none
           keeps its description (a Clear's whole flush included). */
        for (s = 0; s < D3D_STAGES; s++) {
            c->sent_tex_len[s] = ~0u;
            c->sent_pal[s]     = 0;
        }
        c->dep_valid = 0;
    }

    /* The stages that blend, as the reference rasterizer counts them: one
       for a legacy texture handle; otherwise up to the first stage that is
       disabled, or that takes a texture it does not have. The GPU stops at
       the first disabled stage, so only these stages' textures are built;
       stage 0's decides the legacy MODULATE's alpha. With a pixel program
       bound, every stage up to the last it samples, whatever their COLOROP
       (DirectX 8's default for stages 1-7 is DISABLE, which a shader leaves
       as it is). */
    t_tex = (diag_on && with_textures) ? rdtsc() : 0;
    /* The last flush with textures, when it completed and the world has not
       moved since (no surface, handle, colour key or attachment changed),
       described each stage it looked at from what still holds: a stage
       whose texture handle was not set since, and whose texture's memory
       and source colour key are as they were, keeps its description (the
       GPU holds it), without the lookup; a DirectX 7
       or 8 stage's 8-bit texture also its palette as sent, which only a move
       of the palette epoch since makes it look up. */
    const DWORD pal_epoch = d3d7_palette_epoch();

    reuse_ok = with_textures && !full && c->dep_valid && (world == c->dep_world) && !c->dep_always;
    if (with_textures)
        c->dep_valid = 0;
    pmask = d3d8_ps_stages(c);
    limit = pmask ? 32 - (DWORD) __builtin_clz(pmask) : c->rs[D3DRS_TEXTUREHANDLE] ? 1 : D3D_STAGES;
    for (s = 0; s < limit; s++) {
        LPSURF top = NULL;
        int    has_alpha;
        int    present;

        if ((limit > 1) && !pmask && (gpu_tss_value(c, s, VGPU98_TSS_COLOROP, 1) == VGPU98_TOP_DISABLE))
            break;
        /* A DirectX 7 or 8 stage's 8-bit texture also needs its palette as
           sent: SETPALETTE and UPDATEPALETTE leave the world as it is. */
        if (reuse_ok && (s < c->dep_n) && !((c->tex_changed >> s) & 1) && (c->sent_tex_len[s] != ~0u) &&
            (!c->dep_tex[s] || ((c->dep_tex[s]->lpGbl->fpVidMem == c->dep_tex_mem[s]) &&
                                ((c->dep_tex[s]->dwFlags & DDRAWISURF_HASCKEYSRCBLT) == c->dep_tex_ckf[s]) &&
                                (c->dep_tex[s]->ddckCKSrcBlt.dwColorSpaceLowValue == c->dep_tex_ck[s]))) &&
            (!c->dep_p8[s] || (pal_epoch == c->dep_pal_epoch) || (stage_pal_key(c, s) == c->sent_pal[s]))) {
            GUARD_STEP(GUARD_STEP_TEXTURE, s, c->tss[s][D3DTSS_TEXTUREMAP]);
            top   = c->dep_tex[s];
            n_dep = s + 1;
            if (s == 0)
                alpha0 = top ? (int) c->dep_tex_alpha[0] : 1;
            tex_list_kept(c, s);
            DIAG(diag.tex_reused++);
            present = top != NULL;
        } else if (with_textures) {
            int         loose;
            DWORD       pal_key;
            const DWORD len = tex_desc(c, s, texdesc[s], &has_alpha, &top, &loose, &pal_key);

            if (diag_textures)
                tt_bind(c, s, top);
            /* What the stage's texture was built from, for ctx_unchanged.
               DirectDraw changes a DirectX 6 palette's entries, and takes
               off a level AddAttachedSurface attached, without telling the
               driver: such a texture is checked at every call. */
            c->dep_tex[s] = top;
            c->dep_p8[s]  = 0;
            if (top) {
                c->dep_tex_mem[s]   = top->lpGbl->fpVidMem;
                c->dep_tex_ckf[s]   = top->dwFlags & DDRAWISURF_HASCKEYSRCBLT;
                c->dep_tex_ck[s]    = top->ddckCKSrcBlt.dwColorSpaceLowValue;
                c->dep_tex_alpha[s] = (DWORD) has_alpha;
                if (loose || (!ctx_surface_handles(c) && ((texdesc[s][2] & 0xff) == VGPU98_FMT_P8)))
                    always = 1;
            }
            n_dep = s + 1;
            if (s == 0)
                alpha0 = top ? has_alpha : 1;
            if ((len != c->sent_tex_len[s]) || dwords_differ(texdesc[s], c->sent_tex[s], len)) {
                if (!(p = gpu_packet(VGPU98_OP_TEXTURE, len)))
                    return 0;
                for (i = 0; i < len; i++)
                    pkt_put(&p, texdesc[s][i]);
                memcpy(c->sent_tex[s], texdesc[s], len * sizeof(DWORD));
                c->sent_tex_len[s] = len;
                c->sent_pal[s]     = 0;
            }
            /* The palette: DirectX 7 and 8 by its version (the colours read
               only when it changed), the one read with the description's
               alpha flag; DirectX 6 by its colours' signature. */
            if (top && ((texdesc[s][2] & 0xff) == VGPU98_FMT_P8)) {
                const int   by_version = ctx_surface_handles(c);
                const DWORD key        = !by_version ? 0 : pal_key ? pal_key : stage_pal_key(c, s);

                c->dep_p8[s] = (BYTE) by_version;
                if (!by_version || (key != c->sent_pal[s])) {
                    const DWORD sig  = palette_get(c, c->tss[s][D3DTSS_TEXTUREMAP], top, colors);
                    const DWORD sent = by_version ? key : sig;

                    if (sent != c->sent_pal[s]) {
                        if (!(p = gpu_packet(VGPU98_OP_PALETTE, 2 + 256)))
                            return 0;
                        pkt_put(&p, s);
                        pkt_put(&p, 0 | (256u << 16));
                        for (i = 0; i < 256; i++)
                            pkt_put(&p, colors[i]);
                        c->sent_pal[s] = sent;
                        DIAG(diag.pal_unbound += by_version && (sent == PAL_UNBOUND)); /* an 8-bit texture drawn with no palette: zeros */
                        if (diag_textures && by_version && (sent == PAL_UNBOUND))
                            tt_nopal(c, s, top);
                    }
                }
            }
            present = top != NULL;
        } else
            present = tex_surface(c, c->tss[s][D3DTSS_TEXTUREMAP]) != NULL;
        if ((limit > 1) && !pmask && !present &&
            ((gpu_tss_value(c, s, VGPU98_TSS_COLORARG1, 1) & VGPU98_TA_SELECTMASK) == VGPU98_TA_TEXTURE))
            break;
        active++;
    }
    if (t_tex)
        diag.cyc_dp2_tex += rdtsc() - t_tex;

    if (full) {
        for (i = 0; i < ARRAY_SIZE(gstates); i++) {
            DWORD v;

            if (!d3d8_on && (gstates[i].id >= VGPU98_RS_GLOBAL_COUNT))
                break; /* a 2.0 GPU: the 2.1 states come last */
            v                         = gpu_rs_value(c, (int) i);
            pairs[npairs++]           = gstates[i].id;
            pairs[npairs++]           = v;
            c->sent_rs[gstates[i].id] = v;
        }
    } else if (dirty & DIRTY_RS) {
        /* Only the states set since the last flush, in their order. */
        DWORD w;

        for (w = 0; w < D3DRS_MAX / 32; w++) {
            DWORD bits = c->rs_dirty[w];

            while (bits) {
                const DWORD id = 32 * w + (DWORD) __builtin_ctz(bits);
                const DWORD gi = gstate_of[id];
                DWORD       v;

                bits &= bits - 1;
                if ((gi == 0xFF) || (!d3d8_on && (id >= VGPU98_RS_GLOBAL_COUNT)))
                    continue;
                v = gpu_rs_value(c, (int) gi);
                if (c->sent_rs[id] != v) {
                    pairs[npairs++] = id;
                    pairs[npairs++] = v;
                    c->sent_rs[id]  = v;
                }
            }
        }
    }
    /* A stage's values depend on its own states (those set since the last
       flush), and on stage 0's texture alpha (the legacy MODULATE: ALPHAOP)
       and the stage where blending stops (that stage's and the last's
       states all). None of it marked or moved: no stage looked at. */
    for (s = 0; (full || (dirty & DIRTY_STAGES) || (active != c->sent_active) || (alpha0 != c->sent_alpha0)) && (s < D3D_STAGES);
         s++) {
        DWORD bits = 0;
        DWORD n;

        if (full || ((active != c->sent_active) && ((s == (active & 7)) || (s == (c->sent_active & 7)))))
            bits = ~0u;
        else if (dirty & DIRTY_STAGE(s))
            bits = c->tss_dirty[s];
        if ((alpha0 != c->sent_alpha0) && (c->tss[s][VGPU98_TSS_ALPHAOP] == TOP_LEGACY_ALPHAOVR))
            bits |= 1u << VGPU98_TSS_ALPHAOP;
        if (!bits)
            continue;
        for (n = 1; n < VGPU98_TSS_COUNT8; n++) {
            DWORD v;

            if (!((bits >> n) & 1) || !tss_sent_id(n))
                continue;
            v = gpu_tss_value(c, s, n, alpha0);
            if ((n == VGPU98_TSS_COLOROP) && (s == active))
                v = VGPU98_TOP_DISABLE;
            if (full || (c->sent_tss[s][n] != v)) {
                pairs[npairs++]    = VGPU98_TSS(s, n);
                pairs[npairs++]    = v;
                c->sent_tss[s][n] = v;
            }
        }
    }
    if (npairs) {
        if (!(p = gpu_packet(VGPU98_OP_STATE, npairs)))
            return 0;
        for (i = 0; i < npairs; i++)
            pkt_put(&p, pairs[i]);
        DIAG(diag.values += npairs / 2);
    }
    sb->state_owner = handle;
    c->sent_epoch   = sb->state_epoch;
    c->sent_alpha0  = alpha0;
    c->sent_active  = active;
    c->dirty        = 0;
    memset(c->rs_dirty, 0, sizeof(c->rs_dirty));
    memset(c->tss_dirty, 0, sizeof(c->tss_dirty));
    /* Without textures (a Clear), the stages' textures were not checked, and
       stage values that depend on them (stage 0's alpha, where blending
       stops) were guessed: the context stays clean only if it was, the GPU
       still held its state, and the flush sent no value (ctx_unchanged
       checks the rest at the next call). */
    c->clean        = with_textures || (was_clean && !full && !npairs);
    if (with_textures) {
        c->dep_world     = world;
        c->dep_pal_epoch = pal_epoch; /* read before the stages: a change during the flush moved it past this */
        c->dep_always    = always;
        c->dep_n         = n_dep;
        c->dep_valid     = 1;
        c->tex_changed   = 0;
        c->dep_mem[0] = c->target->lpGbl->fpVidMem;
        c->dep_mem[1] = c->zbuf ? c->zbuf->lpGbl->fpVidMem : 0;
    }
    return 1;
}

int
ctx_unchanged(d3d_ctx_t *c)
{
    const DWORD pal_epoch = d3d7_palette_epoch();
    const int   pal_moved = pal_epoch != c->dep_pal_epoch;
    DWORD       s;

    /* The world first: a surface destroyed moved it before it went. */
    if (c->dep_always || (c->dep_world != __atomic_load_n(&world_epoch, __ATOMIC_ACQUIRE)) || !c->target)
        return 0;
    if ((c->target->lpGbl->fpVidMem != c->dep_mem[0]) || ((c->zbuf ? c->zbuf->lpGbl->fpVidMem : 0) != c->dep_mem[1]))
        return 0;
    for (s = 0; s < c->dep_n; s++) {
        const LPSURF t = c->dep_tex[s];

        if (t && ((t->lpGbl->fpVidMem != c->dep_tex_mem[s]) ||
                  ((t->dwFlags & DDRAWISURF_HASCKEYSRCBLT) != c->dep_tex_ckf[s]) ||
                  (t->ddckCKSrcBlt.dwColorSpaceLowValue != c->dep_tex_ck[s])))
            return 0;
        /* A palette bound or changed since (it leaves the world as it is;
           the palette epoch where it was, none did). */
        if (t && c->dep_p8[s] && pal_moved && (stage_pal_key(c, s) != c->sent_pal[s]))
            return 0;
    }
    /* Every stage's palette as sent, looked up after the epoch was read:
       the next call need not look again until it moves. */
    c->dep_pal_epoch = pal_epoch;
    return 1;
}

void
ctx_batch_textures(const d3d_ctx_t *c)
{
    const void *scope = ctx_surface_handles(c) ? c->ddl : NULL;
    DWORD       s;

    for (s = 0; s < c->dep_n; s++) {
        tex_cached_t *e;

        if (!c->dep_tex[s])
            continue;
        e = tex_find(c->tss[s][D3DTSS_TEXTUREMAP], scope, c->dep_tex[s]);
        if (!e)
            ctx_batch_surface(c->dep_tex[s]);
        else if (e->batch != batch_serial) {
            e->batch = batch_serial;
            batch_list(c->dep_tex[s], &e->chain);
        }
    }
}

/* The textures are the ones the batch's draws used (its list), not those
   left bound at its end: a texture bound but not drawn with, say by a Clear
   straight after a flip, would otherwise wait for a fence queued behind the
   flip. */
void
ctx_used(d3d_ctx_t *c, DWORD fence)
{
    DWORD i;

    if (c->target)
        surf_used(c->target, fence);
    if (c->zbuf)
        surf_used(c->zbuf, fence);
    for (i = 0; i < batch_tex_n; i++) {
        if (batch_lvl_n[i]) {
            DWORD n;

            for (n = 0; n < batch_lvl_n[i]; n++)
                surf_used(batch_lvl[i][n], fence);
        } else
            tex_used(batch_tex[i], fence);
    }
    if (batch_tex_all) {
        /* Every texture the context can name (the report counts these). */
        DIAG(diag.batch_overflows++);
        if (ctx_surface_handles(c))
            d3d7_fence_all(c->ddl, fence);
        else {
            for (i = 1; i < MAX_TEXTURES; i++)
                tex_used(tex_surface(c, i), fence);
        }
    }
    batch_tex_n   = 0;
    batch_tex_all = 0;
}

void
ctx_batch_surface(LPSURF s)
{
    DWORD i;

    for (i = 0; i < batch_tex_n; i++) {
        if (batch_tex[i] == s)
            return;
    }
    batch_list(s, NULL);
}

int
ctx_token_live(DWORD token)
{
    const d3d_ctx_t *c = ctx_get(CTX_TOKEN_HANDLE(token));

    return token && c && (ctx_token(c) == token);
}

void
ctx_refused(DWORD token, const char *name, DWORD op, DWORD arg, DWORD code)
{
    d3d_ctx_t  *c   = ctx_get(CTX_TOKEN_HANDLE(token));
    const DWORD now = GetTickCount();

    /* A context since destroyed (its slot may hold another), or stopped. */
    if (!c || (ctx_token(c) != token) || c->stopped)
        return;
    if (c->refused_n && (op == c->refused_op) && (arg == c->refused_arg) && (code == c->refused_code) &&
        (now - c->refused_tick < GPU_ERR_WINDOW_MS))
        c->refused_n++;
    else {
        c->refused_n    = 1;
        c->refused_op   = op;
        c->refused_arg  = arg;
        c->refused_code = code;
    }
    c->refused_tick = now;
    if (c->refused_n < GPU_ERR_STOP)
        return;
    c->stopped = 1;
    DIAG(diag.ctx_stopped++);
    dbg_event(DLL_TAG ": context %u (process %x) stops drawing: the GPU refused its %s (first %x), error %u, %u times in a row",
              ctx_handle(c), c->pid, name, arg, code, c->refused_n);
}

/* --- The callbacks ---------------------------------------------------------------- */

void
ctx_resolve(d3d_ctx_t *c)
{
    LPSURF t;
    LPSURF z;

    if (!c->dx7)
        return;
    GUARD_STEP(GUARD_STEP_TARGET, c->h_target, c->h_zbuf);
    t = d3d7_surface(c->ddl, c->h_target);
    z = d3d7_surface(c->ddl, c->h_zbuf);
    if (c->dx8 && ((!t && c->h_target) || (!z && c->h_zbuf)) && TRACE(TRACE_LOOKUP))
        dbg(DLL_TAG " trace: context %u: target %x (%s), depth %x (%s) under %x", ctx_handle(c), c->h_target,
            t ? "found" : "not found", c->h_zbuf, z ? "found" : "not found", (DWORD) c->ddl);
    if ((t != c->target) || (z != c->zbuf)) {
        c->target = t;
        c->zbuf   = z;
        c->clean  = 0;
    }
}

/* DirectX 8: a target or Z buffer the call named by the runtime's surface
   becomes the DLL's copy when the map holds one (made after the object was
   marked: a second context, a windowed back buffer); the runtime's lasting
   records (the flipping chain, what the device's creation made) stay. */
static void
ctx_own_targets(d3d_ctx_t *c)
{
    LPSURF t;
    LPSURF z;

    if (!c->dx8)
        return;
    d3d7_copy_target(c->ddl, c->target);
    d3d7_copy_target(c->ddl, c->zbuf);
    t = c->h_target ? d3d7_surface(c->ddl, c->h_target) : NULL;
    z = c->h_zbuf ? d3d7_surface(c->ddl, c->h_zbuf) : NULL;
    if (t && heap_owns(t))
        c->target = t;
    if (z && heap_owns(z))
        c->zbuf = z;
}

void
ctx_recheck(d3d_ctx_t *c)
{
    const DWORD epoch = __atomic_load_n(&world_epoch, __ATOMIC_ACQUIRE);
    LPSURF      t     = c->target;
    LPSURF      z     = c->zbuf;

    if (c->rec_epoch == epoch)
        return;
    c->rec_epoch = epoch;
    /* A copy, or none since one went (a restored surface comes back under
       its handle). */
    if (t ? heap_owns(t) : (c->h_target != 0))
        t = d3d7_surface(c->ddl, c->h_target);
    if (z ? heap_owns(z) : (c->h_zbuf != 0))
        z = d3d7_surface(c->ddl, c->h_zbuf);
    if ((t != c->target) || (z != c->zbuf)) {
        c->target = t;
        c->zbuf   = z;
        c->clean  = 0;
    }
}

/* A DirectX 7 runtime names the DirectDraw local object and the surfaces'
   local objects (their handles are what the context keeps); an older one,
   the surfaces' interface objects. */
GUARDED_FIG(static, ContextCreate, D3DHAL_CONTEXTCREATEDATA, ddrval, d->dwhContext, d->dwPID)
{
    DWORD i;

    /* The stream audit proves the fault guard first (once a process). */
    if (diag_audit8)
        guard_selftest();
    /* The slots of processes that ended without destroying their contexts
       come back; the flight recorder shows those that ended without one. */
    ctx_reap(0);
    flight_reap();
    for (i = 0; i < MAX_CONTEXTS; i++) {
        d3d_ctx_t *c        = &d3d_ctx[i];
        DWORD      expected = CTX_FREE;

        if (!__atomic_compare_exchange_n(&c->used, &expected, CTX_BUSY, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
            continue;
        memset((uint8_t *) c + sizeof(c->used), 0, sizeof(*c) - sizeof(c->used));
        c->pid    = d->dwPID;
        c->serial = __atomic_add_fetch(&ctx_serials, 1, __ATOMIC_RELAXED);
        if (d3d7_on) {
            c->dx7    = 1;
            c->ddl    = d->lpDDGbl;
            c->iface  = d->dwhContext; /* an input here: the interface */
            /* The DirectX 8 interface's context: streams, shader handles,
               DirectX 8's states and numbers (with PROFILE.FIXED8). */
            c->dx8 = d3d8_on && (c->iface >= D3D_IFACE_DX8);
            if (c->dx8)
                d3d7_mark_dx8(c->ddl);
            c->target = (LPSURF) d->lpDDS;
            c->zbuf   = (LPSURF) d->lpDDSZ;
            if (c->target && c->target->lpSurfMore)
                c->h_target = c->target->lpSurfMore->dwSurfaceHandle;
            if (c->zbuf && c->zbuf->lpSurfMore)
                c->h_zbuf = c->zbuf->lpSurfMore->dwSurfaceHandle;
            ctx_own_targets(c);
        } else {
            c->target = d->lpDDS ? d->lpDDS->lpLcl : NULL;
            c->zbuf   = d->lpDDSZ ? d->lpDDSZ->lpLcl : NULL;
        }
        ctx_defaults(c);
        __atomic_add_fetch(&ctx_live, 1, __ATOMIC_RELAXED);
        __atomic_store_n(&c->used, CTX_LIVE, __ATOMIC_RELEASE);
        d->dwhContext = ctx_handle(c);
        d->ddrval     = D3D_OK;
        dbg_event(DLL_TAG ": context %u for process %x (%s)%s", d->dwhContext, d->dwPID, trace_process(c->exe, sizeof(c->exe)),
                  c->dx8 ? ", DirectX 8" : c->dx7 ? ", DirectX 7" : "");
        if (diag_textures)
            dp2_proc_again();
        /* A new program, or a new test: its palettes' and the heap's first
           "no room" lines are named again, at Minimal too. */
        d3d7_lines_again();
        heap_lines_again();
        c->trace_cmds = c->dx8 ? 64 : 0;
        c->trace_dip2 = c->dx8 ? 8 : 0;
        c->tt_pal     = TT_PALETTES;
        c->tt_nopal   = TT_NOPAL;
        c->tt_texblt  = TT_TEXBLTS;
        c->tt_mx      = TT_MATRICES;
        memset(c->tt_handle, 0, sizeof(c->tt_handle));
        memset(c->tt_surf, 0, sizeof(c->tt_surf));
        memset(c->tt_mem, 0, sizeof(c->tt_mem));
        /* 3DMark makes a device a test: each gets the trace's budgets
           (a DirectX 7 one too with the texture audit, at both its levels). */
        if ((c->dx8 && diag_audit8) || (diag_textures && (c->dx7 || c->dx8)))
            trace_reset();
        if (TRACE(TRACE_CONTEXT))
            dbg(DLL_TAG " trace: context %u: interface %u, DirectDraw %x, target %x (handle %x), depth %x (handle %x), runtime %x",
                d->dwhContext, c->iface, (DWORD) c->ddl, (DWORD) c->target,
                (c->target && c->target->lpSurfMore) ? c->target->lpSurfMore->dwSurfaceHandle : 0, (DWORD) c->zbuf,
                (c->zbuf && c->zbuf->lpSurfMore) ? c->zbuf->lpSurfMore->dwSurfaceHandle : 0, d3d8_runtime());
        return DDHAL_DRIVER_HANDLED;
    }
    d->ddrval = D3DHAL_OUTOFCONTEXTS;
    return DDHAL_DRIVER_HANDLED;
}

_Static_assert(offsetof(d3d_ctx_t, used) == 0, "a context's claim comes first");

/* One caller tears a context down: DDHELP's clean-up and a new context may
   both find a process's context to reap. */
static void
ctx_free(d3d_ctx_t *c)
{
    DWORD expected = CTX_LIVE;
    DWORD left;

    if (!__atomic_compare_exchange_n(&c->used, &expected, CTX_BUSY, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return;
    if (sb->state_owner == ctx_handle(c))
        sb->state_owner = 0;
    d3d7_sb_forget(ctx_handle(c));
    heap_free(c->lights);
    c->lights     = NULL;
    c->n_lights   = 0;
    c->cap_lights = 0;
    d3d8_vs_forget(c);
    d3d8_ps_forget(c);
    heap_free(c->pal);
    c->pal = NULL;
    left          = __atomic_sub_fetch(&ctx_live, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&c->used, CTX_FREE, __ATOMIC_RELEASE);
    /* The pool kept its free chunks while Direct3D ran. With the log on
       (Minimal too), the host's watchdog hears that drawing ended
       (DEBUG_IDLE), so a program back at its menu is no stall:
       once the GPU has done the last of it,
       as the host times the write when it arrives and the GPU may be a
       frame behind. */
    if (!left) {
        pool_trim();
        if (diag_on || diag_events) {
            (void) gpu_wait(sb->fence_issued);
            reg_write(VGPU98_REG_DEBUG, VGPU98_DEBUG_IDLE);
        }
    }
}

DWORD
d3d_contexts(void)
{
    return __atomic_load_n(&ctx_live, __ATOMIC_RELAXED);
}

/* Is the process still running? Windows 98 keeps an ended process's object
   while a handle to it is open, so its exit code says; an ID that names no
   process (ERROR_INVALID_PARAMETER) is one that ended. Any other failure
   counts as running. */
int
process_alive(DWORD pid)
{
    DWORD h;
    DWORD code = STILL_ACTIVE;

    if (pid == GetCurrentProcessId())
        return 1;
    h = OpenProcess(PROCESS_QUERY_INFORMATION, 0, pid);
    if (!h)
        return GetLastError() != ERROR_INVALID_PARAMETER;
    if (!GetExitCodeProcess(h, &code))
        code = STILL_ACTIVE;
    CloseHandle(h);
    return code == STILL_ACTIVE;
}

/* Everything of a process that ended: the DLL's locks it held, its
   contexts (the flight recorder's lines first, saying `why`), its texture
   handles, its DirectX 7 handles and palettes. */
static void
process_gone(DWORD pid, const char *why)
{
    DWORD i;

    dlock_release_process(pid);
    /* The ring lock first: d3d7_forget may wait for the GPU, which a batch
       the process left half rung would hold up. */
    gpu_release_process(pid);
    shadow_process_gone(pid);
    for (i = 0; i < MAX_CONTEXTS; i++) {
        if ((d3d_ctx[i].used == CTX_LIVE) && (d3d_ctx[i].pid == pid)) {
            flight_dump(why, pid, &d3d_ctx[i], 0);
            ctx_free(&d3d_ctx[i]);
        }
    }
    for (i = 1; i < MAX_TEXTURES; i++) {
        if (d3d_tex[i].surf && (d3d_tex[i].pid == pid))
            d3d_tex[i].surf = NULL;
    }
    d3d7_forget(NULL, pid);
    surf_epoch++;
    world_moved();
}

void
ctx_leaving(DWORD pid, int ending)
{
    DWORD i;
    int   live = 0;

    for (i = 0; i < MAX_CONTEXTS; i++) {
        if ((d3d_ctx[i].used == CTX_LIVE) && (d3d_ctx[i].pid == pid)) {
            flight_dump(ending ? "a process leaving with its contexts" : "a process unloading the DLL with contexts live", pid,
                        &d3d_ctx[i], !ending);
            live = 1;
        }
    }
    if (!live)
        flight_leaving(pid, ending);
}

/* The DX7 DDK has Windows 98 drivers leave ContextDestroyAll out, so a
   process that ends without destroying its contexts (it crashed) may leave
   them for good: they would hold slots, and keep the pool's free chunks
   mapped. */
void
ctx_reap(DWORD pid)
{
    DWORD i;

    for (i = 0; i < MAX_CONTEXTS; i++) {
        const d3d_ctx_t *c = &d3d_ctx[i];

        if ((c->used == CTX_LIVE) && (!pid || (c->pid == pid)) && !process_alive(c->pid)) {
            dbg_event(DLL_TAG ": process %x ended without destroying its contexts", c->pid);
            process_gone(c->pid, "a process ended without destroying its contexts");
        }
    }
}

/* A context goes in its own process, or after its process ended: a
   handle of a process that ended may name a slot another context took
   since (ctx_reap). */
GUARDED_FIG(static, ContextDestroy, D3DHAL_CONTEXTDESTROYDATA, ddrval, d->dwhContext, 0)
{
    d3d_ctx_t *c = ctx_get(d->dwhContext);

    if (!c || ((c->pid != GetCurrentProcessId()) && process_alive(c->pid))) {
        d->ddrval = D3DHAL_CONTEXT_BAD;
        return DDHAL_DRIVER_HANDLED;
    }
    /* DDHELP's clean-up after a process died: what it last did. A
       DirectX 8 context of its own process ending ends a 3DMark test: the
       next device's creation gets the trace's budgets. */
    if (c->pid != GetCurrentProcessId())
        flight_dump("a context destroyed after its process ended", c->pid, c, 0);
    else if (c->dx8 && diag_audit8) {
        /* One that never drew (a test that gave up before its first draw,
           a device made to look): what it did. */
        if (!c->draws)
            flight_dump("a DirectX 8 context destroyed before it drew", c->pid, c, 1);
        trace_reset();
    }
    if (TRACE(TRACE_CONTEXT))
        dbg(DLL_TAG " trace: context %u destroyed (process %x, its own %u)", d->dwhContext, c->pid, c->pid == GetCurrentProcessId());
    ctx_free(c);
    d->ddrval = D3D_OK;
    return DDHAL_DRIVER_HANDLED;
}

GUARDED_FIG(static, ContextDestroyAll, D3DHAL_CONTEXTDESTROYALLDATA, ddrval, d->dwPID, 0)
{
    process_gone(d->dwPID, "ContextDestroyAll with contexts live");
    d->ddrval = D3D_OK;
    return DDHAL_DRIVER_HANDLED;
}

GUARDED(static, SceneCapture, D3DHAL_SCENECAPTUREDATA, ddrval)
{
    d->ddrval = D3D_OK;
    return DDHAL_DRIVER_HANDLED;
}

GUARDED(static, TextureCreate, D3DHAL_TEXTURECREATEDATA, ddrval)
{
    d3d_ctx_t *c = ctx_get(d->dwhContext);
    DWORD      i;

    /* A DirectX 7 runtime that still makes texture handles does so for an
       older interface's context: its stages name these handles. */
    if (c && c->dx7 && !c->legacy_tex) {
        c->legacy_tex = 1;
        world_moved(); /* its stages' handles mean other textures now */
    }
    for (i = 1; i < MAX_TEXTURES; i++) {
        if (!d3d_tex[i].surf) {
            d3d_tex[i].surf = d->lpDDS;
            d3d_tex[i].pid  = GetCurrentProcessId();
            surf_epoch++;
            world_moved();
            d->dwHandle     = i;
            d->ddrval       = D3D_OK;
            return DDHAL_DRIVER_HANDLED;
        }
    }
    d->ddrval = DDERR_OUTOFVIDEOMEMORY;
    return DDHAL_DRIVER_HANDLED;
}

GUARDED(static, TextureDestroy, D3DHAL_TEXTUREDESTROYDATA, ddrval)
{
    DWORD i;
    DWORD s;

    if ((d->dwHandle >= 1) && (d->dwHandle < MAX_TEXTURES))
        d3d_tex[d->dwHandle].surf = NULL;
    surf_epoch++;
    world_moved();
    for (i = 0; i < MAX_CONTEXTS; i++) {
        d3d_ctx[i].clean = 0;
        /* Only contexts whose stages name TextureCreate's handles: surface
           handles have numbers of their own. */
        if ((d3d_ctx[i].used != CTX_LIVE) || ctx_surface_handles(&d3d_ctx[i]))
            continue;
        for (s = 0; s < D3D_STAGES; s++) {
            if (d3d_ctx[i].tss[s][D3DTSS_TEXTUREMAP] == d->dwHandle)
                d3d_ctx[i].tss[s][D3DTSS_TEXTUREMAP] = 0;
        }
    }
    d->ddrval = D3D_OK;
    return DDHAL_DRIVER_HANDLED;
}

GUARDED(static, TextureSwap, D3DHAL_TEXTURESWAPDATA, ddrval)
{
    if ((d->dwHandle1 >= 1) && (d->dwHandle1 < MAX_TEXTURES) && (d->dwHandle2 >= 1) && (d->dwHandle2 < MAX_TEXTURES)) {
        d3d_tex_t t            = d3d_tex[d->dwHandle1];
        DWORD     i;

        d3d_tex[d->dwHandle1]  = d3d_tex[d->dwHandle2];
        d3d_tex[d->dwHandle2]  = t;
        surf_epoch++;
        world_moved();
        for (i = 0; i < MAX_CONTEXTS; i++)
            d3d_ctx[i].clean = 0;
    }
    d->ddrval = D3D_OK;
    return DDHAL_DRIVER_HANDLED;
}

GUARDED(static, TextureGetSurf, D3DHAL_TEXTUREGETSURFDATA, ddrval)
{
    d->lpDDS  = ((d->dwHandle >= 1) && (d->dwHandle < MAX_TEXTURES)) ? (DWORD) d3d_tex[d->dwHandle].surf : 0;
    d->ddrval = d->lpDDS ? D3D_OK : DDERR_INVALIDPARAMS;
    return DDHAL_DRIVER_HANDLED;
}

GUARDED_FIG(static, SetRenderTarget, D3DHAL_SETRENDERTARGETDATA, ddrval, d->dwhContext, d->lpDDS)
{
    d3d_ctx_t *c = ctx_get(d->dwhContext);

    if (!c) {
        d->ddrval = D3DHAL_CONTEXT_BAD;
        return DDHAL_DRIVER_HANDLED;
    }
    /* Interfaces, but the DirectX 7 interface's own context passes local
       objects (as the reference rasterizer reads them). */
    if (c->dx7 && (c->iface > 2)) {
        c->target = (LPSURF) d->lpDDS;
        c->zbuf   = (LPSURF) d->lpDDSZ;
    } else {
        c->target = d->lpDDS ? d->lpDDS->lpLcl : NULL;
        c->zbuf   = d->lpDDSZ ? d->lpDDSZ->lpLcl : NULL;
    }
    if (c->dx7) {
        c->h_target = (c->target && c->target->lpSurfMore) ? c->target->lpSurfMore->dwSurfaceHandle : 0;
        c->h_zbuf   = (c->zbuf && c->zbuf->lpSurfMore) ? c->zbuf->lpSurfMore->dwSurfaceHandle : 0;
        ctx_own_targets(c);
    }
    c->clean = 0;
    /* The older interfaces' SetRenderTarget invalidates the viewport;
       DirectX 7's keeps it. */
    if (!c->dx7 || (c->iface <= 2))
        ctx_viewport_whole(c);
    d->ddrval = D3D_OK;
    return DDHAL_DRIVER_HANDLED;
}

int
ctx_clear(d3d_ctx_t *c, DWORD flags, DWORD color, float depth, DWORD stencil, const D3DRECT *rects, DWORD n)
{
    DWORD gflags = 0;
    DWORD i;

    if (flags & D3DCLEAR_TARGET)
        gflags |= VGPU98_CLEAR_COLOR;
    if (flags & D3DCLEAR_ZBUFFER)
        gflags |= VGPU98_CLEAR_DEPTH;
    if (flags & D3DCLEAR_STENCIL)
        gflags |= VGPU98_CLEAR_STENCIL;
    if (!gflags || !n)
        return 1;
    switch (ctx_flush(c, 0)) {
        case -1:
            return 1; /* no target to clear */
        case 0:
            return 0;
        default:
            break;
    }
    for (i = 0; i < n;) {
        DWORD           k = (n - i > 1000) ? 1000 : n - i;
        DWORD           j;
        volatile DWORD *p = gpu_packet(VGPU98_OP_CLEAR, VGPU98_LEN_CLEAR + 2 * k);

        if (!p)
            return 0;
        pkt_put(&p, gflags);
        pkt_put(&p, color);
        pkt_putf(&p, depth);
        pkt_put(&p, stencil);
        pkt_put(&p, k);
        for (j = 0; j < k; j++) {
            const D3DRECT *r  = &rects[i + j];
            LONG           x1 = (r->x1 < 0) ? 0 : (r->x1 > 0xffff) ? 0xffff : r->x1;
            LONG           y1 = (r->y1 < 0) ? 0 : (r->y1 > 0xffff) ? 0xffff : r->y1;
            LONG           x2 = (r->x2 < x1) ? x1 : (r->x2 > 0xffff) ? 0xffff : r->x2;
            LONG           y2 = (r->y2 < y1) ? y1 : (r->y2 > 0xffff) ? 0xffff : r->y2;

            pkt_put(&p, VGPU98_XY(x1, y1));
            pkt_put(&p, VGPU98_XY(x2, y2));
        }
        i += k;
    }
    return 1;
}

/* Clear and Clear2 (DirectX 5 and 6): a batch of their own. */
static HRESULT
clear(d3d_ctx_t *c, DWORD flags, DWORD color, float depth, DWORD stencil, const D3DRECT *rects, DWORD n)
{
    HRESULT hr;

    if (!(flags & (D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)) || !n)
        return D3D_OK;
    if (c->stopped) {
        DIAG(diag.calls_dropped++);
        return D3D_OK;
    }
    if ((hr = gpu_begin()) != DD_OK)
        return hr;
    /* The restart in gpu_begin may have stopped this very context. */
    if (c->stopped) {
        gpu_release();
        DIAG(diag.calls_dropped++);
        return D3D_OK;
    }
    gpu_batch_owner(ctx_token(c));
    ctx_batch_begin();
    ctx_clear(c, flags, color, depth, stencil, rects, n);
    ctx_used(c, gpu_finish());
    gpu_release();
    return D3D_OK;
}

GUARDED_FIG(static, Clear, D3DHAL_CLEARDATA, ddrval, d->dwhContext, d->dwFlags)
{
    d3d_ctx_t *c = ctx_get(d->dwhContext);
    float      depth;

    if (!c) {
        d->ddrval = D3DHAL_CONTEXT_BAD;
        return DDHAL_DRIVER_HANDLED;
    }
    /* DirectX 5's clear gives depth as the buffer's raw value. */
    {
        gsurf_t gz;
        DWORD   max = 0xffff;

        if (c->zbuf && surf_describe(c->zbuf, &gz) && (gz.format != VGPU98_FMT_D16))
            max = 0xffffff;
        depth = (float) ((d->dwFillDepth > max) ? max : d->dwFillDepth) / (float) max;
    }
    d->ddrval = clear(c, d->dwFlags, d->dwFillColor, depth, 0, d->lpRects, d->dwNumRects);
    return DDHAL_DRIVER_HANDLED;
}

GUARDED_FIG(static, Clear2, D3DHAL_CLEAR2DATA, ddrval, d->dwhContext, d->dwFlags)
{
    d3d_ctx_t *c = ctx_get(d->dwhContext);

    if (!c) {
        d->ddrval = D3DHAL_CONTEXT_BAD;
        return DDHAL_DRIVER_HANDLED;
    }
    d->ddrval = clear(c, d->dwFlags, d->dwFillColor, d->dvFillDepth, d->dwFillStencil, d->lpRects, d->dwNumRects);
    return DDHAL_DRIVER_HANDLED;
}

/* A stage argument the caps advertise: DirectX 7's selectors, and TEMP for
   a DirectX 8 context. */
static int
arg_valid(const d3d_ctx_t *c, DWORD a)
{
    const DWORD sel = a & VGPU98_TA_SELECTMASK;

    return (sel <= VGPU98_TA_SPECULAR) || (c->dx8 && (sel == VGPU98_TA_TEMP));
}

/* One pass for every combination the caps advertise; the operations and
   arguments the GPU lacks are named. With a pixel shader bound the stages'
   operations and arguments are not used (its program draws): one pass. */
GUARDED(static, ValidateTextureStageState, D3DHAL_VALIDATETEXTURESTAGESTATEDATA, ddrval)
{
    d3d_ctx_t *c = ctx_get(d->dwhContext);
    DWORD      s;

    if (!c) {
        d->ddrval = D3DHAL_CONTEXT_BAD;
        return DDHAL_DRIVER_HANDLED;
    }
    d->dwNumPasses = 1;
    d->ddrval      = D3D_OK;
    for (s = 0; !c->ps_on && (s < D3D_STAGES); s++) {
        const DWORD *t  = c->tss[s];
        DWORD        op = t[VGPU98_TSS_COLOROP];

        if (op == VGPU98_TOP_DISABLE)
            break;
        if ((op < 1) || (op > top_last(c))) {
            d->ddrval = D3DERR_UNSUPPORTEDCOLOROPERATION;
            break;
        }
        if (!arg_valid(c, t[VGPU98_TSS_COLORARG1]) || !arg_valid(c, t[VGPU98_TSS_COLORARG2]) ||
            (((op == VGPU98_TOP_MULTIPLYADD) || (op == VGPU98_TOP_LERP)) && !arg_valid(c, t[VGPU98_TSS_COLORARG0]))) {
            d->ddrval = D3DERR_UNSUPPORTEDCOLORARG;
            break;
        }
        op = t[VGPU98_TSS_ALPHAOP];
        if ((op != TOP_LEGACY_ALPHAOVR) && ((op < 1) || (op > top_last(c)) || colour_only_op(op))) {
            d->ddrval = D3DERR_UNSUPPORTEDALPHAOPERATION;
            break;
        }
        if (!arg_valid(c, t[VGPU98_TSS_ALPHAARG1]) || !arg_valid(c, t[VGPU98_TSS_ALPHAARG2]) ||
            (((op == VGPU98_TOP_MULTIPLYADD) || (op == VGPU98_TOP_LERP)) && !arg_valid(c, t[VGPU98_TSS_ALPHAARG0]))) {
            d->ddrval = D3DERR_UNSUPPORTEDALPHAARG;
            break;
        }
        /* DirectX 8's RESULTARG: current or the temporary register. */
        if (c->dx8 && (t[VGPU98_TSS_RESULTARG] != VGPU98_TA_CURRENT) && (t[VGPU98_TSS_RESULTARG] != VGPU98_TA_TEMP)) {
            d->ddrval = D3DERR_UNSUPPORTEDCOLORARG;
            break;
        }
    }
    return DDHAL_DRIVER_HANDLED;
}

DWORD STDCALL DrawPrimitives2(D3DHAL_DRAWPRIMITIVES2DATA *d);

D3DHAL_CALLBACKS  d3d_callbacks;
D3DHAL_CALLBACKS2 d3d_callbacks2;
D3DHAL_CALLBACKS3 d3d_callbacks3;

void
d3d_init(void)
{
    gstate_map();
    memset(&d3d_callbacks, 0, sizeof(d3d_callbacks));
    d3d_callbacks.dwSize            = sizeof(d3d_callbacks);
    d3d_callbacks.ContextCreate     = ContextCreate;
    d3d_callbacks.ContextDestroy    = ContextDestroy;
    d3d_callbacks.ContextDestroyAll = ContextDestroyAll;
    d3d_callbacks.SceneCapture      = SceneCapture;
    d3d_callbacks.TextureCreate     = TextureCreate;
    d3d_callbacks.TextureDestroy    = TextureDestroy;
    d3d_callbacks.TextureSwap       = TextureSwap;
    d3d_callbacks.TextureGetSurf    = TextureGetSurf;

    memset(&d3d_callbacks2, 0, sizeof(d3d_callbacks2));
    d3d_callbacks2.dwSize          = sizeof(d3d_callbacks2);
    d3d_callbacks2.dwFlags         = D3DHAL2_CB32_SETRENDERTARGET | D3DHAL2_CB32_CLEAR;
    d3d_callbacks2.SetRenderTarget = SetRenderTarget;
    d3d_callbacks2.Clear           = Clear;

    memset(&d3d_callbacks3, 0, sizeof(d3d_callbacks3));
    d3d_callbacks3.dwSize  = sizeof(d3d_callbacks3);
    d3d_callbacks3.dwFlags = D3DHAL3_CB32_CLEAR2 | D3DHAL3_CB32_VALIDATETEXTURESTAGESTATE | D3DHAL3_CB32_DRAWPRIMITIVES2;
    d3d_callbacks3.Clear2                    = Clear2;
    d3d_callbacks3.ValidateTextureStageState = ValidateTextureStageState;
    d3d_callbacks3.DrawPrimitives2           = DrawPrimitives2;
}

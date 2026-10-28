/*
 * vgpu98dx.dll: the DirectX 8 driver interface (spec/directx-hal.md, "The
 * DirectX 8 interface"): GetDriverInfo2 and its answers (D3DCAPS8, the
 * format list, the runtime's version), and the vertex shaders of DirectX 8
 * contexts (CREATEVERTEXSHADER's declaration tokens as the GPU's DECLARE
 * elements; with PROFILE.PROGRAMS a function becomes the GPU's program,
 * xlate.c, its declaration INPUT elements and its constants loaded at every
 * SETVERTEXSHADER).
 *
 * Only with the GPU's PROFILE.FIXED8 (command set 2.1): without it the DLL
 * never answers GetDriverInfo2, the 16-bit driver never sets
 * DDHALINFO_GETDRIVERINFO2, and the runtime drives the card through the
 * DirectX 7 interface, as before. The caps are a DirectX 8 card's in fixed
 * function, GeForce3-shaped where DirectX 8 leaves the numbers to the card,
 * and claim only what the GPU does exactly (spec/d3d-caps.md).
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3d.h"
#include "vdecl.h"
#include "xlate.h"

int d3d8_on;
int d3d8_fmt22;

static D3DCAPS8      caps8;
static DDPIXELFORMAT formats8[32];
static DWORD         n_formats8;
/* DXVERSION's last, per process (a DirectX 9 program and a DirectX 8 one
   can run at once): the application runtime's DD_RUNTIME_VERSION. */
#define DXV_PROCS 8

static struct {
    DWORD pid;
    DWORD version;
} dxv[DXV_PROCS];
static DWORD dxv_next;

DWORD
d3d8_runtime(void)
{
    const DWORD pid = GetCurrentProcessId();
    DWORD       i;

    for (i = 0; i < DXV_PROCS; i++) {
        if (dxv[i].pid == pid)
            return dxv[i].version;
    }
    return 0;
}

/* Records a version for the calling process; 1 when it is new for it. */
static int
dxv_set(DWORD version)
{
    const DWORD pid = GetCurrentProcessId();
    DWORD       i;

    for (i = 0; (i < DXV_PROCS) && (dxv[i].pid != pid); i++)
        ;
    if (i == DXV_PROCS) {
        i = dxv_next++ % DXV_PROCS;
        dxv[i].pid     = pid;
        dxv[i].version = 0;
    }
    if (dxv[i].version == version)
        return 0;
    dxv[i].version = version;
    return 1;
}

/* --- The caps and the format list ------------------------------------------------ */

static void
format8(DWORD code, DWORD ops)
{
    DDPIXELFORMAT *pf = &formats8[n_formats8++];

    memset(pf, 0, sizeof(*pf));
    pf->dwSize     = sizeof(DDPIXELFORMAT);
    pf->dwFlags    = DDPF_D3DFORMAT;
    pf->dwFourCC   = code;
    pf->dwRBitMask = ops; /* dwOperations */
}

/* After caps_init: DirectX 7's caps, then what DirectX 8 adds. */
void
d3d8_init(DWORD profile)
{
    const DWORD        need = VGPU98_PROFILE_RENDER3D | VGPU98_PROFILE_VERTEX | VGPU98_PROFILE_FIXED8;
    const D3DPRIMCAPS *tri  = &caps_global.hwCaps.dpcTriCaps;
    D3DCAPS8          *c    = &caps8;
    /* Every texture format is a 2D texture and a cube map; the volume
       formats are those but P8 and DXT (spec/gpu-commands.md, TEXTURE). */
    const DWORD tex = D3DFORMAT_OP_TEXTURE | D3DFORMAT_OP_CUBETEXTURE;
    const DWORD vol = D3DFORMAT_OP_VOLUMETEXTURE;
    const DWORD rt  = D3DFORMAT_OP_OFFSCREEN_RENDERTARGET;
    const DWORD z   = D3DFORMAT_OP_ZSTENCIL | D3DFORMAT_OP_ZSTENCIL_WITH_ARBITRARY_COLOR_DEPTH;
    DWORD       i;

    d3d8_on       = (profile & need) == need;
    d3d8_programs = d3d8_on && (profile & VGPU98_PROFILE_PROGRAMS);
    d3d8_fmt22    = d3d8_programs; /* command set 2.2's formats: a GPU without PROGRAMS is exactly 2.1 */
    sb->hal_flags = d3d8_on ? DDHALINFO_GETDRIVERINFO2 : 0;
    memset(c, 0, sizeof(*c));
    n_formats8 = 0;
    if (!d3d8_on)
        return;

    /* DeviceType, AdapterOrdinal, Caps3 and PresentationIntervals are the
       runtime's to fill. */
    c->Caps    = D3DCAPS_READ_SCANLINE;
    c->Caps2   = D3DCAPS2_CANRENDERWINDOWED;
    c->DevCaps = caps_global.hwCaps.dwDevCaps & ~D3DDEVCAPS_FLOATTLVERTEX; /* not a DirectX 8 bit */
    if (pool_on)
        c->DevCaps |= D3DDEVCAPS_HWVERTEXBUFFER | D3DDEVCAPS_HWINDEXBUFFER;
    c->PrimitiveMiscCaps = tri->dwMiscCaps | D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_TSSARGTEMP | D3DPMISCCAPS_BLENDOP;
    c->RasterCaps        = tri->dwRasterCaps & ~D3DPRASTERCAPS_SUBPIXEL; /* gone from DirectX 8 */
    c->ZCmpCaps          = tri->dwZCmpCaps;
    c->SrcBlendCaps      = tri->dwSrcBlendCaps;
    c->DestBlendCaps     = tri->dwDestBlendCaps;
    c->AlphaCmpCaps      = tri->dwAlphaCmpCaps;
    c->ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB | D3DPSHADECAPS_SPECULARGOURAUDRGB | D3DPSHADECAPS_ALPHAGOURAUDBLEND |
                   D3DPSHADECAPS_FOGGOURAUD;
    c->TextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_POW2 | D3DPTEXTURECAPS_ALPHA |
                     D3DPTEXTURECAPS_ALPHAPALETTE | D3DPTEXTURECAPS_NONPOW2CONDITIONAL | D3DPTEXTURECAPS_PROJECTED |
                     D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_VOLUMEMAP | D3DPTEXTURECAPS_MIPMAP |
                     D3DPTEXTURECAPS_MIPVOLUMEMAP | D3DPTEXTURECAPS_MIPCUBEMAP | D3DPTEXTURECAPS_CUBEMAP_POW2 |
                     D3DPTEXTURECAPS_VOLUMEMAP_POW2;
    c->TextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MINFANISOTROPIC |
                           D3DPTFILTERCAPS_MIPFPOINT | D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MAGFPOINT |
                           D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFANISOTROPIC;
    c->CubeTextureFilterCaps   = c->TextureFilterCaps;
    c->VolumeTextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MIPFPOINT |
                                 D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MAGFLINEAR;
    c->TextureAddressCaps       = tri->dwTextureAddressCaps;
    c->VolumeTextureAddressCaps = tri->dwTextureAddressCaps;
    c->LineCaps = D3DLINECAPS_TEXTURE | D3DLINECAPS_ZTEST | D3DLINECAPS_BLEND | D3DLINECAPS_ALPHACMP | D3DLINECAPS_FOG;
    c->MaxTextureWidth          = caps_extended.dwMaxTextureWidth;
    c->MaxTextureHeight         = caps_extended.dwMaxTextureHeight;
    c->MaxVolumeExtent          = VGPU98_TEX_VOLUME_MAX;
    c->MaxTextureRepeat         = caps_extended.dwMaxTextureRepeat;
    c->MaxTextureAspectRatio    = caps_extended.dwMaxTextureAspectRatio;
    c->MaxAnisotropy            = caps_extended.dwMaxAnisotropy;
    c->MaxVertexW               = caps_extended.dvMaxVertexW;
    c->GuardBandLeft            = caps_extended.dvGuardBandLeft;
    c->GuardBandTop             = caps_extended.dvGuardBandTop;
    c->GuardBandRight           = caps_extended.dvGuardBandRight;
    c->GuardBandBottom          = caps_extended.dvGuardBandBottom;
    c->ExtentsAdjust            = caps_extended.dvExtentsAdjust;
    c->StencilCaps              = caps_extended.dwStencilCaps;
    c->FVFCaps                  = caps_extended.dwFVFCaps | D3DFVFCAPS_PSIZE;
    c->TextureOpCaps            = D3DTEXOPCAPS_ALL8;
    c->MaxTextureBlendStages    = caps_extended.wMaxTextureBlendStages;
    c->MaxSimultaneousTextures  = caps_extended.wMaxSimultaneousTextures;
    c->VertexProcessingCaps     = (caps_extended.dwVertexProcessingCaps & ~D3DVTXPCAPS_VERTEXFOG) | D3DVTXPCAPS_TWEENING;
    c->MaxActiveLights          = caps_extended.dwMaxActiveLights;
    c->MaxUserClipPlanes        = caps_extended.wMaxUserClipPlanes;
    c->MaxVertexBlendMatrices   = caps_extended.wMaxVertexBlendMatrices;
    c->MaxVertexBlendMatrixIndex = D3D_PALETTE - 1;
    c->MaxPointSize             = (float) VGPU98_POINT_SIZE_MAX;
    /* A DRAW takes 1M vertices or indices: a triangle list of this many
       triangles fits, so no draw is ever split. */
    c->MaxPrimitiveCount   = 0x55555;
    c->MaxVertexIndex      = 0xFFFFF;
    c->MaxStreams          = D3D_STREAMS;
    c->MaxStreamStride     = D3D8_MAX_STRIDE;
    /* With PROFILE.PROGRAMS the GPU's programs draw vertex shaders 1.1 with
       a Radeon 8500's 192 constants, and pixel shaders 1.0-1.4 with its range
       of 8 (texcrd's needs it, spec/d3d-caps.md); without
       ("DirectX 8 shaders" Off), none: the runtime runs vertex shaders
       itself. */
    c->VertexShaderVersion  = d3d8_programs ? D3DVS_VERSION_1_1 : D3DVS_VERSION_0_0;
    c->MaxVertexShaderConst = d3d8_programs ? D3D_VS_CONSTS : 0;
    c->PixelShaderVersion  = d3d8_programs ? D3DPS_VERSION_1_4 : D3DPS_VERSION_0_0;
    c->MaxPixelShaderValue = d3d8_programs ? 8.0f : 0.0f;

    /* The display mode formats: one per depth (DirectX 8 takes one 16-bit
       one), with 3D in them. */
    format8(D3DFMT_X8R8G8B8, tex | vol | rt | D3DFORMAT_OP_SAME_FORMAT_RENDERTARGET | D3DFORMAT_OP_DISPLAYMODE |
                                 D3DFORMAT_OP_3DACCELERATION);
    format8(D3DFMT_R5G6B5, tex | vol | rt | D3DFORMAT_OP_SAME_FORMAT_RENDERTARGET | D3DFORMAT_OP_DISPLAYMODE |
                               D3DFORMAT_OP_3DACCELERATION);
    format8(D3DFMT_A8R8G8B8, tex | vol | rt | D3DFORMAT_OP_SAME_FORMAT_UP_TO_ALPHA_RENDERTARGET);
    format8(D3DFMT_X1R5G5B5, tex | vol | rt | D3DFORMAT_OP_SAME_FORMAT_RENDERTARGET);
    format8(D3DFMT_A1R5G5B5, tex | vol | rt);
    format8(D3DFMT_A4R4G4B4, tex | vol);
    format8(D3DFMT_X4R4G4B4, tex | vol);
    format8(D3DFMT_A8, tex | vol);
    format8(D3DFMT_P8, tex);
    format8(D3DFMT_L8, tex | vol);
    format8(D3DFMT_A8L8, tex | vol);
    format8(D3DFMT_A4L4, tex | vol);
    format8(D3DFMT_V8U8, tex | vol);
    format8(D3DFMT_L6V5U5, tex | vol);
    format8(D3DFMT_X8L8V8U8, tex | vol);
    format8(D3DFMT_Q8W8V8U8, tex | vol);
    /* Command set 2.2's: the 16-bit pair formats and the 10-bit signed one. */
    if (d3d8_fmt22) {
        format8(D3DFMT_V16U16, tex | vol);
        format8(D3DFMT_G16R16, tex | vol);
        format8(D3DFMT_A2W10V10U10, tex | vol);
    }
    for (i = '1'; i <= '5'; i++)
        format8(MAKEFOURCC('D', 'X', 'T', i), tex);
    /* D16 is lockable, its bits as the API's D3DFMT_D16_LOCKABLE has them. */
    format8(D3DFMT_D16_LOCKABLE, z);
    format8(D3DFMT_D24X8, z);
    format8(D3DFMT_D24S8, z);
}

DWORD
d3d8_ps_range(void)
{
    union {
        float f;
        DWORD u;
    } v;

    v.f = (caps8.MaxPixelShaderValue >= 1.0f) ? caps8.MaxPixelShaderValue : 1.0f;
    return v.u;
}

/* --- GetDriverInfo2 -------------------------------------------------------------- */

int
d3d8_get_driver_info2(DDHAL_GETDRIVERINFODATA *d)
{
    DD_GETDRIVERINFO2DATA *g = d->lpvData;
    DWORD                  expected;
    DWORD                  type;
    DWORD                  fa = 0; /* the request's figures (the trace's) */
    DWORD                  fb = 0;

    if (!g || (g->dwMagic != D3DGDI2_MAGIC))
        return 0; /* a stereo-mode query: not answered */
    d->ddRVal       = DDERR_CURRENTLYNOTAVAIL;
    d->dwActualSize = 0;
    if (!d3d8_on)
        return 1;
    expected = g->dwExpectedSize;
    type     = g->dwType;
    /* What the request says before the answer overwrites it: the
       versions DirectX 9's runtime puts in the format requests. */
    if (type == D3DGDI2_TYPE_GETFORMATCOUNT)
        fa = ((const DD_GETFORMATCOUNTDATA *) d->lpvData)->dwReserved;
    else if (type == D3DGDI2_TYPE_GETFORMAT) {
        fa = ((const DD_GETFORMATDATA *) d->lpvData)->dwFormatIndex;
        fb = ((const DD_GETFORMATDATA *) d->lpvData)->format.dwSize;
    } else if (type == D3DGDI2_TYPE_DXVERSION)
        fa = ((const DD_DXVERSION *) d->lpvData)->dwDXVersion;
    switch (type) {
        case D3DGDI2_TYPE_GETD3DCAPS8: {
            /* The answer overwrites the request's header. */
            const DWORD n = (expected < sizeof(caps8)) ? expected : sizeof(caps8);
            static int  told;

            memcpy(d->lpvData, &caps8, n);
            d->dwActualSize = n;
            d->ddRVal       = DD_OK;
            if (!told) {
                told = 1;
                dbg(DLL_TAG ": the DirectX 8 driver interface (D3DCAPS8 asked for, %u bytes)", n);
            }
            break;
        }
        case D3DGDI2_TYPE_GETFORMATCOUNT: {
            DD_GETFORMATCOUNTDATA *f = d->lpvData;

            f->dwFormatCount = n_formats8;
            d->dwActualSize  = sizeof(*f);
            d->ddRVal        = DD_OK;
            break;
        }
        case D3DGDI2_TYPE_GETFORMAT: {
            DD_GETFORMATDATA *f = d->lpvData;

            if (f->dwFormatIndex < n_formats8) {
                /* The whole DDPIXELFORMAT, whatever its dwSize held (a DirectX
                   9 runtime puts its version there). */
                f->format       = formats8[f->dwFormatIndex];
                d->dwActualSize = sizeof(*f);
                d->ddRVal       = DD_OK;
            }
            break;
        }
        case D3DGDI2_TYPE_DXVERSION: {
            const DD_DXVERSION *v = d->lpvData;

            if (dxv_set(v->dwDXVersion))
                dbg_event(DLL_TAG ": a DirectX runtime of version %x in process %x", v->dwDXVersion, GetCurrentProcessId());
            d->dwActualSize = sizeof(*v);
            d->ddRVal       = DD_OK;
            break;
        }
        default:
            /* DirectX 9's requests: not available from a DirectX 8 driver. */
            break;
    }
    flight_figures(d->guidInfo.Data1, type);
    if (TRACE(TRACE_QUERY))
        dbg(DLL_TAG " trace: GetDriverInfo2 type %x, %u bytes expected (%x %x): %s, %u bytes", type, expected, fa, fb,
            (d->ddRVal == DD_OK) ? "answered" : "not available", d->dwActualSize);
    return 1;
}

/* --- Vertex shaders -------------------------------------------------------------- */

static DWORD
rd32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((DWORD) p[3] << 24);
}

/* A shader with a function: its declaration as INPUT elements, its
   function (without its comments) translated into the GPU's program, the
   constants its declaration (and any def) loads. 0 if any of it cannot be
   had, with why named (and counted); one whose declaration reaches past
   MaxStreamStride is made unusable instead, as a fixed-function
   declaration that does (it could never draw). Each distinct one named
   once a session with Diagnostics (its instructions too with shader
   capture, and its program with the capture of programs). */
static int
vs_program(const d3d_ctx_t *c, d3d_vs_t *v, const uint8_t *decl, DWORD decl_bytes, const uint8_t *code, DWORD code_bytes)
{
    const DWORD n   = code_bytes / 4;
    const int   pr  = vd_parse(decl, decl_bytes, 1, v->decl, &v->n, &v->streams);
    const char *why = NULL;
    DWORD      *tok = NULL;
    xl_out_t   *o   = NULL;
    DWORD       at  = 0;
    DWORD       h   = 0x811c9dc5u;
    DWORD       kept;
    DWORD       nd;
    DWORD       nc;
    DWORD       i;

    if (pr == VD_STRIDE) {
        v->n        = 0;
        v->streams  = 0;
        v->unusable = VS_TOKENS;
        return 1;
    }
    if (!n || (pr != VD_OK)) {
        why = n ? "its declaration" : "no function";
        goto out;
    }
    if (!(tok = heap_alloc(n * sizeof(DWORD))) || !(o = prog_scratch())) {
        why = "no heap room";
        goto out;
    }
    /* The hash names the declaration and the function: one function under
       two declarations is two shaders to the log. */
    for (i = 0; i < decl_bytes / 4; i++)
        h = (h ^ rd32(decl + 4 * i)) * 16777619u;
    for (i = 0; i < n; i++) {
        tok[i] = rd32(code + 4 * i);
        h      = (h ^ tok[i]) * 16777619u;
    }
    kept = xl_strip(tok, n);
    if (!xl_vertex(tok, kept, o)) {
        why = xl_why(o->why);
        at  = o->at;
        goto out;
    }
    if (!prog_make(&v->prog, o, 0)) {
        why = "no program id or heap room";
        goto out;
    }
    /* A declaration that loads no register still needs an element (the
       GPU's DECLARE has one at least): stream 0's first dword into an input
       register the function does not read. */
    if (!v->n) {
        DWORD r = VGPU98_PROG_INPUTS - 1;

        while (r && ((o->info.inputs >> r) & 1))
            r--;
        v->decl[0] = VGPU98_DECL(0, VGPU98_DT_FLOAT1, VGPU98_DU_INPUT, r);
        v->decl[1] = 0;
        v->n       = 1;
        v->streams = 1;
    }
    nd = vd_consts(decl, decl_bytes, NULL);
    nc = nd + o->n_def;
    if (nc && !(v->consts = heap_alloc(5 * nc * sizeof(DWORD)))) {
        prog_free(&v->prog);
        why = "no heap room";
        goto out;
    }
    vd_consts(decl, decl_bytes, v->consts);
    for (i = 0; i < o->n_def; i++)
        memcpy(v->consts + 5 * (nd + i), o->def[i], 5 * sizeof(DWORD));
    v->n_consts = nc;
    v->tokens   = tok;
    v->n_tokens = kept;
    v->n_in     = o->n_in;
    v->hash     = h;
    v->inputs   = o->info.inputs;
    v->outputs  = o->info.outputs;
    tok         = NULL;
out:
    heap_free(tok);
    heap_free(o);
    if (why) {
        DIAG(diag.vs_refused++);
        d3d8_shader_refused(c, v->handle, (n && code) ? rd32(code) : 0, why, at);
        return 0;
    }
    return 1;
}

/* A shader with a program, once stored: counted, and each distinct one
   named once a session with Diagnostics (its instructions too with shader
   capture, and its program with the capture of programs). */
static void
vs_log(const d3d_ctx_t *c, const d3d_vs_t *v)
{
    if (!v->prog.id)
        return;
    DIAG(diag.vs_made++);
    if (!d3d8_shader_once(v->hash))
        return;
    dbg(DLL_TAG " shader: context %u handle %x: vs.%u.%u, %u instructions, hash %x; its program %u instructions, outputs %x",
        ctx_handle(c), v->handle, (v->tokens[0] >> 8) & 0xFF, v->tokens[0] & 0xFF, v->n_in, v->hash, v->prog.n, v->outputs);
    dbg(DLL_TAG " shader %x: declaration %u elements over streams %x, inputs read %x, %u constants loaded at each set", v->hash,
        v->n, v->streams, v->inputs, v->n_consts);
    if (diag_shaders)
        d3d8_shader_text(v->hash, v->tokens, v->n_tokens, 1);
    if (diag_shaders == 2)
        d3d8_prog_log(v->hash, &v->prog);
}

static void
vs_gone(d3d_vs_t *v)
{
    if (v->drawn && d3d8_shader_gone_line(v->hash))
        dbg(DLL_TAG " shader %x: %u draws, the handle %x gone", v->hash, v->drawn, v->handle);
    prog_free(&v->prog);
    heap_free(v->consts);
    heap_free(v->tokens);
    v->tokens   = NULL;
    v->n_tokens = 0;
    v->consts   = NULL;
    v->n_consts = 0;
}

int
d3d8_vs_create(d3d_ctx_t *c, DWORD handle, const uint8_t *decl, DWORD decl_bytes, const uint8_t *code, DWORD code_bytes)
{
    d3d_vs_t *slot = d3d8_vs_find(c, handle);
    d3d_vs_t  v;

    memset(&v, 0, sizeof(v));
    v.handle = handle;
    if (code_bytes && d3d8_programs) {
        if (!vs_program(c, &v, decl, decl_bytes, code, code_bytes))
            return -1;
    } else if (code_bytes)
        v.unusable = VS_FUNCTION;
    else if (vd_parse(decl, decl_bytes, 0, v.decl, &v.n, &v.streams) != VD_OK) {
        v.n        = 0;
        v.streams  = 0;
        v.unusable = VS_TOKENS;
    }
    if (slot) {
        vs_gone(slot);
        *slot = v;
        vs_log(c, slot);
        return 1;
    }
    if (c->n_shaders == c->cap_shaders) {
        const DWORD cap = c->cap_shaders ? 2 * c->cap_shaders : 8;
        d3d_vs_t   *t   = heap_grow(c->shaders, c->n_shaders * sizeof(d3d_vs_t), cap * sizeof(d3d_vs_t));

        if (!t) {
            /* With programs the call fails, as for any shader not made. */
            if (v.prog.id) {
                DIAG(diag.vs_refused++);
                d3d8_shader_refused(c, handle, v.n_tokens ? v.tokens[0] : 0, "no heap room", 0);
            }
            vs_gone(&v);
            return (code_bytes && d3d8_programs) ? -1 : 0;
        }
        c->shaders     = t;
        c->cap_shaders = cap;
    }
    c->shaders[c->n_shaders++] = v;
    vs_log(c, &c->shaders[c->n_shaders - 1]);
    return 1;
}

d3d_vs_t *
d3d8_vs_find(const d3d_ctx_t *c, DWORD handle)
{
    DWORD i;

    for (i = 0; i < c->n_shaders; i++) {
        if (c->shaders[i].handle == handle)
            return &c->shaders[i];
    }
    return NULL;
}

void
d3d8_vs_delete(d3d_ctx_t *c, DWORD handle)
{
    DWORD i;

    for (i = 0; i < c->n_shaders; i++) {
        if (c->shaders[i].handle == handle) {
            DIAG(diag.vs_deleted += c->shaders[i].prog.id != 0);
            vs_gone(&c->shaders[i]);
            c->shaders[i] = c->shaders[--c->n_shaders];
            return;
        }
    }
}

void
d3d8_vs_forget(d3d_ctx_t *c)
{
    DWORD i;

    for (i = 0; i < c->n_shaders; i++)
        vs_gone(&c->shaders[i]);
    heap_free(c->shaders);
    c->shaders     = NULL;
    c->n_shaders   = 0;
    c->cap_shaders = 0;
    heap_free(c->vconst);
    c->vconst    = NULL;
    c->vconst_hi = 0;
    memset(c->vconst_dirty, 0, sizeof(c->vconst_dirty));
}

/* --- Vertex shader constants ---------------------------------------------------- */

/* One register's four values (raw floats) into the context's constants. */
static void
vconst_put(d3d_ctx_t *c, DWORD reg, const DWORD *v)
{
    if (reg >= D3D_VS_CONSTS)
        return;
    if (!c->vconst) {
        if (!(c->vconst = heap_alloc(D3D_VS_CONSTS * sizeof(c->vconst[0]))))
            return;
        memset(c->vconst, 0, D3D_VS_CONSTS * sizeof(c->vconst[0]));
    }
    /* A register set again to its value is no change (the GPU holds it, or
       it is marked still). */
    if ((reg < c->vconst_hi) && !dwords_differ(c->vconst[reg], v, 4))
        return;
    memcpy(c->vconst[reg], v, sizeof(c->vconst[reg]));
    c->vconst_dirty[reg / 32] |= 1u << (reg % 32);
    if (reg >= c->vconst_hi)
        c->vconst_hi = reg + 1;
}

void
d3d8_vs_const(d3d_ctx_t *c, DWORD reg, DWORD count, const uint8_t *values)
{
    DWORD i;
    DWORD k;

    for (i = 0; (i < count) && (reg < D3D_VS_CONSTS) && (i < D3D_VS_CONSTS - reg); i++) {
        DWORD v[4];

        for (k = 0; k < 4; k++)
            v[k] = rd32(values + 16 * i + 4 * k);
        vconst_put(c, reg + i, v);
    }
}

/* The DDK's rule: a shader's own constants load whenever it is set, the
   others keeping their values. */
void
d3d8_vs_set(d3d_ctx_t *c, DWORD handle)
{
    const d3d_vs_t *v;
    DWORD           i;

    c->vs = handle;
    if (!(handle & 1) || !(v = d3d8_vs_find(c, handle)))
        return;
    for (i = 0; i < v->n_consts; i++)
        vconst_put(c, v->consts[5 * i], v->consts + 5 * i + 1);
}

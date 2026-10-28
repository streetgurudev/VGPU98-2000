/*
 * The Glide core: textures.
 *
 * Texture memory is the device's, a session's own: each TMU's address
 * space, as a Voodoo3 reports it, held on the host. The DLL computes what
 * a Voodoo3's Glide computes (sizes, level offsets, the TMUs' ranges) with
 * the device's own arithmetic, sends downloads as TEXDATA, palettes as
 * PALETTE, and a texture's source and settings as the TMUs' registers.
 *
 * Levels and aspects are Glide 3's numbers here (log2): the Glide 2 DLL
 * turns its own into them.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "glcore.h"
#include "vid_vgpu98_glcpu.h"

#define BOARD_BYTES (16u << 20) /* our Voodoo3's memory */
#define FIFO_BYTES  (256u << 10)
#define TMU_BEFORE  (2u << 20)  /* each TMU's memory before a window opens */

/* The window's buffers in a Voodoo3's memory: rows of 64-pixel tiles, 4 KB
   pages. */
DWORD
glc_fb_bytes(void)
{
    DWORD row, buf;

    if (!glc_is_open())
        return 0;
    row = ((glc_width() + 63) & ~63u) * 2;
    buf = (row * glc_height() + 4095) & ~4095u;
    return buf * (DWORD) (glc_color_buffers() + glc_aux_buffers());
}

/* Each TMU's memory now: before a window, 2 MB; with one, the board's
   memory less the buffers and the command FIFO, halved, as a Voodoo3's
   Glide finds it. */
DWORD
glc_tmu_bytes(void)
{
    DWORD total, tex;

    if (!glc_is_open())
        return TMU_BEFORE;
    total = glc_fb_bytes();
    tex   = (total + FIFO_BYTES < BOARD_BYTES) ? BOARD_BYTES - total - FIFO_BYTES : 0;
    tex   = (tex / 2) & ~15u;
    return (tex > VGPU98_GL_TMU_BYTES) ? VGPU98_GL_TMU_BYTES : tex;
}

static int
tmu_ok(FxI32 tmu)
{
    return (tmu == 0) || (tmu == 1);
}

static int
info_ok(const glc_tex_t *t)
{
    return (t->format >= 0) && (t->format <= 0xf) && (t->format != 0x7) && (t->format != 0xf) && (t->small >= 0) &&
           (t->large <= 8) && (t->small <= t->large) && (t->aspect >= -3) && (t->aspect <= 3) && (t->evenodd >= 1) &&
           (t->evenodd <= 3);
}

static void
as_cpu(const glc_tex_t *t, vgpu98_gltex_t *c)
{
    c->format  = t->format;
    c->small   = t->small;
    c->large   = t->large;
    c->aspect  = t->aspect;
    c->evenodd = t->evenodd;
}

DWORD
glc_tex_mem_required(const glc_tex_t *t)
{
    vgpu98_gltex_t c;

    if (!info_ok(t))
        return 0;
    as_cpu(t, &c);
    return vgpu98_gl_mem_required(&c);
}

/* TEXDATA: bytes at an address of a TMU's memory, in packets the ring
   takes. */
static void
send(FxI32 tmu, DWORD address, const uint8_t *data, DWORD bytes)
{
    const DWORD max = VGPU98_GL_PACKET_BYTES - 64;

    while (bytes) {
        const DWORD n = (bytes > max) ? max : bytes;
        DWORD      *p = glc_cmd(VGPU98_GL_TEXDATA, VGPU98_GL_LEN_TEXDATA + (n + 3) / 4);

        if (!p)
            return;
        p[0]          = (DWORD) tmu;
        p[1]          = address;
        p[2]          = n;
        p[3 + (n - 1) / 4] = 0; /* the last dword's padding */
        memcpy(&p[3], data, n);
        glc_count_bytes(GLC_DOWNLOAD_BYTES, n);
        address += n;
        data += n;
        bytes -= n;
    }
}

/* Rows [first, last] of level `lod` of the texture at `start`, its data
   from `data` (the first row given). A level the texture does not hold is
   skipped, as a Voodoo3's Glide skips it. */
void
glc_tex_download(FxI32 tmu, DWORD start, const glc_tex_t *t, int lod, DWORD first, DWORD last, const void *data)
{
    vgpu98_gltex_t c;
    uint32_t       w, h, bpt, at;

    if (!tmu_ok(tmu) || !info_ok(t) || !data)
        return;
    as_cpu(t, &c);
    c.small = 0; /* the offset needs the largest level alone */
    if ((lod < 0) || (lod > t->large) || !vgpu98_gl_has_level(&c, lod))
        return;
    vgpu98_gl_level_size(lod, t->aspect, &w, &h);
    if (last >= h)
        last = h - 1;
    if (first > last)
        return;
    bpt = vgpu98_gl_texel_bytes(t->format);
    at  = start + vgpu98_gl_level_offset(&c, lod) + first * w * bpt;
    glc_sync();
    send(tmu, at, (const uint8_t *) data, (last - first + 1) * w * bpt);
    glc_count(GLC_DOWNLOADS);
}

/* Every level from the largest to the smallest, the data packed level
   after level; those outside evenOdd skipped (their data passed over). */
void
glc_tex_download_all(FxI32 tmu, DWORD start, const glc_tex_t *t, const void *data)
{
    const uint8_t *src = (const uint8_t *) data;
    int            lod;

    if (!tmu_ok(tmu) || !info_ok(t) || !data)
        return;
    for (lod = t->large; lod >= t->small; lod--) {
        uint32_t w, h;

        vgpu98_gl_level_size(lod, t->aspect, &w, &h);
        glc_tex_download(tmu, start, t, lod, 0, h - 1, src);
        src += w * h * vgpu98_gl_texel_bytes(t->format);
    }
}

/* grTexSource: the TMU's texture and its description. A Voodoo3's Glide 3
   turns P_8 into the 6666 palette's format after that table. */
int
glc_tex_source(FxI32 tmu, DWORD start, const glc_tex_t *t)
{
    const DWORD base = VGPU98_GS_TMU(tmu);
    int         format;

    if (!tmu_ok(tmu) || !info_ok(t))
        return 0;
    format = ((t->format == VGPU98_GL_TEX_P_8) && glc_palette_6666()) ? 0x6 : t->format;
    glc_reg(base + VGPU98_GS_TEX_ADDRESS, start);
    glc_reg(base + VGPU98_GS_TEX_INFO,
            VGPU98_GS_TEXINFO((DWORD) format, (DWORD) t->small, (DWORD) t->large, t->aspect, (DWORD) t->evenodd, (DWORD) tmu));
    glc_reg(base + VGPU98_GS_TEX_SOURCE, 1);
    if (format == VGPU98_GL_TEX_YIQ_422 || format == VGPU98_GL_TEX_AYIQ_8422)
        glc_deferred(GLC_DEFER_YIQ);
    return 1;
}

/* --- Palettes ------------------------------------------------------------- */

static int palette_6666; /* the last table downloaded was the 6666 palette */

int
glc_palette_6666(void)
{
    return palette_6666;
}

/* The last table: the 6666 palette or not (an NCC table, the open, the
   saved state). */
void
glc_palette_6666_set(int on)
{
    palette_6666 = on != 0;
}

/* Entries [first, last] of the palette, from data[first] on (the whole
   table's pointer), to both TMUs, as a Voodoo3's Glide sends it. A 6666
   entry keeps each channel's top six bits, widened again by repeating its
   top two. */
void
glc_palette(const DWORD *data, int first, int last, int is_6666)
{
    DWORD *p;
    int    i;

    if (!data || (first < 0) || (last > 255) || (first > last))
        return;
    palette_6666 = is_6666;
    glc_sync();
    p = glc_cmd(VGPU98_GL_PALETTE, VGPU98_GL_LEN_PALETTE + (DWORD) (last - first + 1));
    if (!p)
        return;
    p[0] = 2; /* both TMUs */
    p[1] = (DWORD) first | ((DWORD) (last - first + 1) << 16);
    for (i = first; i <= last; i++) {
        DWORD e = data[i];

        if (is_6666) {
            DWORD k, out = 0;

            for (k = 0; k < 4; k++) {
                const DWORD c = ((e >> (k * 8)) & 0xff) >> 2;

                out |= ((c << 2) | (c >> 4)) << (k * 8);
            }
            e = out;
        } else
            e &= 0xffffff;
        p[2 + i - first] = e;
    }
    glc_count(GLC_PALETTES);
}

/* --- A TMU's settings ----------------------------------------------------- */

void
glc_tex_combine(FxI32 tmu, FxI32 rgb_fn, FxI32 rgb_factor, FxI32 alpha_fn, FxI32 alpha_factor, FxBool rgb_invert,
                FxBool alpha_invert)
{
    const DWORD base = VGPU98_GS_TMU(tmu);

    if (!tmu_ok(tmu))
        return;
    glc_reg(base + VGPU98_GS_TEX_COMBINE, ((DWORD) rgb_fn & 0xff) | (((DWORD) rgb_factor & 0xf) << 8) |
                                              (((DWORD) alpha_fn & 0xff) << 16) | (((DWORD) alpha_factor & 0xf) << 24));
    glc_reg(base + VGPU98_GS_TEX_INVERT, (rgb_invert ? 1 : 0) | (alpha_invert ? 2 : 0));
}

void
glc_tex_filter(FxI32 tmu, FxI32 min, FxI32 mag)
{
    if (tmu_ok(tmu))
        glc_reg(VGPU98_GS_TMU(tmu) + VGPU98_GS_TEX_FILTER, ((DWORD) min & 1) | (((DWORD) mag & 1) << 8));
}

void
glc_tex_clamp(FxI32 tmu, FxI32 s, FxI32 t)
{
    if (tmu_ok(tmu))
        glc_reg(VGPU98_GS_TMU(tmu) + VGPU98_GS_TEX_CLAMP, ((DWORD) s & 3) | (((DWORD) t & 3) << 8));
}

void
glc_tex_mipmap(FxI32 tmu, FxI32 mode, FxBool trilinear)
{
    if (!tmu_ok(tmu) || (mode < 0) || (mode > 2))
        return;
    /* Dithering between levels only where it is allowed; trilinear turns it
       off. */
    if ((mode == 2) && (!glc_mipmap_dither_allowed() || trilinear))
        mode = 1;
    glc_reg(VGPU98_GS_TMU(tmu) + VGPU98_GS_TEX_MIPMAP, (DWORD) mode | ((trilinear ? 1u : 0u) << 8));
}

/* The bias in quarter steps, -8.0 to 7.75, as a Voodoo3's Glide rounds it:
   trunc((v + 0.125) / 0.25). */
int
glc_lod_steps(float bias)
{
    const int steps = (int) ((bias + 0.125f) / 0.25f);

    return (steps < -32) ? -32 : (steps > 31) ? 31 : steps;
}

void
glc_tex_lod_bias_steps(FxI32 tmu, int steps)
{
    union {
        float f;
        DWORD d;
    } v;

    if (!tmu_ok(tmu))
        return;
    steps = (steps < -32) ? -32 : (steps > 31) ? 31 : steps;
    v.f   = (float) steps * 0.25f;
    glc_reg(VGPU98_GS_TMU(tmu) + VGPU98_GS_TEX_LOD_BIAS, v.d);
}

void
glc_tex_lod_bias(FxI32 tmu, float bias)
{
    glc_tex_lod_bias_steps(tmu, glc_lod_steps(bias));
}

/* tDetail: the bias (6-bit signed), the scale (0-7), the most (x 255,
   cut), as VGPU98_GS_TEX_DETAIL holds them. */
void
glc_tex_detail(FxI32 tmu, int lod_bias, FxU8 scale, float detail_max)
{
    int m;

    if (!tmu_ok(tmu))
        return;
    lod_bias = (lod_bias < -32) ? -32 : (lod_bias > 31) ? 31 : lod_bias;
    m        = (detail_max <= 0.0f) ? 0 : (detail_max >= 1.0f) ? 255 : (int) (detail_max * 255.0f);
    glc_reg(VGPU98_GS_TMU(tmu) + VGPU98_GS_TEX_DETAIL,
            ((DWORD) lod_bias & 0xff) | (((DWORD) scale & 7) << 8) | ((DWORD) m << 16));
}

/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VGPU98 command set 2.4: the Glide device's arithmetic, which
 *          the Glide DLLs and the device share:
 *          a texture's levels in TMU memory as a Voodoo3's Glide lays
 *          them out, the texel formats decoded to 8:8:8:8, and the depth
 *          and fog values the device's pixel shader computes (in C, for
 *          the host tests). Facts from Glide's manuals, the Voodoo specs
 *          and 3dfx's open-source Glide, read for facts.
 *
 *          Kept in the VGPU98 project as card/vid_vgpu98_glcpu.h and
 *          copied unchanged into 86Box. Inline functions only, so either
 *          side includes it alone.
 *
 * Authors: VGPU98 Project contributors
 *
 *          Copyright 2026 VGPU98 Project contributors.
 */
#ifndef VIDEO_VGPU98_GLCPU_H
#define VIDEO_VGPU98_GLCPU_H

/* A texture as Glide describes it: its format (GR_TEXFMT_*), its smallest
   and largest levels as log2 of the long side (0-8), its aspect as log2 of
   width over height (-3 to 3), and the levels it holds
   (GR_MIPMAPLEVELMASK_*: 1 even, 2 odd, 3 both). */
typedef struct vgpu98_gltex_t {
    int format;
    int small;
    int large;
    int aspect;
    int evenodd;
} vgpu98_gltex_t;

/* Level `lod`'s size in texels; the short side never below 1. */
static inline void
vgpu98_gl_level_size(int lod, int aspect, uint32_t *w, uint32_t *h)
{
    const uint32_t s = 1u << ((lod < 0) ? 0 : (lod > 8) ? 8 : lod);

    if (aspect >= 0) {
        *w = s;
        *h = (s >> aspect) ? (s >> aspect) : 1;
    } else {
        *h = s;
        *w = (s >> -aspect) ? (s >> -aspect) : 1;
    }
}

/* Bytes a texel: 1 for formats 0-7, 2 for 8-15. */
static inline uint32_t
vgpu98_gl_texel_bytes(int format)
{
    return (format & 8) ? 2 : 1;
}

/* Is level `lod` of the texture's parity (a split texture holds one)? */
static inline int
vgpu98_gl_of_parity(const vgpu98_gltex_t *t, int lod)
{
    if (t->evenodd == 1)
        return !(lod & 1);
    if (t->evenodd == 2)
        return lod & 1;
    return 1;
}

/* Does the texture hold level `lod`: in its range, of its parity? */
static inline int
vgpu98_gl_has_level(const vgpu98_gltex_t *t, int lod)
{
    return (lod >= t->small) && (lod <= t->large) && vgpu98_gl_of_parity(t, lod);
}

static inline uint32_t
vgpu98_gl_level_bytes(const vgpu98_gltex_t *t, int lod)
{
    uint32_t w, h;

    vgpu98_gl_level_size(lod, t->aspect, &w, &h);
    return w * h * vgpu98_gl_texel_bytes(t->format);
}

/* The bytes of the levels above `lod`, from the 256 level, of the
   texture's parity: where `lod` lies from where the 256 level would. */
static inline uint32_t
vgpu98_gl_above(const vgpu98_gltex_t *t, int lod)
{
    uint32_t n = 0;
    int      k;

    for (k = 8; k > lod; k--) {
        if (vgpu98_gl_of_parity(t, k))
            n += vgpu98_gl_level_bytes(t, k);
    }
    return n;
}

/* Where level `lod` starts from the texture's start address, as a
   Voodoo3's Glide places it: the levels from large to small, each after
   those above it down from the 256 level (of its parity alone for a split
   texture), counted from the start less the largest level's offset
   rounded down to 16. */
static inline uint32_t
vgpu98_gl_level_offset(const vgpu98_gltex_t *t, int lod)
{
    return vgpu98_gl_above(t, lod) - (vgpu98_gl_above(t, t->large) & ~15u);
}

/* grTexTextureMemRequired and grTexCalcMemRequired: the levels' bytes,
   rounded up to 16; a split texture's small levels count padded (1x1 and
   2x1 as 4 texels, 4x1 as 8, 8x1 as 16). */
static inline uint32_t
vgpu98_gl_mem_required(const vgpu98_gltex_t *t)
{
    uint32_t n = 0;
    int      k;

    for (k = t->small; k <= t->large; k++) {
        uint32_t w, h, texels;

        if (!vgpu98_gl_of_parity(t, k))
            continue;
        vgpu98_gl_level_size(k, t->aspect, &w, &h);
        texels = w * h;
        if (t->evenodd != 3) {
            if ((texels == 1) || (texels == 2))
                texels = 4;
            else if (((texels == 4) || (texels == 8)) && ((w == 1) || (h == 1)))
                texels *= 2;
        }
        n += texels * vgpu98_gl_texel_bytes(t->format);
    }
    return (n + 15) & ~15u;
}

/* --- Texels --------------------------------------------------------------- */

static inline uint32_t
vgpu98_gl_rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
    return r | (g << 8) | (b << 16) | (a << 24);
}

static inline uint32_t
vgpu98_gl_rgb332(uint32_t v, uint32_t a)
{
    const uint32_t r = v >> 5, g = (v >> 2) & 7, b = v & 3;

    return vgpu98_gl_rgba((r << 5) | (r << 2) | (r >> 1), (g << 5) | (g << 2) | (g >> 1), b * 0x55, a);
}

/* A palette entry, 0x00RRGGBB (the 6666 palette's 0xAARRGGBB, its alpha
   taken). */
static inline uint32_t
vgpu98_gl_pal(const uint32_t *palette, uint32_t i, int with_alpha, uint32_t a)
{
    const uint32_t e = palette ? palette[i & 255] : 0;

    return vgpu98_gl_rgba((e >> 16) & 0xff, (e >> 8) & 0xff, e & 0xff, with_alpha ? (e >> 24) : a);
}

/* Decodes w x h texels of `format` (rows of w texels, packed,
   little-endian) into R8G8B8A8 (red in the lowest byte), each channel
   widened by repeating its top bits as the Voodoo does. YIQ formats (NCC,
   deferred) read their Y index as grey. 0 for a format the device does not
   know. */
static inline int
vgpu98_gl_decode(int format, const uint8_t *src, uint32_t w, uint32_t h, const uint32_t *palette, uint32_t *out)
{
    const uint32_t n = w * h;
    uint32_t       i;

    for (i = 0; i < n; i++) {
        const uint32_t v = (format & 8) ? ((uint32_t) src[i * 2] | ((uint32_t) src[i * 2 + 1] << 8)) : src[i];
        uint32_t       r, g, b;

        switch (format) {
            case 0x0: /* RGB_332 */
                out[i] = vgpu98_gl_rgb332(v, 0xff);
                break;
            case 0x1: /* YIQ_422 */
                out[i] = vgpu98_gl_rgba((v >> 4) * 17, (v >> 4) * 17, (v >> 4) * 17, 0xff);
                break;
            case 0x2: /* ALPHA_8 */
                out[i] = vgpu98_gl_rgba(v, v, v, v);
                break;
            case 0x3: /* INTENSITY_8 */
                out[i] = vgpu98_gl_rgba(v, v, v, 0xff);
                break;
            case 0x4: /* ALPHA_INTENSITY_44 */
                out[i] = vgpu98_gl_rgba((v & 15) * 17, (v & 15) * 17, (v & 15) * 17, (v >> 4) * 17);
                break;
            case 0x5: /* P_8 */
                out[i] = vgpu98_gl_pal(palette, v, 0, 0xff);
                break;
            case 0x6: /* P_8 through the 6666 palette */
                out[i] = vgpu98_gl_pal(palette, v, 1, 0);
                break;
            case 0x8: /* ARGB_8332 */
                out[i] = vgpu98_gl_rgb332(v & 0xff, v >> 8);
                break;
            case 0x9: /* AYIQ_8422 */
                out[i] = vgpu98_gl_rgba(((v >> 4) & 15) * 17, ((v >> 4) & 15) * 17, ((v >> 4) & 15) * 17, v >> 8);
                break;
            case 0xa: /* RGB_565 */
                r      = v >> 11;
                g      = (v >> 5) & 63;
                b      = v & 31;
                out[i] = vgpu98_gl_rgba((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 0xff);
                break;
            case 0xb: /* ARGB_1555 */
                r      = (v >> 10) & 31;
                g      = (v >> 5) & 31;
                b      = v & 31;
                out[i] = vgpu98_gl_rgba((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), (v & 0x8000) ? 0xff : 0);
                break;
            case 0xc: /* ARGB_4444 */
                out[i] = vgpu98_gl_rgba(((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17, (v >> 12) * 17);
                break;
            case 0xd: /* ALPHA_INTENSITY_88 */
                out[i] = vgpu98_gl_rgba(v & 0xff, v & 0xff, v & 0xff, v >> 8);
                break;
            case 0xe: /* AP_88 */
                out[i] = vgpu98_gl_pal(palette, v & 0xff, 0, v >> 8);
                break;
            default:
                return 0;
        }
    }
    return 1;
}

/* --- Depth and fog, as glide.frag computes them ----------------------------- */

/* 1/w as the Voodoo's 16-bit floating depth: a 4-bit count of the leading
   zeros of its fraction and the 12 bits after the leading one, inverted,
   plus one; 0 at 1 and nearer, FFFFh past 2^-16. It grows with w. */
static inline uint32_t
vgpu98_gl_wfloat(float oow)
{
    uint32_t f, e, mant, v;

    if (!(oow < 1.0f))
        return 0;
    if (!(oow >= 0.0f))
        return 0xffff;
    f = (uint32_t) (oow * 65536.0f);
    if (!f)
        return 0xffff;
    for (e = 0; !(f & (0x8000u >> e)); e++)
        ;
    mant = ((f << (e + 1)) & 0xffff) >> 4;
    v    = ((e << 12) | (0xfff - mant)) + 1;
    return (v > 0xffff) ? 0xffff : v;
}

/* The fog table's alpha at a 16-bit float depth: the entry its top six
   bits pick, toward the next by the eight after them. */
static inline int
vgpu98_gl_fog(const uint8_t *table, uint32_t wf)
{
    const uint32_t i    = (wf >> 10) & 63;
    const uint32_t frac = (wf >> 2) & 255;
    const int      e0   = table[i];
    const int      e1   = (i < 63) ? table[i + 1] : e0;

    return e0 + (((e1 - e0) * (int) frac) >> 8);
}

#endif /* VIDEO_VGPU98_GLCPU_H */

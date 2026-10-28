/*
 * vgpu98dx.dll: DirectDraw surfaces as the GPU sees them: their format code
 * (spec/gpu-formats.md) and their place in video memory.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "d3d.h"

/* Command set 2.1's and 2.2's formats, with the DirectX 8 interface only:
   as the runtime describes them at creation (legacy masks, or their
   D3DFORMAT code as a FourCC, which it uses for the signed ones). */
static DWORD
fmt8_from_ddpf(const DDPIXELFORMAT *pf)
{
    const DWORD f = pf->dwFlags;

    if (f & DDPF_FOURCC) {
        switch (pf->dwFourCC) {
            case D3DFMT_A8:
                return VGPU98_FMT_A8;
            case D3DFMT_X4R4G4B4:
                return VGPU98_FMT_X4R4G4B4;
            case D3DFMT_A4L4:
                return VGPU98_FMT_A4L4;
            case D3DFMT_Q8W8V8U8:
                return VGPU98_FMT_Q8W8V8U8;
            case D3DFMT_V16U16:
                return d3d8_fmt22 ? VGPU98_FMT_V16U16 : 0;
            case D3DFMT_G16R16:
                return d3d8_fmt22 ? VGPU98_FMT_G16R16 : 0;
            case D3DFMT_A2W10V10U10:
                return d3d8_fmt22 ? VGPU98_FMT_A2W10V10U10 : 0;
            default:
                return 0;
        }
    }
    /* Command set 2.2's, by their masks too (the DDK has DirectX 8.0's
       V16U16 come as a FourCC and every later format by its masks). */
    if (d3d8_fmt22 && (pf->dwRGBBitCount == 32)) {
        /* (an alpha flag with no alpha bits taken as none) */
        if ((f & DDPF_RGB) && (!(f & DDPF_ALPHAPIXELS) || !pf->dwRGBAlphaBitMask) && (pf->dwRBitMask == 0x0000ffff) &&
            (pf->dwGBitMask == 0xffff0000) && !pf->dwBBitMask)
            return VGPU98_FMT_G16R16;
        if ((f & DDPF_BUMPDUDV) && (pf->dwRBitMask == 0x0000ffff) && (pf->dwGBitMask == 0xffff0000))
            return VGPU98_FMT_V16U16;
        if ((f & DDPF_BUMPDUDV) && (pf->dwRBitMask == 0x000003ff) && (pf->dwGBitMask == 0x000ffc00))
            return VGPU98_FMT_A2W10V10U10;
    }
    if ((f & DDPF_ALPHA) && !(f & (DDPF_RGB | DDPF_LUMINANCE | DDPF_BUMPDUDV)) && (pf->dwRGBBitCount == 8))
        return VGPU98_FMT_A8;
    if ((f & DDPF_LUMINANCE) && (f & DDPF_ALPHAPIXELS) && (pf->dwRGBBitCount == 8) && (pf->dwRBitMask == 0x0f) &&
        (pf->dwRGBAlphaBitMask == 0xf0))
        return VGPU98_FMT_A4L4;
    if ((f & DDPF_RGB) && !(f & DDPF_ALPHAPIXELS) && (pf->dwRGBBitCount == 16) && (pf->dwRBitMask == 0x0f00) &&
        (pf->dwGBitMask == 0x00f0) && (pf->dwBBitMask == 0x000f))
        return VGPU98_FMT_X4R4G4B4;
    return 0;
}

DWORD
fmt_from_ddpf(const DDPIXELFORMAT *pf)
{
    const DWORD f = pf->dwFlags;

    if (d3d8_on) {
        const DWORD f8 = fmt8_from_ddpf(pf);

        if (f8)
            return f8;
    }
    if (f & DDPF_FOURCC) {
        switch (pf->dwFourCC) {
            case MAKEFOURCC('D', 'X', 'T', '1'):
                return VGPU98_FMT_DXT1;
            case MAKEFOURCC('D', 'X', 'T', '2'):
                return VGPU98_FMT_DXT2;
            case MAKEFOURCC('D', 'X', 'T', '3'):
                return VGPU98_FMT_DXT3;
            case MAKEFOURCC('D', 'X', 'T', '4'):
                return VGPU98_FMT_DXT4;
            case MAKEFOURCC('D', 'X', 'T', '5'):
                return VGPU98_FMT_DXT5;
            default:
                return 0;
        }
    }
    if (f & DDPF_ZBUFFER) {
        /* The bit depth counts stencil. A Z buffer described only by its
           depth (DirectX 5) is D16 or D24X8. */
        if (pf->dwRGBBitCount == 16)
            return ((pf->dwGBitMask == 0xffff) || !pf->dwGBitMask) && !(f & DDPF_STENCILBUFFER) ? VGPU98_FMT_D16 : 0;
        if (pf->dwRGBBitCount == 32) {
            if ((pf->dwGBitMask != 0xffffff00) && pf->dwGBitMask)
                return 0;
            if (f & DDPF_STENCILBUFFER)
                return ((pf->dwRBitMask == 8) && (pf->dwBBitMask == 0xff)) ? VGPU98_FMT_D24S8 : 0;
            return VGPU98_FMT_D24X8;
        }
        return 0;
    }
    if (f & DDPF_BUMPDUDV) {
        if (f & DDPF_BUMPLUMINANCE) {
            if ((pf->dwRGBBitCount == 16) && (pf->dwRBitMask == 0x001f) && (pf->dwGBitMask == 0x03e0) &&
                (pf->dwBBitMask == 0xfc00))
                return VGPU98_FMT_L6V5U5;
            if ((pf->dwRGBBitCount == 32) && (pf->dwRBitMask == 0xff) && (pf->dwGBitMask == 0xff00) &&
                (pf->dwBBitMask == 0xff0000))
                return VGPU98_FMT_X8L8V8U8;
            return 0;
        }
        return ((pf->dwRGBBitCount == 16) && (pf->dwRBitMask == 0xff) && (pf->dwGBitMask == 0xff00)) ? VGPU98_FMT_V8U8 : 0;
    }
    if (f & DDPF_LUMINANCE) {
        if ((pf->dwRGBBitCount == 8) && (pf->dwRBitMask == 0xff) && !(f & DDPF_ALPHAPIXELS))
            return VGPU98_FMT_L8;
        if ((pf->dwRGBBitCount == 16) && (pf->dwRBitMask == 0xff) && (f & DDPF_ALPHAPIXELS) &&
            (pf->dwRGBAlphaBitMask == 0xff00))
            return VGPU98_FMT_A8L8;
        return 0;
    }
    if (f & DDPF_RGB) {
        const DWORD a = (f & DDPF_ALPHAPIXELS) ? pf->dwRGBAlphaBitMask : 0;

        if (f & DDPF_PALETTEINDEXED8)
            return (pf->dwRGBBitCount == 8) ? VGPU98_FMT_P8 : 0;
        if (pf->dwRGBBitCount == 16) {
            if ((pf->dwRBitMask == 0xf800) && (pf->dwGBitMask == 0x07e0) && (pf->dwBBitMask == 0x001f) && !a)
                return VGPU98_FMT_R5G6B5;
            if ((pf->dwRBitMask == 0x7c00) && (pf->dwGBitMask == 0x03e0) && (pf->dwBBitMask == 0x001f))
                return (a == 0x8000) ? VGPU98_FMT_A1R5G5B5 : (a ? 0 : VGPU98_FMT_X1R5G5B5);
            if ((pf->dwRBitMask == 0x0f00) && (pf->dwGBitMask == 0x00f0) && (pf->dwBBitMask == 0x000f) && (a == 0xf000))
                return VGPU98_FMT_A4R4G4B4;
            return 0;
        }
        if ((pf->dwRGBBitCount == 32) && (pf->dwRBitMask == 0xff0000) && (pf->dwGBitMask == 0xff00) &&
            (pf->dwBBitMask == 0xff))
            return (a == 0xff000000) ? VGPU98_FMT_A8R8G8B8 : (a ? 0 : VGPU98_FMT_X8R8G8B8);
    }
    return 0;
}

/* The display's own format, for surfaces without a pixel format of their own. */
static DWORD
display_format(void)
{
    switch (sb->mode_bpp) {
        case 8:
            return VGPU98_FMT_P8;
        case 16:
            return VGPU98_FMT_R5G6B5;
        case 32:
            return VGPU98_FMT_X8R8G8B8;
        default:
            return 0;
    }
}

DWORD
surf_format(LPSURF s)
{
    if ((s->dwFlags & DDRAWISURF_HASPIXELFORMAT) || (s->ddsCaps.dwCaps & DDSCAPS_ZBUFFER))
        return fmt_from_ddpf(&s->lpGbl->ddpfSurface);
    return display_format();
}

int
fmt_is_dxt(DWORD f)
{
    return (f >= VGPU98_FMT_DXT1) && (f <= VGPU98_FMT_DXT5);
}

DWORD
fmt_bytes(DWORD f)
{
    switch (f) {
        case VGPU98_FMT_P8:
        case VGPU98_FMT_L8:
        case VGPU98_FMT_A8:
        case VGPU98_FMT_A4L4:
            return 1;
        case VGPU98_FMT_X8R8G8B8:
        case VGPU98_FMT_A8R8G8B8:
        case VGPU98_FMT_X8L8V8U8:
        case VGPU98_FMT_Q8W8V8U8:
        case VGPU98_FMT_V16U16:
        case VGPU98_FMT_G16R16:
        case VGPU98_FMT_A2W10V10U10:
        case VGPU98_FMT_D24X8:
        case VGPU98_FMT_D24S8:
            return 4;
        default:
            return 2;
    }
}

DWORD
surf_bytes(const gsurf_t *g)
{
    if (fmt_is_dxt(g->format))
        return ((g->height + 3) / 4 - 1) * g->pitch + ((g->width + 3) / 4) * ((g->format == VGPU98_FMT_DXT1) ? 8 : 16);
    return (g->height - 1) * g->pitch + g->width * fmt_bytes(g->format);
}

/* The size, format and pitch of a surface; 0 if the GPU cannot take them. */
static int
describe(LPSURF s, gsurf_t *g)
{
    DDRAWI_DDRAWSURFACE_GBL *gbl = s->lpGbl;

    g->width  = gbl->wWidth;
    g->height = gbl->wHeight;
    g->format = surf_format(s);
    if (!g->format || !g->width || !g->height)
        return 0;
    /* A DXT surface's rows of blocks are packed: its lPitch holds the
       linear size, as the reference rasterizer assumes too. */
    if (fmt_is_dxt(g->format))
        g->pitch = ((g->width + 3) / 4) * ((g->format == VGPU98_FMT_DXT1) ? 8 : 16);
    else
        g->pitch = (DWORD) gbl->lPitch;
    return (g->pitch <= 0xffff) && (g->width <= 0xffff) && (g->height <= 0xffff);
}

int
surf_describe(LPSURF s, gsurf_t *g)
{
    DDRAWI_DDRAWSURFACE_GBL *gbl = s->lpGbl;

    if (s->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY)
        return 0;
    if ((gbl->fpVidMem < sb->fb_linear) || (gbl->fpVidMem >= sb->fb_linear + sb->vram_size))
        return 0;
    if (!describe(s, g))
        return 0;
    g->offset = gbl->fpVidMem - sb->fb_linear;
    return g->offset + surf_bytes(g) <= sb->vram_size;
}

int
surf_describe_sys(LPSURF s, gsurf_t *g, const uint8_t **mem)
{
    if (!(s->ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) || !s->lpGbl->fpVidMem || !describe(s, g))
        return 0;
    /* Rows at least a row apart (a DXT surface's are packed, as above). */
    if (!fmt_is_dxt(g->format) && (g->pitch < g->width * fmt_bytes(g->format)))
        return 0;
    g->offset = 0;
    *mem      = (const uint8_t *) s->lpGbl->fpVidMem;
    return 1;
}

void
pkt_surf(volatile DWORD **p, const gsurf_t *g)
{
    pkt_put(p, g->offset);
    pkt_put(p, VGPU98_SURF_SIZE(g->width, g->height));
    pkt_put(p, VGPU98_SURF_PITCH(g->pitch, g->format));
}

/* --- Loads and copies ---------------------------------------------------- */

/* The largest packet payload the DLL writes, in dwords: half the ring. */
#define MAX_PAYLOAD (SB_RING_SIZE / 2 / 4 - 1)

void
surf_blocks(const gsurf_t *g, RECTL *r)
{
    LONG right;
    LONG bottom;

    if (!fmt_is_dxt(g->format))
        return;
    right  = (r->right + 3) & ~3;
    bottom = (r->bottom + 3) & ~3;
    r->left &= ~3;
    r->top &= ~3;
    r->right  = (right > (LONG) g->width) ? (LONG) g->width : right;
    r->bottom = (bottom > (LONG) g->height) ? (LONG) g->height : bottom;
}

/* A rectangle's rows and the bytes of each, as memory holds them: rows of
   blocks for DXT. */
void
rect_rows(DWORD format, const RECTL *r, DWORD *rows, DWORD *row_bytes)
{
    if (fmt_is_dxt(format)) {
        *rows      = (DWORD) (r->bottom - r->top + 3) / 4;
        *row_bytes = (DWORD) ((r->right - r->left + 3) / 4) * ((format == VGPU98_FMT_DXT1) ? 8 : 16);
    } else {
        *rows      = (DWORD) (r->bottom - r->top);
        *row_bytes = (DWORD) (r->right - r->left) * fmt_bytes(format);
    }
}

/* The byte of a rectangle's top left, in memory laid out as `g`. */
DWORD
rect_start(const gsurf_t *g, const RECTL *r)
{
    if (fmt_is_dxt(g->format))
        return (DWORD) (r->top / 4) * g->pitch + (DWORD) (r->left / 4) * ((g->format == VGPU98_FMT_DXT1) ? 8 : 16);
    return (DWORD) r->top * g->pitch + (DWORD) r->left * fmt_bytes(g->format);
}

DWORD
upload_rect(const gsurf_t *dst, const RECTL *dr, const gsurf_t *src, const uint8_t *mem, const RECTL *sr, int *ok)
{
    const DWORD unit = fmt_is_dxt(dst->format) ? 4 : 1; /* the pixel rows of a row */
    RECTL       d    = *dr;
    RECTL       s    = *sr;
    DWORD       rows;
    DWORD       row_bytes;
    DWORD       ip;
    DWORD       per;
    DWORD       y;
    DWORD       sent = 0;

    *ok = 1;
    if (src->format != dst->format)
        return 0;
    surf_blocks(dst, &d);
    surf_blocks(src, &s);
    if ((d.right <= d.left) || (d.bottom <= d.top) || (d.right - d.left != s.right - s.left) ||
        (d.bottom - d.top != s.bottom - s.top))
        return 0;
    rect_rows(dst->format, &d, &rows, &row_bytes);
    mem += rect_start(src, &s);
    ip  = (row_bytes + 3) & ~3u; /* the packet's own pitch: whole dwords */
    per = (4 * (MAX_PAYLOAD - VGPU98_LEN_UPLOAD)) / ip;
    if (!per)
        return 0; /* a row larger than a packet: no surface is that wide */
    for (y = 0; y < rows; y += per) {
        const DWORD     n = (rows - y < per) ? rows - y : per;
        volatile DWORD *p = gpu_packet(VGPU98_OP_UPLOAD, VGPU98_LEN_UPLOAD + n * ip / 4);
        LONG            top;
        LONG            bottom;
        DWORD           i;

        if (!p) {
            *ok = 0;
            return 0;
        }
        top    = d.top + (LONG) (y * unit);
        bottom = d.top + (LONG) ((y + n) * unit);
        if (bottom > d.bottom)
            bottom = d.bottom;
        pkt_surf(&p, dst);
        pkt_put(&p, VGPU98_XY(d.left, top));
        pkt_put(&p, VGPU98_XY(d.right, bottom));
        pkt_put(&p, 0); /* the data follows */
        pkt_put(&p, ip);
        pkt_put(&p, 0);
        for (i = 0; i < n; i++) {
            const uint8_t *row = mem + (size_t) (y + i) * src->pitch;

            copy_dwords(p, row, row_bytes / 4);
            p += row_bytes / 4;
            if (row_bytes & 3) {
                DWORD tail = 0;

                memcpy(&tail, row + (row_bytes & ~3u), row_bytes & 3);
                pkt_put(&p, tail);
            }
        }
        sent += n * row_bytes;
    }
    return sent;
}

int
blt_packet(const gsurf_t *src, const RECTL *sr, const gsurf_t *dst, const RECTL *dr)
{
    volatile DWORD *p = gpu_packet(VGPU98_OP_BLT, VGPU98_LEN_BLT);

    if (!p)
        return 0;
    pkt_surf(&p, src);
    pkt_put(&p, VGPU98_XY(sr->left, sr->top));
    pkt_put(&p, VGPU98_XY(sr->right, sr->bottom));
    pkt_surf(&p, dst);
    pkt_put(&p, VGPU98_XY(dr->left, dr->top));
    pkt_put(&p, VGPU98_XY(dr->right, dr->bottom));
    pkt_put(&p, 0);
    pkt_put(&p, 0);
    pkt_put(&p, 0);
    return 1;
}

/* --- DXT decompression (system-to-video blits) --------------------------- */

/* A 5:6:5 colour as 8:8:8, each channel's top bits repeated below. */
static DWORD
rgb565(DWORD c)
{
    const DWORD r = (c >> 11) & 31;
    const DWORD g = (c >> 5) & 63;
    const DWORD b = c & 31;

    return (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
}

/* (wa × a + wb × b + round) / div, per colour channel. */
static DWORD
mix(DWORD a, DWORD b, DWORD wa, DWORD wb, DWORD round, DWORD div)
{
    DWORD out = 0;
    int   sh;

    for (sh = 0; sh < 24; sh += 8)
        out |= ((((a >> sh) & 255) * wa + ((b >> sh) & 255) * wb + round) / div) << sh;
    return out;
}

/* A DXT block's 16 texels as A8R8G8B8, row by row, decoded as the GPU
   decodes them (spec/gpu-formats.md: the reference rasterizer's rounding,
   the three-colour mode for DXT1 only). DXT2 and DXT4 keep their
   premultiplied colours, as stored. */
static void
dxt_block(DWORD format, const uint8_t *b, DWORD *t)
{
    const uint8_t *cb   = (format == VGPU98_FMT_DXT1) ? b : b + 8;
    const DWORD    c0   = cb[0] | (cb[1] << 8);
    const DWORD    c1   = cb[2] | (cb[3] << 8);
    const DWORD    bits = cb[4] | (cb[5] << 8) | (cb[6] << 16) | ((DWORD) cb[7] << 24);
    DWORD          col[4];
    int            i;

    col[0] = rgb565(c0) | 0xff000000u;
    col[1] = rgb565(c1) | 0xff000000u;
    if ((format != VGPU98_FMT_DXT1) || (c0 > c1)) {
        col[2] = mix(col[0], col[1], 2, 1, 1, 3) | 0xff000000u;
        col[3] = mix(col[0], col[1], 1, 2, 1, 3) | 0xff000000u;
    } else {
        col[2] = mix(col[0], col[1], 1, 1, 0, 2) | 0xff000000u;
        col[3] = 0; /* transparent black */
    }
    for (i = 0; i < 16; i++)
        t[i] = col[(bits >> (2 * i)) & 3];
    if ((format == VGPU98_FMT_DXT2) || (format == VGPU98_FMT_DXT3)) {
        /* Explicit alpha, four bits a texel. */
        for (i = 0; i < 16; i++)
            t[i] = (t[i] & 0xffffff) | ((((b[i / 2] >> (4 * (i & 1))) & 15) * 17) << 24);
    } else if ((format == VGPU98_FMT_DXT4) || (format == VGPU98_FMT_DXT5)) {
        /* Interpolated alpha: two end points, three-bit indices. */
        DWORD    a[8];
        uint64_t idx = 0;
        DWORD    k;

        a[0] = b[0];
        a[1] = b[1];
        if (a[0] > a[1]) {
            for (k = 1; k < 7; k++)
                a[k + 1] = ((7 - k) * a[0] + k * a[1]) / 7;
        } else {
            for (k = 1; k < 5; k++)
                a[k + 1] = ((5 - k) * a[0] + k * a[1]) / 5;
            a[6] = 0;
            a[7] = 255;
        }
        for (k = 0; k < 6; k++)
            idx |= (uint64_t) b[2 + k] << (8 * k);
        for (i = 0; i < 16; i++)
            t[i] = (t[i] & 0xffffff) | (a[(idx >> (3 * i)) & 7] << 24);
    }
}

/* Bytes of an A8R8G8B8 texel converted to `format`; 0 if it is not a
   colour format decompression writes. */
DWORD
argb_bytes(DWORD format)
{
    switch (format) {
        case VGPU98_FMT_X8R8G8B8:
        case VGPU98_FMT_A8R8G8B8:
            return 4;
        case VGPU98_FMT_R5G6B5:
        case VGPU98_FMT_X1R5G5B5:
        case VGPU98_FMT_A1R5G5B5:
        case VGPU98_FMT_A4R4G4B4:
            return 2;
        default:
            return 0;
    }
}

static void
argb_put(uint8_t *p, DWORD format, DWORD v)
{
    const DWORD a = v >> 24;
    const DWORD r = (v >> 16) & 255;
    const DWORD g = (v >> 8) & 255;
    const DWORD b = v & 255;
    DWORD       w;

    switch (format) {
        case VGPU98_FMT_X8R8G8B8:
        case VGPU98_FMT_A8R8G8B8:
            memcpy(p, &v, 4);
            return;
        case VGPU98_FMT_R5G6B5:
            w = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            break;
        case VGPU98_FMT_A4R4G4B4:
            w = ((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
            break;
        default: /* X1R5G5B5, A1R5G5B5 */
            w = ((a >= 128) ? 0x8000 : 0) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
            break;
    }
    p[0] = (uint8_t) w;
    p[1] = (uint8_t) (w >> 8);
}

DWORD
upload_rect_dxt(const gsurf_t *dst, const RECTL *dr, const gsurf_t *src, const uint8_t *mem, const RECTL *sr, int *ok)
{
    /* Four rows of texels at a time; shared, under the ring lock. */
    static uint8_t strip[4 * 4 * DXT_STRIP];
    const DWORD    block = (src->format == VGPU98_FMT_DXT1) ? 8 : 16;
    const DWORD    bpp   = argb_bytes(dst->format);
    const LONG     w     = sr->right - sr->left;
    gsurf_t        t;
    DWORD          sent = 0;
    LONG           y;

    *ok = 1;
    if (!fmt_is_dxt(src->format) || !bpp || (w <= 0) || (w > DXT_STRIP) || (dr->right - dr->left != w) ||
        (dr->bottom - dr->top != sr->bottom - sr->top))
        return 0;
    t.offset = 0;
    t.width  = (DWORD) w;
    t.pitch  = (DWORD) w * bpp;
    t.format = dst->format;
    for (y = sr->top; y < sr->bottom;) {
        const LONG y1 = (((y & ~3) + 4) < sr->bottom) ? (y & ~3) + 4 : sr->bottom; /* the rest of this row of blocks */
        RECTL      s;
        RECTL      d;
        LONG       x;

        for (x = sr->left & ~3; x < sr->right; x += 4) {
            DWORD texels[16];
            LONG  ty;
            LONG  tx;

            dxt_block(src->format, mem + (DWORD) (y / 4) * src->pitch + (DWORD) (x / 4) * block, texels);
            for (ty = y; ty < y1; ty++) {
                for (tx = (x < sr->left) ? sr->left : x; (tx < x + 4) && (tx < sr->right); tx++)
                    argb_put(strip + (DWORD) (ty - y) * t.pitch + (DWORD) (tx - sr->left) * bpp, dst->format,
                             texels[(ty & 3) * 4 + (tx & 3)]);
            }
        }
        t.height = (DWORD) (y1 - y);
        s.left   = 0;
        s.top    = 0;
        s.right  = w;
        s.bottom = y1 - y;
        d.left   = dr->left;
        d.right  = dr->right;
        d.top    = dr->top + (y - sr->top);
        d.bottom = d.top + (y1 - y);
        sent += upload_rect(dst, &d, &t, strip, &s, ok);
        if (!*ok)
            break;
        y = y1;
    }
    return sent;
}

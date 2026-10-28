/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          VGPU98 2000: Diagnostics "Display",
 *          its arithmetic: a model of how 86Box's VGA core (svga_poll in
 *          vid_svga.c) counts a frame's lines after the timings change
 *          mid-frame, the card's rule for ending a frame its own change
 *          would strand, the V-sync
 *          overrides' rule, the stretched-frame
 *          test, a pixel of video memory as 86Box's renderers turn it into
 *          theirs, and the hash that tells a frame from the last one kept
 *          (in C, for the host tests).
 *
 *          Kept in the VGPU98 project as card/vid_vgpu98_dispdiag.h and
 *          copied unchanged into 86Box. Inline functions only, so either
 *          side includes it alone.
 *
 * Authors: VGPU98 Project contributors
 *
 *          Copyright 2026 VGPU98 Project contributors.
 */
#ifndef VIDEO_VGPU98_DISPDIAG_H
#define VIDEO_VGPU98_DISPDIAG_H

#include <stddef.h>
#include <stdint.h>

/* 86Box's vertical counting where the timings changed: svga_t's fields of
   the same names, and the timings as they are now. */
typedef struct vgpu98_dd_scan_t {
    int vc;         /* the line counter, 11 bits */
    int linepos;    /* svga_poll's next half of the scanline: 0 draws the line, 1 counts it */
    int dispon;     /* in the picture */
    int displine;   /* the frame buffer row of the next line drawn */
    int firstline;  /* the frame's first row drawn so far, 2000 for none */
    int lastline;   /* its last, 0 for none */
    int dispend;    /* the picture's end, */
    int vsyncstart; /* the vertical sync, */
    int vtotal;     /* the frame's total: each a line count, tested for equality */
} vgpu98_dd_scan_t;

/* Half-scanlines the model follows at most: past two laps of the counter. */
#define VGPU98_DD_MAX_STEPS 8192

/* From the scan as recorded and the timings now, what 86Box's next
   vertical sync does: returns the lines it counts for the frame (what
   svga_doblit is given plus one, the screen's height; 0 when it shows no
   frame), or -1 when no sync comes (a sync line the counter never
   reaches). *lines gets the scanlines counted from here to the sync.

   Each scanline as svga_poll has it: first the line is drawn (while in
   the picture, the frame's first and last rows follow it; the row number
   wraps to 0 past 2000), then it is counted (the counter goes up, 11 bits,
   and the picture's end, the sync and the total are each an equality: a
   counter already past one of them goes on to 2047 and wraps). At the
   sync the frame is shown and its rows start again; at the total the
   counter and the row go back to 0 and the picture starts. As the card
   has it: no interlace, no vertical line doubling, no halved line clock. */
static inline int
vgpu98_dd_next_sync(const vgpu98_dd_scan_t *s, int *lines)
{
    int vc       = s->vc & 0x7ff;
    int pos      = s->linepos ? 1 : 0;
    int dispon   = s->dispon ? 1 : 0;
    int displine = s->displine;
    int first    = s->firstline;
    int last     = s->lastline;
    int n        = 0;
    int step;

    for (step = 0; step < VGPU98_DD_MAX_STEPS; step++) {
        if (!pos) {
            if (dispon) {
                if (first == 2000)
                    first = displine;
                if (last < displine)
                    last = displine;
            }
            displine++;
            if (displine > 2000)
                displine = 0;
            pos = 1;
            continue;
        }
        pos = 0;
        n++;
        vc = (vc + 1) & 0x7ff;
        if (vc == s->dispend)
            dispon = 0;
        if (vc == s->vsyncstart) {
            if (lines)
                *lines = n;
            return (last - first > 0) ? last - first + 1 : 0;
        }
        if (vc == s->vtotal) {
            vc       = 0;
            dispon   = 1;
            displine = 0;
        }
    }
    if (lines)
        *lines = n;
    return -1;
}

/* The card's own change (its display engine's mode, the engine on or off,
   a reset) left the counter past the new picture's end while the frame is
   still in its picture, or past the new frame's total: the frame is ended
   there instead. The counter goes to the
   picture's last line, out of the picture, with no rows of the frame
   drawn: the next count is the vertical blank, the sync shows no frame and
   the total starts the new frame whole. Returns 1 when it changed the
   scan. A frame in its picture is always before its end, and one in its
   blanking always before its total, so a normal frame is never ended. */
static inline int
vgpu98_dd_end_frame(vgpu98_dd_scan_t *s)
{
    if ((s->dispend < 1) || ((s->vc & 0x7ff) < s->dispend) || (!s->dispon && ((s->vc & 0x7ff) < s->vtotal)))
        return 0;
    s->vc        = s->dispend - 1;
    s->dispon    = 0;
    s->firstline = 2000;
    s->lastline  = 0;
    return 1;
}

/* The V-sync overrides: VSYNC_CONTROL's field at `shift`
   (Direct3D's at 0, OpenGL's at 2, Glide's at 4) on the vertical blanks a
   flip or swap asked to wait, 0 for none. 0 is the program's choice, 1
   always on (no wait becomes one; a longer one is kept), 2 always off (no
   wait); 3 reads as 0. */
static inline uint32_t
vgpu98_vsync_interval(uint32_t control, int shift, uint32_t asked)
{
    switch ((control >> shift) & 3) {
        case 1:
            return asked ? asked : 1;
        case 2:
            return 0;
        default:
            return asked;
    }
}

/* A frame of `lines` for a mode of `rows`: a quarter or more taller. */
static inline int
vgpu98_dd_stretched(int lines, int rows)
{
    return (rows > 0) && ((int64_t) lines * 4 >= (int64_t) rows * 5);
}

/* 86Box's 32-bit pixel (0x00RRGGBB) as R, G, B and an opaque alpha. */
static inline void
vgpu98_dd_rgba(uint32_t p, uint8_t *out)
{
    out[0] = (uint8_t) (p >> 16);
    out[1] = (uint8_t) (p >> 8);
    out[2] = (uint8_t) p;
    out[3] = 0xff;
}

/* The pixel of video memory at `a` (wrapping by `mask`) as 86Box's
   renderers and display.comp turn it into 86Box's 32-bit pixel: 8 bpp
   through `palette` (each byte value's colour, through the DAC mask), 15
   and 16 bpp scaled to 8 bits a channel by (v * 255) / 31 and / 63, 32 bpp
   as stored. */
static inline uint32_t
vgpu98_dd_pixel(const uint8_t *vram, uint32_t mask, uint32_t a, uint32_t bpp, const uint32_t *palette)
{
    uint32_t v;

    switch (bpp) {
        case 15:
            v = vram[a & mask] | ((uint32_t) vram[(a + 1) & mask] << 8);
            return ((v & 31u) * 255u / 31u) | ((((v >> 5) & 31u) * 255u / 31u) << 8) |
                   ((((v >> 10) & 31u) * 255u / 31u) << 16);
        case 16:
            v = vram[a & mask] | ((uint32_t) vram[(a + 1) & mask] << 8);
            return ((v & 31u) * 255u / 31u) | ((((v >> 5) & 63u) * 255u / 63u) << 8) | (((v >> 11) * 255u / 31u) << 16);
        case 32:
            return vram[a & mask] | ((uint32_t) vram[(a + 1) & mask] << 8) | ((uint32_t) vram[(a + 2) & mask] << 16);
        default:
            return palette[vram[a & mask]] & 0xffffffu;
    }
}

/* A row of `width` pixels from `addr`, as RGBA bytes. */
static inline void
vgpu98_dd_decode_row(const uint8_t *vram, uint32_t mask, uint32_t addr, uint32_t width, uint32_t bpp,
                     const uint32_t *palette, uint8_t *rgba)
{
    const uint32_t bytes = (bpp + 7) / 8;
    uint32_t       x;

    for (x = 0; x < width; x++)
        vgpu98_dd_rgba(vgpu98_dd_pixel(vram, mask, addr + x * bytes, bpp, palette), rgba + (size_t) x * 4);
}

/* FNV-1a's mixing over pixels, from VGPU98_DD_HASH_START. */
#define VGPU98_DD_HASH_START 0xcbf29ce484222325ull

static inline uint64_t
vgpu98_dd_hash(uint64_t h, const uint32_t *p, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

#endif /* VIDEO_VGPU98_DISPDIAG_H */

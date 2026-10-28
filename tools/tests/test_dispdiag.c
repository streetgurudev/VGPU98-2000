/*
 * The Display diagnostics' arithmetic, which 86Box shares
 * (card/vid_vgpu98_dispdiag.h): how 86Box's
 * svga_poll counts a frame's lines when the timings change mid-frame, the
 * card's frame end, the stretched-frame
 * test, a pixel of video memory as display.comp turns it into 86Box's
 * 32-bit pixel, the frame hash, and the V-sync overrides' rule.
 * The counter cases are worked by hand
 * from svga_poll's vertical counting (no interlace, no vertical line
 * doubling, CRTC 17h bit 2 clear), not from the model.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vid_vgpu98_dispdiag.h"
#include "vid_vgpu98_gpu.h"

static int failures;

#define CHECK(cond, ...)                                     \
    do {                                                     \
        if (!(cond)) {                                       \
            fprintf(stderr, "test_dispdiag:%d: ", __LINE__); \
            fprintf(stderr, __VA_ARGS__);                    \
            fputc('\n', stderr);                             \
            failures++;                                      \
        }                                                    \
    } while (0)

/* The engine's timings for a mode of height h: the picture's end h, the
   sync h + 3, the total h + max(h / 20, 23); and the VGA's 80x25 text. */
static const int t640[3]  = { 480, 483, 504 };
static const int t800[3]  = { 600, 603, 630 };
static const int t1024[3] = { 768, 771, 806 };
static const int tvga[3]  = { 400, 413, 449 };

static vgpu98_dd_scan_t
scan(int vc, int linepos, int dispon, int displine, int firstline, int lastline, const int *t)
{
    vgpu98_dd_scan_t s;

    s.vc         = vc;
    s.linepos    = linepos;
    s.dispon     = dispon;
    s.displine   = displine;
    s.firstline  = firstline;
    s.lastline   = lastline;
    s.dispend    = t[0];
    s.vsyncstart = t[1];
    s.vtotal     = t[2];
    return s;
}

/* The next sync from s counts `frame` lines, after `lines` scanlines. */
static void
sync_case(const char *what, vgpu98_dd_scan_t s, int frame, int lines)
{
    int n   = -12345;
    int got = vgpu98_dd_next_sync(&s, &n);

    CHECK((got == frame) && (n == lines), "%s: %d lines after %d scanlines, expected %d after %d", what, got, n, frame,
          lines);
}

static void
steady(void)
{
    /* From the counter at 0 just after the total: rows 0-479 drawn at
       vc 0-479, the count to 480 ends the picture, 480-482 are blank and
       the count to 483 is the sync: rows 0-479, 480 lines (svga_doblit's
       wy 479), after 483 scanlines. */
    sync_case("640x480 steady", scan(0, 0, 1, 0, 2000, 0, t640), 480, 483);
    /* The same at each size: the picture's end lines, then the sync. */
    sync_case("800x600 steady", scan(0, 0, 1, 0, 2000, 0, t800), 600, 603);
    sync_case("1024x768 steady", scan(0, 0, 1, 0, 2000, 0, t1024), 768, 771);
    sync_case("VGA text steady", scan(0, 0, 1, 0, 2000, 0, tvga), 400, 413);
    /* From just after a sync (first and last reset, the picture off, the
       row still counting on): 21 counts to the total 504 (vc 0, row 0, the
       picture on), then 483 more: 480 lines after a whole frame of 504. */
    sync_case("640x480 from its sync", scan(483, 0, 0, 483, 2000, 0, t640), 480, 504);
}

static void
overrun(void)
{
    /* An 800x600 frame at line 560 of its picture (rows 0-559 drawn) when
       the timings become 640x480's. The counter is past 480, 483 and 504,
       each an equality, so it counts 561-2047 (1487), wraps to 0 (1) and
       counts 1-483 (483): 1971 scanlines. The picture never ends until
       vc 480: rows are drawn at vc 560-2047 and 0-479, the row equal to vc
       up to 2000, then wrapped past 2000 to 0 at vc 2001 (vc 2047 is row
       46, vc 0 row 47, vc 479 row 526). Last 2000, first 0: 2001 lines. */
    sync_case("800x600 to 640x480 at 560", scan(560, 0, 1, 560, 0, 559, t640), 2001, 1971);
    /* 1024x768 at 700 becoming 800x600: past 600, 603 and 630; (2048 - 700)
       + 603 = 1951 scanlines, the rows again up to 2000: 2001 lines. */
    sync_case("1024x768 to 800x600 at 700", scan(700, 0, 1, 700, 0, 699, t800), 2001, 1951);
}

static void
shrinks(void)
{
    /* 800x600 at 481 (rows 0-480 drawn) becoming 640x480: past the new end
       (passed, not met: the picture stays on), before the sync. Rows 481
       and 482 drawn, the count to 483 is the sync: rows 0-482, 483 lines
       (wy 482), after 2 scanlines. */
    sync_case("800x600 to 640x480 at 481", scan(481, 0, 1, 481, 0, 480, t640), 483, 2);
    /* 800x600 at 490 becoming 640x480: past the sync, before the total.
       Rows 490-503 drawn, the count to 504 is the total (vc 0, row 0, the
       picture on) with first 0 and last 503 kept; rows 0-479 drawn (none
       above 503), the end at 480, the sync at 483: 503 - 0 + 1 = 504
       lines, after 14 + 483 = 497 scanlines. */
    sync_case("800x600 to 640x480 at 490", scan(490, 0, 1, 490, 0, 489, t640), 504, 497);
    /* 800x600 at 620, in its blanking after its sync (first 2000, last 0),
       becoming 640x480: past the total, so the counter runs to 2047 and
       wraps ((2048 - 620) = 1428), then meets the end at 480 and the sync
       at 483 (1911 scanlines) without meeting the total: the picture never
       starts, nothing is drawn, last - first is -2000 and svga_doblit
       shows nothing (wy <= 0): 0. The frame after it is "640x480 from its
       sync" above. */
    sync_case("800x600 to 640x480 at 620, blanking", scan(620, 0, 0, 620, 2000, 0, t640), 0, 1911);
}

static void
growth(void)
{
    /* 640x480 at 300 (rows 0-299 drawn) becoming 800x600: rows 300-599,
       the end at 600, the sync at 603: 600 lines after 303 scanlines. */
    sync_case("640x480 to 800x600 at 300", scan(300, 0, 1, 300, 0, 299, t800), 600, 303);
    /* 640x480 at 490, in its blanking after its sync, becoming 800x600: the
       counter meets the new end at 600 (the picture already off) and the
       sync at 603 before any total: nothing drawn, 0 after 113. */
    sync_case("640x480 to 800x600 at 490, blanking", scan(490, 0, 0, 490, 2000, 0, t800), 0, 113);
}

static void
engine_on(void)
{
    /* The VGA's text frame at 420, in its blanking after its sync at 413
       (first 2000, last 0), becoming 800x600's: the counter meets the end
       at 600 and the sync at 603 with the picture off: 0 after 183. */
    sync_case("VGA to 800x600 at 420, blanking", scan(420, 0, 0, 420, 2000, 0, t800), 0, 183);
    /* The one after it, from that sync: 27 counts to the total 630, then
       603: 600 lines after 630 scanlines, a steady frame. */
    sync_case("800x600 after the VGA's", scan(603, 0, 0, 603, 2000, 0, t800), 600, 630);
    /* The VGA's text frame at 200 of its picture (rows 0-199 drawn): rows
       200-599, the end at 600, the sync at 603: 600 lines after 403. */
    sync_case("VGA to 800x600 at 200", scan(200, 0, 1, 200, 0, 199, t800), 600, 403);
}

static void
halves(void)
{
    /* The second half of a scanline: the line drawn (last takes it), the
       row one on, the count still to come. Drawing doesn't depend on the
       timings and the next count is the same, so either half of the same
       scan counts the same. The overrun's scan at 560, second half: */
    sync_case("overrun at 560, second half", scan(560, 1, 1, 561, 0, 560, t640), 2001, 1971);
    /* At the row's wrap, the overrun's scan at 2000: first half (row 2000
       to draw) and second (drawn, the row one on wrapped to 0): rows up to
       2000 either way, (2048 - 2000) + 483 = 531 scanlines. */
    sync_case("overrun at 2000, first half", scan(2000, 0, 1, 2000, 0, 1999, t640), 2001, 531);
    sync_case("overrun at 2000, second half", scan(2000, 1, 1, 0, 0, 2000, t640), 2001, 531);
    /* 800x600 at 482 becoming 640x480: row 482 drawn in either half, the
       count to 483 is the sync: rows 0-482, 483 lines after 1 scanline. */
    sync_case("800x600 to 640x480 at 482, first half", scan(482, 0, 1, 482, 0, 481, t640), 483, 1);
    sync_case("800x600 to 640x480 at 482, second half", scan(482, 1, 1, 483, 0, 482, t640), 483, 1);
    /* Where one line matters: one count on, 483 in its first half (the
       count to 483 made under 800x600's timings, no sync). Past the new
       sync, before the total: rows 483-503, the total at 504, rows 0-479
       (last stays 503), the sync at 483: 504 lines after 21 + 483 = 504. */
    sync_case("800x600 to 640x480 at 483, first half", scan(483, 0, 1, 483, 0, 482, t640), 504, 504);
}

/* The card's frame end: s is ended (or not,
   by `want`), then its next sync counts `frame` lines after `lines`. */
static void
end_case(const char *what, vgpu98_dd_scan_t s, int want, int frame, int lines)
{
    const vgpu98_dd_scan_t before = s;
    const int              got    = vgpu98_dd_end_frame(&s);

    CHECK(got == want, "%s: ended %d, expected %d", what, got, want);
    if (got)
        CHECK((s.vc == s.dispend - 1) && !s.dispon && (s.firstline == 2000) && (s.lastline == 0) &&
                  (s.linepos == before.linepos) && (s.displine == before.displine),
              "%s: ended at %d, picture %d, rows %d-%d", what, s.vc, s.dispon, s.firstline, s.lastline);
    else
        CHECK(!memcmp(&s, &before, sizeof(s)), "%s: changed though not ended", what);
    sync_case(what, s, frame, lines);
}

static void
end_frame(void)
{
    static const int *const modes[4] = { t640, t800, t1024, tvga };
    vgpu98_dd_scan_t        s;
    int                     m;
    int                     vc;
    int                     pos;

    /* The overrun, ended: the counter at 479 out of the picture, so the
       counts to 480 (the blank), 481, 482 and 483 (the sync): no frame
       after 4 scanlines, instead of 2001 lines after 1971. */
    end_case("overrun at 560, ended", scan(560, 0, 1, 560, 0, 559, t640), 1, 0, 4);
    /* The same in the scanline's second half: the count first, the same 4. */
    end_case("overrun at 560, second half, ended", scan(560, 1, 1, 561, 0, 560, t640), 1, 0, 4);
    /* The frame after the ended one, from its sync at 483 (the rows on at
       564): 21 counts to the total 504, then rows 0-479 and the sync at
       483: a whole 640x480 frame, 480 lines after 504. */
    sync_case("the frame after the ended one", scan(483, 0, 0, 564, 2000, 0, t640), 480, 504);
    /* Past the new picture's end in the picture (481, a frame of 483 lines
       before) and past the new sync (490, 504 lines before): ended, no
       frame after 4. */
    end_case("800x600 to 640x480 at 481, ended", scan(481, 0, 1, 481, 0, 480, t640), 1, 0, 4);
    end_case("800x600 to 640x480 at 490, ended", scan(490, 0, 1, 490, 0, 489, t640), 1, 0, 4);
    /* Past the total in the blanking (620): ended, no frame after 4 rather
       than after 1911, the run past 2047 gone. */
    end_case("800x600 to 640x480 at 620, blanking, ended", scan(620, 0, 0, 620, 2000, 0, t640), 1, 0, 4);
    /* The engine off at 500 of an 800x600 picture, the VGA's text frame
       (400, 413, 449) after: ended at 399, the counts to 400 and on to 413:
       no frame after 14. */
    end_case("800x600 to VGA text at 500, ended", scan(500, 0, 1, 500, 0, 499, tvga), 1, 0, 14);
    /* Inside the new frame: left alone (600 lines after 303, as before). */
    end_case("640x480 to 800x600 at 300, left", scan(300, 0, 1, 300, 0, 299, t800), 0, 600, 303);
    /* The VGA's blanking at 420 becoming 800x600: before the new end, left
       (no frame after 183, as before). */
    end_case("VGA to 800x600 at 420, blanking, left", scan(420, 0, 0, 420, 2000, 0, t800), 0, 0, 183);
    /* A normal frame's blanking, past its sync but before its total: left.
       14 counts to the total 504, then rows 0-479 and the sync at 483: the
       next frame whole, 480 lines after 497. */
    end_case("640x480 blanking at 490, left", scan(490, 0, 0, 490, 2000, 0, t640), 0, 480, 497);
    /* Ended twice: the second leaves it. */
    s = scan(560, 0, 1, 560, 0, 559, t640);
    vgpu98_dd_end_frame(&s);
    CHECK(!vgpu98_dd_end_frame(&s), "ended twice");
    /* No picture at all (an end at 0): left. */
    s         = scan(10, 0, 1, 10, 0, 9, t640);
    s.dispend = 0;
    CHECK(!vgpu98_dd_end_frame(&s), "a picture's end of 0 ended");

    /* Every line of a steady frame, in either half: in the picture before
       its end, in the blanking before its total, never ended. */
    for (m = 0; m < 4; m++) {
        for (vc = 0; vc < modes[m][2]; vc++) {
            for (pos = 0; pos < 2; pos++) {
                s = scan(vc, pos, vc < modes[m][0], vc + pos, 0, vc, modes[m]);
                CHECK(!vgpu98_dd_end_frame(&s), "steady frame %d at %d (half %d) ended", modes[m][0], vc, pos);
            }
        }
    }
}

static void
no_sync(void)
{
    vgpu98_dd_scan_t s = scan(0, 0, 1, 0, 2000, 0, t640);
    int              n = 0;
    int              got;

    /* The counter is 11 bits: it never reaches 3000. */
    s.vsyncstart = 3000;
    got          = vgpu98_dd_next_sync(&s, &n);
    CHECK(got == -1, "sync at 3000: %d, expected -1", got);
    /* No line count wanted. */
    s   = scan(0, 0, 1, 0, 2000, 0, t640);
    got = vgpu98_dd_next_sync(&s, NULL);
    CHECK(got == 480, "no line count: %d", got);
}

static void
stretched(void)
{
    static const struct {
        int lines, rows, want;
    } t[] = {
        { 600, 480, 1 },  /* 600 * 4 = 2400 = 480 * 5 */
        { 599, 480, 0 },  /* 2396 */
        { 504, 480, 0 },  /* the shrink past the sync's frame */
        { 2001, 480, 1 }, /* the overrun's */
        { 480, 480, 0 },  { 750, 600, 1 }, { 749, 600, 0 }, { 480, 0, 0 }, { 0, 0, 0 },
    };
    size_t i;

    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++)
        CHECK((vgpu98_dd_stretched(t[i].lines, t[i].rows) != 0) == t[i].want, "%d lines of %d rows: %d", t[i].lines,
              t[i].rows, vgpu98_dd_stretched(t[i].lines, t[i].rows));
}

static void
put16(uint8_t *vram, uint32_t a, uint32_t v)
{
    vram[a]     = (uint8_t) v;
    vram[a + 1] = (uint8_t) (v >> 8);
}

/* A 16 or 15 bpp pixel at 0 of a fresh video memory. */
static uint32_t
px16(uint32_t v, uint32_t bpp)
{
    uint8_t vram[16];

    memset(vram, 0, sizeof(vram));
    put16(vram, 0, v);
    return vgpu98_dd_pixel(vram, 15, 0, bpp, NULL);
}

static void
pixels(void)
{
    uint8_t  vram[16];
    uint8_t  out[4];
    uint32_t palette[256];
    uint32_t p;

    /* 16 bpp: 5:6:5 by display.comp's (v * 255) / 31 and / 63, truncated. */
    CHECK(px16(0xf81f, 16) == 0xff00ffu, "16 bpp F81F: %06X", px16(0xf81f, 16));
    CHECK(px16(0x07e0, 16) == 0x00ff00u, "16 bpp 07E0: %06X", px16(0x07e0, 16));
    CHECK(px16(0x001f, 16) == 0x0000ffu, "16 bpp 001F: %06X", px16(0x001f, 16));
    /* 1, 2, 1: 255 / 31 = 8, 510 / 63 = 8. */
    CHECK(px16(0x0841, 16) == 0x080808u, "16 bpp 0841: %06X", px16(0x0841, 16));
    /* 16, 32, 15: 4080 / 31 = 131, 8160 / 63 = 129, 3825 / 31 = 123
       (rounding would give 132 and 130). */
    CHECK(px16(0x840f, 16) == 0x83817bu, "16 bpp 840F: %06X", px16(0x840f, 16));
    /* 15 bpp: 5:5:5, bit 15 ignored. */
    CHECK(px16(0x7c1f, 15) == 0xff00ffu, "15 bpp 7C1F: %06X", px16(0x7c1f, 15));
    CHECK(px16(0xfc1f, 15) == 0xff00ffu, "15 bpp FC1F: %06X", px16(0xfc1f, 15));
    CHECK(px16(0x03e0, 15) == 0x00ff00u, "15 bpp 03E0: %06X", px16(0x03e0, 15));
    CHECK(px16(0x4210, 15) == 0x838383u, "15 bpp 4210: %06X", px16(0x4210, 15)); /* 16 each: 131 */
    /* 32 bpp as stored, B G R, the fourth byte not part of it. */
    memset(vram, 0, sizeof(vram));
    vram[4] = 0x11;
    vram[5] = 0x22;
    vram[6] = 0x33;
    vram[7] = 0x44;
    p       = vgpu98_dd_pixel(vram, 15, 4, 32, NULL);
    CHECK(p == 0x332211u, "32 bpp 11 22 33 44: %08X", p);
    /* 8 bpp through the palette, its top byte not part of the colour. */
    memset(palette, 0, sizeof(palette));
    palette[5] = 0xab123456u;
    vram[3]    = 5;
    p          = vgpu98_dd_pixel(vram, 15, 3, 8, palette);
    CHECK(p == 0x123456u, "8 bpp index 5: %08X", p);
    /* Addresses wrap by the mask: 8 bpp at 16 + 3 is the byte at 3. */
    p = vgpu98_dd_pixel(vram, 15, 19, 8, palette);
    CHECK(p == 0x123456u, "8 bpp at 19 of 16: %08X", p);
    /* A pixel straddling the end: 16 bpp at 15 takes its high byte from
       0, 32 bpp at 14 its red from 0. */
    memset(vram, 0, sizeof(vram));
    vram[15] = 0x1f;
    vram[0]  = 0xf8;
    p        = vgpu98_dd_pixel(vram, 15, 15, 16, NULL);
    CHECK(p == 0xff00ffu, "16 bpp across the end: %06X", p);
    memset(vram, 0, sizeof(vram));
    vram[14] = 0x11;
    vram[15] = 0x22;
    vram[0]  = 0x33;
    p        = vgpu98_dd_pixel(vram, 15, 14, 32, NULL);
    CHECK(p == 0x332211u, "32 bpp across the end: %08X", p);
    /* 86Box's pixel as R, G, B and opaque. */
    vgpu98_dd_rgba(0xab123456u, out);
    CHECK((out[0] == 0x12) && (out[1] == 0x34) && (out[2] == 0x56) && (out[3] == 0xff), "RGBA: %02X %02X %02X %02X", out[0],
          out[1], out[2], out[3]);
}

/* rgba holds n pixels of R, G, B, 255 as `want` (0x00RRGGBB) has them. */
static int
row_is(const uint8_t *rgba, const uint32_t *want, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++) {
        if ((rgba[i * 4] != (uint8_t) (want[i] >> 16)) || (rgba[i * 4 + 1] != (uint8_t) (want[i] >> 8)) ||
            (rgba[i * 4 + 2] != (uint8_t) want[i]) || (rgba[i * 4 + 3] != 0xff))
            return 0;
    }
    return 1;
}

static void
rows(void)
{
    static const uint32_t w8[3]  = { 0x000000u, 0x112233u, 0x445566u };
    static const uint32_t w15[3] = { 0xff0000u, 0x00ff00u, 0x0000ffu };
    static const uint32_t w32[2] = { 0x302010u, 0x706050u };
    uint8_t               vram[16];
    uint8_t               rgba[3 * 4];
    uint32_t              palette[256];

    memset(palette, 0, sizeof(palette));
    palette[1] = 0x112233u;
    palette[2] = 0xff445566u;
    memset(vram, 0, sizeof(vram));
    vram[8] = 0;
    vram[9] = 1;
    vram[10] = 2;
    memset(rgba, 0xee, sizeof(rgba));
    vgpu98_dd_decode_row(vram, 15, 8, 3, 8, palette, rgba);
    CHECK(row_is(rgba, w8, 3), "8 bpp row: %02X %02X %02X %02X / %02X %02X %02X %02X", rgba[0], rgba[1], rgba[2], rgba[3],
          rgba[4], rgba[5], rgba[6], rgba[7]);

    /* Red, green, blue (bit 15 set on the blue). */
    memset(vram, 0, sizeof(vram));
    put16(vram, 2, 0x7c00);
    put16(vram, 4, 0x03e0);
    put16(vram, 6, 0x801f);
    memset(rgba, 0xee, sizeof(rgba));
    vgpu98_dd_decode_row(vram, 15, 2, 3, 15, NULL, rgba);
    CHECK(row_is(rgba, w15, 3), "15 bpp row: %02X %02X %02X %02X / %02X %02X %02X %02X", rgba[0], rgba[1], rgba[2], rgba[3],
          rgba[4], rgba[5], rgba[6], rgba[7]);

    /* The same in 5:6:5, its last pixel past the end, at 0 again. */
    memset(vram, 0, sizeof(vram));
    put16(vram, 12, 0xf800);
    put16(vram, 14, 0x07e0);
    put16(vram, 0, 0x001f);
    memset(rgba, 0xee, sizeof(rgba));
    vgpu98_dd_decode_row(vram, 15, 12, 3, 16, NULL, rgba);
    CHECK(row_is(rgba, w15, 3), "16 bpp row: %02X %02X %02X %02X / %02X %02X %02X %02X", rgba[0], rgba[1], rgba[2], rgba[3],
          rgba[4], rgba[5], rgba[6], rgba[7]);

    /* 4 bytes a pixel, B G R and a fourth byte. */
    memset(vram, 0, sizeof(vram));
    vram[0] = 0x10;
    vram[1] = 0x20;
    vram[2] = 0x30;
    vram[3] = 0x40;
    vram[4] = 0x50;
    vram[5] = 0x60;
    vram[6] = 0x70;
    vram[7] = 0x80;
    memset(rgba, 0xee, sizeof(rgba));
    vgpu98_dd_decode_row(vram, 15, 0, 2, 32, NULL, rgba);
    CHECK(row_is(rgba, w32, 2), "32 bpp row: %02X %02X %02X %02X / %02X %02X %02X %02X", rgba[0], rgba[1], rgba[2], rgba[3],
          rgba[4], rgba[5], rgba[6], rgba[7]);
    CHECK(rgba[8] == 0xee, "32 bpp row wrote past its 2 pixels");
}

static void
hash(void)
{
    uint32_t a[4] = { 1, 2, 3, 4 };
    uint32_t b[4] = { 1, 2, 3, 4 };
    uint32_t one  = 0x61;
    uint64_t h    = vgpu98_dd_hash(VGPU98_DD_HASH_START, a, 4);
    uint64_t g;

    g = vgpu98_dd_hash(VGPU98_DD_HASH_START, a, 0);
    CHECK(g == VGPU98_DD_HASH_START, "empty: %016llX", (unsigned long long) g);
    /* One step of FNV-1a 64's mixing: its published value for "a". */
    g = vgpu98_dd_hash(VGPU98_DD_HASH_START, &one, 1);
    CHECK(g == 0xaf63dc4c8601ec8cull, "one pixel 61h: %016llX", (unsigned long long) g);
    CHECK(vgpu98_dd_hash(VGPU98_DD_HASH_START, b, 4) == h, "the same pixels hash differently");
    b[2] = 5;
    CHECK(vgpu98_dd_hash(VGPU98_DD_HASH_START, b, 4) != h, "a changed pixel hashes the same");
    b[1] = 3;
    b[2] = 2;
    CHECK(vgpu98_dd_hash(VGPU98_DD_HASH_START, b, 4) != h, "two swapped pixels hash the same");
    g = vgpu98_dd_hash(vgpu98_dd_hash(VGPU98_DD_HASH_START, a, 2), a + 2, 2);
    CHECK(g == h, "in two runs: %016llX, in one %016llX", (unsigned long long) g, (unsigned long long) h);
}

/* VSYNC_CONTROL's fields: Direct3D's at bit 0, OpenGL's at
   2, Glide's at 4 (cardc's VSYNC_D3D_SHIFT, VSYNC_OGL_SHIFT and
   VSYNC_GLIDE_SHIFT). The vertical blanks each value of a field gives for
   a wait asked of 0 (none) to 4 (Glide's cap), by hand. */
static const int         vs_shift[3]   = { 0, 2, 4 };
static const char *const vs_api[3]     = { "Direct3D", "OpenGL", "Glide" };
static const uint32_t    vs_want[4][5] = {
    { 0, 1, 2, 3, 4 }, /* 0, application's choice: as asked */
    { 1, 1, 2, 3, 4 }, /* 1, always on: no wait becomes one, a longer one kept */
    { 0, 0, 0, 0, 0 }, /* 2, always off: no wait */
    { 0, 1, 2, 3, 4 }, /* 3, reserved: read as 0 */
};

/* VSYNC_CONTROL of Direct3D's field d, OpenGL's o, Glide's g and `high`
   above bit 5: each API's waits are its own field's, whatever the others. */
static void
vsync_register(uint32_t d, uint32_t o, uint32_t g, uint32_t high)
{
    const uint32_t control  = d | (o << 2) | (g << 4) | high;
    const uint32_t field[3] = { d, o, g };
    uint32_t       asked;
    uint32_t       got;
    int            api;

    for (api = 0; api < 3; api++) {
        for (asked = 0; asked <= 4; asked++) {
            got = vgpu98_vsync_interval(control, vs_shift[api], asked);
            CHECK(got == vs_want[field[api]][asked], "VSYNC_CONTROL %08X, %s asked %u: %u, expected %u", control,
                  vs_api[api], asked, got, vs_want[field[api]][asked]);
        }
    }
}

static void
vsync(void)
{
    static const struct {
        uint32_t control;
        int      shift;
        uint32_t asked, want;
    } t[] = {
        /* 18h: Direct3D 0, OpenGL 2, Glide 1. Direct3D as asked, OpenGL
           never waits, Glide waits at least one. */
        { 0x18, 0, 0, 0 }, { 0x18, 0, 3, 3 }, { 0x18, 2, 1, 0 }, { 0x18, 2, 4, 0 }, { 0x18, 4, 0, 1 }, { 0x18, 4, 2, 2 },
        /* 06h: Direct3D 2, OpenGL 1, Glide 0. */
        { 0x06, 0, 2, 0 }, { 0x06, 2, 0, 1 }, { 0x06, 4, 0, 0 }, { 0x06, 4, 3, 3 },
        /* 21h: Direct3D 1, OpenGL 0, Glide 2. */
        { 0x21, 0, 0, 1 }, { 0x21, 2, 0, 0 }, { 0x21, 2, 1, 1 }, { 0x21, 4, 4, 0 },
        /* 3Fh: all three 3, reserved: as asked. */
        { 0x3f, 0, 0, 0 }, { 0x3f, 2, 2, 2 }, { 0x3f, 4, 0, 0 }, { 0x3f, 4, 3, 3 },
        /* Every bit above 5 set, the fields 18h's: the same as 18h. */
        { 0xffffffd8u, 0, 3, 3 }, { 0xffffffd8u, 2, 1, 0 }, { 0xffffffd8u, 4, 0, 1 },
        /* Bit 6 alone, just above Glide's field: Glide as asked. */
        { 0x40, 4, 0, 0 }, { 0x40, 4, 2, 2 },
    };
    static const uint32_t high[] = { 0, 0x40, 0x80, 0xc0, 0x100, 0x8000u, 0x80000000u, 0xffffffc0u };
    uint32_t              v;
    uint32_t              d;
    uint32_t              o;
    uint32_t              g;
    uint32_t              asked;
    uint32_t              got;
    size_t                i;
    int                   api;

    /* Each value of each field alone, the other two 0. */
    for (api = 0; api < 3; api++) {
        for (v = 0; v < 4; v++) {
            for (asked = 0; asked <= 4; asked++) {
                got = vgpu98_vsync_interval(v << vs_shift[api], vs_shift[api], asked);
                CHECK(got == vs_want[v][asked], "%s %u alone, asked %u: %u, expected %u", vs_api[api], v, asked, got,
                      vs_want[v][asked]);
            }
        }
    }
    /* Settings worked by hand. */
    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        got = vgpu98_vsync_interval(t[i].control, t[i].shift, t[i].asked);
        CHECK(got == t[i].want, "VSYNC_CONTROL %08X at %d, asked %u: %u, expected %u", t[i].control, t[i].shift, t[i].asked,
              got, t[i].want);
    }
    /* The fields apart: every setting of the three, each API's waits its
       own field's; and with bits above 5 set, the same. */
    for (i = 0; i < sizeof(high) / sizeof(high[0]); i++)
        for (d = 0; d < 4; d++)
            for (o = 0; o < 4; o++)
                for (g = 0; g < 4; g++)
                    vsync_register(d, o, g, high[i]);
}

/* A Direct3D FLIP's flags as the card takes them through the rule:
   IMMEDIATE is an asked wait of 0, any other
   FLIP asks its interval (at least 1, as exec_flip takes it), and a wait
   of 0 is IMMEDIATE. */
static uint32_t
flip_flags(uint32_t control, uint32_t flags)
{
    uint32_t interval = (flags & VGPU98_FLIP_INTERVAL_MASK) >> VGPU98_FLIP_INTERVAL_SHIFT;
    uint32_t got;

    if (interval < 1)
        interval = 1;
    got = vgpu98_vsync_interval(control, vs_shift[0], (flags & VGPU98_FLIP_IMMEDIATE) ? 0 : interval);
    return got ? (got << VGPU98_FLIP_INTERVAL_SHIFT) : VGPU98_FLIP_IMMEDIATE;
}

static void
vsync_flip(void)
{
    const uint32_t now   = VGPU98_FLIP_IMMEDIATE;
    const uint32_t wait1 = 1u << VGPU98_FLIP_INTERVAL_SHIFT;
    const uint32_t wait3 = 3u << VGPU98_FLIP_INTERVAL_SHIFT;

    /* Always on: an immediate flip waits one vertical blank. */
    CHECK(flip_flags(1, now) == wait1, "always on, immediate: flags %X, expected %X", flip_flags(1, now), wait1);
    /* Always off: a wait of 3 becomes immediate. */
    CHECK(flip_flags(2, wait3) == now, "always off, wait 3: flags %X, expected %X", flip_flags(2, wait3), now);
    /* The application's choice keeps both. */
    CHECK(flip_flags(0, now) == now, "application's choice, immediate: flags %X", flip_flags(0, now));
    CHECK(flip_flags(0, wait3) == wait3, "application's choice, wait 3: flags %X", flip_flags(0, wait3));
}

int
main(void)
{
    steady();
    overrun();
    shrinks();
    growth();
    engine_on();
    halves();
    end_frame();
    no_sync();
    stretched();
    pixels();
    rows();
    hash();
    vsync();
    vsync_flip();
    printf("test_dispdiag: %d failed\n", failures);
    return failures ? 1 : 0;
}

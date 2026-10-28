/*
 * fontc: VGPU98 font compiler.
 *
 * Reads a text-art font (format in spec/font-format.md), checks it, and writes a
 * NASM include file with one byte per scan line, plus an optional PNG preview.
 *
 * Usage: fontc FONT --inc FILE --label NAME [--png FILE]
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define WIDTH      8
#define MAX_HEIGHT 32

enum { UNDEFINED, FROM_BASE, EXPLICIT };

typedef struct font_t {
    char    *path;
    int      height;
    int      state[256];
    int      lost[256]; /* derived glyphs: 1 + a dropped row that carried pixels, or 0 */
    int      line[256];
    uint8_t  rows[256][MAX_HEIGHT];
    int      have_lines;
    uint32_t h_single; /* row masks of horizontal box-drawing lines */
    uint32_t h_double;
    uint8_t  v_single; /* column masks of vertical box-drawing lines (bit 7 = column 0) */
    uint8_t  v_double;
} font_t;

static VGPU98_NORETURN void
font_error(const font_t *font, int line, int code, const char *fmt, ...) VGPU98_PRINTF(4, 5);

static void
font_error(const font_t *font, int line, int code, const char *fmt, ...)
{
    char    msg[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (code >= 0)
        die("%s:%d: glyph %02Xh: %s", font->path, line, code, msg);
    die("%s:%d: %s", font->path, line, msg);
}

static char *
dir_of(const char *path)
{
    char *copy = xstrdup(path);
    char *cut  = strrchr(copy, '/');
    char *bs   = strrchr(copy, '\\');

    if ((bs != NULL) && ((cut == NULL) || (bs > cut)))
        cut = bs;
    if (cut == NULL) {
        free(copy);
        return xstrdup(".");
    }
    *cut = 0;
    return copy;
}

/* Reads a non-negative decimal number from *p, skipping leading blanks. */
static int
read_int(const char **p, int *out)
{
    const char *s = *p;
    int         v = 0;

    while ((*s == ' ') || (*s == '\t'))
        s++;
    if ((*s < '0') || (*s > '9'))
        return 0;
    while ((*s >= '0') && (*s <= '9')) {
        v = v * 10 + (*s - '0');
        if (v > 1000)
            return 0;
        s++;
    }
    *p   = s;
    *out = v;
    return 1;
}

static void load_font(font_t *font, const char *path);

/* How deep 'derive' may chain (a font derived from a derived font, and so on).
   Also stops a font that derives from itself. */
#define MAX_DERIVE_DEPTH 4
static int derive_depth;

/* derive FILE drop-rows A B ...: every glyph of another font with some rows removed. */
static void
derive(font_t *font, int line, const char *args)
{
    font_t     *base = xcalloc(1, sizeof(font_t));
    char       *dir  = dir_of(font->path);
    char        name[256];
    const char *p = args;
    uint32_t    drop = 0;
    char       *path;
    size_t      len;
    int         n = 0;
    int         c;
    int         v;

    while ((*p == ' ') || (*p == '\t'))
        p++;
    while (*p && (*p != ' ') && (*p != '\t') && (n < (int) sizeof(name) - 1))
        name[n++] = *p++;
    name[n] = 0;
    while ((*p == ' ') || (*p == '\t'))
        p++;
    if ((n == 0) || (strncmp(p, "drop-rows", 9) != 0))
        font_error(font, line, -1, "expected 'derive FILE drop-rows N ...'");
    p += 9;

    len  = strlen(dir) + strlen(name) + 2;
    path = xmalloc(len);
    snprintf(path, len, "%s/%s", dir, name);
    if (++derive_depth > MAX_DERIVE_DEPTH)
        font_error(font, line, -1, "'derive' chains more than 4 fonts deep (does a font derive from itself?)");
    load_font(base, path);
    derive_depth--;

    while (read_int(&p, &v)) {
        if (v >= base->height)
            font_error(font, line, -1, "drop-rows: row outside the base font");
        drop |= 1u << v;
    }
    if (*p)
        font_error(font, line, -1, "drop-rows: expected row numbers");

    for (c = 0; c < 256; c++) {
        int out = 0;
        int r;

        /* A dropped row loses nothing if it repeats its neighbour on the inside
           (a vertical line running off the cell edge). Shades, box drawing and
           blocks (B0h-DFh) are cell patterns and may be cut anywhere. */
        for (r = 0; r < base->height; r++) {
            int inner = (r < base->height / 2) ? r + 1 : r - 1;

            if ((drop & (1u << r)) && base->rows[c][r] && (base->rows[c][r] != base->rows[c][inner]) &&
                ((c < 0xb0) || (c > 0xdf)) && !font->lost[c])
                font->lost[c] = r + 1;
        }

        for (r = 0; r < base->height; r++) {
            if (drop & (1u << r))
                continue;
            if (out < font->height)
                font->rows[c][out] = base->rows[c][r];
            out++;
        }
        if (out != font->height)
            font_error(font, line, -1, "the derived height does not match the 'font' line");
        font->state[c] = FROM_BASE;
        font->line[c]  = line;
    }

    free(path);
    free(dir);
    free(base->path);
    free(base);
}

static void
load_font(font_t *font, const char *path)
{
    char *text      = (char *) read_file(path, NULL);
    char *line      = text;
    int   line_no   = 0;
    int   glyph     = -1; /* glyph receiving pixel rows, or -1 */
    int   next_row  = 0;
    int   derived   = 0;
    int   seen_font = 0;
    int   c;

    font->path = xstrdup(path);
    if ((unsigned char) text[0] == 0xef && (unsigned char) text[1] == 0xbb && (unsigned char) text[2] == 0xbf)
        font_error(font, 1, -1, "the file starts with a UTF-8 byte order mark; save it as plain ASCII");

    while (line != NULL) {
        char *next = strchr(line, '\n');
        char *end;
        char *p;

        if (next != NULL)
            *next++ = 0;
        line_no++;
        end = line + strlen(line);
        while ((end > line) && ((end[-1] == '\r') || (end[-1] == ' ') || (end[-1] == '\t')))
            *--end = 0;
        p    = line;
        line = next;

        if ((*p == '.') || (*p == '#')) {
            uint8_t bits = 0;
            int     i;

            if (glyph < 0)
                font_error(font, line_no, -1, "pixel row outside a glyph");
            if (strlen(p) != WIDTH)
                font_error(font, line_no, glyph, "pixel rows are exactly 8 characters of '.' and '#'");
            if (next_row >= font->height)
                font_error(font, line_no, glyph, "more pixel rows than the font height");
            for (i = 0; i < WIDTH; i++) {
                if ((p[i] != '.') && (p[i] != '#'))
                    font_error(font, line_no, glyph, "pixel rows are exactly 8 characters of '.' and '#'");
                if (p[i] == '#')
                    bits |= (uint8_t) (0x80 >> i);
            }
            font->rows[glyph][next_row++] = bits;
            continue;
        }

        /* Any other line ends the current glyph. */
        glyph = -1;
        if ((*p == 0) || (*p == ';'))
            continue;

        if (strncmp(p, "font ", 5) == 0) {
            const char *q = p + 5;
            int         w;
            int         h;

            if (seen_font || !read_int(&q, &w) || (*q++ != 'x') || !read_int(&q, &h) || *q || (w != WIDTH) ||
                (h < 1) || (h > MAX_HEIGHT))
                font_error(font, line_no, -1, "expected one 'font 8xH' line, H from 1 to 32");
            font->height = h;
            seen_font    = 1;
        } else if (strncmp(p, "derive ", 7) == 0) {
            if (!seen_font || derived)
                font_error(font, line_no, -1, "'derive' comes once, after the 'font' line");
            derive(font, line_no, p + 7);
            derived = 1;
        } else if (strncmp(p, "lines ", 6) == 0) {
            const char *q = p + 6;
            int         v[6];
            int         i;

            if (!seen_font || font->have_lines)
                font_error(font, line_no, -1, "'lines' comes once, after the 'font' line");
            for (i = 0; i < 6; i++) {
                if (!read_int(&q, &v[i]) || (v[i] >= ((i < 3) ? font->height : WIDTH)))
                    font_error(font, line_no, -1, "expected 'lines HS HD1 HD2 VS VD1 VD2' inside the cell");
            }
            if (*q)
                font_error(font, line_no, -1, "expected 'lines HS HD1 HD2 VS VD1 VD2' inside the cell");
            font->h_single   = 1u << v[0];
            font->h_double   = (1u << v[1]) | (1u << v[2]);
            font->v_single   = (uint8_t) (0x80 >> v[3]);
            font->v_double   = (uint8_t) ((0x80 >> v[4]) | (0x80 >> v[5]));
            font->have_lines = 1;
        } else if (strncmp(p, "glyph ", 6) == 0) {
            const char *q = p + 6;
            char       *hex_end;
            long        code;

            if (!seen_font)
                font_error(font, line_no, -1, "'glyph' comes after the 'font' line");
            if (!isxdigit((unsigned char) q[0]) || !isxdigit((unsigned char) q[1]))
                font_error(font, line_no, -1, "expected 'glyph XX [@ROW] [label]' with a two-digit hex code");
            code = strtol(q, &hex_end, 16);
            if ((hex_end - q != 2) || (code < 0) || (code > 255) || (*hex_end && (*hex_end != ' ')))
                font_error(font, line_no, -1, "expected 'glyph XX [@ROW] [label]' with a two-digit hex code");
            if (font->state[code] == EXPLICIT)
                font_error(font, line_no, (int) code, "defined twice");

            font->state[code] = EXPLICIT;
            font->line[code]  = line_no;
            memset(font->rows[code], 0, sizeof(font->rows[code]));
            glyph    = (int) code;
            next_row = 0;

            q = hex_end;
            while (*q == ' ')
                q++;
            if (*q == '@') {
                q++;
                if (!read_int(&q, &next_row) || (next_row >= font->height))
                    font_error(font, line_no, glyph, "@ROW must be a row inside the cell");
            }
        } else
            font_error(font, line_no, -1, "unknown line '%.40s'", p);
    }

    if (!seen_font)
        font_error(font, line_no, -1, "missing 'font 8xH' line");
    for (c = 0; c < 256; c++) {
        if ((font->state[c] == FROM_BASE) && font->lost[c])
            font_error(font, font->line[c], c,
                       "deriving drops row %d, which has pixels; draw the glyph again below the derive line",
                       font->lost[c] - 1);
    }
    free(text);
}

/* ---------------------------------------------------------------------------
 * Checks
 * ------------------------------------------------------------------------- */

/* Box-drawing connections for code page 437 B3h-DAh: up, down, left, right.
 * 0 = none, 1 = single line, 2 = double line. */
static const uint8_t box[0xda - 0xb3 + 1][4] = {
    /* B3 */ { 1, 1, 0, 0 }, /* B4 */ { 1, 1, 1, 0 }, /* B5 */ { 1, 1, 2, 0 }, /* B6 */ { 2, 2, 1, 0 },
    /* B7 */ { 0, 2, 1, 0 }, /* B8 */ { 0, 1, 2, 0 }, /* B9 */ { 2, 2, 2, 0 }, /* BA */ { 2, 2, 0, 0 },
    /* BB */ { 0, 2, 2, 0 }, /* BC */ { 2, 0, 2, 0 }, /* BD */ { 2, 0, 1, 0 }, /* BE */ { 1, 0, 2, 0 },
    /* BF */ { 0, 1, 1, 0 }, /* C0 */ { 1, 0, 0, 1 }, /* C1 */ { 1, 0, 1, 1 }, /* C2 */ { 0, 1, 1, 1 },
    /* C3 */ { 1, 1, 0, 1 }, /* C4 */ { 0, 0, 1, 1 }, /* C5 */ { 1, 1, 1, 1 }, /* C6 */ { 1, 1, 0, 2 },
    /* C7 */ { 2, 2, 0, 1 }, /* C8 */ { 2, 0, 0, 2 }, /* C9 */ { 0, 2, 0, 2 }, /* CA */ { 2, 0, 2, 2 },
    /* CB */ { 0, 2, 2, 2 }, /* CC */ { 2, 2, 0, 2 }, /* CD */ { 0, 0, 2, 2 }, /* CE */ { 2, 2, 2, 2 },
    /* CF */ { 1, 0, 2, 2 }, /* D0 */ { 2, 0, 1, 1 }, /* D1 */ { 0, 1, 2, 2 }, /* D2 */ { 0, 2, 1, 1 },
    /* D3 */ { 2, 0, 0, 1 }, /* D4 */ { 1, 0, 0, 2 }, /* D5 */ { 0, 1, 0, 2 }, /* D6 */ { 0, 2, 0, 1 },
    /* D7 */ { 2, 2, 1, 1 }, /* D8 */ { 1, 1, 2, 2 }, /* D9 */ { 1, 0, 1, 0 }, /* DA */ { 0, 1, 0, 1 },
};

/* Rows whose pixel in column `col` is set, as a mask. */
static uint32_t
column_rows(const font_t *font, int c, int col)
{
    uint32_t mask = 0;
    int      r;

    for (r = 0; r < font->height; r++) {
        if (font->rows[c][r] & (0x80 >> col))
            mask |= 1u << r;
    }
    return mask;
}

static void
check_font(const font_t *font)
{
    int      c;
    int      r;
    int      half = font->height / 2;

    for (c = 0; c < 256; c++) {
        if (font->state[c] == UNDEFINED)
            die("%s: glyph %02Xh is missing; all 256 code points must be defined", font->path, c);
    }

    /* Letters, digits and punctuation keep column 7 clear, so characters stay apart in
       8-dot cells (graphics modes and 8-dot text). */
    for (c = 0x21; c <= 0x7e; c++) {
        if (c == 0x5f) /* underscore spans the cell */
            continue;
        for (r = 0; r < font->height; r++) {
            if (font->rows[c][r] & 0x01)
                font_error(font, font->line[c], c, "printable ASCII must leave column 7 clear");
        }
    }

    if (!font->have_lines)
        die("%s: missing 'lines' (box-drawing geometry)", font->path);

    for (c = 0xb3; c <= 0xda; c++) {
        const uint8_t *k    = box[c - 0xb3];
        uint8_t        vset[3] = { 0, font->v_single, font->v_double };
        uint32_t       hset[3] = { 0, font->h_single, font->h_double };

        if (font->rows[c][0] != vset[k[0]])
            font_error(font, font->line[c], c, "top edge does not match its up connection");
        if (font->rows[c][font->height - 1] != vset[k[1]])
            font_error(font, font->line[c], c, "bottom edge does not match its down connection");
        if (column_rows(font, c, 0) != hset[k[2]])
            font_error(font, font->line[c], c, "left edge does not match its left connection");
        if (column_rows(font, c, 7) != hset[k[3]])
            font_error(font, font->line[c], c, "right edge does not match its right connection");
    }

    for (r = 0; r < font->height; r++) {
        if (font->rows[0xdb][r] != 0xff)
            font_error(font, font->line[0xdb], 0xdb, "full block must be solid");
        if (font->rows[0xdc][r] != ((r >= half) ? 0xff : 0x00))
            font_error(font, font->line[0xdc], 0xdc, "lower half block must fill exactly the lower half");
        if (font->rows[0xdd][r] != 0xf0)
            font_error(font, font->line[0xdd], 0xdd, "left half block must fill columns 0-3");
        if (font->rows[0xde][r] != 0x0f)
            font_error(font, font->line[0xde], 0xde, "right half block must fill columns 4-7");
        if (font->rows[0xdf][r] != ((r < half) ? 0xff : 0x00))
            font_error(font, font->line[0xdf], 0xdf, "upper half block must fill exactly the upper half");
    }

    for (r = 0; r < font->height; r++) {
        if (font->rows[0x00][r] || font->rows[0x20][r] || font->rows[0xff][r])
            die("%s: glyphs 00h, 20h and FFh must be blank", font->path);
    }
}

/* ---------------------------------------------------------------------------
 * Output
 * ------------------------------------------------------------------------- */

static void
write_inc(const font_t *font, const char *path, const char *label, const char *source)
{
    buf_t out = { 0 };
    int   c;
    int   r;

    buf_printf(&out, "; Generated by fontc from %s. Do not edit.\n", source);
    buf_printf(&out, "; 256 glyphs, 8x%d, one byte per scan line, bit 7 = leftmost pixel.\n\n", font->height);
    buf_printf(&out, "%s:\n", label);
    for (c = 0; c < 256; c++) {
        buf_printf(&out, "    db ");
        for (r = 0; r < font->height; r++)
            buf_printf(&out, "0x%02X%s", font->rows[c][r], (r == font->height - 1) ? "" : ", ");
        buf_printf(&out, " ; %02Xh\n", c);
    }
    write_file(path, out.data, out.len);
    buf_free(&out);
}

static void
put_be32(buf_t *out, uint32_t v)
{
    uint8_t b[4] = { (uint8_t) (v >> 24), (uint8_t) (v >> 16), (uint8_t) (v >> 8), (uint8_t) v };

    buf_append(out, b, 4);
}

static uint32_t
crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    size_t i;
    int    k;

    crc = ~crc;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

static void
png_chunk(buf_t *out, const char *type, const uint8_t *data, size_t len)
{
    uint32_t crc = crc32_update(0, (const uint8_t *) type, 4);

    crc = crc32_update(crc, data, len);
    put_be32(out, (uint32_t) len);
    buf_append(out, type, 4);
    buf_append(out, data, len);
    put_be32(out, crc);
}

/*
 * Preview sheet: 16 x 16 glyphs, each pixel drawn 3 x 3, cells separated by gray
 * grid lines. Written as an 8-bit grayscale PNG with stored (uncompressed) deflate
 * blocks, so no compression library is needed.
 */
static void
write_png(const font_t *font, const char *path)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    const int            scale  = 3;
    const int            cell_w = (WIDTH + 1) * scale;
    const int            cell_h = (font->height + 1) * scale;
    const int            img_w  = 16 * cell_w + scale;
    const int            img_h  = 16 * cell_h + scale;
    size_t               raw_len = (size_t) (img_w + 1) * (size_t) img_h;
    uint8_t             *raw     = xcalloc(1, raw_len);
    buf_t                z       = { 0 };
    buf_t                out     = { 0 };
    uint8_t              ihdr[13];
    uint32_t             a = 1;
    uint32_t             b = 0;
    size_t               pos;
    size_t               i;
    int                  x;
    int                  y;

    for (y = 0; y < img_h; y++) {
        uint8_t *row = raw + (size_t) y * (size_t) (img_w + 1);

        row[0] = 0; /* filter: none */
        for (x = 0; x < img_w; x++) {
            uint8_t v = 0x80; /* grid */

            if ((x >= scale) && (y >= scale)) {
                int cx = (x - scale) / cell_w;
                int cy = (y - scale) / cell_h;
                int px = ((x - scale) % cell_w) / scale;
                int py = ((y - scale) % cell_h) / scale;

                if ((px < WIDTH) && (py < font->height))
                    v = (font->rows[cy * 16 + cx][py] & (0x80 >> px)) ? 0xff : 0x00;
            }
            row[1 + x] = v;
        }
    }

    /* zlib stream: header, stored deflate blocks, Adler-32. */
    buf_append(&z, "\x78\x01", 2);
    for (pos = 0; pos < raw_len;) {
        size_t  n = raw_len - pos;
        uint8_t hdr[5];

        if (n > 65535)
            n = 65535;
        hdr[0] = (pos + n == raw_len) ? 1 : 0;
        hdr[1] = (uint8_t) n;
        hdr[2] = (uint8_t) (n >> 8);
        hdr[3] = (uint8_t) ~n;
        hdr[4] = (uint8_t) (~n >> 8);
        buf_append(&z, hdr, 5);
        buf_append(&z, raw + pos, n);
        pos += n;
    }
    for (i = 0; i < raw_len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    put_be32(&z, (b << 16) | a);

    ihdr[0]  = (uint8_t) (img_w >> 24);
    ihdr[1]  = (uint8_t) (img_w >> 16);
    ihdr[2]  = (uint8_t) (img_w >> 8);
    ihdr[3]  = (uint8_t) img_w;
    ihdr[4]  = (uint8_t) (img_h >> 24);
    ihdr[5]  = (uint8_t) (img_h >> 16);
    ihdr[6]  = (uint8_t) (img_h >> 8);
    ihdr[7]  = (uint8_t) img_h;
    ihdr[8]  = 8; /* bit depth */
    ihdr[9]  = 0; /* grayscale */
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;

    buf_append(&out, sig, sizeof(sig));
    png_chunk(&out, "IHDR", ihdr, sizeof(ihdr));
    png_chunk(&out, "IDAT", (const uint8_t *) z.data, z.len);
    png_chunk(&out, "IEND", NULL, 0);
    write_file(path, out.data, out.len);

    buf_free(&z);
    buf_free(&out);
    free(raw);
}

static const char *
base_name(const char *path)
{
    const char *a = strrchr(path, '/');
    const char *b = strrchr(path, '\\');

    if ((b != NULL) && ((a == NULL) || (b > a)))
        a = b;
    return a ? a + 1 : path;
}

int
main(int argc, char **argv)
{
    font_t     *font  = xcalloc(1, sizeof(font_t));
    const char *in    = NULL;
    const char *inc   = NULL;
    const char *label = NULL;
    const char *png   = NULL;
    char        source[300];
    int         i;

    set_progname("fontc");
    for (i = 1; i < argc; i++) {
        if ((i + 1 < argc) && (strcmp(argv[i], "--inc") == 0))
            inc = argv[++i];
        else if ((i + 1 < argc) && (strcmp(argv[i], "--label") == 0))
            label = argv[++i];
        else if ((i + 1 < argc) && (strcmp(argv[i], "--png") == 0))
            png = argv[++i];
        else if ((argv[i][0] != '-') && (in == NULL))
            in = argv[i];
        else
            die("usage: fontc FONT --inc FILE --label NAME [--png FILE]");
    }
    if (!in || !inc || !label)
        die("usage: fontc FONT --inc FILE --label NAME [--png FILE]");

    load_font(font, in);
    check_font(font);

    /* Record only the file name, so outputs do not depend on where the tree is checked out. */
    snprintf(source, sizeof(source), "fonts/%s", base_name(in));
    write_inc(font, inc, label, source);
    if (png != NULL)
        write_png(font, png);
    return 0;
}

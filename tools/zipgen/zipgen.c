/*
 * zipgen: writes a zip archive of the given files, for the release download.
 *
 *   zipgen -o ZIP -t SECONDS|@STAMPFILE NAME=FILE...
 *
 * NAME is the file's path inside the archive (forward slashes), FILE where it
 * is read from. The archive depends only on the inputs: entries go in the
 * order given, every date is the given time (UTC; @STAMPFILE takes it from
 * the second word of the build stamp file), and nothing else varies: no
 * extra fields, comments or attributes. Each file is deflated (zlib, level 9)
 * unless that would not make it smaller, then stored. Before the archive is
 * written, it is read back through its end record and central directory,
 * and every entry inflated and compared with its source.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include "util.h"

#define MAX_FILES   1024
#define SIG_LOCAL   0x04034b50u
#define SIG_CENTRAL 0x02014b50u
#define SIG_END     0x06054b50u
#define VERSION     20 /* 2.0: deflate; made by MS-DOS (0) */
#define STORED      0
#define DEFLATED    8

typedef struct entry_t {
    const char *name;
    const char *path;
    uint32_t    crc, csize, usize, offset;
    uint16_t    method;
} entry_t;

static entry_t  entries[MAX_FILES];
static int      count;
static uint16_t dos_time, dos_date;

static void
put16(buf_t *b, uint32_t v)
{
    uint8_t x[2] = { (uint8_t) v, (uint8_t) (v >> 8) };

    buf_append(b, x, 2);
}

static void
put32(buf_t *b, uint32_t v)
{
    uint8_t x[4] = { (uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16), (uint8_t) (v >> 24) };

    buf_append(b, x, 4);
}

static uint32_t
get16(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8);
}

static uint32_t
get32(const uint8_t *p)
{
    return get16(p) | (get16(p + 2) << 16);
}

/* The time as an MS-DOS date and time (UTC), whose epoch is 1980. */
static void
set_time(long long seconds)
{
    long long days = seconds / 86400, rem = seconds % 86400;
    long long z, era, doe, yoe, y, doy, mp, d, m;

    if (seconds < 315532800) { /* before 1980-01-01 */
        dos_date = (1 << 5) | 1;
        dos_time = 0;
        return;
    }
    /* Civil date from days since 1970-01-01 (proleptic Gregorian). */
    z   = days + 719468;
    era = z / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y   = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp  = (5 * doy + 2) / 153;
    d   = doy - (153 * mp + 2) / 5 + 1;
    m   = mp < 10 ? mp + 3 : mp - 9;
    y += m <= 2;
    if (y > 2107)
        die("the time %lld is past what a zip date can hold", seconds);
    dos_date = (uint16_t) (((y - 1980) << 9) | (m << 5) | d);
    dos_time = (uint16_t) (((rem / 3600) << 11) | (((rem / 60) % 60) << 5) | ((rem % 60) / 2));
}

static long long
parse_time(const char *arg)
{
    char     *end;
    long long t;

    if (arg[0] == '@') {
        size_t   size;
        uint8_t *stamp = read_file(arg + 1, &size);
        char    *p     = strchr((char *) stamp, ' ');

        if (!p)
            die("%s: no time after the stamp", arg + 1);
        t = strtoll(p + 1, &end, 10);
        if (end == p + 1)
            die("%s: no time after the stamp", arg + 1);
        free(stamp);
        return t;
    }
    t = strtoll(arg, &end, 10);
    if (!*arg || *end || t < 0)
        die("bad time '%s'", arg);
    return t;
}

/* A name a zip holds on every system: printable ASCII, forward slashes,
   relative, no empty, "." or ".." component. */
static void
check_name(const char *name)
{
    const char *p, *c = name;
    size_t      len = strlen(name);

    if (!len || len > 255 || name[0] == '/' || name[len - 1] == '/')
        die("bad name '%s'", name);
    for (p = name; *p; p++) {
        if (*p < 0x21 && *p != ' ')
            die("bad name '%s': not printable", name);
        if (*p > 0x7e || *p == '\\' || *p == ':')
            die("bad name '%s': ASCII and forward slashes only", name);
    }
    for (p = name;; p++) {
        if (*p == '/' || !*p) {
            size_t n = (size_t) (p - c);

            if (!n || (n == 1 && c[0] == '.') || (n == 2 && c[0] == '.' && c[1] == '.'))
                die("bad name '%s': empty, '.' or '..' part", name);
            if (!*p)
                break;
            c = p + 1;
        }
    }
}

/* Deflates data into out (raw, no zlib header); returns its size, or 0 when
   deflating would not make it smaller. */
static size_t
deflate_data(const uint8_t *data, size_t size, uint8_t **out)
{
    z_stream s;
    size_t   bound;

    memset(&s, 0, sizeof(s));
    if (deflateInit2(&s, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        die("zlib: deflateInit2 failed");
    bound = deflateBound(&s, (uLong) size);
    *out  = xmalloc(bound ? bound : 1);
    s.next_in   = (Bytef *) data;
    s.avail_in  = (uInt) size;
    s.next_out  = *out;
    s.avail_out = (uInt) bound;
    if (deflate(&s, Z_FINISH) != Z_STREAM_END)
        die("zlib: deflate failed");
    deflateEnd(&s);
    if (s.total_out >= size) {
        free(*out);
        *out = NULL;
        return 0;
    }
    return s.total_out;
}

static void
add_entry(buf_t *zip, entry_t *e)
{
    size_t   size, csize;
    uint8_t *data = read_file(e->path, &size), *packed = NULL;

    if (size > 0xfffffffeu || zip->len > 0xfffffffeu - size)
        die("%s: too large for a zip without zip64", e->path);
    csize     = deflate_data(data, size, &packed);
    e->crc    = (uint32_t) crc32(0L, data, (uInt) size);
    e->usize  = (uint32_t) size;
    e->method = csize ? DEFLATED : STORED;
    e->csize  = csize ? (uint32_t) csize : (uint32_t) size;
    e->offset = (uint32_t) zip->len;

    put32(zip, SIG_LOCAL);
    put16(zip, VERSION);
    put16(zip, 0); /* flags */
    put16(zip, e->method);
    put16(zip, dos_time);
    put16(zip, dos_date);
    put32(zip, e->crc);
    put32(zip, e->csize);
    put32(zip, e->usize);
    put16(zip, (uint32_t) strlen(e->name));
    put16(zip, 0); /* extra */
    buf_append(zip, e->name, strlen(e->name));
    buf_append(zip, csize ? packed : data, e->csize);
    free(packed);
    free(data);
}

static void
add_central(buf_t *zip)
{
    uint32_t start = (uint32_t) zip->len, size;
    int      i;

    for (i = 0; i < count; i++) {
        const entry_t *e = &entries[i];

        put32(zip, SIG_CENTRAL);
        put16(zip, VERSION); /* made by */
        put16(zip, VERSION); /* needed */
        put16(zip, 0);
        put16(zip, e->method);
        put16(zip, dos_time);
        put16(zip, dos_date);
        put32(zip, e->crc);
        put32(zip, e->csize);
        put32(zip, e->usize);
        put16(zip, (uint32_t) strlen(e->name));
        put16(zip, 0); /* extra */
        put16(zip, 0); /* comment */
        put16(zip, 0); /* disk */
        put16(zip, 0); /* internal attributes */
        put32(zip, 0); /* external attributes */
        put32(zip, e->offset);
        buf_append(zip, e->name, strlen(e->name));
    }
    size = (uint32_t) zip->len - start;
    put32(zip, SIG_END);
    put16(zip, 0);
    put16(zip, 0);
    put16(zip, (uint32_t) count);
    put16(zip, (uint32_t) count);
    put32(zip, size);
    put32(zip, start);
    put16(zip, 0); /* comment */
}

/* Reads the archive back as an unzipper would: the end record, the central
   directory, each local header, and each entry inflated and compared. */
static void
check_zip(const uint8_t *z, size_t len)
{
    const uint8_t *end, *cd;
    uint32_t       n, cd_size, cd_start, i;

    if (len < 22 || get32(z + len - 22) != SIG_END)
        die("read-back: no end record");
    end      = z + len - 22;
    n        = get16(end + 10);
    cd_size  = get32(end + 12);
    cd_start = get32(end + 16);
    if (n != (uint32_t) count || get16(end + 8) != n || (size_t) cd_start + cd_size != len - 22)
        die("read-back: the end record does not match");
    cd = z + cd_start;
    for (i = 0; i < n; i++) {
        const entry_t *e = &entries[i];
        uint32_t       nlen, off, method, csize, usize, crc;
        const uint8_t *lh, *data;
        uint8_t       *out, *src;
        size_t         src_size;

        if ((size_t) (cd - z) + 46 > len || get32(cd) != SIG_CENTRAL)
            die("read-back: central entry %u is bad", (unsigned) i);
        method = get16(cd + 10);
        crc    = get32(cd + 16);
        csize  = get32(cd + 20);
        usize  = get32(cd + 24);
        nlen   = get16(cd + 28);
        off    = get32(cd + 42);
        if (nlen != strlen(e->name) || memcmp(cd + 46, e->name, nlen))
            die("read-back: entry %u is not %s", (unsigned) i, e->name);
        lh = z + off;
        if ((size_t) off + 30 > cd_start || get32(lh) != SIG_LOCAL || get16(lh + 8) != method ||
            get32(lh + 14) != crc || get32(lh + 18) != csize || get32(lh + 22) != usize ||
            get16(lh + 26) != nlen || get16(lh + 28) != 0 || memcmp(lh + 30, e->name, nlen))
            die("read-back: %s's local header does not match", e->name);
        data = lh + 30 + nlen;
        if ((size_t) (data - z) + csize > cd_start)
            die("read-back: %s runs past the central directory", e->name);
        out = xmalloc(usize ? usize : 1);
        if (method == STORED) {
            if (csize != usize)
                die("read-back: %s stored with two sizes", e->name);
            memcpy(out, data, usize);
        } else {
            z_stream s;

            memset(&s, 0, sizeof(s));
            if (method != DEFLATED || inflateInit2(&s, -MAX_WBITS) != Z_OK)
                die("read-back: %s: bad method", e->name);
            s.next_in   = (Bytef *) data;
            s.avail_in  = csize;
            s.next_out  = out;
            s.avail_out = usize;
            if (inflate(&s, Z_FINISH) != Z_STREAM_END || s.total_out != usize || s.total_in != csize)
                die("read-back: %s does not inflate to its size", e->name);
            inflateEnd(&s);
        }
        src = read_file(e->path, &src_size);
        if (src_size != usize || memcmp(src, out, usize) || (uint32_t) crc32(0L, out, usize) != crc)
            die("read-back: %s differs from %s", e->name, e->path);
        free(src);
        free(out);
        cd += 46 + nlen;
    }
}

int
main(int argc, char **argv)
{
    const char *out = NULL, *time_arg = NULL;
    buf_t       zip = { 0 };
    int         i, j;

    set_progname("zipgen");
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc)
            out = argv[++i];
        else if (!strcmp(argv[i], "-t") && i + 1 < argc)
            time_arg = argv[++i];
        else if (argv[i][0] == '-')
            die("unknown option %s", argv[i]);
        else {
            char *eq = strchr(argv[i], '=');

            if (!eq)
                die("'%s' is not NAME=FILE", argv[i]);
            if (count == MAX_FILES)
                die("more than %d files", MAX_FILES);
            *eq = 0;
            check_name(argv[i]);
            for (j = 0; j < count; j++)
                if (!strcmp(entries[j].name, argv[i]))
                    die("%s given twice", argv[i]);
            entries[count].name   = argv[i];
            entries[count++].path = eq + 1;
        }
    }
    if (!out || !time_arg || !count)
        die("usage: zipgen -o ZIP -t SECONDS|@STAMPFILE NAME=FILE...");
    set_time(parse_time(time_arg));

    for (i = 0; i < count; i++)
        add_entry(&zip, &entries[i]);
    add_central(&zip);
    check_zip((const uint8_t *) zip.data, zip.len);
    write_file(out, zip.data, zip.len);
    buf_free(&zip);
    return 0;
}

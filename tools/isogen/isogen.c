/*
 * isogen: writes a CD image (ISO 9660, ECMA-119, level 1) holding the given
 * files in its root directory, for installing the driver from a CD.
 *
 *   isogen -o IMAGE -l LABEL -t SECONDS|@STAMPFILE FILE...
 *
 * The image depends only on the inputs: every date is the given time (UTC;
 * @STAMPFILE takes it from the second word of the build stamp file), files
 * are stored contiguously in the directory's order, and 150 blank sectors
 * follow the last one, as CD mastering tools leave them, so no file sits in
 * the last sectors of the disc (operating systems read ahead past the end of
 * the data). Before the image is written, it is read back through its own
 * volume descriptor, path tables and directory, and every file compared with
 * its source. Layout: spec/driver-package.md.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define SECTOR       2048
#define PVD_SECTOR   16
#define TERM_SECTOR  17
#define LPATH_SECTOR 18
#define MPATH_SECTOR 19
#define ROOT_SECTOR  20
#define FIRST_DATA   21
#define PAD_SECTORS  150
#define MAX_FILES    64

typedef struct file_t {
    const char *path;
    char        id[16]; /* "NAME.EXT;1" */
    char        name[9];
    char        ext[4];
    uint8_t    *data;
    size_t      size;
    uint32_t    extent;
} file_t;

static file_t files[MAX_FILES];
static int    n_files;

/* --- Fields -------------------------------------------------------------- */

static void
le16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

static void
be16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 8);
    p[1] = (uint8_t) v;
}

static void
le32(uint8_t *p, uint32_t v)
{
    le16(p, v & 0xffff);
    le16(p + 2, v >> 16);
}

static void
be32(uint8_t *p, uint32_t v)
{
    be16(p, v >> 16);
    be16(p + 2, v & 0xffff);
}

/* Both-byte orders (ECMA-119 7.2.3, 7.3.3): little-endian, then big-endian. */
static void
both16(uint8_t *p, uint32_t v)
{
    le16(p, v);
    be16(p + 2, v);
}

static void
both32(uint8_t *p, uint32_t v)
{
    le32(p, v);
    be32(p + 4, v);
}

static uint32_t
get_le32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24);
}

static uint32_t
get_be32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

/* A space-padded character field. */
static void
text(uint8_t *p, size_t n, const char *s)
{
    size_t len = strlen(s);

    memset(p, ' ', n);
    memcpy(p, s, (len < n) ? len : n);
}

/* --- Dates ----------------------------------------------------------------- */

typedef struct date_t {
    unsigned year, month, day, hour, minute, second;
} date_t;

/* Seconds since 1970 (UTC) to a calendar date, without the C library's time
   zone handling. */
static date_t
date_from_seconds(unsigned long long t)
{
    date_t             d;
    unsigned long long days = t / 86400;
    unsigned long      rem  = (unsigned long) (t % 86400);
    long long          z    = (long long) days + 719468;
    long long          era  = z / 146097;
    unsigned           doe  = (unsigned) (z - era * 146097);
    unsigned           yoe  = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    unsigned           doy  = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned           mp   = (5 * doy + 2) / 153;

    d.day    = doy - (153 * mp + 2) / 5 + 1;
    d.month  = (mp < 10) ? mp + 3 : mp - 9;
    d.year   = (unsigned) (yoe + era * 400) + (d.month <= 2);
    d.hour   = (unsigned) (rem / 3600);
    d.minute = (unsigned) (rem / 60 % 60);
    d.second = (unsigned) (rem % 60);
    return d;
}

/* A volume descriptor's date (ECMA-119 8.4.26.1): 16 digits and the offset
   from UTC in 15-minute steps. */
static void
vd_date(uint8_t *p, const date_t *d)
{
    char s[48];

    snprintf(s, sizeof(s), "%04u%02u%02u%02u%02u%02u00", d->year % 10000, d->month % 100, d->day % 100, d->hour % 100,
             d->minute % 100, d->second % 100);
    memcpy(p, s, 16);
    p[16] = 0;
}

/* A directory record's date (9.1.5): years since 1900, month, day, hour,
   minute, second, offset from UTC. */
static void
dr_date(uint8_t *p, const date_t *d)
{
    p[0] = (uint8_t) (d->year - 1900);
    p[1] = (uint8_t) d->month;
    p[2] = (uint8_t) d->day;
    p[3] = (uint8_t) d->hour;
    p[4] = (uint8_t) d->minute;
    p[5] = (uint8_t) d->second;
    p[6] = 0;
}

/* --- Names ----------------------------------------------------------------- */

static int
d_char(int c)
{
    return isupper(c) || isdigit(c) || (c == '_');
}

/* A level 1 file identifier from an 8.3 file name, in upper case. */
static void
iso_name(file_t *f)
{
    const char *base = f->path;
    const char *p;
    const char *dot;
    size_t      n, i;

    for (p = f->path; *p; p++) {
        if ((*p == '/') || (*p == '\\'))
            base = p + 1;
    }
    dot = strrchr(base, '.');
    n   = dot ? (size_t) (dot - base) : strlen(base);
    if ((n == 0) || (n > 8) || (dot && ((strlen(dot + 1) == 0) || (strlen(dot + 1) > 3))))
        die("%s: not an 8.3 file name", base);
    for (i = 0; i < n; i++)
        f->name[i] = (char) toupper((unsigned char) base[i]);
    f->name[n] = 0;
    f->ext[0]  = 0;
    if (dot) {
        for (i = 0; dot[1 + i]; i++)
            f->ext[i] = (char) toupper((unsigned char) dot[1 + i]);
        f->ext[i] = 0;
    }
    for (i = 0; f->name[i]; i++) {
        if (!d_char((unsigned char) f->name[i]))
            die("%s: '%c' is not allowed in an ISO 9660 file name", base, f->name[i]);
    }
    for (i = 0; f->ext[i]; i++) {
        if (!d_char((unsigned char) f->ext[i]))
            die("%s: '%c' is not allowed in an ISO 9660 file name", base, f->ext[i]);
    }
    {
        char id[sizeof(f->id)];

        snprintf(id, sizeof(id), "%s.%s;1", f->name, f->ext);
        memcpy(f->id, id, sizeof(id));
    }
}

/* Directory order (9.3): by name, then extension, the shorter padded with
   spaces. */
static int
file_cmp(const void *pa, const void *pb)
{
    const file_t *a = pa;
    const file_t *b = pb;
    char          na[9], nb[9], ea[4], eb[4];
    int           r;

    snprintf(na, sizeof(na), "%-8s", a->name);
    snprintf(nb, sizeof(nb), "%-8s", b->name);
    snprintf(ea, sizeof(ea), "%-3s", a->ext);
    snprintf(eb, sizeof(eb), "%-3s", b->ext);
    if ((r = strcmp(na, nb)))
        return r;
    return strcmp(ea, eb);
}

/* --- Records --------------------------------------------------------------- */

/* A directory record (9.1). Returns its length. */
static size_t
dir_record(uint8_t *p, uint32_t extent, uint32_t size, int is_dir, const uint8_t *id, size_t id_len, const date_t *d)
{
    const size_t len = 33 + id_len + ((id_len & 1) ? 0 : 1);

    memset(p, 0, len);
    p[0] = (uint8_t) len;
    p[1] = 0; /* no extended attribute record */
    both32(p + 2, extent);
    both32(p + 10, size);
    dr_date(p + 18, d);
    p[25] = is_dir ? 0x02 : 0x00;
    p[26] = 0; /* not interleaved */
    p[27] = 0;
    both16(p + 28, 1); /* volume sequence number */
    p[32] = (uint8_t) id_len;
    memcpy(p + 33, id, id_len);
    return len;
}

/* --- Reading the image back ------------------------------------------------ */

static void
verify(const uint8_t *img, uint32_t sectors)
{
    const uint8_t *pvd = img + PVD_SECTOR * SECTOR;
    const uint8_t *root;
    uint32_t       root_extent, root_size, lpath, mpath, at;
    int            i;

    if ((pvd[0] != 1) || memcmp(pvd + 1, "CD001", 5) || (pvd[6] != 1))
        die("read back: no primary volume descriptor");
    if (get_le32(pvd + 80) != sectors || get_be32(pvd + 84) != sectors)
        die("read back: the volume size is %u sectors, not %u", get_le32(pvd + 80), sectors);
    if ((img[TERM_SECTOR * SECTOR] != 255) || memcmp(img + TERM_SECTOR * SECTOR + 1, "CD001", 5))
        die("read back: no volume descriptor set terminator");
    root        = pvd + 156;
    root_extent = get_le32(root + 2);
    root_size   = get_le32(root + 10);
    if ((root[0] != 34) || !(root[25] & 0x02) || (root_extent != get_be32(root + 6)))
        die("read back: the root directory record is wrong");
    lpath = get_le32(pvd + 140);
    mpath = get_be32(pvd + 148);
    if ((get_le32(img + lpath * SECTOR + 2) != root_extent) || (get_be32(img + mpath * SECTOR + 2) != root_extent))
        die("read back: the path tables do not lead to the root directory");

    for (i = 0; i < n_files; i++) {
        const file_t *f     = &files[i];
        int           found = 0;

        for (at = 0; at < root_size;) {
            const uint8_t *r = img + root_extent * SECTOR + at;
            uint32_t       extent, size;

            if (!r[0])
                break;
            if ((r[32] == strlen(f->id)) && !memcmp(r + 33, f->id, r[32])) {
                extent = get_le32(r + 2);
                size   = get_le32(r + 10);
                if ((size != f->size) || (extent != get_be32(r + 6)) || (size != get_be32(r + 14)))
                    die("read back: %s: wrong extent or size", f->id);
                if (size && ((uint64_t) extent * SECTOR + size > (uint64_t) (sectors - PAD_SECTORS) * SECTOR))
                    die("read back: %s reaches into the padding", f->id);
                if (memcmp(img + (size_t) extent * SECTOR, f->data, size))
                    die("read back: %s differs from its source", f->id);
                found = 1;
                break;
            }
            at += r[0];
        }
        if (!found)
            die("read back: %s is not in the root directory", f->id);
    }
    for (at = (sectors - PAD_SECTORS) * SECTOR; at < sectors * SECTOR; at++) {
        if (img[at])
            die("read back: the padding is not blank");
    }
}

/* --- Main -------------------------------------------------------------------- */

static unsigned long long
parse_time(const char *arg)
{
    unsigned long long t = 0;
    const char        *s = arg;
    char               word[64];
    char               tail;
    uint8_t           *stamp = NULL;

    if (arg[0] == '@') {
        size_t len;

        stamp = read_file(arg + 1, &len);
        if (sscanf((const char *) stamp, "%*s %63s", word) != 1)
            die("%s: no time after the build stamp", arg + 1);
        s = word;
    }
    if ((sscanf(s, "%llu%c", &t, &tail) != 1) || (t > 4102444799ull)) /* until 2099 */
        die("the time is seconds since 1970 (UTC), or @FILE");
    free(stamp);
    return t;
}

int
main(int argc, char **argv)
{
    const char        *out_path = NULL;
    const char        *label    = NULL;
    const char        *when     = NULL;
    unsigned long long seconds;
    date_t             d;
    uint8_t           *img;
    uint8_t           *pvd;
    uint8_t           *dir;
    uint32_t           next = FIRST_DATA;
    uint32_t           sectors;
    size_t             at;
    size_t             k;
    int                i;

    set_progname("isogen");
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && (i + 1 < argc))
            out_path = argv[++i];
        else if (!strcmp(argv[i], "-l") && (i + 1 < argc))
            label = argv[++i];
        else if (!strcmp(argv[i], "-t") && (i + 1 < argc))
            when = argv[++i];
        else if (argv[i][0] == '-')
            die("usage: isogen -o IMAGE -l LABEL -t SECONDS|@STAMPFILE FILE...");
        else
            break;
    }
    if (!out_path || !label || !when || (i >= argc))
        die("usage: isogen -o IMAGE -l LABEL -t SECONDS|@STAMPFILE FILE...");
    if ((strlen(label) == 0) || (strlen(label) > 32))
        die("the volume label has 1 to 32 characters");
    for (k = 0; label[k]; k++) {
        if (!d_char((unsigned char) label[k]))
            die("the volume label takes upper-case letters, digits and '_'");
    }
    seconds = parse_time(when);
    d       = date_from_seconds(seconds);

    for (; i < argc; i++) {
        file_t *f = &files[n_files];
        int     j;

        if (n_files >= MAX_FILES)
            die("too many files");
        f->path = argv[i];
        iso_name(f);
        for (j = 0; j < n_files; j++) {
            if (!strcmp(files[j].id, f->id))
                die("%s: two files with the same ISO 9660 name", argv[i]);
        }
        f->data = read_file(argv[i], &f->size);
        if ((uint64_t) f->size > 0xffffffffull)
            die("%s: too large", argv[i]);
        n_files++;
    }
    qsort(files, (size_t) n_files, sizeof(files[0]), file_cmp);

    /* Extents, in directory order; a file of no bytes takes no sector. */
    for (i = 0; i < n_files; i++) {
        files[i].extent = files[i].size ? next : 0;
        next += (uint32_t) ((files[i].size + SECTOR - 1) / SECTOR);
    }
    sectors = next + PAD_SECTORS;
    img     = xcalloc(sectors, SECTOR);

    /* The root directory: ".", "..", the files, in one sector. */
    dir = img + ROOT_SECTOR * SECTOR;
    at  = dir_record(dir, ROOT_SECTOR, SECTOR, 1, (const uint8_t *) "\0", 1, &d);
    at += dir_record(dir + at, ROOT_SECTOR, SECTOR, 1, (const uint8_t *) "\1", 1, &d);
    for (i = 0; i < n_files; i++) {
        if (at + 33 + strlen(files[i].id) + 1 > SECTOR)
            die("too many files for one directory sector");
        at += dir_record(dir + at, files[i].extent, (uint32_t) files[i].size, 0, (const uint8_t *) files[i].id,
                         strlen(files[i].id), &d);
        memcpy(img + (size_t) files[i].extent * SECTOR, files[i].data, files[i].size);
    }

    /* Path tables (9.4): the root alone, in each byte order. */
    img[LPATH_SECTOR * SECTOR + 0] = 1;
    le32(img + LPATH_SECTOR * SECTOR + 2, ROOT_SECTOR);
    le16(img + LPATH_SECTOR * SECTOR + 6, 1);
    img[MPATH_SECTOR * SECTOR + 0] = 1;
    be32(img + MPATH_SECTOR * SECTOR + 2, ROOT_SECTOR);
    be16(img + MPATH_SECTOR * SECTOR + 6, 1);

    /* The primary volume descriptor (8.4). */
    pvd    = img + PVD_SECTOR * SECTOR;
    pvd[0] = 1;
    memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    text(pvd + 8, 32, "");
    text(pvd + 40, 32, label);
    both32(pvd + 80, sectors);
    both16(pvd + 120, 1);      /* volume set size */
    both16(pvd + 124, 1);      /* volume sequence number */
    both16(pvd + 128, SECTOR); /* logical block size */
    both32(pvd + 132, 10);     /* path table size: one 10-byte record */
    le32(pvd + 140, LPATH_SECTOR);
    be32(pvd + 148, MPATH_SECTOR);
    dir_record(pvd + 156, ROOT_SECTOR, SECTOR, 1, (const uint8_t *) "\0", 1, &d);
    text(pvd + 190, 128, "");
    text(pvd + 318, 128, "VGPU98 PROJECT");
    text(pvd + 446, 128, "");
    text(pvd + 574, 128, "ISOGEN");
    text(pvd + 702, 37, "");
    text(pvd + 739, 37, "");
    text(pvd + 776, 37, "");
    vd_date(pvd + 813, &d);
    vd_date(pvd + 830, &d);
    memcpy(pvd + 847, "0000000000000000", 16); /* no expiration date */
    memcpy(pvd + 864, "0000000000000000", 16); /* no effective date */
    pvd[881] = 1;                              /* file structure version */

    /* The volume descriptor set terminator (8.3). */
    img[TERM_SECTOR * SECTOR] = 255;
    memcpy(img + TERM_SECTOR * SECTOR + 1, "CD001", 5);
    img[TERM_SECTOR * SECTOR + 6] = 1;

    verify(img, sectors);
    write_file(out_path, img, (size_t) sectors * SECTOR);
    for (i = 0; i < n_files; i++)
        free(files[i].data);
    free(img);
    return 0;
}
